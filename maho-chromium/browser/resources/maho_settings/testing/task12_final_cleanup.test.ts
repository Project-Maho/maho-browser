import { describe, expect, test } from "bun:test";

import {
	type CleanupFinalBrowserStateHooks,
	type CleanupFinalBrowserStateInput,
	type CleanupReceipt,
	cleanupFinalBrowserState,
	type ProcessIdentity,
	type ProcessRecord,
	type ProfileIdentity,
	type TargetIdentity,
} from "./task12_final_cleanup.js";

const userDataDir = "/tmp/task12-owned/user-data";
const tempRoot = "/tmp/task12-owned";
const relay = {
	pid: 100,
	startToken: "relay-start",
	userDataDir,
} satisfies ProcessIdentity;
const app = {
	pid: 200,
	startToken: "app-start",
	userDataDir,
} satisfies ProcessIdentity;
const baselineProcess = {
	pid: 10,
	startToken: "baseline-start",
	userDataDir: "/tmp/baseline",
} satisfies ProcessIdentity;
const baselineTarget = {
	id: "baseline-target",
	type: "page",
} satisfies TargetIdentity;
const baselineProfiles = [
	{ id: "baseline-a", name: "Baseline A" },
	{ id: "baseline-b", name: "Baseline B" },
] satisfies ProfileIdentity[];
const createdProfiles = [
	{ id: "created-a", name: "Created A" },
	{ id: "created-b", name: "Created B" },
] satisfies ProfileIdentity[];
const ownedTargets = ["owned-page", "owned-worker"];

interface HarnessOptions {
	closeFails?: boolean;
	deleteFailsFor?: string;
	createdProfileRemains?: string;
	baselineProfileMissing?: string;
	persistenceFails?: boolean;
	termExits?: ReadonlySet<string>;
	killExits?: ReadonlySet<string>;
	processInventory?: ProcessRecord[];
}

const key = (identity: ProcessIdentity): string =>
	`${identity.pid}@${identity.startToken}`;

function makeHarness(options: HarnessOptions = {}) {
	const relayChild = {
		pid: 101,
		parentPid: relay.pid,
		startToken: "relay-child",
		userDataDir,
	} satisfies ProcessRecord;
	const appChild = {
		pid: 201,
		parentPid: app.pid,
		startToken: "app-child",
		userDataDir,
	} satisfies ProcessRecord;
	const appGrandchild = {
		pid: 202,
		parentPid: appChild.pid,
		startToken: "app-grandchild",
		userDataDir,
	} satisfies ProcessRecord;
	const mismatchedDescendant = {
		pid: 203,
		parentPid: appGrandchild.pid,
		startToken: "app-mismatched-descendant",
		userDataDir: "/tmp/foreign/user-data",
	} satisfies ProcessRecord;
	const foreign = {
		pid: 300,
		parentPid: 1,
		startToken: "foreign",
		userDataDir,
	} satisfies ProcessRecord;
	const baseline = { ...baselineProcess, parentPid: 1 } satisfies ProcessRecord;
	let processes = options.processInventory ?? [
		baseline,
		{ ...relay, parentPid: 1 },
		relayChild,
		{ ...app, parentPid: 1 },
		appChild,
		appGrandchild,
		mismatchedDescendant,
		foreign,
	];
	let profiles: ProfileIdentity[] = [...baselineProfiles, ...createdProfiles];
	let targets: TargetIdentity[] = [
		baselineTarget,
		...ownedTargets.map((id) => ({ id, type: "page" })),
	];
	let rootExists = true;
	const operations: string[] = [];
	const signals: string[] = [];
	const deletedProfiles: string[] = [];
	const persisted: CleanupReceipt[] = [];
	const armed = new Set<string>();
	const termExits =
		options.termExits ??
		new Set(
			processes
				.filter(
					(process) =>
						process.pid !== baseline.pid && process.pid !== foreign.pid,
				)
				.map(key),
		);
	const killExits = options.killExits ?? new Set(processes.map(key));

	const hooks: CleanupFinalBrowserStateHooks = {
		async deleteProfile(profile) {
			operations.push(`delete-profile:${profile.id}`);
			deletedProfiles.push(profile.id);
			if (options.deleteFailsFor === profile.id)
				throw new Error(`delete ${profile.id} failed`);
			profiles = profiles.filter((candidate) => candidate.id !== profile.id);
		},
		async listProfiles() {
			operations.push("list-profiles");
			let result = [...profiles];
			if (options.createdProfileRemains) {
				const missing = createdProfiles.find(
					(profile) => profile.id === options.createdProfileRemains,
				);
				if (missing && !result.some((profile) => profile.id === missing.id))
					result.push(missing);
			}
			if (options.baselineProfileMissing)
				result = result.filter(
					(profile) => profile.id !== options.baselineProfileMissing,
				);
			return result;
		},
		async closeBrowser() {
			operations.push("Browser.close");
			if (options.closeFails) throw new Error("Browser.close failed");
			targets = targets.filter((target) => !ownedTargets.includes(target.id));
		},
		async scanProcesses() {
			operations.push("scan-processes");
			return [...processes];
		},
		armExitObservation(identity) {
			operations.push(`arm:${key(identity)}`);
			armed.add(key(identity));
			return { identity };
		},
		async awaitExit(observation, timeoutMs) {
			operations.push(`await:${key(observation.identity)}:${timeoutMs}`);
			const identityKey = key(observation.identity);
			const lastSignal = [...signals]
				.reverse()
				.find((signal) => signal.endsWith(identityKey));
			const exited = lastSignal?.startsWith("TERM:")
				? termExits.has(identityKey)
				: killExits.has(identityKey);
			if (exited)
				processes = processes.filter((process) => key(process) !== identityKey);
			return exited;
		},
		async signalProcess(identity, signal) {
			expect(armed.has(key(identity))).toBe(true);
			operations.push(`${signal}:${key(identity)}`);
			signals.push(`${signal}:${key(identity)}`);
		},
		async scanTargets() {
			operations.push("scan-targets");
			return [...targets];
		},
		async removeTempRoot(path) {
			operations.push(`remove-temp:${path}`);
			rootExists = false;
		},
		async statPath(path) {
			operations.push(`stat:${path}`);
			return { exists: rootExists };
		},
		async persistReceipt(receipt) {
			operations.push("persist-receipt");
			if (options.persistenceFails) throw new Error("disk full");
			persisted.push(structuredClone(receipt));
		},
	};

	const input: CleanupFinalBrowserStateInput = {
		baseline: {
			processes: [baselineProcess],
			profiles: baselineProfiles,
			targets: [baselineTarget],
		},
		ownedProcessRoots: [relay, app],
		createdProfiles,
		ownedTargetIds: ownedTargets,
		tempRoot,
		exitTimeoutMs: 250,
	};

	return {
		hooks,
		input,
		operations,
		signals,
		deletedProfiles,
		persisted,
		relayChild,
		appChild,
		appGrandchild,
		mismatchedDescendant,
		foreign,
		hasProcess: (identity: ProcessIdentity) =>
			processes.some((process) => key(process) === key(identity)),
	};
}

