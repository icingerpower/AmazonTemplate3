# PanePricing analysis and implementation proposal

**Historical design notes.** Implementation and confirmed behavior as of
2026-09-15 are documented in [pricing-editor.md](pricing-editor.md). Later user
decisions supersede the pending questions and the zero-sales/infinity proposal below.

Date: 2026-09-14. Based on the current working tree, including the in-progress
split between Pricing and Pricing Sync. This document proposes changes; it does
not change runtime behavior. Open product questions were sent to the user.

## Follow-up decisions and initial work

The user approved starting from an export of existing caches and completing
missing data. Prices must never fall back to reference/list prices. The table
requires a separate Price / New Price pair for each marketplace, rather than
one pair for the whole row. Marketplace selection, price generation, and filter
semantics remain pending; the single-marketplace table interpretation below is
superseded by these paired marketplace columns.

An immutable cache export was created and hash-verified at:
`/home/cedric/Dropbox/freelancers/projects/workingDirectory/amazonTemplate3/cache/amazon/exports/20260914T173434Z/`.
It contains 2,013 cache files totaling 7,034,216 bytes, plus `manifest.json`.
The export includes marketplace Store metadata, Sizing SKU maps, Store
thumbnails, a legacy `stores/sales.json` snapshot, and selected stock-cache keys
decoded from QSettings into `stock-caches.json`. No full settings file or
credentials were copied. The originals were verified unchanged.

The Marketplaces inventory/sales cache is timestamped 2026-09-02; Store's stock
cache is timestamped 2026-08-12. These exports seed discovery and preserve old
observations; export time must not renew their freshness. The legacy
`stores/sales.json` is an archive candidate only: no current source-code reader
was found, so its semantics must be established before importing its values.

The shared `AmazonPricingApi` parser now rejects reference-only prices. An
offline Qt test covers current offer shapes, explicit seller price, wrong
marketplace, reference-only data, missing data, and product-type selection.
The new shared retrieval layer and Pricing UI are not implemented yet.

## Current implementation

- `AmazonTemplate3/gui/panes/PanePricing.cpp` only calls `setupUi` and deletes
  the UI. No model, filters, retrieval, reset, update, or settings are wired.
- `PanePricing.ui` contains the requested filtering area and table placeholder.
  Sorting is not enabled. SKU/title fields, brand/product-type choices, size
  bounds, price bounds, minimum inventory days, saved presets, and direction
  radio buttons have no behavior yet.
- Price bounds are integer `QSpinBox` widgets without explicit ranges. Use
  decimal-capable controls, meaningful limits, and an explicit unrestricted
  state. The inventory-days control also needs an explicit range.
- Fix the `Titile` label, name the generic title `lineEdit`, and name the empty
  lower container widget when implementing the pane.
- `MainWindow.cpp` already installs separate Pricing and Pricing Sync tabs.
- The existing `TablePricing` belongs to Pricing Sync: DE base price plus
  dynamic country groups. Keep it intact and add a separate model for this pane.

## Table contract

Interpretation pending confirmation: 11 columns, with sales and size separate.

| Column | Data / proposed behavior |
| --- | --- |
| Image | Asynchronous thumbnail; reuse matching image cache. Use SKU as a deterministic tie/order key if its header is clicked. |
| SKU | Exact seller SKU, not an ASIN-derived representative SKU. |
| Title | Nonempty French title, then English, then available source title. Retain language/marketplace provenance. |
| Last 90 days sales | Numeric unit count; record marketplace coverage, interval, source, and completeness. Unknown differs from zero. |
| Size | Raw display size plus a sortable/filterable normalized value; ordering rules await clarification. |
| Est. days inv. | Proposed reuse of `estimatedDaysOfSupply` in `MarketplaceTypes.h`: rounded available × 90 / sales90; zero stock = 0; no sales with stock = infinity; missing inputs = unknown. Inventory and sales scopes must match. |
| Color | Existing listing attributes, with marketplace/language retained. |
| Price | Current price for the target marketplace and currency; preserve the source field and read timestamp. |
| New Price | Optional proposal, green above current price, orange below it; equal/cleared proposals remain neutral and do not produce updates. Generation/editing policy awaits clarification. |
| Creation date | Proposed seller-listing creation date on the target marketplace, from listing summaries. Sort as a date. |
| ASIN | ASIN associated with this SKU/marketplace. |

Use `TableAmazonPricing : QAbstractTableModel` plus a dedicated
`QSortFilterProxyModel`. Keep numeric/date sort roles separate from formatted
display strings. Use SKU as a stable tie-breaker and place unknown values last
in either direction. Represent infinity explicitly in the new model: the
existing helper's `999` sentinel also collides with a real 999-day estimate.

Enable header sorting and persist the stable column identifier plus ascending /
descending order through `WorkingDirectoryManager::instance()->settings()`:
`pricingEditor/sortColumn` and `pricingEditor/sortOrder`. This writes the chosen
working directory's `settings.ini`, unlike default-constructed `QSettings`.
Restore after assigning the model; absent/invalid settings fall back to SKU
ascending. Validate saved column IDs when the schema changes.

