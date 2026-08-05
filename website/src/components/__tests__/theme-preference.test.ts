import { describe, expect, test } from "bun:test";
import { readFile } from "node:fs/promises";

const THEME_TOGGLE_URL = new URL("../ThemeToggle.tsx", import.meta.url);
const BASE_LAYOUT_URL = new URL("../../layouts/BaseLayout.astro", import.meta.url);

describe("Theme preference defaults", () => {
  test("uses the operating system preference without persisting it", async () => {
    const [themeToggle, baseLayout] = await Promise.all([
      readFile(THEME_TOGGLE_URL, "utf8"),
      readFile(BASE_LAYOUT_URL, "utf8"),
    ]);

    expect(themeToggle).toContain(
      'window.matchMedia("(prefers-color-scheme: light)").matches',
    );
    expect(themeToggle).toContain("const userHasToggled = useRef(false);");
    expect(baseLayout).toContain(
      'if(!t){t=matchMedia("(prefers-color-scheme: light)").matches?"light":"dark"}',
    );
    expect(baseLayout).not.toContain("localStorage.setItem");
  });
});
