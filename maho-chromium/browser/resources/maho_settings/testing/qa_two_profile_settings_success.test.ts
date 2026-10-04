import {afterEach, describe, expect, test} from 'bun:test';
import {mkdtemp, readFile, readdir, rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join} from 'node:path';

import {
  createTask12EvidenceFactory,
  runTask12Qa,
  task12DriverPlan,
  type Task12CaptureResult,
  type Task12EvidenceSessionBinding,
  type Task12RuntimeDependencies,
} from './qa_two_profile_settings.js';
import {
  TASK12_RUNTIME_ARTIFACTS,
  Task12EvidenceSession,
  type Task12ProfileObservablesSnapshotReader,
} from './task12_runtime_evidence.js';

type RuntimeExecutor = NonNullable<Task12RuntimeDependencies['runtimeScenarioExecutor']>;
type ScenarioRequests = Parameters<RuntimeExecutor['execute']>[1];
type ScenarioExecution = Awaited<ReturnType<RuntimeExecutor['execute']>>;
type Availability = Awaited<ReturnType<RuntimeExecutor['availability']>>;
type ObservableSeams = Awaited<ReturnType<RuntimeExecutor['observableSeams']>>;
type ExpectedChanges = ReturnType<NonNullable<Task12RuntimeDependencies['expectedChanges']>>;
type Snapshot = Task12CaptureResult['snapshot'];
type ScenarioId = Parameters<RuntimeExecutor['execute']>[0];

const scenarioIds = [
  'happy-b-only-mutation',
  'failure-unknown-target',
  'failure-deleting-target',
  'failure-stale-target',
  'failure-unsupported-sensitive-pane',
  'lifecycle-create',
  'lifecycle-safe-delete-cancel',
  'cleanup',
] as const satisfies readonly ScenarioId[];

const roots: string[] = [];
const PNG = Uint8Array.from([137, 80, 78, 71, 13, 10, 26, 10, 0]);
const profileObservablesSnapshotReader: Task12ProfileObservablesSnapshotReader = {
  getProfileObservablesSnapshot: async () => ({
    snapshot: {
      activeBrowserProfileId: 'browser-a',
      activeMahoProfileId: 'maho-a',
      registryRevision: '1',
      lifecycleEntries: [
        {profileId: 'a', lifecycleState: 1},
        {profileId: 'b', lifecycleState: 1},
      ],
      pendingDeletionProfileIds: [],
      deletedHistoryAvailability: 0,
    },
  }),
};

afterEach(async () => {
  await Promise.all(roots.splice(0).map(root => rm(root, {recursive: true, force: true})));
});

const requiredArtifacts = [
  'before.json',
  'after.json',
  'state-diff.json',
  'events.jsonl',
  'manifest.json',
  'cleanup-receipt.json',
  'dom-before.txt',
  'dom-after.txt',
  'screenshot-before.png',
  'screenshot-after.png',
] as const;

function observedSnapshot(profileBRevision: number): Snapshot {
  return {
    focusedWindowId: {status: 'observed', value: 'window-1'},
    selectedSpace: {status: 'observed', value: 'space-b'},
    spaceAssignments: {status: 'observed', value: {windowIds: ['window-1']}},
    profileB: {id: 'profile-b', revision: profileBRevision},
  } as unknown as Snapshot;
}

