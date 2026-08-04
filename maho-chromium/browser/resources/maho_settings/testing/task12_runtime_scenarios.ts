import type {
  ProfileArchiveSettings,
  ProfileDownloadSettings,
  ProfileInfo,
  ProfileMetadata,
  ProfileSearchSettings,
  ProfileTarget,
  SelectedProfileContext,
} from '../maho_settings.mojom-webui.js';
import type {SettingsState} from '../react/store.js';
import {MISSING_RUNTIME_BINDING} from './qa_two_profile_settings.js';
import {waitForFrameUrl} from './cdp_harness.js';
import type {CDP} from './cdp_harness.js';
import type {Task12DriverScenarioId} from './task12_static_contract_fixture.js';

export {MISSING_RUNTIME_BINDING};

export const TASK12_RUNTIME_TIMEOUT_MS = 10_000;

export type RuntimeBindingFailure = {
  readonly status: 'missing-runtime-binding';
  readonly reason: typeof MISSING_RUNTIME_BINDING;
  readonly missing: readonly string[];
};

export type RuntimeBindingAvailability =
  | RuntimeBindingFailure
  | {readonly status: 'available'};

export type ObservableSeam =
  | {readonly status: 'available'; readonly source: string}
  | {readonly status: 'unavailable'; readonly reason: string};

export interface Task12ObservableSeamReport {
  readonly activeMahoProfile: ObservableSeam;
  readonly focusedWindow: ObservableSeam;
  readonly selectedSpace: ObservableSeam;
  readonly spaceAssignments: ObservableSeam;
  readonly backendGlobalValues: ObservableSeam;
  readonly fullPassageAvailable: boolean;
}

export interface Task12RuntimeState {
  readonly selectedProfileTarget: ProfileTarget | null;
  readonly selectedProfileContext: SelectedProfileContext | null;
  readonly selectedProfileMetadata: ProfileMetadata | null;
  readonly selectedProfileSearch: ProfileSearchSettings | null;
  readonly selectedProfileDownload: ProfileDownloadSettings | null;
  readonly selectedProfileArchive: ProfileArchiveSettings | null;
  readonly selectedProfileLoading: boolean;
  readonly selectedProfileError: string | null;
  readonly selectedProfileMutationError: {readonly code: number; readonly message: string} | null;
  readonly currentPaneKey: string;
}

export interface Task12RuntimeStore {
  getSnapshot(): Task12RuntimeState;
  subscribe(listener: () => void): () => void;
  getHandler(): Task12RuntimeBrowserProxy;
  selectPane(key: string): void;
  selectProfileFromCatalog(profileId: string): Promise<void>;
  selectProfileTarget(target: ProfileTarget): Promise<void>;
  updateSelectedProfileMetadata(update: {name: string; avatarColor: string}): Promise<boolean>;
  setSelectedProfileDefaultSearchEngine(keyword: string): Promise<boolean>;
  setSelectedProfileSearchSuggestionsEnabled(enabled: boolean): Promise<boolean>;
  setSelectedProfileDownloadPrompt(enabled: boolean): Promise<boolean>;
  setSelectedProfileArchiveTimeout(timeoutHours: number): Promise<boolean>;
}

export interface Task12CurrentBrowserSpaceSnapshot {
  readonly focusedBrowserSessionId: number;
  readonly selectedSpace: {readonly id: string; readonly name: string};
  readonly assignedTabIds: readonly string[];
  readonly assignedWindowIds: readonly string[];
}

export interface Task12CurrentBrowserSpaceSnapshotCapability {
  getCurrentBrowserSpaceSnapshot(): Promise<{snapshot: Task12CurrentBrowserSpaceSnapshot | null}>;
}

export interface Task12RuntimeBrowserProxy {
  getProfiles(): Promise<{profiles: ProfileInfo[]}>;
  createProfile(name: string): Promise<{profile?: ProfileInfo | null}>;
  deleteProfile(profileId: string): Promise<{success: boolean}>;
  getGlobalSettingsSnapshot(): Promise<{settings: readonly unknown[]}>;
  getSpaces(): Promise<{spaces: readonly unknown[]}>;
}

