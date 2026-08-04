export interface ProcessIdentity {
	readonly pid: number;
	readonly startToken: string;
	readonly userDataDir: string;
}

export interface ProcessRecord extends ProcessIdentity {
	readonly parentPid: number;
}

export interface TargetIdentity {
	readonly id: string;
	readonly type: string;
}

export interface ProfileIdentity {
	readonly id: string;
	readonly name: string;
}

export interface ExitObservation {
	readonly identity: ProcessIdentity;
}

export interface CleanupFinalBrowserStateInput {
	readonly baseline: {
		readonly processes: readonly ProcessIdentity[];
		readonly profiles: readonly ProfileIdentity[];
		readonly targets: readonly TargetIdentity[];
	};
	readonly ownedProcessRoots: readonly ProcessIdentity[];
	readonly createdProfiles: readonly ProfileIdentity[];
	readonly ownedTargetIds: readonly string[];
	readonly tempRoot: string;
	readonly exitTimeoutMs: number;
}

export interface CleanupFinalBrowserStateHooks {
	deleteProfile(profile: ProfileIdentity): Promise<void>;
	listProfiles(): Promise<ProfileIdentity[]>;
	closeBrowser(): Promise<void>;
	scanProcesses(): Promise<ProcessRecord[]>;
	armExitObservation(identity: ProcessIdentity): ExitObservation;
	awaitExit(observation: ExitObservation, timeoutMs: number): Promise<boolean>;
	signalProcess(
		process: ProcessIdentity,
		signal: "TERM" | "KILL",
	): Promise<void>;
	scanTargets(): Promise<TargetIdentity[]>;
	removeTempRoot(path: string): Promise<void>;
	statPath(path: string): Promise<{ readonly exists: boolean }>;
	persistReceipt(receipt: CleanupReceipt): Promise<void>;
}

export interface CleanupReceipt {
	passed: boolean;
	readonly ownedProcessRoots: ProcessIdentity[];
	readonly createdProfiles: ProfileIdentity[];
	readonly ownedTargetIds: string[];
	readonly tempRoot: string;
	cleanedProcesses: ProcessIdentity[];
	remainingOwnedProcesses: ProcessIdentity[];
	remainingOwnedTargetIds: string[];
	remainingOwnedProfiles: ProfileIdentity[];
	baselinePreserved: {
		processes: boolean;
		targets: boolean;
		profiles: boolean;
	};
	tempRootAbsent: boolean;
	readonly errors: string[];
}

const completedCleanups = new WeakMap<
	CleanupFinalBrowserStateInput,
	WeakMap<CleanupFinalBrowserStateHooks, Promise<CleanupReceipt>>
>();

function processKey(process: ProcessIdentity): string {
	return `${process.pid}\0${process.startToken}\0${process.userDataDir}`;
}

function sameProcess(left: ProcessIdentity, right: ProcessIdentity): boolean {
	return processKey(left) === processKey(right);
}

function sameTarget(left: TargetIdentity, right: TargetIdentity): boolean {
	return left.id === right.id && left.type === right.type;
}

function sameProfile(left: ProfileIdentity, right: ProfileIdentity): boolean {
	return left.id === right.id && left.name === right.name;
}

function errorText(error: unknown): string {
	return error instanceof Error ? error.message : String(error);
}

