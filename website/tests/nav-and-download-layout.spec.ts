import { expect, test } from "@playwright/test";

const releaseAssets = [
  {
    name: "Download Maho for macOS",
    href: "https://github.com/Project-Maho/release/releases/latest/download/Maho-1.0.0.dmg",
  },
  {
    name: "Download Maho for Windows",
    href: "https://github.com/Project-Maho/release/releases/latest/download/MahoSetup-1.0.0.exe",
  },
  {
    name: "Download Maho for Linux",
    href: "https://github.com/Project-Maho/release/releases/latest/download/maho_1.0.0_amd64.deb",
  },
  {
    name: "Download Maho for Linux as RPM",
    href: "https://github.com/Project-Maho/release/releases/latest/download/maho-1.0.0-1.x86_64.rpm",
  },
  {
    name: "Download Maho for Linux as tar.gz",
    href: "https://github.com/Project-Maho/release/releases/latest/download/maho-1.0.0-x86_64.tar.gz",
  },
] as const;

test("desktop navigation links stay at the viewport midpoint", async ({ page }) => {
  await page.setViewportSize({ width: 1440, height: 900 });
  await page.goto("/");

  const navigation = page.getByRole("navigation", { name: "Main navigation" });
  const docs = navigation.getByRole("link", { name: "Docs", exact: true });
  const blog = navigation.getByRole("link", { name: "Blog", exact: true });
  const pricing = navigation.getByRole("link", { name: "Pricing", exact: true });

  await expect(docs).toBeVisible();
  await expect(blog).toBeVisible();
  await expect(pricing).toBeVisible();

  const [docsBox, pricingBox] = await Promise.all([
    docs.boundingBox(),
    pricing.boundingBox(),
  ]);
  expect(docsBox).not.toBeNull();
  expect(pricingBox).not.toBeNull();

  const linksCenter = (docsBox!.x + pricingBox!.x + pricingBox!.width) / 2;
  expect(Math.abs(linksCenter - 1440 / 2)).toBeLessThanOrEqual(1);
});

test("download offers every published desktop package", async ({ page }) => {
  await page.goto("/download/");

  const directAssetLinks = page.locator(
    'main a[href*="github.com/Project-Maho/release/releases/latest/download/"]',
  );
  await expect(directAssetLinks).toHaveCount(releaseAssets.length);

  for (const asset of releaseAssets) {
    const link = page.getByRole("link", { name: asset.name, exact: true });
    await expect(link).toBeVisible();
    await expect(link).toHaveAttribute("href", asset.href);
  }
});

test("download page carries no installation walkthrough", async ({ page }) => {
  await page.goto("/download/");

  await expect(page.locator("[data-install-section]")).toHaveCount(0);
  await expect(page.locator("main")).not.toContainText(
    /set up maho|drag maho to applications|follow the installer|run \.\/maho|open the extracted folder/i,
  );
});

test("download page states mobile availability without a fake download", async ({ page }) => {
  await page.goto("/download/");

  const mobileCards = page.locator('[data-platform="ios"], [data-platform="android"]');
  await expect(mobileCards).toHaveCount(2);
  await expect(mobileCards.locator("[data-platform-status]")).toHaveText([
    "Coming soon",
    "Coming soon",
  ]);
  await expect(mobileCards.locator("a")).toHaveCount(0);
});

test("download gives all five platforms a distinct visual card", async ({ page }) => {
  await page.goto("/download/");

  const platformCards = page.locator("[data-platform-card]");
  await expect(platformCards).toHaveCount(5);

  const brandAssets = {
    macos: "/platform-icons/apple.svg",
    windows: "/platform-icons/windows.svg",
    linux: "/platform-icons/linux.svg",
    ios: "/platform-icons/apple.svg",
    android: "/platform-icons/android.svg",
  } as const;

  for (const [platform, src] of Object.entries(brandAssets)) {
    const card = page.locator(`[data-platform-card][data-platform="${platform}"]`);
    const mark = card.locator("[data-platform-brand-mark]");
    await expect(card).toBeVisible();
    await expect(mark).toHaveCount(1);
    await expect(mark).toHaveAttribute("src", src);
    await expect(mark).toHaveJSProperty("complete", true);
    expect(await mark.evaluate((image) => (
      image instanceof HTMLImageElement ? image.naturalWidth : 0
    ))).toBeGreaterThan(0);
  }

  await expect(page.locator('[data-platform="macos"] [data-platform-status]')).toHaveCount(0);
  await expect(page.locator('[data-platform="windows"] [data-platform-status]')).toHaveCount(0);
  await expect(page.locator('[data-platform="linux"] [data-platform-status]')).toHaveCount(0);
  await expect(page.locator('[data-platform="macos"] [data-platform-status]')).toHaveCount(0);
  await expect(page.locator('[data-platform="windows"] [data-platform-status]')).toHaveCount(0);
  await expect(page.locator('[data-platform="linux"] [data-platform-status]')).toHaveCount(0);
  await expect(page.locator('[data-platform="ios"] [data-platform-status]')).toHaveText("Coming soon");
  await expect(page.locator('[data-platform="android"] [data-platform-status]')).toHaveText("Coming soon");
});

test("tablet layout gives the final platform a complete row", async ({ page }) => {
  await page.setViewportSize({ width: 768, height: 1000 });
  await page.goto("/download/");

  const linux = page.locator('[data-platform="linux"]');
  const android = page.locator('[data-platform="android"]');
  const [linuxBox, androidBox] = await Promise.all([linux.boundingBox(), android.boundingBox()]);

  expect(linuxBox).not.toBeNull();
  expect(androidBox).not.toBeNull();
  expect(androidBox!.x).toBe(linuxBox!.x);
  expect(androidBox!.width).toBeGreaterThan(linuxBox!.width * 1.8);
});

test("Linux alternate downloads are thumb-sized on touch viewports", async ({ page }) => {
  await page.setViewportSize({ width: 375, height: 1000 });
  await page.goto("/download/");

  for (const label of ["Download Maho for Linux as RPM", "Download Maho for Linux as tar.gz"]) {
    const box = await page.getByRole("link", { name: label, exact: true }).boundingBox();
    expect(box).not.toBeNull();
    expect(box!.width).toBeGreaterThanOrEqual(44);
    expect(box!.height).toBeGreaterThanOrEqual(44);
  }
});

test("all-releases link is thumb-sized on touch viewports", async ({ page }) => {
  await page.setViewportSize({ width: 375, height: 1000 });
  await page.goto("/download/");

  const box = await page.getByRole("link", { name: "View all releases", exact: true }).boundingBox();
  expect(box).not.toBeNull();
  expect(box!.width).toBeGreaterThanOrEqual(44);
  expect(box!.height).toBeGreaterThanOrEqual(44);
});