export interface Task12CdpRuntime {
  eval<T = unknown>(expression: string, awaitPromise?: boolean): Promise<T | undefined>;
}

export interface ExactSignalSource<T> {
  snapshot(): T;
  subscribe(listener: () => void): () => void;
}

export interface ExactMutationSource {
  observe(listener: () => void): () => void;
}

export type ExactSignalTimer = number | ReturnType<typeof setTimeout>;

export type ExactSignalSetTimer = (
  callback: () => void,
  timeoutMs: number,
) => ExactSignalTimer;

export type ExactSignalClearTimer = (timer: ExactSignalTimer) => void;

export interface ExactSignalOptions<T, R> {
  readonly source: ExactSignalSource<T>;
  readonly mutationSource?: ExactMutationSource;
  readonly matches: (snapshot: T) => boolean;
  readonly action: () => Promise<R> | R;
  readonly timeoutMs?: number;
  readonly setTimer?: ExactSignalSetTimer;
  readonly clearTimer?: ExactSignalClearTimer;
}

/**
 * Arms both exact notification sources before invoking the action. There is no
 * polling path: completion can only come from the subscribed store signal or
 * the registered mutation observer. Cleanup is deterministic and stale
 * callbacks cannot settle the already-completed wait.
 */
export async function executeAfterExactSignal<T, R>(options: ExactSignalOptions<T, R>): Promise<R> {
  const timeoutMs = options.timeoutMs ?? TASK12_RUNTIME_TIMEOUT_MS;
  if (!Number.isFinite(timeoutMs) || timeoutMs <= 0) {
    throw new Error(`Invalid exact-signal timeout: ${timeoutMs}`);
  }

  const schedule: ExactSignalSetTimer = options.setTimer ??
    ((callback, delay) => setTimeout(callback, delay));
  const cancel: ExactSignalClearTimer = options.clearTimer ??
    (timer => clearTimeout(timer));
  let active = true;
  let resolveSignal!: () => void;
  let rejectSignal!: (error: Error) => void;
  const signal = new Promise<void>((resolve, reject) => {
    resolveSignal = resolve;
    rejectSignal = reject;
  });
  const inspect = () => {
    if (active && options.matches(options.source.snapshot())) resolveSignal();
  };

  // Registration order is contractual: store, DOM, timeout, then action.
  const unsubscribe = options.source.subscribe(inspect);
  const disconnectMutation = options.mutationSource?.observe(inspect) ?? (() => {});
  const timeout = schedule(() => rejectSignal(new Error('Timed out waiting for exact Task 12 signal')), timeoutMs);

  try {
    const resultPromise = Promise.resolve().then(options.action);
    inspect();
    const result = await resultPromise;
    await signal;
    return result;
  } finally {
    active = false;
    cancel(timeout);
    disconnectMutation();
    unsubscribe();
  }
}

export interface Task12CaptureHooks<TCapture = unknown> {
  captureBefore(scenario: Task12DriverScenarioId): Promise<TCapture> | TCapture;
  captureAfter(scenario: Task12DriverScenarioId): Promise<TCapture> | TCapture;
  assertNoWrites?(before: TCapture, after: TCapture, scenario: Task12DriverScenarioId): Promise<void> | void;
  captureHostPayloadCount?(): Promise<number> | number;
  cleanup?(scenario: Task12DriverScenarioId): Promise<void> | void;
}

export type Task12ScenarioEventSink = (event: {
  readonly kind: 'profile-action-armed' | 'profile-action-complete';
  readonly scenario: Task12DriverScenarioId;
}) => Promise<void> | void;

export type Task12ScenarioFailureReason =
  | 'unknown-target'
  | 'deleting-target'
  | 'stale-target'
  | 'unsupported-sensitive-pane';

