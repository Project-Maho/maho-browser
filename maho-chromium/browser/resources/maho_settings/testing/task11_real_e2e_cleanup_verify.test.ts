import {describe, expect, test} from 'bun:test';
import {mkdtemp, readFile, rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join, resolve} from 'node:path';

import {
  type CleanupObservationHooks,
  parseCleanupVerifierArguments,
  runCleanupVerifier,
  verifyAggregateCleanup,
} from './task11_real_e2e_cleanup_verify.js';

const root = '/tmp/task11';
const artifact = (path: string) => ({path, sha256: 'b'.repeat(64), size: 7, mtime_utc: '2026-08-01T10:00:00.000Z'});
function receipt(scenario: 'profiles' | 'chromium-settings') {
  const marker = `--maho-task11-owner=run-1-${scenario}`;
  return {
    schema_version: 1, receipt_type: 'cleanup', status: 'PASS', run_id: 'run-1', scenario,
    started_at: '2026-08-01T10:00:01.000Z', completed_at: '2026-08-01T10:00:02.000Z',
    artifacts: {app: artifact(`${root}/Maho`), browser_tests: artifact(`${root}/browser_tests`)},
    command: ['bun', 'entry.ts'], source_versions: {executor: 'v1'}, tool_versions: {bun: '1.3.13'},
    before_observables: {}, after_observables: {}, assertions: [{id: 'cleanup', passed: true, evidence_paths: [], detail: 'absent'}],
    ownership: {
      markers: [marker], owned_processes: [{role: 'app', pid: scenario === 'profiles' ? 100 : 200, start_token: `start-${scenario}`, marker}],
      temp_root: `${root}/${scenario}/temp`, chromium_profiles: [{id: `chromium-${scenario}`, path: `${root}/${scenario}/temp/user-data`}],
      maho_profiles: [{id: `maho-${scenario}`, name: `Maho ${scenario}`, path: `${root}/${scenario}/temp/maho`}],
    },
    raw_evidence_paths: [`${root}/${scenario}/after.json`], cleanup_receipt_path: `${root}/${scenario}/cleanup-receipt.json`, errors: [],
    cleanup: {passed: true, remaining_owned_processes: [], remaining_temp_roots: [], remaining_chromium_profiles: [], remaining_maho_profiles: []},
  };
}

const absentHooks: CleanupObservationHooks = {
  listProcesses: async () => [],
  pathExists: async () => false,
};

