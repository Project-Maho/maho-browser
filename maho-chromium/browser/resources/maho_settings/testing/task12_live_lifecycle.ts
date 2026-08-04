export interface ProcessRecord {
  pid: number;
  parentPid: number;
  startToken: string;
  commandLine: string[];
}

export interface CdpTarget {
  id: string;
  url: string;
  processId: number;
}

interface CommandSpec {
  executable: string;
  argv: string[];
  cwd?: string;
}

export interface OwnedLifecyclePlan {
  userDataDir: string;
  baselineProcesses: ProcessRecord[];
  baselineTargetIds: string[];
  relay: CommandSpec;
  app: CommandSpec;
}

interface Inventory<T> {
  list(): Promise<T[]>;
}

interface BuildOwnedLifecyclePlanOptions {
  appExecutable: string;
  userDataDir: string;
  relayExecutable?: string;
  ports: {isAvailable(port: number): Promise<boolean>};
  processes: Inventory<ProcessRecord>;
  targets: Inventory<CdpTarget>;
}

export async function buildOwnedLifecyclePlan(
    options: BuildOwnedLifecyclePlanOptions): Promise<OwnedLifecyclePlan> {
  const cdpPort = 9222;
  const relayPort = 18765;
  const availability = await Promise.all([
    options.ports.isAvailable(cdpPort),
    options.ports.isAvailable(relayPort),
  ]);
  const occupiedPorts = [cdpPort, relayPort].filter((_, index) => !availability[index]);
  if (occupiedPorts.length > 0) {
    throw new Error(`Owned port unavailable: ${occupiedPorts.join(', ')}`);
  }

  const [baselineProcesses, baselineTargets] = await Promise.all([
    options.processes.list(),
    options.targets.list(),
  ]);
  return {
    userDataDir: options.userDataDir,
    baselineProcesses,
    baselineTargetIds: baselineTargets.map(target => target.id),
    relay: {
      executable: options.relayExecutable ?? 'bunx',
      argv: [
        'wrangler', 'dev', '--env', 'staging', '--local', '--ip', '127.0.0.1',
        '--port', String(relayPort),
      ],
      cwd: 'maho/relay',
    },
    app: {
      executable: options.appExecutable,
      argv: [`--user-data-dir=${options.userDataDir}`, `--remote-debugging-port=${cdpPort}`],
    },
  };
}

interface SpawnedProcess {
  pid: number;
}

interface OwnedReadinessDependencies {
  readiness: {arm(url: string): Promise<void>};
  spawner: {spawn(spec: CommandSpec): Promise<SpawnedProcess>};
  startTokens: {lookup(pid: number): Promise<string>};
  processes: Inventory<ProcessRecord>;
  targets: Inventory<CdpTarget>;
}

interface OwnedProcess {
  pid: number;
  startToken: string;
}

interface OwnedReadinessResult {
  relay: OwnedProcess;
  app: OwnedProcess;
  target: CdpTarget;
}

export async function runOwnedReadinessLifecycle(
    plan: OwnedLifecyclePlan,
    dependencies: OwnedReadinessDependencies): Promise<OwnedReadinessResult> {
  const relayReady = dependencies.readiness.arm('http://127.0.0.1:18765/health');
  const cdpReady = dependencies.readiness.arm('http://127.0.0.1:9222/json/version');

  const relaySpawn = await dependencies.spawner.spawn(plan.relay);
  const appSpawn = await dependencies.spawner.spawn(plan.app);
  const [relayStartToken, appStartToken] = await Promise.all([
    dependencies.startTokens.lookup(relaySpawn.pid),
    dependencies.startTokens.lookup(appSpawn.pid),
    relayReady,
    cdpReady,
  ]);
  const [processes, targets] = await Promise.all([
    dependencies.processes.list(),
    dependencies.targets.list(),
  ]);

  const relayRecord = processes.find(record => record.pid === relaySpawn.pid);
  const appRecord = processes.find(record => record.pid === appSpawn.pid);
  if (!relayRecord || !appRecord || relayRecord.startToken !== relayStartToken ||
      appRecord.startToken !== appStartToken) {
    throw new Error('Owned process start-token mismatch');
  }

  const byPid = new Map(processes.map(record => [record.pid, record]));
  const userDataMarker = `--user-data-dir=${plan.userDataDir}`;
  const isAppDescendant = (pid: number): boolean => {
    const visited = new Set<number>();
    let current: ProcessRecord | undefined = byPid.get(pid);
    while (current && !visited.has(current.pid)) {
      if (!current.commandLine.includes(userDataMarker)) return false;
      if (current.pid === appSpawn.pid) return true;
      visited.add(current.pid);
      current = byPid.get(current.parentPid);
    }
    return false;
  };
  const baselineTargetIds = new Set(plan.baselineTargetIds);
  const target = targets.find(candidate =>
    !baselineTargetIds.has(candidate.id) && isAppDescendant(candidate.processId));
  if (!target) {
    throw new Error('Expected a new owned CDP target');
  }

  return {
    relay: {pid: relaySpawn.pid, startToken: relayStartToken},
    app: {pid: appSpawn.pid, startToken: appStartToken},
    target,
  };
}

export interface CdpEventTransport {
  send(method: string, params?: unknown): Promise<unknown>;
  on(event: string, listener: (value: unknown) => void): () => void;
}

export interface LifecycleTimer {
  set(callback: () => void, milliseconds: number): unknown;
  clear(handle: unknown): void;
}

const SETTINGS_URL = 'chrome://maho-settings/?pane=profiles';

export async function navigateOwnedSettingsTarget(
    transport: CdpEventTransport, timer: LifecycleTimer): Promise<void> {
  await transport.send('Page.enable');

  let navigated = false;
  let loaded = false;
  let rootFrameId: string | undefined;
  let resolveNavigation!: () => void;
  let rejectNavigation!: (error: Error) => void;
  const navigation = new Promise<void>((resolve, reject) => {
    resolveNavigation = resolve;
    rejectNavigation = reject;
  });
  const completeIfReady = () => {
    if (navigated && loaded) resolveNavigation();
  };
  const removeFrameListener = transport.on('Page.frameNavigated', value => {
    const event = value as {frame?: {id?: string; url?: string; parentId?: string}};
    if (event.frame?.url === SETTINGS_URL && !event.frame.parentId) {
      rootFrameId = event.frame.id;
      navigated = true;
      completeIfReady();
    }
  });
  const removeLifecycleListener = transport.on('Page.lifecycleEvent', value => {
    const event = value as {frameId?: string; name?: string};
    if (event.name === 'load' && event.frameId === rootFrameId) {
      loaded = true;
      completeIfReady();
    }
  });
  const timeout = timer.set(
      () => rejectNavigation(new Error('Settings navigation timed out')), 10_000);

  try {
    await transport.send('Page.navigate', {url: SETTINGS_URL});
    await navigation;
    const evaluation = await transport.send('Runtime.evaluate', {
      expression: `(() => {
        const active = document.querySelector('nav[aria-label="Settings panes"] [aria-current="page"]');
        return location.href === ${JSON.stringify(SETTINGS_URL)} &&
            document.readyState === 'complete' &&
            active?.getAttribute('data-pane') === 'profiles' &&
            document.querySelector('main [data-pane="profiles"]') !== null;
      })()`,
      returnByValue: true,
    }) as {result?: {value?: unknown}};
    if (evaluation.result?.value !== true) {
      throw new Error('Settings target did not reach the exact Profiles state');
    }
  } finally {
    timer.clear(timeout);
    removeLifecycleListener();
    removeFrameListener();
  }
}
