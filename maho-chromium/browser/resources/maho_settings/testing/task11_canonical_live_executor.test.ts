import {mkdtemp, readFile} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join} from 'node:path';

import {describe, expect, test} from 'bun:test';

import {
  CANONICAL_TASK11_SCRIPTS,
  runTask11CanonicalLiveExecutor,
  type Task11CanonicalExecutorHost,
  type Task11CanonicalRuntime,
} from './task11_canonical_live_executor.js';

const timestamp = '2026-08-01T00:00:00.000Z';
const appIdentity = {
  path: '/out/Maho', sha256: 'a'.repeat(64), size: 42, mtime_utc: timestamp,
};
const runtime: Task11CanonicalRuntime = {
  app: {pid: 200, startToken: 'app-start', userDataDir: '/tmp/task11/user-data'},
  relay: {pid: 100, startToken: 'relay-start', userDataDir: '/tmp/task11/user-data'},
  targetIds: ['owned-target'], tempRoot: '/tmp/task11', userDataDir: '/tmp/task11/user-data',
};
const clean = {
  passed: true,
  ownedProcessRoots: [runtime.relay, runtime.app],
  createdProfiles: [],
  ownedTargetIds: ['owned-target'],
  tempRoot: '/tmp/task11',
  cleanedProcesses: [runtime.relay, runtime.app],
  remainingOwnedProcesses: [],
  remainingOwnedTargetIds: [],
  remainingOwnedProfiles: [],
  baselinePreserved: {processes: true, targets: true, profiles: true},
  tempRootAbsent: true,
  errors: [],
};

async function fixture(overrides: Partial<Task11CanonicalExecutorHost> = {}) {
  const evidenceRoot = await mkdtemp(join(tmpdir(), 'task11-canonical-'));
  const events: string[] = [];
  let tick = 0;
  const host: Task11CanonicalExecutorHost = {
    now: () => `2026-08-01T00:00:0${tick++}.000Z`,
    identifyApp: async path => { events.push(`identify:${path}`); return appIdentity; },
    startOwnedLifecycle: async () => { events.push('start'); return runtime; },
    runScript: async spec => {
      events.push(`script:${spec.script}`);
      return {
        exitCode: 0, signal: null,
        stdout: spec.script.includes('profiles') ?
          '[PASS] Disposable profile create/delete round trip completed\n' : '[PASS] Chromium settings\n',
        stderr: '',
      };
    },
    cleanup: async owned => { events.push(`cleanup:${owned?.app.pid ?? 'none'}`); return clean; },
    ...overrides,
  };
  return {evidenceRoot, events, host};
}

