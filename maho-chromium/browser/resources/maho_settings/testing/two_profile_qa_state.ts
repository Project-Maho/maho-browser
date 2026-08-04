export type JsonPrimitive = boolean | number | string | null;
export type JsonValue = JsonPrimitive | JsonValue[] | {[key: string]: JsonValue};

export type Observation<T> =
  | {status: 'observed'; value: T}
  | {status: 'missing' | 'unavailable'; reason: string};

export interface Task12ProfileValues {
  id: string;
  name: string;
  avatarColor: string;
  homepageUrl: string;
  searchEngineId: string;
  searchSuggestionsEnabled: boolean;
  downloadPrompt: boolean;
  downloadDirectoryToken: string;
  archiveTimeoutHours: number | null;
}

export interface Task12GlobalState {
  account: {[key: string]: JsonValue};
  process: {[key: string]: JsonValue};
  coreGlobal: {[key: string]: JsonValue};
}

export type Task12Lifecycle = 'provisioning' | 'ready' | 'deleting' | 'repair_required' | 'deleted';

export interface Task12LifecycleState {
  profiles: {A: Task12Lifecycle; B: Task12Lifecycle};
  deletedProfileIds: Observation<string[]>;
  pendingDeletionProfileIds: string[];
}

export interface Task12Snapshot {
  timestamp: string;
  activeBrowserProfileId: Observation<string>;
  activeMahoProfileId: Observation<string>;
  focusedWindowId: Observation<string>;
  selectedSpace: Observation<{id: string; name: string}>;
  spaceAssignments: Observation<{tabIds: string[]; windowIds: string[]}>;
  profiles: {
    A: Observation<Task12ProfileValues>;
    B: Observation<Task12ProfileValues>;
  };
  globalState: Observation<Task12GlobalState>;
  lifecycle: Observation<Task12LifecycleState>;
}

export interface ExpectedMutation {
  before: JsonValue;
  after: JsonValue;
}

export interface AllowedCleanupChange extends ExpectedMutation {
  path: string;
}

export interface Task12ExpectedChanges {
  scenario: string;
  profileB: Partial<Record<keyof Task12ProfileValues, ExpectedMutation>>;
  allowedCleanup?: AllowedCleanupChange[];
}

export interface StateChange {
  path: string;
  before?: JsonValue;
  after?: JsonValue;
}

export interface ExpectedStateChange extends StateChange {
  matched: boolean;
  actualBefore?: JsonValue;
  actualAfter?: JsonValue;
}

export interface MissingObservation {
  path: string;
  status: 'missing' | 'unavailable';
  reason: string;
}

export interface Task12StateDiff {
  scenario: string;
  passed: boolean;
  expectedChanges: ExpectedStateChange[];
  forbiddenChanges: StateChange[];
  missingObservations: MissingObservation[];
  cleanupDeltas: StateChange[];
}