Keep proposals keyed by seller, marketplace, and exact SKU so sorting,
filtering, refreshes, or a disappearing row cannot apply an edit to another SKU.
Map selected proxy indexes to source rows for Reset. Snapshot eligible row
identities before starting asynchronous Update.

## Existing data and cache reuse

Paths below are relative to the user's selected working directory, not the
source checkout.

| Consumer | Existing storage / retrieval | Reuse opportunity |
| --- | --- | --- |
| Sizing / Warnings | `sizing/sku_cache_{marketplaceId}.json`: ASIN → SKU. Sizing writes it; Warnings reads it and falls back to other marketplaces/product settings. No stored retrieval timestamp. | Read as discovery hints; do not treat as complete SKU enumeration or verified listing existence on another marketplace. |
| Store | `stores/{marketplaceId}.json`: SKU, ASIN, title, brand, product type/category, color, size, image URL, creation date, and other Store fields. | Useful metadata seed, but no explicit freshness/account metadata. Preserve Store's manual categorization. |
| Store / Sizing | `stores/thumbs/{ASIN}.jpg` and `sizing/{ASIN}-*/{ASIN}_main.jpg`. Store already checks Sizing images before its thumbnails. | Reuse existing matching assets; new cache entries should retain marketplace and image URL identity to avoid wrong variation/localized images. |
| Store | `settings.ini`, `PaneStoreStockCache/{timestamp,available,sales90,sales365}`. Sales cache has a 24-hour global timestamp; refresh always calls the bulk inventory source. | Share per-SKU inventory and 90-day sales after adding source/scope metadata. No need to retrieve 365-day sales for Pricing. |
| Marketplaces | `settings.ini`, `AmazonCache/{timestamp,inventory,sales}`. 24-hour global timestamp; fetches missing inventory live and missing/failed sales separately. | Best existing partial-refresh behavior to preserve. Keep inventory-source abstractions for future Octopia support. |
| Pricing Sync | `_onRetrieve`: uncached FBA report, then DE price and selected-country prices for each SKU. | Share inventory and listing-price reads; preserve its DE-based conversion workflow. |
| Discount | `_onLoad`: uncached planning report, candidate sales, preferred/fallback titles, and per-marketplace prices. | Share report documents, sales with the exact same scope, listing snapshots, and localized titles. Keep eligibility and sale-price behavior pane-specific. |

There is no common persistent price cache in these retrieval flows today.
The shared APIs already exist; the missing layer is shared storage, retrieval
coordination, and records that describe what the data means.

## Correctness constraints found in source

1. `AmazonCatalogApi::fetchAllSkusViaReport` keeps the first non-refurbished SKU
   per ASIN. Two seller SKUs for one ASIN therefore collapse. Add a SKU-preserving
   listing enumeration method for Pricing; keep the old method as a compatibility
   adapter so existing ASIN-oriented callers retain their behavior.
2. Store persists a single title per marketplace file. Its category can include
   manual organization. Cache raw API product type separately from Store's
   presentation category; obtain PATCH product type from the target listing.
3. Store's listing parser handles `size` / `size_name`, but not every composite
   size shape. Extend new snapshot parsing using fixtures for relevant product
   types; avoid guessing regional size conversions.
4. `AmazonFbaInventorySource` sums 90-day sales over eight explicit marketplaces
   (DE, FR, IT, ES, NL, SE, PL, BE). Discount sums whichever countries are checked.
   These totals are interchangeable only when scopes and intervals match.
5. `AmazonInventoryApi::fetchSalesUnits` returns a positive/zero total when any
   marketplace succeeds, silently skipping failed marketplaces. Shared records
   must retain successful/failed coverage; a partial sum is not a complete total.
   The planning report's shipped-sales field is a separate metric/source.
6. Existing stock caches have one timestamp for the whole map. Saving a subset
   can make older, untouched entries look fresh. Use timestamps per record and
   dataset; never reset another record's freshness while merging.
7. Legacy stock caches lack seller/source/marketplace-set identity; some keys
   lowercase SKUs. Do not silently map ambiguous or case-colliding legacy keys
   into an exact-SKU pricing record. Preserve exact identifiers for writes.
8. `AmazonPricingApi` requests `offers,attributes,summaries`, enough to parse
   price and much of the required metadata from the same response. Its current
   parser falls back to `attributes.list_price`; preserve price provenance so
   a reference/list price is not silently presented as a verified current offer.
9. Failed pricing requests and 404 both leave `existsOut=false`; check the error
   as well. A cache needs distinct missing, failed, stale, and successful states.
10. `patchListingPrice` sends `replace` for `purchasable_offer` with only the new
    regular price. Reusing it requires reviewing discount schedules, other offer
    fields, and min/max behavior. Preserve existing Discount behavior. Its
    Boolean success checks HTTP/INVALID, not read-back persistence; distinguish
    submitted/accepted from verified price changes in the new workflow.

## Proposed incremental refactoring

