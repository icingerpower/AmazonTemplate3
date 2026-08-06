/**
 * GSPR (EU General Product Safety Regulation) helpers — drives the Seller
 * Central Account Health "Regulatory compliance" page.
 *
 * Phase 1 only knows how to SNAPSHOT the page per marketplace: navigate,
 * switch the EU unified account to the requested marketplace, and dump the
 * rendered page (screenshot + HTML + text) under /tmp so the page structure
 * can be studied before warning-specific actions are implemented.
 */
import { mkdirSync } from "node:fs";
import { writeFile } from "node:fs/promises";
import { join } from "node:path";
import type { Page } from "playwright";

export const COMPLIANCE_PATH =
  "/performance/account/health/product-policies?t=regulatory-compliance";

// Top-nav partner switcher: its header label shows the currently-selected
// marketplace (used to VERIFY a switch). Its flyout is useless for switching —
// expanding the merchant row leaves a spinner that never resolves — so actual
// switching goes through the full-page switcher the flyout's "See all" links
// to (the same interstitial the Cases worker clicks through).
const SWITCHER_CURRENT = ".dropdown-account-switcher-header-label-regional";
const SWITCHER_PAGE_PATH = "/account-switcher/default/merchantMarketplace";

export interface GsprMarketplace {
  country: string;         // ISO code, e.g. "DE"
  countryName: string;     // switcher label, e.g. "Germany"
  skipAsins?: string[];    // warning/safety info already done — never re-attempted
  skipAsinsRp?: string[];  // Responsible Person already done — never re-attempted
  skipAsinsMfr?: string[]; // manufacturer contact already done — never re-attempted
}

export interface GsprSnapshotResult {
  country: string;
  ok: boolean;
  url?: string;
  dumpDir?: string;
  selected?: string;    // marketplace the page ended up on
  error?: string;
  warnings?: GsprWarningOutcome[];
}

// Outcome of one per-ASIN GSPR warning row.
//   submitted    — saved by this run
//   failed       — attempted but could not be completed
//   pending      — row is not actionable (no Submit / already under review)
//   skipped      — in the caller's done list (recorded done earlier)
//   user-skipped — product excluded by the user (Skip button) — never recorded
export interface GsprWarningOutcome {
  asin: string;
  ok: boolean;
  status: "submitted" | "failed" | "pending" | "skipped" | "user-skipped";
  reason?: string;
  statusText?: string;  // row's next-steps text, for pending diagnosis
  // psi = warning/safety info (default), rp = Responsible Person,
  // mfr = manufacturer contact details
  type?: "psi" | "rp" | "mfr";
}

function log(msg: string) { process.stderr.write(msg + "\n"); }

/** Per-ASIN outcome event, parsed live by the Qt app — record-as-you-go. */
function emitOutcome(country: string, o: GsprWarningOutcome) {
  log(`@@gspr-result ${JSON.stringify({ country, ...o })}`);
}

/** Marketplace currently shown in the top-nav switcher ("" if not found). */
export async function currentMarketplace(page: Page): Promise<string> {
  const el = page.locator(SWITCHER_CURRENT).first();
  const text = await el.innerText({ timeout: 5000 }).catch(() => "");
  return (text ?? "").trim();
}

/** Dump the current DOM + screenshot into dir as {tag}.html / {tag}.png. */
async function dumpDebug(page: Page, dir: string, tag: string): Promise<void> {
  mkdirSync(dir, { recursive: true });
  await writeFile(join(dir, `${tag}.html`), await page.content()).catch(() => {});
  await page.screenshot({ path: join(dir, `${tag}.png`), fullPage: true })
    .catch(() => {});
  log(`[gspr] debug dump: ${join(dir, tag)}.{html,png}`);
}

/**
 * Land on the compliance page with the requested marketplace selected, and
 * return the marketplace label the page actually shows (so callers can detect
 * a failed switch). debugDir receives DOM dumps whenever selection goes wrong.
 *
 * The full-page /account-switcher/?returnTo=… route 404s on this account, so
 * everything goes through the top-nav switcher flyout instead: open it, expand
 * the merchant row (marketplaces are hidden until their merchant is expanded),
 * click the country, and verify via the header label.
 */
export async function openComplianceFor(
  page: Page, domain: string, mp: GsprMarketplace, debugDir: string
): Promise<string> {
  const complianceUrl = `https://${domain}${COMPLIANCE_PATH}`;
  await page.goto(complianceUrl, { waitUntil: "domcontentloaded" });
  await settle(page);

  let current = await currentMarketplace(page);
  if (current === mp.countryName) {
    log(`[gspr:${mp.country}] already on "${current}"`);
    return current;
  }

  log(`[gspr:${mp.country}] page shows "${current}" — switching via the account-switcher page…`);
  const switcherUrl = `https://${domain}${SWITCHER_PAGE_PATH}?returnTo=${encodeURIComponent(COMPLIANCE_PATH)}`;
  await page.goto(switcherUrl, { waitUntil: "domcontentloaded" });

  // The page renders client-side — wait for the country row to appear.
  const row = page.getByText(mp.countryName, { exact: true }).first();
  if (!(await row.waitFor({ state: "visible", timeout: 20_000 })
          .then(() => true).catch(() => false))) {
    log(`[gspr:${mp.country}] WARNING: "${mp.countryName}" never appeared on the account-switcher page`);
    await dumpDebug(page, debugDir, "switcher-page");
  } else {
    await row.click().catch(() => {});
    log(`[gspr:${mp.country}] selected "${mp.countryName}", confirming…`);
    const btn = page.getByRole("button", { name: /select account/i }).first();
    if (await btn.count()) {
      await Promise.all([
        page.waitForNavigation({ waitUntil: "domcontentloaded" }).catch(() => {}),
        btn.click().catch(() => {}),
      ]);
    }
  }

  // Some switches land elsewhere (e.g. the home page) — come back if needed.
  if (!page.url().includes("/performance/account/health/product-policies"))
    await page.goto(complianceUrl, { waitUntil: "domcontentloaded" });
  await settle(page);

  current = await currentMarketplace(page);
  log(`[gspr:${mp.country}] landed with marketplace "${current}"`);
  if (current !== mp.countryName)
    await dumpDebug(page, debugDir, "switch-failed");
  return current;
}

