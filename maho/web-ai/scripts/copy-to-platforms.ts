/**
 * copy-to-platforms.ts
 *
 * Post-build script: copies dist/* to native platform asset directories.
 * Run via `bun run postbuild` or called by vite closeBundle hook.
 *
 * Destinations (relative to maho/web-ai/):
 *   Android: ../android-shell/app/src/main/assets/web-ai/
 *   iOS:     ../ios-shell/web-ai/
 */

import { cpSync, existsSync, mkdirSync, readdirSync } from 'node:fs';
import { join, resolve } from 'node:path';

const __dirname = new URL('.', import.meta.url).pathname;
const root = resolve(__dirname, '..');
const distDir = join(root, 'dist');

const destinations = {
  android: resolve(root, '../android-shell/app/src/main/assets/web-ai'),
  ios: resolve(root, '../ios-shell/web-ai'),
} as const;

function ensureDir(dir: string): void {
  if (!existsSync(dir)) {
    mkdirSync(dir, { recursive: true });
  }
}

function copyDist(dest: string): void {
  ensureDir(dest);
  cpSync(distDir, dest, { recursive: true, force: true });
}

function listContents(dir: string): string {
  if (!existsSync(dir)) return '(directory not found)';
  return readdirSync(dir).join(', ');
}

if (!existsSync(distDir)) {
  console.error('[copy-to-platforms] ERROR: dist/ directory not found. Run `bun run build` first.');
  process.exit(1);
}

copyDist(destinations.android);
copyDist(destinations.ios);

console.log(`Copied web-ai bundle to Android: ${destinations.android}`);
console.log(`Copied web-ai bundle to iOS:     ${destinations.ios}`);
console.log(`  Android contents: ${listContents(destinations.android)}`);
console.log(`  iOS contents:     ${listContents(destinations.ios)}`);