Add a shared `AmazonDataRepository` under `AmazonTemplate3Lib`, backed by an
`AmazonDataCache`. Keep model/filter/proposal state outside this repository.
Construct or inject one repository per working directory/account context and
share it between panes; no pane should depend on another pane's widget.

Suggested new storage:

```text
{workingDir}/cache/amazon/v1/{accountKey}/
  listings/{marketplaceId}/...
  inventory/{sourceAndPoolKey}/...
  sales/{marketplaceId}/{windowKey}/...
  reports/{reportType}/{scopeKey}/...
  images/...
```

Use hashed/encoded identities for filenames, never raw SKUs as paths. Store
schema version, seller identity, exact SKU, marketplace or inventory-pool scope,
retrieval time, source, and completeness. Keep currency and language on relevant
records. Sales records include the actual start/end interval and metric; freezing
the requested interval once per retrieval avoids small per-SKU window drift.
Pool inventory stays separate from per-marketplace listings and sales. Never
sum a shared pool once per country. Scope definitions should be explicit and
validated rather than inferred solely from a region label.

Use atomic writes with `QSaveFile`, merge records rather than replacing another
pane's subset, and serialize writers (including a file lock if multiple app
instances share a working directory). Retain last good values on failures,
marked stale/incomplete. Coalesce identical concurrent requests and share request
throttling so opening two panes does not duplicate reports or amplify traffic.

Migration sequence:

1. Add typed records, cache, injectable retrieval interface, and offline fixture
   tests. Add a listing-snapshot method that parses one response into both price
   and metadata; retain existing public API behavior through adapters.
2. Implement the new Pricing model and retrieval against this layer. Read old
   cache formats conservatively as hints, with unknown freshness/account scope;
   revalidate before using them for pricing decisions. Do not rename/delete or
   rewrite Store/Sizing files or merge Store's manual state into raw API records.
3. Route Pricing Sync and Discount reads through the layer separately. During
   each migration, preserve existing calculations, filters, and write payloads;
   add targeted parity tests before switching that pane.
4. Adapt Store/Marketplaces inventory and sales access while preserving
   `AbstractInventorySource` / `AmazonFbaInventorySource` and existing cache
   invalidation controls. Maintain legacy readers until all consumers, including
   Warnings, have migrated. Remove duplication only after parity is established.

This limits regression exposure; no refactor can guarantee no regressions
without validation against the existing workflows.

## Retrieve data and update flow

Proposed Retrieve behavior: display cached rows immediately with freshness,
then retrieve missing/stale inputs for the chosen scope. Apply cheap SKU and
available metadata filters before costly sales/price enrichment, while fetching
unknown fields needed to decide filter membership. Refresh listing discovery
when incomplete/stale so cache-only filtering cannot hide new SKUs forever.
Load thumbnails lazily and fetch English titles only when French is absent.
Offer a forced refresh without clearing other panes' caches.

Start with separate freshness policies: keep the existing 24-hour sales policy
as a compatibility baseline, refresh current prices for candidate updates, and
reuse relatively stable catalog/image data longer. Exact TTLs remain tunable;
one global timestamp is unsuitable for all of these datasets.

Update should capture the chosen scope and row identities, validate proposed
prices and target product types, refresh stale price baselines, and report
per-row results. An accepted update invalidates the corresponding shared price
record and notifies consumers; read-back establishes the new observed price.
Do not silently recompute/submit proposals when the baseline or scope changes.
Reset affects only selected proposals, not Amazon or the shared observations.

## Clarifications pending

- Target marketplace(s), currency presentation, and FBA-only versus all listings.
- Pricing formula/manual editing and the meaning of direction radio buttons.
- Size ordering and interpretation of composite sizes/ranges.
- Minimum inventory-day comparison and handling of zero sales/unknown coverage.
- Text matching, title languages, inclusive current-price bounds, empty bounds.
- Preset autosave, included fields, and whether Default is editable.
- Sales/stock scope, the 11-column interpretation, creation-date definition, and
  whether Update includes only visible changed rows.

## Targeted validation when implementing

- Numeric/date/size sorting, stable ties, unknown/infinite ordering, and saved
  sorting restored from two independent temporary working directories.
- French → English title fallback and missing/composite listing attributes.
- Price proposal colors, rounding, reset after sorting, filter changes during
  retrieval, and correct source-row identity during updates.
- Filter boundaries and each clarified zero/missing-data rule.
- Every SKU retained when several share an ASIN; case-colliding SKUs preserved.
- Cache isolation by seller/marketplace/pool/interval, exact expiry boundaries,
  partial refresh without timestamp extension, corrupt files, failed requests,
  incomplete marketplace sales, and legacy cache reads without mutation.
- Shared in-flight retrieval, cancellation/lifetime handling, and invalidation
  after an accepted price submission without claiming read-back success.
- Fixture parity for existing API parsers and migrated pane workflows; targeted
  Qt tests, worker checks only if worker code changes, and a GUI smoke test.

No live account operation is needed for these unit tests. This analysis itself
does not require an application rebuild.
