---
name: project-spapi-auth
description: "Amazon SP-API authentication lessons learned — LWA only, no SigV4, refresh token regeneration requirement"
metadata:
  node_type: memory
  type: project
  originSessionId: c73a7e75-03f5-49a5-8ed5-9cca4f062e92
---

SP-API no longer requires AWS SigV4 since October 2, 2023. Only the `x-amz-access-token` LWA header is needed.

**Why:** Amazon deprecated SigV4 requirement on 2023-10-02. Having an unregistered IAM entity sign requests was causing 403 AccessDeniedException for EU/NA marketplaces.

**How to apply:** Do not add SigV4/AWS credentials back. If future SP-API calls return 403, the first thing to check is refresh token age, not AWS credentials.

---

LWA refresh tokens encode the SP-API app's roles at authorization time. If roles are added to the app after tokens were issued, those tokens will not grant the new permissions — they must be regenerated via a new self-authorization.

**Why:** Burned us — EU/NA returned 403 while JP worked because JP token was newer and had the Product Listing role; EU/NA tokens predated the role being added.

**How to apply:** Any time 403 `AccessDeniedException` appears on some regions but not others, the fix is to re-authorize the app for the affected region at solutionproviderportal.amazon.com to get fresh refresh tokens.

---

MCF / Fulfillment Outbound API (`/fba/outbound/2020-07-01/*`, incl. `getFulfillmentOrder`) requires the **"Amazon Fulfillment"** role — there is NO role named "Multi-Channel Fulfillment" in the Solution Provider Portal (verified 2026-07-04 in Amazon's role-mappings doc).

**Why:** We wasted a day telling the user to add a non-existent "Multi-Channel Fulfillment" role; the 403 persisted even with a fresh EU token because "Amazon Fulfillment" was never checked. The role must be enabled in BOTH the developer profile and the app's role list, and then a new refresh token generated.

**How to apply:** When MCF endpoints 403, check for the "Amazon Fulfillment" role by that exact name. Verify token validity outside the app with a python probe (LWA exchange + direct GET) before blaming app code — settings.ini at `~/Dropbox/freelancers/projects/workingDirectory/amazonTemplate3/settings.ini` holds the LWA creds. Also note `PaneMarketplaces::_api()` caches the AmazonInventoryApi instance, so a token changed in Settings requires an app restart.
