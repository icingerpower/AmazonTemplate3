import { chromium, type BrowserContext } from "playwright";
import { mkdirSync } from "node:fs";
import type { Config, Region } from "./types.js";
import { regionConfig } from "./config.js";

/**
 * Launches a *persistent* Chromium context for one region. The profile in that
 * region's userDataDir keeps its Seller Central session cookie between runs, so
 * you only log in (with MFA) once per region via `npm run login -- <region>`.
 */
export async function launchContext(cfg: Config, region: Region): Promise<BrowserContext> {
  const rc = regionConfig(cfg, region);
  mkdirSync(rc.userDataDir, { recursive: true });
  mkdirSync(cfg.screenshotDir, { recursive: true });

  const opts = {
    headless: cfg.headless,
    slowMo: cfg.slowMoMs,
    viewport: { width: 1440, height: 900 },
    // A stable, region-appropriate UA/locale reduces bot-detection friction.
    locale: rc.locale,
    args: ["--disable-blink-features=AutomationControlled"],
  };

  // Only ONE Chromium can use a profile dir at a time. A previous run's browser
  // may still be releasing the profile — retry a few times before giving up.
  let lastErr: unknown;
  for (let attempt = 1; attempt <= 6; attempt++) {
    try {
      return await chromium.launchPersistentContext(rc.userDataDir, opts);
    } catch (e) {
      lastErr = e;
      process.stderr.write(
        `[${region}] browser launch attempt ${attempt}/6 failed (profile busy?); retrying in 2s…\n`);
      await new Promise((r) => setTimeout(r, 2000));
    }
  }
  throw new Error(
    `could not launch browser for ${region}: the profile ${rc.userDataDir} is still in use by ` +
    `another window. Close any leftover "${region.toUpperCase()}" browser window and retry. ` +
    `(${(lastErr as Error)?.message ?? lastErr})`);
}

/** Returns the first page (persistent contexts open with one) or creates one. */
export async function firstPage(ctx: BrowserContext) {
  return ctx.pages()[0] ?? (await ctx.newPage());
}

export function timestampSlug(now: number): string {
  // Caller passes Date.now(); kept out of here so callers control time.
  return new Date(now).toISOString().replace(/[:.]/g, "-");
}
