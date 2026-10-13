---
name: project-panestore
description: PaneStore working directory layout and data storage convention
metadata:
  node_type: memory
  type: project
  originSessionId: 82927c62-a29c-41fb-a8e6-ccdfd47bccbf
---

PaneStore saves its retrieved Amazon catalog data in the subfolder `stores/` under the working directory.

**Why:** Keeps store catalog snapshots (brand/category/gender/age per ASIN) persistent across sessions so the user doesn't have to re-fetch from Amazon every time.

**How to apply:** When working on PaneStore or any code that writes store data, files go to `{workingDir}/stores/{marketplaceId}.json` (e.g. `stores/A1PA6795UKMFR9.json` for EU Germany). The marketplace ID used as filename comes from QSettings key `store/marketplaceId`, defaulting to `A1PA6795UKMFR9`.

## 2026-10-13 — Resizable product-table columns

- All product-table columns use interactive header resizing, including Title (previously forced to Stretch). Initial widths are fitted once when nonempty data first appears, with a 58-pixel image column and title width bounded to 240–600 pixels. Category changes and table refreshes retain user-adjusted widths for the lifetime of the pane; widths are not saved across application restarts. Header boundary double-click remains available to fit a column to its contents.

## 2026-10-11 — Discover US, Canada, and Japan; merge by ASIN

- Previously Retrieve discovered only the primary marketplace (Germany by default); country selection did not change discovery. Retrieve now requests separate merchant listings reports for the primary marketplace plus US, CA, and JP, removing repeated marketplace IDs. Regions lacking client credentials, refresh token, or seller ID are skipped with a warning.
- Reports merge by ASIN, including when countries use different SKUs. Existing SKUs are preferred when still present, otherwise primary/US/CA/JP report priority determines the canonical SKU. Inventory comes from that canonical source rather than summing potentially shared stock. Attribute retrieval uses the source marketplace's endpoint and regional marketplace IDs, with another reported source as fallback. Report membership contributes availability across regions.
- The existing primary-marketplace JSON remains the combined cache; placements, archived deletions, and saved orders retain their formats. Failed/skipped reports retain absent cached items and cached availability; a wholly empty retrieval leaves the saved catalog intact. Complete retrieval still replaces the discovered catalog and filters variation parents and deleted originals.
- Fixed the Listings summaries loop returning early on the primary summary: it now collects all returned marketplace IDs while selecting the primary summary's metadata.
- Offline `StoreTests` HTTP fixtures cover report/endpoint routing, CA-only ASIN `B0DCZLPXTY`, US/JP-only products, duplicate ASINs with different regional SKUs, repeat retrieval, stock not being summed, category overrides/order/deletion persistence, metadata fallback, missing credentials, failed reports, and later marketplace summaries. No live Amazon requests are needed.

### 2026-10-13 — Boot variants split into multiple rows (diagnosed, not fixed)

- Local catalog inspection of the reported denim boots found distinct child ASINs with SKUs such as `CJBHNSNS18837-Light Blue-43-NARROW`, `...-39-NARROW`, and `...-35-NARROW`. `TreeBrandCategories::colorGroupKey` derives families from SKU suffixes rather than parent relationships; it recognizes a final size token but not a final NARROW/WIDE token, so the embedded sizes remain in the family key and each gets its own row.
- Exact catalog colors also differ (`Blue`, `Denim blue`, `Light Blue Denim`), splitting otherwise related rows. Some black boot records share the blue boot thumbnail URL, so image similarity alone is not reliable evidence of identical variants. Cross-market ASIN deduplication does not merge distinct child ASINs into a family.
- Remove archives the selected row's visible group ASINs; Retrieve filters these archived originals in `_applyItems`. New/different ASINs under the same parent are not blocked, because deletion is by ASIN, not parent. Deleting extra rows hides their variants and excludes their counts from the retained row; it does not consolidate groups. No grouping implementation change was made during this diagnosis.

## 2026-10-12 — Global sales ranking, independent of fulfillment stock

