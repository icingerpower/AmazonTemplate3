---
name: octopia-fulfillment-plan
description: "Future plan: inventory + shipping source will move from Amazon FBA to Octopia fulfillment"
metadata:
  node_type: memory
  type: project
  originSessionId: a407f261-6c76-4e0b-a482-e305776ed08f
---

As of 2026-07-04, Temu orders are fulfilled from Amazon FBA (MCF), but the user plans to later handle **inventory + shipping from Octopia fulfillment** (Cdiscount's fulfillment service) as an alternative/additional source.

**Why:** The marketplace sync code (PaneMarketplaces, shipOrder carrier matching) should stay source-agnostic. Today only the Amazon→swiship carrier alias is special-cased; Octopia ships with standard FR carriers (Colissimo, Chronopost, DPD FR, Colis Privé…) whose names should match Temu's carrier list directly via the generic contains() matching in `TemuInventoryApi::shipOrder`.

**How to apply:** DONE 2026-07-05 — the abstraction layer exists in `AmazonTemplate3Lib/marketplaces/`: `AbstractTargetMarketplace` (implemented by `TemuTargetMarketplace`) and `AbstractInventorySource` (implemented by `AmazonFbaInventorySource`), with shared records in `MarketplaceTypes.h` (MarketOrder, ShippingAddress, StockRecord, TrackingInfo, FulfillmentRequest). Implementations self-register via `DECLARE_TARGET_MARKETPLACE_FACTORY` / `DECLARE_INVENTORY_SOURCE_FACTORY` (Recorder pattern, same as ALL_FILLERS_SORTED); factories build configured instances from QSettings (`TemuApi/stores` array, `AmazonApi/*` keys). `PaneMarketplaces` only talks to the abstractions (`_rebuildPlatforms()` on each Load). **Adding Octopia = implement `AbstractInventorySource` + one DECLARE macro — zero pane changes.** Note: pane currently uses `m_sources.first()` as the single active source; multi-source selection is the one open point.
