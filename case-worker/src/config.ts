import { readFileSync, existsSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, resolve } from "node:path";
import type { Config, Region, RegionConfig } from "./types.js";

const here = dirname(fileURLToPath(import.meta.url));
const root = resolve(here, "..");

/**
 * Loads config.json (falling back to config.example.json), strips `//`-comment
 * keys, and resolves each region's profile dir + the screenshot dir to absolute
 * paths.
 */
export function loadConfig(): Config {
  const chosen = existsSync(resolve(root, "config.json"))
    ? resolve(root, "config.json")
    : resolve(root, "config.example.json");

  const raw = JSON.parse(readFileSync(chosen, "utf8")) as Record<string, unknown>;

  // Drop documentation keys ("//", "//foo").
  const strip = (o: Record<string, unknown>): Record<string, unknown> => {
    const out: Record<string, unknown> = {};
    for (const [k, v] of Object.entries(o)) {
      if (k.startsWith("//")) continue;
      out[k] = v && typeof v === "object" && !Array.isArray(v)
        ? strip(v as Record<string, unknown>)
        : v;
    }
    return out;
  };

  const cfg = strip(raw) as unknown as Config;
  cfg.screenshotDir = resolve(root, cfg.screenshotDir);
  for (const rc of Object.values(cfg.regions))
    rc.userDataDir = resolve(root, rc.userDataDir);
  return cfg;
}

export function regionConfig(cfg: Config, region?: Region): RegionConfig {
  const r = region ?? cfg.defaultRegion;
  const rc = cfg.regions[r];
  if (!rc) throw new Error(`unknown region: ${r} (configured: ${Object.keys(cfg.regions).join(", ")})`);
  return rc;
}

export function caseUrl(cfg: Config, region: Region | undefined, caseId: string): string {
  return cfg.caseUrlTemplate
    .replace("{domain}", regionConfig(cfg, region).domain)
    .replace("{caseId}", encodeURIComponent(caseId));
}

export function caseListUrl(cfg: Config, region?: Region): string {
  return cfg.caseListUrl.replace("{domain}", regionConfig(cfg, region).domain);
}
