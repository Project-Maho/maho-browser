import { expect, test, type Page } from "@playwright/test";

const PHONE_PROJECTS = new Set(["mobile-320", "mobile-390", "mobile-430"]);
const MOBILE_PROJECTS = new Set([...PHONE_PROJECTS, "touch-tablet-768"]);

function collectRuntimeErrors(page: Page): string[] {
  const errors: string[] = [];
  page.on("console", (message) => {
    if (message.type() === "error") errors.push(message.text());
  });
  page.on("pageerror", (error) => errors.push(error.message));
  return errors;
}

test.describe("Maho homepage mobile contract", () => {
  test.beforeEach(({}, testInfo) => {
    test.skip(
      !MOBILE_PROJECTS.has(testInfo.project.name),
      "The desktop project is covered by the existing visual suite.",
    );
  });

  test("hydrates without errors and never overflows the viewport", async ({
    page,
  }) => {
    const errors = collectRuntimeErrors(page);
    await page.goto("/", { waitUntil: "load" });

    await expect
      .poll(() => page.locator("[data-theme-toggle][data-hydrated=true]").count())
      .toBe(2);
    await expect
      .poll(() =>
        page.evaluate(
          () =>
            document.documentElement.scrollWidth -
            document.documentElement.clientWidth,
        ),
      )
      .toBe(0);
    expect(errors).toEqual([]);
  });

  test("phone drawer owns focus and restores it on Escape", async ({
    page,
  }, testInfo) => {
    test.skip(!PHONE_PROJECTS.has(testInfo.project.name), "Phone viewport only");
    await page.goto("/", { waitUntil: "load" });

    const toggle = page.locator("[data-nav-toggle]");
    await toggle.click();

    await expect(toggle).toHaveAttribute("aria-expanded", "true");
    await expect(page.locator("[data-nav-drawer]")).toBeVisible();
    await expect(page.locator("[data-nav-drawer] a").first()).toBeFocused();
    await expect
      .poll(() => page.evaluate(() => document.body.style.overflow))
      .toBe("hidden");

    await page.keyboard.press("Escape");
    await expect(toggle).toHaveAttribute("aria-expanded", "false");
    await expect(toggle).toBeFocused();
  });

  test("theme toggles stay synchronized and persist the chosen theme", async ({
    page,
  }, testInfo) => {
    await page.goto("/", { waitUntil: "load" });

    const toggles = page.locator("[data-theme-toggle]");
    await expect(toggles).toHaveCount(2);
    const initialTheme = await page.evaluate(() =>
      document.documentElement.classList.contains("dark") ? "dark" : "light",
    );
    const expectedTheme = initialTheme === "dark" ? "light" : "dark";

    const phoneViewport = PHONE_PROJECTS.has(testInfo.project.name);
    if (phoneViewport) {
      await page.locator("[data-nav-toggle]").click();
    }
    await (phoneViewport ? toggles.last() : toggles.first()).click();
    await expect
      .poll(() =>
        page.evaluate(() =>
          document.documentElement.classList.contains("dark") ? "dark" : "light",
        ),
      )
      .toBe(expectedTheme);
    const expectedLabel =
      expectedTheme === "dark" ? "Switch to light mode" : "Switch to dark mode";
    await expect
      .poll(() => toggles.evaluateAll((elements) =>
        elements.map((element) => element.getAttribute("aria-label")),
      ))
      .toEqual([expectedLabel, expectedLabel]);

    await page.reload({ waitUntil: "load" });
    await expect
      .poll(() =>
        page.evaluate(() =>
          document.documentElement.classList.contains("dark") ? "dark" : "light",
        ),
      )
      .toBe(expectedTheme);
  });

  test("touch navigation exposes 44 pixel targets", async ({
    page,
  }, testInfo) => {
    test.skip(
      testInfo.project.name !== "touch-tablet-768",
      "Touch-tablet viewport only",
    );
    await page.goto("/", { waitUntil: "load" });

    const undersizedTargets = await page
      .locator("nav a:visible, nav button:visible")
      .evaluateAll((elements) =>
        elements.flatMap((element) => {
          const rect = element.getBoundingClientRect();
          if (rect.width >= 44 && rect.height >= 44) return [];
          return [
            {
              label:
                element.getAttribute("aria-label") ??
                element.textContent?.trim() ??
                element.tagName,
              width: Math.round(rect.width),
              height: Math.round(rect.height),
            },
          ];
        }),
      );

    expect(undersizedTargets).toEqual([]);
  });

  test("phone clients receive posters without autoplay video", async ({
    page,
  }, testInfo) => {
    test.skip(!PHONE_PROJECTS.has(testInfo.project.name), "Phone viewport only");
    const mediaRequests: string[] = [];
    let transferredImageAndMediaBytes = 0;
    page.on("request", (request) => {
      if (request.resourceType() === "media") {
        mediaRequests.push(new URL(request.url()).pathname);
      }
    });
    page.on("response", (response) => {
      const resourceType = response.request().resourceType();
      if (resourceType !== "image" && resourceType !== "media") return;
      const contentLength = Number(response.headers()["content-length"] ?? 0);
      transferredImageAndMediaBytes += contentLength;
    });

    await page.goto("/", { waitUntil: "load" });

    expect(mediaRequests).toEqual([]);
    expect(transferredImageAndMediaBytes).toBeLessThanOrEqual(2 * 1024 * 1024);
    await expect(page.locator("[data-hero-browser-stage] picture img")).toBeVisible();
    await expect(page.locator("[data-mini-maho-preview] picture img")).toBeAttached();
  });
});

