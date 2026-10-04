import {describe, expect, test} from 'bun:test';

import {
  TASK12_CRITERIA,
  TASK12_SCENARIOS,
  buildCleanupPlan,
  buildTask12Manifest,
  diffTask12State,
  verifyCleanupReceipt,
  type CleanupInventory,
  type CleanupReceipt,
  type ManifestArtifactResults,
  type Task12ExpectedChanges,
  type Task12Snapshot,
} from './two_profile_qa_state';

const observed = <T>(value: T) => ({status: 'observed' as const, value});
const process = (pid: number, startToken: string) => ({pid, startToken});

function snapshot(): Task12Snapshot {
  return {
    timestamp: '2026-08-01T00:00:00.000Z',
    activeBrowserProfileId: observed('browser-a'),
    activeMahoProfileId: observed('maho-a'),
    focusedWindowId: observed('window-a'),
    selectedSpace: observed({id: 'space-1', name: 'Personal'}),
    spaceAssignments: observed({tabIds: ['tab-existing'], windowIds: ['window-a']}),
    profiles: {
      A: observed({id: 'profile-a', name: 'Personal', avatarColor: '#111111', homepageUrl: 'https://a.example', searchEngineId: 'engine-a', searchSuggestionsEnabled: true, downloadPrompt: false, downloadDirectoryToken: 'download-a', archiveTimeoutHours: 24}),
      B: observed({id: 'profile-b', name: 'Work', avatarColor: '#222222', homepageUrl: 'https://b.example', searchEngineId: 'engine-b', searchSuggestionsEnabled: false, downloadPrompt: true, downloadDirectoryToken: 'download-b', archiveTimeoutHours: 72}),
    },
    globalState: observed({account: {plan: 'free'}, process: {hardwareAcceleration: true}, coreGlobal: {telemetryEnabled: false}}),
    lifecycle: observed({profiles: {A: 'ready', B: 'ready'}, deletedProfileIds: observed([]), pendingDeletionProfileIds: []}),
  };
}

function expected(): Task12ExpectedChanges {
  return {scenario: 'happy-b-only-mutation', profileB: {name: {before: 'Work', after: 'Work QA'}, searchSuggestionsEnabled: {before: false, after: true}}, allowedCleanup: []};
}

function mutateB(after: Task12Snapshot): void {
  if (after.profiles.B.status === 'observed') {
    after.profiles.B.value.name = 'Work QA';
    after.profiles.B.value.searchSuggestionsEnabled = true;
  }
}

