# Amazon pricing editor

The new **Pricing** tab edits regular Amazon listing prices. **Pricing Sync**
keeps its existing currency synchronization workflow.

## Entering prices

Enter a positive **Default (EUR, required)** in the top table. An empty country
price uses that default converted to the country's local currency. A country
price overrides the default and is already expressed in that country's currency.

**Region** is a single selection: All regions, Europe, Americas, or Asia (cache
only). It limits the table, Main price, proposals, retrieval and submissions to
that region. The choice is saved with each filter preset; existing presets start
with All regions. Country selections are preserved when switching regions.
Sales/inventory totals use the active countries; totals from a different scope
stay unknown until retrieved. Exchange rates outside the region do not block
updates. Asia currently provides cached JP data only and cannot submit prices.

The **Use country** row selects the marketplaces to retrieve, display, filter,
and update. Configured European and American marketplaces are enabled initially.
The selections and editable exchange rates are saved in the working directory.
**Refresh exchange rates** loads the ECB EUR reference-rate feed and records its
date. Missing rates do not imply a 1:1 conversion and must be filled before Update.
These conversion inputs remain editable.

- **One price:** one current/proposed pair, expressed in EUR. Different country
  values appear as a range; the tooltip lists individual local prices.
- **Continent:** one pair for Europe and one for the Americas, expressed in EUR.
- **All country:** a current/proposed pair per enabled country in local currency.
- **Preview all prices:** a separate read-only view of every enabled country's
  prices for the current filtered rows.

**Main price** uses the first known current seller price in FR → DE → US → CA → JP
order, even if that country is not selected for updates. A non-EUR price appears
as `US · 60.00 USD (conv 30.00 EUR)` using the configured rate. Missing rates show
`(conv — EUR)` with a tooltip pointing to Refresh exchange rates; parity is never assumed. JP is
currently a cache-only reference: its rate can be edited/refreshed, but this pane
does not retrieve or update Japanese prices live.

**Changes to apply**, beside Main price, is the review summary in every mode.
It updates automatically when prices or the direction rule change. Click
**Preview all prices** for a separate per-country review. **Update** submits
after confirmation; it is not needed to calculate or preview proposals.
It groups eligible countries by proposed EUR amount and direction: for example,
green `↑ 25.00 € (DE / FR)` and red `↓ 25.00 € (US)` can appear in the same cell.
EUR amounts are rounded equivalents of the actual local proposals. Hover to see
each country's exact current → new local price, including manual edits. Only
changes that pass the submission rules appear; blocked manual edits, unchanged
prices, reset proposals and disabled countries are excluded. “No changes” means
that row has no eligible proposals. Both new columns are read-only and sortable;
Changes to apply sorts by the number of affected countries. Price revalidation
still occurs on Update, so a subsequently changed Amazon baseline can be skipped.

Automatic increases have a dark green background; decreases have a dark orange
background. Edit a New Price cell to override that row's proposal. In the grouped
modes, an edit is in EUR and is converted for the group's countries. In All country
mode, it edits only that country's local price. Manual edits have a dark blue
background, including a manual value blocked by the direction rule; the tooltip
explains that it will not be submitted.

**Increase price only** and **Decrease price only** restrict both automatic and
manual proposals. Equal prices are not submitted. Mixed increases and decreases
in an editable grouped New Price cell have a neutral background; Changes to apply
shows both directions separately, and Preview all prices shows individual countries.

**Reset pricing** cancels proposals for the selected SKUs in enabled countries.
Editing or clearing a New Price cell reactivates that cell/group. Defaults and
country rules belong to the saved filter; per-SKU manual edits and resets last
for the current session and survive sorting, filtering, and retrieval.

## Filtering and sorting

SKU and displayed title use case-insensitive substring matching. All filter
fields must pass; current-price bounds match when **any** enabled marketplace
falls within the inclusive bounds, after conversion to EUR. `Any` disables a
numeric bound. Brand and product type use the selected exact value.

