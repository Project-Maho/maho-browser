import {createHash} from 'node:crypto';
import {access, mkdir, mkdtemp, readFile, rm, stat, writeFile} from 'node:fs/promises';
import {createServer} from 'node:net';
import {tmpdir} from 'node:os';
import {isAbsolute, join, relative, resolve} from 'node:path';

import {CDP, CDP_HOST, RELAY} from './cdp_harness.js';
import {runDefaultTask12Qa, type Task12CdpTarget, type Task12LiveProfiles} from './task12_default_runtime.js';
import {MISSING_RUNTIME_BINDING, type Task12DriverStatus} from './qa_two_profile_settings.js';
import {
  cleanupFinalBrowserState,
  type CleanupFinalBrowserStateHooks,
  type CleanupFinalBrowserStateInput,
} from './task12_final_cleanup.js';
import {
  buildOwnedLifecyclePlan,
  navigateOwnedSettingsTarget,
  runOwnedReadinessLifecycle,
  type CdpEventTransport,
  type LifecycleTimer,
} from './task12_live_lifecycle.js';
import {
  runProfileScenarios,
  type ProfileScenarioHost,
  type ProfileScenarioResult,
  type ProfileScenarioSeed,
  type ProfileScenarioSnapshot,
} from './task12_profile_scenarios.js';
import {createMacKernProcArgsReader, createStableProcessArgvReader} from './task12_macos_argv.js';
import {createMacProcessInventory, parseMacPsOutput} from './task12_process_ownership.js';
import {createCdpProfileScenarioHost} from './task12_cdp_profile_scenario_host.js';

const SETTINGS_URL = 'chrome://maho-settings/?pane=profiles';
const TASK11_RECEIPT = '.omo/evidence/profile-settings-architecture/task-11/final-result.json';
const TASK11_APP = 'chromium/src/out/Default/Maho.app/Contents/MacOS/Maho';
const TASK11_BROWSER_TESTS = 'chromium/src/out/Default/browser_tests';
const TASK11_GATES = [
  'focused_production_handler',
  'production_failure_filters',
  'profiles_e2e',
  'chromium_settings_e2e',
  'diagnostics',
  'mojo_drift',
  'fresh_linkage',
] as const;

interface Task11ArtifactIdentity {
  readonly path: string;
  readonly sha256: string;
  readonly size: number;
  readonly mtime_ms: number;
}

interface Task11FinalReceipt {
  readonly schema_version: 1;
  readonly status: 'PASS';
  readonly app: Task11ArtifactIdentity;
  readonly browser_tests: Task11ArtifactIdentity;
  readonly gates: {[K in typeof TASK11_GATES[number]]: 'PASS'};
}

export interface Task11ReceiptPrimitives {
  readText(path: string): Promise<string>;
  sha256(path: string): Promise<string>;
  stat(path: string): Promise<{size: number; mtimeMs: number}>;
}

export interface Task12LiveOptions {readonly app: string; readonly evidenceRoot: string; readonly runId?: string}
export interface Task12OwnedChild {readonly pid: number; readonly startToken: string; readonly exited: Promise<number>; close(): Promise<void>}
export interface Task12LiveOwnership {
  readonly root: string; readonly userDataDir: string; readonly rootPid: number; readonly rootStartToken: string;
  readonly targetIds: readonly string[]; readonly profiles?: Task12LiveProfiles; readonly child: Task12OwnedChild;
}
export interface Task12LiveQaHost {
  task11Passed(app: string): Promise<boolean>;
  appExists(app: string): Promise<boolean>;
  profileObservablesAvailable(
    targetId: string,
    connection?: {transport: CdpEventTransport},
  ): Promise<boolean>;
  cdpPortAvailable(): Promise<boolean>;
  relayAvailable(): Promise<boolean>;
  createTemporaryRoot(): Promise<{root: string; userDataDir: string}>;
  spawnApp(app: string, argv: readonly string[]): Promise<Task12OwnedChild>;
  awaitReadyAndNavigate(child: Task12OwnedChild, userDataDir: string): Promise<{targetId: string}>;
  provisionProfiles(targetId: string): Promise<Task12LiveProfiles>;
  runQa(options: Task12LiveOptions, profiles: Task12LiveProfiles): Promise<Task12DriverStatus>;
  cleanup(ownership: Task12LiveOwnership): Promise<void>;
  lifecyclePlanOptions?(app: string, temporary: {root: string; userDataDir: string}): Parameters<typeof buildOwnedLifecyclePlan>[0];
  lifecycleDependencies?: Parameters<typeof runOwnedReadinessLifecycle>[1];
  connectOwnedTarget?(targetId: string): Promise<{transport: CdpEventTransport; close(): void | Promise<void>}>;
  navigationTimer?: LifecycleTimer;
  createProfileScenarioHost?(connection: {transport: CdpEventTransport}): ProfileScenarioHost;
  finalCleanup?(
    ownership: Task12LiveOwnership,
    plan: Awaited<ReturnType<typeof buildOwnedLifecyclePlan>>,
    lifecycle: Awaited<ReturnType<typeof runOwnedReadinessLifecycle>>,
    profiles: ProfileScenarioResult | undefined,
    connection: {close(): void | Promise<void>} | undefined,
  ): {input: CleanupFinalBrowserStateInput; hooks: CleanupFinalBrowserStateHooks};
}

