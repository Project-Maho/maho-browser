import { describe, test, expect } from "bun:test";
import React from "react";
import { renderToStaticMarkup } from "react-dom/server";
import { CAPTURE_REQUESTS } from "../../../../../scripts/render-maho-features.mjs";
import { MahoBrowserMock } from "../MahoBrowserMock";
import { CAPTURE_SCENES, SCENE_REGISTRY, isSceneKey } from "../data/scenes";
import { CANONICAL_CANVAS, SCENES, TIMELINE_THRESHOLDS } from "../timeline";

describe("Typed Scene Registry", () => {
  test("contains all four capture scene keys", () => {
    expect(CAPTURE_SCENES).toEqual(["spaces", "split-view", "command-bar", "agent"]);
    expect(Object.keys(SCENE_REGISTRY)).toEqual(CAPTURE_SCENES);
    expect(Object.keys(CAPTURE_REQUESTS)).toEqual(CAPTURE_SCENES);
    expect(SCENES.map(({ key }) => key)).toEqual(CAPTURE_SCENES);
    expect(Object.keys(TIMELINE_THRESHOLDS)).toEqual(["splitView", "commandBar", "agent"]);
  });

  test("isSceneKey rejects prototype properties like constructor", () => {
    expect(isSceneKey("constructor")).toBe(false);
    expect(isSceneKey("__proto__")).toBe(false);
    expect(isSceneKey("toString")).toBe(false);
    expect(isSceneKey("spaces")).toBe(true);
    expect(isSceneKey("agent")).toBe(true);
    expect(isSceneKey("optional-ai")).toBe(false);
  });

  test("SCENE_REGISTRY contains exact locked defaults per key", () => {
    expect(SCENE_REGISTRY.spaces.usesSpacesCarousel).toBe(true);
    expect(SCENE_REGISTRY.spaces.defaultActiveDot).toBe(0);

    expect(SCENE_REGISTRY["split-view"].usesSpacesCarousel).toBe(false);
    expect(SCENE_REGISTRY["split-view"].defaultAddress).toBe("aeon.co");
    expect(SCENE_REGISTRY["split-view"].defaultActiveDot).toBe(3);
    expect(SCENE_REGISTRY["split-view"].defaultActiveRowLabel).toBe("Aeon");

    expect(SCENE_REGISTRY["command-bar"].usesSpacesCarousel).toBe(false);
    expect(SCENE_REGISTRY["command-bar"].defaultAddress).toBe("longreads.com");
    expect(SCENE_REGISTRY["command-bar"].defaultActiveDot).toBe(3);
    expect(SCENE_REGISTRY["command-bar"].defaultActiveRowLabel).toBe("Longreads");

    expect(SCENE_REGISTRY.agent.usesSpacesCarousel).toBe(false);
    expect(SCENE_REGISTRY.agent.defaultAddress).toBe("youtube.com");
    expect(SCENE_REGISTRY.agent.defaultActiveDot).toBe(2);
  });

  test("Command Bar representative hold renders DOM palette over browser content", () => {
    const holdProgress = SCENE_REGISTRY["command-bar"].representativeHoldProgress;
    const html = renderToStaticMarkup(
      React.createElement(MahoBrowserMock, { scene: "command-bar", captureProgress: holdProgress }),
    );

    expect(html).toContain('data-scene-root="command-bar"');
    expect(html).toContain('data-command-background="document"');
    expect(html).toContain('data-scene-overlay="command-palette"');
    expect(html).toContain(`data-command-width-ratio="${CANONICAL_CANVAS.commandBarWidthRatio}"`);
    expect(html).toContain('data-command-height-ratio="0.29"');
    expect(html).toContain('data-command-input="true"');
    expect(html).toContain('data-command-result="selected"');
    expect(html).toContain('data-command-shortcut="true"');
    expect(html).not.toContain('src="/demo/ref-command-bar.png"');
    expect(html).not.toContain('data-scene-image="command-bar"');
  });

  test("Split View settled hold renders the two DOM panes and combined sidebar tab", () => {
    const holdProgress = SCENE_REGISTRY["split-view"].representativeHoldProgress;
    const html = renderToStaticMarkup(
      React.createElement(MahoBrowserMock, { scene: "split-view", captureProgress: holdProgress }),
    );

    expect(holdProgress).toBeGreaterThanOrEqual(TIMELINE_THRESHOLDS.splitView.settleEnd);
    expect(html).toContain('data-split-phase="settled"');
    expect(html).toContain('data-split-preview-duration-ms="170"');
    expect(html).toContain('data-split-reveal-duration-ms="200"');
    expect(html).toContain('data-split-pane="left"');
    expect(html).toContain('data-split-pane="right"');
    expect(html).toContain('data-split-divider="true"');
    expect(html).toContain('data-demo-page="longreads"');
    expect(html).toContain('data-demo-page="article"');
    expect(html).toContain('data-split-combined-row="true"');
    expect(html).not.toContain('data-split-sidebar-drag="true"');
    expect(html).not.toContain('data-scene-overlay="split-drag"');
    expect(html).not.toContain('src="/demo/ref-split-view.jpeg"');
    expect(html).not.toContain('src="/demo/ref-split-drag.png"');
  });

  test("Split View drag phase moves Aeon into Longreads' right-side slot", () => {
    const { dragStart, dragEnd } = TIMELINE_THRESHOLDS.splitView;
    const dragProgress = (dragStart + dragEnd) / 2;
    const html = renderToStaticMarkup(
      React.createElement(MahoBrowserMock, { scene: "split-view", captureProgress: dragProgress }),
    );

    expect(dragProgress).toBeGreaterThanOrEqual(dragStart);
    expect(dragProgress).toBeLessThan(dragEnd);
    expect(html).toContain('data-split-phase="drag"');
    expect(html).toContain('data-split-reveal-ratio="0"');
    expect(html).toContain('data-demo-page="article"');
    expect(html).toContain("Longreads");
    expect(html).toContain("Aeon");
    expect(html).not.toContain('data-split-divider="true"');
    expect(html).not.toContain('data-split-combined-row="true"');
    expect(html).toContain('data-split-drop-target="true"');
    expect(html).toContain('data-split-insertion-slot="true"');
    expect(html).toContain('data-split-sidebar-drag="true"');
    expect(html).toContain('data-split-drop-target="true"');
  });

  test("Split View drop snaps both panes fully open in one state change", () => {
    const actionProgress = TIMELINE_THRESHOLDS.splitView.dragEnd;
    const html = renderToStaticMarkup(
      React.createElement(MahoBrowserMock, { scene: "split-view", captureProgress: actionProgress }),
    );

    expect(html).toContain('data-split-phase="settled"');
    expect(html).toContain('data-split-preview-duration-ms="170"');
    expect(html).toContain('data-split-reveal-duration-ms="200"');
    expect(html).toContain('data-split-reveal-ratio="1"');
    expect(html).toContain('data-split-pane="left"');
    expect(html).toContain('data-split-pane="right"');
    expect(html).toContain('data-split-divider="true"');
    expect(html).toContain('data-demo-page="longreads"');
    expect(html).toContain('data-demo-page="article"');
    expect(html).toContain('data-split-combined-row="true"');
    expect(html).not.toContain('data-split-sidebar-drag="true"');
    expect(html).not.toContain('data-split-drop-target="true"');
  });

  test("Split View keeps the left pane collapsed until the drop frame", () => {
    const html = renderToStaticMarkup(
      React.createElement(MahoBrowserMock, {
        scene: "split-view",
        captureProgress: TIMELINE_THRESHOLDS.splitView.dragEnd - 0.01,
      }),
    );

    expect(html).toContain('data-split-reveal-ratio="0"');
    expect(html).toContain('data-split-pane="left"');
    expect(html).toContain('data-split-pane="right"');
    expect(html).not.toContain('data-split-divider="true"');
  });

  test("Command Bar palette remains mounted before reveal so opacity can transition", () => {
    const html = renderToStaticMarkup(
      React.createElement(MahoBrowserMock, { scene: "command-bar", captureProgress: 0 }),
    );

    expect(html).toContain('data-scene-overlay="command-palette"');
    expect(html).toContain('data-command-background="document"');
    expect(html).toContain('data-command-result="selected"');
    expect(html).not.toContain('src="/demo/ref-command-bar.png"');
    expect(html).toContain("duration-[350ms]");
  });

  test("Agent representative hold renders the Maho WebUI panel", () => {
    const holdProgress = SCENE_REGISTRY.agent.representativeHoldProgress;
    const html = renderToStaticMarkup(
      React.createElement(MahoBrowserMock, { scene: "agent", captureProgress: holdProgress }),
    );

    expect(holdProgress).toBeGreaterThanOrEqual(TIMELINE_THRESHOLDS.agent.streamEnd);
    expect(html).toContain('data-scene-overlay="ai-panel"');
    expect(html).toContain('data-agent-panel-position="right-docked"');
    expect(html).toContain('data-agent-background="subdued-page"');
    expect(html).toContain('data-agent-topbar="true"');
    expect(html).toContain('data-agent-composer="true"');
    expect(html).toContain('data-agent-user-message="true"');
    expect(html).toContain('data-agent-assistant-message="true"');
    expect(html).not.toContain('data-agent-quick-action="summarize"');
    expect(html).not.toContain('data-agent-quick-action="quiz-me"');
    expect(html).not.toContain('src="/demo/ref-agent-panel.png"');
    expect(html).toContain("duration-[350ms]");
  });

  test("Agent action progress keeps the live panel composition stable", () => {
    const processingProgress = 0.64;
    const html = renderToStaticMarkup(
      React.createElement(MahoBrowserMock, { scene: "agent", captureProgress: processingProgress }),
    );

    expect(processingProgress).toBeGreaterThan(TIMELINE_THRESHOLDS.agent.streamStart);
    expect(processingProgress).toBeLessThan(TIMELINE_THRESHOLDS.agent.streamEnd);
    expect(html).toContain('data-agent-panel-position="right-docked"');
    expect(html).toContain('data-agent-composer="true"');
    expect(html).toContain('data-agent-submit="true"');
    expect(html).toContain('data-agent-user-message="true"');
    expect(html).toContain('data-agent-assistant-message="true"');
    expect(html).not.toContain('src="/demo/ref-agent-panel.png"');
  });
});
