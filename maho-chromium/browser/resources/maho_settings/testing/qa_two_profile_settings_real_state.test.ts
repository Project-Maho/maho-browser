import {describe, expect, test} from 'bun:test';

import {
  runTask12Qa,
  type Task12CaptureResult,
  type Task12DriverStatus,
  type Task12EvidenceSessionBinding,
  type Task12RuntimeDependencies,
  type Task12ScenarioExecutorBinding,
} from './qa_two_profile_settings.js';
import type {Task12ScenarioRequests} from './task12_runtime_scenarios.js';
import {
  diffTask12State,
  type Task12ExpectedChanges,
  type Task12Snapshot,
  type Task12StateDiff,
} from './two_profile_qa_state.js';
import type {Task12ProfileObservablesSnapshotReader} from './task12_runtime_evidence.js';

type ScenarioId = Parameters<Task12ScenarioExecutorBinding['execute']>[0];
type SnapshotMutation = (snapshot: Task12Snapshot) => void;
type CaptureMutation = (snapshot: Task12Snapshot, phase: 'before' | 'after') => void;

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
const scenarioRequests = Object.freeze({}) as Task12ScenarioRequests;
const browser: Task12CaptureResult['browser'] = {
  url: observed('chrome://settings/'),
  title: observed('Settings'),
  focusedTarget: observed(true),
  target: observed({targetId: 'target-settings', type: 'page', url: 'chrome://settings/', title: 'Settings'}),
  window: observed({windowId: 'window-a', bounds: {left: 0, top: 0, width: 1280, height: 800}}),
  selectedPane: observed('profiles'),
  selectedTarget: observed({label: 'Work'}),
};
const dom: Task12CaptureResult['dom'] = {
  url: 'chrome://settings/',
  title: 'Settings',
  text: 'Task 12 QA',
  html: '<main>Task 12 QA</main>',
  selectedPane: observed('profiles'),
  selectedTarget: observed({label: 'Work'}),
};

function snapshot(): Task12Snapshot {
  return {
    timestamp: '2026-08-01T00:00:00.000Z',
    activeBrowserProfileId: observed('browser-a'),
    activeMahoProfileId: observed('maho-a'),
    focusedWindowId: observed('window-a'),
    selectedSpace: observed({id: 'space-a', name: 'Personal'}),
    spaceAssignments: observed({tabIds: ['tab-a'], windowIds: ['window-a']}),
    profiles: {
      A: observed({
        id: 'profile-a',
        name: 'Personal',
        avatarColor: '#111111',
        homepageUrl: 'https://a.example/',
        searchEngineId: 'engine-a',
        searchSuggestionsEnabled: true,
        downloadPrompt: false,
        downloadDirectoryToken: 'downloads-a',
        archiveTimeoutHours: 24,
      }),
      B: observed({
        id: 'profile-b',
        name: 'Work',
        avatarColor: '#222222',
        homepageUrl: 'https://b.example/',
        searchEngineId: 'engine-b',
        searchSuggestionsEnabled: false,
        downloadPrompt: true,
        downloadDirectoryToken: 'downloads-b',
        archiveTimeoutHours: 72,
      }),
    },
    globalState: observed({
      account: {plan: 'free'},
      process: {hardwareAcceleration: true},
      coreGlobal: {telemetryEnabled: false},
    }),
    lifecycle: observed({
      profiles: {A: 'ready', B: 'ready'},
      deletedProfileIds: {status: 'unavailable', reason: 'Deleted profile history is unavailable'},
      pendingDeletionProfileIds: [],
    }),
  };
}

function mutateApprovedB(snapshotValue: Task12Snapshot): void {
  if (snapshotValue.profiles.B.status === 'observed') {
    snapshotValue.profiles.B.value.name = 'Work QA';
    snapshotValue.profiles.B.value.searchSuggestionsEnabled = true;
  }
}

function expectedChanges(scenario: ScenarioId): Task12ExpectedChanges {
  return {
    scenario,
    profileB: {
      name: {before: 'Work', after: 'Work QA'},
      searchSuggestionsEnabled: {before: false, after: true},
    },
    allowedCleanup: [],
  };
}

