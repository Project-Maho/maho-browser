// Pure ownership primitives; live process discovery is supplied by composition.
export interface ProcessRecord {
	readonly pid: number;
	readonly parentPid: number;
	readonly startToken: string;
	readonly commandLine: readonly string[];
}

export interface OwnedProcessIdentity {
	readonly pid: number;
	readonly startToken: string;
}

export interface CommandResult {
	readonly exitCode: number;
	readonly stdout: string;
	readonly stderr: string;
}

export interface ProcessCommandRunner {
	run(executable: string, argv: readonly string[]): Promise<CommandResult>;
}

export interface ProcessInventory {
	list(): Promise<readonly ProcessRecord[]>;
}

export type ProcessArgvReader = (
	pid: number,
	startToken: string,
) => Promise<readonly string[]>;

export type ProcessMetadata = Omit<ProcessRecord, "commandLine">;

export interface TargetProcessObservation {
	readonly targetId: string;
	readonly processId: number;
	readonly processStartToken: string;
}

export interface OwnedTargetCorrelationInput {
	readonly processes: readonly ProcessRecord[];
	readonly owner: OwnedProcessIdentity;
	readonly userDataDir: string;
	readonly baselineTargetIds: readonly string[];
	readonly discoveredTargetIds: readonly string[];
	readonly observations: readonly TargetProcessObservation[];
}

export interface OwnedTargetCorrelation {
	readonly targetId: string;
	readonly process: ProcessRecord;
	readonly ancestry: readonly ProcessRecord[];
}

const PS_EXECUTABLE = "/bin/ps";
const PS_ARGUMENTS = ["-axo", "pid=,ppid=,lstart="] as const;
const PS_ROW =
	/^\s*(\d+)\s+(\d+)\s+((?:Mon|Tue|Wed|Thu|Fri|Sat|Sun)\s+(?:Jan|Feb|Mar|Apr|May|Jun|Jul|Aug|Sep|Oct|Nov|Dec)\s+\s?\d{1,2}\s+\d{2}:\d{2}:\d{2}\s+\d{4})\s*$/;

function parsePositiveInteger(value: string, label: string): number {
	const parsed = Number(value);
	if (!Number.isSafeInteger(parsed) || parsed <= 0) {
		throw new Error(`Malformed macOS ps ${label}: ${value}`);
	}
	return parsed;
}

export function parseMacPsOutput(output: string): readonly ProcessMetadata[] {
	const records: ProcessMetadata[] = [];
	const seenPids = new Set<number>();
	const lines = output.split(/\r?\n/).filter((line) => line.trim().length > 0);

	for (const [index, line] of lines.entries()) {
		const match = PS_ROW.exec(line);
		if (!match) {
			throw new Error(`Malformed macOS ps row ${index + 1}`);
		}
		const pid = parsePositiveInteger(match[1], "pid");
		const parentPid = Number(match[2]);
		if (!Number.isSafeInteger(parentPid) || parentPid < 0) {
			throw new Error(`Malformed macOS ps parent pid: ${match[2]}`);
		}
		if (seenPids.has(pid)) {
			throw new Error(`Duplicate PID in macOS ps inventory: ${pid}`);
		}
		seenPids.add(pid);
		records.push({
			pid,
			parentPid,
			startToken: match[3],
		});
	}
	return records;
}

export function createMacProcessInventory(
	runner: ProcessCommandRunner,
	readArgv: ProcessArgvReader = async () => {
		throw new Error(
			"KERN_PROCARGS2 argv reader requires a native bridge; no live-ready TypeScript adapter is configured",
		);
	},
): ProcessInventory {
	return {
		async list() {
			const result = await runner.run(PS_EXECUTABLE, PS_ARGUMENTS);
			if (result.exitCode !== 0) {
				const detail = result.stderr.trim() || `exit ${result.exitCode}`;
				throw new Error(`macOS ps inventory failed: ${detail}`);
			}
			const metadata = parseMacPsOutput(result.stdout);
			return Promise.all(
				metadata.map(async (process) => {
					const commandLine = await readArgv(process.pid, process.startToken);
					if (commandLine.length === 0) {
						throw new Error(`Missing argv for PID ${process.pid}`);
					}
					return { ...process, commandLine };
				}),
			);
		},
	};
}

function buildProcessIndex(
	processes: readonly ProcessRecord[],
): ReadonlyMap<number, ProcessRecord> {
	const byPid = new Map<number, ProcessRecord>();
	for (const process of processes) {
		if (byPid.has(process.pid)) {
			throw new Error(
				`Duplicate process PID in ownership inventory: ${process.pid}`,
			);
		}
		byPid.set(process.pid, process);
	}
	return byPid;
}

