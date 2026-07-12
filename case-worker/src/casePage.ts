import { createHash } from "node:crypto";
import { writeFile } from "node:fs/promises";
import { join } from "node:path";
import type { Page } from "playwright";
import type { Config, Region, CaseThread, ThreadMessage } from "./types.js";
import { caseUrl, caseListUrl } from "./config.js";
import { timestampSlug } from "./browser.js";

export class SessionExpiredError extends Error {
  constructor(region: Region) {
    super(`Seller Central [${region}] session expired — run \`npm run login -- ${region}\` to re-authenticate.`);
    this.name = "SessionExpiredError";
  }
}

async function shot(page: Page, cfg: Config, tag: string, now: number): Promise<string> {
  const path = join(cfg.screenshotDir, `${tag}-${timestampSlug(now)}.png`);
  await page.screenshot({ path, fullPage: true }).catch(() => {});
  return path;
}

/**
 * Whether we're signed in — decided by the URL, not fragile DOM markers.
 * Amazon bounces signed-out users to an auth/sign-in URL (ap/signin, MFA, CVF,
 * authportal); signed-in users stay on the Seller Central /cu/case-* path.
 */
export async function isLoggedIn(page: Page, _cfg: Config): Promise<boolean> {
  const url = page.url();
  if (!url || url === "about:blank") return false;
  // Any Amazon auth/sign-in/MFA URL means we are NOT signed in yet.
  if (/\/ap\/(signin|cvf|mfa|register)|authportal|signin\?/i.test(url))
    return false;
  // Any Seller Central page that isn't an auth page means we ARE signed in
  // (covers landing on the home page, the lobby, or a case after login).
  return /sellercentral\.[^/]+\//i.test(url);
}

/**
 * Get past Amazon's "Select an account" marketplace switcher if it appears.
 * Picks the region's configured accountCountry (else leaves the current pick),
 * clicks "Select account", and waits to land on the returnTo target. The choice
 * is remembered in the profile, so this normally only happens once per session.
 */
export async function passAccountSwitcher(page: Page, cfg: Config, region: Region, account?: string): Promise<void> {
  if (!page.url().includes("/account-switcher/")) return;

  const country = account || cfg.regions[region]?.accountCountry;
  if (country) {
    const row = page.getByText(country, { exact: true }).first();
    if (await row.count()) await row.click().catch(() => {});
  }
  const btn = page.getByRole("button", { name: /select account/i }).first();
  if (await btn.count()) {
    await Promise.all([
      page.waitForNavigation({ waitUntil: "domcontentloaded" }).catch(() => {}),
      btn.click().catch(() => {}),
    ]);
  }
}

/** Navigate to a case in a region; throws SessionExpiredError if bounced to sign-in. */
export async function openCase(page: Page, cfg: Config, region: Region, caseId: string, account?: string): Promise<void> {
  await page.goto(caseUrl(cfg, region, caseId), { waitUntil: "domcontentloaded" });
  await passAccountSwitcher(page, cfg, region, account); // may redirect on to the case
  if (!(await isLoggedIn(page, cfg))) throw new SessionExpiredError(region);
  // Let the case thread render (SPA).
  await page.locator(cfg.selectors.threadMessage).first()
    .waitFor({ state: "visible", timeout: 15000 })
    .catch(() => { /* empty/new case — handled by getThread returning 0 messages */ });
}

function classifyAuthor(author: string): ThreadMessage["from"] {
  const a = author.toLowerCase();
  if (a.includes("amazon") || a.includes("seller support") || a.includes("support")) return "amazon";
  if (a.includes("you") || a.includes("vous") || a.includes("seller")) return "seller";
  return "unknown";
}

export async function getThread(page: Page, cfg: Config, region: Region, caseId: string, now: number, account?: string): Promise<CaseThread> {
  await openCase(page, cfg, region, caseId, account);
  const s = cfg.selectors;

  // Debug: dump the rendered DOM so selectors can be tuned against the real page.
  await writeFile(join(cfg.screenshotDir, `thread-${region}-${caseId}.html`),
                  await page.content()).catch(() => {});

  const subject = (await page.locator(s.caseSubject).first().textContent().catch(() => ""))?.trim() || "";
  const status = (await page.locator(s.caseStatus).first().textContent().catch(() => ""))?.trim() || "";

  const nodes = page.locator(s.threadMessage);
  const count = await nodes.count();
  const messages: ThreadMessage[] = [];

  for (let i = 0; i < count; i++) {
    const node = nodes.nth(i);
    const author = ((await node.locator(s.threadMessageAuthor).first().textContent().catch(() => "")) || "").trim();
    const text = ((await node.locator(s.threadMessageBody).first().textContent().catch(() => "")) || "").trim();
    const ts = ((await node.locator(s.threadMessageTime).first().textContent().catch(() => "")) || "").trim();
    if (!text) continue;
    messages.push({ from: classifyAuthor(author), author, text, ts });
  }

  if (count === 0) await shot(page, cfg, `thread-empty-${region}-${caseId}`, now);

  // Seller Central lists messages newest-first; reverse to chronological order
  // (oldest → newest) so the app's "latest message" logic sees the last entry.
  messages.reverse();

  const threadHash = createHash("sha256")
    .update(messages.map((m) => `${m.from}|${m.text}`).join("\n"))
    .digest("hex")
    .slice(0, 16);

  return { caseId, region, subject, status, messages, threadHash };
}