- The previous retrieval change did not extend sales. Store's annual sales still came from `AmazonFbaInventorySource`'s eight EU marketplaces, and Sort only reordered cached numbers. The refreshed catalog contained 37 tennis-dress ASINs in eight groups, but none had a cached annual count.
- `StoreSales` now counts non-canceled ordered units across all configured marketplaces, including GB, IE, US, CA, MX, and JP. It uses `GET_FLAT_FILE_ALL_ORDERS_DATA_BY_ORDER_DATE_GENERAL`, grouped by SP-API region with all that region's marketplace IDs. A trailing 365-day snapshot is split into 13 nonoverlapping periods of at most 30 days per region. Amazon's report limit is documented at https://developer-docs.amazon/sp-api/docs/report-type-values-order. Both fulfillment channels and all seller SKUs are included; pending orders are included, canceled orders/items and non-Amazon MCF channels excluded. This is not net revenue or units after returns.
- The parser attributes rows by sales-channel, filters explicit `[start,end)` purchase timestamps, deduplicates order items (order-item-id when present, otherwise order+SKU+ASIN), and rejects malformed/inconsistent rows rather than reporting false zeroes. Regional request construction, HTTP failures, and parsing remain in `AmazonCatalogApi`; report creation has bounded 429/5xx retries and all report waits support cancellation.
- `stores/sales_v2.json` is separate from the legacy EU SKU cache and contains only aggregated ASIN counts, not raw order records. Its scope fingerprint includes configured marketplace IDs/seller identities. Successful periods are saved atomically and replaced rather than added on retry. Within 30 days, retries fetch missing periods only; all category views reuse the same catalog-wide snapshot. Data older than 30 days refreshes the rolling window automatically (supersedes the initial 24-hour policy).
- Sort by sales now retrieves missing/stale global counts before sorting; Refresh sales & stock also retrieves them before the independent inventory/90-day source-demand refresh. Annual sales no longer use `PaneStoreStockCache/sales365`. Global counts do not drive EU stock-day/4-month coverage calculations.
- Missing totals show an em dash; incomplete or stale numeric totals show `*` with an explanatory tooltip. Partial totals can be ranked but are never presented as complete. Old in bottom only treats a complete zero-sales total as dead stock. No live Amazon report was used to validate this change.
- Offline `StoreTests` covers Canadian sales, regional SKU merging, duplicate order lines/ASINs, exact period boundaries, canceled orders, malformed reports, empty successful reports, failed-region retries without recounting successful periods, fresh cache reuse, account isolation, ranking, cancellation, and pane destruction during retrieval. Existing placement/order/deletion regressions remain in place.
- Validation: full Release and the configured desktop Debug executable built successfully; all 17 registered CTest suites passed offscreen (38.82 seconds). No commit or live sales refresh was performed.

### Follow-up — sales progress display

- The sales dialog now has a determinate progress bar with completed/total report periods and percentage. Cached successful periods are included in its initial value on retry; failures never advance it or force it to 100%.
- Report callbacks show request, Amazon queue/preparation, polling, throttle retry, download, and counting stages, with country codes instead of raw marketplace IDs. These updates do not estimate progress inside Amazon's report generation.
- Sort bypasses retrieval for complete snapshots up to 14 days old. After 14 days through 30 days, a Yes/No dialog offers a refresh: No sorts cached totals, Yes forces a new rolling snapshot. After 30 days, retrieval runs automatically. Missing/incomplete data still triggers retrieval. This policy applies to global annual sales; the separate stock/90-day-demand cache retains its 24-hour lifetime. The offline progress regression checks partial failure at 13/26, reloading and resuming at 13/26, and completion at 26/26 without refetching successful periods. Five offline age-policy cases exercise recent-cache reuse, accepting/declining the two-week prompt, declining near 30 days, and automatic refresh beyond 30 days.

- Follow-up validation: Release and the desktop Debug executable rebuilt successfully; `StoreTests` passed offscreen including the progress and cache-age scenarios (4.46 seconds). No live Amazon requests were made.

### Follow-up — apparent refresh after moving a category

- Read-only inspection on 2026-10-12 found the user's current sales snapshot had 26/39 valid saved periods: all 13 North American and all 13 Japanese periods, but zero of 13 European periods. Sort therefore retries the missing EU periods even though the snapshot is recent. The cache does not persist the previous error, so it does not establish why EU retrieval failed. Moving products only saves catalog/category state and does not invalidate the sales cache.
- The running desktop process still held the replaced Debug executable from before the progress-bar update; the newer executable was already on disk. Restart is needed to see the progress bar and country names. The old dialog's `1/39` label counts traversal from the first missing EU period and does not mean the 26 saved periods were discarded.

### 2026-10-13 — Turkish sales-channel row blocked European periods

- User-supplied logs resolved the previously unknown EU failure: report generation/download succeeded, but `parseSalesReport` rejected `amazon.com.tr` because Turkey is outside the app's marketplace registry. One such row discarded the whole EU period, and the region loop stopped, leaving progress at 26/39.
- Parser now builds its channel lookup from the requested marketplace IDs and ignores other channels, including unconfigured Amazon storefronts and MCF. Unsupported *requested* IDs still fail explicitly; malformed rows for requested storefronts still fail. This does not add Turkey to the configured marketplaces or include Turkish sales in totals.
- An offline parser regression reproduced the exact Turkish-channel error before the fix. The report/cache progress regression now uses three regions: 26/39 after EU failure, then 39/39 after retrying only 13 EU periods, with Turkish rows in the fixture excluded and saved CA/JP counts retained without duplication. No live Amazon requests or cache edits were made.