describe('diffTask12State', () => {
  test('passes a B-only mutation and reports expected changes', () => {
    const before = snapshot();
    const after = snapshot();
    mutateB(after);
    const diff = diffTask12State(before, after, expected());
    expect(diff.passed).toBe(true);
    expect(diff.expectedChanges.map(change => change.path)).toEqual(['profiles.B.name', 'profiles.B.searchSuggestionsEnabled']);
    expect(diff.forbiddenChanges).toEqual([]);
  });

  test('rejects unexpected B key additions and removals through strict runtime shape', () => {
    const added = snapshot() as unknown as {profiles: {B: {status: string; value: Record<string, unknown>}}};
    added.profiles.B.value.unapproved = true;
    expect(() => diffTask12State(snapshot(), added, expected())).toThrow(/profiles\.B.*unexpected field/i);
    const removed = snapshot() as unknown as {profiles: {B: {status: string; value: Record<string, unknown>}}};
    delete removed.profiles.B.value.homepageUrl;
    expect(() => diffTask12State(snapshot(), removed, expected())).toThrow(/profiles\.B.*homepageUrl/i);
  });

  test('structural JSON equality ignores object insertion order and preserves array order', () => {
    const before = snapshot();
    const after = snapshot();
    mutateB(after);
    if (before.globalState.status === 'observed' && after.globalState.status === 'observed') {
      before.globalState.value.account = {alpha: 1, beta: 2};
      after.globalState.value.account = {beta: 2, alpha: 1};
    }
    expect(diffTask12State(before, after, expected()).passed).toBe(true);
    if (after.spaceAssignments.status === 'observed') after.spaceAssignments.value.tabIds = ['tab-2', 'tab-1'];
    if (before.spaceAssignments.status === 'observed') before.spaceAssignments.value.tabIds = ['tab-1', 'tab-2'];
    expect(diffTask12State(before, after, expected()).forbiddenChanges)
      .toContainEqual(expect.objectContaining({path: 'spaceAssignments.tabIds'}));
  });

  test('rejects profile A, account, process, and core-global mutations', () => {
    const cases: Array<[string, (value: Task12Snapshot) => void]> = [
      ['profiles.A.name', value => { if (value.profiles.A.status === 'observed') value.profiles.A.value.name = 'Leaked'; }],
      ['globalState.account.plan', value => { if (value.globalState.status === 'observed') value.globalState.value.account.plan = 'paid'; }],
      ['globalState.process.hardwareAcceleration', value => { if (value.globalState.status === 'observed') value.globalState.value.process.hardwareAcceleration = false; }],
      ['globalState.coreGlobal.telemetryEnabled', value => { if (value.globalState.status === 'observed') value.globalState.value.coreGlobal.telemetryEnabled = true; }],
    ];
    for (const [path, mutate] of cases) {
      const after = snapshot(); mutate(after);
      expect(diffTask12State(snapshot(), after, expected()).forbiddenChanges).toContainEqual(expect.objectContaining({path}));
    }
  });

  test.each([['activeBrowserProfileId', 'browser-b'], ['activeMahoProfileId', 'maho-b'], ['focusedWindowId', 'window-b']] as const)(
    'rejects forbidden %s mutation', (key, value) => {
      const after = snapshot(); after[key] = observed(value);
      expect(diffTask12State(snapshot(), after, expected()).forbiddenChanges).toContainEqual(expect.objectContaining({path: key}));
    });

  test('rejects selected Space and assignment mutations', () => {
    const after = snapshot(); after.selectedSpace = observed({id: 'space-2', name: 'Work'});
    after.spaceAssignments = observed({tabIds: ['tab-new'], windowIds: ['window-a']});
    const paths = diffTask12State(snapshot(), after, expected()).forbiddenChanges.map(change => change.path);
    expect(paths).toContain('selectedSpace.id'); expect(paths).toContain('spaceAssignments.tabIds');
  });

  test('missing and unavailable observables cannot pass', () => {
    const before = snapshot(); const after = snapshot();
    after.globalState = {status: 'unavailable', reason: 'CDP method failed'};
    before.focusedWindowId = {status: 'missing', reason: 'capture omitted'};
    const diff = diffTask12State(before, after, expected());
    expect(diff.passed).toBe(false);
    expect(diff.missingObservations).toEqual(expect.arrayContaining([
      expect.objectContaining({path: 'before.focusedWindowId', status: 'missing'}),
      expect.objectContaining({path: 'after.globalState', status: 'unavailable'}),
    ]));
  });

  test('permits only scenario-declared cleanup deltas', () => {
    const before = snapshot(); const after = snapshot();
    if (after.lifecycle.status === 'observed') { after.lifecycle.value.profiles.B = 'deleted'; after.lifecycle.value.deletedProfileIds = observed(['profile-b']); }
    const cleanupExpected: Task12ExpectedChanges = {scenario: 'lifecycle-safe-delete-cancel', profileB: {}, allowedCleanup: [
      {path: 'lifecycle.profiles.B', before: 'ready', after: 'deleted'},
      {path: 'lifecycle.deletedProfileIds', before: [], after: ['profile-b']},
    ]};
    expect(diffTask12State(before, after, cleanupExpected)).toEqual(expect.objectContaining({passed: true, cleanupDeltas: expect.any(Array)}));
  });

  test('rejects NaN, Infinity, function, undefined, cyclic, and malformed boundary shapes', () => {
    const invalidValues: unknown[] = [Number.NaN, Number.POSITIVE_INFINITY, () => true, undefined];
    for (const invalid of invalidValues) {
      const malformed = snapshot() as unknown as {globalState: {status: string; value: {account: Record<string, unknown>}}};
      malformed.globalState.value.account.bad = invalid;
      expect(() => diffTask12State(malformed, snapshot(), expected())).toThrow(/globalState|JSON-serializable/i);
    }
    const cyclic: Record<string, unknown> = {}; cyclic.self = cyclic;
    const cyclicSnapshot = snapshot() as unknown as {globalState: {status: string; value: {account: Record<string, unknown>}}};
    cyclicSnapshot.globalState.value.account = cyclic;
    expect(() => diffTask12State(cyclicSnapshot, snapshot(), expected())).toThrow(/cyclic/i);
    expect(() => diffTask12State({timestamp: 1}, snapshot(), expected())).toThrow(/before snapshot/i);
    expect(() => diffTask12State({...snapshot(), focusedWindowId: {status: 'observed'}}, snapshot(), expected())).toThrow(/focusedWindowId/i);
    expect(() => diffTask12State(snapshot(), snapshot(), {scenario: '', profileB: {}})).toThrow(/expected changes/i);
    expect(() => diffTask12State(snapshot(), snapshot(), {scenario: 'x', profileB: {name: {before: 'Work'}}})).toThrow(/profileB\.name/i);
  });
});

