import {resolve} from 'node:path';

import {
  CDP,
  CDP_HOST,
  RELAY,
} from './cdp_harness.js';
import {
  MISSING_RUNTIME_BINDING,
  createTask12EvidenceFactory,
  createTask12StateCapture,
  runTask12Qa,
  type Task12BlockedReason,
  type Task12DriverStatus,
  type Task12RuntimeDependencies,
} from './qa_two_profile_settings.js';
import {
  Task12RuntimeScenarioExecutor,
  type Task12CurrentBrowserSpaceSnapshot,
  type Task12CurrentBrowserSpaceSnapshotCapability,
  type Task12RuntimeBrowserProxy,
  type Task12ScenarioRequests,
} from './task12_runtime_scenarios.js';
import type {
  CdpTransport,
  Task12ProfileObservablesSnapshotDto,
  Task12ProfileObservablesSnapshotReader,
} from './task12_runtime_evidence.js';
import type {Task12ExpectedChanges, Task12Snapshot} from './two_profile_qa_state.js';

const SETTINGS_URL_PREFIX = 'chrome://maho-settings/';
const PREFLIGHT_TIMEOUT_MS = 2_000;

export interface Task12CdpTarget {
  readonly id: string;
  readonly type: string;
  readonly url: string;
  readonly title: string;
  readonly webSocketDebuggerUrl: string;
}

export interface Task12InfrastructureSnapshot {
  readonly appFresh: boolean;
  readonly cdpAvailable: boolean;
  readonly relayAvailable: boolean;
  readonly targets: readonly Task12CdpTarget[];
}

export interface Task12LiveConnection {
  readonly cdp: CdpTransport & {eval<T = unknown>(expression: string, awaitPromise?: boolean): Promise<T | undefined>};
  close(): void | Promise<void>;
}

export interface Task12DefaultRuntimeHost {
  inspectInfrastructure(): Promise<Task12InfrastructureSnapshot>;
  connect(target: Task12CdpTarget): Promise<Task12LiveConnection>;
  readonly evidenceRootDirectory?: string;
  readonly scenarioRequests?: Task12ScenarioRequests;
  readonly expectedChanges?: NonNullable<Task12RuntimeDependencies['expectedChanges']>;
  readonly createEvidenceSession?: Task12RuntimeDependencies['createEvidenceSession'];
  readonly registerCleanupOwnership?: Task12RuntimeDependencies['registerCleanupOwnership'];
  readonly signalHooks?: Task12RuntimeDependencies['signalHooks'];
  readonly now?: () => string;
  readonly liveProfiles?: Task12LiveProfiles;
}

export interface Task12LiveProfiles {
  readonly hostProfileId: string;
  readonly targetProfileId: string;
  readonly disposableProfileId: string;
}

export type Task12DefaultRuntimeComposition =
  | {readonly status: 'blocked-current-app'; readonly reasons: readonly Task12BlockedReason[]}
  | {readonly status: 'missing-runtime-binding'; readonly reason: typeof MISSING_RUNTIME_BINDING; readonly missing: readonly string[]}
  | {
      readonly status: 'ready';
      readonly dependencies: Task12RuntimeDependencies;
      readonly browserProxy: Task12RuntimeBrowserProxy & Task12CurrentBrowserSpaceSnapshotCapability &
          Task12ProfileObservablesSnapshotReader;
      cleanup(): Promise<void>;
    };

function record(value: unknown): Record<string, unknown> | undefined {
  return value !== null && typeof value === 'object' ? value as Record<string, unknown> : undefined;
}

function targetFromUnknown(value: unknown): Task12CdpTarget | undefined {
  const candidate = record(value);
  if (typeof candidate?.id !== 'string' || typeof candidate.type !== 'string' ||
      typeof candidate.url !== 'string' || typeof candidate.title !== 'string' ||
      typeof candidate.webSocketDebuggerUrl !== 'string') {
    return undefined;
  }
  return {
    id: candidate.id,
    type: candidate.type,
    url: candidate.url,
    title: candidate.title,
    webSocketDebuggerUrl: candidate.webSocketDebuggerUrl,
  };
}