export interface ReplyOutcome {
  attached: number;  // files successfully attached
  sent: boolean;     // reply actually submitted
  error?: string;    // set when something failed (then the reply is NOT sent)
}

/**
 * Full reply flow: open the case, open the reply editor, type the draft, attach
 * any files, then either submit or (manualSend) leave the browser open for the
 * user to submit. Never clicks Send if attachment upload fails.
 * Dumps the reply-editor DOM to screenshots/reply-*.html for selector tuning.
 */
export async function submitReply(page: Page, cfg: Config, region: Region, caseId: string,
                                  text: string, files: string[], manualSend: boolean,
                                  now: number, account?: string): Promise<ReplyOutcome> {
  const log = (m: string) => process.stderr.write(`[${region}] ${caseId} reply: ${m}\n`);
  await openCase(page, cfg, region, caseId, account);

  // Open the reply editor if there's a Reply button.
  const replyBtn = page.locator(cfg.selectors.replyButton).first();
  const replyBtnCount = await replyBtn.count();
  log(`replyButton count=${replyBtnCount}`);
  if (replyBtnCount) {
    await replyBtn.click().catch(() => {});
    await page.waitForTimeout(1500);
  }

  // Dump the (hopefully open) reply editor DOM so selectors can be tuned. NB:
  // Katal controls (kat-textarea / kat-file-upload) keep their real input in
  // shadow DOM, which page.content() does NOT capture — rely on the counts below.
  await writeFile(join(cfg.screenshotDir, `reply-${region}-${caseId}.html`),
                  await page.content()).catch(() => {});

  // Type the draft. Playwright pierces open shadow DOM to reach the inner box.
  const box = page.locator(cfg.selectors.replyTextarea).first();
  try {
    log(`textarea match count=${await page.locator(cfg.selectors.replyTextarea).count()}`);
    await box.waitFor({ state: "visible", timeout: 15000 });
    await box.click();
    await box.fill(text);
    log(`typed ${text.length} chars`);
  } catch (e) {
    return { attached: 0, sent: false, error: `reply box not found: ${(e as Error).message}` };
  }

  // Attach files. If anything goes wrong, DO NOT send.
  let attached = 0;
  if (files.length) {
    const input = page.locator(cfg.selectors.attachInput).first();
    const inputCount = await input.count();
    log(`attachInput count=${inputCount}, attaching ${files.length} file(s)`);
    if (!inputCount)
      return { attached: 0, sent: false, error: "attachment upload control not found — not sending" };
    try {
      await input.setInputFiles(files);
      attached = files.length;
      await page.waitForTimeout(3000); // let uploads register
      log(`setInputFiles ok (${attached})`);
    } catch (e) {
      return { attached: 0, sent: false, error: `attachment upload failed: ${(e as Error).message}` };
    }
  }

  // Manual mode: keep the window open and wait for the user to click Send.
  if (manualSend) {
    for (let i = 0; i < 150; i++) { // ~5 min
      await page.waitForTimeout(2000);
      if (!(await box.isVisible().catch(() => false)))
        return { attached, sent: true }; // editor gone → user submitted
    }
    return { attached, sent: false }; // timed out; user did not send
  }

  // Auto mode: click Send.
  const sendBtn = page.locator(cfg.selectors.sendButton).first();
  const sendCount = await sendBtn.count();
  log(`sendButton count=${sendCount}`);
  if (!sendCount)
    return { attached, sent: false, error: "send button not found" };
  await sendBtn.click().catch(() => {});
  await page.waitForLoadState("networkidle").catch(() => {});
  log("clicked Send");
  return { attached, sent: true };
}

/** Type the reply into the box but DO NOT submit. Returns after the text is in place. */
export async function fillReply(page: Page, cfg: Config, region: Region, caseId: string, text: string, now: number, account?: string): Promise<void> {
  await openCase(page, cfg, region, caseId, account);
  const box = page.locator(cfg.selectors.replyTextarea).first();
  await box.waitFor({ state: "visible", timeout: 15000 });
  await box.click();
  await box.fill(text);
  await shot(page, cfg, `filled-${region}-${caseId}`, now);
}

/**
 * Click Send. Only call this when the associated group's autoSend is on, or
 * after a human has approved the draft in the app.
 */
export async function send(page: Page, cfg: Config, region: Region, caseId: string, now: number): Promise<void> {
  const btn = page.locator(cfg.selectors.sendButton).first();
  await btn.waitFor({ state: "visible", timeout: 10000 });
  await shot(page, cfg, `pre-send-${region}-${caseId}`, now);
  await btn.click();
  await page.waitForLoadState("networkidle").catch(() => {});
  await shot(page, cfg, `post-send-${region}-${caseId}`, now);
}

export { caseListUrl };
