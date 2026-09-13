import { mkdirSync } from "node:fs";
import { writeFile } from "node:fs/promises";
import { join } from "node:path";
import { createHash } from "node:crypto";
import type { BrowserContext, Page } from "playwright";
import type { Config, Region, ReviewMarketplace, ReviewItem } from "./types.js";

const SWITCHER_CURRENT = ".dropdown-account-switcher-header-label-regional";
const SWITCHER_PAGE_PATH = "/account-switcher/default/merchantMarketplace";

function log(msg: string) {
  process.stderr.write(msg + "\n");
}

function emitReview(r: ReviewItem) {
  log(`@@review-result ${JSON.stringify(r)}`);
}

function md5Hash(str: string): string {
  return createHash("md5").update(str).digest("hex");
}

export async function currentMarketplace(page: Page): Promise<string> {
  const el = page.locator(SWITCHER_CURRENT).first();
  const text = await el.innerText({ timeout: 5000 }).catch(() => "");
  return (text ?? "").trim();
}

async function dumpDebug(page: Page, dir: string, tag: string): Promise<void> {
  mkdirSync(dir, { recursive: true });
  await writeFile(join(dir, `${tag}.html`), await page.content()).catch(() => {});
  await page.screenshot({ path: join(dir, `${tag}.png`), fullPage: true }).catch(() => {});
  log(`[reviews] debug dump: ${join(dir, tag)}.{html,png}`);
}

async function settle(page: Page): Promise<void> {
  await page.waitForLoadState("networkidle", { timeout: 8000 }).catch(() => {});
  await page.waitForTimeout(2000);
}

function reviewsUrlForRegion(domain: string, region: Region): string {
  if (region === "jp") {
    if (domain.includes("japan")) {
      return `https://${domain}/brand-customer-reviews/ref=xx_crvws_dnav_xx`;
    }
    return `https://sellercentral-japan.amazon.com/brand-customer-reviews/ref=xx_crvws_dnav_xx`;
  }
  return `https://${domain}/brand-customer-reviews/ref=xx_crvws_dnav_xx`;
}

const guardedContexts = new WeakSet<BrowserContext>();

function setupTabGuard(ctx: BrowserContext): void {
  if (guardedContexts.has(ctx)) return;
  guardedContexts.add(ctx);

  ctx.on("page", async (p) => {
    const pages = ctx.pages();
    if (pages.length > 1) {
      log(`[reviews] detected auxiliary tab — closing`);
      await p.close().catch(() => {});
    }
  });
}

/** Ensure exactly ONE tab is open in the browser context to prevent empty tab clutter. */
async function ensureSingleTab(ctx: BrowserContext): Promise<Page> {
  const pages = ctx.pages();
  for (let i = 1; i < pages.length; i++) {
    const p = pages[i];
    if (p) await p.close().catch(() => {});
  }
  return pages[0] ?? (await ctx.newPage());
}

