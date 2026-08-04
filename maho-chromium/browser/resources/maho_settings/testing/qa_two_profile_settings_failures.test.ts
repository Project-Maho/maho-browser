import {describe, expect, test} from 'bun:test';

import {
  runTask12Qa,
  type Task12CaptureResult,
  type Task12DriverStatus,
  type Task12EvidenceSessionBinding,
  type Task12RuntimeDependencies,
  type Task12ScenarioExecutorBinding,
} from './qa_two_profile_settings.js';
import {
  Task12CleanupRegistry,
  type OwnedResource,
  type SignalHooks,
  type Task12ProfileObservablesSnapshotReader,
} from './task12_runtime_evidence.js';
import type {
  Task12ScenarioRequests,
  Task12ScenarioResult,
} from './task12_runtime_scenarios.js';
import {diffTask12State} from './two_profile_qa_state.js';
import type {
  CleanupInventory,
  CleanupReceipt,
  Task12ExpectedChanges,
  Task12Snapshot,
} from './two_profile_qa_state.js';

type ScenarioId = Parameters<Task12ScenarioExecutorBinding['execute']>[0];
type Artifact = Awaited<ReturnType<Task12EvidenceSessionBinding['verifyRequiredArtifacts']>>['missing'][number];
type Diff = Awaited<ReturnType<Task12EvidenceSessionBinding['writeStateArtifacts']>>['diff'];
type Event = Parameters<Task12EvidenceSessionBinding['appendEvent']>[0];
type ReceiptWriter = Parameters<Task12EvidenceSessionBinding['writeJson']>[1];

const observed = <T>(value: T) => ({status: 'observed' as const, value});
const profileObservablesSnapshotReader: Task12ProfileObservablesSnapshotReader = {
  getProfileObservablesSnapshot: async () => ({
    snapshot: {
      activeBrowserProfileId: 'browser-a',
      activeMahoProfileId: 'maho-a',
      registryRevision: '1',
      lifecycleEntries: [
        {profileId: 'profile-a', lifecycleState: 1},
        {profileId: 'profile-b', lifecycleState: 1},
      ],
      pendingDeletionProfileIds: [],
      deletedHistoryAvailability: 0,
    },
  }),
};
const snapshot: Task12Snapshot = {
  timestamp: '2026-08-01T00:00:00.000Z',
  activeBrowserProfileId: observed('browser-a'),
  activeMahoProfileId: observed('maho-a'),
  focusedWindowId: observed('window-a'),
  selectedSpace: observed({id: 'space-a', name: 'Personal'}),
  spaceAssignments: observed({tabIds: ['tab-a'], windowIds: ['window-a']}),
  profiles: {
    A: observed({id: 'profile-a', name: 'Personal', avatarColor: '#111111', homepageUrl: 'https://a.example', searchEngineId: 'engine-a', searchSuggestionsEnabled: true, downloadPrompt: false, downloadDirectoryToken: 'download-a', archiveTimeoutHours: 24}),
    B: observed({id: 'profile-b', name: 'Work', avatarColor: '#222222', homepageUrl: 'https://b.example', searchEngineId: 'engine-b', searchSuggestionsEnabled: false, downloadPrompt: true, downloadDirectoryToken: 'download-b', archiveTimeoutHours: 72}),
  },
  globalState: observed({account: {}, process: {}, coreGlobal: {}}),
  lifecycle: observed({
    profiles: {A: 'ready', B: 'ready'},
    deletedProfileIds: {status: 'unavailable', reason: 'Deleted profile history is unavailable'},
    pendingDeletionProfileIds: [],
  }),
};
const capture: Task12CaptureResult = {
  snapshot,
  browser: {
    url: observed('chrome://maho-settings/'),
    title: observed('Settings'),
    focusedTarget: observed(true),
    target: observed({targetId: 'target-settings', type: 'page', url: 'chrome://maho-settings/', title: 'Settings'}),
    window: observed({windowId: 'window-a', bounds: {left: 0, top: 0, width: 1280, height: 800}}),
    selectedPane: observed('Profiles'),
    selectedTarget: observed({label: 'Work'}),
  },
  dom: {
    url: 'chrome://maho-settings/',
    title: 'Settings',
    text: 'qa',
    html: '<main>qa</main>',
    selectedPane: observed('Profiles'),
    selectedTarget: observed({label: 'Work'}),
  },
};
const requests: Task12ScenarioRequests = {
  happy: {profileId: 'profile-b', name: 'Work', avatarColor: '#222222', homepageUrl: 'https://b.example', searchEngineKeyword: 'engine-b', searchSuggestionsEnabled: false, downloadPrompt: true, archiveTimeoutHours: 72},
  unknownTarget: {target: {profileId: 'missing', targetToken: 'missing'}, expectedMessage: 'unavailable'},
  deletingTarget: {target: {profileId: 'deleting', targetToken: 'deleting'}, expectedMessage: 'deleting'},
  staleTarget: {target: {profileId: 'profile-b', targetToken: 'stale'}, expectedMessage: 'stale'},
  sensitivePane: {profileId: 'profile-b', paneKey: 'passwords'},
  lifecycle: {profileName: 'Task 12 disposable', profileId: 'disposable'},
};

