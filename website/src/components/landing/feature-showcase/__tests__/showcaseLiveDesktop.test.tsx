import { describe, test, expect } from "bun:test";
import React from "react";
import { renderToStaticMarkup } from "react-dom/server";
import { FeatureShowcaseFallback } from "../FeatureShowcaseFallback";
import { ShowcaseLiveDesktop } from "../ShowcaseLiveDesktop";

describe("ShowcaseLiveDesktop component", () => {
  test("renders accessible button group and single outer role=img for visual host", () => {
    const html = renderToStaticMarkup(<ShowcaseLiveDesktop />);

    expect(html).toContain("<fieldset");
    expect(html).toContain('aria-label="Feature showcase scenes"');
    expect(html).toContain('role="img"');
    expect(html).toContain('aria-label="Maho Browser feature showcase illustration showing spaces preview"');
    expect(html).toContain('aria-hidden="true"');
    expect(html).toContain('aria-hidden="true" class="w-full h-full pointer-events-none"');
    expect(html).toContain("Pause showcase animation");
  });

  test("uses Maho editorial heading primitives and readable inactive cards", () => {
    const html = renderToStaticMarkup(<ShowcaseLiveDesktop />);

    expect(html).toContain('class="landing-eyebrow text-muted-foreground"');
    expect(html).toContain('class="landing-section-title"');
    expect(html).not.toContain("opacity-60");
    expect(html).not.toContain("purple-");
  });

  test("fallback uses the same Maho editorial heading primitives and restrained accent", () => {
    const html = renderToStaticMarkup(<FeatureShowcaseFallback />);

    expect(html).toContain('class="landing-eyebrow text-muted-foreground"');
    expect(html).toContain('class="landing-section-title"');
    expect(html).not.toContain("purple-");
  });
});
