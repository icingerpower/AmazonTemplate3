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