const emptyInventory = (): CleanupInventory => ({
  profileIds: [],
  profileNames: [],
  tabIds: [],
  tempFiles: [],
  processes: [],
});

function receipt(errors: readonly string[] = []): CleanupReceipt {
  return {
    completedAt: '2026-08-01T00:00:00.000Z',
    removed: emptyInventory(),
    remaining: emptyInventory(),
    errors: [...errors],
  };
}

interface SessionRecord {
  readonly scenario: ScenarioId;
  readonly events: Event[];
  readonly receipts: ReceiptWriter[];
  verifyCalls: number;
}

interface HarnessOptions {
  readonly availability?: Awaited<ReturnType<Task12ScenarioExecutorBinding['availability']>>;
  readonly diff?: (scenario: ScenarioId) => Diff;
  readonly execute?: Task12ScenarioExecutorBinding['execute'];
  readonly captureState?: NonNullable<Task12RuntimeDependencies['captureState']>;
  readonly missingArtifact?: Artifact;
  readonly cleanupErrors?: readonly string[];
  readonly cleanupRemaining?: CleanupReceipt['remaining'];
  readonly signalHooks?: SignalHooks;
  readonly registry?: Task12CleanupRegistry;
  readonly registerCleanupOwnership?: NonNullable<Task12RuntimeDependencies['registerCleanupOwnership']>;
  readonly throwAt?: 'create' | 'append' | 'capture-before' | 'capture-after' | 'expected' | 'state-write' | 'manifest-write' | 'verify' | 'receipt-write' | 'uninstall';
}

function successfulExecution(scenario: ScenarioId): Task12ScenarioResult<unknown> {
  return {status: 'passed', scenario, before: snapshot, after: snapshot};
}