describe("final cleanup v2 contract", () => {
	test("cleans relay and app roots plus exact-marker descendants while preserving baseline and foreign state", async () => {
		const harness = makeHarness();
		const receipt = await cleanupFinalBrowserState(
			harness.input,
			harness.hooks,
		);

		expect(receipt.passed).toBe(true);
		expect(receipt.ownedProcessRoots).toEqual([relay, app]);
		expect(receipt.cleanedProcesses.map(key).sort()).toEqual(
			[
				key(relay),
				key(harness.relayChild),
				key(app),
				key(harness.appChild),
				key(harness.appGrandchild),
			].sort(),
		);
		expect(
			harness.signals.some((signal) => signal.endsWith(key(baselineProcess))),
		).toBe(false);
		expect(
			harness.signals.some((signal) => signal.endsWith(key(harness.foreign))),
		).toBe(false);
		expect(receipt.remainingOwnedProcesses).toEqual([]);
		expect(receipt.remainingOwnedTargetIds).toEqual([]);
		expect(receipt.remainingOwnedProfiles).toEqual([]);
		expect(receipt.baselinePreserved).toEqual({
			processes: true,
			targets: true,
			profiles: true,
		});
		expect(receipt.tempRootAbsent).toBe(true);
		expect(harness.operations.at(-1)).toBe("persist-receipt");
		expect(harness.persisted).toEqual([receipt]);
	});

	test("preserves and never signals a topological descendant with a mismatched userDataDir marker", async () => {
		const harness = makeHarness();
		const receipt = await cleanupFinalBrowserState(
			harness.input,
			harness.hooks,
		);

		expect(receipt.passed).toBe(true);
		expect(receipt.cleanedProcesses.map(key)).not.toContain(
			key(harness.mismatchedDescendant),
		);
		expect(harness.hasProcess(harness.mismatchedDescendant)).toBe(true);
		expect(
			harness.signals.some((signal) =>
				signal.endsWith(key(harness.mismatchedDescendant)),
			),
		).toBe(false);
	});

	test("rejects overlapping ownership trees without signaling ambiguous identities", async () => {
		const harness = makeHarness({
			processInventory: [
				{ ...relay, parentPid: 1 },
				{ ...app, parentPid: relay.pid },
			],
		});
		const receipt = await cleanupFinalBrowserState(
			harness.input,
			harness.hooks,
		);

		expect(receipt.passed).toBe(false);
		expect(receipt.errors.join("\n")).toMatch(/overlapping|ambiguous/i);
		expect(harness.signals).toEqual([]);
	});

	test("deletes only registered current-run profiles before Browser.close and verifies all absent", async () => {
		const harness = makeHarness();
		const receipt = await cleanupFinalBrowserState(
			harness.input,
			harness.hooks,
		);

		expect(harness.deletedProfiles).toEqual(
			createdProfiles.map((profile) => profile.id),
		);
		expect(harness.operations.indexOf("delete-profile:created-a")).toBeLessThan(
			harness.operations.indexOf("Browser.close"),
		);
		expect(harness.operations.indexOf("delete-profile:created-b")).toBeLessThan(
			harness.operations.indexOf("Browser.close"),
		);
		expect(harness.deletedProfiles).not.toContain("baseline-a");
		expect(harness.deletedProfiles).not.toContain("baseline-b");
		expect(receipt.remainingOwnedProfiles).toEqual([]);
	});

	test.each([
		[{ deleteFailsFor: "created-a" }, /delete profile created-a/i],
		[
			{ createdProfileRemains: "created-b" },
			/created profile created-b.*remain/i,
		],
		[
			{ baselineProfileMissing: "baseline-a" },
			/baseline profile baseline-a.*missing/i,
		],
	] as const)("fails profile verification without deleting baseline profiles: %o", async (options, message) => {
		const harness = makeHarness(options);
		const receipt = await cleanupFinalBrowserState(
			harness.input,
			harness.hooks,
		);

		expect(receipt.passed).toBe(false);
		expect(receipt.errors.join("\n")).toMatch(message);
		expect(
			harness.deletedProfiles.every((id) =>
				createdProfiles.some((profile) => profile.id === id),
			),
		).toBe(true);
	});

	test("continues owned TERM/KILL cleanup when Browser.close fails", async () => {
		const harness = makeHarness({ closeFails: true });
		const receipt = await cleanupFinalBrowserState(
			harness.input,
			harness.hooks,
		);

		expect(receipt.passed).toBe(false);
		expect(receipt.errors.join("\n")).toMatch(/Browser\.close failed/);
		expect(harness.signals.some((signal) => signal.startsWith("TERM:"))).toBe(
			true,
		);
		expect(harness.operations.indexOf("Browser.close")).toBeLessThan(
			harness.operations.findIndex((operation) =>
				operation.startsWith("TERM:"),
			),
		);
	});

	test("arms exit observation before TERM and KILLs only identities whose bounded TERM wait times out", async () => {
		const appKey = key(app);
		const harness = makeHarness({
			termExits: new Set([appKey]),
			killExits: new Set([
				key(relay),
				key(harnessIdentity(101, "relay-child")),
				key(harnessIdentity(201, "app-child")),
				key(harnessIdentity(202, "app-grandchild")),
			]),
		});
		const receipt = await cleanupFinalBrowserState(
			harness.input,
			harness.hooks,
		);

		expect(receipt.passed).toBe(true);
		for (const term of harness.signals.filter((signal) =>
			signal.startsWith("TERM:"),
		)) {
			const identity = term.slice("TERM:".length);
			expect(harness.operations.indexOf(`arm:${identity}`)).toBeLessThan(
				harness.operations.indexOf(term),
			);
			expect(harness.operations).toContain(`await:${identity}:250`);
		}
		expect(harness.signals).not.toContain(`KILL:${appKey}`);
		expect(harness.signals).toContain(`KILL:${key(relay)}`);
	});

	test("persistence is attempted exactly once after verification and failure makes the returned receipt failed", async () => {
		const harness = makeHarness({ persistenceFails: true });
		const receipt = await cleanupFinalBrowserState(
			harness.input,
			harness.hooks,
		);

		expect(receipt.passed).toBe(false);
		expect(receipt.errors.join("\n")).toMatch(
			/receipt persistence.*disk full/i,
		);
		expect(
			harness.operations.filter((operation) => operation === "persist-receipt"),
		).toHaveLength(1);
		expect(harness.operations.at(-1)).toBe("persist-receipt");
	});

	test("is idempotent and exposes passed=false for callers to override QA PASS", async () => {
		const harness = makeHarness({ baselineProfileMissing: "baseline-a" });
		const first = await cleanupFinalBrowserState(harness.input, harness.hooks);
		const operationCount = harness.operations.length;
		const second = await cleanupFinalBrowserState(harness.input, harness.hooks);

		expect(first.passed).toBe(false);
		expect(second).toBe(first);
		expect(harness.operations).toHaveLength(operationCount);
	});
});

function harnessIdentity(pid: number, startToken: string): ProcessIdentity {
	return { pid, startToken, userDataDir };
}
