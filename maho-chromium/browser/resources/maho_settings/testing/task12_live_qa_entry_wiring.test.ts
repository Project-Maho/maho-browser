import {describe, expect, test} from 'bun:test';
import {readFileSync} from 'node:fs';

const source = readFileSync(new URL('./task12_live_qa_entry.ts', import.meta.url), 'utf8');

function importedBindings(modulePath: string): string[] {
  const escapedPath = modulePath.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
  const match = source.match(new RegExp(`import\\s*{([^}]+)}\\s*from\\s*['"]${escapedPath}['"]`));
  expect(match, `default live entry must import ${modulePath}`).not.toBeNull();
  return match![1]
      .split(',')
      .map(binding => binding.trim().split(/\s+as\s+/).at(-1)!)
      .filter(Boolean);
}

function expectImportedAdapterIsCalled(modulePath: string): void {
  const bindings = importedBindings(modulePath);
  expect(bindings.length).toBeGreaterThan(0);
  expect(
      bindings.some(binding =>
        new RegExp(`\\b${binding.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')}\\s*\\(`).test(source)),
      `default live entry must call an adapter imported from ${modulePath}`,
  ).toBe(true);
}

describe('Task 12 default live entry adapter wiring', () => {
  test('routes live QA through the default runtime snapshot-reader composition without legacy observables', () => {
    expect(source).toMatch(/runQa:\s*async\s*\([^)]*\)\s*=>\s*runDefaultTask12Qa\s*\(/);
    expect(source).not.toContain('activeBrowserProfileId:');
    expect(source).not.toContain('activeMahoProfileId:');
    expect(source).not.toContain('lifecycle:()');
  });

  test('imports and calls the owned lifecycle adapter', () => {
    expectImportedAdapterIsCalled('./task12_live_lifecycle.js');
  });

  test('imports and calls the profile scenario adapter', () => {
    expectImportedAdapterIsCalled('./task12_profile_scenarios.js');
  });

  test('imports and calls the final cleanup adapter', () => {
    expectImportedAdapterIsCalled('./task12_final_cleanup.js');
  });
});
