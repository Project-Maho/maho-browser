import {describe, expect, test} from 'bun:test';
import {readFile} from 'node:fs/promises';

import {
  defaultTask12LiveQaHost,
  runTask12LiveQa,
  type Task12LiveQaHost,
} from './task12_live_qa_entry.js';
import type {CleanupFinalBrowserStateHooks, CleanupReceipt, ProcessRecord} from './task12_final_cleanup.js';
import type {ProfileScenarioHost, ProfileScenarioSeed, ProfileScenarioSnapshot} from './task12_profile_scenarios.js';

describe('Task 12 default live runner integration', () => {
  test('wires receipt-gated owned lifecycle, deterministic profiles, and verified cleanup', async () => {
    const source = await readFile(new URL('./task12_live_qa_entry.ts', import.meta.url), 'utf8');
    const receiptGate = source.indexOf('validateTask11Receipt');
    const runner = source.indexOf('export async function runTask12LiveQa');
    const lifecyclePlan = source.indexOf('buildOwnedLifecyclePlan', runner);
    const lifecycleRun = source.indexOf('runOwnedReadinessLifecycle', lifecyclePlan);
    const navigation = source.indexOf('navigateOwnedSettingsTarget', lifecycleRun);
    const profiles = source.indexOf('runProfileScenarios', navigation);
    const cleanup = source.indexOf('cleanupFinalBrowserState', profiles);

    expect(receiptGate).toBeGreaterThanOrEqual(0);
    expect(lifecyclePlan).toBeGreaterThan(receiptGate);
    expect(lifecycleRun).toBeGreaterThan(lifecyclePlan);
    expect(navigation).toBeGreaterThan(lifecycleRun);
    expect(profiles).toBeGreaterThan(navigation);
    expect(cleanup).toBeGreaterThan(profiles);
    expect(source).toContain("'--run-id'");
    expect(source).toMatch(/runProfileScenarios[\s\S]*runId/);
    expect(source).toMatch(/cleanupFinalBrowserState[\s\S]*\.passed/);
  });

  test('executes lifecycle, owned navigation, profile scenarios, QA, and verified cleanup in order', async () => {
    const calls: string[] = [];
    const userDataDir = '/tmp/task12-owned/user-data';
    const processRecords = (): ProcessRecord[] => [
      {pid: 100, parentPid: 1, startToken: 'relay-start', userDataDir},
      {pid: 200, parentPid: 1, startToken: 'app-start', userDataDir},
    ];
    let processes = processRecords();
    let profiles: Array<{id: string; name: string}> = [];
    let snapshot: ProfileScenarioSnapshot = {
      targetToken: 'target-token', contextRevision: 9007199254740993n,
      profileRevision: 9007199254740995n, profiles: [], selectedProfileId: null, ready: true,
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
      registerCreatedProfile: (kind, _id, _name) => calls.push(`register:${kind}`),
      seedProfile: async (id, seed) => { seeds.set(id, structuredClone(seed)); },
      readProfile: async id => structuredClone(seeds.get(id)),
      restoreSnapshot: async value => { snapshot = structuredClone(value); },
    };
    const persisted: CleanupReceipt[] = [];
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
      persistReceipt: async receipt => { persisted.push(structuredClone(receipt)); calls.push('cleanup:receipt'); },
    };
    const listeners = new Map<string, (value: unknown) => void>();
    const host: Task12LiveQaHost = {
      task11Passed: async () => { calls.push('receipt'); return true; },
      appExists: async () => true,
      profileObservablesAvailable: defaultTask12LiveQaHost.profileObservablesAvailable,
      cdpPortAvailable: async () => true,
      relayAvailable: async () => true,
      createTemporaryRoot: async () => ({root: '/tmp/task12-owned', userDataDir}),
      spawnApp: async () => { throw new Error('legacy spawn path must not run'); },
      awaitReadyAndNavigate: async () => { throw new Error('legacy readiness path must not run'); },
      provisionProfiles: async () => { throw new Error('legacy profile path must not run'); },
      cleanup: async () => { throw new Error('legacy cleanup path must not run'); },
      lifecyclePlanOptions: () => ({
        appExecutable: '/out/Maho', userDataDir,
        ports: {isAvailable: async () => true},
        processes: {list: async () => []}, targets: {list: async () => []},
      }),
      lifecycleDependencies: {
        readiness: {arm: async url => { calls.push(`arm:${url}`); }},
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
        calls.push(`connect:${targetId}`);
        return {transport: {
          send: async method => {
            calls.push(method);
            if (method === 'Page.navigate') {
              listeners.get('Page.frameNavigated')?.({frame: {id: 'root', url: 'chrome://maho-settings/?pane=profiles'}});
              listeners.get('Page.lifecycleEvent')?.({frameId: 'root', name: 'load'});
            }
            return method === 'Runtime.evaluate' ? {result: {value: true}} : undefined;
          },
          on: (event, listener) => { listeners.set(event, listener); return () => { listeners.delete(event); }; },
        }, close: () => undefined};
      },
      navigationTimer: {set: () => 1, clear: () => undefined},
      createProfileScenarioHost: () => scenarioHost,
      runQa: async (_options, liveProfiles) => {
        calls.push(`qa:${liveProfiles.hostProfileId}:${liveProfiles.targetProfileId}`);
        return {status: 'passed', scenarios: []};
      },
      finalCleanup: (ownership, _plan, lifecycle, created) => ({
        input: {
          baseline: {processes: [], profiles: [], targets: []},
          ownedProcessRoots: [
            {pid: lifecycle.relay.pid, startToken: lifecycle.relay.startToken, userDataDir},
            {pid: lifecycle.app.pid, startToken: lifecycle.app.startToken, userDataDir},
          ],
          createdProfiles: created ? created.names.map((name, index) => ({id: Object.values(created.ids)[index]!, name})) : [],
          ownedTargetIds: ownership.targetIds,
          tempRoot: ownership.root,
          exitTimeoutMs: 250,
        },
        hooks: cleanupHooks,
      }),
    };

    const result = await runTask12LiveQa({app: '/out/Maho', evidenceRoot: '/evidence', runId: 'integration'}, host);
    expect(result.status).toBe('passed');
    expect(persisted).toHaveLength(1);
    expect(persisted[0]?.passed).toBe(true);
    expect(calls.indexOf('receipt')).toBeLessThan(calls.indexOf('spawn:bunx'));
    expect(calls.indexOf('spawn:/out/Maho')).toBeLessThan(calls.indexOf('Page.navigate'));
    expect(calls.filter(call => call === 'Runtime.evaluate')).toHaveLength(2);
    expect(calls.indexOf('Page.navigate')).toBeLessThan(calls.indexOf('register:A'));
    expect(calls.indexOf('register:lifecycle')).toBeLessThan(calls.findIndex(value => value.startsWith('qa:')));
    expect(calls.findIndex(value => value.startsWith('qa:'))).toBeLessThan(calls.indexOf('cleanup:close'));
    expect(calls.at(-1)).toBe('cleanup:receipt');
  });
});