export function selectTask12SettingsTarget(targets: readonly Task12CdpTarget[]): Task12CdpTarget | undefined {
  return [...targets]
    .filter(target => target.type === 'page' && target.url.startsWith(SETTINGS_URL_PREFIX))
    .sort((left, right) => left.url.localeCompare(right.url) || left.id.localeCompare(right.id))[0];
}

async function fetchWithTimeout(url: string): Promise<Response | undefined> {
  try {
    return await fetch(url, {signal: AbortSignal.timeout(PREFLIGHT_TIMEOUT_MS)});
  } catch {
    return undefined;
  }
}

export async function inspectDefaultTask12Infrastructure(): Promise<Task12InfrastructureSnapshot> {
  const [targetsResponse, relayResponse] = await Promise.all([
    fetchWithTimeout(`${CDP_HOST}/json/list`),
    fetchWithTimeout(`${RELAY}/health`),
  ]);
  let targets: Task12CdpTarget[] = [];
  if (targetsResponse?.ok) {
    try {
      const values = await targetsResponse.json();
      if (Array.isArray(values)) targets = values.map(targetFromUnknown).filter(value => value !== undefined);
    } catch {
      targets = [];
    }
  }
  const relayAvailable = relayResponse?.ok === true && (await relayResponse.text()).trim() === 'ok';
  return {
    appFresh: selectTask12SettingsTarget(targets) !== undefined,
    cdpAvailable: targetsResponse?.ok === true,
    relayAvailable,
    targets,
  };
}

async function connectDefaultTask12Target(target: Task12CdpTarget): Promise<Task12LiveConnection> {
  const socket = new WebSocket(target.webSocketDebuggerUrl);
  await new Promise<void>((resolveOpen, rejectOpen) => {
    const onOpen = () => {
      socket.removeEventListener('error', onError);
      resolveOpen();
    };
    const onError = () => {
      socket.removeEventListener('open', onOpen);
      rejectOpen(new Error('Task12 Settings CDP WebSocket connection failed'));
    };
    socket.addEventListener('open', onOpen, {once: true});
    socket.addEventListener('error', onError, {once: true});
  });
  const cdp = new CDP(socket);
  await Promise.all([cdp.send('Runtime.enable'), cdp.send('Page.enable')]);
  const frameTreeReply = record(await cdp.send('Page.getFrameTree'));
  const frameTree = record(record(frameTreeReply?.result)?.frameTree);
  const rootFrame = record(frameTree?.frame);
  if (typeof rootFrame?.url !== 'string' || !rootFrame.url.startsWith(SETTINGS_URL_PREFIX)) {
    socket.close();
    throw new Error('Connected CDP target is not the deterministic Maho Settings root frame');
  }
  return {cdp, close: () => socket.close()};
}

function callHandlerExpression(method: string, args: readonly unknown[]): string {
  return `(async function(){
    const store = window.settingsStore;
    if (!store || typeof store.getHandler !== 'function') throw new Error('Settings store handler is unavailable');
    const handler = store.getHandler();
    if (!handler || typeof handler[${JSON.stringify(method)}] !== 'function') throw new Error(${JSON.stringify(`${method} is unavailable`)});
    return await handler[${JSON.stringify(method)}](...${JSON.stringify(args)});
  })()`;
}

