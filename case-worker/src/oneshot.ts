/**
 * One-shot batch worker. The Qt app launches this per phase via QProcess:
 * a JSON job is fed on stdin, one JSON result object is printed to stdout, then
 * the process exits. No server, no port. All progress/diagnostics go to stderr
 * so stdout stays pure JSON.
 *
 * Jobs:
 *   { "action": "scrape", "cases": [ {"region","caseId"} ] }
 *       → { "results": [ {region, caseId, ok, thread?, error?, sessionExpired?} ] }
 *   { "action": "reply",  "cases": [ {"region","caseId","text"} ] }
 *       → { "results": [ {region, caseId, ok, error?, sessionExpired?} ] }
 *   { "action": "login",  "region": "eu" }
 *       → { "ok": <loggedIn>, "region": "eu" }
 *   { "action": "gspr", "subaction": "snapshot", "dumpDir": "/tmp/gspr",
 *     "marketplaces": [ {"country":"DE","countryName":"Germany"} ] }
 *       → { "results": [ {country, ok, url?, dumpDir?, error?, sessionExpired?} ] }
 *
 * One browser context is opened per region within a single invocation and
 * reused for every case in that region, then closed.
 */
import type { Page } from "playwright";
import { loadConfig, caseListUrl } from "./config.js";
import { launchContext, firstPage } from "./browser.js";
import { join } from "node:path";
import { getThread, submitReply, isLoggedIn, passAccountSwitcher, SessionExpiredError } from "./casePage.js";
import { timestampSlug } from "./browser.js";
import { openComplianceFor, snapshotCompliance, processSafetyWarnings,
         processResponsiblePerson, processManufacturer,
         type GsprMarketplace, type GsprSnapshotResult,
         type ManufacturerEntry, type AskReply } from "./gsprPage.js";
import type { Config, Region } from "./types.js";

interface CaseJob { region?: Region; caseId: string; text?: string; account?: string; files?: string[]; }
interface Job {
  action: "scrape" | "reply" | "login" | "gspr";
  cases?: CaseJob[];
  region?: Region;
  manualSend?: boolean;
  // gspr-only:
  subaction?: string;
  marketplaces?: GsprMarketplace[];
  dumpDir?: string;
  ecRepPattern?: string; // Responsible Person choice prefix
  userSkipAsins?: string[]; // products excluded by the user — skip everywhere
  manualManufacturerSave?: boolean; // pause for the user to Save new manufacturers
  manufacturers?: ManufacturerEntry[]; // SKU-prefix → manufacturer contact map
}

// Line-based stdin: the first line is the job; later lines are interactive
// replies (gspr pauses on @@gspr-ask and waits for one). Callers that close
// stdin right after the job (scrape/reply/login) still work — only the first
// line is consumed for them.
const stdinLines: string[] = [];
const stdinWaiters: Array<(line: string) => void> = [];
{
  let buf = "";
  process.stdin.on("data", (chunk: Buffer) => {
    buf += chunk.toString("utf8");
    let nl;
    while ((nl = buf.indexOf("\n")) >= 0) {
      const line = buf.slice(0, nl).trim();
      buf = buf.slice(nl + 1);
      if (!line) continue;
      const w = stdinWaiters.shift();
      if (w) w(line); else stdinLines.push(line);
    }
  });
}
function readStdinLine(): Promise<string> {
  const queued = stdinLines.shift();
  if (queued !== undefined) return Promise.resolve(queued);
  return new Promise((resolve) => stdinWaiters.push(resolve));
}

function log(msg: string) { process.stderr.write(msg + "\n"); }

