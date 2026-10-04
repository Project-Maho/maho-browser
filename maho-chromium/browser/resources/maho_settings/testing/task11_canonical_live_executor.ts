import {createHash} from 'node:crypto';
import {access, mkdir, mkdtemp, readFile, rm, stat} from 'node:fs/promises';
import {createServer} from 'node:net';
import {tmpdir} from 'node:os';
import {join} from 'node:path';

import {writeAtomicJsonReceipt, type ArtifactReceipt, type ScenarioEntryArguments} from './task11_real_e2e_contract.js';
import type {
  Task11CleanupObservation,
  Task11OwnedRuntime,
  Task11RealE2eHost,
  Task11ScenarioObservation,
} from './task11_real_e2e_entry.js';
import type {CleanupReceipt, ProcessIdentity} from './task12_final_cleanup.js';

export const CANONICAL_TASK11_SCRIPTS = [
  'maho-chromium/browser/resources/maho_settings/testing/e2e_pane_profiles.ts',
  'maho-chromium/browser/resources/maho_settings/testing/e2e_pane_chromium_settings.ts',
] as const;

export interface Task11CanonicalRuntime {
  readonly app: ProcessIdentity;
  readonly relay: ProcessIdentity;
  readonly targetIds: readonly string[];
  readonly tempRoot: string;
  readonly userDataDir: string;
}

export interface Task11CanonicalScriptSpec {
  readonly script: typeof CANONICAL_TASK11_SCRIPTS[number];
  readonly cwd: string;
}

export interface Task11CanonicalScriptResult {
  readonly exitCode: number;
  readonly signal: string | null;
  readonly stdout: string;
  readonly stderr: string;
}

export interface Task11CanonicalExecutorHost {
  now(): string;
  identifyApp(path: string): Promise<ArtifactReceipt>;
  startOwnedLifecycle(options: Task11CanonicalExecutorOptions): Promise<Task11CanonicalRuntime>;
  runScript(spec: Task11CanonicalScriptSpec): Promise<Task11CanonicalScriptResult>;
  cleanup(runtime: Task11CanonicalRuntime | undefined): Promise<CleanupReceipt>;
  persistReceipt?(path: string, receipt: unknown): Promise<void>;
}

export interface Task11CanonicalExecutorOptions {
  readonly app: string;
  readonly workspaceRoot: string;
  readonly evidenceRoot: string;
  readonly runId: string;
}

export interface Task11CanonicalExecutorResult {
  readonly status: 'PASS' | 'FAIL';
  readonly receiptPath: string;
  readonly errors: readonly string[];
}

const errorText = (error: unknown): string => error instanceof Error ? error.message : String(error);

function sameArtifact(left: ArtifactReceipt, right: ArtifactReceipt): boolean {
  return left.path === right.path && left.sha256 === right.sha256 && left.size === right.size &&
    left.mtime_utc === right.mtime_utc;
}

export async function identifyTask11Artifact(path: string): Promise<ArtifactReceipt> {
  const bytes = await readFile(path);
  const metadata = await stat(path);
  return {
    path,
    sha256: createHash('sha256').update(bytes).digest('hex'),
    size: metadata.size,
    mtime_utc: metadata.mtime.toISOString(),
  };
}

