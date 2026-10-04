import {test, expect, chromium} from '@playwright/test';
import * as fs from 'node:fs';
import * as path from 'node:path';
import {fileURLToPath} from 'node:url';
import pixelmatch from 'pixelmatch';
import {PNG} from 'pngjs';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const BASELINE_DIR = path.join(__dirname, '../test_data/baselines');
const VIEWPORT = {width: 1280, height: 720};
const DIFF_THRESHOLD_PCT = 5;

const MAHO_BIN = path.resolve(
  __dirname,
  '../../../../../chromium/src/out/Default/Maho.app/Contents/MacOS/Maho',
);

interface Stage {
  name: string;
  setup: string;
}

const STAGES: Stage[] = [
  {
    name: 'splash',
    setup: `void 0`,
  },
  {
    name: 'import-stage-a',
    setup: `
      window.__mahoWelcomeStore.patch(d => {
        d.currentPage = 1;
        d.direction = 'forward';
        d.importStage = 'A';
      });
    `,
  },
  {
    name: 'import-stage-b',
    setup: `
      window.__mahoWelcomeStore.patch(d => {
        d.currentPage = 1;
        d.direction = 'forward';
        d.importStage = 'B';
      });
    `,
  },
  {
    name: 'search-engine',
    setup: `
      window.__mahoWelcomeStore.patch(d => {
        d.currentPage = 3;
        d.direction = 'forward';
        d.importStage = 'A';
      });
    `,
  },
  {
    name: 'essentials',
    setup: `
      window.__mahoWelcomeStore.patch(d => {
        d.currentPage = 4;
        d.direction = 'forward';
      });
    `,
  },
  {
    name: 'completion',
    setup: `
      window.__mahoWelcomeStore.patch(d => {
        d.currentPage = 5;
        d.direction = 'forward';
      });
    `,
  },
];

function ensureBaselineDir() {
  if (!fs.existsSync(BASELINE_DIR)) {
    fs.mkdirSync(BASELINE_DIR, {recursive: true});
  }
}

async function captureAndDiff(name: string, pngBuffer: Buffer): Promise<{
  pct: number;
  baselineWritten: boolean;
}> {
  ensureBaselineDir();
  const baselinePath = path.join(BASELINE_DIR, `${name}.png`);

  if (!fs.existsSync(baselinePath) || fs.statSync(baselinePath).size < 1000) {
    fs.writeFileSync(baselinePath, pngBuffer);
    return {pct: 0, baselineWritten: true};
  }

  const current = PNG.sync.read(pngBuffer);
  const baseline = PNG.sync.read(fs.readFileSync(baselinePath));
  if (current.width !== baseline.width || current.height !== baseline.height) {
    throw new Error(
      `Dimension mismatch for ${name}: current ${current.width}x${current.height} vs baseline ${baseline.width}x${baseline.height}`,
    );
  }

  const diff = new PNG({width: current.width, height: current.height});
  const mismatch = pixelmatch(
    current.data,
    baseline.data,
    diff.data,
    current.width,
    current.height,
    {threshold: 0.1},
  );
  const totalPx = current.width * current.height;
  const pct = (mismatch / totalPx) * 100;

  if (pct > DIFF_THRESHOLD_PCT) {
    const diffPath = path.join(__dirname, '..', 'test-results', `${name}-diff.png`);
    fs.mkdirSync(path.dirname(diffPath), {recursive: true});
    fs.writeFileSync(diffPath, PNG.sync.write(diff));
  }
  return {pct, baselineWritten: false};
}

test.describe('welcome visual regression (store-driven)', () => {
  for (const stage of STAGES) {
    test(`${stage.name} renders within ${DIFF_THRESHOLD_PCT}% of baseline`, async () => {
      test.setTimeout(90000);
      const browser = await chromium.launch({
        executablePath: MAHO_BIN,
        args: ['--no-first-run', '--no-default-browser-check'],
      });
      const context = await browser.newContext({viewport: VIEWPORT});
      const page = await context.newPage();
      try {
        await page.goto('chrome://maho-welcome/');
        await page.waitForFunction(
          () => !!(window as any).__mahoWelcomeStore,
          null,
          {timeout: 15000},
        );

        await page.evaluate(stage.setup);
        await page.waitForTimeout(2500);

        const buf = await page.screenshot({timeout: 15000, caret: 'hide'});
        const {pct, baselineWritten} = await captureAndDiff(stage.name, buf);
        test.info().annotations.push({
          type: baselineWritten ? 'baseline-written' : 'pixel-diff',
          description: `${stage.name}: ${pct.toFixed(2)}%`,
        });
        if (!baselineWritten) {
          expect(
            pct,
            `${stage.name} pixel diff ${pct.toFixed(2)}% exceeds ${DIFF_THRESHOLD_PCT}%`,
          ).toBeLessThanOrEqual(DIFF_THRESHOLD_PCT);
        }
      } finally {
        await page.close();
        await context.close();
        await browser.close();
      }
    });
  }
});