function requireUniqueValues(
	values: readonly string[],
	label: string,
): Set<string> {
	const unique = new Set<string>();
	for (const value of values) {
		if (value.length === 0) throw new Error(`Missing ${label} identity`);
		if (unique.has(value))
			throw new Error(`Duplicate ${label} identity: ${value}`);
		unique.add(value);
	}
	return unique;
}

function rejectOwnerCycle(
	owner: ProcessRecord,
	byPid: ReadonlyMap<number, ProcessRecord>,
): void {
	const visited = new Set<number>();
	let current: ProcessRecord | undefined = owner;
	while (current) {
		if (visited.has(current.pid)) {
			throw new Error(`cycle in process ancestry at PID ${current.pid}`);
		}
		visited.add(current.pid);
		current = byPid.get(current.parentPid);
	}
}

function traceOwnedAncestry(
	process: ProcessRecord,
	owner: OwnedProcessIdentity,
	marker: string,
	byPid: ReadonlyMap<number, ProcessRecord>,
): readonly ProcessRecord[] {
	const ancestry: ProcessRecord[] = [];
	const visited = new Set<number>();
	let current: ProcessRecord | undefined = process;

	while (current && current.pid !== owner.pid) {
		if (visited.has(current.pid)) {
			throw new Error(`cycle in process ancestry at PID ${current.pid}`);
		}
		visited.add(current.pid);
		ancestry.push(current);
		current = byPid.get(current.parentPid);
	}

	if (!current) {
		throw new Error(
			`Target process ${process.pid} is not a descendant of owned PID ${owner.pid}`,
		);
	}
	ancestry.push(current);
	if (current.startToken !== owner.startToken) {
		throw new Error(`Owned root start token mismatch for PID ${owner.pid}`);
	}
	for (const ancestor of ancestry) {
		if (!ancestor.commandLine.includes(marker)) {
			throw new Error(
				`Exact user-data-dir marker missing from PID ${ancestor.pid}`,
			);
		}
	}
	return ancestry;
}

export function correlateSingleOwnedTarget(
	input: OwnedTargetCorrelationInput,
): OwnedTargetCorrelation {
	const byPid = buildProcessIndex(input.processes);
	const owner = byPid.get(input.owner.pid);
	if (!owner) throw new Error(`Owned root PID ${input.owner.pid} is missing`);
	if (owner.startToken !== input.owner.startToken) {
		throw new Error(
			`Owned root start token mismatch for PID ${input.owner.pid}`,
		);
	}

	rejectOwnerCycle(owner, byPid);

	const observedTargetIds = new Set<string>();
	const observedProcessIds = new Set<number>();
	for (const observation of input.observations) {
		if (observedTargetIds.has(observation.targetId)) {
			throw new Error(`Duplicate target observation: ${observation.targetId}`);
		}
		if (observedProcessIds.has(observation.processId)) {
			throw new Error(
				`Duplicate process mapping for PID ${observation.processId}`,
			);
		}
		observedTargetIds.add(observation.targetId);
		observedProcessIds.add(observation.processId);
	}

	if (input.observations.length === 0) {
		throw new Error(
			"Missing explicit target-to-process correlation observation",
		);
	}

	const baseline = requireUniqueValues(
		input.baselineTargetIds,
		"baseline target",
	);
	const discovered = requireUniqueValues(
		input.discoveredTargetIds,
		"discovered target",
	);
	const newTargetIds = new Set(
		[...discovered].filter((targetId) => !baseline.has(targetId)),
	);
	const hasExactObservationCoverage =
		observedTargetIds.size === newTargetIds.size &&
		[...observedTargetIds].every((targetId) => newTargetIds.has(targetId));
	if (!hasExactObservationCoverage) {
		throw new Error(
			"Target observation coverage must exactly equal non-baseline discovered targets",
		);
	}
	if (newTargetIds.size !== 1) {
		throw new Error(`Expected one owned target, found ${newTargetIds.size}`);
	}
	const candidates: OwnedTargetCorrelation[] = [];
	const marker = `--user-data-dir=${input.userDataDir}`;
	for (const observation of input.observations) {
		if (!discovered.has(observation.targetId)) {
			throw new Error(
				`Observation target was not discovered: ${observation.targetId}`,
			);
		}
		if (baseline.has(observation.targetId)) {
			throw new Error(
				`Target identity belongs to baseline: ${observation.targetId}`,
			);
		}
		const process = byPid.get(observation.processId);
		if (!process) {
			throw new Error(
				`Observed process PID is missing: ${observation.processId}`,
			);
		}
		if (process.startToken !== observation.processStartToken) {
			throw new Error(
				`PID reuse mismatch for observed process ${observation.processId}`,
			);
		}
		candidates.push({
			targetId: observation.targetId,
			process,
			ancestry: traceOwnedAncestry(process, input.owner, marker, byPid),
		});
	}

	if (candidates.length !== 1) {
		throw new Error(`Expected one owned target, found ${candidates.length}`);
	}
	return candidates[0];
}