class Task12CdpBrowserProxy implements Task12RuntimeBrowserProxy,
    Task12CurrentBrowserSpaceSnapshotCapability, Task12ProfileObservablesSnapshotReader {
  constructor(private readonly cdp: Task12LiveConnection['cdp']) {}

  private async call<T>(method: string, args: readonly unknown[] = []): Promise<T> {
    const result = await this.cdp.eval<T>(callHandlerExpression(method, args), true);
    if (result === undefined) throw new Error(`${method} returned no by-value result`);
    return result;
  }

  getProfiles(): ReturnType<Task12RuntimeBrowserProxy['getProfiles']> {
    return this.call('getProfiles');
  }

  async getProfileObservablesSnapshot(): Promise<{
    snapshot: Task12ProfileObservablesSnapshotDto | null;
  }> {
    const expression = `(async function(){
      const fail = message => { throw new TypeError('Malformed profile observables snapshot: ' + message); };
      const isRecord = value => value !== null && typeof value === 'object' && !Array.isArray(value);
      const isNullableId = value => value === null || (typeof value === 'string' && value.length > 0);
      const store = window.settingsStore;
      if (!store || typeof store.getHandler !== 'function') throw new Error('Settings store handler is unavailable');
      const handler = store.getHandler();
      if (!handler || typeof handler.getProfileObservablesSnapshot !== 'function') {
        throw new Error('getProfileObservablesSnapshot is unavailable');
      }
      const response = await handler.getProfileObservablesSnapshot();
      if (!isRecord(response) || !Object.hasOwn(response, 'snapshot')) fail('response');
      if (response.snapshot === null) return {snapshot:null};
      const snapshot = response.snapshot;
      if (!isRecord(snapshot)) fail('snapshot');
      if (!isNullableId(snapshot.activeBrowserProfileId)) fail('activeBrowserProfileId');
      if (!isNullableId(snapshot.activeMahoProfileId)) fail('activeMahoProfileId');
      if (typeof snapshot.registryRevision !== 'bigint' || snapshot.registryRevision < 0n) fail('registryRevision');
      if (!Array.isArray(snapshot.lifecycleEntries)) fail('lifecycleEntries');
      const lifecycleEntries = snapshot.lifecycleEntries.map((entry, index) => {
        if (!isRecord(entry) || typeof entry.profileId !== 'string' || entry.profileId.length === 0 ||
            !Number.isInteger(entry.lifecycleState) || entry.lifecycleState < 0 || entry.lifecycleState > 3) {
          fail('lifecycleEntries[' + index + ']');
        }
        return {profileId:entry.profileId,lifecycleState:entry.lifecycleState};
      });
      if (!Array.isArray(snapshot.pendingDeletionProfileIds) ||
          !snapshot.pendingDeletionProfileIds.every(id => typeof id === 'string' && id.length > 0)) {
        fail('pendingDeletionProfileIds');
      }
      if (!Number.isInteger(snapshot.deletedHistoryAvailability) || snapshot.deletedHistoryAvailability !== 0) {
        fail('deletedHistoryAvailability');
      }
      return {snapshot:{
        activeBrowserProfileId:snapshot.activeBrowserProfileId,
        activeMahoProfileId:snapshot.activeMahoProfileId,
        registryRevision:snapshot.registryRevision.toString(),
        lifecycleEntries,
        pendingDeletionProfileIds:[...snapshot.pendingDeletionProfileIds],
        deletedHistoryAvailability:snapshot.deletedHistoryAvailability,
      }};
    })()`;
    const result = await this.cdp.eval<{
      snapshot: Task12ProfileObservablesSnapshotDto | null;
    }>(expression, true);
    if (result === undefined)
      throw new Error('getProfileObservablesSnapshot returned no by-value result');
    return result;
  }

  createProfile(name: string): ReturnType<Task12RuntimeBrowserProxy['createProfile']> {
    return this.call('createProfile', [name]);
  }

  deleteProfile(profileId: string): ReturnType<Task12RuntimeBrowserProxy['deleteProfile']> {
    return this.call('deleteProfile', [profileId]);
  }

  getGlobalSettingsSnapshot(): ReturnType<Task12RuntimeBrowserProxy['getGlobalSettingsSnapshot']> {
    return this.call('getGlobalSettingsSnapshot');
  }

  getSpaces(): ReturnType<Task12RuntimeBrowserProxy['getSpaces']> {
    return this.call('getSpaces');
  }

  getCurrentBrowserSpaceSnapshot(): Promise<{snapshot: Task12CurrentBrowserSpaceSnapshot | null}> {
    return this.call('getCurrentBrowserSpaceSnapshot');
  }
}

const attachOnlyStateExpressions: NonNullable<Task12RuntimeDependencies['stateExpressions']> = {
  profileA: 'null',
  profileB: 'null',
  globalState: 'null',
};