interface HarnessOptions {
  readonly mutateAfter?: SnapshotMutation;
  readonly mutateCapture?: CaptureMutation;
}

interface SessionRecord {
  readonly scenario: ScenarioId;
  readonly diffs: Task12StateDiff[];
  readonly receipts: unknown[];
}

function createHarness(options: HarnessOptions = {}) {
  const executed: ScenarioId[] = [];
  const sessions: SessionRecord[] = [];
  let cleanupRuns = 0;

  const executor: Task12ScenarioExecutorBinding = {
    availability: async () => ({status: 'available'}),
    observableSeams: async () => ({
      activeMahoProfile: {status: 'available', source: 'typed-test-snapshot'},
      focusedWindow: {status: 'available', source: 'typed-test-snapshot'},
      selectedSpace: {status: 'available', source: 'typed-test-snapshot'},
      spaceAssignments: {status: 'available', source: 'typed-test-snapshot'},
      backendGlobalValues: {status: 'available', source: 'typed-test-snapshot'},
      fullPassageAvailable: true,
    }),
    execute: async scenario => {
      executed.push(scenario);
      return {status: 'passed', scenario, before: undefined, after: undefined};
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
    scenarioRequests,
    captureState: async (_scenario, phase) => {
      const captured = snapshot();
      if (phase === 'after') {
        mutateApprovedB(captured);
        options.mutateAfter?.(captured);
      }
      options.mutateCapture?.(captured, phase);
      return {snapshot: captured, browser, dom};
    },
    expectedChanges: scenario => expectedChanges(scenario),
    createEvidenceSession: async scenario => {
      const record: SessionRecord = {scenario, diffs: [], receipts: []};
      sessions.push(record);
      const session: Task12EvidenceSessionBinding = {
        runDirectory: `/typed-state/${scenario}`,
        appendEvent: async () => {},
        writeJson: async (_artifact, value) => { record.receipts.push(value); },
        writeStateArtifacts: async (before, after, expected) => {
          const diff = diffTask12State(before.snapshot, after.snapshot, expected);
          record.diffs.push(diff);
          return {diff};
        },
        verifyRequiredArtifacts: async () => ({passed: true, missing: []}),
      };
      return session;
    },
    registerCleanupOwnership: register => {
      register({
        kind: 'tempFile',
        identity: `/typed-state/task-${sessions.length}`,
        cleanup: async () => { cleanupRuns += 1; },
      });
    },
    signalHooks: {onSigint: () => () => {}},
    now: () => '2026-08-01T00:05:00.000Z',
  };

  return {deps, executed, sessions, cleanupRuns: () => cleanupRuns};
}

function expectFirstScenarioFailed(result: Task12DriverStatus, executed: readonly ScenarioId[]): void {
  expect(result).toEqual({
    status: 'failed',
    scenario: 'happy-b-only-mutation',
    message: 'Scenario happy-b-only-mutation failed closed state-diff enforcement',
  });
  expect(executed).toEqual(['happy-b-only-mutation']);
}

const forbiddenCases: ReadonlyArray<{
  readonly scope: string;
  readonly path: string;
  readonly mutate: SnapshotMutation;
}> = [
  {
    scope: 'profile A',
    path: 'profiles.A.name',
    mutate: value => { if (value.profiles.A.status === 'observed') value.profiles.A.value.name = 'Leaked'; },
  },
  {
    scope: 'active browser profile',
    path: 'activeBrowserProfileId',
    mutate: value => { value.activeBrowserProfileId = observed('browser-b'); },
  },
  {
    scope: 'active Maho profile',
    path: 'activeMahoProfileId',
    mutate: value => { value.activeMahoProfileId = observed('maho-b'); },
  },
  {
    scope: 'focused window',
    path: 'focusedWindowId',
    mutate: value => { value.focusedWindowId = observed('window-b'); },
  },
  {
    scope: 'selected Space',
    path: 'selectedSpace.id',
    mutate: value => { value.selectedSpace = observed({id: 'space-b', name: 'Work'}); },
  },
  {
    scope: 'Space assignments',
    path: 'spaceAssignments.tabIds',
    mutate: value => { value.spaceAssignments = observed({tabIds: ['tab-b'], windowIds: ['window-a']}); },
  },
  {
    scope: 'global account',
    path: 'globalState.account.plan',
    mutate: value => { if (value.globalState.status === 'observed') value.globalState.value.account.plan = 'paid'; },
  },
  {
    scope: 'global process',
    path: 'globalState.process.hardwareAcceleration',
    mutate: value => { if (value.globalState.status === 'observed') value.globalState.value.process.hardwareAcceleration = false; },
  },
  {
    scope: 'global core',
    path: 'globalState.coreGlobal.telemetryEnabled',
    mutate: value => { if (value.globalState.status === 'observed') value.globalState.value.coreGlobal.telemetryEnabled = true; },
  },
];

describe('runTask12Qa with real state-core diffs', () => {
  test('accepts only the approved profile-B allowlist changes at the scenario diff layer', async () => {
    const harness = createHarness();

    const result = await runTask12Qa(harness.deps);

    expect(result.status).toBe('passed');
    expect(harness.executed).toHaveLength(8);
    expect(harness.sessions).toHaveLength(8);
    expect(harness.sessions.every(session => session.diffs.length === 1 && session.diffs[0]?.passed)).toBe(true);
    expect(harness.sessions.flatMap(session => session.diffs[0]?.expectedChanges.map(change => change.path) ?? []))
      .toEqual(Array.from({length: 8}, () => ['profiles.B.name', 'profiles.B.searchSuggestionsEnabled']).flat());
    expect(harness.cleanupRuns()).toBe(1);
    expect(harness.sessions.every(session => session.receipts.length === 1)).toBe(true);
  });

  for (const {scope, path, mutate} of forbiddenCases) {
    test(`fails closed on a concrete ${scope} mutation and stops later scenarios`, async () => {
      const harness = createHarness({mutateAfter: mutate});

      const result = await runTask12Qa(harness.deps);

      expectFirstScenarioFailed(result, harness.executed);
      expect(harness.sessions[0]?.diffs[0]?.forbiddenChanges).toContainEqual(expect.objectContaining({path}));
      expect(harness.cleanupRuns()).toBe(1);
      expect(harness.sessions[0]?.receipts).toHaveLength(1);
    });
  }

  for (const phase of ['before', 'after'] as const) {
    test(`fails closed when the ${phase} capture is missing a driver-gated observation`, async () => {
      const harness = createHarness({
        mutateCapture: (value, capturePhase) => {
          if (capturePhase === phase) value.focusedWindowId = {status: 'missing', reason: `${phase} capture omitted focus`};
        },
      });

      const result = await runTask12Qa(harness.deps);

      expect(result).toEqual({status: 'blocked-missing-observable', missing: ['focused-window']});
      expect(harness.executed).toHaveLength(phase === 'before' ? 0 : 1);
      expect(harness.sessions[0]?.diffs).toHaveLength(0);
      expect(harness.cleanupRuns()).toBe(1);
      expect(harness.sessions[0]?.receipts).toHaveLength(1);
    });

    test(`reaches the real state-core missingObservations guard from the ${phase} capture`, async () => {
      const harness = createHarness({
        mutateCapture: (value, capturePhase) => {
          if (capturePhase === phase) value.globalState = {status: 'unavailable', reason: `${phase} backend snapshot unavailable`};
        },
      });

      const result = await runTask12Qa(harness.deps);

      expectFirstScenarioFailed(result, harness.executed);
      expect(harness.sessions[0]?.diffs[0]).toMatchObject({
        passed: false,
        missingObservations: [{
          path: `${phase}.globalState`,
          status: 'unavailable',
          reason: `${phase} backend snapshot unavailable`,
        }],
      });
      expect(harness.cleanupRuns()).toBe(1);
      expect(harness.sessions[0]?.receipts).toHaveLength(1);
    });
  }
});