test("desktop mounts the selected Hero scene and progressively loads Mini Maho", async ({
  page,
}, testInfo) => {
  test.skip(testInfo.project.name !== "chromium", "Desktop project only");
  await page.goto("/", { waitUntil: "load" });

  const hero = page.locator("[data-motion-surface='hero']");
  await expect(hero).toHaveAttribute("data-hero-policy", "motion");
  const heroVideo = hero.locator("video");
  await expect
    .poll(() => heroVideo.evaluate((video) => (video as HTMLVideoElement).currentSrc))
    .toContain("/demo/hero/dom-hyperframes/maho-spaces-dom.mp4");
  await expect
    .poll(() => heroVideo.evaluate((video) => (video as HTMLVideoElement).paused))
    .toBe(false);

  const miniVideo = page.locator(
    "[data-mini-maho-preview] video[data-progressive-marketing-video]",
  );
  await page.locator("[data-mini-maho-preview]").scrollIntoViewIfNeeded();
  await expect
    .poll(() => miniVideo.evaluate((video) => (video as HTMLVideoElement).currentSrc))
    .toContain("/demo/hero/maho-mini-maho.mp4");
});

test("reduced motion and Save-Data keep desktop video static", async ({
  browser,
}, testInfo) => {
  test.skip(testInfo.project.name !== "chromium", "Desktop project only");

  const reducedContext = await browser.newContext({
    viewport: { width: 1280, height: 900 },
    reducedMotion: "reduce",
  });
  const reducedPage = await reducedContext.newPage();
  const reducedMediaRequests: string[] = [];
  reducedPage.on("request", (request) => {
    if (request.resourceType() === "media") {
      reducedMediaRequests.push(new URL(request.url()).pathname);
    }
  });
  await reducedPage.goto("/", { waitUntil: "load" });
  expect(reducedMediaRequests).toEqual([]);
  await reducedContext.close();

  const saveDataContext = await browser.newContext({
    viewport: { width: 1280, height: 900 },
  });
  await saveDataContext.addInitScript(() => {
    Object.defineProperty(navigator, "connection", {
      configurable: true,
      value: { saveData: true },
    });
  });
  const saveDataPage = await saveDataContext.newPage();
  const saveDataMediaRequests: string[] = [];
  saveDataPage.on("request", (request) => {
    if (request.resourceType() === "media") {
      saveDataMediaRequests.push(new URL(request.url()).pathname);
    }
  });
  await saveDataPage.goto("/", { waitUntil: "load" });
  expect(saveDataMediaRequests).toEqual([]);
  await saveDataContext.close();
});
