import {afterAll, describe, expect, test} from 'bun:test';
import {mkdtempSync, readFileSync, rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join, resolve} from 'node:path';
import {spawnSync} from 'node:child_process';

const settingsDir = resolve(import.meta.dir, '..');
const temporaryDir = mkdtempSync(join(tmpdir(), 'maho-settings-module-url-'));
const outputFile = join(temporaryDir, 'app_bundle.js');

const canonicalSpecifier = './maho_settings.mojom-webui.js';

afterAll(() => {
  rmSync(temporaryDir, {force: true, recursive: true});
});

describe('Settings bundle Mojo module URL contract', () => {
  test('emits only the WebUI-relative Mojo module specifier', () => {
    const build = spawnSync(
      'bun',
      [join(settingsDir, 'bundle_react.mjs'), settingsDir, outputFile],
      {encoding: 'utf8'},
    );

    expect(build.status, `${build.stdout}${build.stderr}`).toBe(0);

    const bundle = readFileSync(outputFile, 'utf8');
    const specifiers = [...bundle.matchAll(
      /\bfrom(["'])([^"']*maho_settings\.mojom-webui\.js)\1/g,
    )].map(match => match[2]);
    const offendingSpecifiers = specifiers.filter(
      specifier => specifier !== canonicalSpecifier,
    );

    expect(specifiers.length).toBeGreaterThan(0);
    expect(offendingSpecifiers).toEqual([]);
    expect(specifiers.every(specifier => specifier === canonicalSpecifier)).toBe(true);
  });
});
