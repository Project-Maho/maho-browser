import {strict as assert} from 'node:assert';
import test from 'node:test';

import {
  parseProfileScenarioContext,
  ProfileScenarioError,
  runProfileScenarios,
  serializeProfileScenarioContext,
  type ProfileScenarioHost,
  type ProfileScenarioSeed,
  type ProfileScenarioSnapshot,
} from './task12_profile_scenarios.js';

const snapshot = (overrides: Partial<ProfileScenarioSnapshot> = {}): ProfileScenarioSnapshot => ({
  targetToken: 'target-7',
  contextRevision: 11n,
  profileRevision: 19n,
  profiles: [],
  selectedProfileId: null,
  ready: true,
  ...overrides,
});

const deferred = <T>() => {
  let resolve!: (value: T) => void;
  let reject!: (reason?: unknown) => void;
  const promise = new Promise<T>((next, fail) => { resolve = next; reject = fail; });
  return {promise, reject, resolve};
};

const makeHost = (initial = snapshot()) => {
  let current = structuredClone(initial);
  const creates: Array<{name: string; metadata: unknown}> = [];
  const registrations: Array<{kind: string; id: string; name: string}> = [];
  const reads: string[] = [];
  const host: ProfileScenarioHost = {
    snapshot: async () => structuredClone(current),
    listProfileNames: async () => current.profiles.map(profile => profile.name),
    createProfile: async (name, metadata) => {
      creates.push({name, metadata: structuredClone(metadata)});
      const id = `id-${creates.length}`;
      current.profiles.push({id, name});
      return id;
    },
    registerCreatedProfile: (kind, id, name) => registrations.push({kind, id, name}),
    seedProfile: async () => {},
    readProfile: async id => {
      reads.push(id);
      return {id};
    },
    deleteProfile: undefined,
    restoreSnapshot: async next => { current = structuredClone(next); },
  };
  return {host, creates, registrations, reads, get current() { return current; }};
};

test('awaits an asynchronously resolved snapshot before reading profile names', async () => {
  const snapshotRequested = deferred<void>();
  const snapshotResult = deferred<ProfileScenarioSnapshot>();
  let listCalls = 0;
  const fake = makeHost();
  fake.host.snapshot = async () => {
    snapshotRequested.resolve();
    return snapshotResult.promise;
  };
  fake.host.listProfileNames = async () => {
    listCalls++;
    return [];
  };

  const run = runProfileScenarios(fake.host, {runId: 'async-snapshot'});
  await snapshotRequested.promise;
  assert.equal(listCalls, 0);
  snapshotResult.resolve(snapshot());
  await run;
  assert.equal(listCalls, 1);
});

test('validated runId creates four collision-free names without a clock', async () => {
  const fake = makeHost();
  const originalNow = Date.now;
  Date.now = () => { throw new Error('Date.now must not be used'); };
  try {
    const result = await runProfileScenarios(fake.host, {runId: 'Run_12-abc'});
    assert.deepEqual(result.names, [
      'maho-task12-Run_12-abc-a',
      'maho-task12-Run_12-abc-b',
      'maho-task12-Run_12-abc-disposable',
      'maho-task12-Run_12-abc-lifecycle',
    ]);
    assert.equal(new Set(result.names).size, 4);
  } finally {
    Date.now = originalNow;
  }
});

test('an existing-name collision rejects before any create', async () => {
  const fake = makeHost(snapshot({profiles: [{id: 'old', name: 'maho-task12-r4-b'}]}));
  await assert.rejects(
      runProfileScenarios(fake.host, {runId: 'r4'}),
      (error: unknown) => error instanceof ProfileScenarioError && error.code === 'PROFILE_NAME_COLLISION');
  assert.deepEqual(fake.creates, []);
});

test('B, disposable, and lifecycle identities register immediately in creation order', async () => {
  const fake = makeHost();
  const result = await runProfileScenarios(fake.host, {runId: 'order'});
  assert.deepEqual(fake.registrations, [
    {kind: 'A', id: result.ids.A, name: result.names[0]},
    {kind: 'B', id: result.ids.B, name: result.names[1]},
    {kind: 'disposable', id: result.ids.disposable, name: result.names[2]},
    {kind: 'lifecycle', id: result.ids.lifecycle, name: result.names[3]},
  ]);
});