function liveProfileExpression(profileId: string): string {
  return `(async function(){
    const handler = window.settingsStore.getHandler();
    const requested = {profileId:${JSON.stringify(profileId)},targetToken:''};
    const context = (await handler.getSelectedProfileContext(requested)).result.context;
    if (!context) throw new Error('Profile context unavailable');
    const target = {profileId:context.profileId,targetToken:context.targetToken};
    const [metadata,search,download,archive] = await Promise.all([
      handler.getSelectedProfileMetadata(target),
      handler.getSelectedProfileSearchSettings(target),handler.getSelectedProfileDownloadSettings(target),
      handler.getSelectedProfileArchiveSettings(target)]);
    const m=metadata.result.metadata,s=search.result.search;
    const d=download.result.download,a=archive.result.archive;
    if(!m||!s||!d||!a) throw new Error('Profile snapshots unavailable');
    return {id:${JSON.stringify(profileId)},name:m.name,avatarColor:m.avatarColor,
      searchEngineId:(s.engines.find(engine=>engine.isDefault)||{}).keyword||'',
      searchSuggestionsEnabled:s.suggestionsEnabled,downloadPrompt:d.promptForDownload,
      downloadDirectoryToken:d.directoryDisplayPath,archiveTimeoutHours:a.timeoutHours};
  })()`;
}

export function createTask12LiveStateExpressions(profiles: Task12LiveProfiles): NonNullable<Task12RuntimeDependencies['stateExpressions']> {
  return {
    profileA: liveProfileExpression(profiles.hostProfileId),
    profileB: liveProfileExpression(profiles.targetProfileId),
    globalState: `(async function(){const {settings}=await window.settingsStore.getHandler().getGlobalSettingsSnapshot();const groups={account:{},process:{},coreGlobal:{}};for(const item of settings){const group=item.scope===3?'account':item.scope===1||item.scope===2?'process':'coreGlobal';groups[group][item.key]=item.value;}return groups;})()`,
  };
}

function expectedFromRequest(
  requests: Task12ScenarioRequests,
  scenario: Parameters<NonNullable<Task12RuntimeDependencies['expectedChanges']>>[0],
  before: Task12Snapshot,
): Task12ExpectedChanges {
  if (scenario !== 'happy-b-only-mutation' || before.profiles.B.status !== 'observed') {
    return {scenario, profileB: {}, allowedCleanup: []};
  }
  const value = before.profiles.B.value;
  return {
    scenario,
    profileB: {
      name: {before: value.name, after: requests.happy.name},
      avatarColor: {before: value.avatarColor, after: requests.happy.avatarColor},
      homepageUrl: {before: value.homepageUrl, after: requests.happy.homepageUrl},
      searchEngineId: {before: value.searchEngineId, after: requests.happy.searchEngineKeyword},
      searchSuggestionsEnabled: {before: value.searchSuggestionsEnabled, after: requests.happy.searchSuggestionsEnabled},
      downloadPrompt: {before: value.downloadPrompt, after: requests.happy.downloadPrompt},
      archiveTimeoutHours: {before: value.archiveTimeoutHours, after: requests.happy.archiveTimeoutHours},
    },
    allowedCleanup: [],
  };
}

function defaultScenarioRequestsFromEnvironment(): Task12ScenarioRequests | undefined {
  const profileId = process.env.MAHO_TASK12_PROFILE_B_ID;
  if (!profileId) return undefined;
  const disposableId = process.env.MAHO_TASK12_DISPOSABLE_PROFILE_ID;
  return {
    happy: {
      profileId,
      name: process.env.MAHO_TASK12_PROFILE_B_NAME ?? 'Task 12 Work QA',
      avatarColor: process.env.MAHO_TASK12_PROFILE_B_AVATAR_COLOR ?? '#4F46E5',
      homepageUrl: process.env.MAHO_TASK12_PROFILE_B_HOMEPAGE ?? 'https://example.com/task12',
      searchEngineKeyword: process.env.MAHO_TASK12_PROFILE_B_SEARCH_KEYWORD ?? 'google.com',
      searchSuggestionsEnabled: true,
      downloadPrompt: true,
      archiveTimeoutHours: 72,
    },
    unknownTarget: {target: {profileId: 'task12-missing-profile', targetToken: 'task12-missing'}, expectedMessage: 'unavailable'},
    deletingTarget: {target: {profileId: 'task12-deleting-profile', targetToken: 'task12-deleting'}, expectedMessage: 'deleting'},
    staleTarget: {target: {profileId, targetToken: 'task12-stale'}, expectedMessage: 'stale'},
    sensitivePane: {profileId, paneKey: 'passwords'},
    lifecycle: {profileName: process.env.MAHO_TASK12_DISPOSABLE_PROFILE_NAME ?? 'Task 12 disposable', ...(disposableId ? {profileId: disposableId} : {})},
  };
}

