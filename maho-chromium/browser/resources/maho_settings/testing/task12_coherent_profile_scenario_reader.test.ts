import assert from 'node:assert/strict';
import test from 'node:test';

import type {
  ProfileInfo,
  ProfileTarget,
  SelectedProfileContext,
} from '../maho_settings.mojom-webui.js';
import {
  type CapturedSelectedProfileStoreState,
  type CoherentProfileScenarioReaderDependencies,
  CoherentProfileScenarioSnapshotUnavailableError,
  type ProfileObservablesDto,
  readCoherentProfileScenarioSnapshot,
} from './task12_coherent_profile_scenario_reader.js';

test('reads a coherent selected profile scenario in the required order', async () => {
  const calls: string[] = [];
  const selectedTarget: ProfileTarget = {
    profileId: 'selected-id',
    targetToken: 'selected-token',
  };
  const selectedContext: SelectedProfileContext = {
    profileId: 'selected-id',
    targetToken: 'selected-token',
    contextRevision: 11n,
    profileRevision: 22n,
    lifecycleState: 1,
    isHostProfile: false,
    isActiveMahoProfile: false,
  };
  const captured: CapturedSelectedProfileStoreState = {
    selectedProfileTarget: selectedTarget,
    selectedProfileContext: selectedContext,
    selectedProfileLoading: false,
    selectedProfileError: null,
  };
  const selectedProfile: ProfileInfo = {
    id: 'selected-id',
    name: 'Selected',
    isDefault: false,
    isActive: false,
    spaceIds: [],
  };
  const otherProfile: ProfileInfo = {
    id: 'other-id',
    name: 'Other',
    isDefault: false,
    isActive: false,
    spaceIds: [],
  };
  const observablesA: ProfileObservablesDto = {
    activeBrowserProfileId: 'unrelated-browser-id',
    activeMahoProfileId: 'unrelated-maho-id',
    registryRevision: 33n,
    lifecycleEntries: [
      {profileId: 'selected-id', lifecycleState: 1},
      {profileId: 'other-id', lifecycleState: 0},
    ],
  };
  const observablesB: ProfileObservablesDto = {
    activeBrowserProfileId: 'unrelated-browser-id',
    activeMahoProfileId: 'unrelated-maho-id',
    registryRevision: 33n,
    lifecycleEntries: [
      {profileId: 'other-id', lifecycleState: 0},
      {profileId: 'selected-id', lifecycleState: 1},
    ],
  };
  let observableReadCount = 0;

  const snapshot = await readCoherentProfileScenarioSnapshot({
    async awaitSelectedStoreState() {
      calls.push('awaitSelectedStoreState');
      return captured;
    },
    async readObservables() {
      ++observableReadCount;
      calls.push(observableReadCount === 1 ? 'readObservables(A)' : 'readObservables(B)');
      return observableReadCount === 1 ? observablesA : observablesB;
    },
    async listProfiles() {
      calls.push('listProfiles');
      return {profiles: [selectedProfile, otherProfile]};
    },
    async revalidateSelectedTarget(target) {
      calls.push('revalidateSelectedTarget');
      assert.deepEqual(target, selectedTarget);
      return {...selectedContext};
    },
    async readLiveStoreState() {
      calls.push('readLiveStoreState');
      return captured;
    },
  });

  assert.deepEqual(calls, [
    'awaitSelectedStoreState',
    'readObservables(A)',
    'listProfiles',
    'revalidateSelectedTarget',
    'readObservables(B)',
    'readLiveStoreState',
  ]);
  assert.equal(observableReadCount, 2);
  assert.deepEqual(snapshot, {
    profiles: [
      {id: 'selected-id', name: 'Selected'},
      {id: 'other-id', name: 'Other'},
    ],
    selectedProfileId: 'selected-id',
    targetToken: 'selected-token',
    contextRevision: 11n,
    profileRevision: 22n,
    ready: true,
  });
});

function makeTarget(profileId: string, targetToken: string): ProfileTarget {
  return {profileId, targetToken};
}

function makeContext(
    profileId: string,
    targetToken: string,
    contextRevision: bigint,
    profileRevision: bigint,
): SelectedProfileContext {
  return {
    profileId,
    targetToken,
    contextRevision,
    profileRevision,
    lifecycleState: 1,
    isHostProfile: false,
    isActiveMahoProfile: false,
  };
}