export type Task12ScenarioResult<TCapture = unknown> =
  | {
      readonly status: 'passed';
      readonly scenario: Task12DriverScenarioId;
      readonly before: TCapture;
      readonly after: TCapture;
    }
  | RuntimeBindingFailure;

export interface HappyMutationRequest {
  readonly profileId: string;
  readonly name: string;
  readonly avatarColor: string;
  readonly homepageUrl: string;
  readonly searchEngineKeyword: string;
  readonly searchSuggestionsEnabled: boolean;
  readonly downloadPrompt: boolean;
  readonly archiveTimeoutHours: number;
}

export interface FailureTargetRequest {
  readonly target: ProfileTarget;
  readonly expectedMessage: string;
}

export interface SensitivePaneRequest {
  readonly profileId: string;
  readonly paneKey: string;
}

export interface LifecycleRequest {
  readonly profileName: string;
  readonly profileId?: string;
}

export interface Task12ScenarioRequests {
  readonly happy: HappyMutationRequest;
  readonly unknownTarget: FailureTargetRequest;
  readonly deletingTarget: FailureTargetRequest;
  readonly staleTarget: FailureTargetRequest;
  readonly sensitivePane: SensitivePaneRequest;
  readonly lifecycle: LifecycleRequest;
}

const REQUIRED_STORE_METHODS = [
  'getSnapshot',
  'subscribe',
  'getHandler',
  'selectPane',
  'selectProfileFromCatalog',
  'selectProfileTarget',
  'updateSelectedProfileMetadata',
  'setSelectedProfileDefaultSearchEngine',
  'setSelectedProfileSearchSuggestionsEnabled',
  'setSelectedProfileDownloadPrompt',
  'setSelectedProfileArchiveTimeout',
] as const;

const REQUIRED_PROXY_METHODS = [
  'getProfiles',
  'createProfile',
  'deleteProfile',
  'getGlobalSettingsSnapshot',
  'getSpaces',
  'getCurrentBrowserSpaceSnapshot',
  'getProfileObservablesSnapshot',
] as const;

function quoted(value: unknown): string {
  return JSON.stringify(value);
}

function bindingProbeExpression(): string {
  return `(function(){
    const missing = [];
    const store = window.settingsStore;
    if (!store) return {status:'missing-runtime-binding', reason:'${MISSING_RUNTIME_BINDING}', missing:['window.settingsStore']};
    for (const name of ${quoted(REQUIRED_STORE_METHODS)}) if (typeof store[name] !== 'function') missing.push('settingsStore.' + name);
    let handler = null;
    if (typeof store.getHandler === 'function') {
      try { handler = store.getHandler(); } catch (_) { missing.push('settingsStore.getHandler()'); }
    }
    for (const name of ${quoted(REQUIRED_PROXY_METHODS)}) if (!handler || typeof handler[name] !== 'function') missing.push('browserProxy.' + name);
    if (typeof MutationObserver !== 'function') missing.push('window.MutationObserver');
    return missing.length ? {status:'missing-runtime-binding', reason:'${MISSING_RUNTIME_BINDING}', missing} : {status:'available'};
  })()`;
}

export async function inspectTask12RuntimeBinding(cdp: Task12CdpRuntime): Promise<RuntimeBindingAvailability> {
  return await cdp.eval<RuntimeBindingAvailability>(bindingProbeExpression()) ?? {
    status: 'missing-runtime-binding',
    reason: MISSING_RUNTIME_BINDING,
    missing: ['Runtime.evaluate result'],
  };
}

