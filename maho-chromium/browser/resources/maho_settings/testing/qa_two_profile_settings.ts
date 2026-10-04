import {
  TASK12_DRIVER_ARTIFACTS,
  TASK12_FORBIDDEN_STATE_DOMAINS,
  type Task12DriverArtifact,
  type Task12DriverOperation,
  type Task12DriverPlan,
  type Task12DriverScenarioId,
  type Task12DriverScenarioPlan,
} from './task12_static_contract_fixture.js';
import {
  Task12CleanupRegistry,
  Task12EvidenceSession,
  captureTask12Dom,
  captureTask12ReadOnlyState,
  captureTask12Screenshot,
  verifyOwnedCleanup,
  type CdpTransport,
  type OwnedResource,
  type SignalHooks,
  type Task12BrowserState,
  type Task12CurrentBrowserSpaceSnapshotReader,
  type Task12DomCapture,
  type Task12EvidenceSessionOptions,
  type Task12ProfileObservablesSnapshotReader,
  type Task12StateExpressions,
} from './task12_runtime_evidence.js';
import type {
  RuntimeBindingAvailability,
  Task12ObservableSeamReport,
  Task12ScenarioRequests,
  Task12ScenarioResult,
} from './task12_runtime_scenarios.js';
import type {
  CleanupInventory,
  CleanupReceipt,
  Observation,
  Task12ExpectedChanges,
  Task12Snapshot,
} from './two_profile_qa_state.js';

export const MISSING_RUNTIME_BINDING = 'MISSING_RUNTIME_BINDING' as const;

const EXACT_EVENT_TIMEOUT_MS = 10_000;

const artifactSources: Readonly<Record<Task12DriverArtifact, Task12DriverOperation>> = {
  'before.json': {id: 'capture-state-before', kind: 'capture-state', phase: 'before'},
  'after.json': {id: 'capture-state-after', kind: 'capture-state', phase: 'after'},
  'state-diff.json': {id: 'enforce-diff', kind: 'enforce-state-diff', forbiddenDomains: TASK12_FORBIDDEN_STATE_DOMAINS},
  'events.jsonl': {id: 'record-events', kind: 'record-events'},
  'manifest.json': {id: 'await-events', kind: 'await-exact-events', eventClasses: ['navigation', 'profile'], timeoutMs: EXACT_EVENT_TIMEOUT_MS},
  'cleanup-receipt.json': {id: 'verify-cleanup', kind: 'verify-cleanup'},
  'dom-before.txt': {id: 'capture-dom-before', kind: 'capture-dom', phase: 'before'},
  'dom-after.txt': {id: 'capture-dom-after', kind: 'capture-dom', phase: 'after'},
  'screenshot-before.png': {id: 'capture-screenshot-before', kind: 'capture-screenshot', phase: 'before'},
  'screenshot-after.png': {id: 'capture-screenshot-after', kind: 'capture-screenshot', phase: 'after'},
};

const artifactWriters = Object.fromEntries(TASK12_DRIVER_ARTIFACTS.map(artifact => [artifact, {
  id: `write:${artifact}`,
  kind: 'write-artifact' as const,
  artifact,
  sourceOperationId: artifactSources[artifact].id,
}])) as Readonly<Record<Task12DriverArtifact, Task12DriverOperation>>;

const artifactMapping = Object.fromEntries(TASK12_DRIVER_ARTIFACTS.map(artifact => [
  artifact, `write:${artifact}`,
])) as Readonly<Record<Task12DriverArtifact, string>>;

const baseOperations = TASK12_DRIVER_ARTIFACTS.flatMap(artifact => [artifactSources[artifact], artifactWriters[artifact]]);
type FailureReason = 'unknown-target' | 'deleting-target' | 'stale-target' | 'unsupported-sensitive-pane';

function driverOwnedScenario(id: Task12DriverScenarioId, failureReason?: FailureReason): Task12DriverScenarioPlan {
  const operations: readonly Task12DriverOperation[] = failureReason === undefined
    ? baseOperations
    : [...baseOperations, {id: 'assert-failure', kind: 'assert-visible-failure', reason: failureReason}];
  return {id, operations, execution: operations.map(operation => operation.id), artifacts: artifactMapping};
}

