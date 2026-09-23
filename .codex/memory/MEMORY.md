# Memory Index

- [Pricing editor and shared cache](project_pricing_editor.md) — implemented display modes, local-currency proposals, shared cache, zero-sales rule, and validation

- [Amazon SP-API auth lessons](project_spapi_auth.md) — LWA only (no SigV4 since 2023-10-02); refresh tokens must be regenerated after adding app roles; MCF needs the "Amazon Fulfillment" role (no "Multi-Channel Fulfillment" role exists)
- [A+ content upload API lessons](project_aplus_api_lessons.md) — 11 hard-won rules: NA-only Uploads API, S3 form-body params, EBC not EMC, child ASINs only, no em-dashes, decorator pitfalls, module structure
- [PaneStore categories and persistence](project_panestore.md) — stores/{marketplaceId}.json; duplicate category placements, independent ordering, and offline regression tests
- [Temu API lessons](project_temu_api.md) — working endpoints, MD5 signing, goodsSearchType/skuSearchType required as int, per-SKU stock via sku.list.query types 2+3, stock.edit is diff-based
- [No autonomous commits](feedback_no_commit.md) — never commit without explicit user permission; at most ask
- [Octopia fulfillment plan](project_octopia_plan.md) — inventory + shipping will later also come from Octopia; keep marketplace sync code source-agnostic
- [FBA inventory data lessons](project_fba_data.md) — MYI report lags + FATAL quota; live Inventory API fresher; Amazon Qty = fulfillable + FC transfer
- [Variation fix lessons](project_variation_fix_lessons.md) — schema-driven attrs per actual productType (size vs apparel_size), feed can't delete (direct PATCH can), regional size systems, English enums, BE=fr_BE only
- [CLI runPrompt crash](project_cli_runprompt_crash.md) — never co_await AbstractCli::runPrompt() (SIGSEGV); use runPromptAsync→QFuture bridge; keep fire-and-forget Tasks in a member
- [AI CLI output parsing](project_aicli_output_parsing.md) — extractTextFromOutput() is stream-json-only (translation args); for runPrompt/runPromptAsync use CliRunResult.output directly
- [Case auto-reply worker](project_case_worker.md) — Seller Central case replies via Playwright Node worker (no SP-API); draft-first, per-prompt autoSend default off; Qt PaneCases side still TODO
- [Discount feature](project_discount_feature.md) — PaneDiscount; SP-API has no coupon/deal API (only scheduled discounted_price); inventory age = GET_FBA_INVENTORY_PLANNING_DATA; shared ProgressDialog helper
- [OpenAi2 callbacks must not throw](project_openai_callback_no_throw.md) — throwing from onLastError/validate/apply runs in a QNetworkReply slot → std::terminate; log + return true; failures surface as ExceptionOpenAiError via the coroutine future
- [/resume all-projects hang](project_resume_picker.md) — Ctrl+A picker hangs at >1000 project dirs (per-ASIN CLI runs create them); archived to projects-archive-20260802; regrows with scripts