test('A and B seed/readback are deliberately distinct across all scenario fields', async () => {
  const fake = makeHost();
  const seeds = new Map<string, ProfileScenarioSeed>();
  fake.host.seedProfile = async (id, seed) => { seeds.set(id, structuredClone(seed)); };
  fake.host.readProfile = async id => structuredClone(seeds.get(id));
  const result = await runProfileScenarios(fake.host, {runId: 'distinct'});
  const a = result.readbacks.A;
  const b = result.readbacks.B;
  for (const field of ['metadata', 'homepage', 'searchKeyword', 'suggestions', 'download', 'archive'] as const) {
    assert.notDeepEqual(a[field], b[field], `${field} must differ`);
  }
});

test('stale request captures the real revisions and rejects when a newer context does not advance', async () => {
  const fake = makeHost();
  let captured: unknown;
  fake.host.assertRequestCurrent = async request => { captured = structuredClone(request); };
  fake.host.snapshot = async () => snapshot({targetToken: 'target-7', contextRevision: 11n, profileRevision: 19n});
  fake.host.readProfile = async id => ({id});
  await assert.rejects(
      runProfileScenarios(fake.host, {runId: 'stale', newerContext: snapshot({
        targetToken: 'target-8', contextRevision: 11n, profileRevision: 19n,
      })}),
      (error: unknown) => error instanceof ProfileScenarioError && error.code === 'STALE_PROFILE_REQUEST');
  assert.deepEqual(captured, {targetToken: 'target-7', contextRevision: 11n, profileRevision: 19n});
});

test('awaits asynchronous currentness before a subsequent lifecycle mutation', async () => {
  const fake = makeHost();
  const currentnessRequested = deferred<void>();
  const currentnessResult = deferred<void>();
  const ordering: string[] = [];
  fake.host.assertRequestCurrent = async () => {
    ordering.push('currentness:start');
    currentnessRequested.resolve();
    await currentnessResult.promise;
    ordering.push('currentness:done');
  };
  fake.host.deleteProfile = async () => { ordering.push('delete'); };

  const run = runProfileScenarios(fake.host, {
    runId: 'async-currentness',
    deleteLifecycle: true,
  });
  await currentnessRequested.promise;
  assert.deepEqual(ordering, ['currentness:start']);
  currentnessResult.resolve();
  await run;
  assert.deepEqual(ordering, ['currentness:start', 'currentness:done', 'delete']);
});

test('serialized revisions beyond 2^53 parse and serialize without precision loss', () => {
  const serialized = {
    targetToken: 'target-lossless',
    contextRevision: '900719925474099312345678901234567890',
    profileRevision: '18446744073709551615',
  };
  const parsed = parseProfileScenarioContext(serialized);
  assert.deepEqual(parsed, {
    targetToken: serialized.targetToken,
    contextRevision: 900719925474099312345678901234567890n,
    profileRevision: 18446744073709551615n,
  });
  assert.deepEqual(serializeProfileScenarioContext(parsed), serialized);
});

test('serialized revisions reject non-canonical external values', () => {
  const malformed = [
    '-1', '1.5', '1e3', ' 1', '1 ', '+1', '01', '', '0x10', 'Infinity', 'NaN',
  ];
  for (const revision of malformed) {
    assert.throws(
        () => parseProfileScenarioContext({
          targetToken: 'target-7',
          contextRevision: revision,
          profileRevision: '1',
        }),
        {name: 'TypeError'},
        `context revision ${JSON.stringify(revision)} must be rejected`);
    assert.throws(
        () => parseProfileScenarioContext({
          targetToken: 'target-7',
          contextRevision: '1',
          profileRevision: revision,
        }),
        {name: 'TypeError'},
        `profile revision ${JSON.stringify(revision)} must be rejected`);
  }
  assert.equal(parseProfileScenarioContext({
    targetToken: 'target-7', contextRevision: '0', profileRevision: '0',
  }).contextRevision, 0n);
});

