import { readFileSync } from "node:fs";
import { resolve } from "node:path";
import { describe, expect, it } from "vitest";

// Guards the defect behind "OAuth is unavailable. Try IMAP instead.": a raw
// handler.beginOAuth(provider) call missing the mojom's second parameter is
// rejected by Mojo before reaching the handler, and the rejection surfaces as
// that generic message. The generated .d.ts had drifted to one parameter too,
// so TypeScript could not catch it.

const TESTS_DIR = __dirname;
const MOJOM = resolve(TESTS_DIR, "../../../../ui/webui/maho_mail/maho_mail.mojom");
const RAW_CALL_SITES = [
  resolve(TESTS_DIR, "../app.tsx"),
  resolve(TESTS_DIR, "../api/index.ts"),
];
const DECLARATIONS = [
  resolve(TESTS_DIR, "../../maho_mail.mojom-webui.js.d.ts"),
  resolve(TESTS_DIR, "../mojom.d.ts"),
];

function declaredMojomArity(): number {
  const line = readFileSync(MOJOM, "utf8")
    .split("\n")
    .find((candidate) => candidate.includes("BeginOAuth("));
  expect(line, "BeginOAuth must exist in maho_mail.mojom").toBeDefined();
  const params = line!.slice(line!.indexOf("(") + 1, line!.indexOf(")"));
  return params.split(",").filter((part) => part.trim().length > 0).length;
}

function beginOAuthCallArgCounts(source: string): number[] {
  const counts: number[] = [];
  const needle = "beginOAuth(";
  let index = source.indexOf(needle);
  while (index !== -1) {
    // Only raw remote invocations, never the exported wrapper's own definition.
    const isRemoteCall = source.slice(Math.max(0, index - 8), index).includes("handler.");
    if (isRemoteCall) {
      let depth = 0;
      let args = "";
      for (let cursor = index + needle.length - 1; cursor < source.length; cursor += 1) {
        const char = source[cursor];
        if (char === "(") depth += 1;
        if (char === ")") {
          depth -= 1;
          if (depth === 0) break;
        }
        if (depth >= 1 && !(depth === 1 && char === "(")) args += char;
      }
      counts.push(args.trim().length === 0 ? 0 : splitTopLevel(args).length);
    }
    index = source.indexOf(needle, index + 1);
  }
  return counts;
}

// Splits an argument list on commas that are not nested inside brackets.
function splitTopLevel(args: string): string[] {
  const parts: string[] = [];
  let depth = 0;
  let current = "";
  for (const char of args) {
    if ("([{".includes(char)) depth += 1;
    if (")]}".includes(char)) depth -= 1;
    if (char === "," && depth === 0) {
      parts.push(current);
      current = "";
      continue;
    }
    current += char;
  }
  parts.push(current);
  return parts.filter((part) => part.trim().length > 0);
}

describe("BeginOAuth Mojo arity contract", () => {
  const arity = declaredMojomArity();

  it("declares two parameters in the mojom", () => {
    expect(arity).toBe(2);
  });

  it.each(RAW_CALL_SITES)("calls the remote with the declared arity in %s", (file) => {
    const counts = beginOAuthCallArgCounts(readFileSync(file, "utf8"));
    expect(counts.length, `${file} must contain a raw handler.beginOAuth call`).toBeGreaterThan(0);
    for (const count of counts) {
      expect(count).toBe(arity);
    }
  });

  it.each(DECLARATIONS)("declares the same arity in %s", (file) => {
    const line = readFileSync(file, "utf8")
      .split("\n")
      .find((candidate) => candidate.includes("beginOAuth("));
    expect(line, `${file} must declare beginOAuth`).toBeDefined();
    const params = line!.slice(line!.indexOf("(") + 1, line!.indexOf(")"));
    expect(splitTopLevel(params).length).toBe(arity);
  });
});
