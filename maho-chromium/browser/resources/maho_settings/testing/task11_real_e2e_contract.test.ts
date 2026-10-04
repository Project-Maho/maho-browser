import {describe, expect, test} from 'bun:test';

import {
  parseCleanupReceiptText,
  parseCleanupReceipt,
  parseScenarioEntryArguments,
  parseScenarioReceiptText,
  parseScenarioReceipt,
  writeAtomicJsonReceipt,
} from './task11_real_e2e_contract.js';

const absolute = (name: string): string => `/tmp/task11/${name}`;
const artifact = (path: string) => ({
  path,
  sha256: 'a'.repeat(64),
  size: 42,
  mtime_utc: '2026-08-01T10:00:00.000Z',
});
const ownership = {
  markers: ['--maho-task11-owner=run-1'],
  owned_processes: [{role: 'app', pid: 123, start_token: 'start-123', marker: '--maho-task11-owner=run-1'}],
  temp_root: absolute('temp'),
  chromium_profiles: [{id: 'chromium-a', path: absolute('temp/user-data/Profile 1')}],
  maho_profiles: [{id: 'maho-a', name: 'Task 11 A', path: absolute('temp/maho/maho-a')}],
};
const common = {
  schema_version: 1,
  run_id: 'run-1',
  scenario: 'profiles',
  started_at: '2026-08-01T10:00:01.000Z',
  completed_at: '2026-08-01T10:00:02.000Z',
  artifacts: {app: artifact(absolute('Maho')), browser_tests: artifact(absolute('browser_tests'))},
  command: ['bun', 'task11_real_e2e_entry.ts', '--scenario', 'profiles'],
  source_versions: {executor: 'sha256:abc'},
  tool_versions: {bun: '1.3.13'},
  before_observables: {profiles: ['maho-a']},
  after_observables: {profiles: []},
  assertions: [{id: 'profile-a-preserved', passed: true, evidence_paths: [absolute('before.json')], detail: 'unchanged'}],
  ownership,
  raw_evidence_paths: [absolute('before.json'), absolute('after.json')],
  cleanup_receipt_path: absolute('cleanup-receipt.json'),
  errors: [],
};

