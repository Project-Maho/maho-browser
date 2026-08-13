import { describe, expect, test } from "bun:test";
import { access, readFile } from "node:fs/promises";

const FEATURE_GRID_URL = new URL("../../FeatureGridSection.astro", import.meta.url);
const AGENT_PANEL_URL = new URL("../AgentPanelPreview.tsx", import.meta.url);
const MINI_MAHO_URL = new URL("../../MiniMahoPreview.astro", import.meta.url);
const MINI_MAHO_VIDEO_URL = new URL(
  "../../../../../public/demo/hero/maho-mini-maho.mp4",
  import.meta.url,
);
const MINI_MAHO_POSTER_URL = new URL(
  "../../../../../public/demo/hero/maho-mini-maho-poster.png",
  import.meta.url,
);

describe("Feature grid product surfaces", () => {
  test("mounts the looping Agent panel preview client-only", async () => {
    const [gridSource, previewSource] = await Promise.all([
      readFile(FEATURE_GRID_URL, "utf8"),
      readFile(AGENT_PANEL_URL, "utf8"),
    ]);

    expect(gridSource).toContain("feature-showcase/AgentPanelPreview");
    expect(gridSource).toContain('<AgentPanelPreview client:only="react" />');
    expect(gridSource).not.toContain("./AgentPanelPreview.astro");
    expect(previewSource).toContain("AgentPanelSurface");
    expect(previewSource).toContain('position="standalone"');
    expect(previewSource).toContain("requestAnimationFrame");
  });

  test("renders the dedicated Mini Maho motion preview", async () => {
    const [gridSource, previewSource] = await Promise.all([
      readFile(FEATURE_GRID_URL, "utf8"),
      readFile(MINI_MAHO_URL, "utf8"),
    ]);

    expect(gridSource).toContain("MiniMahoPreview");
    expect(gridSource).toContain("<MiniMahoPreview");
    expect(gridSource).not.toContain("maho-browser-hero-full.png");
    expect(gridSource).toContain("@media (min-width: 900px)");
    expect(previewSource).toContain('data-video-path="/demo/hero/maho-mini-maho"');
    expect(previewSource).toContain("/demo/hero/maho-mini-maho-poster.png");

    await access(MINI_MAHO_VIDEO_URL);
    await access(MINI_MAHO_POSTER_URL);
  });
});