function createHarness(options: HarnessOptions = {}) {
  const sessions: SessionRecord[] = [];
  const executed: ScenarioId[] = [];
  const cleanupReceipts: CleanupReceipt[] = [];
  let cleanupRuns = 0;
  const registry = options.registry ?? new Task12CleanupRegistry();

  if (options.registry === undefined) {
    registry.withCleanupFinally = async <T>(
        action: () => Promise<T>,
        _now: () => string,
        record: (value: CleanupReceipt) => void | Promise<void>,
    ): Promise<T> => {
      try {
        return await action();
      } finally {
        cleanupRuns += 1;
        const value = {...receipt(options.cleanupErrors), remaining: options.cleanupRemaining ?? emptyInventory()};
        cleanupReceipts.push(value);
        await record(value);
      }
    };
  } else {
    const original = registry.withCleanupFinally.bind(registry);
    registry.withCleanupFinally = async (...args) => {
      cleanupRuns += 1;
      return original(...args);
    };
  }

  const executor: Task12ScenarioExecutorBinding = {
    availability: async () => options.availability ?? ({status: 'available'}),
    observableSeams: async () => ({
      activeMahoProfile: {status: 'available', source: 'test'},
      focusedWindow: {status: 'available', source: 'test'},
      selectedSpace: {status: 'available', source: 'test'},
      backendGlobalValues: {status: 'available', source: 'test'},
      spaceAssignments: {status: 'available', source: 'test'},
      fullPassageAvailable: true,
    }),
    execute: async (scenario, scenarioRequests) => {
      executed.push(scenario);
      return options.execute?.(scenario, scenarioRequests) ?? successfulExecution(scenario);
    },
  };

  const deps: Task12RuntimeDependencies = {
    probe: {
      appFresh: () => true,
      cdpAvailable: () => true,
      relayAvailable: () => true,
      runtimeBindingAvailable: () => true,
    },
    runtimeScenarioExecutor: executor,
    profileObservablesSnapshotReader,
    scenarioRequests: requests,
    captureState: options.captureState ?? (async (_scenario, phase) => {
      if (options.throwAt === `capture-${phase}`) throw new Error(`capture-${phase} boundary`);
      return capture;
    }),
    expectedChanges: scenario => {
      if (options.throwAt === 'expected') throw new Error('expected boundary');
      return {scenario, profileB: {}} satisfies Task12ExpectedChanges;
    },
    createEvidenceSession: async scenario => {
      if (options.throwAt === 'create') throw new Error('create boundary');
      const record: SessionRecord = {scenario, events: [], receipts: [], verifyCalls: 0};
      sessions.push(record);
      return {
        runDirectory: `/evidence/${scenario}`,
        appendEvent: async event => {
          if (options.throwAt === 'append') throw new Error('append boundary');
          record.events.push(event);
        },
        writeJson: async (_artifact, value) => {
          if (options.throwAt === 'receipt-write') throw new Error('receipt-write boundary');
          record.receipts.push(value);
        },
        writeStateArtifacts: async () => {
          if (options.throwAt === 'state-write') throw new Error('state-write boundary');
          return {diff: options.diff?.(scenario) ?? {passed: true, forbiddenChanges: [], missingObservations: []}};
        },
        finalizeManifest: async () => {
          if (options.throwAt === 'manifest-write') throw new Error('manifest-write boundary');
        },
        verifyRequiredArtifacts: async () => {
          if (options.throwAt === 'verify') throw new Error('verify boundary');
          record.verifyCalls += 1;
          return options.missingArtifact === undefined
            ? {passed: true, missing: []}
            : {passed: false, missing: [options.missingArtifact]};
        },
      };
    },
    cleanupRegistry: registry,
    registerCleanupOwnership: options.registerCleanupOwnership,
    signalHooks: options.signalHooks ?? (options.throwAt === 'uninstall' ? {
      onSigint: () => () => { throw new Error('uninstall boundary'); },
    } : undefined),
    now: () => '2026-08-01T00:00:00.000Z',
  };

  return {
    deps,
    sessions,
    executed,
    cleanupReceipts,
    cleanupRuns: () => cleanupRuns,
  };
}

function expectFirstScenarioFailure(result: Task12DriverStatus, executed: readonly ScenarioId[]): void {
  expect(result.status).toBe('failed');
  if (result.status !== 'failed') throw new Error(`Expected failure, received ${result.status}`);
  expect(result.scenario).toBe('happy-b-only-mutation');
  expect(executed).toEqual(['happy-b-only-mutation']);
}