/** The compliance dashboard renders client-side; give it time to finish. */
async function settle(page: Page): Promise<void> {
  // Seller Central keeps background connections alive, so "networkidle" often
  // only ends by timeout — keep it short or every step feels blocked.
  await page.waitForLoadState("networkidle", { timeout: 10_000 }).catch(() => {});
  await page.waitForTimeout(2000);
}

// ---------------------------------------------------------------------------
// "GPSR: warning and safety information" — per-ASIN safety attestation
// ---------------------------------------------------------------------------

const ROW_SELECTOR = '[data-testid="ahd-product-policies-table-row"]';
const REASON_PSI   = "GPSR: warning and safety information";

// The submission side pane (observed in the drawer dumps): a #flyout-root
// overlay whose .flyoutPanel slides in, with a bare "×" span as close control
// and one role=button card per resolution path. CAUTION: when closed the
// panel stays in the DOM, merely translated off-screen (translateX(100%)) —
// Playwright still reports it "visible" — so open/closed is detected by the
// `active` class, never by plain visibility.
const FLYOUT        = "#flyout-root";
const FLYOUT_PANEL  = `${FLYOUT} .flyoutPanel.active`;
const FLYOUT_CLOSE  = `${FLYOUT} .flyoutPanelContent > span`;
const ATTEST_CARD   = `${FLYOUT} [role="button"][aria-label="Safety attestation"]`;
const ATTEST_CHECKBOX = `${FLYOUT} kat-checkbox[label*="warning and safety information"]`;
const SAVE_BUTTON   = `${FLYOUT} kat-button[label="Save"], ${FLYOUT} button:has-text("Save")`;

/** Close the side pane if it is open; true when it ended up closed. */
async function ensureDrawerClosed(page: Page): Promise<boolean> {
  const panel = page.locator(FLYOUT_PANEL);
  if (!(await panel.isVisible().catch(() => false)))
    return true;
  await page.locator(FLYOUT_CLOSE).first().click({ force: true }).catch(() => {});
  if (await panel.waitFor({ state: "hidden", timeout: 5_000 })
        .then(() => true).catch(() => false))
    return true;
  await page.keyboard.press("Escape").catch(() => {});
  return panel.waitFor({ state: "hidden", timeout: 3_000 })
    .then(() => true).catch(() => false);
}

/** Whether the attestation kat-checkbox reports being ticked. */
async function checkboxTicked(page: Page): Promise<boolean> {
  return page.locator(ATTEST_CHECKBOX).first().evaluate((el: Element) => {
    const cb = el as Element & { checked?: boolean };
    return cb.checked === true || el.getAttribute("value") === "true"
        || el.hasAttribute("checked");
  }).catch(() => false);
}

/**
 * Process every flat (non-expandable) "GPSR: warning and safety information"
 * row on the current compliance page:
 *   Submit → side pane → pick the "Safety attestation" option (the last one)
 *   → tick the "product needs no warning/safety information" checkbox → Save.
 * ASINs whose pane has no "Safety attestation" option are reported as failed
 * so the app can record them. Debug dumps: the first opened pane is always
 * dumped (drawer-{asin}), every failure dumps too.
 */
