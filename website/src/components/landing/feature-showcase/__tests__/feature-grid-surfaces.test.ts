import { describe, expect, test } from "bun:test";
import { readFile } from "node:fs/promises";

const FEATURE_GRID_URL = new URL("../../FeatureGridSection.astro", import.meta.url);
const MINI_MAHO_URL = new URL("../../MiniMahoPreview.astro", import.meta.url);

describe("Feature grid product surfaces", () => {
  test("renders only the dedicated Agent panel preview", async () => {
    const source = await readFile(FEATURE_GRID_URL, "utf8");

    expect(source).toContain("AgentPanelPreview");
    expect(source).toContain("<AgentPanelPreview");
    expect(source).toContain('data-agent-card-title="true"');
    expect(source).toContain('data-agent-card-media="full-height"');
    expect(source).not.toContain("MahoBrowserMock");
    expect(source).not.toContain('scene="agent"');
    expect(source).not.toContain("maho-feature-agent.png");
  });

  test("renders the dedicated Mini Maho motion preview", async () => {
    const [gridSource, previewSource] = await Promise.all([
      readFile(FEATURE_GRID_URL, "utf8"),
      readFile(MINI_MAHO_URL, "utf8"),
    ]);

    expect(gridSource).toContain("MiniMahoPreview");
    expect(gridSource).toContain("<MiniMahoPreview");
    expect(gridSource).not.toContain("maho-browser-hero-full.png");
    expect(previewSource).toContain("/demo/hero/maho-mini-maho.mp4");
    expect(previewSource).toContain("/demo/hero/maho-mini-maho-poster.png");
  });
});