export async function inspectTask12ObservableSeams(cdp: Task12CdpRuntime): Promise<Task12ObservableSeamReport> {
  const report = await cdp.eval<Task12ObservableSeamReport>(`(function(){
    const store = window.settingsStore;
    const handler = store && typeof store.getHandler === 'function' ? store.getHandler() : null;
    const snapshot = store && typeof store.getSnapshot === 'function' ? store.getSnapshot() : null;
    const activeMahoProfile = snapshot && snapshot.selectedProfileContext &&
        typeof snapshot.selectedProfileContext.isActiveMahoProfile === 'boolean'
      ? {status:'available', source:'settingsStore.selectedProfileContext.isActiveMahoProfile'}
      : {status:'unavailable', reason:'No selected-profile context exposes the active Maho profile yet.'};
    const atomicSpaceSnapshot = handler && typeof handler.getCurrentBrowserSpaceSnapshot === 'function'
      ? {status:'available', source:'browserProxy.getCurrentBrowserSpaceSnapshot'}
      : {status:'unavailable', reason:handler && typeof handler.getSpaces === 'function'
          ? 'getSpaces exposes the catalog, not focus, selection, or assignments.'
          : 'Settings has no current-browser Space snapshot seam.'};
    const focusedWindow = atomicSpaceSnapshot;
    const selectedSpace = atomicSpaceSnapshot;
    const spaceAssignments = atomicSpaceSnapshot;
    const backendGlobalValues = handler && typeof handler.getGlobalSettingsSnapshot === 'function'
      ? {status:'available', source:'browserProxy.getGlobalSettingsSnapshot'}
      : {status:'unavailable', reason:'Settings has no backend global snapshot method.'};
    const seams = {activeMahoProfile, focusedWindow, selectedSpace, spaceAssignments, backendGlobalValues};
    return {...seams, fullPassageAvailable:Object.values(seams).every(value => value.status === 'available')};
  })()`);
  return report ?? {
    activeMahoProfile: {status: 'unavailable', reason: 'Runtime.evaluate returned no seam report.'},
    focusedWindow: {status: 'unavailable', reason: 'Runtime.evaluate returned no seam report.'},
    selectedSpace: {status: 'unavailable', reason: 'Runtime.evaluate returned no seam report.'},
    spaceAssignments: {status: 'unavailable', reason: 'Runtime.evaluate returned no seam report.'},
    backendGlobalValues: {status: 'unavailable', reason: 'Runtime.evaluate returned no seam report.'},
    fullPassageAvailable: false,
  };
}

interface InPageObservedAction {
  readonly action: string;
  readonly statePredicate: string;
  readonly domPredicate?: string;
  readonly timeoutMs?: number;
}

function observedActionExpression(spec: InPageObservedAction): string {
  const timeoutMs = spec.timeoutMs ?? TASK12_RUNTIME_TIMEOUT_MS;
  return `(async function(){
    const store = window.settingsStore;
    const statePredicate = function(state){ return !!(${spec.statePredicate}); };
    const domPredicate = function(){ return !!(${spec.domPredicate ?? 'false'}); };
    let active = true;
    let resolveSignal;
    let rejectSignal;
    const signal = new Promise((resolve, reject) => { resolveSignal = resolve; rejectSignal = reject; });
    const inspect = () => {
      if (!active) return;
      try { if (statePredicate(store.getSnapshot()) && domPredicate()) resolveSignal(); }
      catch (error) { rejectSignal(error); }
    };
    const unsubscribe = store.subscribe(inspect);
    const observer = new MutationObserver(inspect);
    observer.observe(document.documentElement, {attributes:true, childList:true, characterData:true, subtree:true});
    const timeout = setTimeout(() => rejectSignal(new Error('Timed out waiting for exact Task 12 signal')), ${timeoutMs});
    try {
      const result = await (${spec.action});
      inspect();
      await signal;
      return {ok:true, result};
    } finally {
      active = false;
      clearTimeout(timeout);
      observer.disconnect();
      unsubscribe();
    }
  })()`;
}

async function runInPageObservedAction(
  cdp: Task12CdpRuntime,
  spec: InPageObservedAction,
): Promise<unknown> {
  const result = await cdp.eval<{ok: boolean; result: unknown}>(observedActionExpression(spec), true);
  if (!result?.ok) throw new Error('Task 12 observed action returned no result');
  return result.result;
}