export async function processSafetyWarnings(
  page: Page, mp: GsprMarketplace, dumpDir: string, userSkip: Set<string>
): Promise<GsprWarningOutcome[]> {
  // The per-ASIN rows carry stable testids: the reason cell ends with
  // _{ASIN}_COSS.GPSR_PSI_ASIN and the action button (when the row is
  // actionable) is ahd-action-button-{ASIN}. Grouped/expandable warnings
  // don't match that suffix and are skipped for now.
  interface RowInfo { asin: string; statusText: string; hasButton: boolean; }
  const rows: RowInfo[] = await page.evaluate(
    ({ rowSel, reason }: { rowSel: string; reason: string }) => {
      const found: { asin: string; statusText: string; hasButton: boolean }[] = [];
      for (const row of Array.from(document.querySelectorAll(rowSel))) {
        const reasonCell = row.querySelector('[data-testid^="ahd-ppc-row-reason-"]');
        if (!reasonCell?.textContent?.includes(reason)) continue;
        const m = reasonCell.getAttribute("data-testid")
          ?.match(/_([A-Z0-9]{10})_COSS\.GPSR_PSI_ASIN$/);
        if (!m?.[1]) continue;
        const statusEl = row.querySelector('[data-testid^="ahd-ppc-row-next-steps-"]');
        found.push({
          asin: m[1],
          statusText: (statusEl?.textContent ?? "").replace(/\s+/g, " ").trim(),
          hasButton: !!row.querySelector(`[data-testid="ahd-action-button-${m[1]}"]`),
        });
      }
      return found;
    }, { rowSel: ROW_SELECTOR, reason: REASON_PSI });

  const skip = new Set(mp.skipAsins ?? []);
  log(`[gspr:${mp.country}] ${rows.length} "${REASON_PSI}" row(s) on the page`);
  const out: GsprWarningOutcome[] = [];
  // Emit each outcome the moment it is known: the Qt app records it live, so
  // an interrupted run keeps the progress made so far.
  const record = (o: GsprWarningOutcome) => { emitOutcome(mp.country, o); out.push(o); };
  let firstForm = true;

  for (const row of rows) {
    const { asin } = row;

    if (userSkip.has(asin)) {
      log(`[gspr:${mp.country}] ${asin}: skipped by the user`);
      record({ asin, ok: true, status: "user-skipped", statusText: row.statusText });
      continue;
    }
    if (skip.has(asin)) {
      log(`[gspr:${mp.country}] ${asin}: already done earlier — skipped`);
      record({ asin, ok: true, status: "skipped", statusText: row.statusText });
      continue;
    }
    // No Submit button, or a status other than "submission is required":
    // someone (or a previous run) already submitted — never touch it again.
    if (!row.hasButton || !/submission is required/i.test(row.statusText)) {
      log(`[gspr:${mp.country}] ${asin}: not actionable (${row.statusText || "no status"}) — pending`);
      record({ asin, ok: true, status: "pending", statusText: row.statusText });
      continue;
    }

    try {
      // A pane left open from the previous ASIN swallows the Submit click.
      if (!(await ensureDrawerClosed(page))) {
        log(`[gspr:${mp.country}] ${asin}: previous pane would not close — stopping`);
        record({ asin, ok: false, status: "failed", reason: "previous pane would not close" });
        break;
      }

      // Amazon renders hidden "shadow-compare" duplicates of rows with the
      // SAME testids — :visible avoids waiting 30 s on the hidden copy.
      const btn = page.locator(`kat-button[data-testid="ahd-action-button-${asin}"]:visible`).first();
      await btn.scrollIntoViewIfNeeded().catch(() => {});
      await btn.click();
      await page.locator(FLYOUT_PANEL).waitFor({ state: "visible", timeout: 10_000 });

      const card = page.locator(ATTEST_CARD).first();
      if (!(await card.waitFor({ state: "visible", timeout: 10_000 })
              .then(() => true).catch(() => false))) {
        log(`[gspr:${mp.country}] ${asin}: no "Safety attestation" option`);
        await dumpDebug(page, dumpDir, `no-attestation-${asin}`);
        record({ asin, ok: false, status: "failed", reason: "Safety attestation option not available" });
        continue;
      }
      await card.click();

      // The attestation view is either the form (checkbox + Save) or an
      // "Under review" notice when a submission already exists — the table
      // row's status lags behind, so this is where pending is detected.
      // Race both markers instead of burning the full timeout on one.
      const checkbox   = page.locator(ATTEST_CHECKBOX).first();
      const reviewNote = page.locator(FLYOUT)
        .getByText("previously submitted is under review").first();
      const formState = await Promise.race([
        checkbox.waitFor({ state: "visible", timeout: 10_000 })
          .then(() => "form").catch(() => null),
        reviewNote.waitFor({ state: "visible", timeout: 10_000 })
          .then(() => "review").catch(() => null),
      ]);
      if (formState === "review") {
        log(`[gspr:${mp.country}] ${asin}: already under review — pending`);
        record({ asin, ok: true, status: "pending",
                   statusText: "Under review (submission pane)" });
        await ensureDrawerClosed(page);
        continue;
      }
      if (formState === null) {
        log(`[gspr:${mp.country}] ${asin}: attestation checkbox not found`);
        await dumpDebug(page, dumpDir, `no-checkbox-${asin}`);
        record({ asin, ok: false, status: "failed", reason: "attestation checkbox not found" });
        continue;
      }
      if (firstForm) {  // one full dump of the attestation form for reference
        await dumpDebug(page, dumpDir, `attest-form-${asin}`);
        firstForm = false;
      }

      // kat-checkbox is a shadow-DOM web component: a normal click never
      // passes Playwright's hit-target check (its inner layers "intercept
      // pointer events"), so force the click, then verify — with a JS-level
      // fallback that sets the property and emits change.
      await checkbox.click({ force: true });
      await page.waitForTimeout(500);
      if (!(await checkboxTicked(page))) {
        await checkbox.evaluate((el: Element) => {
          const cb = el as Element & { checked?: boolean; value?: string };
          cb.checked = true;
          cb.value = "true";
          el.dispatchEvent(new Event("input",  { bubbles: true, composed: true }));
          el.dispatchEvent(new Event("change", { bubbles: true, composed: true }));
        });
        await page.waitForTimeout(500);
      }
      if (!(await checkboxTicked(page))) {
        log(`[gspr:${mp.country}] ${asin}: could not tick the attestation checkbox`);
        await dumpDebug(page, dumpDir, `checkbox-stuck-${asin}`);
        record({ asin, ok: false, status: "failed", reason: "could not tick the attestation checkbox" });
        continue;
      }

      const save = page.locator(SAVE_BUTTON).last();
      if (!(await save.waitFor({ state: "visible", timeout: 5_000 })
              .then(() => true).catch(() => false))) {
        log(`[gspr:${mp.country}] ${asin}: Save button not found`);
        await dumpDebug(page, dumpDir, `no-save-${asin}`);
        record({ asin, ok: false, status: "failed", reason: "Save button not found" });
        continue;
      }
      await save.click();

      // A successful save does NOT close the pane: it shows a confirmation
      // (kat-box data-testid="success-page") with a Close button. Click it as
      // soon as it appears instead of waiting on timeouts.
      const successBox = page.locator(`${FLYOUT} [data-testid="success-page"]`).first();
      if (await successBox.waitFor({ state: "visible", timeout: 15_000 })
            .then(() => true).catch(() => false)) {
        await page.locator(`${FLYOUT} kat-button[label="Close"]`).first()
          .click().catch(() => {});
        await page.locator(FLYOUT_PANEL)
          .waitFor({ state: "hidden", timeout: 5_000 }).catch(() => {});
      } else if (!(await page.locator(FLYOUT_PANEL).isHidden().catch(() => false))) {
        // No confirmation and the pane is still open — a validation error.
        log(`[gspr:${mp.country}] ${asin}: no confirmation after Save`);
        await dumpDebug(page, dumpDir, `after-save-${asin}`);
        record({ asin, ok: false, status: "failed", reason: "no confirmation after Save (validation error?)" });
        continue;
      }

      log(`[gspr:${mp.country}] ${asin}: safety attestation saved`);
      record({ asin, ok: true, status: "submitted" });
    } catch (e) {
      const msg = (e as Error).message ?? String(e);
      // A closed browser is the user stopping the run, not an ASIN failure —
      // don't record anything for it.
      if (/has been closed|Target (page|browser).*closed/i.test(msg)) {
        log(`[gspr:${mp.country}] browser closed — treating as stop`);
        break;
      }
      log(`[gspr:${mp.country}] ${asin} FAILED: ${msg}`);
      await dumpDebug(page, dumpDir, `error-${asin}`).catch(() => {});
      record({ asin, ok: false, status: "failed", reason: msg });
      await ensureDrawerClosed(page);
    }
  }
  await ensureDrawerClosed(page);
  return out;
}