Size bounds use French sizes or letter ordering (XS, S, M, L, XL, etc.). A combined
size such as 38/40 must fit wholly inside the bounds. French size conversions use
the existing adult clothing/shoe tables; unknown gender, age, category, or size
mappings are not guessed. Size tooltips show country equivalents where available.
This pane does not change Amazon size attributes.

Sales are units ordered in the trailing 90 days across enabled marketplaces;
failed marketplace reads leave the total unknown. Inventory combines relevant
pools once. Zero sales or zero stock means **0 inventory days** in this pane.
Other panes retain their existing inventory-days calculation.

Filter edits save automatically. Default can be edited but cannot be renamed or
deleted. Add new copies the current filter; Edit manages the selected preset.
Sorting uses numeric and date values, with unknown values last. Each display mode
remembers its last column/direction in the working directory's `settings.ini`;
new settings default to SKU ascending.

## Retrieval and submission

Cached rows appear without a network request. **Retrieve data** discovers seller
SKUs, preserves multiple SKUs sharing an ASIN, and fills missing/expired listing,
price, inventory, sales, and thumbnail data. Known metadata filters reduce
per-country reads; missing metadata is retrieved to determine membership.
French titles have priority, then English, then another available title.

Retrieval reports its current phase and completed/total checks, including the
90-day sales phase. The dialog shows elapsed time and time since the last activity.
Sales clients retain authentication and connections across SKUs; a large cold
cache still requires many per-country requests. Inventory is saved after each
50-SKU batch, and sales observations are saved after each successful response.
Cancel keeps the log open while reads stop, then enables Close. Read requests
have a 60-second hard deadline in addition to their transfer timeout; cancellation
aborts active inventory/sales/listing/image reads and interrupts discovery polling.
The dialog distinguishes cancelled, failed, and finished runs. Missing/failed
values stay unknown, and completed observations can be reused on the next run.

No reference/list price is used as a substitute for a current seller price.
Missing prices stay blank and cannot generate a submission.

**Update** prepares changes for currently visible rows only and shows their count.
After confirmation, each current price is read live again. A changed or unverifiable
baseline is skipped. The regular price changes while the fetched offer's other
fields, including discounts, price bounds, and business offers, are preserved.
Accepted submissions are logged as awaiting Amazon propagation, their proposals
are cleared, and their cached prices are invalidated. Retrieve again to observe
the resulting price. Cancel stops further work after the in-flight request ends;
it cannot undo a submission already sent.

## Cache compatibility and verification

The September 14 cache export remains untouched under
`cache/amazon/exports/20260914T173434Z/`. Existing Store/Sizing caches seed metadata
and thumbnails. New observations use account-scoped records under
`cache/amazon/v1/`, with individual timestamps, atomic writes, and exact SKU keys.
Catalog discovery/sales/live inventory use a 24-hour cache lifetime and listing
prices use one hour. Writes always revalidate price baselines live.

Pricing Sync and Discount share listing-price reads and invalidate observations
on successful submissions. Their existing formulas are unchanged. Store and
Marketplaces publish fresh inventory and per-marketplace sales through their
existing inventory source. Their refresh/invalidation behavior remains intact.
Report inventory has its own dataset and does not overwrite live inventory.
Legacy EU8 sales totals are reused only for that exact marketplace selection;
legacy mainland inventory is not presented as worldwide stock. Changing the
country selection reloads observations for the new scope. Cached listing bodies
are parsed again, so an old cached numeric price cannot bypass the no-list-price rule.

`PricingEditorTests` checks price rules, colors, conversion, filtering, sorting,
working-directory persistence, legacy import, cache isolation/expiry/corruption,
and the widget itself. `AmazonPricingApiTests` checks price-source parsing and
preservation of other offer fields. Live price submissions are not part of tests.

