---
name: variation-fix-lessons
description: "Hard-won SP-API rules for fixing broken parent/child variations (Broken child tab, JSON_LISTINGS_FEED)"
metadata:
  node_type: memory
  type: project
  originSessionId: 2717d467-073b-4960-a01c-ed13c3072bdf
---

Lessons from fixing broken Amazon EU variation families (July 2026, caftan family P-CAFTAN-CJYD2315867). The schema-driven feed in `PaneSizing::_buildFullVariationMessages` encodes these; keep them when refactoring.

**Why:** ~100 blind attempts failed because feeds returned ACCEPTED while async validation silently rejected or IGNORED the data. The fixes below came from reading real listing state (`checkListing`) + Product Type Definitions schemas.

**How to apply:**
- Attribute names are PER PRODUCT TYPE: APPAREL uses `size` (simple `{value, language_tag, marketplace_id}`, language_tag REQUIRED) + `color`; DRESS-like types use `apparel_size` composite + `color_name`/`color_map`. Sending the wrong set → feed warning 90000900 "attribute does not belong … ignoring" = accepted but never stored. Always read the listing's ACTUAL productType (`summaries[].productType`, differs per marketplace!) and its schema before choosing attributes.
- JSON_LISTINGS_FEED cannot delete attributes (op delete → "Invalid empty value"); the direct Listings Items PATCH can (`deleteListingAttribute`). Needed to purge legacy `apparel_size` junk that blocks validation forever (DE case).
- Composite size attribute is PER PRODUCT TYPE: clothing = `apparel_size`, SWIMWEAR = `shapewear_size` (same nested shape {size, size_system, size_class, body_type, height_type}). Canonical size value is `numeric_<n>`; `size_system` codes (as1/as3/as4/as6/as8) are REGIONAL (FR/ES=as4, DE=as3, IT=as6, MX=as1, IE=as8 for shapewear) — never borrow across regions. For shapewear the schema pins size_system to exactly ONE value per marketplace, so read it from `fetchCompositeSizeEnums` (no stored data needed).
- body_type/height_type=`regular` are OPPOSITE between the two composites: FORBIDDEN for apparel_size (error 90248 "not allowed") but REQUIRED for shapewear_size (error 99022 "does not have enough values"). Both conditional on [size_class, size_system, age_range_description.value]. Send them only for shapewear.
- `department` is language-tagged, not a variation dimension, not required — do NOT send it (a stale wrong-language stored value causes 100720; the wrong-language cleanup deletes size/color/department entries whose language_tag ≠ the marketplace's). `merchant_suggested_asin` must NOT be sent on parents (per-marketplace parent ASIN differs → 101077).
- Wrong collected child sizes: temporary per-SKU override map `kSizeOverrideFr` in `_buildFullVariationMessages` (SKU → FR-region size, converted to other regions).
- `age_range_description` must be the English enum ("Adult"), never localized display names ("Erwachsener") → error 100720. `canonicalAge` maps translations.
- BE (AMEN7PMS3EDWL) accepts ONLY language_tag `fr_BE` (schema $defs/language_tag enum).
- Amazon validation state lags: check issues ≥1h after a feed; the read-only "Check status" button (checkListing + schema props + stored attrs) is the ground truth and never resets propagation.
- Feed processing report + `/tmp/sp-api-feed-body-*.json` + `/tmp/sp-api-schema-*.json` dumps are the evidence trail.

Related: [[project_spapi_auth]]