// ---------------------------------------------------------------------------
// "GPSR: Responsible Person contact details" — grouped per brand
// ---------------------------------------------------------------------------

const REASON_RP = "GPSR: Responsible Person contact details";

function escapeRegex(s: string): string {
  return s.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
}

/** Save → confirmation → Close. Returns null on success, else a fail reason. */
async function saveAndClose(page: Page, dumpDir: string, tag: string): Promise<string | null> {
  const save = page.locator(SAVE_BUTTON).last();
  if (!(await save.waitFor({ state: "visible", timeout: 5_000 })
          .then(() => true).catch(() => false))) {
    await dumpDebug(page, dumpDir, `${tag}-no-save`);
    return "Save button not found";
  }
  await save.click();
  const successBox = page.locator(`${FLYOUT} [data-testid="success-page"]`).first();
  if (await successBox.waitFor({ state: "visible", timeout: 15_000 })
        .then(() => true).catch(() => false)) {
    await page.locator(`${FLYOUT} kat-button[label="Close"]`).first()
      .click().catch(() => {});
    await page.locator(FLYOUT_PANEL)
      .waitFor({ state: "hidden", timeout: 5_000 }).catch(() => {});
    return null;
  }
  if (await page.locator(FLYOUT_PANEL).isHidden().catch(() => false))
    return null; // pane closed by itself — also a success
  await dumpDebug(page, dumpDir, `${tag}-after-save`);
  return "no confirmation after Save (validation error?)";
}

/**
 * Process every expandable "GPSR: Responsible Person contact details" group:
 * NEVER submit at group level — expand the brand, then handle each nested
 * per-ASIN sub-row one by one (walking the group's nested pagination). In the
 * submission pane, pick the first choice whose text starts with ecRepPattern
 * (case-insensitive, both sides trimmed), then Save.
 */
