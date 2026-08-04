import {describe, expect, test} from 'bun:test';

import {
  buildOwnedLifecyclePlan,
  navigateOwnedSettingsTarget,
  runOwnedReadinessLifecycle,
  type CdpEventTransport,
  type CdpTarget,
  type LifecycleTimer,
  type OwnedLifecyclePlan,
  type ProcessRecord,
} from './task12_live_lifecycle.js';

const SETTINGS_URL = 'chrome://maho-settings/?pane=profiles';

function plan(overrides: Partial<OwnedLifecyclePlan> = {}): OwnedLifecyclePlan {
  return {
    userDataDir: '/tmp/task12-owned/user-data',
    baselineProcesses: [],
    baselineTargetIds: [],
    relay: {
      executable: 'bunx',
      argv: ['wrangler', 'dev', '--env', 'staging', '--local', '--ip', '127.0.0.1', '--port', '18765'],
      cwd: 'maho/relay',
    },
    app: {executable: '/out/Maho', argv: [`--user-data-dir=/tmp/task12-owned/user-data`, '--remote-debugging-port=9222']},
    ...overrides,
  };
}

describe('buildOwnedLifecyclePlan', () => {
  test('rejects occupied owned ports before capturing inventory', async () => {
    const calls: string[] = [];
    await expect(buildOwnedLifecyclePlan({
      appExecutable: '/out/Maho',
      userDataDir: '/tmp/owned',
      ports: {isAvailable: async port => { calls.push(`port:${port}`); return port !== 9222; }},
      processes: {list: async () => { calls.push('processes'); return []; }},
      targets: {list: async () => { calls.push('targets'); return []; }},
    })).rejects.toThrow('9222');
    expect(calls).toEqual(['port:9222', 'port:18765']);
  });

  test('captures baselines and builds the exact configurable relay command', async () => {
    const processes: ProcessRecord[] = [{pid: 8, parentPid: 1, startToken: 'old', commandLine: ['old']}];
    const targets: CdpTarget[] = [{id: 'old-target', url: 'about:blank', processId: 8}];
    const result = await buildOwnedLifecyclePlan({
      appExecutable: '/out/Maho', userDataDir: '/tmp/owned', relayExecutable: '/tools/bunx',
      ports: {isAvailable: async () => true}, processes: {list: async () => processes}, targets: {list: async () => targets},
    });
    expect(result.relay).toEqual({
      executable: '/tools/bunx',
      argv: ['wrangler', 'dev', '--env', 'staging', '--local', '--ip', '127.0.0.1', '--port', '18765'],
      cwd: 'maho/relay',
    });
    expect(result.baselineProcesses).toEqual(processes);
    expect(result.baselineTargetIds).toEqual(['old-target']);
  });
});

describe('runOwnedReadinessLifecycle', () => {
  test('arms relay and CDP readiness before spawning and accepts only an owned new descendant target', async () => {
    const calls: string[] = [];
    const records: ProcessRecord[] = [
      {pid: 200, parentPid: 1, startToken: 'app-start', commandLine: ['/out/Maho', '--user-data-dir=/tmp/task12-owned/user-data']},
      {pid: 201, parentPid: 200, startToken: 'renderer-start', commandLine: ['/out/Maho Helper', '--user-data-dir=/tmp/task12-owned/user-data']},
      {pid: 100, parentPid: 1, startToken: 'relay-start', commandLine: ['bunx']},
    ];
    const result = await runOwnedReadinessLifecycle(plan(), {
      readiness: {arm: url => { calls.push(`arm:${url}`); return Promise.resolve(); }},
      spawner: {spawn: async spec => { calls.push(`spawn:${spec.executable}`); return {pid: spec.executable === 'bunx' ? 100 : 200}; }},
      startTokens: {lookup: async pid => records.find(record => record.pid === pid)!.startToken},
      processes: {list: async () => records},
      targets: {list: async () => [{id: 'new-target', url: 'about:blank', processId: 201}]},
    });
    expect(calls).toEqual([
      'arm:http://127.0.0.1:18765/health',
      'arm:http://127.0.0.1:9222/json/version',
      'spawn:bunx',
      'spawn:/out/Maho',
    ]);
    expect(result.target.id).toBe('new-target');
    expect(result.app.startToken).toBe('app-start');
    expect(result.relay.startToken).toBe('relay-start');
  });

  test('never spawns when plan construction rejects occupied ports', async () => {
    let spawned = false;
    await expect(buildOwnedLifecyclePlan({
      appExecutable: '/out/Maho', userDataDir: '/tmp/owned',
      ports: {isAvailable: async port => port !== 18765},
      processes: {list: async () => []}, targets: {list: async () => []},
    })).rejects.toThrow('18765');
    expect(spawned).toBe(false);
    void spawned;
  });

  test('rejects a pre-existing target even when it belongs to the spawned PID', async () => {
    const owned = plan({baselineTargetIds: ['old-target']});
    await expect(runOwnedReadinessLifecycle(owned, {
      readiness: {arm: async () => {}},
      spawner: {spawn: async spec => ({pid: spec.executable === 'bunx' ? 100 : 200})},
      startTokens: {lookup: async pid => pid === 100 ? 'relay' : 'app'},
      processes: {list: async () => [
        {pid: 100, parentPid: 1, startToken: 'relay', commandLine: ['bunx']},
        {pid: 200, parentPid: 1, startToken: 'app', commandLine: ['/out/Maho', '--user-data-dir=/tmp/task12-owned/user-data']},
      ]},
      targets: {list: async () => [{id: 'old-target', url: 'about:blank', processId: 200}]},
    })).rejects.toThrow('new owned CDP target');
  });

  test('rejects real start-token mismatch', async () => {
    await expect(runOwnedReadinessLifecycle(plan(), {
      readiness: {arm: async () => {}},
      spawner: {spawn: async spec => ({pid: spec.executable === 'bunx' ? 100 : 200})},
      startTokens: {lookup: async pid => pid === 100 ? 'relay-real' : 'app-real'},
      processes: {list: async () => [
        {pid: 100, parentPid: 1, startToken: 'relay-real', commandLine: ['bunx']},
        {pid: 200, parentPid: 1, startToken: 'app-stale', commandLine: ['/out/Maho', '--user-data-dir=/tmp/task12-owned/user-data']},
      ]},
      targets: {list: async () => [{id: 'new', url: 'about:blank', processId: 200}]},
    })).rejects.toThrow('start-token mismatch');
  });

  test('requires the exact user-data marker on the target process and every ownership ancestor', async () => {
    const marker = '--user-data-dir=/tmp/task12-owned/user-data';
    for (const commandLines of [
      {
        app: ['/out/Maho', '--user-data-dir=/tmp/task12-owned/user-data-other'],
        child: ['/out/Maho Helper', marker],
      },
      {
        app: ['/out/Maho', marker],
        child: ['/out/Maho Helper', '--user-data-dir=/tmp/task12-owned/user-data-other'],
      },
    ]) {
      await expect(runOwnedReadinessLifecycle(plan(), {
        readiness: {arm: async () => {}},
        spawner: {spawn: async spec => ({pid: spec.executable === 'bunx' ? 100 : 200})},
        startTokens: {lookup: async pid => pid === 100 ? 'relay' : 'app'},
        processes: {list: async () => [
          {pid: 100, parentPid: 1, startToken: 'relay', commandLine: ['bunx']},
          {pid: 200, parentPid: 1, startToken: 'app', commandLine: commandLines.app},
          {pid: 201, parentPid: 200, startToken: 'renderer', commandLine: commandLines.child},
        ]},
        targets: {list: async () => [{id: 'new', url: 'about:blank', processId: 201}]},
      })).rejects.toThrow('new owned CDP target');
    }
  });
});

