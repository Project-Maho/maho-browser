import {describe, expect, test} from 'bun:test';

import {
  createDefaultTask12Runtime,
  runDefaultTask12Qa,
  type Task12DefaultRuntimeHost,
} from './task12_default_runtime.js';
import {MISSING_RUNTIME_BINDING, type Task12RuntimeDependencies} from './qa_two_profile_settings.js';

const target = {
  id: 'settings-target',
  type: 'page',
  url: 'chrome://maho-settings/?pane=profiles',
  title: 'Maho Settings',
  webSocketDebuggerUrl: 'ws://127.0.0.1/devtools/page/settings-target',
};

function readyHost(options: {profileObservablesSnapshotMethod?: boolean} = {}): {
  readonly host: Task12DefaultRuntimeHost;
  readonly expressions: string[];
  readonly calls: {connect: number; close: number; evidence: number; mutation: number; profileObservables: number};
} {
  const expressions: string[] = [];
  const calls = {connect: 0, close: 0, evidence: 0, mutation: 0, profileObservables: 0};
  const profileObservablesSnapshotMethod = options.profileObservablesSnapshotMethod ?? true;
  const cdp = {
    async eval<T>(expression: string): Promise<T | undefined> {
      expressions.push(expression);
      if (expression.includes("missing.push('settingsStore.'")) {
        return (profileObservablesSnapshotMethod ? {status: 'available'} : {
          status: 'missing-runtime-binding',
          reason: MISSING_RUNTIME_BINDING,
          missing: ['browserProxy.getProfileObservablesSnapshot'],
        }) as T;
      }
      if (expression.includes('return await handler["getCurrentBrowserSpaceSnapshot"]')) {
        return {snapshot: {
          focusedBrowserSessionId: 7,
          selectedSpace: {id: 'space-a', name: 'Personal'},
          assignedTabIds: ['tab-a'],
          assignedWindowIds: ['window-a'],
        }} as T;
      }
      if (expression.includes('handler.getProfileObservablesSnapshot()')) {
        calls.profileObservables++;
        return {snapshot: {
          activeBrowserProfileId: 'browser-authority',
          activeMahoProfileId: 'maho-authority',
          registryRevision: '18446744073709551615',
          lifecycleEntries: [
            {profileId: 'A', lifecycleState: 1},
            {profileId: 'B', lifecycleState: 2},
          ],
          pendingDeletionProfileIds: ['B'],
          deletedHistoryAvailability: 0,
        }} as T;
      }
      if (expression.includes('fullPassageAvailable')) {
        return {
          activeMahoProfile: {status: 'available', source: 'mock'},
          focusedWindow: {status: 'available', source: 'mock'},
          selectedSpace: {status: 'available', source: 'mock'},
          spaceAssignments: {status: 'available', source: 'mock'},
          backendGlobalValues: {status: 'available', source: 'mock'},
          fullPassageAvailable: true,
        } as T;
      }
      return undefined;
    },
    async send(method: string, params: Record<string, unknown> = {}): Promise<unknown> {
      if (method === 'Runtime.evaluate') {
        const expression = String(params.expression);
        expressions.push(expression);
        if (expression === 'null') {
          return {result: {result: {exceptionDetails: {text: 'attach-only observable unavailable'}}}};
        }
        if (expression.includes('getCurrentBrowserSpaceSnapshot')) {
          return {result: {result: {value: {snapshot: {
            focusedBrowserSessionId: 7,
            selectedSpace: {id: 'space-a', name: 'Personal'},
            assignedTabIds: ['tab-a'],
            assignedWindowIds: ['window-a'],
          }}}}};
        }
        if (expression.includes('cloneNode(true)')) {
          return {result: {result: {value: {
            url: target.url,
            title: target.title,
            text: 'Settings',
            html: '<html><main>Settings</main></html>',
            selectedPane: 'Profiles',
            selectedTargetLabel: 'Work',
          }}}};
        }
        return {result: {result: {value: null}}};
      }
      if (method === 'Page.captureScreenshot') {
        return {result: {data: 'iVBORw0KGgoAAAANSUhEUgAAAAEAAAAB'}};
      }
      if (method === 'Target.getTargetInfo') {
        return {result: {targetInfo: {targetId: target.id, type: 'page', url: target.url, title: target.title}}};
      }
      if (method === 'Browser.getWindowForTarget') {
        return {result: {windowId: 7, bounds: {left: 0, top: 0, width: 100, height: 100}}};
      }
      return {result: {}};
    },
  };
  return {
    calls,
    expressions,
    host: {
      async inspectInfrastructure() {
        return {appFresh: true, cdpAvailable: true, relayAvailable: true, targets: [target]};
      },
      async connect() {
        calls.connect++;
        return {cdp, close: () => { calls.close++; }};
      },
      evidenceRootDirectory: '/mock/evidence',
      scenarioRequests: {} as NonNullable<Task12RuntimeDependencies['scenarioRequests']>,
      expectedChanges: scenario => ({scenario, profileB: {}, allowedCleanup: []}),
      createEvidenceSession: async () => {
        calls.evidence++;
        throw new Error('test stops before evidence');
      },
    },
  };
}

