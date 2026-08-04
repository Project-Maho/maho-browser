import { expect, test } from "@playwright/test";
import type { ConsoleMessage, Page } from "@playwright/test";
import { CAPTURE_REQUESTS } from "../scripts/render-maho-features.mjs";
import { CAPTURE_SCENES, SCENE_REGISTRY } from "../src/components/landing/feature-showcase/data/scenes";
import {
  assertHeroOnlyHomepage,
  assertHostGeometry,
  assertImagesReady,
  assertSceneContent,
  capturePath,
  type CaptureQuery,
} from "./maho-feature-visual-helpers";

const BASELINE_THEME = "#b58ad8";
const NORMALIZED_DEFAULT_ATTRIBUTES = {
  "data-scene": "spaces",
  "data-theme": BASELINE_THEME,
  "data-progress": "0",
  "data-profile": "feature",
} as const;

type BrowserConsoleFailure = {
  readonly text: string;
  readonly url: string;
  readonly lineNumber: number;
  readonly columnNumber: number;
};
type BrowserErrors = { readonly console: BrowserConsoleFailure[]; readonly page: string[] };

const errorsByPage = new WeakMap<Page, BrowserErrors>();
test.beforeEach(async ({ page }) => {
  const errors: BrowserErrors = { console: [], page: [] }; errorsByPage.set(page, errors);
  page.on("pageerror", (error) => errors.page.push(error.message));
  page.on("console", (message: ConsoleMessage) => {
    if (message.type() !== "error") return;
    const { url, lineNumber, columnNumber } = message.location();
    errors.console.push({ text: message.text(), url, lineNumber, columnNumber });
  });
});

test.afterEach(async ({ page }) => {
  const errors = errorsByPage.get(page);
  expect(errors?.page ?? []).toEqual([]);
  expect(errors?.console ?? []).toEqual([]);
});

test.describe("Maho homepage Hero-only composition", () => {
  for (const viewport of [
    { width: 375, height: 812 },
    { width: 768, height: 1024 },
    { width: 1279, height: 900 },
    { width: 1280, height: 900 },
  ] as const) {
    test(`${viewport.width}px mounts exactly one Hero and no lower FeatureShowcase`, async ({ page }) => {
      await page.setViewportSize(viewport);
      await assertHeroOnlyHomepage(page);
    });
  }

  test("1280px reduced motion still mounts exactly one Hero and no lower FeatureShowcase", async ({ page }) => {
    await page.emulateMedia({ reducedMotion: "reduce" });
    await page.setViewportSize({ width: 1280, height: 900 });
    await assertHeroOnlyHomepage(page);
  });
});

test.describe("Maho Feature frozen capture scenes", () => {
  test("renderer capture requests cover application capture scenes", () => {
    expect(Object.keys(CAPTURE_REQUESTS)).toEqual(CAPTURE_SCENES);
  });

  for (const scene of CAPTURE_SCENES) {
    test(`${scene} renders its frozen capture contract`, async ({ page }, testInfo) => {
      const progress = CAPTURE_REQUESTS[scene].progress;
      await page.goto(
        capturePath({ scene, theme: BASELINE_THEME, progress: String(progress), profile: "feature" }),
        { waitUntil: "domcontentloaded" },
      );
      const host = page.locator(`#capture-host[data-capture-ready='true'][data-scene='${scene}']`);
      await expect(host).toBeVisible({ timeout: 15_000 });
      await expect(host).toHaveAttribute("data-theme", BASELINE_THEME);
      await expect(host).toHaveAttribute("data-progress", String(progress));
      await expect(host).toHaveAttribute("data-profile", "feature");
      await assertHostGeometry(host, 1360, 860);
      await assertImagesReady(host);
      await assertSceneContent(host, scene);
      await testInfo.attach(`maho-feature-${scene}.png`, {
        body: await host.screenshot({ animations: "disabled" }),
        contentType: "image/png",
      });
    });
  }

  test("split-view renders the prepared drag capture during action progress", async ({ page }) => {
    const progress = 0.62;
    await page.goto(
      capturePath({ scene: "split-view", theme: BASELINE_THEME, progress: String(progress), profile: "feature" }),
      { waitUntil: "domcontentloaded" },
    );
    const host = page.locator("#capture-host[data-capture-ready='true'][data-scene='split-view']");
    await expect(host).toBeVisible({ timeout: 15_000 });
    await assertHostGeometry(host, 1360, 860);
    await assertImagesReady(host);
    await assertSceneContent(host, "split-view");
  });

  test("split-view renders the full split capture at representative hold", async ({ page }) => {
    const progress = SCENE_REGISTRY["split-view"].representativeHoldProgress;
    await page.goto(
      capturePath({ scene: "split-view", theme: BASELINE_THEME, progress: String(progress), profile: "feature" }),
      { waitUntil: "domcontentloaded" },
    );
    const host = page.locator("#capture-host[data-capture-ready='true'][data-scene='split-view']");
    await expect(host).toBeVisible({ timeout: 15_000 });
    await assertHostGeometry(host, 1360, 860);
    await assertImagesReady(host);
    await assertSceneContent(host, "split-view");
    await expect(host.locator("[data-scene-overlay='split-drag']")).toHaveCount(0);
  });

  test("reference profile exposes exact 1440x1118 geometry", async ({ page }) => {
    await page.goto(
      capturePath({ scene: "spaces", theme: BASELINE_THEME, progress: "0.32", profile: "reference" }),
      { waitUntil: "domcontentloaded" },
    );
    const host = page.locator("#capture-host[data-capture-ready='true']");
    await expect(host).toBeVisible({ timeout: 15_000 });
    await expect(host).toHaveAttribute("data-profile", "reference");
    await assertHostGeometry(host, 1440, 1118);
    await assertImagesReady(host);
  });
});

