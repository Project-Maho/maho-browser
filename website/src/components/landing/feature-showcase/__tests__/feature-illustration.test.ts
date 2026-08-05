import { describe, expect, test } from "bun:test";
import { readFile } from "node:fs/promises";

const ILLUSTRATION_URL = new URL("../../FeatureIllustration.astro", import.meta.url);

describe("Content blocker illustration", () => {
  test("holds visible noise at the loop boundaries", async () => {
    const source = await readFile(ILLUSTRATION_URL, "utf8");

    expect(source).toContain(
      "@keyframes junk-lifecycle { 0%,28%{opacity:1;transform:scale(1);filter:blur(0)}",
    );
    expect(source).toContain("92%,100%{opacity:1;transform:scale(1);filter:blur(0)}");
    expect(source).toContain("@keyframes badge-reveal { 0%,38%{opacity:0");
    expect(source).toContain("48%,78%{opacity:1");
  });
});
