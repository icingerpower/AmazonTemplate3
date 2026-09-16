---
name: aicli-output-parsing
description: AbstractCli::extractTextFromOutput is ONLY for translationPromptArgs stream-json output; runPrompt/runPromptAsync output is plain text — use CliRunResult.output directly
metadata:
  node_type: memory
  type: project
  originSessionId: ccc921d5-9124-4639-8a3f-7581cb53f613
  modified: 2026-07-25T15:51:47.774Z
---

`AbstractCli::extractTextFromOutput()` (common/aicli) must NOT be applied to
`runPrompt()` / `runPromptAsync()` results. It parses the stream-json event
format that only `translationPromptArgs()` requests (e.g. CliClaude adds
`--output-format stream-json`). `runPromptAsync()` uses `promptArgs()` (plain
`-p` output), so for Claude the stream-json parser returns an empty string and
the reply is silently lost.

**Why:** burned in DialogSuggestEquivalent — Claude's JSON diagnosis was
discarded, dialog showed "Could not parse the Claude reply" with an empty box.

**How to apply:** consume `CliRunResult.output` directly (trim / extract JSON
yourself), as PanePricing and DialogGenStorefrontImage do. Related:
[[project-cli-runprompt-crash]].