describe('runTask12Qa failure closure', () => {
  const forbiddenMutations = [
    ['profile A', (after: Task12Snapshot) => { if (after.profiles.A.status === 'observed') after.profiles.A.value.name = 'changed'; }],
    ['active browser profile', (after: Task12Snapshot) => { after.activeBrowserProfileId = observed('browser-b'); }],
    ['active Maho profile', (after: Task12Snapshot) => { after.activeMahoProfileId = observed('maho-b'); }],
    ['focused window', (after: Task12Snapshot) => { after.focusedWindowId = observed('window-b'); }],
    ['selected Space', (after: Task12Snapshot) => { after.selectedSpace = observed({id: 'space-b', name: 'Work'}); }],
    ['Space assignments', (after: Task12Snapshot) => { after.spaceAssignments = observed({tabIds: ['tab-b'], windowIds: ['window-b']}); }],
    ['global account', (after: Task12Snapshot) => { if (after.globalState.status === 'observed') after.globalState.value.account.plan = 'pro'; }],
    ['global process', (after: Task12Snapshot) => { if (after.globalState.status === 'observed') after.globalState.value.process.gpu = false; }],
    ['global core', (after: Task12Snapshot) => { if (after.globalState.status === 'observed') after.globalState.value.coreGlobal.telemetry = true; }],
  ] as const;

  for (const [domain, mutate] of forbiddenMutations) {
    test(`fails closed on real state-core forbidden ${domain} mutation and stops later scenarios`, async () => {
      const before = structuredClone(snapshot);
      const after = structuredClone(snapshot);
      mutate(after);
      const harness = createHarness({
        diff: () => diffTask12State(before, after, {scenario: 'happy-b-only-mutation', profileB: {}, allowedCleanup: []}),
      });

      const result = await runTask12Qa(harness.deps);

      expectFirstScenarioFailure(result, harness.executed);
      expect(result).toMatchObject({message: 'Scenario happy-b-only-mutation failed closed state-diff enforcement'});
      expect(harness.cleanupRuns()).toBe(1);
      expect(harness.sessions[0]?.events.at(-1)).toMatchObject({kind: 'state-diff-failed'});
    });
  }

  test('returns unavailable executor status before cleanup or evidence setup', async () => {
    const harness = createHarness({availability: {status: 'missing-runtime-binding', reason: 'MISSING_RUNTIME_BINDING', missing: ['executor']}});

    expect(await runTask12Qa(harness.deps)).toEqual({
      status: 'missing-runtime-binding', reason: 'MISSING_RUNTIME_BINDING', missing: ['executor'],
    });
    expect(harness.cleanupRuns()).toBe(0);
    expect(harness.sessions).toHaveLength(0);
  });

  for (const phase of ['before', 'after'] as const) {
    test(`blocks and cleans up when the ${phase} snapshot loses a required observation`, async () => {
      const missingSnapshot = {...snapshot, selectedSpace: {status: 'unavailable', reason: 'lost seam'}} as Task12Snapshot;
      const harness = createHarness({
        captureState: async (_scenario, actualPhase) => ({
          snapshot: actualPhase === phase ? missingSnapshot : snapshot,
          browser: capture.browser,
          dom: capture.dom,
        }),
      });

      expect(await runTask12Qa(harness.deps)).toEqual({status: 'blocked-missing-observable', missing: ['selected-space']});
      expect(harness.cleanupRuns()).toBe(1);
      expect(harness.executed).toHaveLength(phase === 'before' ? 0 : 1);
    });
  }

  test('fails and cleans up when the executor returns a missing binding', async () => {
    const harness = createHarness({execute: async () => ({status: 'missing-runtime-binding', reason: 'MISSING_RUNTIME_BINDING', missing: ['profile event']})});

    const result = await runTask12Qa(harness.deps);

    expectFirstScenarioFailure(result, harness.executed);
    expect(result).toMatchObject({message: 'MISSING_RUNTIME_BINDING: profile event'});
    expect(harness.cleanupRuns()).toBe(1);
  });

  test('fails closed on explicit diff missing observations even when diff passed is true', async () => {
    const harness = createHarness({diff: () => ({passed: true, forbiddenChanges: [], missingObservations: [{path: 'focusedWindowId'}]})});

    const result = await runTask12Qa(harness.deps);

    expectFirstScenarioFailure(result, harness.executed);
    expect(harness.cleanupRuns()).toBe(1);
  });

  test('cleans up after ownership registration partially succeeds and then throws', async () => {
    let cleaned = 0;
    const registry = new Task12CleanupRegistry();
    const harness = createHarness({
      registry,
      registerCleanupOwnership(register) {
        register({kind: 'profileId', identity: 'partial-owned', cleanup: async () => { cleaned += 1; }});
        throw new Error('partial registration failed');
      },
    });

    const result = await runTask12Qa(harness.deps);

    expect(result).toMatchObject({status: 'failed', message: 'partial registration failed'});
    expect(cleaned).toBe(1);
    expect(harness.cleanupRuns()).toBe(1);
  });

  test('turns a scenario throw into a failed status and still cleans up', async () => {
    const harness = createHarness({
      execute: async () => { throw new Error('scenario exploded'); },
    });

    const result = await runTask12Qa(harness.deps);

    expectFirstScenarioFailure(result, harness.executed);
    expect(result).toMatchObject({message: 'scenario exploded'});
    expect(harness.cleanupRuns()).toBe(1);
  });

  test('fails closed on a malformed capture', async () => {
    const harness = createHarness({
      captureState: async () => ({snapshot: null, dom: null} as unknown as Task12CaptureResult),
    });

    const result = await runTask12Qa(harness.deps);

    expect(result.status).toBe('failed');
    if (result.status !== 'failed') throw new Error(`Expected failure, received ${result.status}`);
    expect(result.scenario).toBe('happy-b-only-mutation');
    expect(harness.executed).toEqual([]);
    expect(harness.cleanupRuns()).toBe(1);
  });

  test('reports a missing required artifact after cleanup', async () => {
    const harness = createHarness({missingArtifact: 'manifest.json'});

    const result = await runTask12Qa(harness.deps);

    expect(result).toEqual({
      status: 'failed',
      scenario: 'cleanup',
      message: 'Required artifact verification failed: manifest.json',
    });
    expect(harness.cleanupRuns()).toBe(1);
    expect(harness.executed).toHaveLength(8);
    expect(harness.sessions[0]?.verifyCalls).toBe(1);
  });

  for (const incomplete of [
    {name: 'errors', options: {cleanupErrors: ['failed to remove owned profile']}},
    {name: 'remaining resources', options: {cleanupRemaining: {...emptyInventory(), profileIds: ['still-owned']} as CleanupReceipt['remaining']}},
  ] as const) {
    test(`otherwise successful execution fails closed on cleanup ${incomplete.name}`, async () => {
      const harness = createHarness(incomplete.options);

      const result = await runTask12Qa(harness.deps);

      expect(result.status).toBe('failed');
      expect(harness.executed).toHaveLength(8);
      expect(harness.cleanupRuns()).toBe(1);
    });
  }

  for (const boundary of ['create', 'append', 'capture-before', 'capture-after', 'expected', 'state-write', 'manifest-write', 'verify', 'receipt-write', 'uninstall'] as const) {
    test(`fails closed and cleans up when the ${boundary} boundary throws`, async () => {
      const harness = createHarness({throwAt: boundary});

      const result = await runTask12Qa(harness.deps);

      expect(result.status).toBe('failed');
      expect(harness.cleanupRuns()).toBe(1);
    });
  }

  test('cleanup always runs and records cleanup failure in every opened session', async () => {
    const harness = createHarness({
      execute: async () => { throw new Error('scenario failed'); },
      cleanupErrors: ['failed to remove owned profile'],
    });

    const result = await runTask12Qa(harness.deps);

    expect(result).toMatchObject({status: 'failed', message: 'scenario failed'});
    expect(harness.cleanupRuns()).toBe(1);
    expect(harness.cleanupReceipts).toHaveLength(1);
    expect(harness.cleanupReceipts[0]?.errors).toEqual(['failed to remove owned profile']);
    expect(harness.sessions[0]?.receipts).toEqual(harness.cleanupReceipts);
  });

  test('SIGINT cleanup is idempotent and pre-existing resources are preserved', async () => {
    let sigint: (() => void) | undefined;
    let uninstallCalls = 0;
    const signalHooks: SignalHooks = {
      onSigint(listener) {
        sigint = listener;
        return () => { uninstallCalls += 1; sigint = undefined; };
      },
    };
    const enteredExecute = Promise.withResolvers<void>();
    const releaseExecute = Promise.withResolvers<void>();
    const owned = Object.freeze({name: 'owned'}) as unknown as OwnedResource;
    const preExisting = Object.freeze({name: 'pre-existing'}) as unknown as OwnedResource;
    const registered: OwnedResource[] = [];
    const removed: OwnedResource[] = [];
    const registry = new Task12CleanupRegistry();
    registry.registerOwned = resource => { registered.push(resource); };
    let signalCleanup: Promise<void> | undefined;
    registry.installSigintCleanup = (hooks, _now, record) => hooks.onSigint(() => {
      signalCleanup ??= (async () => {
        removed.push(...registered);
        await record(receipt());
      })();
      void signalCleanup;
    });
    const harness = createHarness({
      registry,
      signalHooks,
      registerCleanupOwnership: register => { register(owned); },
      execute: async () => {
        enteredExecute.resolve();
        await releaseExecute.promise;
        return successfulExecution('happy-b-only-mutation');
      },
    });

    const run = runTask12Qa(harness.deps);
    await enteredExecute.promise;
    expect(sigint).toBeDefined();
    sigint?.();
    sigint?.();
    await signalCleanup;
    releaseExecute.resolve();
    const result = await run;

    expect(result.status).toBe('passed');
    expect(registered).toEqual([owned]);
    expect(removed).toEqual([owned]);
    expect(removed).not.toContain(preExisting);
    expect(harness.sessions.every(session => session.receipts.length >= 1)).toBe(true);
    expect(new Set(harness.sessions.flatMap(session => session.receipts)).size).toBe(1);
    expect(uninstallCalls).toBe(1);
  });
});