function selectedTargetPredicate(profileId: string): string {
  return `state.selectedProfileTarget?.profileId === ${quoted(profileId)} &&
    state.selectedProfileContext?.profileId === ${quoted(profileId)} &&
    state.selectedProfileLoading === false`;
}

function contextGuard(profileId: string, minimumContextRevision: string, minimumProfileRevision: string): string {
  // A data mutation on the selected profile advances profileRevision while the selected
  // context (which profile is selected) is unchanged, so contextRevision stays put. Require
  // the context to remain the SAME target (profileId) and not regress (>=), and require the
  // profile data to actually advance (profileRevision strictly greater). Using '>' on
  // contextRevision here is impossible for a pure data mutation and never resolves.
  return `state.selectedProfileTarget?.profileId === ${quoted(profileId)} &&
    state.selectedProfileContext?.profileId === ${quoted(profileId)} &&
    state.selectedProfileContext.contextRevision >= ${minimumContextRevision} &&
    state.selectedProfileContext.profileRevision > ${minimumProfileRevision}`;
}

export class Task12RuntimeScenarioExecutor<TCapture = unknown> {
  constructor(
    private readonly cdp: Task12CdpRuntime,
    private readonly hooks: Task12CaptureHooks<TCapture>,
  ) {}

  async availability(): Promise<RuntimeBindingAvailability> {
    return inspectTask12RuntimeBinding(this.cdp);
  }

  async observableSeams(): Promise<Task12ObservableSeamReport> {
    return inspectTask12ObservableSeams(this.cdp);
  }

  async execute(
    scenario: Task12DriverScenarioId,
    requests: Task12ScenarioRequests,
    emitEvent: Task12ScenarioEventSink = () => undefined,
  ): Promise<Task12ScenarioResult<TCapture>> {
    const availability = await this.availability();
    if (availability.status !== 'available') return availability;

    const before = await this.hooks.captureBefore(scenario);
    let requiresZeroWrites = false;
    try {
      await emitEvent({kind: 'profile-action-armed', scenario});
      switch (scenario) {
        case 'happy-b-only-mutation':
          await this.happyMutation(requests.happy);
          break;
        case 'failure-unknown-target':
          requiresZeroWrites = true;
          await this.failureTarget(requests.unknownTarget);
          break;
        case 'failure-deleting-target':
          requiresZeroWrites = true;
          await this.failureTarget(requests.deletingTarget);
          break;
        case 'failure-stale-target':
          requiresZeroWrites = true;
          await this.failureTarget(requests.staleTarget);
          break;
        case 'failure-unsupported-sensitive-pane':
          requiresZeroWrites = true;
          await this.sensitivePane(requests.sensitivePane);
          break;
        case 'lifecycle-create':
          await this.createProfile(requests.lifecycle.profileName);
          break;
        case 'lifecycle-safe-delete-cancel':
          requiresZeroWrites = true;
          await this.safeDeleteCancel(requests.lifecycle.profileId ?? requests.happy.profileId);
          break;
        case 'cleanup':
          await this.cleanupProfile(requests.lifecycle.profileId ?? requests.happy.profileId);
          break;
      }
      await emitEvent({kind: 'profile-action-complete', scenario});
      const after = await this.hooks.captureAfter(scenario);
      if (requiresZeroWrites) await this.hooks.assertNoWrites?.(before, after, scenario);
      return {status: 'passed', scenario, before, after};
    } finally {
      await this.hooks.cleanup?.(scenario);
    }
  }

  async navigateChromiumSettings(cdp: CDP, trigger: () => Promise<void>): Promise<string> {
    const frame = waitForFrameUrl(cdp, 'chrome://settings', TASK12_RUNTIME_TIMEOUT_MS);
    await trigger();
    return frame;
  }

