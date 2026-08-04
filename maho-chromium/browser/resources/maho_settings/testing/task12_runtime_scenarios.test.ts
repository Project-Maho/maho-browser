import {describe, expect, test} from 'bun:test';

import {
  MISSING_RUNTIME_BINDING,
  Task12RuntimeScenarioExecutor,
  executeAfterExactSignal,
  inspectTask12ObservableSeams,
  inspectTask12RuntimeBinding,
  type ExactMutationSource,
  type ExactSignalClearTimer,
  type ExactSignalSetTimer,
  type ExactSignalSource,
  type RuntimeBindingAvailability,
  type Task12CdpRuntime,
  type Task12ScenarioRequests,
  type Task12ScenarioResult,
} from './task12_runtime_scenarios.js';

type Listener = () => void;

class MockEmitter implements ExactSignalSource<number>, ExactMutationSource {
  value = 0;
  readonly log: string[];
  private readonly listeners = new Set<Listener>();
  private readonly mutationListeners = new Set<Listener>();

  constructor(log: string[]) {
    this.log = log;
  }

  snapshot(): number {
    return this.value;
  }

  subscribe(listener: Listener): () => void {
    this.log.push('subscribe-store');
    this.listeners.add(listener);
    return () => {
      this.log.push('unsubscribe-store');
      this.listeners.delete(listener);
    };
  }

  observe(listener: Listener): () => void {
    this.log.push('observe-dom');
    this.mutationListeners.add(listener);
    return () => {
      this.log.push('disconnect-dom');
      this.mutationListeners.delete(listener);
    };
  }

  emitStore(value: number): void {
    this.value = value;
    for (const listener of [...this.listeners]) listener();
  }

  emitMutation(value: number): void {
    this.value = value;
    for (const listener of [...this.mutationListeners]) listener();
  }

  captureStoreCallbacks(): Listener[] {
    return [...this.listeners];
  }
}

const requests: Task12ScenarioRequests = {
  happy: {
    profileId: 'B',
    name: 'Work',
    avatarColor: '#007AFF',
    homepageUrl: 'https://example.test/',
    searchEngineKeyword: 'example',
    searchSuggestionsEnabled: false,
    downloadPrompt: true,
    archiveTimeoutHours: 24,
  },
  unknownTarget: {target: {profileId: 'missing', targetToken: 'bad'}, expectedMessage: 'unavailable'},
  deletingTarget: {target: {profileId: 'deleting', targetToken: 'deleting'}, expectedMessage: 'deleting'},
  staleTarget: {target: {profileId: 'B', targetToken: 'stale'}, expectedMessage: 'stale'},
  sensitivePane: {profileId: 'B', paneKey: 'passwords'},
  lifecycle: {profileName: 'Task 12 disposable', profileId: 'disposable'},
};

class MockCdp implements Task12CdpRuntime {
  readonly expressions: string[] = [];
  availability: RuntimeBindingAvailability = {status: 'available'};
  currentBrowserSpaceSnapshotAvailable = true;
  profileObservablesSnapshotAvailable = true;
  actionResult: unknown = {ok: true, result: true};

  async eval<T>(expression: string): Promise<T | undefined> {
    this.expressions.push(expression);
    if (expression.includes("missing.push('settingsStore.'")) {
      if (this.availability.status !== 'available') return this.availability as T;
      const missing = [
        ...(this.currentBrowserSpaceSnapshotAvailable ? [] : ['browserProxy.getCurrentBrowserSpaceSnapshot']),
        ...(this.profileObservablesSnapshotAvailable ? [] : ['browserProxy.getProfileObservablesSnapshot']),
      ];
      return (missing.length === 0 ? this.availability : {
        status: 'missing-runtime-binding',
        reason: MISSING_RUNTIME_BINDING,
        missing,
      }) as T;
    }
    if (expression.includes('fullPassageAvailable:Object.values')) return {
      activeMahoProfile: {status: 'available', source: 'selected context'},
      focusedWindow: this.currentBrowserSpaceSnapshotAvailable
        ? {status: 'available', source: 'browserProxy.getCurrentBrowserSpaceSnapshot'}
        : {status: 'unavailable', reason: 'missing'},
      selectedSpace: this.currentBrowserSpaceSnapshotAvailable
        ? {status: 'available', source: 'browserProxy.getCurrentBrowserSpaceSnapshot'}
        : {status: 'unavailable', reason: 'missing'},
      spaceAssignments: this.currentBrowserSpaceSnapshotAvailable
        ? {status: 'available', source: 'browserProxy.getCurrentBrowserSpaceSnapshot'}
        : {status: 'unavailable', reason: 'missing'},
      backendGlobalValues: {status: 'available', source: 'global snapshot'},
      fullPassageAvailable: this.currentBrowserSpaceSnapshotAvailable,
    } as T;
    return this.actionResult as T;
  }
}

