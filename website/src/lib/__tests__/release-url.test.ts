import { describe, expect, test } from "bun:test";
import { releasePageUrl } from "../release-url";

describe("releasePageUrl", () => {
  test("uses GitHub's stable releases listing route", () => {
    expect(releasePageUrl("https://github.com/Project-Maho/release")).toBe(
      "https://github.com/Project-Maho/release/releases",
    );
  });

  test("normalizes a trailing slash on the release repository URL", () => {
    expect(releasePageUrl("https://github.com/Project-Maho/release/")).toBe(
      "https://github.com/Project-Maho/release/releases",
    );
  });
});
