import { describe, expect, test } from "bun:test";

import {
	correlateSingleOwnedTarget,
	createMacProcessInventory,
	type OwnedProcessIdentity,
	type ProcessArgvReader,
	type ProcessRecord,
	parseMacPsOutput,
	type TargetProcessObservation,
} from "./task12_process_ownership.js";

const USER_DATA_DIR = "/tmp/Task 12 Profile";
const MARKER = `--user-data-dir=${USER_DATA_DIR}`;
const APP: OwnedProcessIdentity = {
	pid: 200,
	startToken: "Sat Aug  1 10:00:00 2026",
};

function record(
	pid: number,
	parentPid: number,
	startToken: string,
	commandLine: readonly string[],
): ProcessRecord {
	return { pid, parentPid, startToken, commandLine };
}

function correlate(
	processes: readonly ProcessRecord[],
	observations: readonly TargetProcessObservation[],
	baselineTargetIds: readonly string[] = [],
	discoveredTargetIds: readonly string[] = observations.map(
		(observation) => observation.targetId,
	),
) {
	return correlateSingleOwnedTarget({
		processes,
		owner: APP,
		userDataDir: USER_DATA_DIR,
		baselineTargetIds,
		discoveredTargetIds,
		observations,
	});
}

describe("macOS process inventory", () => {
	test("parses only pid, ppid, and stable start identity metadata, including PPID 0", () => {
		const output = [
			"  200     0 Sat Aug  1 10:00:00 2026",
			"  201   200 Sat Aug  1 10:00:01 2026",
		].join("\n");

		expect(parseMacPsOutput(output)).toEqual([
			{ pid: 200, parentPid: 0, startToken: "Sat Aug  1 10:00:00 2026" },
			{ pid: 201, parentPid: 200, startToken: "Sat Aug  1 10:00:01 2026" },
		]);
		expect(parseMacPsOutput(output)[0]?.startToken).not.toBe("200");
		expect(parseMacPsOutput(output)[0]?.startToken).not.toBe(
			parseMacPsOutput(output)[1]?.startToken,
		);
	});

	test("composes metadata with an injected lossless per-PID argv reader", async () => {
		const calls: Array<{ executable: string; argv: readonly string[] }> = [];
		const argvReads: number[] = [];
		const expectedArgv = [
			"/Applications/Maho App",
			'--double="retained"',
			"--single='retained'",
			String.raw`--backslash=retained\exactly`,
			MARKER,
		];
		const readArgv: ProcessArgvReader = async (pid) => {
			argvReads.push(pid);
			return expectedArgv;
		};
		const inventory = createMacProcessInventory(
			{
				run: async (executable, argv) => {
					calls.push({ executable, argv });
					return {
						exitCode: 0,
						stdout: "200 0 Sat Aug  1 10:00:00 2026\n",
						stderr: "",
					};
				},
			},
			readArgv,
		);

		expect(await inventory.list()).toEqual([
			record(200, 0, "Sat Aug  1 10:00:00 2026", expectedArgv),
		]);
		expect(calls).toEqual([
			{
				executable: "/bin/ps",
				argv: ["-axo", "pid=,ppid=,lstart="],
			},
		]);
		expect(argvReads).toEqual([200]);
	});

	test("fails closed on malformed, ambiguous, duplicate, or failed inventories", async () => {
		for (const output of [
			"not a ps row",
			'200 1 Sat Aug  1 10:00:00 2026 /out/Maho "unterminated',
			[
				"200 1 Sat Aug  1 10:00:00 2026 /out/Maho",
				"200 1 Sat Aug  1 10:00:01 2026 /out/Other",
			].join("\n"),
		]) {
			expect(() => parseMacPsOutput(output)).toThrow();
		}

		const inventory = createMacProcessInventory({
			run: async () => ({ exitCode: 1, stdout: "", stderr: "ps failed" }),
		});
		await expect(inventory.list()).rejects.toThrow(/ps failed/);

		const successfulRunner = {
			run: async () => ({
				exitCode: 0,
				stdout: "200 0 Sat Aug  1 10:00:00 2026\n",
				stderr: "",
			}),
		};
		await expect(
			createMacProcessInventory(successfulRunner, async () => {
				throw new Error("KERN_PROCARGS2 failed");
			}).list(),
		).rejects.toThrow(/KERN_PROCARGS2 failed/);
		await expect(
			createMacProcessInventory(successfulRunner, async () => []).list(),
		).rejects.toThrow(/missing argv/i);
		await expect(
			createMacProcessInventory(successfulRunner).list(),
		).rejects.toThrow(/KERN_PROCARGS2.*native bridge/i);
	});
});

