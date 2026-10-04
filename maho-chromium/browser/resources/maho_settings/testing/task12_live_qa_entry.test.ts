import {describe, expect, test} from 'bun:test';

import {
  buildTask12LaunchArguments,
  defaultTask12LiveQaHost,
  runTask12LiveQa,
  validateTask11Receipt,
  type Task11ReceiptPrimitives,
  type Task12LiveQaHost,
} from './task12_live_qa_entry.js';

const WORKSPACE = '/workspace';
const APP_RELATIVE = 'chromium/src/out/Default/Maho.app/Contents/MacOS/Maho';
const BROWSER_TESTS_RELATIVE = 'chromium/src/out/Default/browser_tests';
const APP = `${WORKSPACE}/${APP_RELATIVE}`;
const APP_HASH = 'a'.repeat(64);
const BROWSER_TESTS_HASH = 'b'.repeat(64);

function receipt(overrides: Record<string, unknown> = {}): string {
  return JSON.stringify({
    schema_version: 1,
    status: 'PASS',
    app: {path: APP_RELATIVE, sha256: APP_HASH, size: 101, mtime_ms: 1001},
    browser_tests: {path: BROWSER_TESTS_RELATIVE, sha256: BROWSER_TESTS_HASH, size: 202, mtime_ms: 2002},
    gates: {
      focused_production_handler: 'PASS',
      production_failure_filters: 'PASS',
      profiles_e2e: 'PASS',
      chromium_settings_e2e: 'PASS',
      diagnostics: 'PASS',
      mojo_drift: 'PASS',
      fresh_linkage: 'PASS',
    },
    ...overrides,
  });
}

function receiptPrimitives(text: string): Task11ReceiptPrimitives {
  return {
    readText: async () => text,
    sha256: async path => path === APP ? APP_HASH : BROWSER_TESTS_HASH,
    stat: async path => path === APP ? {size: 101, mtimeMs: 1001} : {size: 202, mtimeMs: 2002},
  };
}

function host(overrides: Partial<Task12LiveQaHost> = {}) {
  const calls: string[] = [];
  const value: Task12LiveQaHost = {
    task11Passed: async app => { calls.push(`gate:${app}`); return true; },
    appExists: async () => { calls.push('app'); return true; },
    profileObservablesAvailable: (targetId, connection) => {
      calls.push(`probe:${targetId}`);
      return defaultTask12LiveQaHost.profileObservablesAvailable(targetId, connection ?? {
        transport: {
          send: async (method, params) => {
            calls.push(`capability:${method}`);
            expect((params as {expression?: string}).expression)
                .toContain('handler.getProfileObservablesSnapshot()');
            return {result: {value: true}};
          },
          on: () => () => undefined,
        },
      });
    },
    cdpPortAvailable: async () => { calls.push('port'); return true; },
    relayAvailable: async () => { calls.push('relay'); return true; },
    createTemporaryRoot: async () => { calls.push('temp'); return {root: '/tmp/task12-live-owned', userDataDir: '/tmp/task12-live-owned/user-data'}; },
    spawnApp: async (_app, argv) => {
      calls.push(`spawn:${argv.join('|')}`);
      return {pid: 42, startToken: 'owned-start', exited: Promise.resolve(0), close: async () => { calls.push('close'); }};
    },
    awaitReadyAndNavigate: async () => { calls.push('ready'); return {targetId: 'target-owned'}; },
    provisionProfiles: async () => { calls.push('profiles'); return {hostProfileId: 'A', targetProfileId: 'B', disposableProfileId: 'D'}; },
    runQa: async () => { calls.push('qa'); return {status: 'passed', scenarios: []}; },
    cleanup: async ownership => { calls.push(`cleanup:${ownership.rootPid}:${ownership.targetIds.join(',')}`); },
    ...overrides,
  };
  return {calls, value};
}

describe('canonical Task 11 receipt validation', () => {
  test('rejects a missing receipt', async () => {
    const primitives = receiptPrimitives('');
    primitives.readText = async () => { throw new Error('missing'); };
    expect(await validateTask11Receipt(APP, WORKSPACE, primitives)).toBe(false);
  });

  test('rejects malformed receipt JSON', async () => {
    expect(await validateTask11Receipt(APP, WORKSPACE, receiptPrimitives('{'))).toBe(false);
  });

  test('rejects stale app identity', async () => {
    const primitives = receiptPrimitives(receipt());
    primitives.sha256 = async path => path === APP ? 'c'.repeat(64) : BROWSER_TESTS_HASH;
    expect(await validateTask11Receipt(APP, WORKSPACE, primitives)).toBe(false);
  });

  test('rejects stale artifact stat identity', async () => {
    const primitives = receiptPrimitives(receipt());
    primitives.stat = async path => path === APP ? {size: 101, mtimeMs: 1002} : {size: 202, mtimeMs: 2002};
    expect(await validateTask11Receipt(APP, WORKSPACE, primitives)).toBe(false);
  });

  test('rejects a non-PASS receipt status', async () => {
    expect(await validateTask11Receipt(APP, WORKSPACE, receiptPrimitives(receipt({status: 'INCOMPLETE'})))).toBe(false);
  });

  test('rejects a receipt for a different app executable', async () => {
    expect(await validateTask11Receipt(`${WORKSPACE}/other/Maho`, WORKSPACE, receiptPrimitives(receipt()))).toBe(false);
  });

  test('rejects a receipt for the wrong browser_tests path', async () => {
    const text = receipt({browser_tests: {path: 'out/browser_tests', sha256: BROWSER_TESTS_HASH, size: 202, mtime_ms: 2002}});
    expect(await validateTask11Receipt(APP, WORKSPACE, receiptPrimitives(text))).toBe(false);
  });

  test('rejects any non-PASS named gate', async () => {
    const text = receipt({gates: {
      focused_production_handler: 'PASS', production_failure_filters: 'FAIL', profiles_e2e: 'PASS',
      chromium_settings_e2e: 'PASS', diagnostics: 'PASS', mojo_drift: 'PASS', fresh_linkage: 'PASS',
    }});
    expect(await validateTask11Receipt(APP, WORKSPACE, receiptPrimitives(text))).toBe(false);
  });

  test('accepts only a matching canonical receipt and both fresh identities', async () => {
    expect(await validateTask11Receipt(APP, WORKSPACE, receiptPrimitives(receipt()))).toBe(true);
  });
});

