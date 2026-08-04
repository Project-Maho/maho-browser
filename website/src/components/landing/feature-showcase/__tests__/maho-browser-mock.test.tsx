import React from "react";
import { renderToStaticMarkup } from "react-dom/server";
import { MahoBrowserMock } from "../MahoBrowserMock";
import { TIMELINE_THRESHOLDS } from "../timeline";

type Matcher = {
  readonly toBe: (expected: unknown) => void;
  readonly toContain: (expected: string) => void;
  readonly toHaveLength: (expected: number) => void;
  readonly not: { readonly toContain: (expected: string) => void };
};

declare function describe(name: string, callback: () => void): void;
declare function test(name: string, callback: () => void): void;
declare function expect(value: unknown): Matcher;

describe("MahoBrowserMock Component", () => {
  test("renders static markup with 18 CSS vars and no hardcoded aspect ratio", () => {
    const html = renderToStaticMarkup(
      <MahoBrowserMock themeColor="#b58ad8" scene="spaces" captureProgress={0.32} />,
    );

    expect(html).toContain("data-showcase=\"browser-mock\"");
    expect(html).toContain("--maho-window");
    expect(html).toContain("--maho-chrome-surface");
    expect(html).toContain("container-type:inline-size");
    expect(html).not.toContain("aspect-[");
  });

  test("dual-profile host adaptation renders cleanly in 1360x860 and 1440x1118 hosts", () => {
    const featureProfileHtml = renderToStaticMarkup(
      <div style={{ width: "1360px", height: "860px" }}>
        <MahoBrowserMock themeColor="#b58ad8" scene="split-view" captureProgress={0.5} />
      </div>,
    );
    expect(featureProfileHtml).toContain("1360px");

    const referenceProfileHtml = renderToStaticMarkup(
      <div style={{ width: "1440px", height: "1118px" }}>
        <MahoBrowserMock themeColor="#b58ad8" scene="command-bar" captureProgress={0.5} />
      </div>,
    );
    expect(referenceProfileHtml).toContain("1440px");
  });

  test("renders the shared browser shell for Split View and Command Bar", () => {
    for (const scene of ["split-view", "command-bar"] as const) {
      const html = renderToStaticMarkup(
        <MahoBrowserMock themeColor="#b58ad8" scene={scene} captureProgress={0.78} />,
      );

      expect(html).toContain('data-showcase="browser-mock"');
      expect(html).toContain('data-showcase="address"');
      expect(html).toContain('data-showcase="favorite-tile"');
      expect(html).toContain('data-showcase="footer-dots"');
      expect(html).not.toContain('/demo/ref-split-view.jpeg');
      expect(html).not.toContain('/demo/ref-split-drag.png');
      expect(html).not.toContain('/demo/ref-command-bar.png');
    }
  });

  test("Spaces renders a real DOM sidebar with favicons, not a screenshot paste", () => {
    const html = renderToStaticMarkup(
      <MahoBrowserMock themeColor="#b58ad8" scene="spaces" captureProgress={0.32} />,
    );

    expect(html).toContain('data-scene-root="spaces"');
    expect(html).toContain('data-showcase="address"');
    expect(html).toContain('data-showcase="favorite-tile"');
    expect(html).toContain('/demo/favicons/notion.png');
    expect(html).toContain('data-showcase="content-layer"');
    expect(html).not.toContain('/demo/spaces-recording/');
  });

  test("search pill uses the product sans face instead of mono UI text", () => {
    const html = renderToStaticMarkup(
      <MahoBrowserMock themeColor="#b58ad8" scene="split-view" captureProgress={0.78} />,
    );
    const addressIndex = html.indexOf('data-showcase="address"');
    const addressTag = html.slice(html.lastIndexOf("<span", addressIndex), html.indexOf(">", addressIndex) + 1);

    expect(html.match(/data-showcase="address"/g) ?? []).toHaveLength(1);
    expect(addressTag).toContain("font-sans");
    expect(addressTag).not.toContain("font-mono");
  });

  test("split view drag phase reserves Longreads' right-side insertion slot for Aeon", () => {
    const { dragStart, dragEnd } = TIMELINE_THRESHOLDS.splitView;
    const html = renderToStaticMarkup(
      <MahoBrowserMock
        themeColor="#b58ad8"
        scene="split-view"
        captureProgress={(dragStart + dragEnd) / 2}
      />,
    );

    expect(html).toContain('data-split-phase="drag"');
    expect(html).toContain("Longreads");
    expect(html).toContain("Aeon");
    expect(html).toContain('data-split-drop-target="true"');
    expect(html).toContain('data-split-sidebar-drag="true"');
    expect(html).toContain('data-split-drop-target="true"');
    expect(html).toContain('data-split-insertion-slot="true"');
    expect(html).toContain('w-[19.2cqw]');
  });
});