test.describe("Maho Feature capture query normalization", () => {
  const cases = [
    { name: "constructor scene", query: { scene: "constructor" }, expected: NORMALIZED_DEFAULT_ATTRIBUTES },
    { name: "__proto__ scene", query: { scene: "__proto__" }, expected: NORMALIZED_DEFAULT_ATTRIBUTES },
    { name: "unknown scene", query: { scene: "unknown" }, expected: NORMALIZED_DEFAULT_ATTRIBUTES },
    { name: "invalid theme", query: { theme: "purple" }, expected: NORMALIZED_DEFAULT_ATTRIBUTES },
    { name: "invalid progress", query: { progress: "0.5extra" }, expected: NORMALIZED_DEFAULT_ATTRIBUTES },
    { name: "nonfinite progress", query: { progress: "Infinity" }, expected: NORMALIZED_DEFAULT_ATTRIBUTES },
    { name: "low progress", query: { progress: "-1" }, expected: NORMALIZED_DEFAULT_ATTRIBUTES },
    {
      name: "high progress",
      query: { progress: "2" },
      expected: { ...NORMALIZED_DEFAULT_ATTRIBUTES, "data-progress": "1" },
    },
    { name: "invalid profile", query: { profile: "preview" }, expected: NORMALIZED_DEFAULT_ATTRIBUTES },
  ] as const satisfies readonly {
    readonly name: string;
    readonly query: CaptureQuery;
    readonly expected: Readonly<Record<string, string>>;
  }[];

  for (const testCase of cases) {
    test(`normalizes ${testCase.name}`, async ({ page }) => {
      await page.goto(capturePath(testCase.query), { waitUntil: "domcontentloaded" });
      const host = page.locator("#capture-host[data-capture-ready='true']");
      await expect(host).toBeVisible({ timeout: 15_000 });
      for (const [attribute, value] of Object.entries(testCase.expected)) {
        await expect(host).toHaveAttribute(attribute, value);
      }
      await assertImagesReady(host);
    });
  }
});

