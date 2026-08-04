import { afterEach, describe, expect, test } from "bun:test";
import { mkdtemp, readdir, readFile, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";

import {
	type CdpTransport,
	captureTask12Dom,
	captureTask12ReadOnlyState,
	captureTask12Screenshot,
	isPng,
	type SignalHooks,
	TASK12_RUNTIME_ARTIFACTS,
	type Task12BrowserState,
	type Task12CapturedState,
	Task12CleanupRegistry,
	type Task12DomCapture,
	Task12EvidenceSession,
	type Task12FileSystem,
	type Task12ProfileObservablesSnapshotReader,
	type Task12StateExpressions,
	verifyOwnedCleanup,
} from "./task12_runtime_evidence";
import type {
	CleanupInventory,
	Task12ExpectedChanges,
	Task12Snapshot,
} from "./two_profile_qa_state";

const roots: string[] = [];
const PNG = Uint8Array.from([137, 80, 78, 71, 13, 10, 26, 10, 0, 0, 0, 0]);
const observed = <T>(value: T) => ({ status: "observed" as const, value });

function profileObservablesReader(
	override: unknown = {
		snapshot: {
			activeBrowserProfileId: "a",
			activeMahoProfileId: "a",
			registryRevision: "12",
			lifecycleEntries: [
				{ profileId: "a", lifecycleState: 1 },
				{ profileId: "b", lifecycleState: 2 },
			],
			pendingDeletionProfileIds: ["b"],
			deletedHistoryAvailability: 0,
		},
	},
): Task12ProfileObservablesSnapshotReader & { calls: number } {
	return {
		calls: 0,
		async getProfileObservablesSnapshot() {
			this.calls++;
			return override as Awaited<
				ReturnType<
					Task12ProfileObservablesSnapshotReader["getProfileObservablesSnapshot"]
				>
			>;
		},
	};
}

type ProfileObservablesSnapshotFixture = {
	activeBrowserProfileId: string | null;
	activeMahoProfileId: string | null;
	registryRevision: string;
	lifecycleEntries: Array<{ profileId: string; lifecycleState: number }>;
	pendingDeletionProfileIds: string[];
	deletedHistoryAvailability: number;
};

function profileObservablesResponse(
	overrides: Partial<ProfileObservablesSnapshotFixture> = {},
): { snapshot: ProfileObservablesSnapshotFixture } {
	return {
		snapshot: {
			activeBrowserProfileId: "a",
			activeMahoProfileId: "a",
			registryRevision: "12",
			lifecycleEntries: [
				{ profileId: "a", lifecycleState: 1 },
				{ profileId: "b", lifecycleState: 1 },
			],
			pendingDeletionProfileIds: [],
			deletedHistoryAvailability: 0,
			...overrides,
		},
	};
}

async function captureProfileObservablesFixture(
	response: unknown,
	configuredProfiles: Readonly<{ A: boolean; B: boolean }> = {
		A: true,
		B: true,
	},
): Promise<{
	captured: Task12CapturedState;
	reader: Task12ProfileObservablesSnapshotReader & { calls: number };
}> {
	const state = snapshot();
	if (
		state.profiles.A.status !== "observed" ||
		state.profiles.B.status !== "observed" ||
		state.globalState.status !== "observed"
	) {
		throw new Error("invalid profile observables fixture");
	}
	const values = new Map<string, unknown>([
		["profileA()", state.profiles.A.value],
		["profileB()", state.profiles.B.value],
		["globalState()", state.globalState.value],
		["document.hasFocus()", true],
	]);
	const cdp = new FakeCdp((method, params) => {
		if (method === "Runtime.evaluate") {
			const expression = String(params.expression);
			if (
				(expression === "profileA()" && !configuredProfiles.A) ||
				(expression === "profileB()" && !configuredProfiles.B)
			) {
				return { result: { result: { type: "undefined" } } };
			}
			return { result: { result: { value: values.get(expression) } } };
		}
		if (method === "Target.getTargetInfo")
			return { error: { message: "method unavailable" } };
		throw new Error(`unexpected ${method}`);
	});
	const reader = profileObservablesReader(response);
	const captured = await captureTask12ReadOnlyState(
		cdp,
		{
			profileA: "profileA()",
			profileB: "profileB()",
			globalState: "globalState()",
		},
		"2026-08-01T00:00:00.000Z",
		{
			url: "chrome://maho-settings/",
			title: "Settings",
			selectedPane: observed("Profiles"),
			selectedTarget: observed({ label: "Work" }),
		},
		reader,
	);
	return { captured, reader };
}

async function temporaryRoot(): Promise<string> {
	const root = await mkdtemp(join(tmpdir(), "task12-runtime-"));
	roots.push(root);
	return root;
}

afterEach(async () => {
	await Promise.all(
		roots.splice(0).map((root) => rm(root, { recursive: true, force: true })),
	);
});

function snapshot(): Task12Snapshot {
	return {
		timestamp: "2026-08-01T00:00:00.000Z",
		activeBrowserProfileId: observed("browser-a"),
		activeMahoProfileId: observed("maho-a"),
		focusedWindowId: observed("window-a"),
		selectedSpace: observed({ id: "space", name: "Personal" }),
		spaceAssignments: observed({ tabIds: ["tab-a"], windowIds: ["window-a"] }),
		profiles: {
			A: observed({
				id: "a",
				name: "Personal",
				avatarColor: "#111111",
				homepageUrl: "https://a.example",
				searchEngineId: "a",
				searchSuggestionsEnabled: true,
				downloadPrompt: false,
				downloadDirectoryToken: "a",
				archiveTimeoutHours: 24,
			}),
			B: observed({
				id: "b",
				name: "Work",
				avatarColor: "#222222",
				homepageUrl: "https://b.example",
				searchEngineId: "b",
				searchSuggestionsEnabled: false,
				downloadPrompt: true,
				downloadDirectoryToken: "b",
				archiveTimeoutHours: 72,
			}),
		},
		globalState: observed({
			account: { plan: "free" },
			process: { gpu: true },
			coreGlobal: { telemetry: false },
		}),
		lifecycle: observed({
			profiles: { A: "ready", B: "ready" },
			deletedProfileIds: observed([]),
			pendingDeletionProfileIds: [],
		}),
	};
}

function browser(
	overrides: Partial<Task12BrowserState> = {},
): Task12BrowserState {
	return {
		url: observed("chrome://maho-settings/?pane=profiles"),
		title: observed("Profiles"),
		focusedTarget: observed(true),
		target: observed({
			targetId: "target-settings-1",
			type: "page",
			url: "chrome://maho-settings/?pane=profiles",
			title: "Profiles",
		}),
		window: observed({
			windowId: "window-a",
			bounds: { left: 0, top: 0, width: 1280, height: 800 },
		}),
		selectedPane: observed("Profiles"),
		selectedTarget: observed({ label: "Work" }),
		...overrides,
	};
}

function capturedState(
	snapshotValue = snapshot(),
	browserValue = browser(),
): Task12CapturedState {
	return { snapshot: snapshotValue, browser: browserValue };
}

async function session(root: string): Promise<Task12EvidenceSession> {
	return Task12EvidenceSession.create({
		rootDirectory: root,
		scenarioId: "happy-b-only-mutation",
		runId: "run-001",
		uuid: (() => {
			let id = 0;
			return () => `write-${++id}`;
		})(),
	});
}

async function writeCompleteArtifactSet(
	value: Task12EvidenceSession,
): Promise<void> {
	const before = snapshot();
	const after = snapshot();
	if (after.profiles.B.status === "observed")
		after.profiles.B.value.name = "Work QA";
	const expected: Task12ExpectedChanges = {
		scenario: "happy-b-only-mutation",
		profileB: { name: { before: "Work", after: "Work QA" } },
		allowedCleanup: [],
	};
	await value.writeStateArtifacts(
		capturedState(before),
		capturedState(after),
		expected,
	);
	await value.appendEvent({ sequence: 1, kind: "navigation" });
	await value.writeJson("cleanup-receipt.json", {
		completedAt: "2026-08-01T00:01:00.000Z",
		removed: {
			profileIds: [],
			profileNames: [],
			tabIds: [],
			tempFiles: [],
			processes: [],
		},
		remaining: {
			profileIds: [],
			profileNames: [],
			tabIds: [],
			tempFiles: [],
			processes: [],
		},
		errors: [],
	});
	await value.writeText("dom-before.txt", "before");
	await value.writeText("dom-after.txt", "after");
	await value.writePng("screenshot-before.png", PNG);
	await value.writePng("screenshot-after.png", PNG);
	await value.finalizeManifest(true);
}

class FakeCdp implements CdpTransport {
	readonly calls: Array<{
		method: string;
		params: Readonly<Record<string, unknown>>;
	}> = [];
	constructor(
		private readonly reply: (
			method: string,
			params: Readonly<Record<string, unknown>>,
		) => unknown | Promise<unknown>,
	) {}
	async send(
		method: string,
		params: Readonly<Record<string, unknown>> = {},
	): Promise<unknown> {
		this.calls.push({ method, params });
		return this.reply(method, params);
	}
}

describe("Task12EvidenceSession", () => {
	test("creates a unique deterministic run directory and complete driver artifact set", async () => {
		const root = await temporaryRoot();
		const value = await session(root);
		expect(value.runDirectory).toBe(
			join(root, "happy-b-only-mutation--run-001"),
		);
		expect(value.artifacts).toEqual(TASK12_RUNTIME_ARTIFACTS);
		await writeCompleteArtifactSet(value);
		expect(await value.verifyRequiredArtifacts()).toEqual({
			passed: true,
			missing: [],
			invalid: [],
		});
		expect((await readdir(value.runDirectory)).sort()).toEqual(
			[...TASK12_RUNTIME_ARTIFACTS].sort(),
		);
		await expect(session(root)).rejects.toThrow();
	});

	test("roundtrips distinct before and after browser context without adding browser-only state diffs", async () => {
		const value = await session(await temporaryRoot());
		const before = capturedState();
		const afterSnapshot = snapshot();
		const afterBrowser = browser({
			focusedTarget: observed(false),
			target: observed({
				targetId: "target-settings-2",
				type: "page",
				url: "chrome://maho-settings/?pane=profiles",
				title: "Profiles",
			}),
		});

		await value.writeStateArtifacts(
			before,
			capturedState(afterSnapshot, afterBrowser),
			{
				scenario: "happy-b-only-mutation",
				profileB: {},
				allowedCleanup: [],
			},
		);

		const persistedBefore = JSON.parse(
			await readFile(value.pathFor("before.json"), "utf8"),
		) as Task12CapturedState;
		const persistedAfter = JSON.parse(
			await readFile(value.pathFor("after.json"), "utf8"),
		) as Task12CapturedState;
		const diff = JSON.parse(
			await readFile(value.pathFor("state-diff.json"), "utf8"),
		) as { forbiddenChanges: unknown[] };
		expect(persistedBefore).toEqual(before);
		expect(persistedAfter.browser.target).toEqual(afterBrowser.target);
		expect(persistedAfter.browser.focusedTarget).toEqual(observed(false));
		expect(persistedBefore.browser.target).not.toEqual(
			persistedAfter.browser.target,
		);
		expect(diff.forbiddenChanges).toEqual([]);
	});

	test.each([
		["missing browser context", (state: Task12CapturedState) => state.snapshot],
		[
			"malformed browser context",
			(state: Task12CapturedState) => ({
				...state,
				browser: {
					...state.browser,
					focusedTarget: { status: "observed", value: "yes" },
				},
			}),
		],
		[
			"mismatched browser context",
			(state: Task12CapturedState) => ({
				...state,
				browser: {
					...state.browser,
					window: observed({ windowId: "window-other", bounds: {} }),
				},
			}),
		],
	])("rejects %s during required artifact verification", async (_label, mutate) => {
		const value = await session(await temporaryRoot());
		await writeCompleteArtifactSet(value);
		const beforePath = value.pathFor("before.json");
		const persisted = JSON.parse(
			await readFile(beforePath, "utf8"),
		) as Task12CapturedState;
		await writeFile(
			beforePath,
			`${JSON.stringify(mutate(persisted), null, 2)}\n`,
		);

		expect(await value.verifyRequiredArtifacts()).toEqual({
			passed: false,
			missing: [],
			invalid: ["before.json", "manifest.json"],
		});
	});

	test("reports every missing required artifact without inventing evidence", async () => {
		const value = await session(await temporaryRoot());
		await value.writeText("dom-before.txt", "only artifact");
		await value.finalizeManifest(true);
		const verification = await value.verifyRequiredArtifacts();
		expect(verification.passed).toBe(false);
		expect(verification.missing).toEqual(
			TASK12_RUNTIME_ARTIFACTS.filter(
				(key) => key !== "dom-before.txt" && key !== "manifest.json",
			),
		);
		expect(verification.invalid).toEqual(["manifest.json"]);
		const manifest = JSON.parse(
			await readFile(value.pathFor("manifest.json"), "utf8"),
		);
		const entry = manifest.entries.find(
			(candidate: { scenarioId: string }) =>
				candidate.scenarioId === "happy-b-only-mutation",
		);
		expect(entry.status).toBe("missing");
		expect(
			entry.artifacts.find(
				(artifact: { key: string }) => artifact.key === "before.json",
			).status,
		).toBe("missing");
	});

	test("writes exact PNG bytes and rejects non-PNG data", async () => {
		const value = await session(await temporaryRoot());
		const cdp = new FakeCdp((method) =>
			method === "Page.captureScreenshot"
				? { result: { data: Buffer.from(PNG).toString("base64") } }
				: {},
		);
		expect(await captureTask12Screenshot(cdp, value, "before")).toEqual(PNG);
		expect(
			new Uint8Array(await readFile(value.pathFor("screenshot-before.png"))),
		).toEqual(PNG);
		expect(isPng(PNG)).toBe(true);
		await expect(
			value.writePng("screenshot-after.png", Uint8Array.from([1, 2, 3])),
		).rejects.toThrow(/not a PNG/);
	});

	test("serializes concurrent JSONL events in call order", async () => {
		const value = await session(await temporaryRoot());
		await Promise.all([
			value.appendEvent({ sequence: 1, kind: "navigation" }),
			value.appendEvent({ sequence: 2, kind: "profile" }),
			value.appendEvent({ sequence: 3, kind: "navigation" }),
		]);
		const lines = (await readFile(value.pathFor("events.jsonl"), "utf8"))
			.trim()
			.split("\n")
			.map((line) => JSON.parse(line));
		expect(lines.map((line) => line.sequence)).toEqual([1, 2, 3]);
	});

	test("preserves an existing artifact and removes the temporary file when atomic rename fails", async () => {
		const root = await temporaryRoot();
		const base = await session(root);
		await writeFile(base.pathFor("dom-before.txt"), "original\n");
		const real = await import("node:fs/promises");
		const failing: Task12FileSystem = {
			mkdir: real.mkdir,
			writeFile: async (path, data, options) => {
				await real.writeFile(path, data, options);
			},
			rename: async () => {
				throw new Error("injected rename failure");
			},
			rm: real.rm,
			readFile: real.readFile,
			stat: real.stat,
		};
		const injected = await Task12EvidenceSession.create({
			rootDirectory: root,
			scenarioId: "cleanup",
			runId: "atomic-failure",
			fileSystem: failing,
			uuid: () => "temp",
		});
		await writeFile(injected.pathFor("dom-before.txt"), "original\n");
		await expect(
			injected.writeText("dom-before.txt", "replacement"),
		).rejects.toThrow("injected rename failure");
		expect(await readFile(injected.pathFor("dom-before.txt"), "utf8")).toBe(
			"original\n",
		);
		expect(await readdir(injected.runDirectory)).toEqual(["dom-before.txt"]);
	});

	test("rejects non-JSON values at the artifact boundary", async () => {
		const value = await session(await temporaryRoot());
		await expect(
			value.writeJson("manifest.json", { bad: Number.NaN }),
		).rejects.toThrow(/strict JSON/);
		await expect(
			value.appendEvent({ bad: undefined } as never),
		).rejects.toThrow(/strict JSON/);
	});
});

describe("CDP capture", () => {
	test("writes sanitized DOM display metadata without raw internal IDs", async () => {
		const value = await session(await temporaryRoot());
		const cdp = new FakeCdp(() => ({
			result: {
				result: {
					value: {
						url: "chrome://maho-settings/?pane=profiles",
						title: "Profiles",
						text: "Work profile",
						html: '<html><body><button aria-label="Work profile">Work profile</button></body></html>',
						selectedPane: "Profiles",
						selectedTargetLabel: "Work profile",
					},
				},
			},
		}));
		const capture = await captureTask12Dom(cdp, value, "before");
		expect(capture.selectedTarget).toEqual(observed({ label: "Work profile" }));
		const text = await readFile(value.pathFor("dom-before.txt"), "utf8");
		expect(text).toContain("Selected target: Work profile");
		expect(text).not.toContain("profile-internal-47");
		const expression = String(cdp.calls[0]?.params.expression);
		expect(expression).toContain("removeAttribute");
		expect(expression).not.toContain("filesystem");
	});

	test("maps one atomic current-browser Space snapshot to all three Task12 observations", async () => {
		const expressions: Task12StateExpressions = {
			profileA: "profileA()",
			profileB: "profileB()",
			globalState: "globalState()",
		};
		const fixture = snapshot();
		if (
			fixture.profiles.A.status !== "observed" ||
			fixture.profiles.B.status !== "observed" ||
			fixture.globalState.status !== "observed" ||
			fixture.lifecycle.status !== "observed"
		)
			throw new Error("invalid fixture");
		const values = new Map<string, unknown>([
			["profileA()", fixture.profiles.A.value],
			["profileB()", fixture.profiles.B.value],
			["globalState()", fixture.globalState.value],
			["document.hasFocus()", true],
		]);
		const observables = profileObservablesReader();
		const cdp = new FakeCdp((method, params) => {
			if (method === "Runtime.evaluate") {
				const expression = String(params.expression);
				if (expression.includes("getCurrentBrowserSpaceSnapshot"))
					return {
						result: {
							result: {
								value: {
									snapshot: {
										focusedBrowserSessionId: 42,
										selectedSpace: { id: "space-work", name: "Work" },
										assignedTabIds: ["tab-a", "tab-b"],
										assignedWindowIds: ["42"],
									},
								},
							},
						},
					};
				return { result: { result: { value: values.get(expression) } } };
			}
			if (method === "Target.getTargetInfo")
				return { error: { message: "method unavailable" } };
			throw new Error(`unexpected ${method}`);
		});
		const dom: Pick<
			Task12DomCapture,
			"url" | "title" | "selectedPane" | "selectedTarget"
		> = {
			url: "chrome://maho-settings/",
			title: "Settings",
			selectedPane: observed("Profiles"),
			selectedTarget: observed({ label: "Work" }),
		};

		const captured = await captureTask12ReadOnlyState(
			cdp,
			expressions,
			"2026-08-01T00:00:00.000Z",
			dom,
			observables,
		);

		expect(observables.calls).toBe(1);
		expect(captured.snapshot.activeBrowserProfileId).toEqual(observed("a"));
		expect(captured.snapshot.activeMahoProfileId).toEqual(observed("a"));
		expect(captured.snapshot.lifecycle).toEqual(
			observed({
				profiles: { A: "ready", B: "deleting" },
				deletedProfileIds: {
					status: "unavailable",
					reason:
						"Deleted profile history is not retained by the product registry",
				},
				pendingDeletionProfileIds: ["b"],
			}),
		);
		expect(captured.snapshot.focusedWindowId).toEqual(observed("42"));
		expect(captured.snapshot.selectedSpace).toEqual(
			observed({ id: "space-work", name: "Work" }),
		);
		expect(captured.snapshot.spaceAssignments).toEqual(
			observed({ tabIds: ["tab-a", "tab-b"], windowIds: ["42"] }),
		);
		const snapshotCalls = cdp.calls.filter(
			(call) =>
				call.method === "Runtime.evaluate" &&
				String(call.params.expression).includes(
					"getCurrentBrowserSpaceSnapshot",
				),
		);
		expect(snapshotCalls).toHaveLength(1);
		expect(snapshotCalls[0]?.params.awaitPromise).toBe(true);
		expect(
			cdp.calls.map((call) => String(call.params.expression)),
		).not.toContain("legacySpace()");
		expect(
			cdp.calls.map((call) => String(call.params.expression)),
		).not.toContain("legacyAssignments()");
	});

	test.each([
		[0, "provisioning"],
		[3, "repair_required"],
	] as const)("projects lifecycle state %i as %s with deleted history unavailable", async (lifecycleState, expectedLifecycle) => {
		const { captured, reader } = await captureProfileObservablesFixture(
			profileObservablesResponse({
				lifecycleEntries: [
					{ profileId: "a", lifecycleState },
					{ profileId: "b", lifecycleState: 1 },
				],
			}),
		);

		expect(reader.calls).toBe(1);
		expect(captured.snapshot.lifecycle).toEqual(
			observed({
				profiles: { A: expectedLifecycle, B: "ready" },
				deletedProfileIds: {
					status: "unavailable",
					reason:
						"Deleted profile history is not retained by the product registry",
				},
				pendingDeletionProfileIds: [],
			}),
		);
	});

	test.each([
		["activeBrowserProfileId", "activeMahoProfileId"],
		["activeMahoProfileId", "activeBrowserProfileId"],
	] as const)("keeps snapshot projections usable when %s is null", async (nullableField, observedField) => {
		const { captured, reader } = await captureProfileObservablesFixture(
			profileObservablesResponse({ [nullableField]: null }),
		);

		expect(reader.calls).toBe(1);
		expect(captured.snapshot[nullableField].status).toBe("unavailable");
		expect(captured.snapshot[observedField]).toEqual(observed("a"));
		expect(captured.snapshot.lifecycle).toEqual(
			observed({
				profiles: { A: "ready", B: "ready" },
				deletedProfileIds: {
					status: "unavailable",
					reason:
						"Deleted profile history is not retained by the product registry",
				},
				pendingDeletionProfileIds: [],
			}),
		);
		expect(captured.snapshot.profiles.A.status).toBe("observed");
		expect(captured.snapshot.profiles.B.status).toBe("observed");
		expect(captured.snapshot.globalState.status).toBe("observed");
	});

	test.each([
		["A", [{ profileId: "b", lifecycleState: 1 }]],
		["B", [{ profileId: "a", lifecycleState: 1 }]],
	] as const)("marks lifecycle unavailable rather than deleted when configured profile %s is absent", async (missingProfile, lifecycleEntries) => {
		const { captured, reader } = await captureProfileObservablesFixture(
			profileObservablesResponse({ lifecycleEntries: [...lifecycleEntries] }),
		);

		expect(reader.calls).toBe(1);
		expect(captured.snapshot.activeBrowserProfileId).toEqual(observed("a"));
		expect(captured.snapshot.activeMahoProfileId).toEqual(observed("a"));
		expect(captured.snapshot.lifecycle.status).toBe("unavailable");
		if (captured.snapshot.lifecycle.status !== "unavailable")
			throw new Error("expected unavailable lifecycle");
		expect(captured.snapshot.lifecycle.reason).toContain(
			`Profile ${missingProfile} lifecycle is unavailable`,
		);
		expect(captured.snapshot.lifecycle.reason).not.toContain("deleted");
	});

	test("attach-only capture preserves active observations while configured lifecycle is unavailable", async () => {
		const { captured, reader } = await captureProfileObservablesFixture(
			profileObservablesResponse(),
			{ A: false, B: false },
		);

		expect(reader.calls).toBe(1);
		expect(captured.snapshot.activeBrowserProfileId).toEqual(observed("a"));
		expect(captured.snapshot.activeMahoProfileId).toEqual(observed("a"));
		expect(captured.snapshot.lifecycle).toEqual({
			status: "unavailable",
			reason:
				"Profile lifecycle is unavailable until both configured profile IDs are known",
		});
		expect(captured.snapshot.profiles.A.status).toBe("unavailable");
		expect(captured.snapshot.profiles.B.status).toBe("unavailable");
		expect(captured.snapshot.globalState.status).toBe("observed");
	});

	test.each([
		["null snapshot", { result: { result: { value: { snapshot: null } } } }],
		[
			"rejected snapshot call",
			{
				result: {
					exceptionDetails: { text: "Mojo rejected" },
					result: { type: "object" },
				},
			},
		],
		[
			"malformed snapshot DTO",
			{
				result: {
					result: {
						value: {
							snapshot: {
								focusedBrowserSessionId: "42",
								selectedSpace: { id: "space-work", name: 7 },
								assignedTabIds: ["tab-a"],
								assignedWindowIds: ["42"],
							},
						},
					},
				},
			},
		],
	])("marks atomic context unavailable for %s without falling back to legacy globals", async (_label, atomicReply) => {
		const expressions: Task12StateExpressions = {
			profileA: "profileA()",
			profileB: "profileB()",
			globalState: "globalState()",
		};
		const observables = profileObservablesReader();
		const cdp = new FakeCdp((method, params) => {
			if (method === "Runtime.evaluate") {
				const expression = String(params.expression);
				if (expression.includes("getCurrentBrowserSpaceSnapshot"))
					return atomicReply;
				return {
					result: {
						result: { value: expression === "document.hasFocus()" ? true : {} },
					},
				};
			}
			if (method === "Target.getTargetInfo")
				return { error: { message: "method unavailable" } };
			throw new Error(`unexpected ${method}`);
		});
		const dom: Pick<
			Task12DomCapture,
			"url" | "title" | "selectedPane" | "selectedTarget"
		> = {
			url: "chrome://maho-settings/",
			title: "Settings",
			selectedPane: observed("Profiles"),
			selectedTarget: observed({ label: "Work" }),
		};

		const captured = await captureTask12ReadOnlyState(
			cdp,
			expressions,
			"2026-08-01T00:00:00.000Z",
			dom,
			observables,
		);

		expect(captured.snapshot.focusedWindowId.status).toBe("unavailable");
		expect(captured.snapshot.selectedSpace.status).toBe("unavailable");
		expect(captured.snapshot.spaceAssignments.status).toBe("unavailable");
		expect(
			cdp.calls.map((call) => String(call.params.expression)),
		).not.toContain("legacySpace()");
		expect(
			cdp.calls.map((call) => String(call.params.expression)),
		).not.toContain("legacyAssignments()");
	});

	test.each([
		[
			"non-canonical revision",
			{
				snapshot: {
					activeBrowserProfileId: "a",
					activeMahoProfileId: "a",
					registryRevision: "01",
					lifecycleEntries: [
						{ profileId: "a", lifecycleState: 1 },
						{ profileId: "b", lifecycleState: 1 },
					],
					pendingDeletionProfileIds: [],
					deletedHistoryAvailability: 0,
				},
			},
		],
		[
			"duplicate lifecycle profile ID",
			{
				snapshot: {
					activeBrowserProfileId: "a",
					activeMahoProfileId: "a",
					registryRevision: "1",
					lifecycleEntries: [
						{ profileId: "a", lifecycleState: 1 },
						{ profileId: "a", lifecycleState: 2 },
					],
					pendingDeletionProfileIds: [],
					deletedHistoryAvailability: 0,
				},
			},
		],
		[
			"duplicate pending deletion ID",
			{
				snapshot: {
					activeBrowserProfileId: "a",
					activeMahoProfileId: "a",
					registryRevision: "1",
					lifecycleEntries: [
						{ profileId: "a", lifecycleState: 1 },
						{ profileId: "b", lifecycleState: 2 },
					],
					pendingDeletionProfileIds: ["b", "b"],
					deletedHistoryAvailability: 0,
				},
			},
		],
		[
			"unknown lifecycle state",
			{
				snapshot: {
					activeBrowserProfileId: "a",
					activeMahoProfileId: "a",
					registryRevision: "1",
					lifecycleEntries: [
						{ profileId: "a", lifecycleState: 4 },
						{ profileId: "b", lifecycleState: 1 },
					],
					pendingDeletionProfileIds: [],
					deletedHistoryAvailability: 0,
				},
			},
		],
	])("fails closed for malformed profile observables: %s", async (_label, response) => {
		const state = snapshot();
		if (
			state.profiles.A.status !== "observed" ||
			state.profiles.B.status !== "observed" ||
			state.globalState.status !== "observed"
		)
			throw new Error("invalid fixture");
		const values = new Map<string, unknown>([
			["profileA()", state.profiles.A.value],
			["profileB()", state.profiles.B.value],
			["globalState()", state.globalState.value],
			["document.hasFocus()", true],
		]);
		const cdp = new FakeCdp((method, params) => {
			if (method === "Runtime.evaluate")
				return {
					result: { result: { value: values.get(String(params.expression)) } },
				};
			if (method === "Target.getTargetInfo")
				return { error: { message: "method unavailable" } };
			throw new Error(`unexpected ${method}`);
		});
		const reader = profileObservablesReader(response);
		const captured = await captureTask12ReadOnlyState(
			cdp,
			{
				profileA: "profileA()",
				profileB: "profileB()",
				globalState: "globalState()",
			},
			"2026-08-01T00:00:00.000Z",
			{
				url: "chrome://maho-settings/",
				title: "Settings",
				selectedPane: observed("Profiles"),
				selectedTarget: observed({ label: "Work" }),
			},
			reader,
		);

		expect(reader.calls).toBe(1);
		expect(captured.snapshot.activeBrowserProfileId.status).toBe("unavailable");
		expect(captured.snapshot.activeMahoProfileId.status).toBe("unavailable");
		expect(captured.snapshot.lifecycle.status).toBe("unavailable");
	});

	test("captures observable browser/store state and marks unavailable values explicitly", async () => {
		const expressions: Task12StateExpressions = {
			profileA: "profileA()",
			profileB: "profileB()",
			globalState: "globalState()",
		};
		const state = snapshot();
		if (
			state.profiles.A.status !== "observed" ||
			state.profiles.B.status !== "observed" ||
			state.globalState.status !== "observed" ||
			state.lifecycle.status !== "observed"
		) {
			throw new Error("snapshot fixture must expose all required observations");
		}
		const values = new Map<string, unknown>([
			["profileA()", state.profiles.A.value],
			["profileB()", state.profiles.B.value],
			["globalState()", state.globalState.value],
			["document.hasFocus()", true],
		]);
		const observables = profileObservablesReader();
		const cdp = new FakeCdp((method, params) => {
			if (method === "Runtime.evaluate") {
				const expression = String(params.expression);
				if (expression === "globalThis.__mahoFocusedWindowId")
					return { result: { result: { type: "undefined" } } };
				return { result: { result: { value: values.get(expression) } } };
			}
			if (method === "Target.getTargetInfo")
				return { error: { message: "method unavailable" } };
			throw new Error(`unexpected ${method}`);
		});
		const dom: Pick<
			Task12DomCapture,
			"url" | "title" | "selectedPane" | "selectedTarget"
		> = {
			url: "chrome://maho-settings/?pane=profiles",
			title: "Profiles",
			selectedPane: observed("Profiles"),
			selectedTarget: { status: "unavailable", reason: "No display label" },
		};
		const captured = await captureTask12ReadOnlyState(
			cdp,
			expressions,
			"2026-08-01T00:00:00.000Z",
			dom,
			observables,
		);
		expect(captured.snapshot.activeBrowserProfileId).toEqual(observed("a"));
		expect(captured.snapshot.focusedWindowId).toEqual({
			status: "unavailable",
			reason:
				"Current-browser Space snapshot is unavailable: null or malformed response",
		});
		expect(captured.snapshot.selectedSpace.status).toBe("unavailable");
		expect(captured.snapshot.spaceAssignments.status).toBe("unavailable");
		expect(captured.browser.target).toEqual({
			status: "unavailable",
			reason: "Target.getTargetInfo failed: method unavailable",
		});
		expect(captured.browser.window.status).toBe("unavailable");
		expect(captured.browser.selectedTarget).toEqual({
			status: "unavailable",
			reason: "No display label",
		});
	});
});

describe("cleanup ownership and lifecycle hooks", () => {
	const empty: CleanupInventory = {
		profileIds: [],
		profileNames: [],
		tabIds: [],
		tempFiles: [],
		processes: [],
	};

	test("cleans only registered owned resources and preserves pre-existing resources", async () => {
		const calls: string[] = [];
		const registry = new Task12CleanupRegistry();
		registry.registerOwned({
			kind: "profileId",
			identity: "owned-profile",
			cleanup: async () => {
				calls.push("owned-profile");
			},
		});
		registry.registerOwned({
			kind: "process",
			identity: { pid: 12, startToken: "start" },
			cleanup: async () => {
				calls.push("owned-process");
			},
		});
		const baseline: CleanupInventory = {
			...empty,
			profileIds: ["pre-existing-profile"],
			processes: [{ pid: 10, startToken: "old" }],
		};
		const receipt = await registry.cleanup("2026-08-01T00:05:00.000Z");
		expect(calls).toEqual(["owned-process", "owned-profile"]);
		expect(
			verifyOwnedCleanup(baseline, registry, receipt).verification,
		).toEqual({ passed: true, missing: [], violations: [] });
		expect(receipt.removed.profileIds).not.toContain("pre-existing-profile");
	});

	test("records cleanup failures and remains idempotent", async () => {
		let calls = 0;
		const registry = new Task12CleanupRegistry();
		registry.registerOwned({
			kind: "tabId",
			identity: "tab-owned",
			cleanup: async () => {
				calls++;
				throw new Error("close failed");
			},
		});
		const first = registry.cleanup("2026-08-01T00:05:00.000Z");
		const second = registry.cleanup("2026-08-01T00:06:00.000Z");
		expect(first).toBe(second);
		const receipt = await first;
		expect(calls).toBe(1);
		expect(receipt.remaining.tabIds).toEqual(["tab-owned"]);
		expect(receipt.errors).toEqual(["tabId:tab-owned: close failed"]);
		expect(
			verifyOwnedCleanup(empty, registry, receipt).verification.passed,
		).toBe(false);
	});

	test("finally and SIGINT share one idempotent cleanup receipt without sleeps", async () => {
		let listener: (() => void) | undefined;
		let cleanups = 0;
		const receipts: string[] = [];
		const hooks: SignalHooks = {
			onSigint: (callback) => {
				listener = callback;
				return () => {
					listener = undefined;
				};
			},
		};
		const registry = new Task12CleanupRegistry();
		registry.registerOwned({
			kind: "tempFile",
			identity: "/tmp/task12-owned",
			cleanup: async () => {
				cleanups++;
			},
		});
		registry.installSigintCleanup(
			hooks,
			() => "2026-08-01T00:05:00.000Z",
			(receipt) => {
				receipts.push(receipt.completedAt);
			},
		);
		listener?.();
		await registry.withCleanupFinally(
			async () => "done",
			() => "2026-08-01T00:06:00.000Z",
			(receipt) => {
				receipts.push(receipt.completedAt);
			},
		);
		expect(cleanups).toBe(1);
		expect(receipts).toEqual([
			"2026-08-01T00:05:00.000Z",
			"2026-08-01T00:05:00.000Z",
		]);
	});
});
