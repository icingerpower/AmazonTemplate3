# amazon-case-worker

Playwright worker that drives **Amazon Seller Central case management** for
AmazonTemplate3. It does four things against your real Seller Central session:

| Op | What it does |
|----|--------------|
| `getThread` | Open a case by ID, scrape the full message thread (+ subject/status) |
| `fillReply` | Type a reply into the case reply box — **does not send** |
| `send`      | Click Send (only after human approval, or when a prompt has `autoSend`) |
| `ping`      | Report whether the Seller Central session is still valid |

The AI drafting lives in the Qt app (via the selected CLI); this worker is
*only* the browser hands. **No message is ever sent without an explicit reply.**

## How the app runs it: one-shot, no server

The Qt app does **not** start a server. It launches the worker **per phase** via
`QProcess` (exactly like it launches the AI CLIs), feeds a JSON job on stdin,
reads one JSON result on stdout, and the process exits. Zero setup at run time —
click Run and it works. The persistent per-region login profile keeps you signed
in across invocations.

One-shot entry: `src/oneshot.ts`. Jobs (stdin → stdout):

```json
{"action":"scrape","cases":[{"region":"eu","caseId":"123"}]}
{"action":"reply","cases":[{"region":"eu","caseId":"123","text":"Hello…"}]}
{"action":"login","region":"eu"}
```

The app finds this project via the `CASE_WORKER_DIR` compile define (the repo's
`case-worker/`), overridable with the `cases/workerDir` QSetting. The
`npm run serve` TCP server below is now **optional**, kept only for manual
debugging.

## Why Playwright and not an API

Amazon SP-API has no public endpoint for Seller Support cases. The Messaging API
only covers fixed, order-scoped buyer messages. Case replies exist only in the
Seller Central UI, so we automate the UI.

> ⚠️ Automating Seller Central is a gray area in Amazon's terms. The design here
> keeps a human in the loop (draft-only by default) to minimise account risk.
> Run headed, log in as yourself, don't hammer it.

## Regions

Seller Central is **region-scoped**: an EU case only exists on the EU dashboard,
NA on `.com`, JP on `.co.jp` — and each region has its **own login session**
(own Chromium profile), mirroring the app's EU/NA/JP refresh-token split. Every
command and RPC call takes a region: `eu` | `na` | `jp`. Log in once per region
you actually use. A group in the Qt app carries its region.

## Setup

```bash
cd case-worker
npm install                      # also runs `playwright install chromium`
cp config.example.json config.json
# edit config.json: set each region's `domain` to your Seller Central TLD
```

## Prove it against a real case (do this first)

```bash
npm run login  -- eu                     # EU browser opens; log in with MFA; leave it, Ctrl+C
npm run open   -- eu <caseId>            # navigate to a case; inspect the DOM
npm run thread -- eu <caseId>            # print the scraped thread as JSON
npm run fill   -- eu <caseId> "draft text"   # type into the reply box, NOT sent
```

Swap `eu` for `na` / `jp` for other regions (log in to each once).

The selectors in `config.json` are **educated guesses** — Seller Central's DOM
varies by region and changes over time. Use `npm run open` to inspect the live
page, then fix the `selectors` block until `npm run thread` returns clean data.
Screenshots of each step land in `./screenshots/` for debugging.

## Run as the server (for the Qt app)

```bash
npm run serve                    # listens on 127.0.0.1:8787 (config.serverPort)
```

Protocol: newline-delimited JSON over TCP. One request per line; `region`
defaults to `config.defaultRegion` if omitted:

```json
{"id":"1","method":"getThread","params":{"region":"eu","caseId":"12345678901"}}
{"id":"2","method":"fillReply","params":{"region":"eu","caseId":"12345678901","text":"Hello..."}}
{"id":"3","method":"send","params":{"region":"eu","caseId":"12345678901"}}
```

Each yields one response line: `{"id":"1","ok":true,"result":{...}}` or
`{"id":"1","ok":false,"error":"..."}`. A `SessionExpired` error means re-run
`npm run login`.