Exchange-rate source: [ECB euro reference rates](https://www.ecb.europa.eu/stats/policy_and_exchange_rates/euro_reference_exchange_rates/html/index.en.html).

Offline validation on 2026-09-15: full Release build succeeded; all 10 registered
CTest suites passed (12 seconds), including both new pricing suites. The pane was
also rendered offscreen for visual inspection. Amazon retrieval and submission
still need a live account smoke test; no live price changes were made.
- Region validation: Release and Qt Creator Debug builds passed; PricingEditorTests
  and PricingRetrievalTests passed. Offscreen layout inspected with Region beside Brand.

- Product type now uses ProductTypeSelector, a checkbox dropdown that stays
  open for multiple choices (mouse or Space/Enter). All means no type restriction.
  Filter.productTypes matches any selected exact type, ANDed with other filters;
  unknown type metadata is still eligible for hydration. Presets store `types`
  arrays and migrate legacy single `type` values. Rebuilding choices preserves
  selected values even when absent from the latest catalog.
- Multi-type validation: PricingEditorTests and PricingRetrievalTests passed,
  including mouse toggles, OR/AND filtering, legacy migration and persistence.


## 2026-09-16 — Detailed submission audit

- Supersedes the earlier count-only confirmation and SKU-only submission logs.
  Update confirmation now has Show Details with every SKU, country/marketplace,
  old → new local-currency price and direction. The same identities appear in
  planned, verifying, submitting, submitted, skipped, failed/unconfirmed and
  cancelled log entries. Changed-baseline skips include the fetched live price.
- Repository update saves timestamped audit logs in working-dir
  `pricing-update-logs/`, flushing each line. Final summary counts submitted,
  skipped, failed/unconfirmed and cancelled/not sent. File-write errors warn
  visibly and Copy log remains available. Submitted means accepted, not verified.
- Update now uses PricingProgressDialog in update mode. Cancel retains the log
  and waits for unwind, then offers Close. AmazonPricingApi's optional cancellation
  argument checks after auth/rate limiting before PATCH. An already-sent PATCH
  is not aborted; its result is logged before remaining updates are cancelled.
  Legacy API callers retain the default behavior.
- PricingFactory and a borrowed API network manager permit offline repository
  tests. Verified country/currency/old-new logs, live baseline skips, unavailable
  offers, rejected writes, persisted log equality, cancellation before reads,
  after verification, during rate limiting and during an in-flight write.
- Release and Qt Creator Desktop_Qt_6_8_3-Debug builds succeeded. All three relevant
  suites passed: AmazonPricingApiTests, PricingEditorTests, PricingRetrievalTests.
  No real Amazon price changes were made by these tests.

## 2026-09-16 — All price position and preview

- All (EUR) Price is now a fixed, read-only aggregate column immediately after
  Main price, before Changes to apply, in every mode and Preview all prices.
  It keeps the enabled-country/current-region EUR range, local-price tooltips,
  numeric sorting and existing `all:price` persistence key.
- One price retains only its dynamic New Price column; its old dynamic current
  price moved to the fixed AllPrice slot. Continent/All country retain their
  current/new pairs alongside the aggregate. Use columnForKey for mode-dependent
  columns instead of assuming a fixed column offset.
- Release and Qt Creator Debug builds succeeded; PricingEditorTests passed.

## 2026-09-16 — Live top-table edits and synchronized preview

- Top-table PriceDelegate now commits text edits live, keeps the cursor/selection
  stable across model notifications, and restores the pre-edit value on Escape.
  Bottom-table manual New Price edits keep their existing commit behavior.
- Preview all prices previously held a stale snapshot. Open previews now follow
  top controls/filter changes and source model data/reset/insert signals, including
  manual prices, resets and retrieval. TableAmazonPricing::setRows has optional
  preserveEdits=false for snapshot replacement; normal cache hydration still
  preserves edits by default.
- Real keyboard widget regression verifies immediate main/preview refresh before
  Enter, repeated edits, Escape rollback, successive manual changes, resets,
  reactivation and filter synchronization. Let queued editor-close events finish
  between edits; fixture must explicitly enable at least one marketplace.
- Investigation also found user's saved Increase-only rule with default 99.99
  and cached DE listings at 129.99 for the selected SKU family. That reduction is
  intentionally blocked; explained switching direction to user without changing
  saved rules. Committed main-table edits worked with eligible fixture data.
- Release and Qt Creator Debug builds passed; AmazonPricingApiTests,
  PricingEditorTests and PricingRetrievalTests passed. No live API writes.
