import { describe, it, expect, beforeAll, afterEach } from "vitest";
import { readFileSync } from "node:fs";
import { resolve, dirname } from "node:path";
import { fileURLToPath } from "node:url";

/**
 * Security contract of the Trusted Types banner shipped by bundle_react.mjs:
 *  1. A `default` createHTML policy exists (chrome:// pages demand
 *     TrustedHTML on HTML sinks; a missing policy crashes reader/print).
 *  2. createHTML is not a pass-through: pipeline-marked output passes,
 *     everything else goes through the registered sanitizer.
 *  3. No default createScript/createScriptURL — script sinks keep throwing.
 */

const here = dirname(fileURLToPath(import.meta.url));
const bundleScriptPath = resolve(here, "../../../bundle_react.mjs");

function extractBanner(source: string): string {
  const match = source.match(/const trustedTypesBanner\s*=\s*\n?\s*"((?:[^"\\]|\\.)*)";/);
  if (!match) return "";
  return match[1].replace(/\\"/g, '"').replace(/\\\\/g, "\\");
}

interface CapturedPolicy {
  name: string;
  policy: Record<string, unknown>;
}

function runBanner(banner: string): {
  captured: CapturedPolicy[];
  sharedGlobal: Record<string, unknown>;
} {
  const captured: CapturedPolicy[] = [];
  const fakeWindow = {
    trustedTypes: {
      createPolicy(name: string, policy: Record<string, unknown>) {
        captured.push({ name, policy });
        return policy;
      },
    },
  };
  // One shared global object stands in for the page global: the banner reads
  // __mahoSanitizeHtml from it and sanitizeHtml.ts registers on the same one.
  const sharedGlobal: Record<string, unknown> = {};
  new Function("window", "globalThis", banner)(fakeWindow, sharedGlobal);
  return { captured, sharedGlobal };
}

describe("Trusted Types default policy banner (bundle_react.mjs)", () => {
  let banner = "";
  let captured: CapturedPolicy[] = [];
  let sharedGlobal: Record<string, unknown> = {};

  beforeAll(() => {
    banner = extractBanner(readFileSync(bundleScriptPath, "utf8"));
    if (banner) {
      const run = runBanner(banner);
      captured = run.captured;
      sharedGlobal = run.sharedGlobal;
    }
  });
  afterEach(() => {
    delete sharedGlobal.__mahoSanitizeHtml;
  });

  it("installs a default policy so HTML sinks do not throw at runtime", () => {
    expect(banner).not.toBe("");
    const def = captured.find((p) => p.name === "default");
    expect(def).toBeDefined();
    expect(typeof def!.policy.createHTML).toBe("function");
  });

  it("does NOT install default createScript/createScriptURL pass-throughs", () => {
    const def = captured.find((p) => p.name === "default");
    expect(def).toBeDefined();
    expect(def!.policy.createScript).toBeUndefined();
    expect(def!.policy.createScriptURL).toBeUndefined();
  });

  it("routes unmarked HTML through the registered sanitizer", () => {
    const def = captured.find((p) => p.name === "default")!;
    sharedGlobal.__mahoSanitizeHtml = (s: string) =>
      s.replace(/<script[\s\S]*?<\/script>/g, "[sanitized]");
    const out = (def.policy.createHTML as (s: string) => string)(
      'hello <script>alert(1)</script> world'
    );
    expect(out).toBe("hello [sanitized] world");
  });

  it("passes pipeline-marked output through unchanged (no double-sanitize)", () => {
    const def = captured.find((p) => p.name === "default")!;
    sharedGlobal.__mahoSanitizeHtml = (s: string) =>
      s.replace(/<script[\s\S]*?<\/script>/g, "[sanitized]");
    const marked = "<style>body{color:#111}</style><!--maho-tt-pipeline-->";
    const out = (def.policy.createHTML as (s: string) => string)(marked);
    expect(out).toBe(marked);
  });
});
