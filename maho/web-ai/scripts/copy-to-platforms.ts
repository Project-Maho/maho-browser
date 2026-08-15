/**
 * copy-to-platforms.ts
 *
 * Post-build script: copies dist/* to native platform asset directories.
 * Wired into `bun run build` (`... && bun run copy:platforms`); also runnable
 * standalone via `bun run copy:platforms`.
 *
 * Destinations (relative to maho/web-ai/):
 *   Android: ../android-shell/app/src/main/assets/web-ai/
 *   iOS:     ../ios-shell/web-ai/
 */

import { createHash } from 'node:crypto';
import { cpSync, existsSync, mkdirSync, readdirSync, readFileSync, rmSync, statSync, writeFileSync } from 'node:fs';
import { join, relative, resolve } from 'node:path';

const __dirname = new URL('.', import.meta.url).pathname;
const root = resolve(__dirname, '..');
const distDir = join(root, 'dist');

const destinations = {
  android: resolve(root, '../android-shell/app/src/main/assets/web-ai'),
  ios: resolve(root, '../ios-shell/web-ai'),
} as const;

interface AssetFileEntry {
  size: number;
  sha256: string;
  modifiedTimestamp: number;
}

interface AssetManifest {
  schemaVersion: number;
  bundleVersion: string;
  buildTimestamp: number;
  files: Record<string, AssetFileEntry>;
}

function getAllFiles(dir: string): string[] {
  let results: string[] = [];
  const list = readdirSync(dir, { withFileTypes: true });
  for (const item of list) {
    const fullPath = join(dir, item.name);
    if (item.isDirectory()) {
      results = results.concat(getAllFiles(fullPath));
    } else if (item.isFile()) {
      if (item.name === 'asset-manifest.json') continue;
      results.push(fullPath);
    }
  }
  return results;
}

function generateAssetManifest(distPath: string): AssetManifest {
  const packageJsonPath = resolve(distPath, '../package.json');
  let bundleVersion = '0.1.0';
  if (existsSync(packageJsonPath)) {
    try {
      const pkg = JSON.parse(readFileSync(packageJsonPath, 'utf8'));
      if (pkg.version) {
        bundleVersion = pkg.version;
      }
    } catch {}
  }

  const allFiles = getAllFiles(distPath);
  const files: Record<string, AssetFileEntry> = {};

  for (const filePath of allFiles) {
    const relPath = relative(distPath, filePath).replace(/\\/g, '/');
    const content = readFileSync(filePath);
    const sha256 = createHash('sha256').update(content).digest('hex');
    const stats = statSync(filePath);
    files[`web-ai/${relPath}`] = {
      size: stats.size,
      sha256,
      modifiedTimestamp: Math.floor(stats.mtimeMs / 1000),
    };
  }

  return {
    schemaVersion: 1,
    bundleVersion,
    buildTimestamp: Math.floor(Date.now() / 1000),
    files,
  };
}

function cleanAndEnsureDir(dir: string): void {
  if (existsSync(dir)) {
    rmSync(dir, { recursive: true, force: true });
  }
  mkdirSync(dir, { recursive: true });
}

function copyDist(dest: string): void {
  cleanAndEnsureDir(dest);
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

// Generate asset-manifest.json in distDir before copying
const manifest = generateAssetManifest(distDir);
const manifestPath = join(distDir, 'asset-manifest.json');
writeFileSync(manifestPath, JSON.stringify(manifest, null, 2), 'utf8');
console.log(`Generated asset-manifest.json with ${Object.keys(manifest.files).length} files (v${manifest.bundleVersion})`);

copyDist(destinations.android);
copyDist(destinations.ios);

console.log(`Copied web-ai bundle to Android: ${destinations.android}`);
console.log(`Copied web-ai bundle to iOS:     ${destinations.ios}`);
console.log(`  Android contents: ${listContents(destinations.android)}`);
console.log(`  iOS contents:     ${listContents(destinations.ios)}`);