describe("owned CDP target correlation", () => {
	const validTree = [
		record(100, 1, "Sat Aug  1 09:59:59 2026", ["bunx", "wrangler"]),
		record(200, 1, APP.startToken, ["/out/Maho", MARKER]),
		record(201, 200, "Sat Aug  1 10:00:01 2026", [
			"/out/Maho Helper",
			MARKER,
			"--type=renderer",
		]),
	];
	const ownedObservation: TargetProcessObservation = {
		targetId: "new-owned",
		processId: 201,
		processStartToken: "Sat Aug  1 10:00:01 2026",
	};

	test("distinguishes relay from app and returns the single exact owned descendant", () => {
		expect(correlate(validTree, [ownedObservation])).toEqual({
			targetId: "new-owned",
			process: validTree[2],
			ancestry: [validTree[2], validTree[1]],
		});

		expect(() =>
			correlate(validTree, [
				{
					targetId: "relay-target",
					processId: 100,
					processStartToken: "Sat Aug  1 09:59:59 2026",
				},
			]),
		).toThrow(/descendant/);
	});

	test("rejects missing observations and pre-existing targets", () => {
		expect(() => correlate(validTree, [])).toThrow(/observation/);
		expect(() =>
			correlate(validTree, [ownedObservation], ["new-owned"]),
		).toThrow(/baseline/);
	});

	test("rejects duplicate target identities and duplicate process mappings", () => {
		expect(() =>
			correlate(validTree, [ownedObservation, ownedObservation]),
		).toThrow(/duplicate target/i);
		expect(() =>
			correlate(validTree, [
				ownedObservation,
				{
					...ownedObservation,
					targetId: "second-target",
				},
			]),
		).toThrow(/duplicate process/i);
	});

	test("requires exact coverage of every non-baseline discovered target", () => {
		expect(() =>
			correlate(validTree, [ownedObservation], [], ["new-owned", "unobserved"]),
		).toThrow(/observation coverage/i);
		expect(() => correlate(validTree, [ownedObservation], [], [])).toThrow(
			/observation coverage/i,
		);
		expect(() =>
			correlate(validTree, [ownedObservation], ["old-target"], ["old-target"]),
		).toThrow(/observation coverage/i);
	});

	test("rejects more than one newly discovered target even when all are observed", () => {
		const secondProcess = record(202, 200, "Sat Aug  1 10:00:02 2026", [
			"/out/Maho Helper",
			MARKER,
		]);
		expect(() =>
			correlate(
				[...validTree, secondProcess],
				[
					ownedObservation,
					{
						targetId: "second-owned",
						processId: 202,
						processStartToken: secondProcess.startToken,
					},
				],
				[],
				["new-owned", "second-owned"],
			),
		).toThrow(/expected one.*found 2/i);
	});

	test("rejects unrelated processes, cycles, and PID reuse mismatches", () => {
		const unrelated = [
			...validTree,
			record(300, 1, "Sat Aug  1 10:00:02 2026", ["/out/Other", MARKER]),
		];
		expect(() =>
			correlate(unrelated, [
				{
					targetId: "unrelated",
					processId: 300,
					processStartToken: "Sat Aug  1 10:00:02 2026",
				},
			]),
		).toThrow(/descendant/);

		const cycle = [
			record(200, 201, APP.startToken, ["/out/Maho", MARKER]),
			record(201, 200, "Sat Aug  1 10:00:01 2026", [
				"/out/Maho Helper",
				MARKER,
			]),
		];
		expect(() => correlate(cycle, [ownedObservation])).toThrow(/cycle/);

		expect(() =>
			correlate(
				[
					record(200, 1, "Sat Aug  1 11:00:00 2026", ["/out/Maho", MARKER]),
					validTree[2],
				],
				[ownedObservation],
			),
		).toThrow(/start token/i);
		expect(() =>
			correlate(validTree, [
				{
					...ownedObservation,
					processStartToken: "Sat Aug  1 11:00:01 2026",
				},
			]),
		).toThrow(/PID reuse/i);
	});

	test("requires the exact user-data marker throughout the owned chain", () => {
		for (const processes of [
			[
				record(200, 1, APP.startToken, ["/out/Maho", `${MARKER}-other`]),
				validTree[2],
			],
			[
				validTree[1],
				record(201, 200, ownedObservation.processStartToken, [
					"/out/Maho Helper",
					`${MARKER}-other`,
				]),
			],
		]) {
			expect(() => correlate(processes, [ownedObservation])).toThrow(
				/user-data-dir/,
			);
		}
	});
});