describe('Task 11 canonical live executor', () => {
  test('gives a freshly signed macOS app enough bounded time to expose CDP', async () => {
    const source = await Bun.file(`${import.meta.dir}/task11_canonical_live_executor.ts`).text();
    expect(source).toContain('const TASK11_APP_STARTUP_TIMEOUT_MS = 60_000;');
    expect(source).toMatch(
      /waitForHttp\(\s*'http:\/\/127\.0\.0\.1:9222\/json\/version'[\s\S]*TASK11_APP_STARTUP_TIMEOUT_MS/,
    );
  });

  test('runs the two exact canonical Bun scripts sequentially and binds every row to the current app', async () => {
    const {evidenceRoot, events, host} = await fixture();
    const result = await runTask11CanonicalLiveExecutor({
      app: '/out/Maho', workspaceRoot: '/workspace', evidenceRoot, runId: 'run-1',
    }, host);

    expect(result.status).toBe('PASS');
    expect(events).toEqual([
      'identify:/out/Maho', 'start', 'identify:/out/Maho',
      `script:${CANONICAL_TASK11_SCRIPTS[0]}`, 'identify:/out/Maho',
      `script:${CANONICAL_TASK11_SCRIPTS[1]}`, 'identify:/out/Maho', 'cleanup:200',
    ]);
    const receipt = JSON.parse(await readFile(result.receiptPath, 'utf8')) as {
      status: string;
      scripts: Array<{command: string; argv: string[]; cwd: string; current_app: typeof appIdentity}>;
      cleanup: {profiles_disposable_delete_reported: boolean};
    };
    expect(receipt.status).toBe('PASS');
    expect(receipt.scripts.map(row => row.command)).toEqual(
      CANONICAL_TASK11_SCRIPTS.map(script => `bun ${script}`));
    expect(receipt.scripts.map(row => row.argv)).toEqual(
      CANONICAL_TASK11_SCRIPTS.map(script => ['bun', script]));
    expect(receipt.scripts.every(row => row.cwd === '/workspace')).toBe(true);
    expect(receipt.scripts.every(row => row.current_app.sha256 === appIdentity.sha256)).toBe(true);
    expect(receipt.cleanup.profiles_disposable_delete_reported).toBe(true);
  });

  test('fails closed before lifecycle launch when the app cannot be identified', async () => {
    const {evidenceRoot, events, host} = await fixture({
      identifyApp: async () => { events.push('identify:failed'); throw new Error('stale app'); },
    });
    const result = await runTask11CanonicalLiveExecutor({
      app: '/out/Maho', workspaceRoot: '/workspace', evidenceRoot, runId: 'run-2',
    }, host);
    expect(result.status).toBe('FAIL');
    expect(events).toEqual(['identify:failed', 'cleanup:none']);
  });

  test('stops before the second script when the first child fails and still cleans up', async () => {
    const {evidenceRoot, events, host} = await fixture({
      runScript: async spec => {
        events.push(`script:${spec.script}`);
        return {exitCode: 7, signal: null, stdout: 'partial\n', stderr: 'failed\n'};
      },
    });
    const result = await runTask11CanonicalLiveExecutor({
      app: '/out/Maho', workspaceRoot: '/workspace', evidenceRoot, runId: 'run-3',
    }, host);
    expect(result.status).toBe('FAIL');
    expect(events.filter(event => event.startsWith('script:'))).toEqual([
      `script:${CANONICAL_TASK11_SCRIPTS[0]}`,
    ]);
    expect(events.at(-1)).toBe('cleanup:200');
  });

  test('rejects an app identity change before executing the affected row', async () => {
    let reads = 0;
    const {evidenceRoot, events, host} = await fixture({
      identifyApp: async path => {
        events.push(`identify:${path}`);
        reads++;
        return reads < 3 ? appIdentity : {...appIdentity, sha256: 'b'.repeat(64)};
      },
    });
    const result = await runTask11CanonicalLiveExecutor({
      app: '/out/Maho', workspaceRoot: '/workspace', evidenceRoot, runId: 'run-4',
    }, host);
    expect(result.status).toBe('FAIL');
    expect(events.filter(event => event.startsWith('script:'))).toEqual([
      `script:${CANONICAL_TASK11_SCRIPTS[0]}`,
    ]);
    expect(events.at(-1)).toBe('cleanup:200');
  });

  test('cleanup failure overrides successful children', async () => {
    const {evidenceRoot, host} = await fixture({
      cleanup: async () => ({...clean, passed: false, tempRootAbsent: false, errors: ['temp remains']}),
    });
    const result = await runTask11CanonicalLiveExecutor({
      app: '/out/Maho', workspaceRoot: '/workspace', evidenceRoot, runId: 'run-5',
    }, host);
    expect(result.status).toBe('FAIL');
    const receipt = JSON.parse(await readFile(result.receiptPath, 'utf8')) as {status: string};
    expect(receipt.status).toBe('FAIL');
  });

  test('publishes the final receipt once, after cleanup, and propagates atomic persistence failure', async () => {
    const {evidenceRoot, events, host} = await fixture({
      persistReceipt: async () => { events.push('persist'); throw new Error('rename failed'); },
    });
    await expect(runTask11CanonicalLiveExecutor({
      app: '/out/Maho', workspaceRoot: '/workspace', evidenceRoot, runId: 'run-6',
    }, host)).rejects.toThrow('rename failed');
    expect(events.at(-2)).toBe('cleanup:200');
    expect(events.at(-1)).toBe('persist');
  });
});
