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

## 2026-10-10 — Monthly batching and throttling recovery

- Supersedes the Amazon per-month `granularity=Total` implementation above:
  PaneSales now uses `fetchMonthlyOrderCounts`, and Amazon sends one
  `granularity=Month` request for the range per storefront (14 calls instead of
  168 for 14 storefronts). Map results by their UTC interval, not array order;
  validate month boundaries and retain missing/invalid months as unavailable.
  The single-interval API is still available; Temu's default implementation
  continues counting each requested month independently.
- Amazon sales-pane clients share a monotonic request gate across storefronts,
  retries, and refreshes: default spacing 2100 ms, slowed further for lower
  reported rate limits. Transient HTTP 429/5xx/network failures get up to five
  attempts with 2/4/8/16-second backoff and server Retry-After handling.
  This gate covers the sales-pane metrics path, not the separate legacy SKU
  sales retrieval used by pricing/inventory.
- Cancel interrupts Amazon network/pacing/backoff waits. The shared gate keeps
  server cooldowns even when a job is canceled. Temu stops between requests.
- Retry failed retains successful cells and requests only missing months/rows,
  using the original count's end timestamp for consistency. Count order starts
  a full fresh count. Changed channel labels/order require a fresh count.
- Failure details include HTTP status, Amazon error code/message and request ID
  when available, shown in the status label and cell tooltips. The screenshots
  did not establish the original HTTP error; throttling remains unconfirmed.
- Offline regressions cover 429 recovery, Retry-After delay, shared pacing
  across two clients, all twelve counts in a single batch, shuffled/sparse/
  duplicate intervals, canceled backoff, diagnostic details, and retrying only
  a failed cell while keeping successful counts. No live API calls made.
- Final validation: full Release build succeeded and all 16 CTest suites passed
  offscreen (30.07 seconds), including SalesTests with the real pacing delays.

## 2026-10-10 — Live monthly-label correction

- The subsequent all-N/A screenshot showed a parser error after HTTP success,
  not an access block. A read-only UK Sales API probe confirmed HTTP 200 and
  twelve entries. The response uses minute precision (`2026-10-01T00:00Z`)
  and labels the partial current month with its FULL calendar interval:
  `2026-10-01T00:00Z--2026-11-01T00:00Z`, even for a request ending October 10.
- The original batching parser incorrectly required the final interval label
  to end at the request cutoff, then discarded the entire row on mismatch.
  Accept either the calendar-month end or the clipped request end. Keep strict
  checks for the month start, duplicate intervals, and arbitrary longer ranges;
  isolate invalid/duplicate months instead of erasing other valid months.
- Regression `monthlyCalendarLabelsWithPartialCurrentMonth` reproduces the
  observed response shape with synthetic counts. It failed before the fix and
  passes afterward. Existing clipped-end and shuffled/sparse tests still pass.
- Unlike earlier offline-only validation, the corrected production parser was
  checked against a live UK response: all twelve months parsed. A separate
  Total query for the current month with the identical cutoff matched the
  monthly result, confirming that accepting the calendar label does not extend
  the counted period. Diagnostics retained no credentials or actual counts in
  repository files. Only authentication and read-only sales requests were made.
- Final Release build and all 16 CTest suites passed (29.97 seconds).
