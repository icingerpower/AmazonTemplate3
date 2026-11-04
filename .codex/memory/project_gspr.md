# GSPR Manufacturer Parser and Country Mapping Lessons

## 1. Manufacturer Parser (`GsprManufacturerStore`)

- **Bare email parsing**:
  `GsprManufacturerStore::parseManufacturer` previously only recognized emails prefixed with `email:` / `e-mail:`. When an email was present as a bare segment without a label (e.g. `... - info@conpaxayakkabi.com - +90 212 619 09 10`), it was not matched as an email and fell through to the unlabelled address continuation (`addr = joinAddress(addr, seg)`). This left `email` empty and polluted `addr`.
  `RE_BARE_EMAIL` (`^(?:mailto:)?<?([A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,})>?$`) was added to detect bare emails and populate `email`.
- **Inline label collision with `Email address:`**:
  `RE_INLINE_LABEL` previously matched `\s+(address\s*:)`. When a line contained `- Email address: nico@sinniko.cn`, `RE_INLINE_LABEL` matched ` address:` inside `Email address:`, splitting it into `Email` and `address: nico@sinniko.cn`. `RE_INLINE_LABEL` and `RE_EMAIL` now prioritize `(?:e-?mail(?:\s*address)?|courriel)`.
- **Purchase link column email fallback**:
  In `GsprManufacturerStore::reload`, if `e.url` and `e.email` are both empty, the "Purchase link" column is checked for `http` URLs and also for email addresses via `RE_BARE_EMAIL`.
- **Cache versioning**:
  `CACHE_FILE` is `gspr-manufacturer-cache-v4.json`. Files are cached based on file modification time (`mtime`). Bumping the cache version forces all supplier `.xlsx` files to be re-parsed fresh.

## 2. Address & Country Mapping (`case-worker/src/gsprPage.ts`)

- In `splitAddress()`, `COUNTRY_ALIASES` maps the last address token to standard country names accepted by Amazon Seller Central dropdowns.
- `COUNTRY_ALIASES` now includes Turkey variations (`tr`, `turkey`, `turkie`, `turquie`, `turkiye`, `türkiye`) as well as major European countries (`de`, `germany`, `fr`, `france`, `it`, `italy`, `es`, `spain`, `uk`, `gb`, `united kingdom`) to prevent defaulting to "China".

## 3. Seller Central Regulatory Compliance UI (`case-worker/src/gsprPage.ts`)

- **Two federated drawer UIs (`MYCWebWidget`)**:
  Amazon serves both the legacy compliance drawer (`475.7b8e31e0.chunk.js`) and a newer drawer (`259.42013625.chunk.js`). Key differences in `chunk 259`:
  - **Safety attestation card**: `<div role="button">` has no `aria-label`; its child `<kat-card>` has `aria-label="Safety attestation, Status: Not started"` (or `Status: Under review`). Match both `[role="button"][aria-label*="Safety attestation" i]` and `kat-card[aria-label*="Safety attestation" i]`.
  - **Safety attestation checkbox**: `onChange` reads `e.detail.checked` on `kat-checkbox`. JS fallback dispatch must send `new CustomEvent("change", { detail: { checked: true, value: "true" }, bubbles: true, composed: true })`.
  - **Single-step submission confirmation**: Single-step tasks (`je`, `_e`) use a `Submit` primary button. While `isSubmitting === true`, the confirmation screen (`rn`) renders `[role="status"][aria-label="Loading"]` and a **disabled** `Close` button (`kat-button[disabled]`). When submission finishes (`isSubmitting === false`), `rn` renders `<kat-icon name="clock" size="xxlarge">` (`nn`) and enables the `Close` button (`kat-button:not([disabled])`). There is no `[data-testid="success-page"]` in `chunk 259`.
  - **RP & MFR landing card**: The task card on the landing screen is titled `"Single product upload"` (`<kat-card aria-label="Single product upload, Status: Not started">`) instead of `"Responsible Person Information"` or `"Manufacturer information"`.
  - **Registry list (`Ie` / `_e`)**: Search input is only rendered when `entries.length >= 5`. Cards use `kat-checkbox` in `MULTIPLE` selection mode (`kat-radiobutton` in `SINGLE`), and clicking `kat-card` toggles selection.
  - **Multi-marketplace selector**: `chunk 259` renders `<kat-dropdown multiple="true">` at the top of the drawer, so Country/RP dropdown locators must use `kat-dropdown:not([multiple])`.
- **Main table page size & pagination**:
  - The main Regulatory Compliance table defaults to 25 rows per page (`#ahd-pp-pagination-page-size-dropdown value="25"`, options `"25"`, `"50"`, `"100"`). When a marketplace has >25 rows, rows and expandable RP/MFR accordion groups can be pushed onto page 2+.
  - `maximizeMainTablePageSize()` dispatches `new CustomEvent("change", { detail: { value: "100" }, bubbles: true, composed: true })` on `#ahd-pp-pagination-page-size-dropdown`, and `getMainTablePageCount()` / `goToMainTablePage()` iterate across `#ahd-pp-pagination-katal-control` pages when total rows exceed 100.