export const task12DriverPlan: Task12DriverPlan = {
  scenarios: [
    driverOwnedScenario('happy-b-only-mutation'),
    driverOwnedScenario('failure-unknown-target', 'unknown-target'),
    driverOwnedScenario('failure-deleting-target', 'deleting-target'),
    driverOwnedScenario('failure-stale-target', 'stale-target'),
    driverOwnedScenario('failure-unsupported-sensitive-pane', 'unsupported-sensitive-pane'),
    driverOwnedScenario('lifecycle-create'),
    driverOwnedScenario('lifecycle-safe-delete-cancel'),
    driverOwnedScenario('cleanup'),
  ],
  cleanupHooks: {
    failure: {registered: true, operationId: 'cleanup-on-failure'},
    sigint: {registered: true, operationId: 'cleanup-on-sigint'},
  },
  cleanupOperations: [
    {id: 'cleanup-on-success', kind: 'verify-cleanup'},
    {id: 'cleanup-on-failure', kind: 'verify-cleanup'},
    {id: 'cleanup-on-sigint', kind: 'verify-cleanup'},
  ],
};

export interface Task12RuntimeProbe {
  readonly appFresh: () => boolean | Promise<boolean>;
  readonly cdpAvailable: () => boolean | Promise<boolean>;
  readonly relayAvailable: () => boolean | Promise<boolean>;
  readonly runtimeBindingAvailable: () => boolean | Promise<boolean>;
}

export type Task12BlockedReason = 'STALE_APP' | 'CDP_9222_UNAVAILABLE' | 'RELAY_18765_UNAVAILABLE';
export type Task12MissingObservable = 'focused-window' | 'selected-space' | 'space-assignments';

export type Task12DriverStatus =
  | {status: 'blocked-current-app'; reasons: readonly Task12BlockedReason[]}
  | {status: 'missing-runtime-binding'; reason: typeof MISSING_RUNTIME_BINDING; missing?: readonly string[]}
  | {status: 'blocked-missing-observable'; missing: readonly Task12MissingObservable[]}
  | {status: 'failed'; scenario?: Task12DriverScenarioId; message: string}
  | {status: 'passed'; scenarios: readonly Task12DriverScenarioId[]};

export const defaultTask12RuntimeProbe: Task12RuntimeProbe = {
  appFresh: () => false,
  cdpAvailable: () => false,
  relayAvailable: () => false,
  runtimeBindingAvailable: () => false,
};

export async function preflightTask12Driver(probe: Task12RuntimeProbe): Promise<Task12DriverStatus | {status: 'ready'}> {
  const [appFresh, cdpAvailable, relayAvailable] = await Promise.all([
    probe.appFresh(), probe.cdpAvailable(), probe.relayAvailable(),
  ]);
  const reasons: Task12BlockedReason[] = [];
  if (!appFresh) reasons.push('STALE_APP');
  if (!cdpAvailable) reasons.push('CDP_9222_UNAVAILABLE');
  if (!relayAvailable) reasons.push('RELAY_18765_UNAVAILABLE');
  if (reasons.length > 0) return {status: 'blocked-current-app', reasons};
  if (!await probe.runtimeBindingAvailable()) return {status: 'missing-runtime-binding', reason: MISSING_RUNTIME_BINDING};
  return {status: 'ready'};
}

export interface Task12ScenarioExecutorBinding {
  availability(): Promise<RuntimeBindingAvailability>;
  observableSeams(): Promise<Task12ObservableSeamReport & {
    readonly spaceAssignments?: {readonly status: 'available'; readonly source: string} |
      {readonly status: 'unavailable'; readonly reason: string};
  }>;
  execute(
    scenario: Task12DriverScenarioId,
    requests: Task12ScenarioRequests,
    emitEvent?: (event: {readonly kind: string; readonly scenario: Task12DriverScenarioId}) => Promise<void> | void,
  ): Promise<Task12ScenarioResult<unknown>>;
}