export type HardenedTask12LiveQaHost = Required<Pick<Task12LiveQaHost,
  'lifecyclePlanOptions' | 'lifecycleDependencies' | 'connectOwnedTarget' |
  'navigationTimer' | 'createProfileScenarioHost' | 'finalCleanup'>>;

export function buildTask12LaunchArguments(userDataDir: string): readonly string[] {
  return [`--user-data-dir=${userDataDir}`,'--remote-debugging-port=9222','--maho-disable-login-gate','--no-first-run','--no-default-browser-check',
    '--disable-background-networking','--disable-component-update','--enable-logging=stderr','--v=0',SETTINGS_URL];
}

async function portAvailable(port: number): Promise<boolean> {
  return new Promise(resolveResult => {
    const server = createServer();
    server.once('error', () => resolveResult(false));
    server.listen(port, '127.0.0.1', () => server.close(() => resolveResult(true)));
  });
}

function workspacePath(workspaceRoot: string, path: string): string | undefined {
  if (isAbsolute(path)) return undefined;
  const root = resolve(workspaceRoot);
  const candidate = resolve(root, path);
  const fromRoot = relative(root, candidate);
  return fromRoot !== '' && !fromRoot.startsWith('..') && !isAbsolute(fromRoot) ? candidate : undefined;
}

function artifactIdentity(value: unknown, exactPath: string): value is Task11ArtifactIdentity {
  if (!value || typeof value !== 'object') return false;
  const artifact = value as Record<string, unknown>;
  return artifact.path === exactPath && typeof artifact.sha256 === 'string' && /^[a-f0-9]{64}$/.test(artifact.sha256) &&
    typeof artifact.size === 'number' && Number.isSafeInteger(artifact.size) && artifact.size >= 0 &&
    typeof artifact.mtime_ms === 'number' && Number.isFinite(artifact.mtime_ms) && artifact.mtime_ms >= 0;
}

function finalReceipt(value: unknown): value is Task11FinalReceipt {
  if (!value || typeof value !== 'object') return false;
  const receipt = value as Record<string, unknown>;
  if (receipt.schema_version !== 1 || receipt.status !== 'PASS' ||
      !artifactIdentity(receipt.app, TASK11_APP) || !artifactIdentity(receipt.browser_tests, TASK11_BROWSER_TESTS) ||
      !receipt.gates || typeof receipt.gates !== 'object') return false;
  const gates = receipt.gates as Record<string, unknown>;
  return TASK11_GATES.every(gate => gates[gate] === 'PASS');
}

async function identityMatches(identity: Task11ArtifactIdentity, workspaceRoot: string, primitives: Task11ReceiptPrimitives): Promise<boolean> {
  const path = workspacePath(workspaceRoot, identity.path);
  if (!path) return false;
  const [actualHash, actualStat] = await Promise.all([primitives.sha256(path), primitives.stat(path)]);
  return actualHash === identity.sha256 && actualStat.size === identity.size && actualStat.mtimeMs === identity.mtime_ms;
}

export async function validateTask11Receipt(app: string, workspaceRoot: string, primitives: Task11ReceiptPrimitives): Promise<boolean> {
  try {
    const receiptPath = workspacePath(workspaceRoot, TASK11_RECEIPT);
    const expectedApp = workspacePath(workspaceRoot, TASK11_APP);
    if (!receiptPath || !expectedApp || resolve(app) !== expectedApp) return false;
    const receipt: unknown = JSON.parse(await primitives.readText(receiptPath));
    return finalReceipt(receipt) && await identityMatches(receipt.app, workspaceRoot, primitives) &&
      await identityMatches(receipt.browser_tests, workspaceRoot, primitives);
  } catch { return false; }
}