function makeCaptured(
    target: ProfileTarget,
    context: SelectedProfileContext,
): CapturedSelectedProfileStoreState {
  return {
    selectedProfileTarget: target,
    selectedProfileContext: context,
    selectedProfileLoading: false,
    selectedProfileError: null,
  };
}

function makeProfile(id: string, name: string): ProfileInfo {
  return {
    id,
    name,
    isDefault: false,
    isActive: false,
    spaceIds: [],
  };
}

test('restarts the complete attempt when the selected target changes at the live-state fence', async () => {
  const calls: string[] = [];
  const oldTarget = makeTarget('old-id', 'old-token');
  const oldContext = makeContext('old-id', 'old-token', 101n, 201n);
  const oldCaptured = makeCaptured(oldTarget, oldContext);
  const newTarget = makeTarget('new-id', 'new-token');
  const newContext = makeContext('new-id', 'new-token', 102n, 202n);
  const newCaptured = makeCaptured(newTarget, newContext);
  let attempt = 0;
  let observableReadCount = 0;

  const snapshot = await readCoherentProfileScenarioSnapshot({
    async awaitSelectedStoreState() {
      ++attempt;
      calls.push(`attempt${attempt}:awaitSelectedStoreState`);
      return attempt === 1 ? oldCaptured : newCaptured;
    },
    async readObservables() {
      ++observableReadCount;
      const phase = observableReadCount % 2 === 1 ? 'A' : 'B';
      calls.push(`attempt${attempt}:readObservables(${phase})`);
      return {
        activeBrowserProfileId: `browser-${attempt}`,
        activeMahoProfileId: `maho-${attempt}`,
        registryRevision: BigInt(300 + attempt),
        lifecycleEntries: [
          {profileId: attempt === 1 ? 'old-id' : 'new-id', lifecycleState: 1},
        ],
      };
    },
    async listProfiles() {
      calls.push(`attempt${attempt}:listProfiles`);
      return {profiles: [makeProfile(attempt === 1 ? 'old-id' : 'new-id', 'Selected')]};
    },
    async revalidateSelectedTarget(target) {
      calls.push(`attempt${attempt}:revalidateSelectedTarget`);
      assert.deepEqual(target, attempt === 1 ? oldTarget : newTarget);
      return attempt === 1 ? {...oldContext} : {...newContext};
    },
    async readLiveStoreState() {
      calls.push(`attempt${attempt}:readLiveStoreState`);
      return newCaptured;
    },
  });

  assert.deepEqual(calls, [
    'attempt1:awaitSelectedStoreState',
    'attempt1:readObservables(A)',
    'attempt1:listProfiles',
    'attempt1:revalidateSelectedTarget',
    'attempt1:readObservables(B)',
    'attempt1:readLiveStoreState',
    'attempt2:awaitSelectedStoreState',
    'attempt2:readObservables(A)',
    'attempt2:listProfiles',
    'attempt2:revalidateSelectedTarget',
    'attempt2:readObservables(B)',
    'attempt2:readLiveStoreState',
  ]);
  assert.equal(attempt, 2);
  assert.equal(observableReadCount, 4);
  assert.deepEqual(snapshot, {
    profiles: [{id: 'new-id', name: 'Selected'}],
    selectedProfileId: 'new-id',
    targetToken: 'new-token',
    contextRevision: 102n,
    profileRevision: 202n,
    ready: true,
  });
});

