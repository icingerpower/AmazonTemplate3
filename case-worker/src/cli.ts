#!/usr/bin/env -S npx tsx
/**
 * Manual test harness — prove each operation against a real case before wiring
 * the Qt app. The first argument after the command is the REGION (eu|na|jp):
 *
 *   npm run login  -- eu                    # open EU browser, log in once (MFA), leave it
 *   npm run open   -- eu <caseId>           # navigate to an EU case
 *   npm run thread -- eu <caseId>           # scrape + print the thread JSON
 *   npm run fill   -- eu <caseId> "reply"   # type a reply, DO NOT send
 *   npm run send   -- eu <caseId>           # click Send (careful!)
 *
 * Each region has its own login session — log in once per region you use.
 */
import { loadConfig, caseListUrl } from "./config.js";
import { launchContext, firstPage } from "./browser.js";
import { getThread, fillReply, send, openCase, isLoggedIn } from "./casePage.js";
import type { Region } from "./types.js";

const REGIONS: Region[] = ["eu", "na", "jp"];

async function main() {
  const [cmd, regionArg, caseId, arg] = process.argv.slice(2);
  const cfg = loadConfig();

  const region = (regionArg ?? cfg.defaultRegion) as Region;
  if (!REGIONS.includes(region)) {
    console.error(`Region must be one of: ${REGIONS.join(" | ")} (got "${regionArg}")`);
    console.error(`Usage: npm run ${cmd ?? "<cmd>"} -- <region> <caseId> [text]`);
    process.exit(1);
  }

  const ctx = await launchContext(cfg, region);
  const page = await firstPage(ctx);
  const now = Date.now();

  try {
    switch (cmd) {
      case "login": {
        await page.goto(caseListUrl(cfg, region), { waitUntil: "domcontentloaded" });
        console.log(`[${region}] Log in (with MFA) in the browser window. Session is saved to the profile.`);
        console.log("When the case dashboard is visible, press Ctrl+C here — the profile persists.");
        await page.waitForTimeout(10 * 60 * 1000); // 10 min to finish login
        console.log("loggedIn:", await isLoggedIn(page, cfg));
        break;
      }
      case "open":
        await openCase(page, cfg, region, caseId!);
        console.log("Opened. Inspect the DOM, then Ctrl+C.");
        await page.waitForTimeout(10 * 60 * 1000);
        break;
      case "thread": {
        const t = await getThread(page, cfg, region, caseId!, now);
        console.log(JSON.stringify(t, null, 2));
        break;
      }
      case "fill":
        await fillReply(page, cfg, region, caseId!, arg ?? "TEST DRAFT — not sent", now);
        console.log("Filled (not sent). Check the browser, then Ctrl+C.");
        await page.waitForTimeout(5 * 60 * 1000);
        break;
      case "send":
        await send(page, cfg, region, caseId!, now);
        console.log("Sent.");
        break;
      default:
        console.error("Unknown command. Use: login | open | thread | fill | send");
        process.exitCode = 1;
    }
  } finally {
    if (cmd === "thread" || cmd === "send") await ctx.close();
  }
}

main().catch((e) => {
  console.error(e);
  process.exit(1);
});