export async function processResponsiblePerson(
  page: Page, mp: GsprMarketplace, dumpDir: string, ecRepPattern: string,
  userSkip: Set<string>
): Promise<GsprWarningOutcome[]> {
  const out: GsprWarningOutcome[] = [];
  const record = (o: GsprWarningOutcome) => { emitOutcome(mp.country, o); out.push(o); };
  const pattern = ecRepPattern.trim();
  if (!pattern) {
    log(`[gspr:${mp.country}] EC Rep pattern is empty — Responsible Person warnings skipped`);
    return out;
  }
  const optionRegex = new RegExp("^\\s*" + escapeRegex(pattern), "i");
  const skip = new Set(mp.skipAsinsRp ?? []);
  let firstPane = true;

  const groups = page.locator(ROW_SELECTOR)
    .filter({ has: page.locator(".ahd-accordion") })
    .filter({ hasText: REASON_RP });
  const nGroups = await groups.count();
  log(`[gspr:${mp.country}] ${nGroups} expandable "${REASON_RP}" group(s)`);

  for (let g = 0; g < nGroups; g++) {
    const group = groups.nth(g);

    // Expand (the chevron button is labelled "expand" while collapsed).
    const expander = group.locator('.ahd-accordion__button[aria-label="expand"]').first();
    if (await expander.isVisible().catch(() => false)) {
      await expander.click().catch(() => {});
    }
    const body = group.locator(".ahd-accordion__body").first();
    if (!(await body.locator('[data-testid="ahd-nested-product-policy"]').first()
            .waitFor({ state: "visible", timeout: 10_000 })
            .then(() => true).catch(() => false))) {
      log(`[gspr:${mp.country}] RP group ${g + 1}: no sub-rows after expanding`);
      await dumpDebug(page, dumpDir, `rp-group${g + 1}-empty`);
      continue;
    }

    // Walk the group's nested pagination (10 sub-rows per page).
    for (let nestedPage = 1; ; nestedPage++) {
      const subs = body.locator('[data-testid="ahd-nested-product-policy"]');
      const nSubs = await subs.count();
      log(`[gspr:${mp.country}] RP group ${g + 1} page ${nestedPage}: ${nSubs} sub-row(s)`);

      for (let i = 0; i < nSubs; i++) {
        const sub = subs.nth(i);

        // RP sub-row Submit buttons are keyed by SKU (not ASIN) — take the
        // button as-is and read the ASIN from the View-listing link. Always
        // count() before getAttribute(): getAttribute on a missing element
        // waits the full 30 s default timeout.
        const btn = sub.locator('[data-testid^="ahd-action-button-"]').first();
        const hasButton = (await btn.count()) > 0;
        const link = sub
          .locator('a[data-testid="ahd-listing-url-product-policy-desktop"]').first();
        const href = (await link.count())
          ? await link.getAttribute("href").catch(() => null) : null;
        const asin = href?.match(/[?&]asin=([A-Z0-9]{10})/)?.[1] ?? "";
        if (!asin) {
          log(`[gspr:${mp.country}] RP group ${g + 1} row ${i + 1}: no ASIN — ignored`);
          continue;
        }

        const statusText = (await sub.locator(".text-size-sm.text-secondary").last()
          .innerText().catch(() => "")).replace(/\s+/g, " ").trim();
        if (userSkip.has(asin)) {
          log(`[gspr:${mp.country}] RP ${asin}: skipped by the user`);
          record({ asin, ok: true, status: "user-skipped", type: "rp", statusText });
          continue;
        }
        if (skip.has(asin)) {
          log(`[gspr:${mp.country}] RP ${asin}: already done earlier — skipped`);
          record({ asin, ok: true, status: "skipped", type: "rp", statusText });
          continue;
        }
        if (!hasButton || !/submission is required/i.test(statusText)) {
          log(`[gspr:${mp.country}] RP ${asin}: not actionable (${statusText || "no status"}) — pending`);
          record({ asin, ok: true, status: "pending", statusText, type: "rp" });
          continue;
        }

        try {
          if (!(await ensureDrawerClosed(page))) {
            record({ asin, ok: false, status: "failed", type: "rp",
                     reason: "previous pane would not close" });
            return out;
          }
          await btn.scrollIntoViewIfNeeded().catch(() => {});
          await btn.click();
          await page.locator(FLYOUT_PANEL).waitFor({ state: "visible", timeout: 10_000 });
          await page.waitForTimeout(1500); // pane content loads lazily

          if (firstPane) { // reference dump of this warning type's pane
            await dumpDebug(page, dumpDir, `rp-drawer-${asin}`);
            firstPane = false;
          }

          // Already submitted? Same under-review note as the safety pane.
          const reviewNote = page.locator(FLYOUT)
            .getByText("previously submitted is under review").first();
          if (await reviewNote.isVisible().catch(() => false)) {
            log(`[gspr:${mp.country}] RP ${asin}: already under review — pending`);
            record({ asin, ok: true, status: "pending", type: "rp",
                     statusText: "Under review (submission pane)" });
            await ensureDrawerClosed(page);
            continue;
          }

          // First choice starting with the EC Rep pattern. If none is visible
          // the choices may sit in a dropdown — open it and retry.
          let option = page.locator(FLYOUT).getByText(optionRegex).first();
          if (!(await option.waitFor({ state: "visible", timeout: 8_000 })
                  .then(() => true).catch(() => false))) {
            const dropdown = page.locator(`${FLYOUT} kat-dropdown`).first();
            if (await dropdown.count()) {
              await dropdown.click({ force: true }).catch(() => {});
              await page.waitForTimeout(800);
            }
            option = page.locator(FLYOUT).getByText(optionRegex).first();
          }
          if (!(await option.isVisible().catch(() => false))) {
            log(`[gspr:${mp.country}] RP ${asin}: no choice starts with "${pattern}"`);
            await dumpDebug(page, dumpDir, `rp-nooption-${asin}`);
            record({ asin, ok: false, status: "failed", type: "rp",
                     reason: `no choice starting with "${pattern}"` });
            await ensureDrawerClosed(page);
            continue;
          }
          await option.click({ force: true });
          await page.waitForTimeout(500);

          const failReason = await saveAndClose(page, dumpDir, `rp-${asin}`);
          if (failReason) {
            log(`[gspr:${mp.country}] RP ${asin}: ${failReason}`);
            record({ asin, ok: false, status: "failed", type: "rp", reason: failReason });
            await ensureDrawerClosed(page);
            continue;
          }
          log(`[gspr:${mp.country}] RP ${asin}: responsible person submitted`);
          record({ asin, ok: true, status: "submitted", type: "rp" });
          // All sub-rows of a group write to the same brand-level record —
          // rapid consecutive saves were observed to be silently dropped by
          // Amazon (5/15). Give the backend a moment between submissions.
          await page.waitForTimeout(3000);
        } catch (e) {
          const msg = (e as Error).message ?? String(e);
          if (/has been closed|Target (page|browser).*closed/i.test(msg)) {
            log(`[gspr:${mp.country}] browser closed — treating as stop`);
            return out;
          }
          log(`[gspr:${mp.country}] RP ${asin} FAILED: ${msg}`);
          await dumpDebug(page, dumpDir, `rp-error-${asin}`).catch(() => {});
          record({ asin, ok: false, status: "failed", type: "rp", reason: msg });
          await ensureDrawerClosed(page);
        }
      }

      // Next nested page, if any: fill the page-number input and click Go.
      const pageInput = body.locator('[data-testid="ahd-nested-pagination-number-input"]').first();
      if (!(await pageInput.count()))
        break; // no nested pagination in this group
      const maxAttr = await pageInput.getAttribute("max").catch(() => null);
      const maxPage = maxAttr ? parseInt(maxAttr, 10) : 1;
      if (nestedPage >= maxPage)
        break;
      await pageInput.fill(String(nestedPage + 1)).catch(() => {});
      await body.locator('[data-testid="ahd-nested-pagination-submit-button"]')
        .first().click().catch(() => {});
      await page.waitForTimeout(1500);
    }
  }
  await ensureDrawerClosed(page);
  return out;
}

// ---------------------------------------------------------------------------
// "GPSR: manufacturer contact details" — grouped per brand, data from the
// supplier xlsx files (parsed and cached by the Qt app, shipped in the job)
// ---------------------------------------------------------------------------

const REASON_MFR = "GPSR: manufacturer contact details";

export interface ManufacturerEntry {
  prefix: string;   // SKU prefix, longest match wins
  name: string;
  phone: string;
  address: string;
  email: string;
  url?: string;     // supplier contact/shop URL — valid as primary contact
}

// Reply the Qt app sends after a @@gspr-ask pause: either "done" with a
// refreshed manufacturer list (user edited the xlsx files) or "stop".
export interface AskReply { cmd?: string; manufacturers?: ManufacturerEntry[]; }
export type AskFn = (payload: Record<string, unknown>) => Promise<AskReply>;

/** Longest case-insensitive prefix match of sku against the entries. */
function findManufacturer(entries: ManufacturerEntry[], sku: string): ManufacturerEntry | null {
  const s = sku.toLowerCase();
  let best: ManufacturerEntry | null = null;
  for (const e of entries) {
    if (!e.prefix) continue;
    if (s.startsWith(e.prefix.toLowerCase())
        && (!best || e.prefix.length > best.prefix.length))
      best = e;
  }
  return best;
}

/** Fill the inner input/textarea of a kat web component. */
async function fillKatField(page: Page, sel: string, value: string): Promise<boolean> {
  const host = page.locator(sel).first();
  if (!(await host.count())) return false;
  // kat-input keeps a hidden mirror <input> in the light DOM — skip it and
  // fill the real (visible, shadow-DOM) input.
  const input = host.locator("input:not([hidden]), textarea:not([hidden])").first();
  if (!(await input.count())) return false;
  try { await input.fill(value); return true; } catch { return false; }
}

