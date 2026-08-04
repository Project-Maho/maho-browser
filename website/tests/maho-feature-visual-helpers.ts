import { expect } from "@playwright/test";
import type { Locator, Page } from "@playwright/test";
import { getSplitPhase } from "../src/components/landing/feature-showcase/timeline";
import type { SceneKey } from "../src/components/landing/feature-showcase/timeline";

export type CaptureQuery = {
  readonly scene?: string;
  readonly theme?: string;
  readonly progress?: string;
  readonly profile?: string;
};

const CAPTURE_ROUTE = "/capture/maho-feature/";

export function capturePath(query: CaptureQuery): string {
  const search = new URLSearchParams();
  if (query.scene !== undefined) search.set("scene", query.scene);
  if (query.theme !== undefined) search.set("theme", query.theme);
  if (query.progress !== undefined) search.set("p", query.progress);
  if (query.profile !== undefined) search.set("profile", query.profile);
  return `${CAPTURE_ROUTE}?${search.toString()}`;
}

export async function assertHeroOnlyHomepage(page: Page): Promise<void> {
  await page.goto("/", { waitUntil: "domcontentloaded" });
  const hero = page.locator("[data-motion-surface='hero']");
  await expect(hero).toHaveCount(1);
  await expect(hero).toBeVisible();
  await expect(page.locator("[data-feature-showcase-host]")).toHaveCount(0);
  await expect(page.locator("[data-motion-surface='showcase']")).toHaveCount(0);
}

export async function assertHostGeometry(host: Locator, width: number, height: number): Promise<void> {
  const geometry = await host.evaluate((element) => {
    const rect = element.getBoundingClientRect();
    return {
      width: rect.width,
      height: rect.height,
      fullyWithinViewport: rect.left >= 0 && rect.top >= 0 && rect.right <= window.innerWidth && rect.bottom <= window.innerHeight,
    };
  });
  expect(geometry).toEqual({ width, height, fullyWithinViewport: true });
}

export async function assertImagesReady(host: Locator): Promise<void> {
  const images = host.locator("img");
  const imageCount = await images.count();
  if (imageCount === 0) {
    await expect(host.locator("[data-split-pane], [data-command-background]").first()).toBeVisible();
    return;
  }
  const states = await images.evaluateAll((elements) => elements.map((element) => ({
    complete: element instanceof HTMLImageElement && element.complete,
    naturalWidth: element instanceof HTMLImageElement ? element.naturalWidth : 0,
  })));
  expect(states.every((state) => state.complete && state.naturalWidth > 0)).toBe(true);
}

export async function getBoundingBox(locator: Locator, label: string): Promise<NonNullable<Awaited<ReturnType<Locator["boundingBox"]>>>> {
  const box = await locator.boundingBox();
  if (box === null) throw new Error(`Expected ${label} to have a bounding box`);
  return box;
}

function expectCssGeometry(pairs: readonly (readonly [actual: number, expected: number])[]): void {
  for (const [actual, expected] of pairs) expect(Math.abs(actual - expected)).toBeLessThanOrEqual(1 / 64);
}