describe('cleanup ownership', () => {
  const baseline: CleanupInventory = {profileIds: ['profile-a'], profileNames: ['Personal'], tabIds: ['tab-existing'], tempFiles: ['/tmp/pre-existing'], processes: [process(100, 'boot-a')]};
  const created: CleanupInventory = {profileIds: ['profile-b'], profileNames: ['Task 12 Work'], tabIds: ['tab-task12'], tempFiles: ['/tmp/task12-state'], processes: [process(200, 'boot-b')]};
  const empty: CleanupInventory = {profileIds: [], profileNames: [], tabIds: [], tempFiles: [], processes: []};

  test('builds a cleanup plan that never targets pre-existing resources', () => {
    const plan = buildCleanupPlan(baseline, created);
    expect(plan.targets).toEqual(created); expect(plan.preExisting).toEqual(baseline);
  });

  test('rejects duplicate inventory identities instead of silently deduplicating', () => {
    expect(() => buildCleanupPlan(baseline, {...created, profileIds: ['profile-b', 'profile-b']})).toThrow(/duplicate/i);
    expect(() => buildCleanupPlan(baseline, {...created, processes: [process(200, 'boot-b'), process(200, 'boot-b')]})).toThrow(/duplicate/i);
  });

  test('accepts a complete cleanup receipt with stable process identity', () => {
    const receipt: CleanupReceipt = {completedAt: '2026-08-01T00:05:00.000Z', removed: created, remaining: empty, errors: []};
    expect(verifyCleanupReceipt(buildCleanupPlan(baseline, created), receipt)).toEqual({passed: true, missing: [], violations: []});
  });

  test('rejects PID reuse and malformed receipt runtime shape', () => {
    const plan = buildCleanupPlan(baseline, created);
    const reused: CleanupReceipt = {completedAt: '2026-08-01T00:05:00.000Z', removed: {...created, processes: [process(200, 'different-start')]}, remaining: empty, errors: []};
    expect(verifyCleanupReceipt(plan, reused)).toEqual(expect.objectContaining({passed: false, missing: ['processes:200@boot-b']}));
    expect(() => verifyCleanupReceipt(plan, {completedAt: 'bad', removed: created, remaining: empty, errors: []})).toThrow(/receipt/i);
    const missingStartToken: unknown = {
      completedAt: '2026-08-01T00:05:00.000Z',
      removed: {...created, processes: [{pid: 200}]},
      remaining: empty,
      errors: [],
    };
    expect(() => verifyCleanupReceipt(plan, missingStartToken)).toThrow(/startToken/i);
  });

  test('rejects incomplete cleanup and pre-existing removal', () => {
    const receipt: CleanupReceipt = {completedAt: '2026-08-01T00:05:00.000Z', removed: {...created, profileIds: ['profile-b', 'profile-a'], processes: []}, remaining: created, errors: ['still running']};
    const verification = verifyCleanupReceipt(buildCleanupPlan(baseline, created), receipt);
    expect(verification.passed).toBe(false); expect(verification.missing).toContain('processes:200@boot-b'); expect(verification.violations).toContain('profileIds:profile-a');
  });
});

function completeManifestResults(): ManifestArtifactResults {
  const results: ManifestArtifactResults = {};
  for (const scenario of TASK12_SCENARIOS) {
    results[scenario.id] = {};
    for (const criterion of scenario.criteria) {
      results[scenario.id]![criterion.id] = {};
      for (const artifact of criterion.artifacts) results[scenario.id]![criterion.id]![artifact] = {present: true, passed: true};
    }
  }
  return results;
}

describe('buildTask12Manifest', () => {
  test('maps every Task 12 criterion and required artifact independently', () => {
    const results = completeManifestResults();
    const manifest = buildTask12Manifest(results);
    expect(manifest.passed).toBe(true);
    expect(manifest.entries.map(entry => entry.criterionId).sort()).toEqual([...TASK12_CRITERIA].sort());
    for (const entry of manifest.entries) expect(entry.artifacts.every(artifact => artifact.status === 'pass')).toBe(true);
  });

  test('does not reuse one artifact result across scenarios or criteria', () => {
    const results = completeManifestResults();
    results['failure-stale-target']!['visible-failure']!['dom-after.txt'] = {present: true, passed: false};
    delete results['happy-b-only-mutation']!['b-only-state-diff']!['before.json'];
    const manifest = buildTask12Manifest(results);
    expect(manifest.passed).toBe(false);
    expect(manifest.entries.find(entry => entry.scenarioId === 'failure-stale-target' && entry.criterionId === 'visible-failure')?.failedArtifacts).toEqual(['dom-after.txt']);
    expect(manifest.entries.find(entry => entry.scenarioId === 'happy-b-only-mutation' && entry.criterionId === 'b-only-state-diff')?.missingArtifacts).toEqual(['before.json']);
    expect(manifest.entries.find(entry => entry.scenarioId === 'failure-unknown-target' && entry.criterionId === 'visible-failure')?.status).toBe('pass');
  });
});
