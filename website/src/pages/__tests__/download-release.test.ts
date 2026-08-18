import { expect, test } from "bun:test";
import { readdir, readFile } from "node:fs/promises";
import { join } from "node:path";

import { latestReleaseAssetUrl, releasePageUrl } from "../../lib/release-url";

const downloadPagePath = new URL("../download.astro", import.meta.url);
const platformIconPath = new URL("../../components/download/PlatformIcon.astro", import.meta.url);
const desktopAvailabilityPagePaths = [
  downloadPagePath,
  new URL("../about.astro", import.meta.url),
  new URL("../roadmap.astro", import.meta.url),
  new URL("../why.astro", import.meta.url),
  new URL("../../components/LandingPage.tsx", import.meta.url),
  new URL("../../components/Footer.astro", import.meta.url),
];

test("release page URL stays on the release listing", () => {
  expect(releasePageUrl("https://github.com/Project-Maho/release/")).toBe(
    "https://github.com/Project-Maho/release/releases",
  );

  expect(
    latestReleaseAssetUrl(
      "https://github.com/Project-Maho/release/",
      "Maho-1.0.0.dmg",
    ),
  ).toBe(
    "https://github.com/Project-Maho/release/releases/latest/download/Maho-1.0.0.dmg",
  );
});

test("download page publishes every desktop package format", async () => {
  const page = await readFile(downloadPagePath, "utf8");

  expect(page).not.toContain("PUBLIC_RELEASE_LIVE");
  expect(page).toContain("latestReleaseAssetUrl(RELEASE_REPO, 'Maho-1.0.0.dmg')");
  expect(page).toContain("latestReleaseAssetUrl(RELEASE_REPO, 'MahoSetup-1.0.0.exe')");
  expect(page).toContain("latestReleaseAssetUrl(RELEASE_REPO, 'maho_1.0.0_amd64.deb')");
  expect(page).toContain("latestReleaseAssetUrl(RELEASE_REPO, 'maho-1.0.0-1.x86_64.rpm')");
  expect(page).toContain("latestReleaseAssetUrl(RELEASE_REPO, 'maho-1.0.0-x86_64.tar.gz')");
  expect(page).toContain('href={platform.downloadUrl}');
  expect(page).toContain('href={RELEASE_PAGE_URL}');
  expect(page).toContain("View all releases");
});

test("download page states mobile availability honestly", async () => {
globalThis.page = await readFile(downloadPagePath, "utf8");

  expect(page).toContain("id: 'ios'");
  expect(page).toContain("id: 'android'");
  expect(page).toContain("status: 'Beta'");
  expect(page).toContain("https://testflight.apple.com/join/JQf4rVm6");
  expect(page).toContain("https://play.google.com/apps/internaltest/4699692750515426389");
  expect(page.toLowerCase()).not.toContain("download for iphone");
  expect(page.toLowerCase()).not.toContain("download for android");
});


test("download page gives each platform a dedicated icon card", async () => {
  const [page, icon] = await Promise.all([
    readFile(downloadPagePath, "utf8"),
    readFile(platformIconPath, "utf8"),
  ]);

  expect(page).toContain("data-platform-card");
  expect(page).toContain("<PlatformIcon platform={platform.id} />");
  expect(icon).toContain("data-platform-icon");
  expect(icon).toContain("data-platform-brand-mark");
  expect(icon).toContain("const brandAssets");
  expect(icon).toContain("/platform-icons/apple.svg");
  expect(icon).toContain("/platform-icons/windows.svg");
  expect(icon).toContain("/platform-icons/linux.svg");
  expect(icon).toContain("/platform-icons/android.svg");
  expect(icon).not.toContain("platform === '");
  for (const platform of ["macos", "windows", "linux", "ios", "android"]) {
    expect(page).toContain("id: '" + platform + "'");
  }
});

test("download page does not label available desktop platforms", async () => {
  const page = await readFile(downloadPagePath, "utf8");

  expect(page).not.toContain("Available now");
  expect(page).toContain("{platform.status && (");
});

test("download page avoids a blank tablet grid cell", async () => {
  const page = await readFile(downloadPagePath, "utf8");

  expect(page).toContain("platform.id === 'android' ? 'sm:col-span-2 lg:col-span-3'");
});

test("Linux alternate downloads preserve 44px touch targets", async () => {
  const page = await readFile(downloadPagePath, "utf8");

  expect(page).toContain("min-h-11 min-w-11");
});

test("all-releases link preserves a 44px touch target", async () => {
  const page = await readFile(downloadPagePath, "utf8");

  expect(page).toContain("min-h-11 items-center");
});

test("download page no longer ships an installation walkthrough", async () => {
  const page = await readFile(downloadPagePath, "utf8");

  expect(page).not.toContain("Set up Maho.");
  expect(page).not.toContain("data-install-section");
  expect(page).not.toContain("data-detected-os");
  expect(page).not.toContain("InstallStepVisual");
});

test("desktop availability copy does not present a future launch", async () => {
  const pageSources = await Promise.all(
    desktopAvailabilityPagePaths.map((pagePath) => readFile(pagePath, "utf8")),
  );

  expect(pageSources.join("\n").toLowerCase()).not.toContain("in development");
});

async function readMdxFiles(directory: URL): Promise<string[]> {
  const entries = await readdir(directory, { withFileTypes: true });
  const sources = await Promise.all(
    entries.map(async (entry) => {
      const path = new URL(join(entry.name, entry.isDirectory() ? "/" : ""), directory);
      return entry.isDirectory()
        ? readMdxFiles(path)
        : entry.name.endsWith(".mdx")
          ? [await readFile(path, "utf8")]
          : [];
    }),
  );

  return sources.flat();
}

test("published documentation links to downloads instead of pre-release signup", async () => {
  const docs = await readMdxFiles(new URL("../../content/docs/", import.meta.url));
  const content = docs.join("\n").toLowerCase();

  expect(content).not.toContain("](/waitlist)");
  expect(content).not.toContain("pre-release build of maho");
  expect(content).not.toContain("first pre-release build");
  expect(content).not.toMatch(/^#{1,3} +(get notified|join the waitlist|get early access)$/m);
});