const defaultTask11ReceiptPrimitives: Task11ReceiptPrimitives = {
  readText: path => readFile(path, 'utf8'),
  sha256: async path => createHash('sha256').update(await readFile(path)).digest('hex'),
  stat: async path => { const value = await stat(path); return {size: value.size, mtimeMs: value.mtimeMs}; },
};

async function task11Passed(app: string): Promise<boolean> {
  return validateTask11Receipt(app, process.cwd(), defaultTask11ReceiptPrimitives);
}

async function targets(): Promise<Task12CdpTarget[]> {
  return await fetch(`${CDP_HOST}/json/list`, {signal: AbortSignal.timeout(20_000)}).then(response => response.json()) as Task12CdpTarget[];
}

async function connectTarget(targetId: string): Promise<{cdp: CDP; close(): void}> {
  const target = (await targets()).find(candidate => candidate.id === targetId);
  if (!target) throw new Error('Task 12 Settings target unavailable');
  const socket = new WebSocket(target.webSocketDebuggerUrl);
  await new Promise<void>((resolveOpen, reject) => { socket.onopen = () => resolveOpen(); socket.onerror = () => reject(new Error('CDP connection failed')); });
  return {cdp: new CDP(socket), close: () => socket.close()};
}

type ProfileObservablesProbeStatus = 'ready' | 'loading' | 'absent';

async function probeProfileObservablesStatus(
  transport: Pick<CdpEventTransport, 'send'>,
): Promise<ProfileObservablesProbeStatus> {
  try {
    const response = await transport.send('Runtime.evaluate', {
      expression: `(async function(){
        const store=window.settingsStore;
        if(!store||typeof store.getHandler!=='function')return 'loading';
        const handler=store.getHandler();
        if(!handler)return 'loading';
        if(typeof handler.getProfileObservablesSnapshot!=='function')return 'absent';
        const response=await handler.getProfileObservablesSnapshot();
        return (response!==null&&typeof response==='object'&&
            Object.hasOwn(response,'snapshot'))?'ready':'absent';
      })()`,
      awaitPromise: true,
      returnByValue: true,
    }) as {result?: {value?: unknown; result?: {value?: unknown}}};
    const value = response.result?.value ?? response.result?.result?.value;
    if (value === 'ready' || value === true) return 'ready';
    if (value === 'loading') return 'loading';
    return 'absent';
  } catch {
    // A transient evaluate failure while the SPA is still coming up is a loading state.
    return 'loading';
  }
}

async function profileObservablesAvailable(
  targetId: string,
  connection?: {transport: CdpEventTransport},
): Promise<boolean> {
  if (connection) return (await probeProfileObservablesStatus(connection.transport)) === 'ready';
  let ownedTarget: Awaited<ReturnType<typeof connectTarget>> | undefined;
  try {
    ownedTarget = await connectTarget(targetId);
    return (await probeProfileObservablesStatus(ownedTarget.cdp)) === 'ready';
  } catch {
    return false;
  } finally {
    ownedTarget?.close();
  }
}

