import { describe, expect, test } from 'bun:test';
import { existsSync, readFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = dirname(fileURLToPath(import.meta.url));
const websiteRoot = resolve(__dirname, '..', '..', '..');
const changelogPagePath = join(websiteRoot, 'src', 'pages', 'changelog.astro');
const footerPath = join(websiteRoot, 'src', 'components', 'Footer.astro');
const changelogJsonPath = join(websiteRoot, 'src', 'data', 'changelog.json');
const syncScriptPath = join(websiteRoot, 'scripts', 'sync-changelog.mjs');
const packageJsonPath = join(websiteRoot, 'package.json');

describe('Changelog pipeline & page integration', () => {
  test('Footer links to /changelog', () => {
    const footerSource = readFileSync(footerPath, 'utf8');
    expect(footerSource).toContain("{ href: '/changelog', label: 'Changelog' }");
  });

  test('package.json build script runs sync-changelog.mjs', () => {
    const pkg = JSON.parse(readFileSync(packageJsonPath, 'utf8'));
    expect(pkg.scripts.build).toContain('node scripts/sync-changelog.mjs');
  });

  test('bundled changelog.json exists and is valid', () => {
    expect(existsSync(changelogJsonPath)).toBe(true);
    const data = JSON.parse(readFileSync(changelogJsonPath, 'utf8'));
    expect(Array.isArray(data.releases)).toBe(true);
    expect(data.releases.length).toBeGreaterThanOrEqual(1);

    const v1 = data.releases.find((r: { version: string }) => r.version === '1.0.0');
    expect(v1).toBeDefined();
    expect(v1.channel).toBe('stable');
    expect(v1.date).toBe('2026-08-12');
    expect(v1.highlights.length).toBeGreaterThan(0);
    expect(v1.sections.length).toBeGreaterThan(0);
  });

  test('changelog.astro imports bundled JSON and renders release layout', () => {
    const pageSource = readFileSync(changelogPagePath, 'utf8');
    expect(pageSource).toMatch(/import changelogData from ['"]\.\.\/data\/changelog\.json['"];/);
    expect(pageSource).toContain('BaseLayout');
    expect(pageSource).toContain('Nav');
    expect(pageSource).toContain('Footer');
    expect(pageSource).toContain('JSONLD');
    expect(pageSource).toContain('ScrollReveal');
    expect(pageSource).toContain('formatDate');
  });
});