export interface Task12EvidenceSessionBinding {
  readonly runDirectory: string;
  appendEvent(event: {readonly [key: string]: string | number | boolean | null}): Promise<void>;
  writeJson(artifact: 'cleanup-receipt.json', value: unknown): Promise<void>;
  writeStateArtifacts(
    before: Pick<Task12CaptureResult, 'snapshot' | 'browser'>,
    after: Pick<Task12CaptureResult, 'snapshot' | 'browser'>,
    expected: Task12ExpectedChanges,
  ): Promise<{diff: {passed: boolean; forbiddenChanges: readonly unknown[]; missingObservations: readonly unknown[]}}>;
  finalizeManifest?(scenarioPassed: boolean): Promise<unknown>;
  verifyRequiredArtifacts(): Promise<{passed: boolean; missing: readonly Task12DriverArtifact[]; invalid?: readonly string[]}>;
}

export interface Task12CaptureResult {
  readonly snapshot: Task12Snapshot;
  readonly browser: Task12BrowserState;
  readonly dom: Task12DomCapture;
}

export interface Task12RuntimeDependencies {
  readonly probe: Task12RuntimeProbe;
  readonly preflight?: (probe: Task12RuntimeProbe) => Promise<Task12DriverStatus | {status: 'ready'}>;
  readonly cdp?: CdpTransport;
  readonly runtimeScenarioExecutor?: Task12ScenarioExecutorBinding;
  readonly scenarioRequests?: Task12ScenarioRequests;
  readonly stateExpressions?: Task12StateExpressions;
  readonly currentBrowserSpaceSnapshotReader?: Task12CurrentBrowserSpaceSnapshotReader;
  readonly profileObservablesSnapshotReader: Task12ProfileObservablesSnapshotReader;
  readonly evidenceRootDirectory?: string;
  readonly createEvidenceSession?: (scenario: Task12DriverScenarioId) => Promise<Task12EvidenceSessionBinding>;
  readonly captureState?: (
    scenario: Task12DriverScenarioId,
    phase: 'before' | 'after',
    session: Task12EvidenceSessionBinding,
  ) => Promise<Task12CaptureResult>;
  readonly expectedChanges?: (
    scenario: Task12DriverScenarioId,
    before: Task12Snapshot,
    after: Task12Snapshot,
  ) => Task12ExpectedChanges;
  readonly registerCleanupOwnership?: (register: (resource: OwnedResource) => void) => void | Promise<void>;
  readonly preExistingCleanupInventory?: CleanupInventory;
  readonly cleanupRegistry?: Task12CleanupRegistry;
  readonly signalHooks?: SignalHooks;
  readonly now?: () => string;
}

const EMPTY_INVENTORY = (): CleanupInventory => ({
  profileIds: [], profileNames: [], tabIds: [], tempFiles: [], processes: [],
});

function emptyReceipt(completedAt: string): CleanupReceipt {
  return {completedAt, removed: EMPTY_INVENTORY(), remaining: EMPTY_INVENTORY(), errors: []};
}

function nodeSignalHooks(): SignalHooks {
  return {
    onSigint(listener) {
      process.on('SIGINT', listener);
      return () => process.off('SIGINT', listener);
    },
  };
}

function missingSnapshotObservables(snapshot: Task12Snapshot): Task12MissingObservable[] {
  const missing: Task12MissingObservable[] = [];
  if (snapshot.focusedWindowId.status !== 'observed') missing.push('focused-window');
  if (snapshot.selectedSpace.status !== 'observed') missing.push('selected-space');
  if (snapshot.spaceAssignments.status !== 'observed') missing.push('space-assignments');
  return missing;
}

function missingPassageObservables(report: Awaited<ReturnType<Task12ScenarioExecutorBinding['observableSeams']>>): Task12MissingObservable[] {
  const missing: Task12MissingObservable[] = [];
  if (report.focusedWindow.status !== 'available') missing.push('focused-window');
  if (report.selectedSpace.status !== 'available') missing.push('selected-space');
  if (report.spaceAssignments?.status !== 'available') missing.push('space-assignments');
  return missing;
}

async function writeCleanupReceipts(
  sessions: readonly Task12EvidenceSessionBinding[],
  receipt: CleanupReceipt,
): Promise<void> {
  await Promise.all(sessions.map(session => session.writeJson('cleanup-receipt.json', receipt)));
}

