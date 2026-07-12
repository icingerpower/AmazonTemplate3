/**
 * Local TCP server the Qt app talks to. Protocol: newline-delimited JSON — one
 * RpcRequest per line in, one RpcResponse per line out. Binds to 127.0.0.1 only.
 *
 * One browser context per region (eu/na/jp), each with its own login session,
 * launched lazily on first use. Requests are serialised through a single queue
 * so concurrent case operations never race the same context.
 */
import { createServer, type Socket } from "node:net";
import { loadConfig } from "./config.js";
import { launchContext, firstPage } from "./browser.js";
import { getThread, fillReply, send, openCase, isLoggedIn, SessionExpiredError } from "./casePage.js";
import type { RpcRequest, RpcResponse, Config, Region } from "./types.js";
import type { Page } from "playwright";

const cfg: Config = loadConfig();

const pages = new Map<Region, Promise<Page>>();
async function getPage(region: Region): Promise<Page> {
  let p = pages.get(region);
  if (!p) {
    p = (async () => {
      const ctx = await launchContext(cfg, region);
      return firstPage(ctx);
    })();
    pages.set(region, p);
  }
  return p;
}

// Serialise all browser work: chain each job onto the previous one.
let queue: Promise<unknown> = Promise.resolve();
function enqueue<T>(job: () => Promise<T>): Promise<T> {
  const run = queue.then(job, job);
  queue = run.catch(() => {});
  return run;
}

async function handle(req: RpcRequest): Promise<RpcResponse> {
  const now = Date.now();
  const region: Region = req.params?.region ?? cfg.defaultRegion;
  try {
    if (!cfg.regions[region])
      return { id: req.id, ok: false, error: `unknown region: ${region}` };

    const page = await getPage(region);
    const caseId = String(req.params?.caseId ?? "");

    switch (req.method) {
      case "ping":
        return { id: req.id, ok: true, result: { region, loggedIn: await isLoggedIn(page, cfg) } };
      case "openCase":
        await openCase(page, cfg, region, caseId);
        return { id: req.id, ok: true, result: { opened: caseId, region } };
      case "getThread":
        return { id: req.id, ok: true, result: await getThread(page, cfg, region, caseId, now) };
      case "fillReply":
        await fillReply(page, cfg, region, caseId, String(req.params?.text ?? ""), now);
        return { id: req.id, ok: true, result: { filled: caseId, region } };
      case "send":
        await send(page, cfg, region, caseId, now);
        return { id: req.id, ok: true, result: { sent: caseId, region } };
      default:
        return { id: req.id, ok: false, error: `unknown method: ${req.method}` };
    }
  } catch (e) {
    const msg = e instanceof SessionExpiredError ? e.message : (e as Error).message ?? String(e);
    return { id: req.id, ok: false, error: msg };
  }
}

function onConnection(sock: Socket) {
  sock.setEncoding("utf8");
  let buf = "";
  sock.on("data", (chunk: string) => {
    buf += chunk;
    let nl: number;
    while ((nl = buf.indexOf("\n")) >= 0) {
      const line = buf.slice(0, nl).trim();
      buf = buf.slice(nl + 1);
      if (!line) continue;
      let req: RpcRequest;
      try {
        req = JSON.parse(line) as RpcRequest;
      } catch {
        sock.write(JSON.stringify({ id: "?", ok: false, error: "bad JSON" }) + "\n");
        continue;
      }
      enqueue(() => handle(req)).then((res) => {
        sock.write(JSON.stringify(res) + "\n");
      });
    }
  });
  sock.on("error", () => { /* client dropped */ });
}

const server = createServer(onConnection);
server.listen(cfg.serverPort, "127.0.0.1", () => {
  console.log(`case-worker listening on 127.0.0.1:${cfg.serverPort}`);
  const regions = Object.entries(cfg.regions)
    .map(([r, rc]) => `${r}→${rc.domain}`).join("  ");
  console.log(`regions: ${regions}   default: ${cfg.defaultRegion}   headless: ${cfg.headless}`);
});
