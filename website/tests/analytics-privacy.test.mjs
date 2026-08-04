import { describe, expect, test } from 'bun:test';
import { readFile } from 'node:fs/promises';

const projectRoot = new URL('../', import.meta.url);

describe('website analytics privacy', () => {
  test('does not load Google Analytics from marketing or documentation pages', async () => {
    // Given: the two shared head definitions used by public website routes.
    const [baseLayout, astroConfig] = await Promise.all([
      readFile(new URL('src/layouts/BaseLayout.astro', projectRoot), 'utf8'),
      readFile(new URL('astro.config.mjs', projectRoot), 'utf8'),
    ]);

    // When: their external script sources and configuration are inspected.
    const publicHeadSource = `${baseLayout}\n${astroConfig}`;

    // Then: no Google Analytics loader or measurement configuration is present.
    expect(publicHeadSource).not.toContain('googletagmanager.com/gtag/js');
    expect(publicHeadSource).not.toContain('G-VMGL28W81B');
    expect(publicHeadSource).not.toContain("gtag('config'");
  });
});
