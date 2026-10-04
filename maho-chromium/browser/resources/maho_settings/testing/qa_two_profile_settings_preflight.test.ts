import {describe, expect, test} from 'bun:test';

import {
  MISSING_RUNTIME_BINDING,
  defaultTask12RuntimeDependencies,
  runTask12Qa,
  type Task12DriverStatus,
  type Task12EvidenceSessionBinding,
  type Task12RuntimeDependencies,
  type Task12RuntimeProbe,
  type Task12ScenarioExecutorBinding,
} from './qa_two_profile_settings.js';

type ProbeValues = Readonly<{
  appFresh: boolean;
  cdpAvailable: boolean;
  relayAvailable: boolean;
  runtimeBindingAvailable: boolean;
}>;

type Calls = {
  evidence: number;
  create: number;
  cleanup: number;
  register: number;
  capture: number;
  mutation: number;
};

type MockRuntime = {
  readonly deps: Task12RuntimeDependencies;
  readonly calls: Calls;
};

const READY_PROBE: ProbeValues = {
  appFresh: true,
  cdpAvailable: true,
  relayAvailable: true,
  runtimeBindingAvailable: true,
};

function createMockRuntime(probeValues: Partial<ProbeValues> = {}): MockRuntime {
  const values = {...READY_PROBE, ...probeValues};
  const calls: Calls = {
    evidence: 0,
    create: 0,
    cleanup: 0,
    register: 0,
    capture: 0,
    mutation: 0,
  };
  const probe: Task12RuntimeProbe = {
    appFresh: async () => values.appFresh,
    cdpAvailable: async () => values.cdpAvailable,
    relayAvailable: async () => values.relayAvailable,
    runtimeBindingAvailable: async () => values.runtimeBindingAvailable,
  };
  const evidenceSession: Task12EvidenceSessionBinding = {
    runDirectory: '/unused/task12-evidence',
    async appendEvent() {
      calls.evidence++;
    },
    async writeJson() {
      calls.evidence++;
    },
    async writeStateArtifacts() {
      calls.evidence++;
      return {diff: {passed: true, forbiddenChanges: [], missingObservations: []}};
    },
    async verifyRequiredArtifacts() {
      calls.evidence++;
      return {passed: true, missing: []};
    },
  };
  const runtimeScenarioExecutor: Task12ScenarioExecutorBinding = {
    async availability() {
      return {status: 'available'};
    },
    async observableSeams() {
      return {
        activeMahoProfile: {status: 'available', source: 'mock'},
        focusedWindow: {status: 'available', source: 'mock'},
        selectedSpace: {status: 'available', source: 'mock'},
        backendGlobalValues: {status: 'available', source: 'mock'},
        spaceAssignments: {status: 'available', source: 'mock'},
        fullPassageAvailable: true,
      };
    },
    async execute() {
      calls.mutation++;
      throw new Error('mutation must not run during preflight');
    },
  };
  const deps: Task12RuntimeDependencies = {
    probe,
    profileObservablesSnapshotReader: {
      getProfileObservablesSnapshot: async () => ({snapshot: null}),
    },
    runtimeScenarioExecutor,
    scenarioRequests: {} as NonNullable<Task12RuntimeDependencies['scenarioRequests']>,
    async createEvidenceSession() {
      calls.create++;
      return evidenceSession;
    },
    async captureState() {
      calls.capture++;
      throw new Error('capture must not run during preflight');
    },
    expectedChanges() {
      calls.evidence++;
      throw new Error('state-diff evidence must not run during preflight');
    },
    registerCleanupOwnership() {
      calls.register++;
    },
    signalHooks: {
      onSigint() {
        calls.cleanup++;
        return () => {
          calls.cleanup++;
        };
      },
    },
  };
  return {deps, calls};
}

function expectNoRunSideEffects(calls: Calls): void {
  expect(calls).toEqual({
    evidence: 0,
    create: 0,
    cleanup: 0,
    register: 0,
    capture: 0,
    mutation: 0,
  });
}

const preflightCases: ReadonlyArray<{
  readonly name: string;
  readonly probe: Partial<ProbeValues>;
  readonly expected: Task12DriverStatus;
}> = [
  {
    name: 'stale app',
    probe: {appFresh: false},
    expected: {status: 'blocked-current-app', reasons: ['STALE_APP']},
  },
  {
    name: 'missing CDP',
    probe: {cdpAvailable: false},
    expected: {status: 'blocked-current-app', reasons: ['CDP_9222_UNAVAILABLE']},
  },
  {
    name: 'missing relay',
    probe: {relayAvailable: false},
    expected: {status: 'blocked-current-app', reasons: ['RELAY_18765_UNAVAILABLE']},
  },
  {
    name: 'missing runtime binding',
    probe: {runtimeBindingAvailable: false},
    expected: {status: 'missing-runtime-binding', reason: MISSING_RUNTIME_BINDING},
  },
];

describe('runTask12Qa preflight', () => {
  for (const entry of preflightCases) {
    test(`returns the exact status for ${entry.name} without starting a run`, async () => {
      const runtime = createMockRuntime(entry.probe);

      expect(await runTask12Qa(runtime.deps)).toEqual(entry.expected);
      expectNoRunSideEffects(runtime.calls);
    });
  }

  for (const dependency of ['runtimeScenarioExecutor', 'scenarioRequests', 'captureState', 'expectedChanges', 'createEvidenceSession'] as const) {
    test(`returns MISSING_RUNTIME_BINDING when ${dependency} is absent`, async () => {
      const runtime = createMockRuntime();
      const deps: Task12RuntimeDependencies = {...runtime.deps, [dependency]: undefined};

      expect(await runTask12Qa(deps)).toEqual({
        status: 'missing-runtime-binding',
        reason: MISSING_RUNTIME_BINDING,
      });
      expectNoRunSideEffects(runtime.calls);
    });
  }

  test('the default probe is structurally blocked on the current app and creates no artifacts', async () => {
    const result = await runTask12Qa(defaultTask12RuntimeDependencies);

    expect(result).toEqual({
      status: 'blocked-current-app',
      reasons: ['STALE_APP', 'CDP_9222_UNAVAILABLE', 'RELAY_18765_UNAVAILABLE'],
    });
    expect('scenarios' in result).toBe(false);
  });
});