export async function runTask11CanonicalLiveExecutor(
  options: Task11CanonicalExecutorOptions,
  host: Task11CanonicalExecutorHost,
): Promise<Task11CanonicalExecutorResult> {
  const receiptPath = join(options.evidenceRoot, `${options.runId}.canonical-live.json`);
  const startedAt = host.now();
  const errors: string[] = [];
  const scripts: Array<Record<string, unknown>> = [];
  let expectedApp: ArtifactReceipt | undefined;
  let runtime: Task11CanonicalRuntime | undefined;

  try {
    expectedApp = await host.identifyApp(options.app);
    runtime = await host.startOwnedLifecycle(options);
    for (const script of CANONICAL_TASK11_SCRIPTS) {
      const currentApp = await host.identifyApp(options.app);
      if (!sameArtifact(expectedApp, currentApp)) {
        throw new Error(`Current app identity changed before ${script}`);
      }
      const result = await host.runScript({script, cwd: options.workspaceRoot});
      scripts.push({
        script,
        command: `bun ${script}`,
        argv: ['bun', script],
        cwd: options.workspaceRoot,
        current_app: currentApp,
        exit_code: result.exitCode,
        signal: result.signal,
        stdout: result.stdout,
        stderr: result.stderr,
      });
      if (result.exitCode !== 0 || result.signal !== null) {
        throw new Error(`${script} failed with ${result.signal ?? `exit ${result.exitCode}`}`);
      }
    }
    const finalApp = await host.identifyApp(options.app);
    if (!sameArtifact(expectedApp, finalApp)) throw new Error('Current app identity changed after canonical scripts');
  } catch (error) {
    errors.push(errorText(error));
  }

  let cleanup: CleanupReceipt;
  try {
    cleanup = await host.cleanup(runtime);
    if (!cleanup.passed) errors.push(`Verified cleanup failed: ${cleanup.errors.join('; ')}`);
  } catch (error) {
    errors.push(errorText(error));
    cleanup = {
      passed: false,
      ownedProcessRoots: [], createdProfiles: [], ownedTargetIds: [], tempRoot: runtime?.tempRoot ?? '',
      cleanedProcesses: [], remainingOwnedProcesses: [], remainingOwnedTargetIds: [], remainingOwnedProfiles: [],
      baselinePreserved: {processes: false, targets: false, profiles: false}, tempRootAbsent: false,
      errors: [errorText(error)],
    };
  }

  const status = errors.length === 0 && scripts.length === CANONICAL_TASK11_SCRIPTS.length ? 'PASS' : 'FAIL';
  const receipt = {
    schema_version: 1,
    receipt_type: 'task11-canonical-live',
    status,
    run_id: options.runId,
    started_at: startedAt,
    completed_at: host.now(),
    app: expectedApp,
    scripts,
    cleanup: {
      ...cleanup,
      profiles_disposable_delete_reported: scripts.some(row =>
        typeof row.stdout === 'string' && row.stdout.includes('Disposable profile create/delete round trip completed')),
    },
    errors,
  };
  await mkdir(options.evidenceRoot, {recursive: true});
  await (host.persistReceipt ?? writeAtomicJsonReceipt)(receiptPath, receipt);
  return {status, receiptPath, errors};
}

async function runBunScript(script: string, cwd: string): Promise<Task11CanonicalScriptResult> {
  const child = Bun.spawn(['bun', script], {cwd, stdin: 'ignore', stdout: 'pipe', stderr: 'pipe'});
  const [exitCode, stdout, stderr] = await Promise.all([
    child.exited,
    new Response(child.stdout).text(),
    new Response(child.stderr).text(),
  ]);
  return {exitCode, signal: child.signalCode, stdout, stderr};
}

interface DefaultOwnedRuntime extends Task11OwnedRuntime {
  readonly appChild: ReturnType<typeof Bun.spawn>;
  readonly relayChild: ReturnType<typeof Bun.spawn>;
  readonly tempRoot: string;
  readonly userDataDir: string;
  readonly evidenceRoot: string;
  readonly runId: string;
  readonly scenario: ScenarioEntryArguments['scenario'];
  readonly appIdentityPath: string;
  readonly lifecycleReceiptPath: string;
}

async function portAvailable(port: number): Promise<boolean> {
  return await new Promise(resolve => {
    const server = createServer();
    server.once('error', () => resolve(false));
    server.listen(port, '127.0.0.1', () => server.close(() => resolve(true)));
  });
}

const TASK11_APP_STARTUP_TIMEOUT_MS = 60_000;