test('captured request preserves exact token and revisions after host state changes', async () => {
  const hugeContextRevision = 900719925474099312345678901234567890n;
  const hugeProfileRevision = 900719925474099398765432109876543210n;
  const fake = makeHost(snapshot({
    targetToken: 'captured-token',
    contextRevision: hugeContextRevision,
    profileRevision: hugeProfileRevision,
  }));
  let captured: unknown;
  fake.host.assertRequestCurrent = async request => { captured = structuredClone(request); };
  fake.host.listProfileNames = async () => {
    await fake.host.restoreSnapshot(snapshot({
      targetToken: 'replacement-token',
      contextRevision: hugeContextRevision + 100n,
      profileRevision: hugeProfileRevision + 100n,
    }));
    return [];
  };
  await runProfileScenarios(fake.host, {runId: 'capture'});
  assert.deepEqual(captured, {
    targetToken: 'captured-token',
    contextRevision: hugeContextRevision,
    profileRevision: hugeProfileRevision,
  });
});

test('replacement context must advance both lossless revision axes strictly', async () => {
  const base = snapshot({
    contextRevision: 900719925474099300000000000000000000n,
    profileRevision: 900719925474099400000000000000000000n,
  });
  const rejected = [
    snapshot({...base, contextRevision: base.contextRevision, profileRevision: base.profileRevision}),
    snapshot({...base, contextRevision: base.contextRevision + 1n, profileRevision: base.profileRevision}),
    snapshot({...base, contextRevision: base.contextRevision, profileRevision: base.profileRevision + 1n}),
    snapshot({...base, contextRevision: base.contextRevision - 1n, profileRevision: base.profileRevision + 1n}),
    snapshot({...base, contextRevision: base.contextRevision + 1n, profileRevision: base.profileRevision - 1n}),
  ];
  for (let index = 0; index < rejected.length; index++) {
    await assert.rejects(
        runProfileScenarios(makeHost(base).host, {
          runId: `advance-rejected-${index}`,
          newerContext: rejected[index],
        }),
        (error: unknown) => error instanceof ProfileScenarioError && error.code === 'STALE_PROFILE_REQUEST');
  }
  await runProfileScenarios(makeHost(base).host, {
    runId: 'advance-accepted',
    newerContext: snapshot({
      ...base,
      contextRevision: base.contextRevision + 1n,
      profileRevision: base.profileRevision + 1n,
    }),
  });
});

test('unsupported deletion returns typed MISSING_DELETING_LIFECYCLE_API', async () => {
  const fake = makeHost();
  await assert.rejects(
      runProfileScenarios(fake.host, {runId: 'delete', deleteLifecycle: true}),
      (error: unknown) => error instanceof ProfileScenarioError && error.code === 'MISSING_DELETING_LIFECYCLE_API');
});

test('cancel restores the exact snapshot and leaves it ready and reselectable', async () => {
  const before = snapshot({
    profiles: [{id: 'original', name: 'Original'}],
    selectedProfileId: 'original',
  });
  const fake = makeHost(before);
  await runProfileScenarios(fake.host, {runId: 'cancel', cancel: true});
  assert.deepEqual(fake.current, before);
  assert.equal(fake.current.ready, true);
  assert.ok(fake.current.profiles.some(profile => profile.id === fake.current.selectedProfileId));
});

test('cancel awaits asynchronous snapshot restoration before resolving', async () => {
  const fake = makeHost();
  const restoreRequested = deferred<void>();
  const restoreResult = deferred<void>();
  let resolved = false;
  fake.host.restoreSnapshot = async () => {
    restoreRequested.resolve();
    await restoreResult.promise;
  };

  const run = runProfileScenarios(fake.host, {runId: 'async-rollback', cancel: true});
  void run.then(() => { resolved = true; });
  await restoreRequested.promise;
  assert.equal(resolved, false);
  restoreResult.resolve();
  await run;
  assert.equal(resolved, true);
});

test('cancel awaits and preserves asynchronous restoration failure', async () => {
  const fake = makeHost();
  const restoreRequested = deferred<void>();
  const restoreResult = deferred<void>();
  const rollbackError = new Error('restore failed');
  let rejected = false;
  fake.host.restoreSnapshot = async () => {
    restoreRequested.resolve();
    await restoreResult.promise;
  };

  const run = runProfileScenarios(fake.host, {runId: 'async-rollback-error', cancel: true});
  void run.catch(() => { rejected = true; });
  await restoreRequested.promise;
  assert.equal(rejected, false);
  restoreResult.reject(rollbackError);
  await assert.rejects(run, error => error === rollbackError);
  assert.equal(rejected, true);
});