test('restarts the complete attempt when observable registry revisions differ', async () => {
  const calls: string[] = [];
  const target = makeTarget('selected-id', 'selected-token');
  const context = makeContext('selected-id', 'selected-token', 401n, 501n);
  const captured = makeCaptured(target, context);
  let attempt = 0;
  let observableReadCount = 0;

  const snapshot = await readCoherentProfileScenarioSnapshot({
    async awaitSelectedStoreState() {
      ++attempt;
      calls.push(`attempt${attempt}:awaitSelectedStoreState`);
      return captured;
    },
    async readObservables() {
      ++observableReadCount;
      const phase = observableReadCount % 2 === 1 ? 'A' : 'B';
      calls.push(`attempt${attempt}:readObservables(${phase})`);
      return {
        activeBrowserProfileId: `browser-${observableReadCount}`,
        activeMahoProfileId: `maho-${observableReadCount}`,
        registryRevision: observableReadCount === 1 ? 601n : 602n,
        lifecycleEntries: [{profileId: 'selected-id', lifecycleState: 1}],
      };
    },
    async listProfiles() {
      calls.push(`attempt${attempt}:listProfiles`);
      return {profiles: [makeProfile('selected-id', 'Selected')]};
    },
    async revalidateSelectedTarget(revalidatedTarget) {
      calls.push(`attempt${attempt}:revalidateSelectedTarget`);
      assert.deepEqual(revalidatedTarget, target);
      return {...context};
    },
    async readLiveStoreState() {
      calls.push(`attempt${attempt}:readLiveStoreState`);
      return captured;
    },
  });

  assert.deepEqual(calls, [
    'attempt1:awaitSelectedStoreState',
    'attempt1:readObservables(A)',
    'attempt1:listProfiles',
    'attempt1:revalidateSelectedTarget',
    'attempt1:readObservables(B)',
    'attempt1:readLiveStoreState',
    'attempt2:awaitSelectedStoreState',
    'attempt2:readObservables(A)',
    'attempt2:listProfiles',
    'attempt2:revalidateSelectedTarget',
    'attempt2:readObservables(B)',
    'attempt2:readLiveStoreState',
  ]);
  assert.equal(observableReadCount, 4);
  assert.deepEqual(snapshot, {
    profiles: [{id: 'selected-id', name: 'Selected'}],
    selectedProfileId: 'selected-id',
    targetToken: 'selected-token',
    contextRevision: 401n,
    profileRevision: 501n,
    ready: true,
  });
});

async function readLifecycleSnapshot(lifecycleState: number) {
  const target = makeTarget('selected-id', 'selected-token');
  const context = makeContext('selected-id', 'selected-token', 701n, 801n);
  const captured = makeCaptured(target, context);
  const observables: ProfileObservablesDto = {
    activeBrowserProfileId: 'independent-browser-id',
    activeMahoProfileId: 'independent-maho-id',
    registryRevision: 901n,
    lifecycleEntries: [{profileId: 'selected-id', lifecycleState}],
  };
  const dependencies: CoherentProfileScenarioReaderDependencies = {
    async awaitSelectedStoreState() {
      return captured;
    },
    async readObservables() {
      return observables;
    },
    async listProfiles() {
      return {profiles: [makeProfile('selected-id', 'Selected')]};
    },
    async revalidateSelectedTarget() {
      return {...context};
    },
    async readLiveStoreState() {
      return captured;
    },
  };
  return readCoherentProfileScenarioSnapshot(dependencies);
}

test('returns a coherent provisioning lifecycle snapshot as not ready', async () => {
  const snapshot = await readLifecycleSnapshot(0);

  assert.equal(snapshot.selectedProfileId, 'selected-id');
  assert.equal(snapshot.ready, false);
});

test('returns a coherent deleting lifecycle snapshot as not ready', async () => {
  const snapshot = await readLifecycleSnapshot(2);

  assert.equal(snapshot.selectedProfileId, 'selected-id');
  assert.equal(snapshot.ready, false);
});

const SNAPSHOT_UNAVAILABLE_MESSAGE =
    'Coherent selected profile scenario snapshot is unavailable or stale.';

interface RejectionDependenciesOptions {
  readonly capture: (attempt: number) => CapturedSelectedProfileStoreState;
  readonly catalogProfiles?: readonly ProfileInfo[];
  readonly lifecycleEntries?: ProfileObservablesDto['lifecycleEntries'];
}