async function waitForHttp(url: string, expected: (response: Response) => Promise<boolean>, timeoutMs = 30_000): Promise<void> {
  const deadline = Date.now() + timeoutMs;
  let lastError = 'not ready';
  while (Date.now() < deadline) {
    try {
      const response = await fetch(url, {signal: AbortSignal.timeout(1_000)});
      if (await expected(response)) return;
      lastError = `HTTP ${response.status}`;
    } catch (error) {
      lastError = errorText(error);
    }
    await Bun.sleep(100);
  }
  throw new Error(`Timed out waiting for ${url}: ${lastError}`);
}

async function startToken(pid: number): Promise<string> {
  const child = Bun.spawn(['/bin/ps', '-p', String(pid), '-o', 'lstart='], {stdout: 'pipe', stderr: 'pipe'});
  const [exitCode, output] = await Promise.all([child.exited, new Response(child.stdout).text()]);
  const token = output.trim();
  if (exitCode !== 0 || !token) throw new Error(`Cannot establish start token for PID ${pid}`);
  return token;
}

async function processAlive(pid: number): Promise<boolean> {
  try { process.kill(pid, 0); return true; } catch { return false; }
}

async function stopChild(child: ReturnType<typeof Bun.spawn>): Promise<void> {
  if (!await processAlive(child.pid)) return;
  child.kill('SIGTERM');
  const exited = await Promise.race([child.exited.then(() => true), Bun.sleep(5_000).then(() => false)]);
  if (!exited && await processAlive(child.pid)) {
    child.kill('SIGKILL');
    await Promise.race([child.exited, Bun.sleep(5_000)]);
  }
}

async function sha256(path: string): Promise<string> {
  return createHash('sha256').update(await readFile(path)).digest('hex');
}

function scriptObservation(id: string, result: Task11CanonicalScriptResult, evidencePath: string): Task11ScenarioObservation {
  const output = `${result.stdout}\n${result.stderr}`;
  const passed = result.exitCode === 0 && result.signal === null;
  return {
    afterObservables: {exit_code: result.exitCode, signal: result.signal, stdout: result.stdout, stderr: result.stderr},
    rawEvidencePaths: [evidencePath],
    assertions: [
      {id, passed, evidence_paths: [evidencePath], detail: passed ? 'Deterministic live script exited 0' : output.trim() || `exit ${result.exitCode}`},
      ...(id === 'profiles-script' ? [{
        id: 'profiles-disposable-deletion',
        passed: output.includes('Disposable profile create/delete round trip completed'),
        evidence_paths: [evidencePath],
        detail: output.includes('Disposable profile create/delete round trip completed') ?
          'Disposable profile create/delete round trip directly reported' : 'Disposable deletion proof missing',
      }] : []),
    ],
  };
}