describe('Task12 default runtime composition', () => {
  test('ready CDP/store/proxy creates executor and snapshot-backed capture using one generated call', async () => {
    const runtime = readyHost();
    const composed = await createDefaultTask12Runtime(runtime.host);

    expect(composed.status).toBe('ready');
    if (composed.status !== 'ready') return;
    expect(composed.dependencies.runtimeScenarioExecutor).toBeDefined();
    expect(composed.dependencies.captureState).toBeDefined();
    expect(composed.dependencies.currentBrowserSpaceSnapshotReader).toBe(composed.browserProxy);
    expect(composed.dependencies.profileObservablesSnapshotReader).toBe(composed.browserProxy);

    const captured = await composed.dependencies.captureState!(
      'happy-b-only-mutation',
      'before',
      {
        runDirectory: '/mock',
        writeText: async () => undefined,
        writePng: async () => undefined,
      } as never,
    );

    expect(captured.snapshot.focusedWindowId).toEqual({status: 'observed', value: '7'});
    expect(captured.snapshot.activeBrowserProfileId).toEqual({status: 'observed', value: 'browser-authority'});
    expect(captured.snapshot.activeMahoProfileId).toEqual({status: 'observed', value: 'maho-authority'});
    expect(captured.snapshot.selectedSpace).toEqual({
      status: 'observed', value: {id: 'space-a', name: 'Personal'},
    });
    expect(captured.snapshot.spaceAssignments).toEqual({
      status: 'observed', value: {tabIds: ['tab-a'], windowIds: ['window-a']},
    });
    expect(runtime.expressions.filter(value =>
      value.includes('return await handler["getCurrentBrowserSpaceSnapshot"]'))).toHaveLength(1);
    const profileObservablesExpressions = runtime.expressions.filter(value =>
      value.includes('handler.getProfileObservablesSnapshot()'));
    expect(profileObservablesExpressions).toHaveLength(1);
    expect(profileObservablesExpressions[0]!.match(/handler\.getProfileObservablesSnapshot\(\)/g)).toHaveLength(1);
    expect(profileObservablesExpressions[0]).toContain('snapshot.registryRevision.toString()');
    expect(profileObservablesExpressions[0]).toContain('lifecycleState:entry.lifecycleState');
    expect(profileObservablesExpressions[0]).not.toContain('getSelectedProfileContext');
    expect(profileObservablesExpressions[0]).not.toContain('getProfiles');
    expect(runtime.calls.profileObservables).toBe(1);
    expect(runtime.calls.connect).toBe(1);
    await composed.cleanup();
    expect(runtime.calls.close).toBe(1);
  });

  test('blocked infrastructure creates no binding, evidence, or mutation surface', async () => {
    const runtime = readyHost();
    const blockedHost: Task12DefaultRuntimeHost = {
      ...runtime.host,
      inspectInfrastructure: async () => ({
        appFresh: false,
        cdpAvailable: false,
        relayAvailable: false,
        targets: [],
      }),
    };

    const composed = await createDefaultTask12Runtime(blockedHost);

    expect(composed).toEqual({
      status: 'blocked-current-app',
      reasons: ['STALE_APP', 'CDP_9222_UNAVAILABLE', 'RELAY_18765_UNAVAILABLE'],
    });
    expect(runtime.calls).toEqual({connect: 0, close: 0, evidence: 0, mutation: 0, profileObservables: 0});
  });

  test('ready infrastructure with a missing generated snapshot method fails closed before evidence or mutation', async () => {
    const runtime = readyHost({profileObservablesSnapshotMethod: false});

    const composed = await createDefaultTask12Runtime(runtime.host);

    expect(composed).toEqual({
      status: 'missing-runtime-binding',
      reason: MISSING_RUNTIME_BINDING,
      missing: ['browserProxy.getProfileObservablesSnapshot'],
    });
    expect(runtime.expressions[0]).toContain('getCurrentBrowserSpaceSnapshot');
    expect(runtime.expressions[0]).toContain('getProfileObservablesSnapshot');
    expect(runtime.calls.evidence).toBe(0);
    expect(runtime.calls.mutation).toBe(0);
    expect(runtime.calls.profileObservables).toBe(0);
    expect(runtime.calls.close).toBe(1);
  });

  test('composed ready run invokes runTask12Qa with injected runtime dependencies', async () => {
    const runtime = readyHost();
    let injected: Task12RuntimeDependencies | undefined;

    const result = await runDefaultTask12Qa(runtime.host, async dependencies => {
      injected = dependencies;
      return {status: 'blocked-missing-observable', missing: ['focused-window']};
    });

    expect(result).toEqual({status: 'blocked-missing-observable', missing: ['focused-window']});
    expect(injected?.runtimeScenarioExecutor).toBeDefined();
    expect(injected?.captureState).toBeDefined();
    expect(injected?.profileObservablesSnapshotReader).toBeDefined();
    expect(runtime.calls.close).toBe(1);
  });

  test('live state expressions read host A and target B independently and contain no null placeholders', async () => {
    const runtime = readyHost();
    const composed = await createDefaultTask12Runtime({
      ...runtime.host,
      liveProfiles: {hostProfileId: 'A', targetProfileId: 'B', disposableProfileId: 'D'},
    });
    expect(composed.status).toBe('ready');
    if (composed.status !== 'ready') return;
    const expressions = composed.dependencies.stateExpressions!;
    expect(Object.values(expressions)).not.toContain('null');
    expect(Object.keys(expressions)).not.toContain('activeBrowserProfileId');
    expect(Object.keys(expressions)).not.toContain('activeMahoProfileId');
    expect(Object.keys(expressions)).not.toContain('lifecycle');
    expect(expressions.profileA).toContain('getSelectedProfileContext');
    expect(expressions.profileB).toContain('B');
    expect(expressions.globalState).toContain('getGlobalSettingsSnapshot');
    await composed.cleanup();
  });
});