export const defaultTask12LiveQaHost: Task12LiveQaHost = {
  task11Passed,
  appExists: async app => access(app).then(() => true, () => false),
  profileObservablesAvailable,
  cdpPortAvailable: () => portAvailable(9222),
  relayAvailable: async () => { try { return (await fetch(`${RELAY}/health`, {signal: AbortSignal.timeout(2_000)})).ok; } catch { return false; } },
  async createTemporaryRoot() {
    const root = await mkdtemp(join(tmpdir(), 'task12-live-'));
    const userDataDir = join(root, 'user-data');
    await mkdir(userDataDir, {mode: 0o700});
    return {root, userDataDir};
  },
  async spawnApp(app, argv) {
    const child = Bun.spawn([app, ...argv], {stdin: 'ignore', stdout: 'ignore', stderr: 'inherit'});
    return {pid: child.pid, startToken: String(child.pid), exited: child.exited,
      close: async () => { child.kill('SIGTERM'); await child.exited; }};
  },
  async awaitReadyAndNavigate() {
    const existing = await targets();
    const found = existing.find(candidate => candidate.url === SETTINGS_URL);
    if (found) return {targetId: found.id};
    const created = await fetch(`${CDP_HOST}/json/new?${SETTINGS_URL}`, {method: 'PUT', signal: AbortSignal.timeout(5_000)})
      .then(response => response.ok ? response.json() as Promise<Task12CdpTarget> : undefined);
    if (created && created.url === SETTINGS_URL) return {targetId: created.id};
    throw new Error('Task 12 Settings navigation did not complete');
  },
  async provisionProfiles(targetId) {
    const connection = await connectTarget(targetId);
    try {
      const result = await connection.cdp.eval<Task12LiveProfiles>(`(async function(){
        const handler=window.settingsStore.getHandler();const {profiles}=await handler.getProfiles();
        const host=profiles.find(profile=>profile.isActive)||profiles[0];if(!host)throw new Error('Host profile unavailable');
        const b=(await handler.createProfile('Task 12 B')).profile;
        const d=(await handler.createProfile('Task 12 disposable')).profile;
        if(!b||!d)throw new Error('Profile provisioning failed');
        return {hostProfileId:host.id,targetProfileId:b.id,disposableProfileId:d.id};})()`, true);
      if (!result) throw new Error('Profile provisioning returned no identities');
      return result;
    } finally { connection.close(); }
  },
  runQa: async (options, profiles) => runDefaultTask12Qa({
    inspectInfrastructure: async () => ({appFresh: true, cdpAvailable: true, relayAvailable: true, targets: await targets()}),
    connect: async target => {
      const socket = new WebSocket(target.webSocketDebuggerUrl);
      await new Promise<void>((resolveOpen, reject) => { socket.onopen = () => resolveOpen(); socket.onerror = () => reject(new Error('CDP connection failed')); });
      return {cdp: new CDP(socket), close: () => socket.close()};
    },
    evidenceRootDirectory: options.evidenceRoot,
    liveProfiles: profiles,
    scenarioRequests: {
      happy: {profileId: profiles.targetProfileId,name:'Task 12 Work QA',avatarColor:'#4F46E5',homepageUrl:'https://example.com/task12',searchEngineKeyword:'google.com',searchSuggestionsEnabled:true,downloadPrompt:true,archiveTimeoutHours:72},
      unknownTarget:{target:{profileId:'../task12-invalid',targetToken:'task12-invalid'},expectedMessage:'invalid'},
      deletingTarget:{target:{profileId:profiles.disposableProfileId,targetToken:'task12-deleting'},expectedMessage:'deleting'},
      staleTarget:{target:{profileId:profiles.targetProfileId,targetToken:'task12-stale'},expectedMessage:'stale'},
      sensitivePane:{profileId:profiles.targetProfileId,paneKey:'passwords'},
      lifecycle:{profileName:'Task 12 disposable',profileId:profiles.disposableProfileId},
    },
  }),
  async cleanup(ownership) { await ownership.child.close().catch(() => undefined); await rm(ownership.root, {recursive: true, force: true}); },
};
Object.defineProperty(defaultTask12LiveQaHost, 'createProfileScenarioHost', {
  value: (connection: {transport: CdpEventTransport}) =>
    createCdpProfileScenarioHost(connection.transport),
  enumerable: false,
});

export function createDefaultTask12LiveQaHost(
  hardened: HardenedTask12LiveQaHost,
): Task12LiveQaHost & HardenedTask12LiveQaHost {
  return {
    ...defaultTask12LiveQaHost,
    lifecyclePlanOptions: hardened.lifecyclePlanOptions,
    lifecycleDependencies: hardened.lifecycleDependencies,
    connectOwnedTarget: hardened.connectOwnedTarget,
    navigationTimer: hardened.navigationTimer,
    createProfileScenarioHost: hardened.createProfileScenarioHost,
    finalCleanup: hardened.finalCleanup,
  };
}