function completeSnapshot(): Snapshot {
  const observed = <T>(value: T) => ({status: 'observed' as const, value});
  return {
    timestamp: '2026-08-01T00:00:00.000Z',
    activeBrowserProfileId: observed('browser-a'), activeMahoProfileId: observed('maho-a'),
    focusedWindowId: observed('window-1'), selectedSpace: observed({id: 'space-b', name: 'Work'}),
    spaceAssignments: observed({tabIds: [], windowIds: ['window-1']}),
    profiles: {
      A: observed({id: 'a', name: 'A', avatarColor: '#111111', homepageUrl: 'https://a.example', searchEngineId: 'a', searchSuggestionsEnabled: true, downloadPrompt: false, downloadDirectoryToken: 'a', archiveTimeoutHours: 24}),
      B: observed({id: 'b', name: 'B', avatarColor: '#222222', homepageUrl: 'https://b.example', searchEngineId: 'b', searchSuggestionsEnabled: false, downloadPrompt: true, downloadDirectoryToken: 'b', archiveTimeoutHours: 72}),
    },
    globalState: observed({account: {}, process: {}, coreGlobal: {}}),
    lifecycle: observed({
      profiles: {A: 'ready', B: 'ready'},
      deletedProfileIds: {status: 'unavailable', reason: 'Deleted profile history is unavailable'},
      pendingDeletionProfileIds: [],
    }),
  };
}

function browser(windowId = 'window-1') {
  const observed = <T>(value: T) => ({status: 'observed' as const, value});
  return {
    url: observed('chrome://maho-settings/'),
    title: observed('Settings'),
    focusedTarget: observed(true),
    target: observed({targetId: 'target-settings', type: 'page', url: 'chrome://maho-settings/', title: 'Settings'}),
    window: observed({windowId, bounds: {left: 0, top: 0, width: 1280, height: 800}}),
    selectedPane: observed('Profiles'),
    selectedTarget: observed({label: 'Work'}),
  };
}

function availableSeams(): ObservableSeams {
  return {
    focusedWindow: {status: 'available', source: 'mock-focused-window'},
    selectedSpace: {status: 'available', source: 'mock-selected-space'},
    spaceAssignments: {status: 'available', source: 'mock-space-assignments'},
  } as ObservableSeams;
}

function readyProbe(): Task12RuntimeDependencies['probe'] {
  return {
    appFresh: async () => true,
    cdpAvailable: async () => true,
    relayAvailable: async () => true,
    runtimeBindingAvailable: async () => true,
  };
}

