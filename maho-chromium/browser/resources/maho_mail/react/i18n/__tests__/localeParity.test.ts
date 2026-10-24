import { readdirSync, readFileSync } from "node:fs";
import { join, resolve } from "node:path";
import { describe, expect, it } from "vitest";
import en from "../locales/en.json";
import ko from "../locales/ko.json";

type Json = string | number | boolean | null | Json[] | { [key: string]: Json };

function kind(value: Json): string {
  if (Array.isArray(value)) return "array";
  if (value === null) return "null";
  return typeof value;
}

function leafShape(tree: Json, prefix = "", out = new Map<string, string>()): Map<string, string> {
  if (kind(tree) === "object") {
    for (const [key, value] of Object.entries(tree as Record<string, Json>)) {
      leafShape(value, prefix ? `${prefix}.${key}` : key, out);
    }
  } else {
    out.set(prefix, kind(tree));
  }
  return out;
}

function sourceFiles(dir: string, out: string[] = []): string[] {
  for (const entry of readdirSync(dir, { withFileTypes: true })) {
    const path = join(dir, entry.name);
    if (entry.isDirectory()) {
      if (entry.name !== "node_modules" && entry.name !== "__tests__") sourceFiles(path, out);
    } else if (/\.tsx?$/.test(entry.name) && !/\.(test|d)\.tsx?$/.test(entry.name)) {
      out.push(path);
    }
  }
  return out;
}

/** Every literal key passed as the first argument of `t(...)` in Mail source. */
function literalTranslationKeys(): Set<string> {
  const keys = new Set<string>();
  for (const file of sourceFiles(resolve(import.meta.dirname, "../.."))) {
    for (const match of readFileSync(file, "utf8").matchAll(/\bt\(\s*["'`]([A-Za-z0-9_.]+)["'`]/g)) {
      keys.add(match[1]);
    }
  }
  return keys;
}

describe("locale key parity", () => {
  const enShape = leafShape(en as Json);
  const koShape = leafShape(ko as Json);

  it("ko defines every en key with the same leaf type", () => {
    const mismatches = [...enShape].filter(([key, type]) => koShape.get(key) !== type);
    expect(mismatches).toEqual([]);
  });

  it("ko defines no keys that en lacks", () => {
    const extra = [...koShape.keys()].filter((key) => !enShape.has(key));
    expect(extra).toEqual([]);
  });

  it("en defines every literal t() key used in source", () => {
    const defined = (key: string) =>
      enShape.get(key) === "string" || enShape.has(`${key}_one`) || enShape.has(`${key}_other`);
    const missing = [...literalTranslationKeys()].filter((key) => !defined(key)).sort();
    expect(missing).toEqual([]);
  });
});
