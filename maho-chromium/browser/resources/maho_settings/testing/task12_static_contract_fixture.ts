export const TASK12_DRIVER_ARTIFACTS = [
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

export type Task12DriverArtifact = typeof TASK12_DRIVER_ARTIFACTS[number];

export const TASK12_DRIVER_SCENARIOS = [
  'happy-b-only-mutation',
  'failure-unknown-target',
  'failure-deleting-target',
  'failure-stale-target',
  'failure-unsupported-sensitive-pane',
  'lifecycle-create',
  'lifecycle-safe-delete-cancel',
  'cleanup',
] as const;

export type Task12DriverScenarioId = typeof TASK12_DRIVER_SCENARIOS[number];

type FailureScenarioId = Extract<Task12DriverScenarioId, `failure-${string}`>;
type FailureReason =
  | 'unknown-target'
  | 'deleting-target'
  | 'stale-target'
  | 'unsupported-sensitive-pane';

export const TASK12_FORBIDDEN_STATE_DOMAINS = [
  'profile-a',
  'active-browser-profile',
  'active-maho-profile',
  'focused-window',
  'selected-space',
  'space-assignments',
  'global-account',
  'global-process',
  'global-core',
] as const;

export type Task12ForbiddenStateDomain = typeof TASK12_FORBIDDEN_STATE_DOMAINS[number];

export type Task12DriverOperation =
  | {id: string; kind: 'capture-state'; phase: 'before' | 'after'}
  | {id: string; kind: 'capture-dom'; phase: 'before' | 'after'}
  | {id: string; kind: 'capture-screenshot'; phase: 'before' | 'after'}
  | {id: string; kind: 'record-events'}
  | {id: string; kind: 'await-exact-events'; eventClasses: readonly ('navigation' | 'profile')[]; timeoutMs: number}
  | {id: string; kind: 'fixed-sleep'; milliseconds: number}
  | {id: string; kind: 'poll'; timeoutMs: number}
  | {id: string; kind: 'assert-visible-failure'; reason: FailureReason}
  | {id: string; kind: 'enforce-state-diff'; forbiddenDomains: readonly Task12ForbiddenStateDomain[]}
  | {id: string; kind: 'verify-cleanup'}
  | {id: string; kind: 'write-artifact'; artifact: Task12DriverArtifact; sourceOperationId: string};

export interface Task12DriverScenarioPlan {
  id: Task12DriverScenarioId;
  operations: readonly Task12DriverOperation[];
  execution: readonly string[];
  artifacts: Readonly<Record<Task12DriverArtifact, string>>;
}

export interface Task12DriverPlan {
  scenarios: readonly Task12DriverScenarioPlan[];
  cleanupHooks: {
    failure: {registered: boolean; operationId: string};
    sigint: {registered: boolean; operationId: string};
  };
  cleanupOperations: readonly Task12DriverOperation[];
  declaredConstants?: readonly string[];
}

function fail(message: string): never {
  throw new Error(`Task 12 driver contract failed: ${message}`);
}

function uniqueById(operations: readonly Task12DriverOperation[], label: string): Map<string, Task12DriverOperation> {
  const result = new Map<string, Task12DriverOperation>();
  for (const operation of operations) {
    if (result.has(operation.id)) fail(`${label} has duplicate operation id ${operation.id}`);
    result.set(operation.id, operation);
  }
  return result;
}

function requireExactSet(actual: readonly string[], expected: readonly string[], label: string): void {
  const actualSet = new Set(actual);
  if (actualSet.size !== actual.length || actualSet.size !== expected.length || expected.some(value => !actualSet.has(value))) {
    fail(`${label} must contain every required value exactly once`);
  }
}

function expectedFailureReason(id: FailureScenarioId): FailureReason {
  return id.slice('failure-'.length) as FailureReason;
}

export function assertTask12DriverPlan(plan: Task12DriverPlan): void {
  requireExactSet(plan.scenarios.map(scenario => scenario.id), TASK12_DRIVER_SCENARIOS, 'scenario plan');

  const cleanupOperations = uniqueById(plan.cleanupOperations, 'cleanup plan');
  for (const [hookName, hook] of Object.entries(plan.cleanupHooks)) {
    if (!hook.registered) fail(`${hookName} cleanup hook is not registered`);
    const operation = cleanupOperations.get(hook.operationId);
    if (operation?.kind !== 'verify-cleanup') fail(`${hookName} cleanup hook does not invoke cleanup verification`);
  }

  for (const scenario of plan.scenarios) {
    const operations = uniqueById(scenario.operations, scenario.id);
    requireExactSet(scenario.execution, [...operations.keys()], `${scenario.id} execution`);

    for (const operation of operations.values()) {
      if (operation.kind === 'fixed-sleep' || operation.kind === 'poll') {
        fail(`${scenario.id} contains forbidden ${operation.kind}`);
      }
      if (operation.kind === 'await-exact-events') {
        requireExactSet(operation.eventClasses, ['navigation', 'profile'], `${scenario.id} exact-event wait`);
        if (!Number.isFinite(operation.timeoutMs) || operation.timeoutMs <= 0) {
          fail(`${scenario.id} exact-event wait must have a positive bounded timeout`);
        }
      }
    }

    const eventWaits = [...operations.values()].filter(operation => operation.kind === 'await-exact-events');
    if (eventWaits.length !== 1) fail(`${scenario.id} must execute one exact-event wait`);

    const diffOperations = [...operations.values()].filter(operation => operation.kind === 'enforce-state-diff');
    if (diffOperations.length !== 1) fail(`${scenario.id} must execute one state-diff enforcement`);
    requireExactSet(
      diffOperations[0]!.kind === 'enforce-state-diff' ? diffOperations[0].forbiddenDomains : [],
      TASK12_FORBIDDEN_STATE_DOMAINS,
      `${scenario.id} forbidden state domains`,
    );

    if (scenario.id.startsWith('failure-')) {
      const expectedReason = expectedFailureReason(scenario.id as FailureScenarioId);
      const failureAssertions = [...operations.values()].filter(
        operation => operation.kind === 'assert-visible-failure' && operation.reason === expectedReason,
      );
      if (failureAssertions.length !== 1) fail(`${scenario.id} must execute its visible ${expectedReason} failure assertion`);
    }

    requireExactSet(Object.keys(scenario.artifacts), TASK12_DRIVER_ARTIFACTS, `${scenario.id} artifact mapping`);
    for (const artifact of TASK12_DRIVER_ARTIFACTS) {
      const writerId = scenario.artifacts[artifact];
      const writer = operations.get(writerId);
      if (writer?.kind !== 'write-artifact' || writer.artifact !== artifact) {
        fail(`${scenario.id} artifact ${artifact} is not mapped to its writer operation`);
      }
      if (!operations.has(writer.sourceOperationId)) {
        fail(`${scenario.id} artifact ${artifact} writer has no live source operation`);
      }
      if (!scenario.execution.includes(writer.id)) {
        fail(`${scenario.id} artifact ${artifact} writer is not executed`);
      }
    }
  }
}

const sourceKinds: Record<Task12DriverArtifact, Task12DriverOperation> = {
  'before.json': {id: 'capture-state-before', kind: 'capture-state', phase: 'before'},
  'after.json': {id: 'capture-state-after', kind: 'capture-state', phase: 'after'},
  'state-diff.json': {id: 'enforce-diff', kind: 'enforce-state-diff', forbiddenDomains: TASK12_FORBIDDEN_STATE_DOMAINS},
  'events.jsonl': {id: 'record-events', kind: 'record-events'},
  'manifest.json': {id: 'await-events', kind: 'await-exact-events', eventClasses: ['navigation', 'profile'], timeoutMs: 10_000},
  'cleanup-receipt.json': {id: 'verify-cleanup', kind: 'verify-cleanup'},
  'dom-before.txt': {id: 'capture-dom-before', kind: 'capture-dom', phase: 'before'},
  'dom-after.txt': {id: 'capture-dom-after', kind: 'capture-dom', phase: 'after'},
  'screenshot-before.png': {id: 'capture-screenshot-before', kind: 'capture-screenshot', phase: 'before'},
  'screenshot-after.png': {id: 'capture-screenshot-after', kind: 'capture-screenshot', phase: 'after'},
};

function scenarioPlan(id: Task12DriverScenarioId): Task12DriverScenarioPlan {
  const sources = Object.values(sourceKinds).map(operation => ({...operation}));
  const failure = id.startsWith('failure-')
    ? [{id: 'assert-failure', kind: 'assert-visible-failure', reason: expectedFailureReason(id as FailureScenarioId)} as const]
    : [];
  const writers = TASK12_DRIVER_ARTIFACTS.map(artifact => ({
    id: `write:${artifact}`,
    kind: 'write-artifact' as const,
    artifact,
    sourceOperationId: sourceKinds[artifact].id,
  }));
  const operations = [...sources, ...failure, ...writers];
  return {
    id,
    operations,
    execution: operations.map(operation => operation.id),
    artifacts: Object.fromEntries(writers.map(writer => [writer.artifact, writer.id])) as Record<Task12DriverArtifact, string>,
  };
}

export function positiveTask12DriverPlan(): Task12DriverPlan {
  return {
    scenarios: TASK12_DRIVER_SCENARIOS.map(scenarioPlan),
    cleanupHooks: {
      failure: {registered: true, operationId: 'cleanup-on-failure'},
      sigint: {registered: true, operationId: 'cleanup-on-sigint'},
    },
    cleanupOperations: [
      {id: 'cleanup-on-failure', kind: 'verify-cleanup'},
      {id: 'cleanup-on-sigint', kind: 'verify-cleanup'},
    ],
  };
}

export function cloneTask12DriverPlan(plan: Task12DriverPlan): Task12DriverPlan {
  return structuredClone(plan);
}