// Land on the region's case lobby and, if not signed in, PAUSE there polling
// until the user logs in (in the same window) — up to ~10 min. Returns whether
// we ended up logged in. This runs once per region, before any case is opened,
// so we never flip through case pages while signed out.
async function ensureLoggedIn(page: Page, cfg: Config, region: Region): Promise<boolean> {
  await page.goto(caseListUrl(cfg, region), { waitUntil: "domcontentloaded" });
  await passAccountSwitcher(page, cfg, region);
  if (await isLoggedIn(page, cfg)) return true;
  log(`[${region}] NOT LOGGED IN — waiting for you to log in via the browser window…`);
  for (let i = 0; i < 300; i++) {          // ~10 min at 2s intervals
    await page.waitForTimeout(2000);
    await passAccountSwitcher(page, cfg, region); // clear the switcher if it shows
    if (await isLoggedIn(page, cfg)) {
      log(`[${region}] logged in — continuing.`);
      return true;
    }
  }
  log(`[${region}] login wait timed out.`);
  return false;
}

async function main() {
  const cfg = loadConfig();
  const job = JSON.parse(await readStdinLine()) as Job;
  // Qt keeps our stdin open for interactive replies (gspr). An active stdin
  // keeps the Node event loop alive forever, so unref it: replies still
  // arrive while the browser keeps the process running, and once all work is
  // done the process can exit instead of hanging.
  process.stdin.unref();
  const now = Date.now();

  // -- login -------------------------------------------------------------
  if (job.action === "login") {
    const region = (job.region ?? cfg.defaultRegion) as Region;
    const ctx = await launchContext(cfg, region);
    try {
      const page = await firstPage(ctx);
      await page.goto(caseListUrl(cfg, region), { waitUntil: "domcontentloaded" });
      log(`[${region}] log in via the browser window…`);
      let loggedIn = false;
      for (let i = 0; i < 150; i++) {            // ~5 min at 2s intervals
        if (await isLoggedIn(page, cfg)) { loggedIn = true; break; }
        await page.waitForTimeout(2000);
      }
      process.stdout.write(JSON.stringify({ ok: loggedIn, region }) + "\n");
    } finally {
      await ctx.close().catch(() => {});
    }
    return;
  }

  // -- gspr: compliance page, one EU marketplace at a time ----------------
  if (job.action === "gspr") {
    const region: Region = "eu"; // GSPR is EU-only; all marketplaces share the EU session
    const rc = cfg.regions[region];
    const dumpDir = job.dumpDir ?? "/tmp/gspr";
    const marketplaces = job.marketplaces ?? [];
    const results: unknown[] = [];

    // Shared across marketplaces; refreshed in place when the user completes
    // the xlsx files after a @@gspr-ask pause.
    const gsprManufacturers: ManufacturerEntry[] = job.manufacturers ?? [];
    // User-excluded products: skipped in every phase and every country.
    const userSkip = new Set<string>(job.userSkipAsins ?? []);
    const askUser = async (payload: Record<string, unknown>): Promise<AskReply> => {
      log(`@@gspr-ask ${JSON.stringify(payload)}`);
      const line = await readStdinLine(); // Qt answers via the worker's stdin
      try { return JSON.parse(line) as AskReply; } catch { return { cmd: "stop" }; }
    };

    // Deliberate user action (closing the Chromium window) — STOP, no retry.
    const CLOSED_RE = /has been closed|Target (page|browser).*closed|browser has been closed/i;
    // Chromium renderer died on its own (OOM, GPU fault, etc.) — RECOVERABLE:
    // relaunch a fresh context/page and resume the SAME marketplace. Amazon's
    // own row status (already-submitted rows read back as "under review")
    // makes the retry naturally idempotent — nothing gets double-submitted.
    const CRASHED_RE = /crashed/i;
    const MAX_RELAUNCHES = 3; // per marketplace — a fresh country starts with a clean budget
    let relaunches = 0;
    let relaunchBudgetIndex = -1; // marketplace index the counter above applies to

    let ctx = await launchContext(cfg, region);
    let page = await firstPage(ctx);
    try {
      if (!(await ensureLoggedIn(page, cfg, region))) {
        for (const mp of marketplaces)
          results.push({ country: mp.country, ok: false,
                         error: "not logged in (login wait timed out)", sessionExpired: true });
      } else {
        let i = 0;
        while (i < marketplaces.length) {
          const mp = marketplaces[i]!;
          if (i !== relaunchBudgetIndex) { relaunches = 0; relaunchBudgetIndex = i; }
          const mpDumpDir = join(dumpDir, timestampSlug(now), mp.country);
          try {
            log(`[gspr:${mp.country}] opening compliance page…`);
            const selected = await openComplianceFor(page, rc.domain, mp, mpDumpDir);
            if (!(await isLoggedIn(page, cfg)))
              throw new SessionExpiredError(region);

            // Only act on the page once we're sure the right marketplace is up.
            const onTarget = selected === mp.countryName;
            let warnings;
            if (job.subaction === "safety" && onTarget) {
              // Warning types in order: warning/safety info, then Responsible
              // Person, then manufacturer contact (skip the rest if the
              // browser was closed = user stop).
              warnings = await processSafetyWarnings(page, mp, mpDumpDir, userSkip);
              if (!page.isClosed()) {
                const rp = await processResponsiblePerson(
                  page, mp, mpDumpDir, job.ecRepPattern ?? "", userSkip);
                warnings = warnings.concat(rp);
              }
              if (!page.isClosed()) {
                const mfr = await processManufacturer(
                  page, mp, mpDumpDir, gsprManufacturers, askUser, userSkip,
                  job.manualManufacturerSave ?? true);
                warnings = warnings.concat(mfr);
              }
            }

            // Keep the collected warnings even when the snapshot can't be
            // taken any more (e.g. the user closed the browser mid-run).
            let snap: GsprSnapshotResult;
            try {
              snap = await snapshotCompliance(page, mp, mpDumpDir);
            } catch (e) {
              snap = { country: mp.country, ok: true,
                       error: `snapshot failed: ${(e as Error).message ?? e}` };
            }
            snap.selected = selected;
            snap.warnings = warnings;
            if (!onTarget) {
              snap.ok = false;
              snap.error = `marketplace mismatch: wanted "${mp.countryName}" but page shows "${selected}" — see ${mpDumpDir}`;
            }
            results.push(snap);
            i++; // this marketplace is done — advance

            // The user closing the browser means STOP — don't relaunch for
            // the remaining marketplaces.
            if (page.isClosed()) {
              log(`[gspr] browser closed — stopping run, remaining marketplaces skipped`);
              break;
            }
          } catch (e) {
            const msg = (e as Error).message ?? String(e);

            if (CRASHED_RE.test(msg) && !CLOSED_RE.test(msg)) {
              if (relaunches >= MAX_RELAUNCHES) {
                log(`[gspr:${mp.country}] browser crashed again — giving up after ${MAX_RELAUNCHES} relaunches`);
                results.push({ country: mp.country, ok: false,
                               error: `browser crashed ${MAX_RELAUNCHES} times: ${msg}` });
                i++;
                continue;
              }
              relaunches++;
              log(`[gspr:${mp.country}] browser crashed (${msg}) — relaunching`
                  + ` (attempt ${relaunches}/${MAX_RELAUNCHES}) and resuming this marketplace…`);
              await ctx.close().catch(() => {});
              try {
                ctx = await launchContext(cfg, region);
                page = await firstPage(ctx);
                if (!(await ensureLoggedIn(page, cfg, region))) {
                  log(`[gspr:${mp.country}] re-login failed after crash recovery — giving up on this marketplace`);
                  results.push({ country: mp.country, ok: false,
                                 error: "re-login failed after crash recovery", sessionExpired: true });
                  i++;
                }
                // else: retry the SAME marketplace (i unchanged) with the new page.
              } catch (relaunchErr) {
                const relaunchMsg = (relaunchErr as Error).message ?? String(relaunchErr);
                log(`[gspr] could not relaunch the browser: ${relaunchMsg} — stopping run`);
                results.push({ country: mp.country, ok: false,
                               error: `could not relaunch after crash: ${relaunchMsg}` });
                break;
              }
              continue;
            }

            log(`[gspr:${mp.country}] FAILED: ${msg}`);
            results.push({ country: mp.country, ok: false, error: msg,
                           sessionExpired: e instanceof SessionExpiredError });
            i++;
            if (CLOSED_RE.test(msg)) {
              log(`[gspr] browser/context closed — stopping run`);
              break;
            }
          }
        }
      }
    } finally {
      await ctx.close().catch(() => {});
    }
    process.stdout.write(JSON.stringify({ results }) + "\n");
    return;
  }

  // -- scrape / reply: group cases by region -----------------------------
  const cases = job.cases ?? [];
  const byRegion = new Map<Region, CaseJob[]>();
  for (const c of cases) {
    const r = (c.region ?? cfg.defaultRegion) as Region;
    let arr = byRegion.get(r);
    if (!arr) { arr = []; byRegion.set(r, arr); }
    arr.push(c);
  }

  const results: unknown[] = [];
  for (const [region, list] of byRegion) {
    const ctx = await launchContext(cfg, region);
    // ALWAYS close the context — a skipped close leaves Chromium holding the
    // profile lock, which makes the NEXT run fail to launch.
    try {
      const loginPage = await firstPage(ctx);

      // Pause for login BEFORE touching any case page.
      if (!(await ensureLoggedIn(loginPage, cfg, region))) {
        for (const c of list)
          results.push({ region, caseId: c.caseId, ok: false,
                         error: "not logged in (login wait timed out)", sessionExpired: true });
        continue;
      }

      for (const c of list) {
        // Submitting a reply can close the case tab, so grab a live page each
        // time (reuse an open one, else open a fresh tab).
        let page;
        try {
          page = ctx.pages().find((p) => !p.isClosed()) ?? await ctx.newPage();
        } catch {
          log(`[${region}] browser was closed — stopping; remaining cases not processed`);
          break;
        }

        try {
          if (job.action === "scrape") {
            const thread = await getThread(page, cfg, region, c.caseId, now, c.account);
            log(`[${region}] scraped ${c.caseId} (${thread.messages.length} msg)`);
            results.push({ region, caseId: c.caseId, ok: true, thread });
          } else {
            const files = c.files ?? [];
            if (job.manualSend)
              log(`[${region}] ${c.caseId}: filling + attaching ${files.length} file(s); click Send in the browser…`);
            const r = await submitReply(page, cfg, region, c.caseId, c.text ?? "", files,
                                        job.manualSend ?? false, now, c.account);
            log(`[${region}] ${c.caseId}: attached=${r.attached} sent=${r.sent}${r.error ? " error=" + r.error : ""}`);
            results.push({ region, caseId: c.caseId, ok: r.sent && !r.error,
                           attached: r.attached, sent: r.sent, error: r.error });
          }
        } catch (e) {
          const msg = (e as Error).message ?? String(e);
          const sessionExpired = e instanceof SessionExpiredError;
          log(`[${region}] ${c.caseId} FAILED: ${msg}`);
          results.push({ region, caseId: c.caseId, ok: false, error: msg, sessionExpired });
          // If the whole browser/context is gone, stop instead of failing every
          // remaining case with the same error.
          if (/has been closed|Target (page|browser).*closed|browser has been closed/i.test(msg)) {
            log(`[${region}] browser/context closed — stopping run for this region`);
            break;
          }
        }
      }
    } finally {
      await ctx.close().catch(() => {});
    }
  }

  process.stdout.write(JSON.stringify({ results }) + "\n");
}

main().catch((e) => {
  log(String(e && (e as Error).stack ? (e as Error).stack : e));
  process.stdout.write(JSON.stringify({ error: (e as Error).message ?? String(e) }) + "\n");
  process.exit(1);
});