export const defaultTask12RuntimeHost: Task12DefaultRuntimeHost = {
  inspectInfrastructure: inspectDefaultTask12Infrastructure,
  connect: connectDefaultTask12Target,
  evidenceRootDirectory: resolve(process.env.MAHO_TASK12_EVIDENCE_ROOT ?? '.omo/evidence/profile-settings-architecture/task-12/runtime'),
  scenarioRequests: defaultScenarioRequestsFromEnvironment(),
};

export async function createDefaultTask12Runtime(
  host: Task12DefaultRuntimeHost = defaultTask12RuntimeHost,
): Promise<Task12DefaultRuntimeComposition> {
  const infrastructure = await host.inspectInfrastructure();
  const reasons: Task12BlockedReason[] = [];
  if (!infrastructure.appFresh) reasons.push('STALE_APP');
  if (!infrastructure.cdpAvailable) reasons.push('CDP_9222_UNAVAILABLE');
  if (!infrastructure.relayAvailable) reasons.push('RELAY_18765_UNAVAILABLE');
  if (reasons.length > 0) return {status: 'blocked-current-app', reasons};

  const target = selectTask12SettingsTarget(infrastructure.targets);
  if (!target) return {status: 'blocked-current-app', reasons: ['STALE_APP']};

  let connection: Task12LiveConnection;
  try {
    connection = await host.connect(target);
  } catch {
    return {status: 'missing-runtime-binding', reason: MISSING_RUNTIME_BINDING, missing: ['Settings CDP target/frame']};
  }
  let closed = false;
  const cleanup = async () => {
    if (closed) return;
    closed = true;
    await connection.close();
  };

  const browserProxy = new Task12CdpBrowserProxy(connection.cdp);
  const executor = new Task12RuntimeScenarioExecutor(connection.cdp, {
    captureBefore: () => undefined,
    captureAfter: () => undefined,
  });
  const availability = await executor.availability();
  if (availability.status !== 'available') {
    await cleanup();
    return availability;
  }

  const scenarioRequests = host.scenarioRequests;
  const evidenceRootDirectory = host.evidenceRootDirectory;
  const createEvidenceSession = host.createEvidenceSession ?? (evidenceRootDirectory
    ? createTask12EvidenceFactory({rootDirectory: evidenceRootDirectory})
    : undefined);
  const missing: string[] = [];
  if (!scenarioRequests) missing.push('Task12 scenario requests');
  if (!createEvidenceSession) missing.push('Task12 evidence root');
  if (missing.length > 0 || !scenarioRequests || !createEvidenceSession) {
    await cleanup();
    return {status: 'missing-runtime-binding', reason: MISSING_RUNTIME_BINDING, missing};
  }

  const stateExpressions = host.liveProfiles
    ? createTask12LiveStateExpressions(host.liveProfiles)
    : attachOnlyStateExpressions;
  const dependencies: Task12RuntimeDependencies = {
    probe: {
      appFresh: () => true,
      cdpAvailable: () => true,
      relayAvailable: () => true,
      runtimeBindingAvailable: () => true,
    },
    cdp: connection.cdp,
    runtimeScenarioExecutor: executor,
    scenarioRequests,
    stateExpressions,
    currentBrowserSpaceSnapshotReader: browserProxy,
    profileObservablesSnapshotReader: browserProxy,
    createEvidenceSession,
    captureState: createTask12StateCapture(
      connection.cdp,
      stateExpressions,
      browserProxy,
      host.now,
      browserProxy,
    ),
    expectedChanges: host.expectedChanges ?? ((scenario, before) => expectedFromRequest(scenarioRequests, scenario, before)),
    registerCleanupOwnership: host.registerCleanupOwnership,
    signalHooks: host.signalHooks,
    now: host.now,
  };
  return {status: 'ready', dependencies, browserProxy, cleanup};
}

export async function runDefaultTask12Qa(
  host: Task12DefaultRuntimeHost = defaultTask12RuntimeHost,
  run: (dependencies: Task12RuntimeDependencies) => Promise<Task12DriverStatus> = runTask12Qa,
): Promise<Task12DriverStatus> {
  const composition = await createDefaultTask12Runtime(host);
  if (composition.status !== 'ready') return composition;
  try {
    return await run(composition.dependencies);
  } finally {
    await composition.cleanup();
  }
}