export async function runTask12Qa(deps: Task12RuntimeDependencies): Promise<Task12DriverStatus> {
  const preflight = await (deps.preflight ?? preflightTask12Driver)(deps.probe);
  if (preflight.status !== 'ready') return preflight;

  const executor = deps.runtimeScenarioExecutor;
  const createEvidenceSession = deps.createEvidenceSession ?? (deps.evidenceRootDirectory === undefined
    ? undefined
    : createTask12EvidenceFactory({rootDirectory: deps.evidenceRootDirectory}));
  const captureState = deps.captureState ?? (deps.cdp === undefined || deps.stateExpressions === undefined
    ? undefined
    : createTask12StateCapture(
        deps.cdp,
        deps.stateExpressions,
        deps.profileObservablesSnapshotReader,
        deps.now,
        deps.currentBrowserSpaceSnapshotReader,
      ));
  if (!executor || !deps.scenarioRequests || !captureState || !deps.expectedChanges || !createEvidenceSession) {
    return {status: 'missing-runtime-binding', reason: MISSING_RUNTIME_BINDING};
  }

  const availability = await executor.availability();
  if (availability.status !== 'available') return availability;
  const missingObservables = missingPassageObservables(await executor.observableSeams());
  if (missingObservables.length > 0) {
    return {status: 'blocked-missing-observable', missing: missingObservables};
  }

  const now = deps.now ?? (() => new Date().toISOString());
  const registry = deps.cleanupRegistry ?? new Task12CleanupRegistry();
  const sessions: Task12EvidenceSessionBinding[] = [];
  let activeScenario: Task12DriverScenarioId | undefined;
  let receipt: CleanupReceipt | undefined;
  const persistReceipt = async (value: CleanupReceipt) => {
    receipt ??= value;
    await writeCleanupReceipts(sessions, receipt);
  };
  const uninstallSigint = registry.installSigintCleanup(
    deps.signalHooks ?? nodeSignalHooks(),
    now,
    value => persistReceipt(value),
  );

  try {
    const outcome = await registry.withCleanupFinally(async () => {
      await deps.registerCleanupOwnership?.(resource => registry.registerOwned(resource));
      const passed: Task12DriverScenarioId[] = [];
      for (const plan of task12DriverPlan.scenarios) {
        activeScenario = plan.id;
        const session = await createEvidenceSession(plan.id);
        sessions.push(session);
        await session.appendEvent({sequence: 1, kind: 'scenario-start', scenario: plan.id});

        const before = await captureState(plan.id, 'before', session);
        await session.appendEvent({sequence: 2, kind: 'before-captured', scenario: plan.id});
        const missingBefore = missingSnapshotObservables(before.snapshot);
        if (missingBefore.length > 0) {
          await session.appendEvent({sequence: 3, kind: 'blocked-missing-observable', scenario: plan.id});
          return {status: 'blocked-missing-observable' as const, missing: missingBefore};
        }

        const execution = await executor.execute(
          plan.id,
          deps.scenarioRequests!,
          event => session.appendEvent({sequence: 3, kind: event.kind, scenario: event.scenario}),
        );
        if (execution.status !== 'passed') {
          await session.appendEvent({sequence: 3, kind: 'missing-runtime-binding', scenario: plan.id});
          throw new Error(`${MISSING_RUNTIME_BINDING}: ${execution.missing.join(', ')}`);
        }
        await session.appendEvent({sequence: 3, kind: 'exact-action-complete', scenario: plan.id});

        const after = await captureState(plan.id, 'after', session);
        await session.appendEvent({sequence: 4, kind: 'after-captured', scenario: plan.id});
        const missingAfter = missingSnapshotObservables(after.snapshot);
        if (missingAfter.length > 0) {
          await session.appendEvent({sequence: 5, kind: 'blocked-missing-observable', scenario: plan.id});
          return {status: 'blocked-missing-observable' as const, missing: missingAfter};
        }

        const expected = deps.expectedChanges!(plan.id, before.snapshot, after.snapshot);
        const {diff} = await session.writeStateArtifacts(
          {snapshot: before.snapshot, browser: before.browser},
          {snapshot: after.snapshot, browser: after.browser},
          expected,
        );
        if (!diff.passed || diff.forbiddenChanges.length > 0 || diff.missingObservations.length > 0) {
          await session.appendEvent({sequence: 5, kind: 'state-diff-failed', scenario: plan.id});
          throw new Error(`Scenario ${plan.id} failed closed state-diff enforcement`);
        }
        await session.appendEvent({sequence: 5, kind: 'scenario-passed', scenario: plan.id});
        passed.push(plan.id);
      }
      return {status: 'passed' as const, scenarios: passed};
    }, now, persistReceipt);

    if (outcome.status !== 'passed') return outcome;
    if (receipt === undefined) throw new Error('Cleanup completed without a receipt');
    const remainingOwned = Object.values(receipt.remaining).some(resources => resources.length > 0);
    if (receipt.errors.length > 0 || remainingOwned) {
      return {status: 'failed', scenario: activeScenario, message: `Cleanup receipt incomplete: ${[
        ...receipt.errors, ...(remainingOwned ? ['owned resources remain'] : []),
      ].join(', ')}`};
    }
    const cleanup = verifyOwnedCleanup(deps.preExistingCleanupInventory ?? EMPTY_INVENTORY(), registry, receipt);
    if (!cleanup.verification.passed) {
      return {status: 'failed', scenario: activeScenario, message: `Cleanup verification failed: ${[
        ...cleanup.verification.missing, ...cleanup.verification.violations,
      ].join(', ')}`};
    }
    for (const session of sessions) {
      await session.finalizeManifest?.(true);
      const artifacts = await session.verifyRequiredArtifacts();
      if (!artifacts.passed) {
        const details = [...artifacts.missing, ...(artifacts.invalid ?? [])];
        return {status: 'failed', scenario: activeScenario, message: `Required artifact verification failed: ${details.join(', ')}`};
      }
    }
    return outcome;
  } catch (error) {
    return {
      status: 'failed',
      ...(activeScenario === undefined ? {} : {scenario: activeScenario}),
      message: error instanceof Error ? error.message : String(error),
    };
  } finally {
    try {
      uninstallSigint();
    } catch (error) {
      return {
        status: 'failed',
        ...(activeScenario === undefined ? {} : {scenario: activeScenario}),
        message: error instanceof Error ? error.message : String(error),
      };
    }
  }
}