### 2026-10-13 — Old in bottom resolves incomplete sales first

- With every annual total incomplete (`0*`), the old button's `salesComplete && salesYear == 0` predicate left old rows in place ahead of recent/stocked rows. It was a stable partition, not date sorting; it did not actively move the recent rows downward.
- The button now retains an async task that awaits the same sales refresh/cache policy before applying the existing age/sales/inventory partition. Failure leaves ordering unchanged with an explicit explanation that `0*` is not confirmed zero. Cancellation, pane destruction, and category/working-directory changes abort the subsequent reorder. The stock predicate and relative order within each partition are unchanged.
- Offline coverage starts at 26/39 saved periods with an old zero-sale row above a recent stocked row, then checks missing-EU-only retry and correct/persisted ordering, cache reuse, failure messaging, and cancellation. Existing complete-cache partition coverage remains.

## 2026-09-23 — Multiple category placements feasibility (not implemented)

Historical assessment; superseded by the implementation entry below.

- Current `PaneStore` keeps one `StoreItem` per ASIN in `m_asinToItem`; each item has one brand/category/gender/age path. Simply copying catalog records would collide in this lookup. Move changes matching ASINs' path fields; Remove deletes matching ASINs from the local catalog.
- A visible table row represents a product-family/color group, potentially containing multiple size ASINs. `_buildAsinGroups()` expands the selected row for Move/Remove; duplication should preserve this grouping too.
- Proposed design: persist additional category placements separately from canonical catalog items, identify actions by placement, and mark extra placements dark blue in `TableStoreAsin`. This supports copying to another category without moving the source and moving/removing an extra placement independently. This is a proposal only.
- Account for Retrieve (rebuilds catalog items), category merge/removal, and saved ordering (currently global by color-group key). Independent category ordering needs category/placement scope.
- `TreeBrandCategories` aggregates ASIN lists upward without deduplicating; multiple placements must deduplicate ancestor views before stock/sales aggregation, counts, and export. Existing color-group counts already use sets.
- Custom nodes are saved in `stores/custom_paths.json`; the current catalog tree has fixed brand/category/gender/age levels. Pumps, Low heels, and Square heels can be sibling categories under a brand.
- PaneStore's examined flows persist local organization and export category assets/ASIN lists; changing the local tree does not itself publish storefront navigation.

## 2026-09-23 — Multiple category placements implemented

- `StorePlacements` owns category membership overrides independently of canonical catalog data. `PaneStore` still keeps one `StoreItem` per ASIN. Overrides are saved as an optional `placements` array (`path`: four raw strings; `duplicate`: boolean) inside each item in the existing marketplace JSON, so catalog and memberships are written atomically together. Old files without this field keep their original behavior.
- Duplicate beside Move uses the same destination picker and expands selected product/color groups to their visible size ASINs. It retains source placements and deduplicates destination paths. Original placements use canonical item paths; extra placements retain their own paths when catalog attributes are refreshed.
- Move/Remove operate only on placements under the current node. At an ancestor node this includes all placements within that subtree. Removing an original leaves extra placements and shared data intact; removing the last placement removes the local catalog item, as before. Retrieve may restore a fully removed item, as in the old implementation.
- Extra-only rows initially used dark blue text (superseded by the light blue change below), with red stock warnings taking precedence. Ancestor views show a group normally if any visible original placement exists. Tree ancestor ASIN lists are deduplicated before grouping, stock/sales aggregation, and export.
- Order files accept old ASIN/group-key arrays and now save an object with `legacy` fallback order plus `nodes` orders keyed by display path joined with `\x1f`. Reordering one node no longer changes another node's order.
- Merge is scoped to the selected subtree, combines colliding placements, and remaps custom paths, English names, and saved node orders. Removing a custom category removes its extra placements and reassigns originals to `(unknown)` with `manuallyMoved=true`, preserving that decision on Retrieve. Add category is limited to the supported four tree levels.
- `StoreTests` is registered with CTest (offscreen, 30-second timeout), with 13 offline scenarios covering legacy grouping/move/remove/copy, duplicate move/delete/color, stock/count deduplication, persistence and simulated refresh, original deletion, collisions, per-category order migration, category removal/merge, cancellation/directory isolation, mixed selections, ancestor scoping, export with a local no-op CLI, and metadata validation. All fixtures/settings are isolated in temporary directories; no real Amazon or AI calls are required.

### Follow-up — light blue and destination brand restriction