  private async selectProfile(profileId: string): Promise<void> {
    await runInPageObservedAction(this.cdp, {
      // The profile editor only mounts on the Profiles pane; ensure it is active before
      // selecting the catalog entry so the editor DOM the predicate waits for exists.
      action: `(function(){ if (window.settingsStore.getSnapshot().currentPaneKey !== 'profiles') window.settingsStore.selectPane('profiles'); window.settingsStore.selectProfileFromCatalog(${quoted(profileId)}); })()`,
      statePredicate: selectedTargetPredicate(profileId),
      domPredicate: `document.querySelector('[data-profile-editor="true"]') !== null`,
    });
  }

  private async happyMutation(request: HappyMutationRequest): Promise<void> {
    await this.selectProfile(request.profileId);
    const before = await this.cdp.eval<{contextRevision: string; profileRevision: string}>(`(function(){
      const context = window.settingsStore.getSnapshot().selectedProfileContext;
      if (!context || context.profileId !== ${quoted(request.profileId)}) throw new Error('Selected profile context unavailable');
      return {contextRevision:String(context.contextRevision), profileRevision:String(context.profileRevision)};
    })()`);
    if (!before) throw new Error('Selected profile revision capture failed');

    const guardedAction = async (action: string, snapshotPredicate: string): Promise<void> => {
      await runInPageObservedAction(this.cdp, {
        action,
        statePredicate: `${contextGuard(request.profileId, `${before.contextRevision}n`, `${before.profileRevision}n`)} && (${snapshotPredicate})`,
        domPredicate: `document.querySelector('[data-profile-editor="true"]') !== null`,
      });
    };

    await guardedAction(
      `window.settingsStore.updateSelectedProfileMetadata(${quoted({name: request.name, avatarColor: request.avatarColor})})`,
      `state.selectedProfileMetadata?.name === ${quoted(request.name)} && state.selectedProfileMetadata?.avatarColor === ${quoted(request.avatarColor)}`,
    );
    await guardedAction(
      `window.settingsStore.setSelectedProfileDefaultSearchEngine(${quoted(request.searchEngineKeyword)})`,
      `state.selectedProfileSearch?.engines?.some(engine => engine.keyword === ${quoted(request.searchEngineKeyword)} && engine.isDefault)`,
    );
    await guardedAction(
      `window.settingsStore.setSelectedProfileSearchSuggestionsEnabled(${request.searchSuggestionsEnabled})`,
      `state.selectedProfileSearch?.suggestionsEnabled === ${request.searchSuggestionsEnabled}`,
    );
    await guardedAction(
      `window.settingsStore.setSelectedProfileDownloadPrompt(${request.downloadPrompt})`,
      `state.selectedProfileDownload?.promptForDownload === ${request.downloadPrompt}`,
    );
    await guardedAction(
      `window.settingsStore.setSelectedProfileArchiveTimeout(${request.archiveTimeoutHours})`,
      `state.selectedProfileArchive?.timeoutHours === ${request.archiveTimeoutHours}`,
    );
  }

  private async failureTarget(request: FailureTargetRequest): Promise<void> {
    await runInPageObservedAction(this.cdp, {
      action: `window.settingsStore.selectProfileTarget(${quoted(request.target)})`,
      statePredicate: `state.selectedProfileTarget?.profileId === ${quoted(request.target.profileId)} &&
        state.selectedProfileLoading === false &&
        typeof state.selectedProfileError === 'string' &&
        state.selectedProfileError.includes(${quoted(request.expectedMessage)})`,
      domPredicate: `document.querySelector('[role="alert"]')?.textContent?.includes(${quoted(request.expectedMessage)}) === true`,
    });
  }

  private async sensitivePane(request: SensitivePaneRequest): Promise<void> {
    await this.selectProfile(request.profileId);
    const payloadsBefore = await this.hooks.captureHostPayloadCount?.();
    await runInPageObservedAction(this.cdp, {
      action: `Promise.resolve(window.settingsStore.selectPane(${quoted(request.paneKey)}))`,
      statePredicate: `state.currentPaneKey === ${quoted(request.paneKey)} && state.selectedProfileContext?.isHostProfile === false`,
      domPredicate: `document.querySelector('[data-selected-profile-limitation="true"][data-pane=${quoted(request.paneKey)}]') !== null`,
    });
    if (payloadsBefore !== undefined) {
      const payloadsAfter = await this.hooks.captureHostPayloadCount?.();
      if (payloadsAfter !== payloadsBefore) throw new Error('Unsupported non-host route emitted a host payload');
    }
  }