describe('executeAfterExactSignal', () => {
  test('subscribes to store and MutationObserver seam before invoking the action', async () => {
    const log: string[] = [];
    const emitter = new MockEmitter(log);
    const promise = executeAfterExactSignal({
      source: emitter,
      mutationSource: emitter,
      matches: value => value === 1,
      action: () => {
        log.push('action');
        emitter.emitStore(1);
        return 'done';
      },
      setTimer: (() => {
        log.push('set-timeout');
        return 17;
      }) satisfies ExactSignalSetTimer,
      clearTimer: (() => log.push('clear-timeout')) satisfies ExactSignalClearTimer,
    });

    await expect(promise).resolves.toBe('done');
    expect(log.slice(0, 4)).toEqual(['subscribe-store', 'observe-dom', 'set-timeout', 'action']);
    expect(log.slice(-3)).toEqual(['clear-timeout', 'disconnect-dom', 'unsubscribe-store']);
  });

  test('ignores a stale delayed callback after completion', async () => {
    const log: string[] = [];
    const emitter = new MockEmitter(log);
    let staleCallbacks: Listener[] = [];
    const result = executeAfterExactSignal({
      source: emitter,
      matches: value => value === 1,
      action: () => {
        staleCallbacks = emitter.captureStoreCallbacks();
        emitter.emitStore(1);
        return 'accepted';
      },
      setTimer: (() => 18) satisfies ExactSignalSetTimer,
      clearTimer: (() => {}) satisfies ExactSignalClearTimer,
    });

    await expect(result).resolves.toBe('accepted');
    emitter.value = 99;
    expect(() => staleCallbacks.forEach(callback => callback())).not.toThrow();
    expect(log.at(-1)).toBe('unsubscribe-store');
  });

  test('runs cleanup callbacks in timer, DOM, store order when the action rejects', async () => {
    const log: string[] = [];
    const emitter = new MockEmitter(log);
    await expect(executeAfterExactSignal({
      source: emitter,
      mutationSource: emitter,
      matches: () => false,
      action: () => {
        log.push('action');
        throw new Error('boom');
      },
      setTimer: (() => 19) satisfies ExactSignalSetTimer,
      clearTimer: (() => log.push('clear-timeout')) satisfies ExactSignalClearTimer,
    })).rejects.toThrow('boom');
    expect(log.slice(-3)).toEqual(['clear-timeout', 'disconnect-dom', 'unsubscribe-store']);
  });
});

describe('runtime binding and observability', () => {
  test('returns typed MISSING_RUNTIME_BINDING from the in-page capability probe', async () => {
    const cdp = new MockCdp();
    cdp.availability = {
      status: 'missing-runtime-binding',
      reason: MISSING_RUNTIME_BINDING,
      missing: ['settingsStore.subscribe'],
    };
    const availability = await inspectTask12RuntimeBinding(cdp);
    expect(availability).toEqual(cdp.availability);
  });

  test('requires the atomic current-browser Space snapshot proxy method', async () => {
    const cdp = new MockCdp();
    cdp.currentBrowserSpaceSnapshotAvailable = false;
    const availability = await inspectTask12RuntimeBinding(cdp);
    expect(availability).toEqual({
      status: 'missing-runtime-binding',
      reason: MISSING_RUNTIME_BINDING,
      missing: ['browserProxy.getCurrentBrowserSpaceSnapshot'],
    });
    expect(cdp.expressions[0]).toContain('getCurrentBrowserSpaceSnapshot');
  });

  test('requires the profile observables snapshot while current-browser snapshot remains available', async () => {
    const cdp = new MockCdp();
    cdp.profileObservablesSnapshotAvailable = false;
    const calls: string[] = [];
    const executor = new Task12RuntimeScenarioExecutor(cdp, {
      captureBefore: () => { calls.push('before'); return {}; },
      captureAfter: () => { calls.push('after'); return {}; },
    });

    const availability = await executor.availability();
    expect(availability).toEqual({
      status: 'missing-runtime-binding',
      reason: MISSING_RUNTIME_BINDING,
      missing: ['browserProxy.getProfileObservablesSnapshot'],
    });
    expect(cdp.currentBrowserSpaceSnapshotAvailable).toBe(true);
    expect(cdp.expressions[0]).toContain('getCurrentBrowserSpaceSnapshot');
    expect(cdp.expressions[0]).toContain('getProfileObservablesSnapshot');

    const result = await executor.execute('happy-b-only-mutation', requests, event => {
      calls.push(event.kind);
    });
    expect(result).toEqual({
      status: 'missing-runtime-binding',
      reason: MISSING_RUNTIME_BINDING,
      missing: ['browserProxy.getProfileObservablesSnapshot'],
    });
    expect(calls).toEqual([]);
  });

  test('marks focus, selected Space, and assignments available only through the atomic proxy seam', async () => {
    const unavailableCdp = new MockCdp();
    unavailableCdp.currentBrowserSpaceSnapshotAvailable = false;
    const unavailable = await inspectTask12ObservableSeams(unavailableCdp);
    expect(unavailable.fullPassageAvailable).toBe(false);
    expect(unavailable.focusedWindow.status).toBe('unavailable');
    expect(unavailable.selectedSpace.status).toBe('unavailable');
    expect(unavailable.spaceAssignments.status).toBe('unavailable');

    const availableCdp = new MockCdp();
    availableCdp.currentBrowserSpaceSnapshotAvailable = true;
    expect(await inspectTask12RuntimeBinding(availableCdp)).toEqual({status: 'available'});
    const available = await inspectTask12ObservableSeams(availableCdp);
    expect(available).toEqual({
      activeMahoProfile: {status: 'available', source: 'selected context'},
      focusedWindow: {status: 'available', source: 'browserProxy.getCurrentBrowserSpaceSnapshot'},
      selectedSpace: {status: 'available', source: 'browserProxy.getCurrentBrowserSpaceSnapshot'},
      spaceAssignments: {status: 'available', source: 'browserProxy.getCurrentBrowserSpaceSnapshot'},
      backendGlobalValues: {status: 'available', source: 'global snapshot'},
      fullPassageAvailable: true,
    });
    expect(availableCdp.expressions.join('\n')).toContain('getCurrentBrowserSpaceSnapshot');
  });

  test('checks missing runtime binding before any capture or mutation', async () => {
    const cdp = new MockCdp();
    cdp.availability = {
      status: 'missing-runtime-binding',
      reason: MISSING_RUNTIME_BINDING,
      missing: ['window.settingsStore'],
    };
    const calls: string[] = [];
    const executor = new Task12RuntimeScenarioExecutor(cdp, {
      captureBefore: () => { calls.push('before'); return {}; },
      captureAfter: () => { calls.push('after'); return {}; },
    });

    const result: Task12ScenarioResult<{}> = await executor.execute('happy-b-only-mutation', requests);
    expect(result).toEqual(cdp.availability);
    expect(calls).toEqual([]);
    expect(cdp.expressions).toHaveLength(1);
  });
});