// Split a free-form address into the structured fields of Amazon's add-new
// manufacturer form. Heuristic: comma tokens; a trailing country name/code is
// mapped for the country dropdown; state = last token, city = the one before.
const COUNTRY_ALIASES: Record<string, string> = {
  cn: "China", china: "China", pk: "Pakistan", pakistan: "Pakistan",
  vn: "Vietnam", vietnam: "Vietnam", in: "India", india: "India",
  tr: "Turkey", turkey: "Turkey",
};
function splitAddress(addr: string) {
  const tokens = addr.split(",").map((t) => t.trim()).filter(Boolean);
  let country = "";
  if (tokens.length) {
    const last = tokens[tokens.length - 1]!.toLowerCase().replace(/\.$/, "");
    if (COUNTRY_ALIASES[last]) { country = COUNTRY_ALIASES[last]!; tokens.pop(); }
  }
  let postal = "";
  for (const t of tokens) {
    const m = t.match(/(\d{5,6})/);
    if (m?.[1]) postal = m[1];
  }
  const state = tokens.length ? tokens[tokens.length - 1]! : "";
  const city  = tokens.length > 1 ? tokens[tokens.length - 2]! : state;
  const line  = tokens.slice(0, Math.max(0, tokens.length - 2)).join(", ") || city;
  return { line1: line.slice(0, 100), line2: line.slice(100, 200),
           city, state, postal, country: country || "China" };
}

/**
 * Process every expandable "GPSR: manufacturer contact details" group: NEVER
 * submit at group level — expand, then handle nested sub-rows one by one.
 * The manufacturer for a sub-row is the longest-prefix match of its SKU in
 * `manufacturers`. When no entry matches, `ask` pauses the run (the Qt app
 * shows a dialog); on "done" the refreshed list is retried, on "stop" the
 * whole phase aborts.
 */
