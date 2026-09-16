---
name: project-aplus-api-lessons
description: "Lessons learned implementing Amazon SP-API A+ content upload — hard-won fixes for Uploads API, S3 POST, module structure, ASIN association, and community guidelines"
metadata:
  node_type: memory
  type: project
  originSessionId: a0c506c3-86a3-4cd9-8613-ab6b51c5435b
---

Full details live in `READMEAI.md` (Amazon SP-API — A+ Content Upload section). Key rules:

1. **Uploads API is NA-only.** Always POST images to `sellingpartnerapi-na.amazon.com` with a NA token + NA marketplace ID (`ATVPDKIKX0DER`), even for EU A+ content.
2. **S3 presigned POST: query params go in the form body, not the URL.** Strip all query params from the S3 URL, re-send as `text/plain` multipart form fields before the image part.
3. **S3 success = HTTP 204**, not 200.
4. **`contentType: "EBC"`** for 3P sellers. `"EMC"` is vendor-only and will be rejected.
5. **Module type: `STANDARD_HEADER_IMAGE_TEXT`**, key `standardHeaderImageText`, structure `{headline?, block: {image, body}}`. `altText` inside image is required. Wrong tried: `STANDARD_SINGLE_SIDE_IMAGE`, `standardImageCaption`, `imageBlock`.
6. **No em/en dashes in headlines.** Replace U+2014 `—` and U+2013 `–` with `-` before setting any headline value — community guidelines violation.
7. **Associate with child ASINs, not the parent variation ASIN.** Parent ASIN doesn't exist in the catalog.
8. **Validate 403 is non-fatal.** The validate endpoint requires Brand Registry; treat HTTP 403 as a warning and proceed to `submitForApproval`.
9. **`STYLE_LINEBREAK` + `\n` in value = double spacing.** A `\n` in a TextItem value is already a paragraph break; adding `STYLE_LINEBREAK` at the same offset adds a second break. Use a plain space between Q and A in the same item instead.
10. **Module headline creates unavoidable spacing.** Amazon's CSS adds a fixed gap below headline text. Can't reduce it via the API — bake text into the image if tight spacing matters.
11. **Refresh tokens must be regenerated after adding SP-API app roles.** Tokens encode roles at time of issue.

**Why:** We spent multiple sessions debugging each of these one by one. Each fix was discovered only after live upload attempts and examining error responses.

**How to apply:** Before writing any new SP-API upload code, re-read READMEAI.md and this memory entry to avoid repeating the same mistakes.