test.describe("Maho homepage recorded Hero sequence", () => {
  test.use({ viewport: { width: 1440, height: 1000 } });

  test("SSR starts with the Spaces poster and no video source URLs", async ({ page }) => {
    const response = await page.request.get("/");
    expect(response.ok()).toBe(true);
    const html = await response.text();

    expect(html).toContain("/demo/hero/dom-hyperframes/maho-spaces-dom-poster.webp");
    expect(html).not.toContain("/demo/hero/hyperframes/");
    expect(html).not.toContain(".mp4");
    expect(html).not.toContain("<source");
    expect(html).not.toContain("maho-spaces-monet");
  });

  test("Hero mounts only the selected final MP4, crossfades scenes, pauses offscreen, and replays Spaces", async ({ page }) => {
    const heroMediaRequests = new Set<string>();
    page.on("request", (request) => { if (request.url().includes("/demo/hero/")) heroMediaRequests.add(new URL(request.url()).pathname); });
    await page.goto("/", { waitUntil: "domcontentloaded" });
    const hero = page.locator("[data-motion-surface='hero']");
    await hero.scrollIntoViewIfNeeded();
    await expect(hero).toHaveAttribute("data-hero-policy", "motion");
    await expect(hero).toHaveAttribute("data-motion-owner", "hero");
    await expect(hero.locator("[role='img']")).toHaveAttribute("aria-label", "Maho Browser hero showing Spaces");
    await expect(hero.locator("video")).toHaveCount(1);
    await expect(hero.locator("source")).toHaveCount(0);

    const spacesVideo = hero.locator("video[src='/demo/hero/dom-hyperframes/maho-spaces-dom.mp4']");
    await expect(spacesVideo).toHaveAttribute("poster", "/demo/hero/dom-hyperframes/maho-spaces-dom-poster.webp");
    await expect(spacesVideo).toHaveAttribute("width", "1920");
    await expect(spacesVideo).toHaveAttribute("height", "1080");
    await expect.poll(() => spacesVideo.evaluate((element) => element instanceof HTMLVideoElement && !element.paused)).toBe(true);
    expect(heroMediaRequests).toContain("/demo/hero/dom-hyperframes/maho-spaces-dom.mp4");
    expect([...heroMediaRequests].filter((path) => path.endsWith(".mp4"))).toEqual(["/demo/hero/dom-hyperframes/maho-spaces-dom.mp4"]);
    await hero.getByRole("button", { name: "Pause hero video" }).click();
    await expect(hero.getByRole("button", { name: "Resume hero video" })).toHaveAttribute("aria-pressed", "true");
    await expect.poll(() => spacesVideo.evaluate((element) => element instanceof HTMLVideoElement && element.paused)).toBe(true);
    await hero.getByRole("button", { name: "Resume hero video" }).click();
    await expect.poll(() => spacesVideo.evaluate((element) => element instanceof HTMLVideoElement && !element.paused)).toBe(true);

    await hero.getByRole("button", { name: "Split View", exact: true }).click();
    await expect(hero).toHaveAttribute("data-hero-scene", "split-view");
    await expect(hero.locator("[role='img']")).toHaveAttribute("aria-label", "Maho Browser hero showing Split View");
    await expect(hero.locator("video")).toHaveCount(1);
    const splitVideo = hero.locator("video[src='/demo/hero/dom-hyperframes/maho-split-view-dom.mp4']");
    await expect(splitVideo).toHaveAttribute("poster", "/demo/hero/dom-hyperframes/maho-split-view-dom-poster.webp");
    await expect(splitVideo).toHaveCSS("object-fit", "contain");
    await expect(splitVideo).toHaveCSS("transition-duration", "0.32s");
    await expect(splitVideo).toHaveCSS("opacity", "1");
    await expect(spacesVideo).toHaveCount(0);
    expect(heroMediaRequests).toContain("/demo/hero/dom-hyperframes/maho-split-view-dom.mp4");
    expect([...heroMediaRequests].some((path) => path.includes("mobile"))).toBe(false);
    expect([...heroMediaRequests].some((path) => path.includes("master"))).toBe(false);
    expect([...heroMediaRequests].some((path) => !path.startsWith("/demo/hero/dom-hyperframes/"))).toBe(false);
    expect([...heroMediaRequests].some((path) => path.startsWith("/demo/hero/hyperframes/"))).toBe(false);

    await splitVideo.dispatchEvent("ended");
    await expect(hero).toHaveAttribute("data-hero-scene", "command-bar-ai");
    const commandVideo = hero.locator("video[src='/demo/hero/dom-hyperframes/maho-command-ai-dom.mp4']");
    await expect(commandVideo).toHaveAttribute("poster", "/demo/hero/dom-hyperframes/maho-command-ai-dom-poster.webp");
    await commandVideo.dispatchEvent("ended");
    await expect(hero).toHaveAttribute("data-hero-scene", "spaces");

    await page.evaluate(() => window.scrollTo(0, document.body.scrollHeight));
    await expect(hero).toHaveAttribute("data-motion-owner", "none");
    await expect.poll(() => spacesVideo.evaluate((element) => element instanceof HTMLVideoElement && element.paused)).toBe(true);
    await hero.scrollIntoViewIfNeeded();
    await expect(hero).toHaveAttribute("data-motion-owner", "hero");
    await hero.getByRole("button", { name: "Replay hero sequence" }).click();
    await expect(hero).toHaveAttribute("data-hero-scene", "spaces");
    const replayedSpacesVideo = hero.locator("video[src='/demo/hero/dom-hyperframes/maho-spaces-dom.mp4']");
    await expect.poll(() => replayedSpacesVideo.evaluate((element) => element instanceof HTMLVideoElement ? element.currentTime : Number.POSITIVE_INFINITY)).toBeLessThan(0.5);
  });

  test("reduced motion selects posters without mounting video sources", async ({ page }) => {
    await page.emulateMedia({ reducedMotion: "reduce" });
    await page.goto("/", { waitUntil: "domcontentloaded" });
    const hero = page.locator("[data-motion-surface='hero']");
    await expect(hero).toHaveAttribute("data-hero-policy", "static");
    await expect(hero.locator("video, source")).toHaveCount(0);
    await hero.getByRole("button", { name: "Command Bar + AI", exact: true }).click();
    await expect(hero.locator("[role='img']")).toHaveAttribute("aria-label", "Maho Browser hero showing Command Bar + AI");
    await expect(hero.locator("img")).toHaveAttribute("src", "/demo/hero/dom-hyperframes/maho-command-ai-dom-poster.webp");
    await expect(hero.locator("video, source")).toHaveCount(0);
  });

  test("Save-Data selects posters without mounting video sources", async ({ page }) => {
    await page.addInitScript(() => {
      const connection = new EventTarget();
      Object.defineProperty(connection, "saveData", { value: true });
      Object.defineProperty(navigator, "connection", { configurable: true, value: connection });
    });
    await page.goto("/", { waitUntil: "domcontentloaded" });
    const hero = page.locator("[data-motion-surface='hero']");
    await expect(hero).toHaveAttribute("data-hero-policy", "static");
    await expect(hero.locator("video, source")).toHaveCount(0);
    await hero.getByRole("button", { name: "Split View", exact: true }).click();
    await expect(hero.locator("img")).toHaveAttribute("src", "/demo/hero/dom-hyperframes/maho-split-view-dom-poster.webp");
    await expect(hero.locator("video, source")).toHaveCount(0);
  });

  test("a delayed failed play from the previous scene cannot corrupt the newly selected scene", async ({ page }) => {
    await page.addInitScript(() => {
      const originalPlay = HTMLMediaElement.prototype.play;
      let rejectDelayedPlay: ((reason?: unknown) => void) | null = null;
      let shouldDelaySpacesPlay = true;

      HTMLMediaElement.prototype.play = function play(): Promise<void> {
        if (shouldDelaySpacesPlay && this.currentSrc.includes("maho-spaces-dom.mp4")) {
          shouldDelaySpacesPlay = false;
          return new Promise((_resolve, reject) => {
            rejectDelayedPlay = reject;
          });
        }
        return originalPlay.call(this);
      };

      window.addEventListener("reject-stale-hero-play", () => {
        rejectDelayedPlay?.(new DOMException("Delayed stale playback failure", "NotSupportedError"));
        rejectDelayedPlay = null;
      });
    });
    await page.goto("/", { waitUntil: "domcontentloaded" });
    const hero = page.locator("[data-motion-surface='hero']");
    await hero.scrollIntoViewIfNeeded();
    await expect(hero.locator("video[src='/demo/hero/dom-hyperframes/maho-spaces-dom.mp4']")).toHaveCount(1);

    await hero.getByRole("button", { name: "Split View", exact: true }).click();
    await expect(hero).toHaveAttribute("data-hero-scene", "split-view");
    await expect(hero).toHaveAttribute("data-hero-video-state", "ready");
    await page.evaluate(() => window.dispatchEvent(new Event("reject-stale-hero-play")));

    await expect(hero).toHaveAttribute("data-hero-scene", "split-view");
    await expect(hero).toHaveAttribute("data-hero-video-state", "ready");
    await expect(hero.locator("[role='img']")).toHaveAttribute("aria-label", "Maho Browser hero showing Split View");
    await expect(hero.locator("video[src='/demo/hero/dom-hyperframes/maho-split-view-dom.mp4']")).toHaveCount(1);
  });

  test("Split View video 404 retains its final poster and announces the unavailable state", async ({ page }) => {
    await page.route("**/demo/hero/dom-hyperframes/maho-split-view-dom.mp4", async (route) => route.fulfill({ status: 404, contentType: "video/mp4", body: "" }));
    await page.goto("/", { waitUntil: "domcontentloaded" });
    const hero = page.locator("[data-motion-surface='hero']");
    await hero.scrollIntoViewIfNeeded();
    await expect(hero).toHaveAttribute("data-hero-policy", "motion");

    await hero.getByRole("button", { name: "Split View", exact: true }).click();

    await expect(hero).toHaveAttribute("data-hero-scene", "split-view");
    await expect(hero).toHaveAttribute("data-hero-video-state", "unavailable");
    await expect(hero.locator("[role='img']")).toHaveAttribute("aria-label", "Maho Browser hero showing Split View poster; video unavailable");
    await expect(hero.locator("img")).toHaveAttribute("src", "/demo/hero/dom-hyperframes/maho-split-view-dom-poster.webp");
    await expect(hero.locator("video, source")).toHaveCount(0);
    await expect(hero.locator("[aria-live='polite']")).toHaveText("Split View video unavailable; showing poster");

    const errors = errorsByPage.get(page);
    expect(errors?.page ?? []).toEqual([]);
    expect(errors?.console ?? []).toEqual([
      {
        text: "Failed to load resource: the server responded with a status of 404 (Not Found)",
        url: new URL("/demo/hero/dom-hyperframes/maho-split-view-dom.mp4", page.url()).href,
        lineNumber: 0,
        columnNumber: 0,
      },
    ]);
    errors?.console.shift();
  });
});