export async function openReviewsFor(
  ctx: BrowserContext,
  domain: string,
  region: Region,
  mp: ReviewMarketplace,
  debugDir: string
): Promise<Page> {
  setupTabGuard(ctx);

  await ctx.addInitScript(() => {
    try {
      (window as any).open = (url?: string | URL) => {
        if (url) window.location.href = url.toString();
        return window;
      };
    } catch (_) {}
  }).catch(() => {});

  let page = await ensureSingleTab(ctx);
  const targetUrl = reviewsUrlForRegion(domain, region);

  log(`[reviews:${mp.country}] opening reviews page: ${targetUrl}`);
  await page.goto(targetUrl, { waitUntil: "domcontentloaded" });
  await settle(page);

  let current = await currentMarketplace(page);
  if (current === mp.countryName || !mp.countryName) {
    log(`[reviews:${mp.country}] already on target marketplace "${current || mp.country}"`);
    return page;
  }

  log(`[reviews:${mp.country}] page shows "${current}" — switching to "${mp.countryName}"…`);
  const switcherUrl = `https://${domain}${SWITCHER_PAGE_PATH}?returnTo=${encodeURIComponent("/brand-customer-reviews/ref=xx_crvws_dnav_xx")}`;
  await page.goto(switcherUrl, { waitUntil: "domcontentloaded" }).catch(() => {});
  await settle(page);

  const row = page.getByText(mp.countryName, { exact: true }).first();
  if (await row.waitFor({ state: "visible", timeout: 15_000 }).then(() => true).catch(() => false)) {
    await row.click().catch(() => {});
    log(`[reviews:${mp.country}] selected "${mp.countryName}", confirming…`);

    // Remove target="_blank" from form/links so clicking does not open new tabs
    await page.evaluate(() => {
      document.querySelectorAll('a[target="_blank"], form[target="_blank"], kat-link[target="_blank"]').forEach((el) => {
        el.removeAttribute("target");
      });
    }).catch(() => {});

    const btn = page.getByRole("button", { name: /select account/i }).first();
    if (await btn.count()) {
      await Promise.all([
        page.waitForNavigation({ waitUntil: "domcontentloaded" }).catch(() => {}),
        btn.click().catch(() => {}),
      ]);
    }
  } else {
    log(`[reviews:${mp.country}] "${mp.countryName}" not found on account-switcher page`);
    await dumpDebug(page, debugDir, `switcher-failed-${mp.country}`);
  }

  // Close any second tab that might have been spawned during account switch
  page = await ensureSingleTab(ctx);

  if (!page.url().includes("/brand-customer-reviews")) {
    await page.goto(targetUrl, { waitUntil: "domcontentloaded" });
  }
  await settle(page);

  current = await currentMarketplace(page);
  log(`[reviews:${mp.country}] landed on "${current}"`);
  return page;
}