export function createDefaultTask11RealE2eHost({
  provisionRealProfiles = async runtime => runtime.ownership.maho_profiles,
}: {
  provisionRealProfiles?: (runtime: Task11OwnedRuntime) => Promise<Task11OwnedRuntime['ownership']['maho_profiles']>;
} = {}): Task11RealE2eHost {
  return {
    now: () => new Date().toISOString(),
    verifyArtifact: identifyTask11Artifact,
    async startOwnedRuntime(args: ScenarioEntryArguments): Promise<Task11OwnedRuntime> {
      if (!await portAvailable(9222) || !await portAvailable(18765)) {
        throw new Error('Owned ports 9222 and 18765 must both be free before launch');
      }
      const workspaceRoot = process.cwd();
      const tempRoot = await mkdtemp(join(tmpdir(), `task11-${args.runId}-`));
      const userDataDir = join(tempRoot, 'user-data');
      await mkdir(userDataDir, {mode: 0o700});
      await mkdir(args.evidenceRoot, {recursive: true});
      const appLogPath = join(args.evidenceRoot, `${args.runId}.app.log`);
      const relayLogPath = join(args.evidenceRoot, `${args.runId}.relay.log`);
      await Promise.all([Bun.write(appLogPath, ''), Bun.write(relayLogPath, '')]);
      const relayChild = Bun.spawn([
        'bunx', 'wrangler', 'dev', '--env', 'staging', '--local', '--ip', '127.0.0.1', '--port', '18765',
      ], {cwd: join(workspaceRoot, 'maho/relay'), stdin: 'ignore', stdout: 'ignore', stderr: 'ignore'});
      const appArgs = [
        `--user-data-dir=${userDataDir}`, '--remote-debugging-port=9222', '--maho-disable-login-gate',
        '--no-first-run', '--no-default-browser-check', '--disable-background-networking', '--disable-component-update',
        'https://example.com/',
      ];
      const appChild = Bun.spawn([args.app, ...appArgs], {stdin: 'ignore', stdout: 'ignore', stderr: 'ignore'});
      try {
        await Promise.all([
          waitForHttp('http://127.0.0.1:18765/health', async response => response.ok && (await response.text()).trim() === 'ok'),
          waitForHttp(
            'http://127.0.0.1:9222/json/version',
            async response => response.ok,
            TASK11_APP_STARTUP_TIMEOUT_MS,
          ),
        ]);
      } catch (error) {
        await Promise.all([stopChild(appChild), stopChild(relayChild)]);
        await rm(tempRoot, {recursive: true, force: true});
        throw error;
      }
      const [appStart, relayStart, sourceHash, sourceStat, appArtifact, browserTestsArtifact] = await Promise.all([
        startToken(appChild.pid), startToken(relayChild.pid),
        sha256(join(workspaceRoot, 'maho-chromium/browser/ui/webui/maho_settings/maho_settings_page_handler.cc')),
        stat(join(workspaceRoot, 'maho-chromium/browser/ui/webui/maho_settings/maho_settings_page_handler.cc')),
        identifyTask11Artifact(args.app), identifyTask11Artifact(args.browserTests),
      ]);
      const appIdentityPath = join(args.evidenceRoot, `${args.runId}.app-identity.json`);
      const lifecycleReceiptPath = join(args.evidenceRoot, `${args.runId}.lifecycle.json`);
      await writeAtomicJsonReceipt(appIdentityPath, {app: appArtifact, browser_tests: browserTestsArtifact,
        source: {path: join(workspaceRoot, 'maho-chromium/browser/ui/webui/maho_settings/maho_settings_page_handler.cc'), sha256: sourceHash, mtime_utc: sourceStat.mtime.toISOString()}});
      await writeAtomicJsonReceipt(lifecycleReceiptPath, {status: 'READY', run_id: args.runId, scenario: args.scenario,
        app: {pid: appChild.pid, start_token: appStart, argv: [args.app, ...appArgs]},
        relay: {pid: relayChild.pid, start_token: relayStart}, temp_root: tempRoot, user_data_dir: userDataDir,
        listeners: {cdp_9222: true, relay_18765: true}});
      return {
        command: ['bun', 'maho-chromium/browser/resources/maho_settings/testing/task11_real_e2e_entry.ts',
          '--scenario', args.scenario, '--app', args.app, '--browser-tests', args.browserTests,
          '--evidence-root', args.evidenceRoot, '--run-id', args.runId],
        sourceVersions: {maho_settings_page_handler_sha256: sourceHash, maho_settings_page_handler_mtime_utc: sourceStat.mtime.toISOString()},
        toolVersions: {bun: Bun.version, platform: process.platform},
        beforeObservables: {ports_free_before_launch: true, app: appArtifact, browser_tests: browserTestsArtifact},
        ownership: {markers: [`--user-data-dir=${userDataDir}`, args.runId], owned_processes: [
          {role: 'app', pid: appChild.pid, start_token: appStart, marker: `--user-data-dir=${userDataDir}`},
          {role: 'relay', pid: relayChild.pid, start_token: relayStart, marker: args.runId},
        ], temp_root: tempRoot, chromium_profiles: [], maho_profiles: []},
        rawEvidencePaths: [appLogPath, relayLogPath, appIdentityPath, lifecycleReceiptPath],
        appChild, relayChild, tempRoot, userDataDir, evidenceRoot: args.evidenceRoot, runId: args.runId,
        scenario: args.scenario, appIdentityPath, lifecycleReceiptPath,
      } satisfies DefaultOwnedRuntime;
    },
    async awaitOwnedSettingsTarget(): Promise<void> {
      await waitForHttp('http://127.0.0.1:9222/json/version', async response => response.ok);
    },
    async provisionOwnedProfiles(runtime): Promise<void> {
      const profiles = await provisionRealProfiles(runtime);
      (runtime.ownership.maho_profiles as unknown[]).splice(0, Infinity, ...profiles);
    },
    async runProfilesScenario(runtime): Promise<Task11ScenarioObservation> {
      const owned = runtime as DefaultOwnedRuntime;
      const result = await runBunScript(CANONICAL_TASK11_SCRIPTS[0], process.cwd());
      const evidencePath = join(owned.evidenceRoot, `${owned.runId}.profiles.output.log`);
      await Bun.write(evidencePath, `${result.stdout}${result.stderr}`);
      return scriptObservation('profiles-script', result, evidencePath);
    },
    async runChromiumSettingsScenario(runtime): Promise<Task11ScenarioObservation> {
      const owned = runtime as DefaultOwnedRuntime;
      const result = await runBunScript(CANONICAL_TASK11_SCRIPTS[1], process.cwd());
      const evidencePath = join(owned.evidenceRoot, `${owned.runId}.chromium-settings.output.log`);
      await Bun.write(evidencePath, `${result.stdout}${result.stderr}`);
      return scriptObservation('chromium-settings-script', result, evidencePath);
    },
    async verifiedCleanup(runtime): Promise<Task11CleanupObservation | undefined> {
      if (!runtime) return undefined;
      const owned = runtime as DefaultOwnedRuntime;
      await Promise.all([stopChild(owned.appChild), stopChild(owned.relayChild)]);
      await rm(owned.tempRoot, {recursive: true, force: true});
      const [appAlive, relayAlive, cdpFree, relayFree, tempExists] = await Promise.all([
        processAlive(owned.appChild.pid), processAlive(owned.relayChild.pid), portAvailable(9222), portAvailable(18765),
        access(owned.tempRoot).then(() => true, () => false),
      ]);
      const assertions = [
        {id: 'app-process-absent', passed: !appAlive, evidence_paths: [owned.lifecycleReceiptPath], detail: `PID ${owned.appChild.pid} absent=${!appAlive}`},
        {id: 'relay-process-absent', passed: !relayAlive, evidence_paths: [owned.lifecycleReceiptPath], detail: `PID ${owned.relayChild.pid} absent=${!relayAlive}`},
        {id: 'listeners-absent', passed: cdpFree && relayFree, evidence_paths: [owned.lifecycleReceiptPath], detail: `9222_free=${cdpFree}, 18765_free=${relayFree}`},
        {id: 'temp-root-absent', passed: !tempExists, evidence_paths: [owned.lifecycleReceiptPath], detail: `${owned.tempRoot} absent=${!tempExists}`},
      ];
      const passed = assertions.every(assertion => assertion.passed);
      return {afterObservables: {app_alive: appAlive, relay_alive: relayAlive, cdp_9222_free: cdpFree,
        relay_18765_free: relayFree, temp_root_exists: tempExists}, assertions,
        rawEvidencePaths: [owned.appIdentityPath, owned.lifecycleReceiptPath], cleanup: {passed,
          remaining_owned_processes: [], remaining_temp_roots: tempExists ? [owned.tempRoot] : [],
          remaining_chromium_profiles: [], remaining_maho_profiles: []}};
    },
  };
}
