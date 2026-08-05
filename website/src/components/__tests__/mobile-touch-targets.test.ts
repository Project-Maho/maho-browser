import { describe, expect, test } from "bun:test";
import { readFile } from "node:fs/promises";

const NAV_URL = new URL("../Nav.astro", import.meta.url);
const FOOTER_URL = new URL("../Footer.astro", import.meta.url);

describe("Mobile navigation touch targets", () => {
  test("reserves a 44px target for the compact navigation controls", async () => {
    // Given: the source that renders the mobile header controls.
    const navigation = await readFile(NAV_URL, "utf8");

    // When: a touch user reaches the compact navigation.
    // Then: the primary control and menu trigger each reserve a 44px target.
    expect(navigation).toContain("min-h-11");
    expect(navigation).toContain("h-11 w-11");
  });

  test("reserves a 44px target for every footer navigation link", async () => {
    // Given: the source that renders the footer navigation groups.
    const footer = await readFile(FOOTER_URL, "utf8");

    // When: a touch user navigates the footer.
    // Then: each of its five link groups provides a 44px-tall target.
    expect(footer.match(/min-h-11/g)).toHaveLength(5);
    expect(footer).toContain("min-w-11");
  });
});
