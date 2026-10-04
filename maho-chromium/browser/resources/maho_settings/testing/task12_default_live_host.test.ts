import {describe, expect, test} from 'bun:test';

import {
  createDefaultTask12LiveQaHost,
  defaultTask12LiveQaHost,
  runTask12LiveQa,
  type HardenedTask12LiveQaHost,
  type Task12LiveQaHost,
} from './task12_live_qa_entry.js';
import type {CleanupFinalBrowserStateHooks, ProcessRecord} from './task12_final_cleanup.js';
import type {ProfileScenarioHost, ProfileScenarioSeed, ProfileScenarioSnapshot} from './task12_profile_scenarios.js';

function createDefaultHostAtBoundary(
  candidate: Partial<HardenedTask12LiveQaHost>,
): Task12LiveQaHost & HardenedTask12LiveQaHost {
  if (typeof candidate.lifecyclePlanOptions !== 'function' ||
      candidate.lifecycleDependencies === undefined ||
      typeof candidate.connectOwnedTarget !== 'function' ||
      candidate.navigationTimer === undefined ||
      typeof candidate.createProfileScenarioHost !== 'function' ||
      typeof candidate.finalCleanup !== 'function') {
    throw new TypeError('All six hardened live-host adapters are required');
  }
  return createDefaultTask12LiveQaHost({
    lifecyclePlanOptions: candidate.lifecyclePlanOptions,
    lifecycleDependencies: candidate.lifecycleDependencies,
    connectOwnedTarget: candidate.connectOwnedTarget,
    navigationTimer: candidate.navigationTimer,
    createProfileScenarioHost: candidate.createProfileScenarioHost,
    finalCleanup: candidate.finalCleanup,
  });
}

