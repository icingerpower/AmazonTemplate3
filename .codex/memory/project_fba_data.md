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