describe('Task 11 real E2E contract', () => {
  test('strictly parses the complete scenario CLI before launch', () => {
    expect(parseScenarioEntryArguments([
      '--scenario', 'profiles', '--app', absolute('Maho'), '--browser-tests', absolute('browser_tests'),
      '--evidence-root', absolute('evidence'), '--run-id', 'run-1',
    ])).toEqual({scenario: 'profiles', app: absolute('Maho'), browserTests: absolute('browser_tests'), evidenceRoot: absolute('evidence'), runId: 'run-1'});

    for (const argv of [
      ['--scenario', 'profiles', '--scenario', 'profiles', '--app', absolute('Maho'), '--browser-tests', absolute('browser_tests'), '--evidence-root', absolute('evidence'), '--run-id', 'run-1'],
      ['--scenario', 'unknown', '--app', absolute('Maho'), '--browser-tests', absolute('browser_tests'), '--evidence-root', absolute('evidence'), '--run-id', 'run-1'],
      ['--scenario', 'profiles', '--app', 'relative', '--browser-tests', absolute('browser_tests'), '--evidence-root', absolute('evidence'), '--run-id', 'run-1'],
      ['--scenario', 'profiles', '--app', absolute('Maho'), '--browser-tests', absolute('browser_tests'), '--evidence-root', absolute('evidence'), '--run-id', ''],
      ['--scenario', 'profiles', '--app', absolute('Maho'), '--browser-tests', absolute('browser_tests'), '--evidence-root', absolute('evidence'), '--run-id', 'run-1', '--extra', 'x'],
    ]) expect(() => parseScenarioEntryArguments(argv)).toThrow();
  });

  test('strictly parses complete scenario and cleanup receipts', () => {
    const scenario = parseScenarioReceipt({...common, receipt_type: 'scenario', status: 'PASS'});
    expect(scenario.ownership.owned_processes[0]?.pid).toBe(123);

    const cleanup = parseCleanupReceipt({
      ...common,
      receipt_type: 'cleanup',
      status: 'PASS',
      cleanup: {
        passed: true,
        remaining_owned_processes: [],
        remaining_temp_roots: [],
        remaining_chromium_profiles: [],
        remaining_maho_profiles: [],
      },
    });
    expect(cleanup.cleanup.passed).toBe(true);

    expect(() => parseScenarioReceipt({...common, receipt_type: 'scenario', status: 'PASS', extra: true})).toThrow(/unknown/i);
    expect(() => parseCleanupReceipt({...common, receipt_type: 'cleanup', status: 'PASS', cleanup: {passed: true}})).toThrow();
    expect(() => parseScenarioReceipt({...common, receipt_type: 'scenario', status: 'PASS', artifacts: {...common.artifacts, app: {...common.artifacts.app, sha256: 'bad'}}})).toThrow(/sha256/i);
  });

  test('rejects duplicate receipt keys at the top level and every nested object level', () => {
    const scenarioText = JSON.stringify({...common, receipt_type: 'scenario', status: 'PASS'});
    const cleanupText = JSON.stringify({
      ...common,
      receipt_type: 'cleanup',
      status: 'PASS',
      cleanup: {
        passed: true,
        remaining_owned_processes: [],
        remaining_temp_roots: [],
        remaining_chromium_profiles: [],
        remaining_maho_profiles: [],
      },
    });
    expect(() => parseScenarioReceiptText(scenarioText.replace('{', '{"status":"FAIL",'))).toThrow(/duplicate.*status/i);
    expect(() => parseScenarioReceiptText(scenarioText.replace('"artifacts":{', '"artifacts":{"app":null,"escaped":{"key":1,"\\u006bey":2},'))).toThrow(/duplicate.*key/i);
    expect(() => parseCleanupReceiptText(cleanupText.replace('{', '{"run_id":"other",'))).toThrow(/duplicate.*run_id/i);
    expect(() => parseCleanupReceiptText(cleanupText.replace('"cleanup":{', '"cleanup":{"remaining_temp_roots":[{"path":1,"path":2}],'))).toThrow(/duplicate.*path/i);
  });

  test('writes strict JSON atomically and removes a failed temporary file', async () => {
    const operations: string[] = [];
    const files = new Map<string, string>();
    await writeAtomicJsonReceipt(absolute('receipt.json'), {ok: true}, {
      writeFile: async (path, data) => { operations.push(`write:${path}`); files.set(path, data); },
      rename: async (from, to) => { operations.push(`rename:${from}:${to}`); files.set(to, files.get(from) ?? ''); files.delete(from); },
      rm: async path => { operations.push(`rm:${path}`); files.delete(path); },
      uuid: () => 'uuid-1',
    });
    expect(operations[0]).toContain('.receipt.json.uuid-1.tmp');
    expect(operations[1]).toContain(`:${absolute('receipt.json')}`);
    expect(files.get(absolute('receipt.json'))).toBe('{\n  "ok": true\n}\n');

    operations.length = 0;
    files.clear();
    files.set(absolute('receipt.json'), 'existing\n');
    await expect(writeAtomicJsonReceipt(absolute('receipt.json'), {ok: false}, {
      writeFile: async (path, data) => { operations.push(`write:${path}`); files.set(path, data); },
      rename: async (from, to) => { operations.push(`rename:${from}:${to}`); throw new Error('injected rename failure'); },
      rm: async path => { operations.push(`rm:${path}`); files.delete(path); },
      uuid: () => 'uuid-failure',
    })).rejects.toThrow('injected rename failure');
    expect(operations).toEqual([
      `write:${absolute('.receipt.json.uuid-failure.tmp')}`,
      `rename:${absolute('.receipt.json.uuid-failure.tmp')}:${absolute('receipt.json')}`,
      `rm:${absolute('.receipt.json.uuid-failure.tmp')}`,
    ]);
    expect(files.get(absolute('receipt.json'))).toBe('existing\n');
    expect(files.has(absolute('.receipt.json.uuid-failure.tmp'))).toBe(false);

    operations.length = 0;
    files.clear();
    await expect(writeAtomicJsonReceipt(absolute('missing.json'), {ok: false}, {
      writeFile: async (path, data) => { operations.push(`write:${path}`); files.set(path, data); throw new Error('injected write failure'); },
      rename: async (from, to) => { operations.push(`rename:${from}:${to}`); },
      rm: async path => { operations.push(`rm:${path}`); files.delete(path); },
      uuid: () => 'uuid-write-failure',
    })).rejects.toThrow('injected write failure');
    expect(operations).toEqual([
      `write:${absolute('.missing.json.uuid-write-failure.tmp')}`,
      `rm:${absolute('.missing.json.uuid-write-failure.tmp')}`,
    ]);
    expect(files.has(absolute('missing.json'))).toBe(false);
    expect(files.has(absolute('.missing.json.uuid-write-failure.tmp'))).toBe(false);
  });
});