export async function processManufacturer(
  page: Page, mp: GsprMarketplace, dumpDir: string,
  manufacturers: ManufacturerEntry[], ask: AskFn, userSkip: Set<string>,
  manualSave: boolean
): Promise<GsprWarningOutcome[]> {
  const out: GsprWarningOutcome[] = [];
  const record = (o: GsprWarningOutcome) => { emitOutcome(mp.country, o); out.push(o); };
  if (!manufacturers.length) {
    log(`[gspr:${mp.country}] no manufacturer entries — manufacturer warnings skipped`);
    return out;
  }
  const skip = new Set(mp.skipAsinsMfr ?? []);
  let firstPane = true;
  let firstForm = true;

  const groups = page.locator(ROW_SELECTOR)
    .filter({ has: page.locator(".ahd-accordion") })
    .filter({ hasText: REASON_MFR });
  const nGroups = await groups.count();
  log(`[gspr:${mp.country}] ${nGroups} expandable "${REASON_MFR}" group(s)`);

  for (let g = 0; g < nGroups; g++) {
    const group = groups.nth(g);
    const expander = group.locator('.ahd-accordion__button[aria-label="expand"]').first();
    if (await expander.isVisible().catch(() => false))
      await expander.click().catch(() => {});
    const body = group.locator(".ahd-accordion__body").first();
    if (!(await body.locator('[data-testid="ahd-nested-product-policy"]').first()
            .waitFor({ state: "visible", timeout: 10_000 })
            .then(() => true).catch(() => false))) {
      log(`[gspr:${mp.country}] MFR group ${g + 1}: no sub-rows after expanding`);
      await dumpDebug(page, dumpDir, `mfr-group${g + 1}-empty`);
      continue;
    }

    for (let nestedPage = 1; ; nestedPage++) {
      const subs = body.locator('[data-testid="ahd-nested-product-policy"]');
      const nSubs = await subs.count();
      log(`[gspr:${mp.country}] MFR group ${g + 1} page ${nestedPage}: ${nSubs} sub-row(s)`);

      for (let i = 0; i < nSubs; i++) {
        const sub = subs.nth(i);
        const btn = sub.locator('[data-testid^="ahd-action-button-"]').first();
        const hasButton = (await btn.count()) > 0;
        const link = sub
          .locator('a[data-testid="ahd-listing-url-product-policy-desktop"]').first();
        const href = (await link.count())
          ? await link.getAttribute("href").catch(() => null) : null;
        const asin = href?.match(/[?&]asin=([A-Z0-9]{10})/)?.[1] ?? "";
        const rawSku = href?.match(/[?&]sku=([^&]+)/)?.[1] ?? "";
        const sku = decodeURIComponent(rawSku.replace(/\+/g, "%20"));
        if (!asin) {
          log(`[gspr:${mp.country}] MFR group ${g + 1} row ${i + 1}: no ASIN — ignored`);
          continue;
        }

        const statusText = (await sub.locator(".text-size-sm.text-secondary").last()
          .innerText().catch(() => "")).replace(/\s+/g, " ").trim();
        if (userSkip.has(asin)) {
          log(`[gspr:${mp.country}] MFR ${asin}: skipped by the user`);
          record({ asin, ok: true, status: "user-skipped", type: "mfr", statusText });
          continue;
        }
        if (skip.has(asin)) {
          log(`[gspr:${mp.country}] MFR ${asin}: already done earlier — skipped`);
          record({ asin, ok: true, status: "skipped", type: "mfr", statusText });
          continue;
        }
        if (!hasButton || !/submission is required/i.test(statusText)) {
          log(`[gspr:${mp.country}] MFR ${asin}: not actionable (${statusText || "no status"}) — pending`);
          record({ asin, ok: true, status: "pending", statusText, type: "mfr" });
          continue;
        }

        // Resolve the manufacturer BEFORE touching the page — no match pauses
        // the run so the user can complete the xlsx files.
        let entry = findManufacturer(manufacturers, sku);
        let skippedByUser = false;
        while (!entry && !skippedByUser) {
          log(`[gspr:${mp.country}] MFR ${asin}: no manufacturer for SKU "${sku}" — waiting for the user…`);
          const reply = await ask({ kind: "manufacturer", country: mp.country, asin, sku });
          if (reply?.cmd === "done" && Array.isArray(reply.manufacturers)) {
            manufacturers.length = 0;
            manufacturers.push(...reply.manufacturers);
            entry = findManufacturer(manufacturers, sku);
            if (!entry)
              log(`[gspr:${mp.country}] MFR ${asin}: SKU "${sku}" still has no manufacturer — asking again`);
          } else if (reply?.cmd === "skip") {
            skippedByUser = true;
          } else {
            log(`[gspr:${mp.country}] stop requested by the user`);
            return out;
          }
        }
        if (skippedByUser || !entry) {
          log(`[gspr:${mp.country}] MFR ${asin}: skipped by the user`);
          userSkip.add(asin); // also skips it in later phases/countries this run
          record({ asin, ok: true, status: "user-skipped", type: "mfr" });
          continue;
        }

        try {
          if (!(await ensureDrawerClosed(page))) {
            record({ asin, ok: false, status: "failed", type: "mfr",
                     reason: "previous pane would not close" });
            return out;
          }
          await btn.scrollIntoViewIfNeeded().catch(() => {});
          await btn.click();
          await page.locator(FLYOUT_PANEL).waitFor({ state: "visible", timeout: 10_000 });
          await page.waitForTimeout(1500);

          if (firstPane) { // reference dump of this warning type's pane
            await dumpDebug(page, dumpDir, `mfr-drawer-${asin}`);
            firstPane = false;
          }

          const reviewNote = page.locator(FLYOUT)
            .getByText("previously submitted is under review").first();
          if (await reviewNote.isVisible().catch(() => false)) {
            log(`[gspr:${mp.country}] MFR ${asin}: already under review — pending`);
            record({ asin, ok: true, status: "pending", type: "mfr",
                     statusText: "Under review (submission pane)" });
            await ensureDrawerClosed(page);
            continue;
          }

          // The pane content loads asynchronously (a spinner shows first),
          // then offers a registry of saved manufacturer entries and an
          // "Add a new Manufacturer information" link (see the mfr dumps).
          const registry = page.locator(`${FLYOUT} [data-testid="registry-list"]`).first();
          if (!(await registry.waitFor({ state: "visible", timeout: 20_000 })
                  .then(() => true).catch(() => false))) {
            log(`[gspr:${mp.country}] MFR ${asin}: manufacturer pane did not load`);
            await dumpDebug(page, dumpDir, `mfr-noload-${asin}`);
            record({ asin, ok: false, status: "failed", type: "mfr",
                     reason: "manufacturer pane did not load" });
            await ensureDrawerClosed(page);
            continue;
          }

          // Reuse a saved entry with the same manufacturer name when there is
          // one; otherwise create it through the add-new form. The entries
          // render shortly after the list container — give them a moment.
          await page.waitForTimeout(800);
          const existing = registry.getByText(entry.name.trim()).first();
          if (await existing.isVisible().catch(() => false)) {
            log(`[gspr:${mp.country}] MFR ${asin}: selecting existing entry "${entry.name}"`);
            await existing.click({ force: true });
            await page.waitForTimeout(500);
          } else {
            log(`[gspr:${mp.country}] MFR ${asin}: creating entry "${entry.name}"`);
            await page.locator(`${FLYOUT} kat-link[data-testid="add-new-registry"]`)
              .first().click().catch(() => {});
            await page.waitForTimeout(1500); // the form renders lazily
            if (firstForm) { // reference dump of the add-new form
              await dumpDebug(page, dumpDir, `mfr-form-${asin}`);
              firstForm = false;
            }
            // Field names observed in the mfr-form dump. "Primary email
            // address or URL" is REQUIRED — email, else the supplier URL. In
            // manual-save mode a missing one is fine: the user completes it.
            const primary = entry.email || entry.url || "";
            if (!primary && !manualSave) {
              log(`[gspr:${mp.country}] MFR ${asin}: "${entry.name}" has no email/URL (required) — fix the xlsx`);
              record({ asin, ok: false, status: "failed", type: "mfr",
                       reason: `manufacturer "${entry.name}" has no email or URL (required by the form)` });
              await ensureDrawerClosed(page);
              continue;
            }
            const a = splitAddress(entry.address);
            const filledName = await fillKatField(page,
              `${FLYOUT} kat-input[name="name"]`, entry.name);
            await fillKatField(page,
              `${FLYOUT} kat-input[name="primary_contact_reference"]`, primary);
            // Phone number is deliberately NEVER filled: entering one in the
            // GSPR manufacturer details triggers an Amazon bug.
            await fillKatField(page, `${FLYOUT} kat-input[name="address_line_1"]`, a.line1);
            if (a.line2)
              await fillKatField(page, `${FLYOUT} kat-input[name="address_line_2"]`, a.line2);
            await fillKatField(page, `${FLYOUT} kat-input[name="city"]`, a.city);
            if (a.state)
              await fillKatField(page, `${FLYOUT} kat-input[name="state_or_region"]`, a.state);
            if (a.postal)
              await fillKatField(page, `${FLYOUT} kat-input[name="postal_code"]`, a.postal);
            if (!filledName) {
              log(`[gspr:${mp.country}] MFR ${asin}: add-new form fields not found`);
              await dumpDebug(page, dumpDir, `mfr-nofields-${asin}`);
              record({ asin, ok: false, status: "failed", type: "mfr",
                       reason: "add-new manufacturer form fields not found" });
              await ensureDrawerClosed(page);
              continue;
            }
            // Country dropdown (in manual mode a miss is not fatal — the
            // user picks it before saving).
            const dd = page.locator(`${FLYOUT} kat-dropdown[name="country"]`).first();
            await dd.click({ force: true }).catch(() => {});
            await page.waitForTimeout(800);
            // Options are rendered ALL-CAPS ("CHINA") — match case-insensitively.
            const countryOpt = page.locator(FLYOUT)
              .getByText(new RegExp("^\\s*" + escapeRegex(a.country) + "\\s*$", "i")).last();
            if (await countryOpt.isVisible().catch(() => false)) {
              await countryOpt.click({ force: true }).catch(() => {});
              await page.waitForTimeout(300);
            } else if (!manualSave) {
              log(`[gspr:${mp.country}] MFR ${asin}: country "${a.country}" not in dropdown`);
              await dumpDebug(page, dumpDir, `mfr-country-${asin}`);
              record({ asin, ok: false, status: "failed", type: "mfr",
                       reason: `country "${a.country}" not found in the dropdown` });
              await ensureDrawerClosed(page);
              continue;
            } else {
              log(`[gspr:${mp.country}] MFR ${asin}: country "${a.country}" not found — pick it yourself`);
            }

            if (manualSave) {
              // The user reviews/completes the form and clicks Save.
              log(`[gspr:${mp.country}] MFR ${asin}: PAUSED — verify the form for "${entry.name}" in the browser, complete missing fields and click Save (up to 10 min)…`);
              const nameField = page.locator(`${FLYOUT} kat-input[name="name"]`).first();
              let savedByUser = false, paneClosed = false;
              for (let t = 0; t < 300; t++) { // ~10 min at 2 s
                await page.waitForTimeout(2000);
                if (!(await page.locator(FLYOUT_PANEL).isVisible().catch(() => false))) {
                  paneClosed = true; break;
                }
                if (!(await nameField.isVisible().catch(() => false))) {
                  savedByUser = true; break;
                }
              }
              if (!savedByUser) {
                const reason = paneClosed ? "pane closed before the manufacturer was saved"
                                          : "timed out waiting for the manufacturer Save";
                log(`[gspr:${mp.country}] MFR ${asin}: ${reason}`);
                record({ asin, ok: false, status: "failed", type: "mfr", reason });
                await ensureDrawerClosed(page);
                continue;
              }
              log(`[gspr:${mp.country}] MFR ${asin}: manufacturer saved by the user`);
            } else {
              // Auto mode: click the form's Save; the pane returns to the
              // registry list.
              await page.locator(SAVE_BUTTON).last().click().catch(() => {});
              await page.waitForTimeout(1500);
            }

            // Click Refresh until the new entry shows up in the registry,
            // then select it.
            const created = registry.getByText(entry.name.trim()).first();
            let visibleNow = false;
            for (let t = 0; t < 10; t++) {
              if (await created.isVisible().catch(() => false)) { visibleNow = true; break; }
              await page.locator(`${FLYOUT} kat-link[data-testid="refresh"]`)
                .first().click({ force: true }).catch(() => {});
              await page.waitForTimeout(1500);
            }
            if (!visibleNow) {
              log(`[gspr:${mp.country}] MFR ${asin}: "${entry.name}" still not in the registry after refresh`);
              await dumpDebug(page, dumpDir, `mfr-norefresh-${asin}`);
              record({ asin, ok: false, status: "failed", type: "mfr",
                       reason: "created manufacturer not visible after refresh" });
              await ensureDrawerClosed(page);
              continue;
            }
            await created.click({ force: true });
            await page.waitForTimeout(500);
          }
          await page.waitForTimeout(500);

          const failReason = await saveAndClose(page, dumpDir, `mfr-${asin}`);
          if (failReason) {
            log(`[gspr:${mp.country}] MFR ${asin}: ${failReason}`);
            record({ asin, ok: false, status: "failed", type: "mfr", reason: failReason });
            await ensureDrawerClosed(page);
            continue;
          }
          log(`[gspr:${mp.country}] MFR ${asin}: manufacturer "${entry.name}" submitted`);
          record({ asin, ok: true, status: "submitted", type: "mfr" });
        } catch (e) {
          const msg = (e as Error).message ?? String(e);
          if (/has been closed|Target (page|browser).*closed/i.test(msg)) {
            log(`[gspr:${mp.country}] browser closed — treating as stop`);
            return out;
          }
          log(`[gspr:${mp.country}] MFR ${asin} FAILED: ${msg}`);
          await dumpDebug(page, dumpDir, `mfr-error-${asin}`).catch(() => {});
          record({ asin, ok: false, status: "failed", type: "mfr", reason: msg });
          await ensureDrawerClosed(page);
        }
      }

      const pageInput = body.locator('[data-testid="ahd-nested-pagination-number-input"]').first();
      if (!(await pageInput.count()))
        break;
      const maxAttr = await pageInput.getAttribute("max").catch(() => null);
      const maxPage = maxAttr ? parseInt(maxAttr, 10) : 1;
      if (nestedPage >= maxPage)
        break;
      await pageInput.fill(String(nestedPage + 1)).catch(() => {});
      await body.locator('[data-testid="ahd-nested-pagination-submit-button"]')
        .first().click().catch(() => {});
      await page.waitForTimeout(1500);
    }
  }
  await ensureDrawerClosed(page);
  return out;
}

/**
 * Dump the current page under dir: page.png (full-page screenshot),
 * page.html (DOM), page.txt (visible text), url.txt (final URL).
 */
export async function snapshotCompliance(
  page: Page, mp: GsprMarketplace, dir: string
): Promise<GsprSnapshotResult> {
  mkdirSync(dir, { recursive: true });

  const url = page.url();
  await writeFile(join(dir, "url.txt"), url + "\n");
  await writeFile(join(dir, "page.html"), await page.content());
  const text = await page.evaluate(() => document.body?.innerText ?? "")
    .catch(() => "");
  await writeFile(join(dir, "page.txt"), text);
  await page.screenshot({ path: join(dir, "page.png"), fullPage: true })
    .catch((e) => log(`[gspr:${mp.country}] screenshot failed: ${e}`));

  log(`[gspr:${mp.country}] page dumped to ${dir} (url: ${url})`);
  return { country: mp.country, ok: true, url, dumpDir: dir };
}