export async function runTask12LiveQa(options: Task12LiveOptions, host: Task12LiveQaHost = defaultTask12LiveQaHost): Promise<Task12DriverStatus> {
  if (!await host.task11Passed(options.app)) return {status:'blocked-current-app',reasons:['STALE_APP']};
  if (!await host.appExists(options.app)) return {status:'blocked-current-app',reasons:['STALE_APP']};
  if (host.lifecyclePlanOptions && host.lifecycleDependencies && host.connectOwnedTarget &&
      host.navigationTimer && host.createProfileScenarioHost && host.finalCleanup) {
    let ownership: Task12LiveOwnership | undefined;
    let plan: Awaited<ReturnType<typeof buildOwnedLifecyclePlan>> | undefined;
    let lifecycle: Awaited<ReturnType<typeof runOwnedReadinessLifecycle>> | undefined;
    let connection: {transport: CdpEventTransport; close(): void | Promise<void>} | undefined;
    let profiles: ProfileScenarioResult | undefined;
    let result: Task12DriverStatus;
    try {
      const temporary = await host.createTemporaryRoot();
      plan = await buildOwnedLifecyclePlan(host.lifecyclePlanOptions(options.app, temporary));
      lifecycle = await runOwnedReadinessLifecycle(plan, host.lifecycleDependencies);
      ownership = {
        ...temporary,
        rootPid: lifecycle.app.pid,
        rootStartToken: lifecycle.app.startToken,
        targetIds: [lifecycle.target.id],
        child: {
          pid: lifecycle.app.pid,
          startToken: lifecycle.app.startToken,
          exited: Promise.resolve(0),
          close: async () => undefined,
        },
      };
      connection = await host.connectOwnedTarget(lifecycle.target.id);
      await navigateOwnedSettingsTarget(connection.transport, host.navigationTimer);
      if (!await host.profileObservablesAvailable(lifecycle.target.id, connection)) {
        result = {
          status: 'missing-runtime-binding',
          reason: MISSING_RUNTIME_BINDING,
          missing: ['ProfileObservablesSnapshot'],
        };
      } else {
        if (!options.runId) throw new Error('Task 12 runId is required for live profile scenarios');
        profiles = await runProfileScenarios(host.createProfileScenarioHost(connection), {runId: options.runId});
        const liveProfiles: Task12LiveProfiles = {
          hostProfileId: profiles.ids.A,
          targetProfileId: profiles.ids.B,
          disposableProfileId: profiles.ids.disposable,
        };
        ownership = {...ownership, profiles: liveProfiles};
        result = await host.runQa(options, liveProfiles);
      }
    } catch (error) {
      result = {status:'failed',message:error instanceof Error?error.message:String(error)};
    }
    if (!ownership || !plan || !lifecycle) return result;
    const cleanup = host.finalCleanup(ownership, plan, lifecycle, profiles, connection);
    const cleanupReceipt = await cleanupFinalBrowserState(cleanup.input, cleanup.hooks);
    if (!cleanupReceipt.passed) return {
      status: 'failed',
      message: `Task 12 final cleanup failed: ${cleanupReceipt.errors.join('; ')}`,
    };
    return result;
  }
  if (!await host.cdpPortAvailable()) return {status:'blocked-current-app',reasons:['CDP_9222_UNAVAILABLE']};
  if (!await host.relayAvailable()) return {status:'blocked-current-app',reasons:['RELAY_18765_UNAVAILABLE']};
  let ownership: Task12LiveOwnership | undefined;
  try {
    const temporary=await host.createTemporaryRoot();
    const child=await host.spawnApp(options.app,buildTask12LaunchArguments(temporary.userDataDir));
    ownership={...temporary,rootPid:child.pid,rootStartToken:child.startToken,targetIds:[],child};
    const navigation=await host.awaitReadyAndNavigate(child,temporary.userDataDir);
    ownership={...ownership,targetIds:[navigation.targetId]};
    if (!await host.profileObservablesAvailable(navigation.targetId)) return {
      status: 'missing-runtime-binding',
      reason: MISSING_RUNTIME_BINDING,
      missing: ['ProfileObservablesSnapshot'],
    };
    const profiles=await host.provisionProfiles(navigation.targetId);
    ownership={...ownership,profiles};
    return await host.runQa(options,profiles);
  } catch(error) { return {status:'failed',message:error instanceof Error?error.message:String(error)}; }
  finally { if(ownership) await host.cleanup(ownership); }
}

function parseArgs(args: readonly string[]): Task12LiveOptions {
  const value=(flag:string)=>{const index=args.indexOf(flag);return index<0?undefined:args[index+1];};
  const app=value('--app'),evidenceRoot=value('--evidence-root'),runId=value('--run-id');
  if(!app||!evidenceRoot||!runId)throw new Error('Usage: task12_live_qa_entry.ts --app <path> --evidence-root <path> --run-id <id>');
  return {app:resolve(app),evidenceRoot:resolve(evidenceRoot),runId};
}

if(import.meta.main){let result:Task12DriverStatus;try{result=await runTask12LiveQa(parseArgs(process.argv.slice(2)));}
catch(error){result={status:'failed',message:error instanceof Error?error.message:String(error)};}
process.stdout.write(`${JSON.stringify(result)}\n`);process.exitCode=result.status==='passed'?0:1;}
