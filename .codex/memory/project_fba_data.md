---
name: fba-inventory-data-lessons
description: Amazon FBA inventory data sources — MYI report lags & has a generation quota (FATAL); live FBA Inventory API is fresher; quantity semantics
metadata:
  node_type: memory
  type: project
  originSessionId: a407f261-6c76-4e0b-a482-e305776ed08f
---

Lessons from PaneMarketplaces inventory work (2026-07-04):

- **MYI report (`GET_FBA_MYI_UNSUPPRESSED_INVENTORY_DATA`) lags reality.** During FC transfers a SKU can read 0 in EVERY afn-* column while Seller Central shows stock. Also has a **generation quota**: requesting it repeatedly (several per hour) returns processingStatus=FATAL on creation — not an account problem, just throttling. FATAL reports carry an error document (reportDocumentId) explaining why.
- **Live FBA Inventory API (`/fba/inventory/v1/summaries?details=true`) is the fresher source**, no generation quota, 50 SKUs per call. `inventoryDetails.reservedQuantity` breaks down into `pendingCustomerOrderQuantity`, `pendingTransshipmentQuantity` (FC transfer), `fcProcessingQuantity`; `researchingQuantity.totalResearchingQuantity` for lost/under-investigation units.
- **Quantity semantics chosen by the user:** app's "Amazon Qty" = `fulfillableQuantity + pendingTransshipmentQuantity` (FC-transfer units stay sellable). Researching, customer-order-reserved and FC-processing units are excluded. Seller Central's "On-hand" = afn-warehouse-quantity includes reserved+researching, hence apparent mismatches.
- Architecture in `PaneMarketplaces::_onLoad`: cold start = MYI report (with live-API fallback on failure); partial refresh (SKUs missing from the 24 h cache) = live API only. Report parser cross-checks zero/missing whitelist SKUs against the live API and prefers live numbers. Per-SKU cache invalidation via right-click on the products table.
- EU FBA inventory is one pooled network — any EU marketplace's report/API reflects the whole pool.

## Marketplace Sync Qty rule and manual edits (2026-10-03)

- `TableMarketplaceProducts::_targetQty`: with Min days enabled and estimated days at/below the threshold, propose **1** when available source quantity is **strictly greater than 5** and inventory percentage is positive; otherwise propose 0. This supersedes the previous unconditional 0 for low coverage. Above-threshold correction, percentage, and maximum rules remain as before; 0% still yields 0.
- Sync Qty cells accept nonnegative integer overrides per SKU/store. `PaneMarketplaces::_onSyncInventory` uses `targetQtyForSku(sku, storeId)` for each store, so the uploaded target matches the edited cell. Overrides survive applying sync parameters and disappear on Load (which replaces the model).
- The dark-red warning remains based on the automatic target being lower than current store stock, even when an override raises the displayed target; an edited target below current stock also warns. SKU has an editable flag solely to open a text editor for selection/copying; `setData` rejects SKU writes. Other columns and unknown-source-stock Sync Qty cells are read-only.
- Offline regression coverage: `TableMarketplaceProductsTests` checks stock/coverage boundaries, percentage/cap behavior, store-specific overrides, warning retention, and invalid edits. No live inventory writes were used to validate this change.