- Duplicate text now uses light blue (`#87CEFA`); red stock warnings still take precedence.
- Both Move and Duplicate show only descendants of the selected products' canonical brand in the destination picker. The picker validates the brand again on acceptance and rejects mixed-brand selections; the unknown-brand placeholder maps back to the empty raw brand. Category/gender/age destinations remain supported.
- This restriction applies to product Move/Duplicate in PaneStore; the lower-level placement remapping used by explicit category/brand Merge remains available. Existing placements are not migrated or removed.
- Four additional data-driven UI regression cases cover Move/Duplicate for named and unknown brands, including rejecting an out-of-brand index before accepting an allowed category.

### Follow-up — preserve navigation during product actions

- Product Remove, Duplicate, and Move now call `_refreshAfterProductAction()`. They retain the source selection, expanded/collapsed branches, sibling order, and tree scroll position rather than resetting the tree or navigating to the destination.
- `TreeBrandCategories::setItems(..., preserveNodes=true)` updates existing node membership/counts in place, emits row insertions only for newly needed nodes, and emits data changes for counts. Existing empty nodes are retained for continued navigation during product actions; ordinary reloads and explicit category changes still rebuild the tree normally. Empty catalog-derived nodes are not added to persisted custom paths.
- English-name persistence now filters dataChanged for EditRole so count refreshes do not rewrite the English-name file.
- Six additional UI cases exercise all three actions with remaining products and with the last product, checking stable persistent indexes, no model resets, unchanged branches/order/selection/scroll, and refreshed source counts/table rows. Existing transfer tests now explicitly navigate to the destination only after asserting that the action stayed at the source.

## 2026-10-02 — Deleted originals and category navigation

- Supersedes the earlier statement that Retrieve can restore fully removed originals: `TreeDeletedProducts` owns deleted original snapshots and displays category/product nodes. `View deleted` beside product Remove opens a QTreeView dialog; `Cancel deletion` restores selected products or subtrees while retaining surviving duplicate placements.
- Deleted snapshots use `deleted: true` records in the same marketplace JSON array, saved atomically with live items. A live duplicate-only record and archived original can share an ASIN. Archived records carry no placement overrides. Loading resets deletion and placement state even when the marketplace cache is missing.
- Retrieve filters deleted originals without surviving overrides and saves the filtered list. Duplicate removals are never archived; deleting the last duplicate after deleting its original cannot resurrect the original on refresh/reload.
- Category Remove removes only its model subtree and refreshes membership in place, preserving surviving indexes and expansion. Existing original-to-unknown reassignment remains; category removal itself does not delete originals.
- Offline StoreTests cover archive/reload/refresh/restore via the dialog, cancellation, duplicate exclusion, surviving duplicates, marketplace/directory isolation, and category expansion with model invariant checks.

## 2026-10-05 — Move full selection to top/bottom

- Both edge-ordering buttons move every selected product row, preserving the relative order of selected and unselected products. Persistent model indexes track rows across moves; the resulting block stays selected, and the existing per-category order persistence is retained.
- StoreTests adds 14 offline cases covering both directions with disjoint, contiguous, edge, all, single, and empty selections, including selection retention and reloaded ordering. Single-step Up/Down behavior is unchanged.

## 2026-10-05 — Representative footwear sizes and saved work

- A displayed size is a representative for the whole product/color group, not an extra size row. Remove expands it to all visible group ASINs. The table tooltip and existing removal confirmation now explain this.
- The prior footwear picker compared EU 44/39 directly against regional `sizeValue`, often UK sizes (e.g. SKU suffix `-45` with `sizeValue="10"`), or an empty field on opaque dropship SKUs. It then fell back to the first ASIN, showing EU 37 even with 44/45 available.
- Representative selection now restricts to positive cached store inventory when available, then prefers footwear EU 45, 44, 39. EU size comes from a numeric SKU suffix, an EU-range catalog value, or a final title variant such as `(Black, EU-45)`. Half sizes are not rounded. Heel/pump/sneaker/platform categories also use footwear selection.
- Group keys, category placements, deletion snapshots, and order formats are unchanged; no working-directory migration is needed. Offline tests cover regional/catalog/title sizes, stock preference, and legacy/per-node reloads where the representative changes but ordering, duplicate placements, and archived deletions remain intact. Reload tests also compare catalog/order file bytes to ensure read-only loading.
- The running desktop app inspected on this date used `/home/cedric/ApplicationsBuild/AmazonTemplate3/build/Desktop_Qt_6_8_3-Debug/AmazonTemplate3/AmazonTemplate3`, not the repository's `build-release` executable. Its CMake home points to this repository. Rebuild that configured Debug target as well when the user needs changes through their existing launcher; the running process still requires a restart. Recheck the running executable before assuming this path remains current.
