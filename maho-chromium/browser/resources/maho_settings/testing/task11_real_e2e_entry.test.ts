import {mkdtemp, readFile, rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {EventEmitter} from 'node:events';

import {describe, expect, test} from 'bun:test';

import {parseCleanupReceipt, parseScenarioReceipt, type ScenarioEntryArguments} from './task11_real_e2e_contract.ts';
import {createDefaultTask11RealE2eHost} from './task11_canonical_live_executor.ts';
import {
  runTask11RealE2e,
  type Task11StdoutWriter,
  type Task11CleanupObservation,
  type Task11OwnedRuntime,
  type Task11RealE2eHost,
  type Task11ScenarioObservation,
  writeStdout,
} from './task11_real_e2e_entry.ts';

const timestamp = '2026-08-01T00:00:00.000Z';
const artifact = (path: string) => ({path, sha256: 'a'.repeat(64), size: 1, mtime_utc: timestamp});
const runtime: Task11OwnedRuntime = {
  command: ['maho'], sourceVersions: {maho: '1'}, toolVersions: {bun: '1'},
  beforeObservables: {ready: false}, rawEvidencePaths: [],
  ownership: {markers: ['owned'], owned_processes: [], temp_root: '/tmp/task11', chromium_profiles: [], maho_profiles: []},
};
const scenario: Task11ScenarioObservation = {
  afterObservables: {ready: true}, assertions: [{id: 'scenario', passed: true, evidence_paths: [], detail: 'passed'}], rawEvidencePaths: [],
};
const cleanup: Task11CleanupObservation = {
  afterObservables: {clean: true}, assertions: [{id: 'cleanup', passed: true, evidence_paths: [], detail: 'passed'}], rawEvidencePaths: [],
  cleanup: {passed: true, remaining_owned_processes: [], remaining_temp_roots: [], remaining_chromium_profiles: [], remaining_maho_profiles: []},
};

async function fixture(selected: ScenarioEntryArguments['scenario'], overrides: Partial<Task11RealE2eHost> = {}) {
  const evidenceRoot = await mkdtemp(join(tmpdir(), 'task11-entry-'));
  const args: ScenarioEntryArguments = {scenario: selected, app: '/app', browserTests: '/browser-tests', evidenceRoot, runId: 'run'};
  const events: string[] = [];
  const host: Task11RealE2eHost = {
    now: () => timestamp,
    verifyArtifact: async path => { events.push(`verify:${path}`); return artifact(path); },
    startOwnedRuntime: async () => { events.push('start'); return runtime; },
    awaitOwnedSettingsTarget: async () => { events.push('await'); },
    provisionOwnedProfiles: async () => {
      events.push('provision-profiles');
    },
    runProfilesScenario: async () => { events.push('profiles'); return scenario; },
    runChromiumSettingsScenario: async () => { events.push('chromium'); return scenario; },
    verifiedCleanup: async () => { events.push('cleanup'); return cleanup; },
    ...overrides,
  };
  return {args, events, host};
}

class ControlledStdoutWriter extends EventEmitter implements Task11StdoutWriter {
  readonly chunks: string[] = [];
  private callback: ((error?: Error | null) => void) | undefined;

  write(value: string, callback: (error?: Error | null) => void): boolean {
    this.chunks.push(value);
    this.callback = callback;
    return false;
  }

  completeWrite(error?: Error): void {
    const callback = this.callback;
    this.callback = undefined;
    if (!callback) throw new Error('No pending stdout write');
    callback(error);
  }

  completeDrain(): void {
    this.emit('drain');
  }
}

describe('runTask11RealE2e', () => {
  test('stdout completion waits for both the write callback and backpressure drain', async () => {
    const writer = new ControlledStdoutWriter();
    let completed = false;
    const writing = writeStdout({status: 'FAIL', errors: ['missing app']}, writer)
      .then(() => { completed = true; });

    expect(writer.chunks).toEqual(['{"status":"FAIL","errors":["missing app"]}\n']);
    writer.completeWrite();
    await Promise.resolve();
    expect(completed).toBe(false);

    writer.completeDrain();
    await writing;
    expect(completed).toBe(true);
  });

  test('stdout completion rejects write callback errors', async () => {
    const writer = new ControlledStdoutWriter();
    const writing = writeStdout({status: 'FAIL', errors: ['missing app']}, writer);
    writer.completeWrite(new Error('stdout failed'));
    await expect(writing).rejects.toThrow('stdout failed');
  });

  for (const selected of ['profiles', 'chromium-settings'] as const) {
    test(`CLI/default invocation installs a concrete host for ${selected}`, async () => {
      const executable = join(import.meta.dir, 'task11_real_e2e_entry.ts');
      const evidenceRoot = await mkdtemp(join(tmpdir(), 'task11-cli-default-'));
      const stdoutPath = join(evidenceRoot, 'stdout');
      const stderrPath = join(evidenceRoot, 'stderr');
      const exitPath = join(evidenceRoot, 'exit');
      try {
        const child = Bun.spawn([
          '/bin/sh', '-c',
          '"$1" "$2" --scenario "$3" --app "$4" --browser-tests "$5" --evidence-root "$6" --run-id "$7" > "$8" 2> "$9"; printf %s "$?" > "${10}"',
          'task11-entry', process.execPath, executable, selected,
          join(evidenceRoot, 'missing-app'), join(evidenceRoot, 'missing-browser-tests'),
          evidenceRoot, `default-${selected}`, stdoutPath, stderrPath, exitPath,
        ], {stdin: 'ignore', stdout: 'ignore', stderr: 'ignore'});
        await child.exited;
        const [exitCode, stdout, stderr] = await Promise.all([
          readFile(exitPath, 'utf8').then(Number),
          readFile(stdoutPath, 'utf8'),
          readFile(stderrPath, 'utf8'),
        ]);

        expect(exitCode).toBe(1);
        expect(stderr).toBe('');
        expect(stdout).not.toContain('No concrete Task11RealE2eHost is installed');
        expect(stdout).toMatch(/missing-app|ENOENT|no such file/i);
        expect(stdout.split('\n')).toHaveLength(2);
        expect(stdout.endsWith('\n')).toBe(true);
        expect(JSON.parse(stdout)).toMatchObject({status: 'FAIL'});
      } finally {
        await rm(evidenceRoot, {recursive: true, force: true});
      }
    });
  }

  test('default host provisions and records run-owned real profiles only for profiles', async () => {
    const events: string[] = [];
    const ownedRuntime: Task11OwnedRuntime = {
      ...runtime,
      ownership: {...runtime.ownership, maho_profiles: []},
    };
    const host = createDefaultTask11RealE2eHost({
      provisionRealProfiles: async () => {
        events.push('real-provision');
        return [
          {id: 'default-a', name: 'Default', path: '/tmp/task11/Default'},
          {id: 'editable-b', name: 'Profile B', path: '/tmp/task11/Profile B'},
        ];
      },
    });

    expect(typeof host.provisionOwnedProfiles).toBe('function');
    await host.provisionOwnedProfiles(ownedRuntime);
    expect(events).toEqual(['real-provision']);
    expect(ownedRuntime.ownership.maho_profiles).toEqual([
      {id: 'default-a', name: 'Default', path: '/tmp/task11/Default'},
      {id: 'editable-b', name: 'Profile B', path: '/tmp/task11/Profile B'},
    ]);

    const chromiumHost = createDefaultTask11RealE2eHost({
      provisionRealProfiles: async () => {
        throw new Error('chromium-settings must not provision profiles');
      },
    });
    const chromiumFixture = await fixture('chromium-settings', {
      provisionOwnedProfiles: chromiumHost.provisionOwnedProfiles,
    });
    expect((await runTask11RealE2e(chromiumFixture.args, chromiumFixture.host)).status).toBe('PASS');
  });

  for (const selected of ['profiles', 'chromium-settings'] as const) {
    test(`${selected} ordering and exclusivity`, async () => {
      const {args, events, host} = await fixture(selected);
      const result = await runTask11RealE2e(args, host);
      expect(result.status).toBe('PASS');
      expect(events).toEqual(selected === 'profiles' ?
        ['verify:/app', 'verify:/browser-tests', 'start', 'await', 'provision-profiles', 'profiles', 'cleanup'] :
        ['verify:/app', 'verify:/browser-tests', 'start', 'await', 'chromium', 'cleanup']);
      parseScenarioReceipt(JSON.parse(await readFile(result.scenarioReceiptPath!, 'utf8')));
      parseCleanupReceipt(JSON.parse(await readFile(result.cleanupReceiptPath!, 'utf8')));
    });
  }

  test('fresh owned profiles lifecycle provisions real default A and editable B before the script', async () => {
    const ownedProfilesRuntime: Task11OwnedRuntime = {
      ...runtime,
      ownership: {...runtime.ownership, maho_profiles: ['Default', 'Profile B']},
    };
    const {args, events, host} = await fixture('profiles', {
      startOwnedRuntime: async () => { events.push('start'); return ownedProfilesRuntime; },
    });

    const result = await runTask11RealE2e(args, host);

    expect(result.status).toBe('PASS');
    expect(events.indexOf('await')).toBeLessThan(events.indexOf('provision-profiles'));
    expect(events.indexOf('provision-profiles')).toBeLessThan(events.indexOf('profiles'));
    expect(events.at(-1)).toBe('cleanup');
    expect(JSON.parse(await readFile(result.scenarioReceiptPath!, 'utf8'))
      .ownership.maho_profiles).toEqual(['Default', 'Profile B']);
  });

  test('artifact failure occurs before launch', async () => {
    const {args, events, host} = await fixture('profiles', {
      verifyArtifact: async path => { events.push(`verify:${path}`); throw new Error('bad artifact'); },
    });
    const result = await runTask11RealE2e(args, host);
    expect(result.status).toBe('FAIL');
    expect(events).toEqual(['verify:/app', 'cleanup']);
  });

  test('scenario failure still cleans up and writes both receipts', async () => {
    const {args, events, host} = await fixture('profiles', {
      runProfilesScenario: async () => { events.push('profiles'); throw new Error('scenario failed'); },
    });
    const result = await runTask11RealE2e(args, host);
    expect(result.status).toBe('FAIL');
    expect(events.at(-1)).toBe('cleanup');
    expect(parseScenarioReceipt(JSON.parse(await readFile(result.scenarioReceiptPath!, 'utf8'))).status).toBe('FAIL');
    expect(parseCleanupReceipt(JSON.parse(await readFile(result.cleanupReceiptPath!, 'utf8'))).status).toBe('PASS');
  });

  test('cleanup failure makes the final result fail', async () => {
    const failedCleanup: Task11CleanupObservation = {...cleanup, cleanup: {...cleanup.cleanup, passed: false}};
    const {args, host} = await fixture('chromium-settings', {verifiedCleanup: async () => failedCleanup});
    const result = await runTask11RealE2e(args, host);
    expect(result.status).toBe('FAIL');
    expect(parseCleanupReceipt(JSON.parse(await readFile(result.cleanupReceiptPath!, 'utf8'))).status).toBe('FAIL');
  });
});