describe('Task 11 aggregate cleanup verifier', () => {
  test('dispatches direct Bun CLI help and invalid arguments', async () => {
    const executable = resolve(import.meta.dir, 'task11_real_e2e_cleanup_verify.ts');
    const directory = await mkdtemp(join(tmpdir(), 'task11-cleanup-cli-'));
    const controlPath = join(directory, 'control');
    const invoke = async (argument: string) => {
      const stdoutPath = join(directory, `${argument.slice(2)}.stdout`);
      const stderrPath = join(directory, `${argument.slice(2)}.stderr`);
      const exitPath = join(directory, `${argument.slice(2)}.exit`);
      const child = Bun.spawn(['/bin/sh', '-c', '"$1" "$2" "$3" > "$4" 2> "$5"; printf %s "$?" > "$6"', 'task11-cli', process.execPath, executable, argument, stdoutPath, stderrPath, exitPath], {stdin: 'ignore', stdout: 'ignore', stderr: 'ignore'});
      await child.exited;
      const [stdout, stderr, exitCode] = await Promise.all([readFile(stdoutPath), readFile(stderrPath), readFile(exitPath, 'utf8')]);
      return {stdout, stderr, exitCode: Number(exitCode)};
    };
    try {
      const control = Bun.spawn(['/bin/sh', '-c', 'printf hello > "$1"', 'task11-cli', controlPath], {stdin: 'ignore', stdout: 'ignore', stderr: 'ignore'});
      expect(await control.exited).toBe(0);
      expect(await readFile(controlPath, 'utf8')).toBe('hello');

      const help = await invoke('--help');
      expect(help.exitCode).toBe(0);
      expect([...help.stdout]).toEqual([...new TextEncoder().encode('Usage: task11_real_e2e_cleanup_verify --profiles-receipt <absolute-path> --chromium-settings-receipt <absolute-path>\n')]);
      expect([...help.stderr]).toEqual([]);

      const invalid = await invoke('--bad');
      expect(invalid.exitCode).toBe(2);
      const lines = invalid.stdout.toString().trimEnd().split('\n');
      expect(lines).toHaveLength(1);
      expect(JSON.parse(lines[0] ?? '')).toMatchObject({schema_version: 1, status: 'FAIL'});
      expect([...invalid.stderr]).toEqual([]);
    } finally {
      await rm(directory, {recursive: true, force: true});
    }
  });

  test('strictly parses help and two absolute receipt paths', () => {
    expect(parseCleanupVerifierArguments(['--help'])).toEqual({kind: 'help'});
    expect(parseCleanupVerifierArguments(['--profiles-receipt', `${root}/profiles.json`, '--chromium-settings-receipt', `${root}/settings.json`])).toEqual({kind: 'verify', profilesReceipt: `${root}/profiles.json`, chromiumSettingsReceipt: `${root}/settings.json`});
    expect(() => parseCleanupVerifierArguments(['--profiles-receipt', 'relative', '--chromium-settings-receipt', `${root}/settings.json`])).toThrow(/absolute/i);
    expect(() => parseCleanupVerifierArguments(['--profiles-receipt', `${root}/a`, '--profiles-receipt', `${root}/b`, '--chromium-settings-receipt', `${root}/c`])).toThrow(/duplicate/i);
  });

  test('passes only matching fresh receipts with no re-observed owned resources', async () => {
    const result = await verifyAggregateCleanup(receipt('profiles'), receipt('chromium-settings'), absentHooks, () => new Date('2026-08-01T10:00:03.000Z'));
    expect(result).toEqual({schema_version: 1, status: 'PASS', run_id: 'run-1', errors: [], observations: {remaining_owned_processes: [], remaining_temp_roots: [], remaining_chromium_profiles: [], remaining_maho_profiles: []}});
  });

  test('fails closed on mismatches, stale timestamps, receipt failures, and stable marker-correlated remnants', async () => {
    expect((await verifyAggregateCleanup(receipt('profiles'), {...receipt('chromium-settings'), run_id: 'other'}, absentHooks)).status).toBe('FAIL');
    expect((await verifyAggregateCleanup({...receipt('profiles'), started_at: '2026-08-01T09:00:00.000Z'}, receipt('chromium-settings'), absentHooks)).errors.join('\n')).toMatch(/stale/i);
    expect((await verifyAggregateCleanup({...receipt('profiles'), cleanup: {...receipt('profiles').cleanup, passed: false}}, receipt('chromium-settings'), absentHooks)).status).toBe('FAIL');
    const owned = receipt('profiles').ownership.owned_processes[0];
    const hooks: CleanupObservationHooks = {
      listProcesses: async () => [{pid: owned.pid, parentPid: 1, startToken: owned.start_token, commandLine: ['/tmp/Maho', owned.marker]}],
      pathExists: async path => path.endsWith('/maho'),
    };
    const failed = await verifyAggregateCleanup(receipt('profiles'), receipt('chromium-settings'), hooks);
    expect(failed.status).toBe('FAIL');
    expect(failed.observations.remaining_owned_processes).toHaveLength(1);
    expect(failed.observations.remaining_maho_profiles).toHaveLength(1);
  });

  test('emits one machine-readable status and nonzero for invalid input', async () => {
    const stdout: string[] = [];
    const stderr: string[] = [];
    const exitCode = await runCleanupVerifier(['--bad'], {
      readText: async () => { throw new Error('must not read'); },
      hooks: absentHooks,
      stdout: value => stdout.push(value),
      stderr: value => stderr.push(value),
    });
    expect(exitCode).toBe(2);
    expect(JSON.parse(stdout.join(''))).toMatchObject({status: 'FAIL'});
    expect(stderr).toEqual([]);
  });

  test('rejects duplicate keys while reading receipt files before schema validation', async () => {
    const valid = JSON.stringify(receipt('chromium-settings'));
    const stdout: string[] = [];
    const exitCode = await runCleanupVerifier([
      '--profiles-receipt', `${root}/profiles.json`,
      '--chromium-settings-receipt', `${root}/settings.json`,
    ], {
      readText: async path => path.endsWith('profiles.json')
        ? JSON.stringify(receipt('profiles')).replace('"cleanup":{', '"cleanup":{"passed":false,')
        : valid,
      hooks: absentHooks,
      stdout: value => stdout.push(value),
      stderr: () => undefined,
    });
    expect(exitCode).toBe(1);
    expect(JSON.parse(stdout.join('')).errors.join('\n')).toMatch(/duplicate.*passed/i);
  });
});