export const TASK12_ARTIFACT_KEYS = [
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

export type Task12ArtifactKey = typeof TASK12_ARTIFACT_KEYS[number];
export type Task12ScenarioId =
  | 'happy-b-only-mutation'
  | 'failure-unknown-target'
  | 'failure-deleting-target'
  | 'failure-stale-target'
  | 'failure-unsupported-sensitive-pane'
  | 'lifecycle-create'
  | 'lifecycle-safe-delete-cancel'
  | 'cleanup';

export interface ArtifactResult {
  present: boolean;
  passed: boolean;
}

export type Task12CriterionId =
  | 'b-only-state-diff'
  | 'visible-failure'
  | 'lifecycle-state-diff'
  | 'cleanup-complete';

export interface Task12Criterion {
  id: Task12CriterionId;
  artifacts: Task12ArtifactKey[];
}

export interface Task12Scenario {
  id: Task12ScenarioId;
  criteria: Task12Criterion[];
}

export type ManifestArtifactResults = Partial<Record<
  Task12ScenarioId,
  Partial<Record<Task12CriterionId, Partial<Record<Task12ArtifactKey, ArtifactResult>>>>
>>;

export interface Task12ManifestArtifact {
  key: Task12ArtifactKey;
  status: 'pass' | 'fail' | 'missing';
}

export interface Task12ManifestEntry {
  scenarioId: Task12ScenarioId;
  criterionId: Task12CriterionId;
  artifacts: Task12ManifestArtifact[];
  status: 'pass' | 'fail' | 'missing';
  missingArtifacts: Task12ArtifactKey[];
  failedArtifacts: Task12ArtifactKey[];
}

export interface Task12Manifest {
  passed: boolean;
  entries: Task12ManifestEntry[];
}

export interface CleanupProcessIdentity {
  pid: number;
  startToken: string;
}

export interface CleanupInventory {
  profileIds: string[];
  profileNames: string[];
  tabIds: string[];
  tempFiles: string[];
  processes: CleanupProcessIdentity[];
}

export interface CleanupPlan {
  preExisting: CleanupInventory;
  targets: CleanupInventory;
}

export interface CleanupReceipt {
  completedAt: string;
  removed: CleanupInventory;
  remaining: CleanupInventory;
  errors: string[];
}

export interface CleanupVerification {
  passed: boolean;
  missing: string[];
  violations: string[];
}

// Contract for the future runtime driver; this core does not implement or launch it.
export interface Task12RuntimeDriverRequirements {
  boundedEventTimeoutMs: number;
  awaitExactNavigationEvents: true;
  awaitExactProfileEvents: true;
  cleanupOnFailure: true;
  cleanupOnSigint: true;
}

const PROFILE_FIELDS: ReadonlySet<string> = new Set([
  'id', 'name', 'avatarColor', 'homepageUrl', 'searchEngineId',
  'searchSuggestionsEnabled', 'downloadPrompt', 'downloadDirectoryToken',
  'archiveTimeoutHours',
]);
const SNAPSHOT_KEYS = [
  'timestamp', 'activeBrowserProfileId', 'activeMahoProfileId', 'focusedWindowId',
  'selectedSpace', 'spaceAssignments', 'profiles', 'globalState', 'lifecycle',
] as const;
const LIFECYCLE_VALUES: ReadonlySet<string> = new Set([
  'provisioning', 'ready', 'deleting', 'repair_required', 'deleted',
]);

type ValueValidator = (value: unknown, label: string) => void;

function isRecord(value: unknown): value is Record<string, unknown> {
  if (typeof value !== 'object' || value === null || Array.isArray(value)) return false;
  const prototype = Object.getPrototypeOf(value);
  return prototype === Object.prototype || prototype === null;
}

function assertExactKeys(value: Record<string, unknown>, allowed: readonly string[], label: string): void {
  const allowedKeys = new Set(allowed);
  for (const key of Object.keys(value)) {
    if (!allowedKeys.has(key)) throw new TypeError(`${label} has unexpected field ${key}`);
  }
  for (const key of allowed) {
    if (!Object.prototype.hasOwnProperty.call(value, key)) throw new TypeError(`${label}.${key} is required`);
  }
}

function assertJsonValue(value: unknown, label: string, ancestors = new Set<object>()): asserts value is JsonValue {
  if (value === null || typeof value === 'string' || typeof value === 'boolean') return;
  if (typeof value === 'number') {
    if (Number.isFinite(value)) return;
    throw new TypeError(`${label} must not contain a non-finite number`);
  }
  if (typeof value !== 'object') throw new TypeError(`${label} must be JSON-serializable`);
  if (ancestors.has(value)) throw new TypeError(`${label} must not contain a cyclic value`);
  ancestors.add(value);
  try {
    if (Array.isArray(value)) {
      const keys = Reflect.ownKeys(value);
      for (let index = 0; index < value.length; index++) {
        if (!Object.prototype.hasOwnProperty.call(value, index)) {
          throw new TypeError(`${label} must not contain a sparse array`);
        }
        assertJsonValue(value[index], `${label}[${index}]`, ancestors);
      }
      if (keys.some(key => typeof key === 'symbol' || (key !== 'length' && !/^(0|[1-9]\d*)$/.test(key)))) {
        throw new TypeError(`${label} array has a non-JSON property`);
      }
      return;
    }
    if (!isRecord(value)) throw new TypeError(`${label} must contain only JSON objects`);
    for (const key of Reflect.ownKeys(value)) {
      if (typeof key === 'symbol') throw new TypeError(`${label} must not contain symbol keys`);
      const descriptor = Object.getOwnPropertyDescriptor(value, key);
      if (!descriptor?.enumerable || !('value' in descriptor)) {
        throw new TypeError(`${label}.${key} must be an enumerable JSON value`);
      }
      assertJsonValue(descriptor.value, `${label}.${key}`, ancestors);
    }
  } finally {
    ancestors.delete(value);
  }
}

function requireString(value: unknown, label: string): asserts value is string {
  if (typeof value !== 'string' || value.length === 0) throw new TypeError(`${label} must be a non-empty string`);
}

function requireBoolean(value: unknown, label: string): void {
  if (typeof value !== 'boolean') throw new TypeError(`${label} must be a boolean`);
}

function validateStringArray(value: unknown, label: string): void {
  if (!Array.isArray(value)) throw new TypeError(`${label} must be an array`);
  assertJsonValue(value, label);
  for (const entry of value) requireString(entry, `${label} entry`);
}

function validateProfileField(field: string, value: unknown, label: string): void {
  if (field === 'searchSuggestionsEnabled' || field === 'downloadPrompt') {
    requireBoolean(value, label);
  } else if (field === 'archiveTimeoutHours') {
    if (value !== null && (typeof value !== 'number' || !Number.isFinite(value))) {
      throw new TypeError(`${label} must be a finite number or null`);
    }
  } else {
    requireString(value, label);
  }
}

function validateProfile(value: unknown, label: string): void {
  if (!isRecord(value)) throw new TypeError(`${label} must be an object`);
  assertExactKeys(value, [...PROFILE_FIELDS], label);
  assertJsonValue(value, label);
  for (const field of PROFILE_FIELDS) validateProfileField(field, value[field], `${label}.${field}`);
}

function validateSelectedSpace(value: unknown, label: string): void {
  if (!isRecord(value)) throw new TypeError(`${label} must be an object`);
  assertExactKeys(value, ['id', 'name'], label);
  requireString(value.id, `${label}.id`);
  requireString(value.name, `${label}.name`);
}

function validateSpaceAssignments(value: unknown, label: string): void {
  if (!isRecord(value)) throw new TypeError(`${label} must be an object`);
  assertExactKeys(value, ['tabIds', 'windowIds'], label);
  validateStringArray(value.tabIds, `${label}.tabIds`);
  validateStringArray(value.windowIds, `${label}.windowIds`);
}

function validateGlobalState(value: unknown, label: string): void {
  if (!isRecord(value)) throw new TypeError(`${label} must be an object`);
  assertExactKeys(value, ['account', 'process', 'coreGlobal'], label);
  for (const key of ['account', 'process', 'coreGlobal'] as const) {
    if (!isRecord(value[key])) throw new TypeError(`${label}.${key} must be an object`);
    assertJsonValue(value[key], `${label}.${key}`);
  }
}

function validateLifecycle(value: unknown, label: string): void {
  if (!isRecord(value)) throw new TypeError(`${label} must be an object`);
  assertExactKeys(value, ['profiles', 'deletedProfileIds', 'pendingDeletionProfileIds'], label);
  if (!isRecord(value.profiles)) throw new TypeError(`${label}.profiles must be an object`);
  assertExactKeys(value.profiles, ['A', 'B'], `${label}.profiles`);
  for (const profile of ['A', 'B'] as const) {
    if (!LIFECYCLE_VALUES.has(value.profiles[profile] as string)) {
      throw new TypeError(`${label}.profiles.${profile} is invalid`);
    }
  }
  validateObservation(
      value.deletedProfileIds, `${label}.deletedProfileIds`, validateStringArray);
  validateStringArray(value.pendingDeletionProfileIds, `${label}.pendingDeletionProfileIds`);
}

function validateObservation(value: unknown, label: string, validateValue: ValueValidator): void {
  if (!isRecord(value)) throw new TypeError(`${label} must be an observation`);
  if (value.status === 'observed') {
    assertExactKeys(value, ['status', 'value'], label);
    validateValue(value.value, `${label}.value`);
    return;
  }
  if (value.status === 'missing' || value.status === 'unavailable') {
    assertExactKeys(value, ['status', 'reason'], label);
    requireString(value.reason, `${label}.reason`);
    return;
  }
  throw new TypeError(`${label}.status is invalid`);
}

const validateObservedString: ValueValidator = (value, label) => requireString(value, label);

export function validateTask12Snapshot(value: unknown, label: string): asserts value is Task12Snapshot {
  if (!isRecord(value)) throw new TypeError(`${label} snapshot must be an object`);
  assertExactKeys(value, SNAPSHOT_KEYS, `${label} snapshot`);
  requireString(value.timestamp, `${label} snapshot timestamp`);
  if (!Number.isFinite(Date.parse(value.timestamp))) throw new TypeError(`${label} snapshot timestamp must be ISO-compatible`);
  validateObservation(value.activeBrowserProfileId, `${label}.activeBrowserProfileId`, validateObservedString);
  validateObservation(value.activeMahoProfileId, `${label}.activeMahoProfileId`, validateObservedString);
  validateObservation(value.focusedWindowId, `${label}.focusedWindowId`, validateObservedString);
  validateObservation(value.selectedSpace, `${label}.selectedSpace`, validateSelectedSpace);
  validateObservation(value.spaceAssignments, `${label}.spaceAssignments`, validateSpaceAssignments);
  if (!isRecord(value.profiles)) throw new TypeError(`${label}.profiles must be an object`);
  assertExactKeys(value.profiles, ['A', 'B'], `${label}.profiles`);
  validateObservation(value.profiles.A, `${label}.profiles.A`, validateProfile);
  validateObservation(value.profiles.B, `${label}.profiles.B`, validateProfile);
  validateObservation(value.globalState, `${label}.globalState`, validateGlobalState);
  validateObservation(value.lifecycle, `${label}.lifecycle`, validateLifecycle);
}

function validateExpected(value: unknown): asserts value is Task12ExpectedChanges {
  if (!isRecord(value)) throw new TypeError('expected changes must be an object');
  const expectedKeys = value.allowedCleanup === undefined ? ['scenario', 'profileB'] : ['scenario', 'profileB', 'allowedCleanup'];
  assertExactKeys(value, expectedKeys, 'expected changes');
  requireString(value.scenario, 'expected changes scenario');
  if (!isRecord(value.profileB)) throw new TypeError('expected changes profileB must be an object');
  for (const [field, mutation] of Object.entries(value.profileB)) {
    if (!PROFILE_FIELDS.has(field) || !isRecord(mutation)) {
      throw new TypeError(`expected changes profileB.${field} is malformed`);
    }
    assertExactKeys(mutation, ['before', 'after'], `expected changes profileB.${field}`);
    assertJsonValue(mutation.before, `expected changes profileB.${field}.before`);
    assertJsonValue(mutation.after, `expected changes profileB.${field}.after`);
    validateProfileField(field, mutation.before, `expected changes profileB.${field}.before`);
    validateProfileField(field, mutation.after, `expected changes profileB.${field}.after`);
  }
  if (value.allowedCleanup !== undefined) {
    if (!Array.isArray(value.allowedCleanup)) throw new TypeError('expected changes allowedCleanup must be an array');
    const paths = new Set<string>();
    for (const change of value.allowedCleanup) {
      if (!isRecord(change)) throw new TypeError('expected changes allowedCleanup entry is malformed');
      assertExactKeys(change, ['path', 'before', 'after'], 'expected changes allowedCleanup entry');
      if (typeof change.path !== 'string' || !change.path.startsWith('lifecycle.')) {
        throw new TypeError('expected changes allowedCleanup entry path is malformed');
      }
      if (paths.has(change.path)) throw new TypeError(`expected changes allowedCleanup has duplicate path ${change.path}`);
      paths.add(change.path);
      assertJsonValue(change.before, `expected changes allowedCleanup ${change.path}.before`);
      assertJsonValue(change.after, `expected changes allowedCleanup ${change.path}.after`);
    }
  }
}

function equal(left: JsonValue | undefined, right: JsonValue | undefined): boolean {
  if (left === right) return true;
  if (left === undefined || right === undefined || left === null || right === null || typeof left !== typeof right) return false;
  if (Array.isArray(left) || Array.isArray(right)) {
    return Array.isArray(left) && Array.isArray(right) && left.length === right.length &&
      left.every((value, index) => equal(value, right[index]));
  }
  if (typeof left === 'object' && typeof right === 'object') {
    const leftKeys = Object.keys(left);
    const rightKeys = Object.keys(right);
    return leftKeys.length === rightKeys.length && leftKeys.every(key =>
      Object.prototype.hasOwnProperty.call(right, key) && equal(left[key], right[key]));
  }
  return false;
}

function flatten(value: JsonValue, prefix: string, output: Map<string, JsonValue>): void {
  if (isRecord(value)) {
    const entries = Object.entries(value);
    if (entries.length === 0) output.set(prefix, value as JsonValue);
    for (const [key, child] of entries) flatten(child as JsonValue, `${prefix}.${key}`, output);
    return;
  }
  output.set(prefix, value);
}

function observedValue(observation: Observation<unknown>): JsonValue | undefined {
  return observation.status === 'observed' ? observation.value as JsonValue : undefined;
}

function collectMissing(snapshot: Task12Snapshot, side: 'before' | 'after'): MissingObservation[] {
  const observations: Array<[string, Observation<unknown>]> = [
    ['activeBrowserProfileId', snapshot.activeBrowserProfileId],
    ['activeMahoProfileId', snapshot.activeMahoProfileId],
    ['focusedWindowId', snapshot.focusedWindowId],
    ['selectedSpace', snapshot.selectedSpace],
    ['spaceAssignments', snapshot.spaceAssignments],
    ['profiles.A', snapshot.profiles.A],
    ['profiles.B', snapshot.profiles.B],
    ['globalState', snapshot.globalState],
    ['lifecycle', snapshot.lifecycle],
  ];
  return observations.flatMap(([path, observation]) => observation.status === 'observed' ? [] : [{
    path: `${side}.${path}`,
    status: observation.status,
    reason: observation.reason,
  }]);
}

function snapshotLeaves(snapshot: Task12Snapshot): Map<string, JsonValue> {
  const leaves = new Map<string, JsonValue>();
  const observations: Array<[string, Observation<unknown>]> = [
    ['activeBrowserProfileId', snapshot.activeBrowserProfileId],
    ['activeMahoProfileId', snapshot.activeMahoProfileId],
    ['focusedWindowId', snapshot.focusedWindowId],
    ['selectedSpace', snapshot.selectedSpace],
    ['spaceAssignments', snapshot.spaceAssignments],
    ['profiles.A', snapshot.profiles.A],
    ['profiles.B', snapshot.profiles.B],
    ['globalState', snapshot.globalState],
    ['lifecycle', snapshot.lifecycle],
  ];
  for (const [path, observation] of observations) {
    const value = observedValue(observation);
    if (value !== undefined) flatten(value, path, leaves);
  }
  return leaves;
}

export function diffTask12State(
  beforeInput: unknown,
  afterInput: unknown,
  expectedInput: unknown,
): Task12StateDiff {
  validateTask12Snapshot(beforeInput, 'before');
  validateTask12Snapshot(afterInput, 'after');
  validateExpected(expectedInput);
  const before = beforeInput;
  const after = afterInput;
  const expected = expectedInput;
  const beforeLeaves = snapshotLeaves(before);
  const afterLeaves = snapshotLeaves(after);
  const missingObservations = [...collectMissing(before, 'before'), ...collectMissing(after, 'after')];
  const expectedChanges: ExpectedStateChange[] = Object.entries(expected.profileB).map(([field, mutation]) => {
    const path = `profiles.B.${field}`;
    const actualBefore = beforeLeaves.get(path);
    const actualAfter = afterLeaves.get(path);
    return {
      path,
      before: mutation.before,
      after: mutation.after,
      matched: equal(actualBefore, mutation.before) && equal(actualAfter, mutation.after),
      ...(actualBefore === undefined ? {} : {actualBefore}),
      ...(actualAfter === undefined ? {} : {actualAfter}),
    };
  });
  const allowedCleanup = new Map((expected.allowedCleanup ?? []).map(change => [change.path, change]));
  const expectedPaths = new Set(expectedChanges.map(change => change.path));
  const forbiddenChanges: StateChange[] = [];
  const cleanupDeltas: StateChange[] = [];
  const allPaths = new Set([...beforeLeaves.keys(), ...afterLeaves.keys()]);
  for (const path of [...allPaths].sort()) {
    const hasOldValue = beforeLeaves.has(path);
    const hasNewValue = afterLeaves.has(path);
    const oldValue = beforeLeaves.get(path);
    const newValue = afterLeaves.get(path);
    if (hasOldValue && hasNewValue && equal(oldValue, newValue)) continue;
    const change: StateChange = {
      path,
      ...(hasOldValue ? {before: oldValue} : {}),
      ...(hasNewValue ? {after: newValue} : {}),
    };
    if (expectedPaths.has(path)) continue;
    const cleanup = allowedCleanup.get(path);
    if (cleanup && hasOldValue && hasNewValue &&
        equal(oldValue, cleanup.before) && equal(newValue, cleanup.after)) {
      cleanupDeltas.push(change);
    } else {
      forbiddenChanges.push(change);
    }
  }
  const cleanupComplete = [...allowedCleanup.keys()].every(path => cleanupDeltas.some(change => change.path === path));
  return {
    scenario: expected.scenario,
    passed: missingObservations.length === 0 && forbiddenChanges.length === 0 &&
      expectedChanges.every(change => change.matched) && cleanupComplete,
    expectedChanges,
    forbiddenChanges,
    missingObservations,
    cleanupDeltas,
  };
}

const STATE_DIFF_ARTIFACTS: Task12ArtifactKey[] = [
  'before.json', 'after.json', 'state-diff.json', 'events.jsonl', 'manifest.json',
];
const VISIBLE_ARTIFACTS: Task12ArtifactKey[] = [
  'dom-before.txt', 'dom-after.txt', 'screenshot-before.png', 'screenshot-after.png',
  'manifest.json',
];

export const TASK12_SCENARIOS: readonly Task12Scenario[] = [
  {id: 'happy-b-only-mutation', criteria: [
    {id: 'b-only-state-diff', artifacts: STATE_DIFF_ARTIFACTS},
  ]},
  {id: 'failure-unknown-target', criteria: [
    {id: 'visible-failure', artifacts: VISIBLE_ARTIFACTS},
  ]},
  {id: 'failure-deleting-target', criteria: [
    {id: 'visible-failure', artifacts: VISIBLE_ARTIFACTS},
  ]},
  {id: 'failure-stale-target', criteria: [
    {id: 'visible-failure', artifacts: VISIBLE_ARTIFACTS},
  ]},
  {id: 'failure-unsupported-sensitive-pane', criteria: [
    {id: 'visible-failure', artifacts: VISIBLE_ARTIFACTS},
  ]},
  {id: 'lifecycle-create', criteria: [
    {id: 'lifecycle-state-diff', artifacts: STATE_DIFF_ARTIFACTS},
  ]},
  {id: 'lifecycle-safe-delete-cancel', criteria: [
    {id: 'lifecycle-state-diff', artifacts: STATE_DIFF_ARTIFACTS},
  ]},
  {id: 'cleanup', criteria: [
    {id: 'cleanup-complete', artifacts: ['cleanup-receipt.json', 'manifest.json']},
  ]},
];

export const TASK12_CRITERIA: readonly Task12CriterionId[] = TASK12_SCENARIOS.flatMap(
  scenario => scenario.criteria.map(criterion => criterion.id),
);

export function buildTask12Manifest(results: ManifestArtifactResults): Task12Manifest {
  const entries = TASK12_SCENARIOS.flatMap(scenario => scenario.criteria.map(criterion => {
    const criterionResults = results[scenario.id]?.[criterion.id];
    const artifacts = criterion.artifacts.map(key => {
      const result = criterionResults?.[key];
      return {
        key,
        status: result?.present !== true ? 'missing' as const :
          result.passed === true ? 'pass' as const : 'fail' as const,
      };
    });
    const missingArtifacts = artifacts.filter(artifact => artifact.status === 'missing').map(artifact => artifact.key);
    const failedArtifacts = artifacts.filter(artifact => artifact.status === 'fail').map(artifact => artifact.key);
    const status: Task12ManifestEntry['status'] = missingArtifacts.length > 0
      ? 'missing'
      : failedArtifacts.length > 0
        ? 'fail'
        : 'pass';
    return {
      scenarioId: scenario.id,
      criterionId: criterion.id,
      artifacts,
      status,
      missingArtifacts,
      failedArtifacts,
    };
  }));
  return {passed: entries.every(entry => entry.status === 'pass'), entries};
}

const STRING_INVENTORY_KEYS = ['profileIds', 'profileNames', 'tabIds', 'tempFiles'] as const;
type StringInventoryKey = typeof STRING_INVENTORY_KEYS[number];

function processLabel(process: CleanupProcessIdentity): string {
  return `${process.pid}@${process.startToken}`;
}

function validateStringList(value: unknown, label: string): asserts value is string[] {
  if (!Array.isArray(value)) throw new TypeError(`${label} must be an array`);
  const seen = new Set<string>();
  for (const entry of value) {
    requireString(entry, `${label} entry`);
    if (seen.has(entry)) throw new TypeError(`${label} contains duplicate identity ${entry}`);
    seen.add(entry);
  }
}

function validateProcesses(value: unknown, label: string): asserts value is CleanupProcessIdentity[] {
  if (!Array.isArray(value)) throw new TypeError(`${label} must be an array`);
  const identities = new Set<string>();
  const pids = new Set<number>();
  for (const entry of value) {
    if (!isRecord(entry)) throw new TypeError(`${label} entry must be a process identity`);
    if (!Number.isSafeInteger(entry.pid) || (entry.pid as number) <= 0) {
      throw new TypeError(`${label} entry pid must be a positive integer`);
    }
    requireString(entry.startToken, `${label} entry startToken`);
    const identity = processLabel(entry as unknown as CleanupProcessIdentity);
    if (identities.has(identity) || pids.has(entry.pid as number)) {
      throw new TypeError(`${label} contains duplicate or ambiguous process identity ${identity}`);
    }
    identities.add(identity);
    pids.add(entry.pid as number);
  }
}

function validateInventory(value: unknown, label: string): asserts value is CleanupInventory {
  if (!isRecord(value)) throw new TypeError(`${label} must be an inventory object`);
  for (const key of STRING_INVENTORY_KEYS) validateStringList(value[key], `${label}.${key}`);
  validateProcesses(value.processes, `${label}.processes`);
}

function validateCleanupReceipt(value: unknown): asserts value is CleanupReceipt {
  if (!isRecord(value)) throw new TypeError('cleanup receipt must be an object');
  requireString(value.completedAt, 'cleanup receipt completedAt');
  if (!Number.isFinite(Date.parse(value.completedAt))) {
    throw new TypeError('cleanup receipt completedAt must be ISO-compatible');
  }
  validateInventory(value.removed, 'cleanup receipt removed');
  validateInventory(value.remaining, 'cleanup receipt remaining');
  if (!Array.isArray(value.errors) || value.errors.some(error => typeof error !== 'string')) {
    throw new TypeError('cleanup receipt errors must be a string array');
  }
}

export function buildCleanupPlan(preExisting: CleanupInventory, created: CleanupInventory): CleanupPlan {
  validateInventory(preExisting, 'pre-existing cleanup inventory');
  validateInventory(created, 'created cleanup inventory');
  const baselinePids = new Map(preExisting.processes.map(process => [process.pid, process]));
  for (const process of created.processes) {
    const baseline = baselinePids.get(process.pid);
    if (baseline && baseline.startToken !== process.startToken) {
      throw new TypeError(`process PID ${process.pid} has ambiguous ownership or was reused`);
    }
  }
  return {
    preExisting: {
      profileIds: [...preExisting.profileIds],
      profileNames: [...preExisting.profileNames],
      tabIds: [...preExisting.tabIds],
      tempFiles: [...preExisting.tempFiles],
      processes: preExisting.processes.map(process => ({...process})),
    },
    targets: {
      profileIds: created.profileIds.filter(value => !preExisting.profileIds.includes(value)),
      profileNames: created.profileNames.filter(value => !preExisting.profileNames.includes(value)),
      tabIds: created.tabIds.filter(value => !preExisting.tabIds.includes(value)),
      tempFiles: created.tempFiles.filter(value => !preExisting.tempFiles.includes(value)),
      processes: created.processes
        .filter(process => !preExisting.processes.some(existing =>
          existing.pid === process.pid && existing.startToken === process.startToken))
        .map(process => ({...process})),
    },
  };
}

function verifyStringResources(
  key: StringInventoryKey,
  plan: CleanupPlan,
  receipt: CleanupReceipt,
  missing: string[],
  violations: string[],
): void {
  for (const target of plan.targets[key]) {
    if (!receipt.removed[key].includes(target) || receipt.remaining[key].includes(target)) {
      missing.push(`${key}:${target}`);
    }
  }
  for (const removed of receipt.removed[key]) {
    if (plan.preExisting[key].includes(removed)) violations.push(`${key}:${removed}`);
  }
}

export function verifyCleanupReceipt(plan: CleanupPlan, receiptInput: unknown): CleanupVerification {
  validateInventory(plan.preExisting, 'cleanup plan preExisting');
  validateInventory(plan.targets, 'cleanup plan targets');
  validateCleanupReceipt(receiptInput);
  const receipt = receiptInput;
  const missing: string[] = [];
  const violations: string[] = [];
  for (const key of STRING_INVENTORY_KEYS) {
    verifyStringResources(key, plan, receipt, missing, violations);
  }
  for (const target of plan.targets.processes) {
    const removed = receipt.removed.processes.some(process =>
      process.pid === target.pid && process.startToken === target.startToken);
    const remaining = receipt.remaining.processes.some(process =>
      process.pid === target.pid && process.startToken === target.startToken);
    if (!removed || remaining) missing.push(`processes:${processLabel(target)}`);
  }
  for (const removed of receipt.removed.processes) {
    if (plan.preExisting.processes.some(process =>
      process.pid === removed.pid && process.startToken === removed.startToken)) {
      violations.push(`processes:${processLabel(removed)}`);
    }
  }
  if (receipt.errors.length > 0) missing.push(...receipt.errors.map(error => `error:${error}`));
  return {passed: missing.length === 0 && violations.length === 0, missing, violations};
}