function makeRejectionDependencies(
    calls: string[],
    captures: CapturedSelectedProfileStoreState[],
    options: RejectionDependenciesOptions,
): CoherentProfileScenarioReaderDependencies {
  let attempt = 0;
  const target = makeTarget('selected-id', 'selected-token');
  const context = makeContext('selected-id', 'selected-token', 1001n, 1002n);
  return {
    async awaitSelectedStoreState() {
      ++attempt;
      calls.push(`attempt${attempt}:awaitSelectedStoreState`);
      const captured = options.capture(attempt);
      captures.push(captured);
      return captured;
    },
    async readObservables() {
      calls.push(`attempt${attempt}:readObservables`);
      return {
        activeBrowserProfileId: 'browser-id',
        activeMahoProfileId: 'maho-id',
        registryRevision: 1003n,
        lifecycleEntries: options.lifecycleEntries ??
            [{profileId: 'selected-id', lifecycleState: 1}],
      };
    },
    async listProfiles() {
      calls.push(`attempt${attempt}:listProfiles`);
      return {
        profiles: options.catalogProfiles ?? [makeProfile('selected-id', 'Selected')],
      };
    },
    async revalidateSelectedTarget(revalidatedTarget) {
      calls.push(`attempt${attempt}:revalidateSelectedTarget`);
      assert.deepEqual(revalidatedTarget, target);
      return {...context};
    },
    async readLiveStoreState() {
      calls.push(`attempt${attempt}:readLiveStoreState`);
      return makeCaptured({...target}, {...context});
    },
  };
}

async function assertSnapshotUnavailable(
    dependencies: CoherentProfileScenarioReaderDependencies,
): Promise<void> {
  await assert.rejects(
      readCoherentProfileScenarioSnapshot(dependencies),
      error => error instanceof CoherentProfileScenarioSnapshotUnavailableError &&
          error.message === SNAPSHOT_UNAVAILABLE_MESSAGE,
  );
}

function assertFreshCaptures(captures: CapturedSelectedProfileStoreState[]): void {
  assert.equal(captures.length, 2);
  assert.notStrictEqual(captures[0], captures[1]);
}

const CAPTURE_ONLY_CALLS = [
  'attempt1:awaitSelectedStoreState',
  'attempt2:awaitSelectedStoreState',
];

test('fails closed when selectedProfileLoading stays true across both attempts', async () => {
  const calls: string[] = [];
  const captures: CapturedSelectedProfileStoreState[] = [];
  const dependencies = makeRejectionDependencies(calls, captures, {
    capture: () => ({
      ...makeCaptured(
          makeTarget('selected-id', 'selected-token'),
          makeContext('selected-id', 'selected-token', 1001n, 1002n)),
      selectedProfileLoading: true,
    }),
  });

  await assertSnapshotUnavailable(dependencies);

  assert.deepEqual(calls, CAPTURE_ONLY_CALLS);
  assertFreshCaptures(captures);
});

test('fails closed when selectedProfileError stays non-null across both attempts', async () => {
  const calls: string[] = [];
  const captures: CapturedSelectedProfileStoreState[] = [];
  const dependencies = makeRejectionDependencies(calls, captures, {
    capture: attempt => ({
      ...makeCaptured(
          makeTarget('selected-id', 'selected-token'),
          makeContext('selected-id', 'selected-token', 1001n, 1002n)),
      selectedProfileError: new Error(`persistent error ${attempt}`),
    }),
  });

  await assertSnapshotUnavailable(dependencies);

  assert.deepEqual(calls, CAPTURE_ONLY_CALLS);
  assertFreshCaptures(captures);
});

test('fails closed when selectedProfileTarget is null across both attempts', async () => {
  const calls: string[] = [];
  const captures: CapturedSelectedProfileStoreState[] = [];
  const dependencies = makeRejectionDependencies(calls, captures, {
    capture: () => ({
      selectedProfileTarget: null,
      selectedProfileContext:
          makeContext('selected-id', 'selected-token', 1001n, 1002n),
      selectedProfileLoading: false,
      selectedProfileError: null,
    }),
  });

  await assertSnapshotUnavailable(dependencies);

  assert.deepEqual(calls, CAPTURE_ONLY_CALLS);
  assertFreshCaptures(captures);
});

test('fails closed when selectedProfileContext is null across both attempts', async () => {
  const calls: string[] = [];
  const captures: CapturedSelectedProfileStoreState[] = [];
  const dependencies = makeRejectionDependencies(calls, captures, {
    capture: () => ({
      selectedProfileTarget: makeTarget('selected-id', 'selected-token'),
      selectedProfileContext: null,
      selectedProfileLoading: false,
      selectedProfileError: null,
    }),
  });

  await assertSnapshotUnavailable(dependencies);

  assert.deepEqual(calls, CAPTURE_ONLY_CALLS);
  assertFreshCaptures(captures);
});

