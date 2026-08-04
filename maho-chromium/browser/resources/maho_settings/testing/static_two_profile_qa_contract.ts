#!/usr/bin/env bun

import {task12DriverPlan} from './qa_two_profile_settings';
import {
  TASK12_DRIVER_ARTIFACTS,
  TASK12_FORBIDDEN_STATE_DOMAINS,
  assertTask12DriverPlan,
  cloneTask12DriverPlan,
  positiveTask12DriverPlan,
  type Task12DriverPlan,
  type Task12DriverScenarioPlan,
} from './task12_static_contract_fixture';

function check(condition: boolean, message: string): void {
  if (!condition) throw new Error(`Task 12 static contract failed: ${message}`);
  console.log(`PASS ${message}`);
}

function expectRejected(name: string, mutate: (plan: Task12DriverPlan) => void): void {
  const plan = cloneTask12DriverPlan(positiveTask12DriverPlan());
  mutate(plan);
  let rejected = false;
  try {
    assertTask12DriverPlan(plan);
  } catch (error) {
    rejected = error instanceof Error && error.message.startsWith('Task 12 driver contract failed:');
  }
  check(rejected, `rejects ${name}`);
}

function replaceScenario(
  plan: Task12DriverPlan,
  index: number,
  update: (scenario: Task12DriverScenarioPlan) => Task12DriverScenarioPlan,
): void {
  const scenarios = [...plan.scenarios];
  scenarios[index] = update(scenarios[index]!);
  plan.scenarios = scenarios;
}

assertTask12DriverPlan(task12DriverPlan);
check(true, 'driver-owned plan passes');

assertTask12DriverPlan(positiveTask12DriverPlan());
check(true, 'positive fixture plan passes validator unit test');

expectRejected('missing per-scenario artifact mapping while artifact strings remain declared', plan => {
  plan.declaredConstants = [...TASK12_DRIVER_ARTIFACTS];
  replaceScenario(plan, 0, scenario => {
    const artifacts = {...scenario.artifacts};
    delete (artifacts as Partial<typeof artifacts>)['events.jsonl'];
    return {...scenario, artifacts};
  });
});

expectRejected('failure cleanup hook missing while cleanup tokens remain declared', plan => {
  plan.declaredConstants = ['cleanupOnFailure', 'cleanupOnSigint', 'cleanup-receipt.json'];
  plan.cleanupHooks = {...plan.cleanupHooks, failure: {...plan.cleanupHooks.failure, registered: false}};
});

expectRejected('SIGINT cleanup hook missing while cleanup tokens remain declared', plan => {
  plan.declaredConstants = ['cleanupOnFailure', 'cleanupOnSigint', 'cleanup-receipt.json'];
  plan.cleanupHooks = {...plan.cleanupHooks, sigint: {...plan.cleanupHooks.sigint, registered: false}};
});

expectRejected('fixed sleep disguised alongside an exact-event wait', plan => {
  plan.declaredConstants = ['awaitExactNavigationEvents', 'awaitExactProfileEvents'];
  replaceScenario(plan, 0, scenario => ({
    ...scenario,
    operations: [...scenario.operations, {id: 'hidden-sleep', kind: 'fixed-sleep', milliseconds: 250}],
    execution: [...scenario.execution, 'hidden-sleep'],
  }));
});

expectRejected('polling loop disguised alongside an exact-event wait', plan => {
  plan.declaredConstants = ['awaitExactNavigationEvents', 'awaitExactProfileEvents'];
  replaceScenario(plan, 0, scenario => ({
    ...scenario,
    operations: [...scenario.operations, {id: 'hidden-poll', kind: 'poll', timeoutMs: 10_000}],
    execution: [...scenario.execution, 'hidden-poll'],
  }));
});

for (const scenarioId of [
  'failure-unknown-target',
  'failure-deleting-target',
  'failure-stale-target',
  'failure-unsupported-sensitive-pane',
] as const) {
  expectRejected(`${scenarioId} label without its visible failure behavior`, plan => {
    plan.declaredConstants = [scenarioId];
    const index = plan.scenarios.findIndex(scenario => scenario.id === scenarioId);
    replaceScenario(plan, index, scenario => ({
      ...scenario,
      operations: scenario.operations.filter(operation => operation.kind !== 'assert-visible-failure'),
      execution: scenario.execution.filter(operationId => operationId !== 'assert-failure'),
    }));
  });
}

for (const domain of [
  'active-browser-profile',
  'global-account',
  'selected-space',
  'space-assignments',
] as const) {
  expectRejected(`state-diff evidence keys without ${domain} enforcement`, plan => {
    plan.declaredConstants = ['state-diff.json', ...TASK12_FORBIDDEN_STATE_DOMAINS];
    replaceScenario(plan, 0, scenario => ({
      ...scenario,
      operations: scenario.operations.map(operation => operation.kind === 'enforce-state-diff'
        ? {...operation, forbiddenDomains: operation.forbiddenDomains.filter(value => value !== domain)}
        : operation),
    }));
  });
}

for (const artifact of [
  'screenshot-before.png',
  'dom-before.txt',
  'events.jsonl',
  'cleanup-receipt.json',
] as const) {
  expectRejected(`${artifact} present only as a dead constant and never written`, plan => {
    plan.declaredConstants = [artifact];
    replaceScenario(plan, 0, scenario => {
      const writerId = scenario.artifacts[artifact];
      return {...scenario, execution: scenario.execution.filter(operationId => operationId !== writerId)};
    });
  });
}

console.log('PASS Task 12 static contract is behaviorally red-capable');
