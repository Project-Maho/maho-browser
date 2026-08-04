import { readFileSync } from "node:fs";
import React from "react";
import { renderToStaticMarkup } from "react-dom/server";
import { describe, test, expect } from "bun:test";
import { MahoBrowserMock } from "../MahoBrowserMock";
import { MahoSidebar, SIDEBAR_TINT_OPACITY } from "../MahoSidebar";
import { SpacesScene } from "../SpacesScene";
import { WORKSPACES } from "../data/workspaces";
import { getContentBlend, getThemeBlend } from "../spaces-carousel";
import { blendMahoThemes } from "../theme/blendMahoThemes";

describe("feature showcase styling contract", () => {
  const WORKSPACE_THEMES = [
    WORKSPACES[0].theme,
    WORKSPACES[1].theme,
    WORKSPACES[2].theme,
    WORKSPACES[3].theme,
  ] as const;

  test("spaces scene renders four abstract content-layer pages without screenshot pastes", () => {
    const progress = 0.68;

    const html = renderToStaticMarkup(<SpacesScene captureProgress={progress} />);
    expect(html.match(/data-showcase="content-layer"/g) ?? []).toHaveLength(4);
    expect(html).not.toContain('/demo/spaces-recording/');
    expect(html).not.toContain(".mp4");
  });

  test("sidebar tint variables are inherited by a translucent layer beneath opaque content", () => {
    const progress = 0.32;
    const sidebarTheme = blendMahoThemes(WORKSPACE_THEMES, getThemeBlend(progress));
    const html = renderToStaticMarkup(<MahoSidebar spacesProgress={progress} />);
    const scopeIndex = html.indexOf('data-theme-scope="sidebar"');
    const sidebarIndex = html.indexOf("<aside");
    const tintIndex = html.indexOf('data-showcase="sidebar-translucent-tint"');
    const sidebarTag = html.slice(sidebarIndex, html.indexOf(">", sidebarIndex) + 1);
    const tintTag = html.slice(
      html.lastIndexOf("<div", tintIndex),
      html.indexOf(">", tintIndex) + 1,
    );
    const contentIndex = html.indexOf('<div class="relative z-10 flex', tintIndex);
    const contentTag = html.slice(contentIndex, html.indexOf(">", contentIndex) + 1);

    expect(scopeIndex >= 0).toBe(true);
    expect(scopeIndex < sidebarIndex).toBe(true);
    expect(sidebarIndex < tintIndex).toBe(true);
    expect(tintIndex < contentIndex).toBe(true);
    expect(html).toContain(`--maho-sidebar-top:${sidebarTheme.sidebarTop}`);
    expect(html).toContain(`--maho-sidebar-bottom:${sidebarTheme.sidebarBottom}`);
    expect(html).toContain(`--maho-sidebar-gradient:${sidebarTheme.sidebarGradient}`);
    expect(sidebarTag).not.toContain("background:var(--maho-sidebar-gradient)");
    expect(sidebarTag).not.toContain("opacity:");
    expect(tintTag).toContain('data-showcase="sidebar-translucent-tint"');
    expect(tintTag).toContain('aria-hidden="true"');
    expect(tintTag).toContain("pointer-events-none absolute inset-0");
    expect(tintTag).toContain("background:var(--maho-sidebar-gradient)");
    expect(tintTag).toContain(`opacity:${SIDEBAR_TINT_OPACITY}`);
    expect(contentTag).toContain("relative z-10");
    expect(html).not.toContain('data-theme-scope="favorite"');
  });

  test("public className belongs only to the MahoBrowserMock root", () => {
    const html = renderToStaticMarkup(<MahoBrowserMock className="public-mock-root" />);
    const rootStart = html.indexOf('<div data-showcase="browser-mock"');
    const rootTag = html.slice(rootStart, html.indexOf(">", rootStart) + 1);

    expect(rootTag).toContain("public-mock-root");
    expect(html.split("public-mock-root").length - 1).toBe(1);
  });

  test("all scenes expose stable roots and visible overlay hooks", () => {
    const sceneProgress = [
      ["spaces", 0.32],
      ["split-view", 0.5],
      ["command-bar", 0.5],
      ["agent", 0.8],
    ] as const;

    for (const [scene, progress] of sceneProgress) {
      const html = renderToStaticMarkup(
      <MahoBrowserMock scene={scene} captureProgress={progress} />,
      );
      expect(html).toContain(`data-scene-root="${scene}"`);
      if (scene === "spaces") {
        expect(html).toContain('data-showcase="content-layer"');
      } else if (scene === "split-view") {
        expect(html).toContain("data-split-phase=");
        expect(html).toContain("data-split-pane=");
      } else {
        expect(html).toContain("data-scene-overlay=");
      }
    }
  });

  test("Maho-owned scene surfaces contain only documented literal-color exemptions", () => {
    const sourceFiles = [
      "../SpacesScene.tsx",
      "../SplitViewScene.tsx",
      "../CommandBarScene.tsx",
      "../DemoPageSurface.tsx",
      "../AgentScene.tsx",
      "../MahoSidebar.tsx",
      "../WorkspaceBody.tsx",
    ] as const;
    const literals = new Set(
      sourceFiles.flatMap((relativePath) => {
        const source = readFileSync(new URL(relativePath, import.meta.url), "utf8");
        return source.match(/#[0-9a-f]{6}/gi) ?? [];
      }),
    );

    expect([...literals].sort()).toEqual(
      [
        "#28c840",
        "#febc2e",
        "#ff5f57",
      ].sort(),
    );
  });
});