describe('Task 12 live QA orchestration', () => {
  test('blocks before temp creation or launch when Task 11 is not PASS', async () => {
    const fixture = host({task11Passed: async () => false});
    const result = await runTask12LiveQa({app: '/app/Maho', evidenceRoot: '/evidence'}, fixture.value);
    expect(result).toEqual({status: 'blocked-current-app', reasons: ['STALE_APP']});
    expect(fixture.calls).toEqual([]);
  });

  test('refuses an occupied fixed CDP port before temp creation', async () => {
    const fixture = host({cdpPortAvailable: async () => false});
    const result = await runTask12LiveQa({app: '/app/Maho', evidenceRoot: '/evidence'}, fixture.value);
    expect(result).toEqual({status: 'blocked-current-app', reasons: ['CDP_9222_UNAVAILABLE']});
    expect(fixture.calls).toEqual(['gate:/app/Maho', 'app']);
  });

  test('fails closed before profile mutations when the owned Settings handler lacks the generated method', async () => {
    const fixture = host({profileObservablesAvailable: targetId => {
      fixture.calls.push(`probe:${targetId}`);
      return defaultTask12LiveQaHost.profileObservablesAvailable(targetId, {
        transport: {
          send: async () => ({result: {value: false}}),
          on: () => () => undefined,
        },
      });
    }});
    const result = await runTask12LiveQa({app: '/app/Maho', evidenceRoot: '/evidence'}, fixture.value);
    expect(result).toEqual({
      status: 'missing-runtime-binding',
      reason: 'MISSING_RUNTIME_BINDING',
      missing: ['ProfileObservablesSnapshot'],
    });
    expect(fixture.calls).toEqual([
      'gate:/app/Maho', 'app', 'port', 'relay', 'temp',
      `spawn:${buildTask12LaunchArguments('/tmp/task12-live-owned/user-data').join('|')}`,
      'ready', 'probe:target-owned', 'cleanup:42:target-owned',
    ]);
    expect(fixture.calls).not.toContain('profiles');
    expect(fixture.calls).not.toContain('qa');
  });

  test('uses exact isolated launch argv, binds owned identities, and always cleans up', async () => {
    const fixture = host();
    const result = await runTask12LiveQa({app: '/app/Maho', evidenceRoot: '/evidence'}, fixture.value);
    expect(result.status).toBe('passed');
    expect(fixture.calls).toContain(`spawn:${buildTask12LaunchArguments('/tmp/task12-live-owned/user-data').join('|')}`);
    expect(fixture.calls).toContain('capability:Runtime.evaluate');
    expect(fixture.calls.at(-1)).toBe('cleanup:42:target-owned');
    expect(fixture.calls.indexOf('ready')).toBeLessThan(fixture.calls.indexOf('profiles'));
    expect(fixture.calls.indexOf('capability:Runtime.evaluate')).toBeLessThan(fixture.calls.indexOf('profiles'));
    expect(fixture.calls.indexOf('profiles')).toBeLessThan(fixture.calls.indexOf('qa'));
  });

  test('cleans owned launch state when runtime execution fails', async () => {
    const fixture = host({runQa: async () => { throw new Error('runtime failed'); }});
    expect(await runTask12LiveQa({app: '/app/Maho', evidenceRoot: '/evidence'}, fixture.value)).toEqual({status: 'failed', message: 'runtime failed'});
    expect(fixture.calls.at(-1)).toBe('cleanup:42:target-owned');
  });

  test('cleans the temporary root and owned process when provisioning fails', async () => {
    const fixture = host({provisionProfiles: async () => { throw new Error('provision failed'); }});
    expect(await runTask12LiveQa({app: '/app/Maho', evidenceRoot: '/evidence'}, fixture.value)).toEqual({status: 'failed', message: 'provision failed'});
    expect(fixture.calls.at(-1)).toBe('cleanup:42:target-owned');
  });
});