  private async createProfile(profileName: string): Promise<void> {
    const result = await runInPageObservedAction(this.cdp, {
      action: `(async function(){
        const handler = window.settingsStore.getHandler();
        const {profile} = await handler.createProfile(${quoted(profileName)});
        if (!profile) throw new Error('Profile creation was rejected');
        await window.settingsStore.selectProfileFromCatalog(profile.id);
        return {id:profile.id, name:profile.name};
      })()`,
      statePredicate: `state.selectedProfileMetadata?.name === ${quoted(profileName)} && state.selectedProfileLoading === false`,
      domPredicate: `Array.from(document.querySelectorAll('[data-profile-name]')).some(node => node.getAttribute('data-profile-name') === ${quoted(profileName)})`,
    });
    if (!result) throw new Error('Profile creation returned no profile');
  }

  private async safeDeleteCancel(profileId: string): Promise<void> {
    await this.selectProfile(profileId);
    await runInPageObservedAction(this.cdp, {
      action: `(async function(){
        const selected = window.settingsStore.getSnapshot().selectedProfileMetadata?.name;
        const request = selected && Array.from(document.querySelectorAll('main button')).find(
          button => button.getAttribute('aria-label') === 'Delete profile ' + selected);
        if (!(request instanceof HTMLElement)) throw new Error('Delete request control unavailable');
        request.click();
        return true;
      })()`,
      statePredicate: `state.selectedProfileTarget?.profileId === ${quoted(profileId)}`,
      domPredicate: `Array.from(document.querySelectorAll('main button')).some(button => button.textContent?.trim() === 'Confirm delete')`,
    });
    await runInPageObservedAction(this.cdp, {
      action: `(async function(){
        const cancel = Array.from(document.querySelectorAll('main button')).find(button => button.textContent?.trim() === 'Cancel');
        if (!(cancel instanceof HTMLElement)) throw new Error('Delete cancel control unavailable');
        cancel.click();
        return true;
      })()`,
      statePredicate: `state.selectedProfileTarget?.profileId === ${quoted(profileId)} && state.selectedProfileContext !== null`,
      domPredicate: `!Array.from(document.querySelectorAll('main button')).some(button => button.textContent?.trim() === 'Confirm delete')`,
    });
    await this.selectProfile(profileId);
  }

  private async cleanupProfile(profileId: string): Promise<void> {
    await runInPageObservedAction(this.cdp, {
      action: `(async function(){
        const handler = window.settingsStore.getHandler();
        const {profiles} = await handler.getProfiles();
        const profile = profiles.find(item => item.id === ${quoted(profileId)});
        if (!profile) return {alreadyAbsent:true};
        if (profile.isDefault || profile.isActive || profile.spaceIds.length > 0 || profiles.length <= 1) {
          throw new Error('Cleanup profile is protected');
        }
        const {success} = await handler.deleteProfile(profile.id);
        if (!success) throw new Error('Cleanup profile deletion was rejected');
        return {alreadyAbsent:false};
      })()`,
      statePredicate: `state.selectedProfileTarget?.profileId !== ${quoted(profileId)}`,
      domPredicate: `!Array.from(document.querySelectorAll('[data-profile-name]')).some(node => node.getAttribute('data-profile-id') === ${quoted(profileId)})`,
    });
  }
}

// Compile-time compatibility checks against the real Settings store state.
type _SettingsStateCompatibility = SettingsState extends Task12RuntimeState ? true : never;
const _settingsStateCompatibility: _SettingsStateCompatibility = true;
void _settingsStateCompatibility;
