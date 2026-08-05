import { describe, expect, test } from "bun:test";
import { readFile } from "node:fs/promises";

const PRICING_SECTION_URL = new URL("../../PricingSection.tsx", import.meta.url);
const LANDING_PAGE_URL = new URL("../../../LandingPage.tsx", import.meta.url);

describe("Landing pricing footer", () => {
  test("links to the complete pricing page", async () => {
    const [pricingSection, landingPage] = await Promise.all([
      readFile(PRICING_SECTION_URL, "utf8"),
      readFile(LANDING_PAGE_URL, "utf8"),
    ]);

    expect(pricingSection).toContain(
      "footerLink?: { label: string; href: string };",
    );
    expect(pricingSection).toContain("href={config.footerLink.href}");
    expect(landingPage).toContain(
      'footerLink: { label: "See full pricing", href: "/pricing" },',
    );
    expect(landingPage).not.toContain("See full pricing.");
  });
});