describe('Task 12 canonical default live host', () => {
  test('exports a side-effect-free factory that preserves defaults and installs exactly six hardened members', () => {
    let sideEffects = 0;
    const lifecyclePlanOptions: HardenedTask12LiveQaHost['lifecyclePlanOptions'] = (_app, temporary) => {
      sideEffects++;
      return {
        appExecutable: '/out/Maho',
        userDataDir: temporary.userDataDir,
        ports: {isAvailable: async () => true},
        processes: {list: async () => []},
        targets: {list: async () => []},
      };
    };
    const lifecycleDependencies: HardenedTask12LiveQaHost['lifecycleDependencies'] = {
      readiness: {arm: async () => undefined},
      spawner: {spawn: async () => ({pid: 1})},
      startTokens: {lookup: async () => 'start-token'},
      processes: {list: async () => []},
      targets: {list: async () => []},
    };
    const connectOwnedTarget: HardenedTask12LiveQaHost['connectOwnedTarget'] = async () => {
      sideEffects++;
      return {
        transport: {
          send: async () => undefined,
          on: () => () => undefined,
        },
        close: () => undefined,
      };
    };
    const navigationTimer: HardenedTask12LiveQaHost['navigationTimer'] = {
      set: () => 1,
      clear: () => undefined,
    };
    const createProfileScenarioHost: HardenedTask12LiveQaHost['createProfileScenarioHost'] = () => {
      sideEffects++;
      return {
        snapshot: async () => ({
          targetToken: 'target', contextRevision: 1n, profileRevision: 1n,
          profiles: [], selectedProfileId: null, ready: true,
        }),
        listProfileNames: async () => [],
        createProfile: async () => 'profile',
        registerCreatedProfile: () => undefined,
        seedProfile: async () => undefined,
        readProfile: async () => undefined,
        restoreSnapshot: async () => undefined,
      };
    };
    const finalCleanup: HardenedTask12LiveQaHost['finalCleanup'] = ownership => {
      sideEffects++;
      return {
        input: {
          baseline: {processes: [], profiles: [], targets: []},
          ownedProcessRoots: [], createdProfiles: [], ownedTargetIds: [],
          tempRoot: ownership.root, exitTimeoutMs: 1,
        },
        hooks: {
          deleteProfile: async () => undefined,
          listProfiles: async () => [],
          closeBrowser: async () => undefined,
          scanProcesses: async () => [],
          armExitObservation: identity => ({identity}),
          awaitExit: async () => true,
          signalProcess: async () => undefined,
          scanTargets: async () => [],
          removeTempRoot: async () => undefined,
          statPath: async () => ({exists: false}),
          persistReceipt: async () => undefined,
        },
      };
    };
    const hardened: HardenedTask12LiveQaHost = {
      lifecyclePlanOptions,
      lifecycleDependencies,
      connectOwnedTarget,
      navigationTimer,
      createProfileScenarioHost,
      finalCleanup,
    };

    expect(typeof createDefaultTask12LiveQaHost).toBe('function');
    const canonical = createDefaultTask12LiveQaHost(hardened);

    for (const [member, value] of Object.entries(defaultTask12LiveQaHost)) {
      expect(canonical[member as keyof Task12LiveQaHost]).toBe(value);
    }
    expect(Object.keys(canonical).sort()).toEqual([
      ...Object.keys(defaultTask12LiveQaHost),
      ...Object.keys(hardened),
    ].sort());
    expect(canonical.lifecyclePlanOptions).toBe(lifecyclePlanOptions);
    expect(canonical.lifecycleDependencies).toBe(lifecycleDependencies);
    expect(canonical.connectOwnedTarget).toBe(connectOwnedTarget);
    expect(canonical.navigationTimer).toBe(navigationTimer);
    expect(canonical.createProfileScenarioHost).toBe(createProfileScenarioHost);
    expect(canonical.finalCleanup).toBe(finalCleanup);
    expect(sideEffects).toBe(0);

    const partial = {
      lifecyclePlanOptions,
      lifecycleDependencies,
      connectOwnedTarget,
      navigationTimer,
      createProfileScenarioHost,
    } satisfies Partial<HardenedTask12LiveQaHost>;
    expect(() => createDefaultHostAtBoundary(partial)).toThrow(
      'All six hardened live-host adapters are required');
  });

  test('selects all six hardened adapters for owned lifecycle, run-scoped scenarios, and persisted cleanup', async () => {
    const calls: string[] = [];
    const userDataDir = '/tmp/task12-default/user-data';
    let processes: ProcessRecord[] = [
      {pid: 100, parentPid: 1, startToken: 'relay-start', userDataDir: ''},
      {pid: 200, parentPid: 1, startToken: 'app-start', userDataDir},
    ];
    let profiles: Array<{id: string; name: string}> = [];
    let snapshot: ProfileScenarioSnapshot = {
      targetToken: 'owned-target-token', contextRevision: 11n, profileRevision: 17n,
      profiles: [], selectedProfileId: null, ready: true,
    };
    const seeds = new Map<string, ProfileScenarioSeed>();
    const scenarioHost: ProfileScenarioHost = {
      snapshot: async () => structuredClone(snapshot),
      listProfileNames: async () => profiles.map(profile => profile.name),
      createProfile: async name => {
        const id = `profile-${profiles.length + 1}`;
        profiles.push({id, name});
        snapshot = {...snapshot, profiles: structuredClone(profiles)};
        return id;
      },
      registerCreatedProfile: kind => { calls.push(`register:${kind}`); },
      seedProfile: async (id, seed) => { seeds.set(id, structuredClone(seed)); },
      readProfile: async id => structuredClone(seeds.get(id)),
      restoreSnapshot: async value => { snapshot = structuredClone(value); },
    };
    const cleanupHooks: CleanupFinalBrowserStateHooks = {
      deleteProfile: async profile => { profiles = profiles.filter(value => value.id !== profile.id); },
      listProfiles: async () => structuredClone(profiles),
      closeBrowser: async () => { calls.push('cleanup:close'); },
      scanProcesses: async () => structuredClone(processes),
      armExitObservation: identity => ({identity}),
      awaitExit: async observation => {
        processes = processes.filter(process => process.pid !== observation.identity.pid);
        return true;
      },
      signalProcess: async () => undefined,
      scanTargets: async () => [],
      removeTempRoot: async () => undefined,
      statPath: async () => ({exists: false}),
      persistReceipt: async receipt => { calls.push(`cleanup:receipt:${receipt.passed}`); },
    };
    const listeners = new Map<string, (value: unknown) => void>();
    const adapterCalls = {
      lifecyclePlanOptions: [] as Array<{app: string; root: string; userDataDir: string}>,
      readiness: [] as string[],
      connectedTargets: [] as string[],
      timerSetMilliseconds: [] as number[],
      scenarioHostCreations: 0,
      finalCleanupRoots: [] as string[],
    };
    const hardened: HardenedTask12LiveQaHost = {
      lifecyclePlanOptions: (app, temporary) => {
        adapterCalls.lifecyclePlanOptions.push({app, ...temporary});
        return {
          appExecutable: '/out/Maho', userDataDir: temporary.userDataDir,
          ports: {isAvailable: async () => true},
          processes: {list: async () => []}, targets: {list: async () => []},
        };
      },
      lifecycleDependencies: {
        readiness: {arm: async url => { adapterCalls.readiness.push(url); calls.push(`arm:${url}`); }},
        spawner: {spawn: async spec => {
          calls.push(`spawn:${spec.executable}`);
          return {pid: spec.executable === 'bunx' ? 100 : 200};
        }},
        startTokens: {lookup: async pid => pid === 100 ? 'relay-start' : 'app-start'},
        processes: {list: async () => [
          {pid: 100, parentPid: 1, startToken: 'relay-start', commandLine: ['bunx']},
          {pid: 200, parentPid: 1, startToken: 'app-start', commandLine: ['/out/Maho', `--user-data-dir=${userDataDir}`]},
        ]},
        targets: {list: async () => [{id: 'owned-target', url: 'about:blank', processId: 200}]},
      },
      connectOwnedTarget: async targetId => {
        adapterCalls.connectedTargets.push(targetId);
        return {
          transport: {
            send: async method => {
              calls.push(method);
              if (method === 'Page.navigate') {
                listeners.get('Page.frameNavigated')?.({frame: {id: 'root', url: 'chrome://maho-settings/?pane=profiles'}});
                listeners.get('Page.lifecycleEvent')?.({frameId: 'root', name: 'load'});
              }
              return method === 'Runtime.evaluate' ? {result: {value: true}} : undefined;
            },
            on: (event, listener) => { listeners.set(event, listener); return () => { listeners.delete(event); }; },
          },
          close: () => undefined,
        };
      },
      navigationTimer: {
        set: (_callback, milliseconds) => {
          adapterCalls.timerSetMilliseconds.push(milliseconds);
          return 1;
        },
        clear: () => undefined,
      },
      createProfileScenarioHost: () => {
        adapterCalls.scenarioHostCreations++;
        return scenarioHost;
      },
      finalCleanup: (ownership, _plan, lifecycle, created) => {
        adapterCalls.finalCleanupRoots.push(ownership.root);
        return {
          input: {
            baseline: {processes: [], profiles: [], targets: []},
            ownedProcessRoots: [
              {pid: lifecycle.relay.pid, startToken: lifecycle.relay.startToken, userDataDir: ''},
              {pid: lifecycle.app.pid, startToken: lifecycle.app.startToken, userDataDir},
            ],
            createdProfiles: created ? created.names.map((name, index) => ({
              id: Object.values(created.ids)[index] ?? '', name,
            })) : [],
            ownedTargetIds: ownership.targetIds, tempRoot: ownership.root, exitTimeoutMs: 250,
          },
          hooks: cleanupHooks,
        };
      },
    };
    const canonical = createDefaultTask12LiveQaHost(hardened);
    const host: Task12LiveQaHost = {
      ...canonical,
      task11Passed: async () => true,
      appExists: async () => true,
      createTemporaryRoot: async () => ({root: '/tmp/task12-default', userDataDir}),
      profileObservablesAvailable: async () => true,
      runQa: async (_options, liveProfiles) => {
        calls.push(`qa:${liveProfiles.hostProfileId}:${liveProfiles.targetProfileId}`);
        return {status: 'passed', scenarios: []};
      },
    };

    const result = await runTask12LiveQa(
      {app: '/out/Maho', evidenceRoot: '/evidence', runId: 'canonical'}, host);

    expect(result.status).toBe('passed');
    expect(adapterCalls.lifecyclePlanOptions).toEqual([
      {app: '/out/Maho', root: '/tmp/task12-default', userDataDir},
    ]);
    expect(adapterCalls.readiness).toEqual([
      'http://127.0.0.1:18765/health',
      'http://127.0.0.1:9222/json/version',
    ]);
    expect(adapterCalls.connectedTargets).toEqual(['owned-target']);
    expect(adapterCalls.timerSetMilliseconds).toEqual([10_000]);
    expect(adapterCalls.scenarioHostCreations).toBe(1);
    expect(adapterCalls.finalCleanupRoots).toEqual(['/tmp/task12-default']);
    expect(calls).toContain('arm:http://127.0.0.1:18765/health');
    expect(calls).toContain('arm:http://127.0.0.1:9222/json/version');
    expect(calls).toContain('spawn:bunx');
    expect(calls).toContain('spawn:/out/Maho');
    expect(calls).toContain('register:A');
    expect(calls).toContain('register:lifecycle');
    expect(calls).toContain('qa:profile-1:profile-2');
    expect(calls.at(-1)).toBe('cleanup:receipt:true');
  });
});