function discoverOwnedProcesses(
	processes: readonly ProcessRecord[],
	roots: readonly ProcessIdentity[],
	baseline: readonly ProcessIdentity[],
): ProcessRecord[] {
	const baselineKeys = new Set(baseline.map(processKey));
	const recordsByKey = new Map<string, ProcessRecord>();
	for (const process of processes) {
		const key = processKey(process);
		if (recordsByKey.has(key)) {
			throw new Error(
				`ambiguous process inventory contains duplicate identity ${process.pid}@${process.startToken}`,
			);
		}
		recordsByKey.set(key, process);
	}

	const rootKeys = new Set<string>();
	const exactRoots: ProcessRecord[] = [];
	for (const root of roots) {
		const key = processKey(root);
		if (rootKeys.has(key))
			throw new Error(
				`ambiguous owned process root ${root.pid}@${root.startToken}`,
			);
		rootKeys.add(key);
		if (baselineKeys.has(key))
			throw new Error(
				`owned process root overlaps baseline ${root.pid}@${root.startToken}`,
			);
		const record = recordsByKey.get(key);
		if (record) exactRoots.push(record);
	}

	const childrenByParent = new Map<number, ProcessRecord[]>();
	for (const process of processes) {
		const children = childrenByParent.get(process.parentPid) ?? [];
		children.push(process);
		childrenByParent.set(process.parentPid, children);
	}

	const ownerByProcess = new Map<string, string>();
	const owned: ProcessRecord[] = [];
	for (const root of exactRoots) {
		const rootKey = processKey(root);
		const queue = [root];
		const visited = new Set<string>();
		while (queue.length > 0) {
			const process = queue.shift();
			if (!process) break;
			const key = processKey(process);
			if (visited.has(key)) continue;
			visited.add(key);
			if (process.userDataDir !== root.userDataDir || baselineKeys.has(key))
				continue;

			const existingOwner = ownerByProcess.get(key);
			if (existingOwner && existingOwner !== rootKey) {
				throw new Error(
					`overlapping or ambiguous ownership for ${process.pid}@${process.startToken}`,
				);
			}
			ownerByProcess.set(key, rootKey);
			if (!existingOwner) owned.push(process);
			queue.push(...(childrenByParent.get(process.pid) ?? []));
		}
	}

	return owned;
}

