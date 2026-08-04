import { chromium } from "@playwright/test";
import { existsSync, mkdirSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import {
  CAPTURE_HOST,
  getFreePort,
  runCommand,
  startPreview,
  stopProcessGroup,
  waitForCaptureRoute,
} from "./capture-lifecycle.mjs";

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);
const ROOT_DIR = join(__dirname, "..");
const ASSETS_DIR = join(ROOT_DIR, "src", "assets", "images");
const BASELINE_SEED = "#b58ad8";

export { CAPTURE_HOST, getFreePort, runCommand, stopProcessGroup };

export const CAPTURE_REQUESTS = Object.freeze({
  spaces: Object.freeze({ progress: 0.32 }),
  "split-view": Object.freeze({ progress: 0.62 }),
  "command-bar": Object.freeze({ progress: 0.5 }),
  agent: Object.freeze({ progress: 0.75 }),
});

export function createCaptureUrl({ baseUrl, scene }) {
  const request = CAPTURE_REQUESTS[scene];
  if (request === undefined) {
    throw new Error(`Unknown capture scene: ${scene}`);
  }
  const url = new URL("/capture/maho-feature/", baseUrl);
  url.search = new URLSearchParams({
    profile: "feature",
    scene,
    theme: BASELINE_SEED,
    p: String(request.progress),
  }).toString();
  return url.toString();
}

async function closeResources({ browser, context, preview, failure }) {
  const cleanupFailures = [];
  if (context !== undefined) {
    try {
      await context.close();
    } catch (error) {
      cleanupFailures.push(error);
    }
  }
  if (browser !== undefined) {
    try {
      await browser.close();
    } catch (error) {
      cleanupFailures.push(error);
    }
  }
  if (preview !== undefined) {
    try {
      await stopProcessGroup({ child: preview.child });
    } catch (error) {
      cleanupFailures.push(error);
    }
  }
  if (cleanupFailures.length > 0) {
    const cleanupError = new AggregateError(cleanupFailures, "Feature capture cleanup failed");
    if (failure !== undefined) {
      console.error(cleanupError);
      return;
    }
    throw cleanupError;
  }
}

export async function renderFeatures({ baseUrl, outputDir = ASSETS_DIR } = {}) {
  let browser;
  let context;
  let preview;
  let failure;
  try {
    let targetUrl = baseUrl;
    if (targetUrl === undefined) {
      console.log("No --base-url provided. Building and starting preview server...");
      await runCommand({ command: "bun", args: ["run", "build"], cwd: ROOT_DIR });
      const port = await getFreePort();
      console.log(`Starting preview server on ${CAPTURE_HOST}:${port}...`);
      preview = startPreview({ cwd: ROOT_DIR, port });
      await waitForCaptureRoute({ preview, port });
      targetUrl = `http://${CAPTURE_HOST}:${port}`;
    }

    console.log(`Rendering feature captures against ${targetUrl}...`);
    if (!existsSync(outputDir)) {
      mkdirSync(outputDir, { recursive: true });
    }

    browser = await chromium.launch();
    context = await browser.newContext({
      deviceScaleFactor: 1,
      viewport: { width: 1600, height: 1200 },
    });
    for (const scene of Object.keys(CAPTURE_REQUESTS)) {
      const page = await context.newPage();
      try {
        const url = createCaptureUrl({ baseUrl: targetUrl, scene });
        console.log(`Capturing [${scene}] at ${url}...`);
        await page.goto(url, { waitUntil: "domcontentloaded" });
        const host = page.locator("[data-capture-ready='true']");
        await host.waitFor({ state: "visible", timeout: 15_000 });
        await host.screenshot({
          path: join(outputDir, `maho-feature-${scene}.png`),
          animations: "disabled",
        });
      } finally {
        await page.close();
      }
    }
  } catch (error) {
    failure = error;
    throw error;
  } finally {
    await closeResources({ browser, context, preview, failure });
  }
}

if (process.argv[1] === fileURLToPath(import.meta.url)) {
  const baseUrl = process.argv.slice(2).find((argument) => argument.startsWith("--base-url="))?.slice(11);
  renderFeatures({ baseUrl }).catch((error) => {
    console.error("Failed to render features:", error);
    process.exitCode = 1;
  });
}
