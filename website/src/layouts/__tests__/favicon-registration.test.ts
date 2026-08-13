import { expect, test } from "bun:test";
import { readFile } from "node:fs/promises";

const layoutPath = new URL("../BaseLayout.astro", import.meta.url);
const manifestPath = new URL("../../../public/site.webmanifest", import.meta.url);

test("site registers the centered Maho app icon for browser and installed surfaces", async () => {
  const [layout, manifest] = await Promise.all([
    readFile(layoutPath, "utf8"),
    readFile(manifestPath, "utf8"),
  ]);

  expect(layout).not.toContain('rel="icon" type="image/svg+xml" href="/favicon.svg"');
  expect(layout).toContain('rel="icon" type="image/png" sizes="512x512" href="/icon-512.png"');
  expect(layout).toContain('rel="apple-touch-icon" sizes="512x512" href="/icon-512.png"');
  expect(layout).toContain('rel="manifest" href="/site.webmanifest"');
  expect(manifest).toContain('"src": "/icon-512.png"');
  expect(manifest).toContain('"sizes": "512x512"');
});
