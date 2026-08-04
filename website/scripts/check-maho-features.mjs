import { existsSync, mkdtempSync, readFileSync, rmSync } from "fs";
import { tmpdir } from "os";
import { dirname, join } from "path";
import { fileURLToPath } from "url";
import { decodePngPixels } from "./png-pixels.mjs";
import { CAPTURE_REQUESTS, renderFeatures } from "./render-maho-features.mjs";

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);
const ROOT_DIR = join(__dirname, "..");
const COMMITTED_DIR = join(ROOT_DIR, "src", "assets", "images");

const EXPECTED_WIDTH = 1360;
const EXPECTED_HEIGHT = 860;

async function runCheck() {
  const tempDir = mkdtempSync(join(tmpdir(), "maho-feature-check-"));
  console.log(`Checking feature PNG freshness in temp dir: ${tempDir}`);

  try {
    await renderFeatures({ outputDir: tempDir });

    let hasDrift = false;

    for (const scene of Object.keys(CAPTURE_REQUESTS)) {
      const fileName = `maho-feature-${scene}.png`;
      const committedPath = join(COMMITTED_DIR, fileName);
      const renderedPath = join(tempDir, fileName);

      if (!existsSync(committedPath)) {
        console.error(`MISSING: Committed asset ${fileName} does not exist!`);
        hasDrift = true;
        continue;
      }

      const committedBuf = readFileSync(committedPath);
      const renderedBuf = readFileSync(renderedPath);

      const committedImg = decodePngPixels(committedBuf);
      const renderedImg = decodePngPixels(renderedBuf);

      if (
        committedImg.width !== EXPECTED_WIDTH ||
        committedImg.height !== EXPECTED_HEIGHT
      ) {
        console.error(
          `DIMENSION ERROR [${scene}]: Committed PNG is ${committedImg.width}x${committedImg.height}, expected ${EXPECTED_WIDTH}x${EXPECTED_HEIGHT}`,
        );
        hasDrift = true;
      }

      if (
        renderedImg.width !== EXPECTED_WIDTH ||
        renderedImg.height !== EXPECTED_HEIGHT
      ) {
        console.error(
          `DIMENSION ERROR [${scene}]: Rendered PNG is ${renderedImg.width}x${renderedImg.height}, expected ${EXPECTED_WIDTH}x${EXPECTED_HEIGHT}`,
        );
        hasDrift = true;
      }

      if (committedBuf.equals(renderedBuf)) {
        console.log(`PASS [${scene}]: Byte-identical (${EXPECTED_WIDTH}x${EXPECTED_HEIGHT})`);
        continue;
      }

      // Decoded Pixel-by-Pixel Diff Comparison (Normalized 4-channel RGBA)
      let pixelDiffCount = 0;
      const totalPixels = committedImg.width * committedImg.height;

      for (let i = 0; i < committedImg.pixels.length; i += 4) {
        const dr = Math.abs(committedImg.pixels[i] - renderedImg.pixels[i]);
        const dg = Math.abs(committedImg.pixels[i + 1] - renderedImg.pixels[i + 1]);
        const db = Math.abs(committedImg.pixels[i + 2] - renderedImg.pixels[i + 2]);
        const da = Math.abs(committedImg.pixels[i + 3] - renderedImg.pixels[i + 3]);
        if (dr > 4 || dg > 4 || db > 4 || da > 4) {
          pixelDiffCount++;
        }
      }

      const mismatchRatio = pixelDiffCount / totalPixels;
      if (mismatchRatio < 0.001) {
        console.log(
          `PASS [${scene}]: Decoded pixels match within tolerance (mismatch ratio ${(mismatchRatio * 100).toFixed(3)}%)`,
        );
      } else {
        console.error(
          `DRIFT DETECTED [${scene}]: Committed PNG differs from React mock render (${pixelDiffCount} mismatched pixels, ratio ${(
            mismatchRatio * 100
          ).toFixed(2)}%)`,
        );
        hasDrift = true;
      }
    }

    if (hasDrift) {
      throw new Error("Freshness check FAILED: Asset drift detected.");
    } else {
      console.log("\nFreshness check PASSED: All 4 feature PNGs are fresh and valid.");
    }
  } finally {
    rmSync(tempDir, { recursive: true, force: true });
  }
}

runCheck().catch((err) => {
  console.error("Freshness check error:", err);
  process.exit(1);
});