test('fails closed when selected target and context profile IDs mismatch', async () => {
  const calls: string[] = [];
  const captures: CapturedSelectedProfileStoreState[] = [];
  const dependencies = makeRejectionDependencies(calls, captures, {
    capture: () => makeCaptured(
        makeTarget('selected-id', 'selected-token'),
        makeContext('other-id', 'selected-token', 1001n, 1002n)),
  });

  await assertSnapshotUnavailable(dependencies);

  assert.deepEqual(calls, CAPTURE_ONLY_CALLS);
  assertFreshCaptures(captures);
});

test('fails closed when selected target and context target tokens mismatch', async () => {
  const calls: string[] = [];
  const captures: CapturedSelectedProfileStoreState[] = [];
  const dependencies = makeRejectionDependencies(calls, captures, {
    capture: () => makeCaptured(
        makeTarget('selected-id', 'selected-token'),
        makeContext('selected-id', 'other-token', 1001n, 1002n)),
  });

  await assertSnapshotUnavailable(dependencies);

  assert.deepEqual(calls, CAPTURE_ONLY_CALLS);
  assertFreshCaptures(captures);
});

test('fails closed on an empty target token without revalidation', async () => {
  const calls: string[] = [];
  const captures: CapturedSelectedProfileStoreState[] = [];
  const dependencies = makeRejectionDependencies(calls, captures, {
    capture: () => makeCaptured(
        makeTarget('selected-id', ''),
        makeContext('selected-id', '', 1001n, 1002n)),
  });

  await assertSnapshotUnavailable(dependencies);

  assert.deepEqual(calls, CAPTURE_ONLY_CALLS);
  assert.equal(calls.filter(call => call.endsWith(':revalidateSelectedTarget')).length, 0);
  assertFreshCaptures(captures);
});

test('fails closed when catalog or lifecycle IDs violate uniqueness or equal-set integrity', async () => {
  const invalidTables: readonly {
    readonly name: string;
    readonly catalogProfiles: readonly ProfileInfo[];
    readonly lifecycleEntries: ProfileObservablesDto['lifecycleEntries'];
  }[] = [
    {
      name: 'duplicate catalog IDs',
      catalogProfiles: [
        makeProfile('selected-id', 'Selected'),
        makeProfile('selected-id', 'Duplicate'),
      ],
      lifecycleEntries: [{profileId: 'selected-id', lifecycleState: 1}],
    },
    {
      name: 'duplicate lifecycle IDs',
      catalogProfiles: [makeProfile('selected-id', 'Selected')],
      lifecycleEntries: [
        {profileId: 'selected-id', lifecycleState: 1},
        {profileId: 'selected-id', lifecycleState: 1},
      ],
    },
    {
      name: 'unequal catalog and lifecycle ID sets',
      catalogProfiles: [makeProfile('selected-id', 'Selected')],
      lifecycleEntries: [{profileId: 'other-id', lifecycleState: 1}],
    },
  ];
  let explicitSubcaseCount = 0;

  for (const invalidTable of invalidTables) {
    ++explicitSubcaseCount;
    const calls: string[] = [];
    const captures: CapturedSelectedProfileStoreState[] = [];
    const dependencies = makeRejectionDependencies(calls, captures, {
      capture: () => makeCaptured(
          makeTarget('selected-id', 'selected-token'),
          makeContext('selected-id', 'selected-token', 1001n, 1002n)),
      catalogProfiles: invalidTable.catalogProfiles,
      lifecycleEntries: invalidTable.lifecycleEntries,
    });

    await assertSnapshotUnavailable(dependencies);

    assert.deepEqual(calls, [
      'attempt1:awaitSelectedStoreState',
      'attempt1:readObservables',
      'attempt1:listProfiles',
      'attempt1:revalidateSelectedTarget',
      'attempt1:readObservables',
      'attempt1:readLiveStoreState',
      'attempt2:awaitSelectedStoreState',
      'attempt2:readObservables',
      'attempt2:listProfiles',
      'attempt2:revalidateSelectedTarget',
      'attempt2:readObservables',
      'attempt2:readLiveStoreState',
    ], invalidTable.name);
    assertFreshCaptures(captures);
  }

  assert.equal(explicitSubcaseCount, 3);
});