export async function assertSceneContent(host: Locator, scene: SceneKey): Promise<void> {
  const root = host.locator(`[data-scene-root='${scene}']`);
  await expect(root).toBeVisible();
  await expect(root).toHaveCount(1);
  await expect(host.locator("[data-scene-root]")).toHaveCount(1);
  const rootBox = await getBoundingBox(root, `${scene} scene root`);

  switch (scene) {
    case "spaces": {
      await expect(host.locator("[data-showcase='address']")).toHaveCount(1);
      await expect(host.locator("[data-showcase='favorite-tile']")).toHaveCount(6);
      await expect(root.locator("[data-showcase='content-layer']")).toHaveCount(4);
      await expect(host.locator("img[src^='/demo/favicons/']").first()).toBeVisible();
      await expect(host.locator("img[src^='/demo/spaces-recording/']")).toHaveCount(0);
      return;
    }
    case "split-view": {
      await expect(root).toHaveAttribute("data-split-preview-duration-ms", "170");
      await expect(root).toHaveAttribute("data-split-reveal-duration-ms", "200");
      const progress = Number(await host.getAttribute("data-progress"));
      const phase = getSplitPhase(progress);
      const isSplit = phase === "settling" || phase === "settled";
      const open = isSplit ? 1 : 0;
      const leftPane = root.locator("[data-split-pane='left']");
      const rightPane = root.locator("[data-split-pane='right']");
      const divider = root.locator("[data-split-divider='true']");
      await expect(root.locator("[data-split-pane]")).toHaveCount(2);
      await expect(rightPane).toBeVisible();
      await expect(leftPane).toHaveAttribute("data-split-reveal-ratio", String(open));
      await expect(leftPane).toHaveCSS("transition-duration", "0.2s");
      const leftOpacity = await leftPane.evaluate((element) => Number(getComputedStyle(element).opacity));
      expect(leftOpacity).toBeCloseTo(open, 5);
      await expect(root.locator("[data-demo-page='article']")).toBeVisible();

      if (isSplit) {
        await expect(divider).toBeVisible();
        await expect(root.locator("[data-demo-page='longreads']")).toBeVisible();
        const leftBox = await getBoundingBox(leftPane, "left split pane");
        expect(Math.abs(leftBox.width - rootBox.width * 0.5)).toBeLessThanOrEqual(12);
        expect(Math.abs(leftBox.height - rootBox.height)).toBeLessThanOrEqual(1 / 64);
      } else {
        await expect(divider).toHaveCount(0);
      }

      const combinedRow = host.locator("[data-split-combined-row='true']");
      if (phase === "settled") {
        await expect(combinedRow).toBeVisible();
      } else {
        await expect(combinedRow).toHaveCount(0);
      }
      await expect(host.locator("[data-split-sidebar-drag='true']")).toHaveCount(0);
      await expect(host.locator("[data-split-drop-target='true']")).toHaveCount(0);

      await expect(root.locator("[data-scene-overlay='split-drag']")).toHaveCount(0);
      await expect(root.locator("[data-scene-image='split-view'], [data-scene-image='split-drag']")).toHaveCount(0);
      return;
    }
    case "command-bar": {
      const palette = root.locator("[data-scene-overlay='command-palette']");
      await expect(root.locator("[data-command-background='document']")).toBeVisible();
      await expect(palette).toBeVisible();
      await expect(palette).toHaveAttribute("data-command-width-ratio", "0.805");
      await expect(palette).toHaveAttribute("data-command-height-ratio", "0.29");
      await expect(palette).toHaveCSS("transition-duration", "0.35s");
      await expect(palette.locator("[data-command-input='true']")).toBeVisible();
      await expect(palette.locator("[data-command-result='selected']")).toBeVisible();
      await expect(palette.locator("[data-command-shortcut='true']")).toBeVisible();
      const paletteBox = await getBoundingBox(palette, "command palette");
      expect(Math.abs(paletteBox.width - rootBox.width * 0.805)).toBeLessThanOrEqual(8);
      expect(Math.abs(paletteBox.height - rootBox.height * 0.29)).toBeLessThanOrEqual(8);
      expect(paletteBox.x).toBeGreaterThan(rootBox.x);
      await expect(root.locator("[data-scene-image='command-bar']")).toHaveCount(0);
      return;
    }
    case "agent": {
      const panel = root.locator("[data-scene-overlay='ai-panel']");
      await expect(panel).toBeVisible();
      await expect(panel).toHaveCSS("transition-duration", "0.35s");
      const panelBox = await getBoundingBox(panel, "AI panel");
      expectCssGeometry([
        [panelBox.width, rootBox.width * 0.48],
        [panelBox.x + panelBox.width, rootBox.x + rootBox.width],
        [panelBox.y, rootBox.y],
        [panelBox.height, rootBox.height],
      ]);
      await expect(panel).toHaveAttribute("data-agent-panel-position", "right-docked");
      const panelImage = panel.locator("[data-scene-image='agent-panel']");
      await expect(panelImage).toHaveAttribute("src", "/demo/ref-agent-panel.png");
      await expect(panelImage).toHaveCSS("object-fit", "contain");
      await expect(panelImage).toHaveCSS("object-position", "100% 50%");
      expect(await getBoundingBox(panelImage, "Agent panel capture")).toEqual(panelBox);
      const background = root.locator("[data-agent-background='subdued-page']");
      await expect(background).toBeVisible();
      expect(await getBoundingBox(background, "Agent subdued background")).toEqual(rootBox);
      await expect(panel.locator("[data-agent-composer], [data-agent-quick-action], [data-agent-submit]")).toHaveCount(0);
      return;
    }
    default:
      return assertNever(scene);
  }
}

function assertNever(value: never): never {
  throw new TypeError(`Unhandled capture scene: ${String(value)}`);
}
