/**
 * check-bundle-size.ts
 *
 * Bundle size budget gate for maho/web-ai.
 * Verifies that dist/ai-bundle.js does not exceed target budget limits.
 * Exits with non-zero exit code if budget is exceeded.
 */

import { existsSync, readFileSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { gzipSync } from 'node:zlib';

const __dirname = new URL('.', import.meta.url).pathname;
const root = resolve(__dirname, '..');
const distDir = join(root, 'dist');
const bundlePath = join(distDir, 'ai-bundle.js');

// Budget limits in bytes
// Uncompressed budget: 75 KB (75 * 1024 bytes = 76,800 bytes)
// Gzipped budget: 30 KB (30 * 1024 bytes = 30,720 bytes)
const BUDGETS = {
  uncompressed: {
    maxBytes: 75 * 1024,
    label: '75 KB',
  },
  gzip: {
    maxBytes: 30 * 1024,
    label: '30 KB',
  },
};

function formatBytes(bytes: number): string {
  return `${(bytes / 1024).toFixed(2)} KB (${bytes.toLocaleString()} bytes)`;
}

function checkBundleSize(): void {
  if (!existsSync(bundlePath)) {
    console.error(`[check-bundle-size] ERROR: Bundle file not found at ${bundlePath}`);
    console.error('[check-bundle-size] Please run `bun run build` before checking bundle size.');
    process.exit(1);
  }

  const content = readFileSync(bundlePath);
  const rawSize = content.length;
  const gzippedSize = gzipSync(content).length;

  console.log('=== maho-web-ai Bundle Size Budget Check ===');
  console.log(`Target: ${bundlePath}`);
  console.log(`Raw Size:      ${formatBytes(rawSize)} (Budget: ${BUDGETS.uncompressed.label} / ${BUDGETS.uncompressed.maxBytes.toLocaleString()} bytes)`);
  console.log(`Gzip Size:     ${formatBytes(gzippedSize)} (Budget: ${BUDGETS.gzip.label} / ${BUDGETS.gzip.maxBytes.toLocaleString()} bytes)`);

  let exceeded = false;

  if (rawSize > BUDGETS.uncompressed.maxBytes) {
    console.error(`\n[FAIL] Raw size exceeded budget! ${formatBytes(rawSize)} > ${BUDGETS.uncompressed.label}`);
    exceeded = true;
  }

  if (gzippedSize > BUDGETS.gzip.maxBytes) {
    console.error(`\n[FAIL] Gzip size exceeded budget! ${formatBytes(gzippedSize)} > ${BUDGETS.gzip.label}`);
    exceeded = true;
  }

  if (exceeded) {
    console.error('\n❌ Bundle size budget gate FAILED.');
    process.exit(1);
  }

  console.log('\n✅ All bundle size budget checks PASSED.');
}

checkBundleSize();