export async function scrapeReviewsFor(
  ctx: BrowserContext,
  cfg: Config,
  region: Region,
  mp: ReviewMarketplace,
  dumpDir: string
): Promise<ReviewItem[]> {
  const rc = cfg.regions[region];
  const domain = rc.domain;
  const mpDumpDir = join(dumpDir, mp.country);

  let page = await openReviewsFor(ctx, domain, region, mp, mpDumpDir);

  const reviews: ReviewItem[] = [];
  const seenIds = new Set<string>();

  // Determine total pages from the page DOM
  const paginationInfo = await page.evaluate(() => {
    // 1. Check form input max attribute
    const input = document.querySelector('form input[type="number"]') as HTMLInputElement | null;
    if (input) {
      const maxAttr = input.getAttribute("max") || input.getAttribute("aria-valuemax");
      if (maxAttr) {
        const m = parseInt(maxAttr, 10);
        if (m > 0) return { totalPages: m };
      }
    }
    // 2. Check text "of N" in form or pagination
    const ofMatch = document.body.innerText.match(/Page\s+\d+\s+of\s+(\d+)/i);
    if (ofMatch && ofMatch[1]) {
      return { totalPages: parseInt(ofMatch[1], 10) };
    }
    // 3. Check kat-pagination total-items and items-per-page
    const kp = document.querySelector("kat-pagination");
    if (kp) {
      const total = parseInt(kp.getAttribute("total-items") || "0", 10);
      const perPage = parseInt(kp.getAttribute("items-per-page") || "10", 10);
      if (total > 0 && perPage > 0) {
        return { totalPages: Math.ceil(total / perPage) };
      }
    }
    return { totalPages: 1 };
  }).catch(() => ({ totalPages: 1 }));

  const totalPages = Math.min(Math.max(paginationInfo.totalPages, 1), 30);
  log(`[reviews:${mp.country}] detected ${totalPages} page(s) of reviews.`);

  for (let pageIdx = 1; pageIdx <= totalPages; pageIdx++) {
    log(`[reviews:${mp.country}] scraping page ${pageIdx}/${totalPages}…`);
    await dumpDebug(page, mpDumpDir, `page-${pageIdx}`);

    // Wait for review container elements to be visible
    await page.locator(".reviewContainer, kat-table, table").first()
      .waitFor({ state: "visible", timeout: 8000 }).catch(() => {});

    // Evaluate in page DOM without any nested named functions (prevents __name reference error)
    const pageReviews = await page.evaluate((countryCode: string) => {
      // Define __name if esbuild injects it into serialized code
      if (typeof (window as any).__name !== "function") {
        (window as any).__name = (f: any) => f;
      }

      const items: Array<{
        id: string;
        country: string;
        asin: string;
        productTitle: string;
        imageUrl: string;
        stars: number;
        date: string;
        link: string;
        title: string;
        text: string;
      }> = [];

      // Primary extraction: Seller Central .reviewContainer elements
      const containers = Array.from(document.querySelectorAll(".reviewContainer"));
      if (containers.length > 0) {
        for (const c of containers) {
          const testid = c.getAttribute("data-testid") || "";
          let reviewId = testid ? testid.replace(/^review-/, "") : "";

          // Star rating
          let stars = 0;
          const starEl = c.querySelector("kat-star-rating.reviewRating, kat-star-rating");
          if (starEl) {
            const val = starEl.getAttribute("value");
            if (val) stars = Math.round(parseFloat(val));
          }
          if (!stars) {
            const alt = c.querySelector(".a-icon-alt, [aria-label*='star'], [aria-label*='étoile']");
            if (alt) {
              const m = (alt.getAttribute("aria-label") || alt.textContent || "").match(/([1-5])/);
              if (m && m[1]) stars = parseInt(m[1], 10);
            }
          }

          // Date / author line
          let date = "";
          const dateEl = c.querySelector(".css-g7g1lz, [data-hook='review-date']");
          if (dateEl) {
            date = (dateEl.textContent || "").trim();
          }

          // Review title
          let title = "";
          const titleEl = c.querySelector('[id$="-title"], [data-hook="review-title"], h4, h5');
          if (titleEl) {
            title = (titleEl.textContent || "").trim();
          }

          // Review body
          let text = "";
          const bodyEl = c.querySelector('[id^="review-content-"], [data-hook="review-body"]');
          if (bodyEl) {
            text = (bodyEl.textContent || "").trim();
          }

          // Review link
          let link = "";
          const linkEl = c.querySelector('kat-link[href*="customer-reviews"], a[href*="customer-reviews"]');
          if (linkEl) {
            link = linkEl.getAttribute("href") || "";
            if (!reviewId) {
              const idMatch = link.match(/customer-reviews\/([A-Z0-9]+)/);
              if (idMatch && idMatch[1]) reviewId = idMatch[1];
            }
          }

          // Product details inside .asinDetail
          let asin = "";
          let productTitle = "";
          let imageUrl = "";
          const asinDiv = c.querySelector(".asinDetail");
          if (asinDiv) {
            const img = asinDiv.querySelector("img");
            if (img) imageUrl = img.getAttribute("src") || "";

            const h5 = asinDiv.querySelector("h5");
            if (h5) {
              const kl = h5.querySelector("kat-link");
              productTitle = (kl ? kl.getAttribute("label") : "") || (h5.textContent || "").trim();
            }

            // Extract child ASIN and parent ASIN
            const rows = Array.from(asinDiv.querySelectorAll(".css-yyccc7, div"));
            for (const r of rows) {
              const rText = r.textContent || "";
              if (rText.includes("Child ASIN")) {
                const parts = rText.split("Child ASIN");
                if (parts[1]) {
                  const m = parts[1].match(/([A-Z0-9]{10})/);
                  if (m && m[1]) asin = m[1];
                }
              }
              if (!asin && rText.includes("Parent ASIN")) {
                const parts = rText.split("Parent ASIN");
                if (parts[1]) {
                  const m = parts[1].match(/([A-Z0-9]{10})/);
                  if (m && m[1]) asin = m[1];
                }
              }
            }

            if (!asin) {
              const dpLink = asinDiv.querySelector('a[href*="/dp/"], kat-link[href*="/dp/"]');
              if (dpLink) {
                const href = dpLink.getAttribute("href") || "";
                const m = href.match(/\/dp\/([A-Z0-9]{10})/);
                if (m && m[1]) asin = m[1];
              }
            }
          }

          if (title || text || stars > 0) {
            if (!reviewId) {
              reviewId = asin + "_" + (title.slice(0, 15) + "_" + text.slice(0, 15)).replace(/\s+/g, "_");
            }
            items.push({
              id: reviewId,
              country: countryCode,
              asin,
              productTitle,
              imageUrl,
              stars,
              date,
              link,
              title,
              text,
            });
          }
        }
      }

      // Fallback: table rows
      if (items.length === 0) {
        const rows = Array.from(document.querySelectorAll("kat-table tr, table tbody tr"));
        for (const row of rows) {
          const rowText = (row.textContent || "").trim();
          if (!rowText || rowText.includes("Star rating")) continue;

          let stars = 0;
          const starMatch = rowText.match(/([1-5])(?:\.[0-9])?\s*(?:out of|\/|stars?|étoiles?)/i);
          if (starMatch && starMatch[1]) stars = parseInt(starMatch[1], 10);

          const imgEl = row.querySelector("img");
          const imageUrl = imgEl ? (imgEl.getAttribute("src") || "") : "";

          let asin = "";
          const asinMatch = rowText.match(/\b(B[0-9A-Z]{9})\b/);
          if (asinMatch && asinMatch[1]) asin = asinMatch[1];

          let link = "";
          const aLink = row.querySelector('a[href*="customer-reviews"]');
          if (aLink) link = aLink.getAttribute("href") || "";

          items.push({
            id: asin + "_" + rowText.slice(0, 30).replace(/\s+/g, "_"),
            country: countryCode,
            asin,
            productTitle: "",
            imageUrl,
            stars,
            date: "",
            link,
            title: "",
            text: rowText,
          });
        }
      }

      return items;
    }, mp.country);

    log(`[reviews:${mp.country}] page ${pageIdx}: extracted ${pageReviews.length} review(s)`);

    let newOnThisPage = 0;
    for (const r of pageReviews) {
      const stableId = r.id || md5Hash(`${r.country}-${r.asin}-${r.date}-${r.title}-${r.text}`);
      r.id = stableId;
      if (!seenIds.has(stableId)) {
        seenIds.add(stableId);
        reviews.push(r);
        emitReview(r);
        newOnThisPage++;
      }
    }

    if (pageIdx > 1 && newOnThisPage === 0) {
      log(`[reviews:${mp.country}] no new reviews on page ${pageIdx} — ending pagination.`);
      break;
    }

    if (pageIdx >= totalPages) {
      log(`[reviews:${mp.country}] reached last page (${pageIdx}/${totalPages}).`);
      break;
    }

    const firstIdBefore = pageReviews[0]?.id || "";

    // Navigate to next page using the form input and Go button
    const nextPage = pageIdx + 1;
    log(`[reviews:${mp.country}] navigating to page ${nextPage}…`);

    const pageInput = page.locator('form input[type="number"]').first();
    const goBtn = page.locator('form kat-button[label="Go"], form kat-button[form-action="submit"], form kat-button, form button').first();

    let navSuccess = false;
    if (await pageInput.count()) {
      await pageInput.fill(String(nextPage)).catch(() => {});
      await pageInput.press("Enter").catch(() => {});
      if (await goBtn.count()) {
        await goBtn.click().catch(() => {});
      }
      navSuccess = true;
    }

    if (!navSuccess) {
      // Fallback: click next in kat-pagination or direct page button
      const nextBtn = page.locator(
        `kat-pagination button:has-text("${nextPage}"), ` +
        'kat-pagination button[aria-label*="next" i], kat-pagination [aria-label*="next" i], ' +
        'kat-pagination button:has-text(">"), li.a-last a, button[aria-label*="next" i]'
      ).first();
      if (await nextBtn.isVisible().catch(() => false)) {
        await nextBtn.click().catch(() => {});
        navSuccess = true;
      } else {
        log(`[reviews:${mp.country}] no further pagination controls available.`);
        break;
      }
    }

    // Wait for the reviews container to reflect the new page (first item changes)
    if (firstIdBefore) {
      await page.waitForFunction(
        (oldId) => {
          const first = document.querySelector(".reviewContainer");
          if (!first) return false;
          const testid = first.getAttribute("data-testid") || "";
          return testid && !testid.includes(oldId);
        },
        firstIdBefore,
        { timeout: 5000 }
      ).catch(() => {});
    }
    await settle(page);

    // Keep single tab in case any click created a tab
    page = await ensureSingleTab(ctx);
  }

  log(`[reviews:${mp.country}] finished: total ${reviews.length} review(s) collected.`);
  return reviews;
}