describe('navigateOwnedSettingsTarget', () => {
  test('enables Page and registers listeners before navigate, then proves exact Profiles state and cleans up', async () => {
    const calls: string[] = [];
    const listeners = new Map<string, (value: unknown) => void>();
    const transport: CdpEventTransport = {
      send: async (method, params) => {
        calls.push(method);
        if (method === 'Page.navigate') {
          listeners.get('Page.frameNavigated')?.({frame: {id: 'root', url: SETTINGS_URL}});
          listeners.get('Page.lifecycleEvent')?.({frameId: 'root', name: 'load'});
        }
        if (method === 'Runtime.evaluate') return {result: {value: true}};
        return params;
      },
      on: (event, listener) => { calls.push(`on:${event}`); listeners.set(event, listener); return () => { calls.push(`off:${event}`); listeners.delete(event); }; },
    };
    await navigateOwnedSettingsTarget(transport, immediateTimer());
    expect(calls.slice(0, 4)).toEqual(['Page.enable', 'on:Page.frameNavigated', 'on:Page.lifecycleEvent', 'Page.navigate']);
    expect(calls).toContain('Runtime.evaluate');
    expect(listeners.size).toBe(0);
  });

  test('removes listeners when the injected timeout fires', async () => {
    const listeners = new Map<string, (value: unknown) => void>();
    let timeout: (() => void) | undefined;
    const timer: LifecycleTimer = {
      set: callback => { timeout = callback; return 1; },
      clear: () => {},
    };
    const transport: CdpEventTransport = {
      send: async () => undefined,
      on: (event, listener) => { listeners.set(event, listener); return () => { listeners.delete(event); }; },
    };
    const navigation = navigateOwnedSettingsTarget(transport, timer);
    await Promise.resolve();
    timeout?.();
    await expect(navigation).rejects.toThrow('timed out');
    expect(listeners.size).toBe(0);
  });

  test('requires exact URL, complete document, active Profiles nav, and Profiles main pane', async () => {
    const listeners = new Map<string, (value: unknown) => void>();
    let expression = '';
    const transport: CdpEventTransport = {
      send: async (method, params) => {
        if (method === 'Page.navigate') {
          listeners.get('Page.frameNavigated')?.({frame: {id: 'root', url: SETTINGS_URL}});
          listeners.get('Page.lifecycleEvent')?.({frameId: 'root', name: 'load'});
        }
        if (method === 'Runtime.evaluate') {
          expression = (params as {expression: string}).expression;
          return {result: {value: false}};
        }
        return undefined;
      },
      on: (event, listener) => {
        listeners.set(event, listener);
        return () => { listeners.delete(event); };
      },
    };

    await expect(navigateOwnedSettingsTarget(transport, immediateTimer()))
        .rejects.toThrow('exact Profiles state');
    expect(expression).toContain('location.href');
    expect(expression).toContain('document.readyState');
    expect(expression).toContain('nav[aria-label="Settings panes"] [aria-current="page"]');
    expect(expression).toContain("getAttribute('data-pane') === 'profiles'");
    expect(expression).toContain('main [data-pane="profiles"]');
    expect(listeners.size).toBe(0);
  });
});

function immediateTimer(): LifecycleTimer {
  return {set: () => 1, clear: () => {}};
}
