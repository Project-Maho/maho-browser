export interface ProcessIdentity {
  pid: number;
  startToken: string;
  userDataDir: string;
}

export interface ProcessRecord extends ProcessIdentity {
  parentPid: number;
}

export interface TargetRecord {
  id: string;
  profileId?: string;
  profileName?: string;
}

export interface CleanupInventory {
  baselineProcesses: ProcessIdentity[];
  baselineProfileIds: string[];
  baselineProfileNames: string[];
  baselineTargetIds: string[];
  ownedProcess: ProcessIdentity;
  createdProfileIds: string[];
  createdProfileNames: string[];
  createdTargetIds: string[];
  tempRoot: string;
}

export type CleanupSignal = "TERM" | "KILL";

export interface CleanupPrimitives {
  closeBrowser(): Promise<void>;
  listProcesses(): Promise<ProcessRecord[]>;
  signalProcess(process: ProcessIdentity, signal: CleanupSignal): Promise<void>;
  waitForExit(process: ProcessIdentity, signal: CleanupSignal): Promise<boolean>;
  listTargets(): Promise<TargetRecord[]>;
  closeTarget(id: string): Promise<void>;
  removeTempRoot(path: string): Promise<void>;
  statPath(path: string): Promise<boolean>;
  persistReceipt(receipt: CleanupReceipt): Promise<void>;
}

export interface CleanupReceipt {
  passed: boolean;
  cleaned: {
    processes: ProcessIdentity[];
    profileIds: string[];
    profileNames: string[];
    targetIds: string[];
  };
  remainingProcesses: ProcessIdentity[];
  remainingTargetIds: string[];
  tempRootAbsent: boolean;
  errors: string[];
}

const completed = new WeakMap<CleanupInventory, Promise<CleanupReceipt>>();

function identityKey(process: ProcessIdentity): string {
  return `${process.pid}\0${process.startToken}\0${process.userDataDir}`;
}

function errorMessage(error: unknown): string {
  return error instanceof Error ? error.message : String(error);
}

function ownedProcesses(inventory: CleanupInventory, processes: ProcessRecord[]): ProcessRecord[] {
  const baseline = new Set(inventory.baselineProcesses.map(identityKey));
  const byParent = new Map<number, ProcessRecord[]>();
  for (const process of processes) {
    const siblings = byParent.get(process.parentPid) ?? [];
    siblings.push(process);
    byParent.set(process.parentPid, siblings);
  }

  const exactRoot = processes.find((process) =>
    process.pid === inventory.ownedProcess.pid &&
    process.startToken === inventory.ownedProcess.startToken &&
    process.userDataDir === inventory.ownedProcess.userDataDir &&
    !baseline.has(identityKey(process))
  );
  if (!exactRoot) return [];

  const result: ProcessRecord[] = [];
  const queue = [exactRoot];
  const seen = new Set<string>();
  while (queue.length > 0) {
    const process = queue.shift()!;
    const key = identityKey(process);
    if (seen.has(key)) continue;
    seen.add(key);
    if (process.userDataDir !== inventory.ownedProcess.userDataDir || baseline.has(key)) continue;
    result.push(process);
    queue.push(...(byParent.get(process.pid) ?? []));
  }
  return result;
}

function isOwnedTarget(inventory: CleanupInventory, target: TargetRecord): boolean {
  if (inventory.baselineTargetIds.includes(target.id)) return false;
  if (target.profileId && inventory.baselineProfileIds.includes(target.profileId)) return false;
  if (target.profileName && inventory.baselineProfileNames.includes(target.profileName)) return false;
  return inventory.createdTargetIds.includes(target.id) ||
    (target.profileId !== undefined && inventory.createdProfileIds.includes(target.profileId)) ||
    (target.profileName !== undefined && inventory.createdProfileNames.includes(target.profileName));
}

async function runCleanup(
  inventory: CleanupInventory,
  primitives: CleanupPrimitives,
): Promise<CleanupReceipt> {
  const errors: string[] = [];
  const attempt = async (label: string, operation: () => Promise<void>): Promise<void> => {
    try {
      await operation();
    } catch (error) {
      errors.push(`${label}: ${errorMessage(error)}`);
    }
  };

  await attempt("Browser.close", () => primitives.closeBrowser());

  let discovered: ProcessRecord[] = [];
  try {
    discovered = ownedProcesses(inventory, await primitives.listProcesses());
  } catch (error) {
    errors.push(`initial process scan: ${errorMessage(error)}`);
  }

  for (const process of discovered) {
    await attempt(`TERM ${process.pid}`, async () => {
      await primitives.signalProcess(process, "TERM");
      if (!await primitives.waitForExit(process, "TERM")) {
        await primitives.signalProcess(process, "KILL");
        if (!await primitives.waitForExit(process, "KILL")) {
          throw new Error("process remained after KILL");
        }
      }
    });
  }

  let initialTargets: TargetRecord[] = [];
  try {
    initialTargets = (await primitives.listTargets()).filter((target) => isOwnedTarget(inventory, target));
  } catch (error) {
    errors.push(`initial target scan: ${errorMessage(error)}`);
  }
  for (const target of initialTargets) {
    await attempt(`close target ${target.id}`, () => primitives.closeTarget(target.id));
  }

  await attempt("remove temp root", () => primitives.removeTempRoot(inventory.tempRoot));
  let tempRootAbsent = false;
  try {
    tempRootAbsent = !await primitives.statPath(inventory.tempRoot);
  } catch (error) {
    errors.push(`temp root absence check: ${errorMessage(error)}`);
  }

  let remainingProcesses: ProcessIdentity[] = [];
  try {
    remainingProcesses = ownedProcesses(inventory, await primitives.listProcesses());
  } catch (error) {
    errors.push(`final process scan: ${errorMessage(error)}`);
  }

  let remainingTargetIds: string[] = [];
  try {
    remainingTargetIds = (await primitives.listTargets())
      .filter((target) => isOwnedTarget(inventory, target))
      .map((target) => target.id);
  } catch (error) {
    errors.push(`final target scan: ${errorMessage(error)}`);
  }

  const receipt: CleanupReceipt = {
    passed: errors.length === 0 && remainingProcesses.length === 0 && remainingTargetIds.length === 0 && tempRootAbsent,
    cleaned: {
      processes: discovered.map(({ pid, startToken, userDataDir }) => ({ pid, startToken, userDataDir })),
      profileIds: [...inventory.createdProfileIds],
      profileNames: [...inventory.createdProfileNames],
      targetIds: initialTargets.map((target) => target.id),
    },
    remainingProcesses: remainingProcesses.map(({ pid, startToken, userDataDir }) => ({ pid, startToken, userDataDir })),
    remainingTargetIds,
    tempRootAbsent,
    errors,
  };

  try {
    await primitives.persistReceipt(receipt);
  } catch (error) {
    receipt.errors.push(`persist receipt: ${errorMessage(error)}`);
    receipt.passed = false;
  }
  return receipt;
}

export function cleanupOwnedBrowser(
  inventory: CleanupInventory,
  primitives: CleanupPrimitives,
): Promise<CleanupReceipt> {
  const existing = completed.get(inventory);
  if (existing) return existing;
  const cleanup = runCleanup(inventory, primitives);
  completed.set(inventory, cleanup);
  return cleanup;
}
