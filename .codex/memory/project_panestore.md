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