async function performCleanup(
	input: CleanupFinalBrowserStateInput,
	hooks: CleanupFinalBrowserStateHooks,
): Promise<CleanupReceipt> {
	const receipt: CleanupReceipt = {
		passed: true,
		ownedProcessRoots: input.ownedProcessRoots.map((root) => ({ ...root })),
		createdProfiles: input.createdProfiles.map((profile) => ({ ...profile })),
		ownedTargetIds: [...input.ownedTargetIds],
		tempRoot: input.tempRoot,
		cleanedProcesses: [],
		remainingOwnedProcesses: [],
		remainingOwnedTargetIds: [],
		remainingOwnedProfiles: [],
		baselinePreserved: { processes: true, targets: true, profiles: true },
		tempRootAbsent: false,
		errors: [],
	};
	const fail = (stage: string, error: unknown): void => {
		receipt.passed = false;
		receipt.errors.push(`${stage}: ${errorText(error)}`);
	};

	for (const profile of input.createdProfiles) {
		try {
			await hooks.deleteProfile(profile);
		} catch (error) {
			fail(`delete profile ${profile.id}`, error);
		}
	}

	try {
		await hooks.closeBrowser();
	} catch (error) {
		fail("Browser.close", error);
	}

	let initiallyOwned: ProcessRecord[] = [];
	try {
		initiallyOwned = discoverOwnedProcesses(
			await hooks.scanProcesses(),
			input.ownedProcessRoots,
			input.baseline.processes,
		);
		receipt.cleanedProcesses = initiallyOwned.map((process) => ({
			pid: process.pid,
			startToken: process.startToken,
			userDataDir: process.userDataDir,
		}));
	} catch (error) {
		fail("owned process discovery", error);
	}

	const observations = new Map<string, ExitObservation>();
	for (const process of initiallyOwned) {
		try {
			observations.set(processKey(process), hooks.armExitObservation(process));
		} catch (error) {
			fail(`arm exit observation ${process.pid}`, error);
		}
	}

	const termCandidates: ProcessRecord[] = [];
	for (const process of initiallyOwned) {
		if (!observations.has(processKey(process))) continue;
		try {
			await hooks.signalProcess(process, "TERM");
			termCandidates.push(process);
		} catch (error) {
			fail(`TERM ${process.pid}`, error);
		}
	}

	const killCandidates: ProcessRecord[] = [];
	for (const process of termCandidates) {
		try {
			const observation = observations.get(processKey(process));
			if (!observation) throw new Error("exit observation is missing");
			const exited = await hooks.awaitExit(observation, input.exitTimeoutMs);
			if (!exited) killCandidates.push(process);
		} catch (error) {
			fail(`await TERM exit ${process.pid}`, error);
			killCandidates.push(process);
		}
	}

	for (const process of killCandidates) {
		try {
			await hooks.signalProcess(process, "KILL");
		} catch (error) {
			fail(`KILL ${process.pid}`, error);
			continue;
		}
		try {
			const observation = observations.get(processKey(process));
			if (!observation) throw new Error("exit observation is missing");
			if (!(await hooks.awaitExit(observation, input.exitTimeoutMs))) {
				fail(
					`await KILL exit ${process.pid}`,
					new Error("process remained after KILL"),
				);
			}
		} catch (error) {
			fail(`await KILL exit ${process.pid}`, error);
		}
	}

	try {
		await hooks.removeTempRoot(input.tempRoot);
	} catch (error) {
		fail("temp root removal", error);
	}

	try {
		receipt.tempRootAbsent = !(await hooks.statPath(input.tempRoot)).exists;
		if (!receipt.tempRootAbsent)
			fail(
				"temp root verification",
				new Error(`${input.tempRoot} still exists`),
			);
	} catch (error) {
		fail("temp root verification", error);
	}

	try {
		const finalProcesses = await hooks.scanProcesses();
		const finalKeys = new Set(finalProcesses.map(processKey));
		receipt.remainingOwnedProcesses = receipt.cleanedProcesses.filter(
			(process) => finalKeys.has(processKey(process)),
		);
		if (receipt.remainingOwnedProcesses.length > 0) {
			fail("final process verification", new Error("owned processes remain"));
		}
		receipt.baselinePreserved.processes = input.baseline.processes.every(
			(baseline) =>
				finalProcesses.some((process) => sameProcess(process, baseline)),
		);
		if (!receipt.baselinePreserved.processes) {
			fail(
				"baseline process verification",
				new Error("one or more baseline processes are missing"),
			);
		}
	} catch (error) {
		receipt.baselinePreserved.processes = false;
		fail("final process verification", error);
	}

	try {
		const finalTargets = await hooks.scanTargets();
		const finalTargetIds = new Set(finalTargets.map((target) => target.id));
		receipt.remainingOwnedTargetIds = input.ownedTargetIds.filter((id) =>
			finalTargetIds.has(id),
		);
		if (receipt.remainingOwnedTargetIds.length > 0) {
			fail("final target verification", new Error("owned targets remain"));
		}
		receipt.baselinePreserved.targets = input.baseline.targets.every(
			(baseline) => finalTargets.some((target) => sameTarget(target, baseline)),
		);
		if (!receipt.baselinePreserved.targets) {
			fail(
				"baseline target verification",
				new Error("one or more baseline targets are missing"),
			);
		}
	} catch (error) {
		receipt.baselinePreserved.targets = false;
		fail("final target verification", error);
	}

	try {
		const finalProfiles = await hooks.listProfiles();
		const finalProfileIds = new Set(finalProfiles.map((profile) => profile.id));
		receipt.remainingOwnedProfiles = input.createdProfiles.filter((profile) =>
			finalProfileIds.has(profile.id),
		);
		for (const profile of receipt.remainingOwnedProfiles) {
			fail(
				"final profile verification",
				new Error(`created profile ${profile.id} remains`),
			);
		}
		receipt.baselinePreserved.profiles = input.baseline.profiles.every(
			(baseline) =>
				finalProfiles.some((profile) => sameProfile(profile, baseline)),
		);
		if (!receipt.baselinePreserved.profiles) {
			const missing = input.baseline.profiles.find(
				(baseline) =>
					!finalProfiles.some((profile) => sameProfile(profile, baseline)),
			);
			fail(
				"baseline profile verification",
				new Error(
					`baseline profile ${missing?.id ?? "unknown"} is missing or changed`,
				),
			);
		}
	} catch (error) {
		receipt.baselinePreserved.profiles = false;
		fail("final profile verification", error);
	}

	try {
		await hooks.persistReceipt(receipt);
	} catch (error) {
		fail("receipt persistence", error);
	}

	return receipt;
}

export function cleanupFinalBrowserState(
	input: CleanupFinalBrowserStateInput,
	hooks: CleanupFinalBrowserStateHooks,
): Promise<CleanupReceipt> {
	let byHooks = completedCleanups.get(input);
	if (!byHooks) {
		byHooks = new WeakMap();
		completedCleanups.set(input, byHooks);
	}
	const existing = byHooks.get(hooks);
	if (existing) return existing;
	const cleanup = performCleanup(input, hooks);
	byHooks.set(hooks, cleanup);
	return cleanup;
}
