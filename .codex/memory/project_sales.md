# Sales pane — 2026-10-09

- `PaneSales` sits immediately after Marketplaces. The left QListView uses
  `AbstractTargetMarketplaceFactory::ALL_MARKETPLACE_FACTORIES()` and selects
  the first entry. Count order rebuilds configured channels using current
  settings. The table has twelve UTC calendar-month columns, oldest first,
  including the partial current month. Selecting a row updates the line chart
  below a vertical QSplitter. The chart uses QWidget/QPainter, with no new
  Qt Charts dependency.
- The existing Recorder/factory registry now exposes `createSalesChannels()`.
  `AbstractSalesChannel` is a read-only capability. Target marketplaces inherit
  it; Temu forwards to its existing API client. Amazon's sales-only factory
  returns no inventory-sync targets, so fulfillment remains independent.
  Explicit built-in registration still prevents static-library linker stripping.
- Amazon rows come from `AmazonMarketplace` for regions with configured LWA
  credentials (eu/na/jp). Counts use Sales API `orderCount`, with no SKU or
  fulfillment filter, `granularity=Total`, and UTC `[start,end)` intervals.
  HTTP throttling/server failures get bounded retries. A missing metric is N/A,
  never zero. Official schema:
  https://github.com/amzn/selling-partner-api-models/blob/main/models/sales-api-model/sales.json
- Temu reuses configured country/store entries and their proxy settings. Each
  monthly query reads `totalItemNum` from `bg.order.list.v2.get` with pageNumber=1
  and pageSize=1. This counts parent orders, not lines/units, and avoids pagination.
  All statuses (including canceled orders) are included; the pane explains this
  in its metric tooltip. `createBefore` is inclusive, so subtract one second
  from the abstraction's exclusive upper bound. Official reference checked:
  https://partner.temu.com/documentation?sub_menu_code=554fd46b45ee49269cbdd6d4008a5dc1
- No live order calls were made during implementation. Historical availability
  and account permissions still depend on the provider. Do not treat this note
  as evidence of successful live retrieval.
- Cancel stops after the current request; clients have bounded network waits.
  A shared job owns API clients across awaits, and QPointer guards pane access
  after destruction. The retained QCoro task's completed frame must not retain
  clients: release them explicitly on completion/cancellation.
- `SalesTests` uses fake transports and factories for order-vs-unit semantics,
  malformed responses, zero counts, UTC/leap-year boundaries, eight Temu rows,
  selection/chart painting, cancellation, and destruction during an await.
- The shared Temu request helper no longer prints signed request payloads or
  order response bodies, which could expose credentials/customer information.
- Validation: full Release build succeeded; all 16 registered CTest suites
  passed with `QT_QPA_PLATFORM=offscreen` (including SalesTests). No commit made.
