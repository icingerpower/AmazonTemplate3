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
 *
 * One browser context is opened per region within a single invocation and
 * reused for every case in that region, then closed.
 */
import type { Page } from "playwright";
import { loadConfig, caseListUrl } from "./config.js";
import { launchContext, firstPage } from "./browser.js";
import { getThread, submitReply, isLoggedIn, passAccountSwitcher, SessionExpiredError } from "./casePage.js";
import type { Config, Region } from "./types.js";

interface CaseJob { region?: Region; caseId: string; text?: string; account?: string; files?: string[]; }
interface Job { action: "scrape" | "reply" | "login"; cases?: CaseJob[]; region?: Region; manualSend?: boolean; }

async function readStdin(): Promise<string> {
  const chunks: Buffer[] = [];
  for await (const c of process.stdin) chunks.push(c as Buffer);
  return Buffer.concat(chunks).toString("utf8");
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
  const job = JSON.parse(await readStdin()) as Job;
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
