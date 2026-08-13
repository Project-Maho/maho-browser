import { readdir, readFile } from "node:fs/promises";
import { describe, test, expect } from "bun:test";
import { landingContent } from "../../../LandingPage";
import { SCENES } from "../timeline";

const HANGUL_OR_JAMO = /[\uAC00-\uD7A3\u3130-\u318F]/;
const FEATURE_SHOWCASE_URL = new URL("../../FeatureShowcase.tsx", import.meta.url);
const FEATURE_SHOWCASE_SOURCE_DIR = new URL("../", import.meta.url);
const INDEX_URL = new URL("../../../../pages/index.astro", import.meta.url);
const FALLBACK_URL = new URL("../FeatureShowcaseFallback.tsx", import.meta.url);
const AGENT_SCENE_URL = new URL("../AgentScene.tsx", import.meta.url);
const AGENT_PANEL_SURFACE_URL = new URL("../AgentPanelSurface.tsx", import.meta.url);

const AGENT_DESCRIPTION = "Analyze the current page when you ask — user-started, with your own key or local models, and always under your control. Agent never clicks, purchases, submits forms, or uses tools on its own.";

async function getProductionTsFiles(dirUrl: URL): Promise<URL[]> {
  const entries = await readdir(dirUrl, { withFileTypes: true });
  const urls: URL[] = [];
  for (const entry of entries) {
    if (entry.name === "__tests__" || entry.name.endsWith(".test.ts") || entry.name.includes(".omo")) {
      continue;
    }
    const entryUrl = new URL(`${entry.name}${entry.isDirectory() ? "/" : ""}`, dirUrl);
    if (entry.isDirectory()) {
      urls.push(...(await getProductionTsFiles(entryUrl)));
    } else if (entry.isFile() && /\.tsx?$/.test(entry.name)) {
      urls.push(entryUrl);
    }
  }
  return urls;
}

const EXPECTED_SCENE_COPY = [
  {
    badge: "Spaces",
    description: "Persistent workspaces in the sidebar. Group tabs by project, client, or context. Switch instantly with ⌃1–9. Pick up where you left off.",
  },
  {
    badge: "Split View",
    description: "Two pages side by side. Compare, reference, and multitask without switching tabs.",
  },
  {
    badge: "Command Bar",
    description: "One shortcut for everything. Navigate, search open tabs, run actions, jump between Spaces. Keyboard-first from the start.",
  },
  {
    badge: "Agent",
    description: AGENT_DESCRIPTION,
  },
] as const;

describe("FeatureShowcase English-only guard", () => {
  test("contains no Hangul or Jamo in production TypeScript sources", async () => {
    const sourceUrls = await getProductionTsFiles(FEATURE_SHOWCASE_SOURCE_DIR);
    const sourceContents = await Promise.all(
      [FEATURE_SHOWCASE_URL, ...sourceUrls].map(async (sourceUrl) => ({
        file: sourceUrl.pathname,
        content: await readFile(sourceUrl, "utf8"),
      })),
    );

    expect(
      sourceContents
        .filter(({ content }) => HANGUL_OR_JAMO.test(content))
        .map(({ file }) => file),
    ).toEqual([]);
  });

  test("keeps the four canonical English scene badges and descriptions", () => {
    expect(
      SCENES.map(({ badge, description }) => ({ badge, description })),
    ).toEqual(EXPECTED_SCENE_COPY);
  });

  test("keeps the fourth browser feature user-started and under user control", () => {
    expect(landingContent.browser.features.items[3]).toEqual({
      eyebrow: "Agent",
      title: "Agent",
      body: AGENT_DESCRIPTION,
      visual: {
        type: "image",
        alt: "Maho Browser Agent side panel analyzing the current page after the user asked, next to the live web page.",
        sizes: "(min-width: 1024px) 50vw, 100vw",
      },
    });
    expect(landingContent.mail.features.items[5]?.body).toContain("Optional managed AI via OpenRouter");
  });

  test("keeps fallback assets and copy out of homepage ownership", async () => {
    const [indexSource, fallbackSource, agentSceneSource, agentPanelSurfaceSource] = await Promise.all([
      readFile(INDEX_URL, "utf8"),
      readFile(FALLBACK_URL, "utf8"),
      readFile(AGENT_SCENE_URL, "utf8"),
      readFile(AGENT_PANEL_SURFACE_URL, "utf8"),
    ]);

    expect(indexSource).not.toContain("import { FeatureShowcase }");
    expect(indexSource).not.toContain("<FeatureShowcase");
    expect(indexSource).not.toContain("maho-feature-spaces.png");
    expect(indexSource).not.toContain("maho-feature-split-view.png");
    expect(indexSource).not.toContain("maho-feature-command-bar.png");
    expect(indexSource).not.toContain("maho-feature-agent.png");
    expect(indexSource).not.toContain("spacesOptimized");
    expect(indexSource).not.toContain("featureImageSrcByTitle");
    expect(indexSource).not.toContain("featureOptimizedByTitle");
    expect(indexSource).not.toContain("browserFeatures");
    expect(fallbackSource).toContain("maho-feature-spaces.png");
    expect(fallbackSource).toContain("maho-feature-split-view.png");
    expect(fallbackSource).toContain("maho-feature-command-bar.png");
    expect(fallbackSource).toContain("maho-feature-agent.png");
    expect(fallbackSource).toContain('title: "Agent"');
    expect(fallbackSource).toContain('badge: "Agent"');
    expect(fallbackSource).toContain(AGENT_DESCRIPTION);
    expect(agentSceneSource).toContain('position="right-docked"');
    expect(agentSceneSource).toContain('data-agent-background="subdued-page"');
    expect(agentPanelSurfaceSource).toContain('data-agent-topbar="true"');
    expect(agentPanelSurfaceSource).toContain('data-agent-composer="true"');
    expect(agentPanelSurfaceSource).toContain('data-agent-quick-action="summarize"');
    expect(agentPanelSurfaceSource).toContain('data-agent-quick-action="quiz-me"');
    expect(agentSceneSource).not.toContain('src="/demo/ref-agent-panel.png"');

    for (const source of [indexSource, fallbackSource, agentSceneSource, agentPanelSurfaceSource]) {
      expect(source).not.toContain("optional-ai");
      expect(source).not.toContain("Optional AI");
      expect(source).not.toContain("optionalAi");
      expect(source).not.toContain("ready for tab grouping and extraction");
    }
  });
});
