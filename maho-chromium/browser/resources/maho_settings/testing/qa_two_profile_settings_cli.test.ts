import {describe, expect, test} from 'bun:test';

import {runTask12QaCli} from './qa_two_profile_settings_cli.js';
import {
  EXPECTED_BLOCKED_JSON_LINE,
  TASK12_ENTRY_RELATIVE_PATH,
} from './verify_two_profile_settings_cli.js';

describe('Task12 QA CLI in-process contract', () => {
  test('maps the blocked result to exit 1 and exactly one JSON line', async () => {
    const stdout: string[] = [];

    const exitCode = await runTask12QaCli(
      async () => ({
        status: 'blocked-current-app',
        reasons: ['STALE_APP', 'CDP_9222_UNAVAILABLE', 'RELAY_18765_UNAVAILABLE'],
      }),
      line => stdout.push(line),
    );

    expect(exitCode).toBe(1);
    expect(stdout).toEqual([`${EXPECTED_BLOCKED_JSON_LINE}\n`]);
  });

  test.each([
    {status: 'missing-runtime-binding', reason: 'MISSING_RUNTIME_BINDING'},
    {status: 'blocked-missing-observable', missing: ['focused-window']},
  ] as const)('maps $status to exit 1', async result => {
    expect(await runTask12QaCli(async () => result, () => undefined)).toBe(1);
  });

  test('maps a failed result to exit 1 and exactly one JSON line', async () => {
    const stdout: string[] = [];

    const exitCode = await runTask12QaCli(
      async () => ({status: 'failed', message: 'driver failed'}),
      line => stdout.push(line),
    );

    expect(exitCode).toBe(1);
    expect(stdout).toEqual(['{"status":"failed","message":"driver failed"}\n']);
  });

  test('standalone verification is pinned to the guard-free entry and exact output', () => {
    expect(TASK12_ENTRY_RELATIVE_PATH).toBe(
      'maho-chromium/browser/resources/maho_settings/testing/qa_two_profile_settings_entry.ts',
    );
    expect(EXPECTED_BLOCKED_JSON_LINE.endsWith('\n')).toBe(false);
    expect(JSON.stringify(JSON.parse(EXPECTED_BLOCKED_JSON_LINE))).toBe(
      EXPECTED_BLOCKED_JSON_LINE,
    );
  });
});