describe('scenario failure proofs', () => {
  test('captures before and after, proves zero writes, then cleans up', async () => {
    const cdp = new MockCdp();
    const calls: string[] = [];
    const before = {writes: 0};
    const after = {writes: 0};
    const executor = new Task12RuntimeScenarioExecutor(cdp, {
      captureBefore: scenario => { calls.push(`before:${scenario}`); return before; },
      captureAfter: scenario => { calls.push(`after:${scenario}`); return after; },
      assertNoWrites: (actualBefore, actualAfter, scenario) => {
        calls.push(`zero-writes:${scenario}`);
        expect(actualBefore).toBe(before);
        expect(actualAfter).toBe(after);
      },
      cleanup: scenario => { calls.push(`cleanup:${scenario}`); },
    });

    const result = await executor.execute('failure-unknown-target', requests);
    expect(result.status).toBe('passed');
    expect(calls).toEqual([
      'before:failure-unknown-target',
      'after:failure-unknown-target',
      'zero-writes:failure-unknown-target',
      'cleanup:failure-unknown-target',
    ]);
    expect(cdp.expressions[1]).toContain('new MutationObserver(inspect)');
    expect(cdp.expressions[1]).toContain('store.subscribe(inspect)');
  });

  test('safe delete cancellation is also proved as a zero-write scenario', async () => {
    const cdp = new MockCdp();
    const calls: string[] = [];
    const executor = new Task12RuntimeScenarioExecutor(cdp, {
      captureBefore: () => ({writes: 0}),
      captureAfter: () => ({writes: 0}),
      assertNoWrites: (_before, _after, scenario) => { calls.push(`zero-writes:${scenario}`); },
    });

    await executor.execute('lifecycle-safe-delete-cancel', requests);
    expect(calls).toEqual(['zero-writes:lifecycle-safe-delete-cancel']);
  });

  test('source has no switchProfile, fixed waits, polling helpers, intervals, or Bun.sleep', async () => {
    const source = await Bun.file(new URL('./task12_runtime_scenarios.ts', import.meta.url)).text();
    expect(source).not.toContain('switchProfile(');
    expect(source).not.toContain('setActiveSpaceId(');
    expect(source).not.toContain('switchToSpace(');
    expect(source).not.toContain('activateSpace(');
    expect(source).not.toContain('waitForCondition');
    expect(source).not.toContain('Bun.sleep');
    expect(source).not.toContain('setInterval(');
    expect(source).not.toContain('while (Date.now()');
    expect(source).toContain("waitForFrameUrl(cdp, 'chrome://settings'");
    expect(source).toContain('statePredicate(store.getSnapshot()) && domPredicate()');
  });
});