const defaultStateExpressions: Task12StateExpressions = {
  profileA: 'globalThis.__mahoTask12ProfileA',
  profileB: 'globalThis.__mahoTask12ProfileB',
  globalState: 'globalThis.__mahoTask12GlobalState',
};

export const defaultTask12RuntimeDependencies: Task12RuntimeDependencies = {
  probe: defaultTask12RuntimeProbe,
  stateExpressions: defaultStateExpressions,
  profileObservablesSnapshotReader: {
    getProfileObservablesSnapshot: async () => ({snapshot: null}),
  },
};

export function createTask12EvidenceFactory(options: Omit<Task12EvidenceSessionOptions, 'scenarioId'>):
    (scenario: Task12DriverScenarioId) => Promise<Task12EvidenceSession> {
  return scenario => Task12EvidenceSession.create({...options, scenarioId: scenario});
}

export function createTask12StateCapture(
  cdp: CdpTransport,
  expressions: Task12StateExpressions,
  profileObservablesSnapshotReader: Task12ProfileObservablesSnapshotReader,
  now: () => string = () => new Date().toISOString(),
  currentBrowserSpaceSnapshotReader?: Task12CurrentBrowserSpaceSnapshotReader,
): Task12RuntimeDependencies['captureState'] {
  return async (_scenario, phase, session) => {
    const evidence = session as Task12EvidenceSession;
    const dom = await captureTask12Dom(cdp, evidence, phase);
    await captureTask12Screenshot(cdp, evidence, phase);
    const captured = await captureTask12ReadOnlyState(
      cdp, expressions, now(), dom, profileObservablesSnapshotReader,
      currentBrowserSpaceSnapshotReader,
    );
    return {snapshot: captured.snapshot, browser: captured.browser, dom};
  };
}

export async function main(deps: Task12RuntimeDependencies = defaultTask12RuntimeDependencies): Promise<Task12DriverStatus> {
  return runTask12Qa(deps);
}