describe('runTask12Qa ready mocked path', () => {
  test('produces and verifies the exact ten-file registry through real evidence sessions', async () => {
    const root = await mkdtemp(join(tmpdir(), 'task12-driver-'));
    roots.push(root);
    const before = completeSnapshot();
    const executor: RuntimeExecutor = {
      availability: async () => ({status: 'available'} as Availability),
      observableSeams: async () => availableSeams(),
      execute: async () => ({status: 'passed'} as ScenarioExecution),
    };

    const result = await runTask12Qa({
      probe: readyProbe(),
      profileObservablesSnapshotReader,
      runtimeScenarioExecutor: executor,
      scenarioRequests: Object.freeze({}) as ScenarioRequests,
      createEvidenceSession: createTask12EvidenceFactory({rootDirectory: root, runId: 'verified'}),
      captureState: async (_scenario, phase, binding) => {
        const session = binding as Task12EvidenceSession;
        await session.writeText(`dom-${phase}.txt`, phase);
        await session.writePng(`screenshot-${phase}.png`, PNG);
        return {
          snapshot: structuredClone(before),
          browser: browser(),
          dom: {html: `<main>${phase}</main>`},
        } as unknown as Task12CaptureResult;
      },
      expectedChanges: scenario => ({scenario, profileB: {}, allowedCleanup: []}),
      signalHooks: {onSigint: () => () => undefined},
      now: () => '2026-08-01T00:00:00.000Z',
    });

    expect(result).toEqual({status: 'passed', scenarios: [...scenarioIds]});
    for (const scenario of scenarioIds) {
      const directory = join(root, `${scenario}--verified`);
      expect((await readdir(directory)).sort()).toEqual([...TASK12_RUNTIME_ARTIFACTS].sort());
      const manifest = JSON.parse(await readFile(join(directory, 'manifest.json'), 'utf8'));
      const entries = manifest.entries.filter((entry: {scenarioId: ScenarioId}) => entry.scenarioId === scenario);
      expect(entries.length).toBeGreaterThan(0);
      expect(entries.every((entry: {status: string}) => entry.status === 'pass')).toBe(true);
    }
  });

  test('runs the exact plan, requests all evidence, permits a B-only change, and writes cleanup receipts last', async () => {
    expect(task12DriverPlan.scenarios).toHaveLength(8);
    expect(task12DriverPlan.scenarios.map(({id}) => id)).toEqual([...scenarioIds]);

    for (const scenario of task12DriverPlan.scenarios) {
      expect(Object.keys(scenario.artifacts)).toEqual([...requiredArtifacts]);
      expect(Object.values(scenario.artifacts)).toEqual(requiredArtifacts.map(artifact => `write:${artifact}`));
      expect(scenario.execution).toEqual(scenario.operations.map(operation => operation.id));
      expect(scenario.operations.filter(operation => operation.kind === 'write-artifact').map(operation => operation.artifact))
        .toEqual([...requiredArtifacts]);
    }

    expect(task12DriverPlan.cleanupHooks.failure.registered).toBe(true);
    expect(task12DriverPlan.cleanupHooks.sigint.registered).toBe(true);

    const timeline: string[] = [];
    const executed: ScenarioId[] = [];
    const executionRequests: ScenarioRequests[] = [];
    const sessions = new Map<ScenarioId, string[]>();
    const scenarioRequests = Object.freeze({}) as ScenarioRequests;

    const executor: RuntimeExecutor = {
      availability: async () => ({status: 'available'} as Availability),
      observableSeams: async () => availableSeams(),
      execute: async (scenario, requests) => {
        timeline.push(`mutate:${scenario}`);
        executed.push(scenario);
        executionRequests.push(requests);
        return {status: 'passed'} as ScenarioExecution;
      },
    };

    const createEvidenceSession: NonNullable<Task12RuntimeDependencies['createEvidenceSession']> = async scenario => {
      const writes: string[] = [];
      sessions.set(scenario, writes);
      const session: Task12EvidenceSessionBinding = {
        runDirectory: `/mock/${scenario}`,
        appendEvent: async event => {
          writes.push(`events:${String(event.kind)}`);
        },
        writeJson: async artifact => {
          writes.push(artifact);
          timeline.push(`artifact:${scenario}:${artifact}`);
        },
        writeStateArtifacts: async (before, after, _expected) => {
          expect((before.snapshot as unknown as {profileB: {revision: number}}).profileB.revision).toBe(1);
          expect((after.snapshot as unknown as {profileB: {revision: number}}).profileB.revision).toBe(2);
          expect(before.browser).not.toBe(after.browser);
          writes.push('before.json', 'dom-before.txt', 'screenshot-before.png');
          writes.push('after.json', 'dom-after.txt', 'screenshot-after.png');
          writes.push('state-diff.json');
          return {diff: {passed: true, forbiddenChanges: [], missingObservations: []}};
        },
        finalizeManifest: async () => { writes.push('manifest.json'); },
        verifyRequiredArtifacts: async () => {
          timeline.push(`verify:${scenario}`);
          return {passed: true, missing: []};
        },
      };
      return session;
    };

    const captureState: NonNullable<Task12RuntimeDependencies['captureState']> = async (scenario, phase) => {
      timeline.push(`capture:${scenario}:${phase}`);
      const writes = sessions.get(scenario);
      expect(writes).toBeDefined();
      writes!.push(`${phase}.json`, `dom-${phase}.txt`, `screenshot-${phase}.png`);
      return {
        snapshot: observedSnapshot(phase === 'before' ? 1 : 2),
        browser: browser(),
        dom: {html: `<main data-phase="${phase}"></main>`},
      } as unknown as Task12CaptureResult;
    };

    const expectedChanges: NonNullable<Task12RuntimeDependencies['expectedChanges']> = (_scenario, before, after) => {
      const beforeB = (before as unknown as {profileB: {revision: number}}).profileB.revision;
      const afterB = (after as unknown as {profileB: {revision: number}}).profileB.revision;
      expect(afterB).toBe(beforeB + 1);
      return {} as ExpectedChanges;
    };

    const signalHooks: NonNullable<Task12RuntimeDependencies['signalHooks']> = {
      onSigint: _listener => {
        timeline.push('register:sigint');
        return () => timeline.push('unregister:sigint');
      },
    };

    const result = await runTask12Qa({
      probe: readyProbe(),
      profileObservablesSnapshotReader,
      runtimeScenarioExecutor: executor,
      scenarioRequests,
      createEvidenceSession,
      captureState,
      expectedChanges,
      signalHooks,
      registerCleanupOwnership: async _register => {
        timeline.push('register:cleanup');
      },
      now: () => '2026-08-01T00:00:00.000Z',
    });

    expect(result).toEqual({status: 'passed', scenarios: [...scenarioIds]});
    expect(executed).toEqual([...scenarioIds]);
    expect(executionRequests).toHaveLength(8);
    expect(executionRequests.every(requests => requests === scenarioRequests)).toBe(true);

    const firstMutation = timeline.findIndex(entry => entry.startsWith('mutate:'));
    expect(timeline.indexOf('register:sigint')).toBeLessThan(firstMutation);
    expect(timeline.indexOf('register:cleanup')).toBeLessThan(firstMutation);

    for (const scenario of scenarioIds) {
      const writes = sessions.get(scenario);
      expect(writes).toBeDefined();
      expect(writes).toContain('events:scenario-start');
      expect(writes).toContain('before.json');
      expect(writes).toContain('dom-before.txt');
      expect(writes).toContain('screenshot-before.png');
      expect(writes).toContain('after.json');
      expect(writes).toContain('dom-after.txt');
      expect(writes).toContain('screenshot-after.png');
      expect(writes).toContain('state-diff.json');
      expect(writes).toContain('manifest.json');
      expect(writes!.indexOf('cleanup-receipt.json')).toBeLessThan(writes!.indexOf('manifest.json'));
    }
  });

  for (const [label, unavailableReport, expectedMissing] of [
    [
      'focused-window',
      {focusedWindow: {status: 'unavailable', reason: 'missing'}, selectedSpace: {status: 'available', source: 'mock'}, spaceAssignments: {status: 'available', source: 'mock'}},
      ['focused-window'],
    ],
    [
      'selected-Space',
      {focusedWindow: {status: 'available', source: 'mock'}, selectedSpace: {status: 'unavailable', reason: 'missing'}, spaceAssignments: {status: 'available', source: 'mock'}},
      ['selected-space'],
    ],
    [
      'assignments',
      {focusedWindow: {status: 'available', source: 'mock'}, selectedSpace: {status: 'available', source: 'mock'}, spaceAssignments: {status: 'unavailable', reason: 'missing'}},
      ['space-assignments'],
    ],
  ] as const) {
    test(`blocks final passage when ${label} is missing`, async () => {
      let mutations = 0;
      const executor: RuntimeExecutor = {
        availability: async () => ({status: 'available'} as Availability),
        observableSeams: async () => unavailableReport as ObservableSeams,
        execute: async () => {
          mutations += 1;
          return {status: 'passed'} as ScenarioExecution;
        },
      };

      const result = await runTask12Qa({
        probe: readyProbe(),
        profileObservablesSnapshotReader,
        runtimeScenarioExecutor: executor,
        scenarioRequests: Object.freeze({}) as ScenarioRequests,
        createEvidenceSession: async () => {
          throw new Error('evidence session must not be created when an observable seam is missing');
        },
        captureState: async () => {
          throw new Error('state must not be captured when an observable seam is missing');
        },
        expectedChanges: () => {
          throw new Error('expected changes must not be computed when an observable seam is missing');
        },
      });

      expect(result).toEqual({status: 'blocked-missing-observable', missing: expectedMissing});
      expect(mutations).toBe(0);
    });
  }
});
