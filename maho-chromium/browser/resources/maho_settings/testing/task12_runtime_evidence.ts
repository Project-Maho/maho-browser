import { randomUUID } from "node:crypto";
import {
	mkdir,
	open,
	readFile,
	rename,
	rm,
	stat,
	writeFile,
} from "node:fs/promises";
import { basename, join, resolve, sep } from "node:path";

import {
	buildCleanupPlan,
	buildTask12Manifest,
	type CleanupInventory,
	type CleanupPlan,
	type CleanupProcessIdentity,
	type CleanupReceipt,
	type CleanupVerification,
	diffTask12State,
	type JsonValue,
	type ManifestArtifactResults,
	type Observation,
	TASK12_ARTIFACT_KEYS,
	type Task12ArtifactKey,
	type Task12ExpectedChanges,
	type Task12Lifecycle,
	type Task12Manifest,
	type Task12ScenarioId,
	type Task12Snapshot,
	type Task12StateDiff,
	validateTask12Snapshot,
	verifyCleanupReceipt,
} from "./two_profile_qa_state.js";

export const TASK12_RUNTIME_ARTIFACTS: readonly Task12ArtifactKey[] =
	TASK12_ARTIFACT_KEYS;

export interface Task12FileSystem {
	mkdir(path: string, options?: { recursive?: boolean }): Promise<unknown>;
	writeFile(
		path: string,
		data: string | Uint8Array,
		options?: { flag?: string },
	): Promise<void>;
	rename(from: string, to: string): Promise<void>;
	rm(
		path: string,
		options?: { force?: boolean; recursive?: boolean },
	): Promise<void>;
	readFile(path: string): Promise<Uint8Array>;
	stat(path: string): Promise<{ isFile(): boolean }>;
}

const nodeFileSystem: Task12FileSystem = {
	mkdir,
	writeFile: async (path, data, options) => {
		await writeFile(path, data, options);
	},
	rename,
	rm,
	readFile,
	stat,
};

function requireSafeSegment(value: string, label: string): void {
	if (
		!/^[A-Za-z0-9][A-Za-z0-9._-]*$/.test(value) ||
		value === "." ||
		value === ".."
	) {
		throw new TypeError(`${label} must be a safe path segment`);
	}
}

function artifactPath(
	runDirectory: string,
	artifact: Task12ArtifactKey,
): string {
	if (!TASK12_RUNTIME_ARTIFACTS.includes(artifact))
		throw new TypeError(`Unknown Task 12 artifact ${artifact}`);
	const path = resolve(runDirectory, artifact);
	if (
		!path.startsWith(`${resolve(runDirectory)}${sep}`) ||
		basename(path) !== artifact
	) {
		throw new TypeError(`Unsafe Task 12 artifact path ${artifact}`);
	}
	return path;
}

function assertJsonValue(
	value: unknown,
	label: string,
	ancestors = new Set<object>(),
): asserts value is JsonValue {
	if (value === null || typeof value === "string" || typeof value === "boolean")
		return;
	if (typeof value === "number" && Number.isFinite(value)) return;
	if (typeof value !== "object")
		throw new TypeError(`${label} is not strict JSON`);
	if (ancestors.has(value)) throw new TypeError(`${label} contains a cycle`);
	ancestors.add(value);
	try {
		if (Array.isArray(value)) {
			for (let index = 0; index < value.length; index++) {
				if (!Object.hasOwn(value, index))
					throw new TypeError(`${label} contains a sparse array`);
				assertJsonValue(value[index], `${label}[${index}]`, ancestors);
			}
			return;
		}
		const prototype = Object.getPrototypeOf(value);
		if (prototype !== Object.prototype && prototype !== null)
			throw new TypeError(`${label} contains a non-JSON object`);
		for (const key of Reflect.ownKeys(value)) {
			if (typeof key !== "string")
				throw new TypeError(`${label} contains a symbol key`);
			const descriptor = Object.getOwnPropertyDescriptor(value, key);
			if (!descriptor?.enumerable || !("value" in descriptor))
				throw new TypeError(`${label}.${key} is not a JSON data property`);
			assertJsonValue(descriptor.value, `${label}.${key}`, ancestors);
		}
	} finally {
		ancestors.delete(value);
	}
}

export function stringifyStrictJson(value: unknown): string {
	assertJsonValue(value, "value");
	return `${JSON.stringify(value, null, 2)}\n`;
}

export interface Task12EvidenceSessionOptions {
	rootDirectory: string;
	scenarioId: Task12ScenarioId;
	runId?: string;
	fileSystem?: Task12FileSystem;
	uuid?: () => string;
}

export class Task12EvidenceSession {
	readonly scenarioId: Task12ScenarioId;
	readonly runId: string;
	readonly runDirectory: string;
	readonly artifacts = TASK12_RUNTIME_ARTIFACTS;
	private readonly fileSystem: Task12FileSystem;
	private readonly uuid: () => string;
	private jsonlTail: Promise<void> = Promise.resolve();

	private constructor(options: Task12EvidenceSessionOptions, runId: string) {
		requireSafeSegment(options.scenarioId, "scenarioId");
		requireSafeSegment(runId, "runId");
		this.scenarioId = options.scenarioId;
		this.runId = runId;
		this.runDirectory = join(
			resolve(options.rootDirectory),
			`${options.scenarioId}--${runId}`,
		);
		this.fileSystem = options.fileSystem ?? nodeFileSystem;
		this.uuid = options.uuid ?? randomUUID;
	}

	static async create(
		options: Task12EvidenceSessionOptions,
	): Promise<Task12EvidenceSession> {
		const uuid = options.uuid ?? randomUUID;
		const runId = options.runId ?? uuid();
		const session = new Task12EvidenceSession({ ...options, uuid }, runId);
		await session.fileSystem.mkdir(resolve(options.rootDirectory), {
			recursive: true,
		});
		await session.fileSystem.mkdir(session.runDirectory);
		return session;
	}

	pathFor(artifact: Task12ArtifactKey): string {
		return artifactPath(this.runDirectory, artifact);
	}

	private async atomicWrite(
		artifact: Task12ArtifactKey,
		data: string | Uint8Array,
	): Promise<void> {
		const destination = this.pathFor(artifact);
		const temporary = join(
			this.runDirectory,
			`.${artifact}.${this.uuid()}.tmp`,
		);
		try {
			await this.fileSystem.writeFile(temporary, data, { flag: "wx" });
			await this.fileSystem.rename(temporary, destination);
		} catch (error) {
			await this.fileSystem
				.rm(temporary, { force: true })
				.catch(() => undefined);
			throw error;
		}
	}

	async writeJson(
		artifact: Extract<Task12ArtifactKey, `${string}.json`>,
		value: unknown,
	): Promise<void> {
		await this.atomicWrite(artifact, stringifyStrictJson(value));
	}

	writeText(
		artifact: "dom-before.txt" | "dom-after.txt",
		value: string,
	): Promise<void> {
		return this.atomicWrite(
			artifact,
			value.endsWith("\n") ? value : `${value}\n`,
		);
	}

	async writePng(
		artifact: "screenshot-before.png" | "screenshot-after.png",
		bytes: Uint8Array,
	): Promise<void> {
		if (!isPng(bytes)) throw new TypeError(`${artifact} is not a PNG`);
		await this.atomicWrite(artifact, bytes);
	}

	appendEvent(event: JsonValue): Promise<void> {
		const append = async () => {
			assertJsonValue(event, "event");
			const handle = await open(this.pathFor("events.jsonl"), "a");
			try {
				await handle.writeFile(`${JSON.stringify(event)}\n`);
				await handle.sync();
			} finally {
				await handle.close();
			}
		};
		const result = this.jsonlTail.then(append);
		this.jsonlTail = result.catch(() => undefined);
		return result;
	}

	async writeStateArtifacts(
		before: Task12CapturedState,
		after: Task12CapturedState,
		expected: Task12ExpectedChanges,
	): Promise<{ diff: Task12StateDiff }> {
		validateTask12CapturedState(before, "before");
		validateTask12CapturedState(after, "after");
		const diff = diffTask12State(before.snapshot, after.snapshot, expected);
		await this.writeJson("before.json", before);
		await this.writeJson("after.json", after);
		await this.writeJson("state-diff.json", diff);
		return { diff };
	}

	private async existingArtifacts(): Promise<Set<Task12ArtifactKey>> {
		await this.jsonlTail;
		const existing = new Set<Task12ArtifactKey>();
		for (const artifact of TASK12_RUNTIME_ARTIFACTS) {
			try {
				if ((await this.fileSystem.stat(this.pathFor(artifact))).isFile())
					existing.add(artifact);
			} catch {
				// Missing evidence remains absent from the derived manifest.
			}
		}
		return existing;
	}

	async finalizeManifest(scenarioPassed: boolean): Promise<Task12Manifest> {
		const existing = await this.existingArtifacts();
		const results: ManifestArtifactResults = {};
		const template = buildTask12Manifest({});
		for (const entry of template.entries.filter(
			(candidate) => candidate.scenarioId === this.scenarioId,
		)) {
			const scenarioResults = results[this.scenarioId] ?? {};
			results[this.scenarioId] = scenarioResults;
			scenarioResults[entry.criterionId] = Object.fromEntries(
				entry.artifacts.map((artifact) => [
					artifact.key,
					{
						present:
							artifact.key === "manifest.json" || existing.has(artifact.key),
						passed: scenarioPassed,
					},
				]),
			);
		}
		const manifest = buildTask12Manifest(results);
		await this.writeJson("manifest.json", manifest);
		return manifest;
	}

	async verifyRequiredArtifacts(): Promise<{
		passed: boolean;
		missing: Task12ArtifactKey[];
		invalid: string[];
	}> {
		const existing = await this.existingArtifacts();
		const missing = TASK12_RUNTIME_ARTIFACTS.filter(
			(artifact) => !existing.has(artifact),
		);
		const invalid: string[] = [];
		if (!missing.includes("manifest.json")) {
			try {
				const decoder = new TextDecoder();
				const manifest = JSON.parse(
					decoder.decode(
						await this.fileSystem.readFile(this.pathFor("manifest.json")),
					),
				) as Task12Manifest;
				const entries = manifest.entries.filter(
					(entry) => entry.scenarioId === this.scenarioId,
				);
				if (
					entries.length === 0 ||
					entries.some((entry) => entry.status !== "pass")
				)
					invalid.push("manifest.json");
				for (const artifact of ["before.json", "after.json"] as const) {
					if (missing.includes(artifact)) continue;
					try {
						const state = JSON.parse(
							decoder.decode(
								await this.fileSystem.readFile(this.pathFor(artifact)),
							),
						);
						validateTask12CapturedState(state, artifact.replace(".json", ""));
					} catch {
						invalid.push(artifact);
						if (!invalid.includes("manifest.json"))
							invalid.push("manifest.json");
					}
				}
			} catch {
				invalid.push("manifest.json");
			}
		}
		return {
			passed: missing.length === 0 && invalid.length === 0,
			missing,
			invalid,
		};
	}
}

export interface CdpTransport {
	send(
		method: string,
		params?: Readonly<Record<string, unknown>>,
	): Promise<unknown>;
}

interface CdpResultEnvelope {
	result?: unknown;
	error?: { message?: unknown };
}

function cdpResult(response: unknown, method: string): unknown {
	if (typeof response !== "object" || response === null)
		throw new Error(`${method} returned no response`);
	const envelope = response as CdpResultEnvelope;
	if (envelope.error)
		throw new Error(
			`${method} failed: ${String(envelope.error.message ?? "unknown CDP error")}`,
		);
	return envelope.result;
}

function record(value: unknown): Record<string, unknown> | undefined {
	return typeof value === "object" && value !== null && !Array.isArray(value)
		? (value as Record<string, unknown>)
		: undefined;
}

export async function captureTask12Screenshot(
	cdp: CdpTransport,
	session: Task12EvidenceSession,
	phase: "before" | "after",
): Promise<Uint8Array> {
	const result = record(
		cdpResult(
			await cdp.send("Page.captureScreenshot", {
				format: "png",
				fromSurface: true,
			}),
			"Page.captureScreenshot",
		),
	);
	if (typeof result?.data !== "string")
		throw new Error("Page.captureScreenshot returned no PNG data");
	const decoded = atob(result.data);
	const bytes = Uint8Array.from(decoded, (character) =>
		character.charCodeAt(0),
	);
	await session.writePng(`screenshot-${phase}.png`, bytes);
	return bytes;
}

export function isPng(bytes: Uint8Array): boolean {
	const signature = [137, 80, 78, 71, 13, 10, 26, 10];
	return (
		bytes.length >= signature.length &&
		signature.every((byte, index) => bytes[index] === byte)
	);
}

export interface Task12DomCapture {
	url: string;
	title: string;
	text: string;
	html: string;
	selectedPane: Observation<string>;
	selectedTarget: Observation<{ label: string }>;
}

const DOM_CAPTURE_EXPRESSION = `(() => {
  const clone = document.documentElement.cloneNode(true);
  clone.querySelectorAll('script,style').forEach(node => node.remove());
  clone.querySelectorAll('*').forEach(node => {
    for (const attribute of Array.from(node.attributes)) {
      if (/^(id|for|aria-controls|aria-owns|data-(profile|target|internal|id)|.*-id)$/i.test(attribute.name)) node.removeAttribute(attribute.name);
    }
  });
  const clean = value => String(value || '').replace(/[\\u0000-\\u001f\\u007f]+/g, ' ').replace(/\\s+/g, ' ').trim();
  const pane = document.querySelector('nav[aria-label="Settings panes"] [aria-current="page"]');
  const target = document.querySelector('[aria-label*="profile" i][aria-current="true"], [data-selected-profile="true"]');
  return {
    url: String(location.href), title: clean(document.title), text: clean(document.body?.innerText),
    html: clone.outerHTML, selectedPane: clean(pane?.textContent || pane?.getAttribute('data-pane')),
    selectedTargetLabel: clean(target?.getAttribute('aria-label') || target?.textContent)
  };
})()`;

function observedString(
	value: unknown,
	unavailableReason: string,
): Observation<string> {
	return typeof value === "string" && value.length > 0
		? { status: "observed", value }
		: { status: "unavailable", reason: unavailableReason };
}

export async function captureTask12Dom(
	cdp: CdpTransport,
	session: Task12EvidenceSession,
	phase: "before" | "after",
): Promise<Task12DomCapture> {
	const result = record(
		cdpResult(
			await cdp.send("Runtime.evaluate", {
				expression: DOM_CAPTURE_EXPRESSION,
				returnByValue: true,
			}),
			"Runtime.evaluate",
		),
	);
	const remote = record(result?.result);
	const value = record(remote?.value);
	if (
		!value ||
		typeof value.url !== "string" ||
		typeof value.title !== "string" ||
		typeof value.text !== "string" ||
		typeof value.html !== "string"
	) {
		throw new Error("DOM capture expression returned an invalid value");
	}
	const capture: Task12DomCapture = {
		url: value.url,
		title: value.title,
		text: value.text,
		html: value.html,
		selectedPane: observedString(
			value.selectedPane,
			"No selected Settings pane is exposed in the DOM",
		),
		selectedTarget:
			typeof value.selectedTargetLabel === "string" &&
			value.selectedTargetLabel.length > 0
				? { status: "observed", value: { label: value.selectedTargetLabel } }
				: {
						status: "unavailable",
						reason: "No selected target display label is exposed in the DOM",
					},
	};
	await session.writeText(
		`dom-${phase}.txt`,
		[
			`URL: ${capture.url}`,
			`Title: ${capture.title}`,
			`Selected pane: ${capture.selectedPane.status === "observed" ? capture.selectedPane.value : `[unavailable: ${capture.selectedPane.reason}]`}`,
			`Selected target: ${capture.selectedTarget.status === "observed" ? capture.selectedTarget.value.label : `[unavailable: ${capture.selectedTarget.reason}]`}`,
			"",
			capture.text,
			"",
			capture.html,
		].join("\n"),
	);
	return capture;
}

export interface Task12StateExpressions {
	profileA: string;
	profileB: string;
	globalState: string;
}

export interface Task12BrowserState {
	url: Observation<string>;
	title: Observation<string>;
	focusedTarget: Observation<boolean>;
	target: Observation<{
		targetId: string;
		type: string;
		url: string;
		title: string;
	}>;
	window: Observation<{ windowId: string; bounds: JsonValue }>;
	selectedPane: Observation<string>;
	selectedTarget: Observation<{ label: string }>;
}

export interface Task12CapturedState {
	snapshot: Task12Snapshot;
	browser: Task12BrowserState;
}

function exactKeys(
	value: Record<string, unknown>,
	keys: readonly string[],
	label: string,
): void {
	const actual = Object.keys(value);
	if (
		actual.length !== keys.length ||
		keys.some((key) => !Object.hasOwn(value, key))
	) {
		throw new TypeError(`${label} must contain exactly ${keys.join(", ")}`);
	}
}

function validateBrowserObservation(
	value: unknown,
	label: string,
	validateValue: (candidate: unknown, valueLabel: string) => void,
): void {
	const observation = record(value);
	if (!observation) throw new TypeError(`${label} must be an observation`);
	if (observation.status === "observed") {
		exactKeys(observation, ["status", "value"], label);
		validateValue(observation.value, `${label}.value`);
		return;
	}
	if (
		observation.status === "missing" ||
		observation.status === "unavailable"
	) {
		exactKeys(observation, ["status", "reason"], label);
		if (
			typeof observation.reason !== "string" ||
			observation.reason.length === 0
		) {
			throw new TypeError(`${label}.reason must be a non-empty string`);
		}
		return;
	}
	throw new TypeError(`${label}.status is invalid`);
}

function validateTask12BrowserState(
	value: unknown,
	label: string,
	snapshot: Task12Snapshot,
): asserts value is Task12BrowserState {
	const browser = record(value);
	if (!browser) throw new TypeError(`${label} must be an object`);
	exactKeys(
		browser,
		[
			"url",
			"title",
			"focusedTarget",
			"target",
			"window",
			"selectedPane",
			"selectedTarget",
		],
		label,
	);
	const nonEmptyString = (candidate: unknown, valueLabel: string) => {
		if (typeof candidate !== "string" || candidate.length === 0)
			throw new TypeError(`${valueLabel} must be a non-empty string`);
	};
	validateBrowserObservation(browser.url, `${label}.url`, nonEmptyString);
	validateBrowserObservation(browser.title, `${label}.title`, nonEmptyString);
	validateBrowserObservation(
		browser.focusedTarget,
		`${label}.focusedTarget`,
		(candidate, valueLabel) => {
			if (typeof candidate !== "boolean")
				throw new TypeError(`${valueLabel} must be a boolean`);
		},
	);
	validateBrowserObservation(
		browser.target,
		`${label}.target`,
		(candidate, valueLabel) => {
			const target = record(candidate);
			if (!target) throw new TypeError(`${valueLabel} must be an object`);
			exactKeys(target, ["targetId", "type", "url", "title"], valueLabel);
			for (const key of ["targetId", "type", "url", "title"] as const)
				nonEmptyString(target[key], `${valueLabel}.${key}`);
		},
	);
	validateBrowserObservation(
		browser.window,
		`${label}.window`,
		(candidate, valueLabel) => {
			const window = record(candidate);
			if (!window) throw new TypeError(`${valueLabel} must be an object`);
			exactKeys(window, ["windowId", "bounds"], valueLabel);
			nonEmptyString(window.windowId, `${valueLabel}.windowId`);
			assertJsonValue(window.bounds, `${valueLabel}.bounds`);
			if (
				snapshot.focusedWindowId.status === "observed" &&
				window.windowId !== snapshot.focusedWindowId.value
			) {
				throw new TypeError(
					`${valueLabel}.windowId must match snapshot.focusedWindowId`,
				);
			}
		},
	);
	validateBrowserObservation(
		browser.selectedPane,
		`${label}.selectedPane`,
		nonEmptyString,
	);
	validateBrowserObservation(
		browser.selectedTarget,
		`${label}.selectedTarget`,
		(candidate, valueLabel) => {
			const target = record(candidate);
			if (!target) throw new TypeError(`${valueLabel} must be an object`);
			exactKeys(target, ["label"], valueLabel);
			nonEmptyString(target.label, `${valueLabel}.label`);
		},
	);
}

export function validateTask12CapturedState(
	value: unknown,
	label: string,
): asserts value is Task12CapturedState {
	const state = record(value);
	if (!state) throw new TypeError(`${label} state must be an object`);
	exactKeys(state, ["snapshot", "browser"], `${label} state`);
	validateTask12Snapshot(state.snapshot, label);
	validateTask12BrowserState(state.browser, `${label}.browser`, state.snapshot);
}

export interface Task12CurrentBrowserSpaceSnapshotDto {
	readonly focusedBrowserSessionId: number;
	readonly selectedSpace: { readonly id: string; readonly name: string };
	readonly assignedTabIds: readonly string[];
	readonly assignedWindowIds: readonly string[];
}

export interface Task12ProfileObservablesSnapshotDto {
	readonly activeBrowserProfileId: string | null;
	readonly activeMahoProfileId: string | null;
	readonly registryRevision: string;
	readonly lifecycleEntries: ReadonlyArray<{
		readonly profileId: string;
		readonly lifecycleState: number;
	}>;
	readonly pendingDeletionProfileIds: readonly string[];
	readonly deletedHistoryAvailability: number;
}

interface Task12AtomicSpaceObservations {
	readonly focusedWindowId: Observation<string>;
	readonly selectedSpace: Task12Snapshot["selectedSpace"];
	readonly spaceAssignments: Task12Snapshot["spaceAssignments"];
}

interface Task12ProfileObservablesProjection {
	readonly activeBrowserProfileId: Task12Snapshot["activeBrowserProfileId"];
	readonly activeMahoProfileId: Task12Snapshot["activeMahoProfileId"];
	readonly lifecycle: Task12Snapshot["lifecycle"];
}

export interface Task12CurrentBrowserSpaceSnapshotReader {
	getCurrentBrowserSpaceSnapshot(): Promise<{
		snapshot: Task12CurrentBrowserSpaceSnapshotDto | null;
	}>;
}

export interface Task12ProfileObservablesSnapshotReader {
	getProfileObservablesSnapshot(): Promise<{
		snapshot: Task12ProfileObservablesSnapshotDto | null;
	}>;
}

const CURRENT_BROWSER_SPACE_SNAPSHOT_EXPRESSION = `(async function(){
  const store = window.settingsStore;
  if (!store || typeof store.getHandler !== 'function') throw new Error('Settings store handler is unavailable');
  const handler = store.getHandler();
  if (!handler || typeof handler.getCurrentBrowserSpaceSnapshot !== 'function') {
    throw new Error('getCurrentBrowserSpaceSnapshot is unavailable');
  }
  return await handler.getCurrentBrowserSpaceSnapshot();
})()`;

function unavailableAtomicSpaceObservations(
	reason: string,
): Task12AtomicSpaceObservations {
	return {
		focusedWindowId: { status: "unavailable", reason },
		selectedSpace: { status: "unavailable", reason },
		spaceAssignments: { status: "unavailable", reason },
	};
}

function stringArray(value: unknown): value is string[] {
	return (
		Array.isArray(value) && value.every((item) => typeof item === "string")
	);
}

function unavailableObservation<T>(reason: string): Observation<T> {
	return { status: "unavailable", reason };
}

function parseNullableObservedString(
	value: string | null,
	reason: string,
): Observation<string> {
	return typeof value === "string" && value.length > 0
		? { status: "observed", value }
		: unavailableObservation(reason);
}

function isCanonicalDecimalString(value: unknown): value is string {
	return typeof value === "string" && /^(0|[1-9]\d*)$/.test(value);
}

function uniqueStringArray(
	value: readonly string[],
	label: string,
): string[] | undefined {
	const seen = new Set<string>();
	const entries: string[] = [];
	for (const entry of value) {
		if (typeof entry !== "string" || entry.length === 0 || seen.has(entry)) {
			return undefined;
		}
		seen.add(entry);
		entries.push(entry);
	}
	return entries;
}

function mapLifecycleState(value: number): Task12Lifecycle {
	switch (value) {
		case 0:
			return "provisioning";
		case 1:
			return "ready";
		case 2:
			return "deleting";
		case 3:
			return "repair_required";
		default:
			throw new TypeError(`Unknown lifecycle state ${value}`);
	}
}

function captureProfileValueLifecycle(
	profileId: string,
	entries: ReadonlyMap<string, Task12Lifecycle>,
		label: string,
): Task12Lifecycle {
	const lifecycle = entries.get(profileId);
	if (lifecycle === undefined) throw new TypeError(`${label} lifecycle is unavailable`);
	return lifecycle;
}

function projectProfileObservablesSnapshot(
	value: unknown,
	targetProfileAId: string | undefined,
	targetProfileBId: string | undefined,
): Task12ProfileObservablesProjection {
	const unavailable = (reason: string): Task12ProfileObservablesProjection => ({
		activeBrowserProfileId: unavailableObservation(reason),
		activeMahoProfileId: unavailableObservation(reason),
		lifecycle: unavailableObservation(reason),
	});
	const response = record(value);
	if (!response || !Object.hasOwn(response, "snapshot")) {
		return unavailable("Profile observables snapshot is unavailable: malformed response");
	}
	if (response.snapshot === null) {
		return unavailable("Profile observables snapshot is unavailable: product returned null");
	}
	const snapshot = record(response.snapshot);
	if (!snapshot) {
		return unavailable("Profile observables snapshot is unavailable: malformed snapshot");
	}
	if (
		(snapshot.activeBrowserProfileId !== null &&
			(typeof snapshot.activeBrowserProfileId !== "string" || snapshot.activeBrowserProfileId.length === 0)) ||
		(snapshot.activeMahoProfileId !== null &&
			(typeof snapshot.activeMahoProfileId !== "string" || snapshot.activeMahoProfileId.length === 0)) ||
		!isCanonicalDecimalString(snapshot.registryRevision) ||
		!Array.isArray(snapshot.lifecycleEntries) ||
		!Array.isArray(snapshot.pendingDeletionProfileIds) ||
		snapshot.deletedHistoryAvailability !== 0
	) {
		return unavailable("Profile observables snapshot is unavailable: malformed snapshot fields");
	}
	const pendingDeletionProfileIds = uniqueStringArray(
		snapshot.pendingDeletionProfileIds,
		"pendingDeletionProfileIds",
	);
	if (!pendingDeletionProfileIds) {
		return unavailable("Profile observables snapshot is unavailable: malformed pending deletion IDs");
	}
	const lifecycleEntries = new Map<string, Task12Lifecycle>();
		for (const [index, entryValue] of snapshot.lifecycleEntries.entries()) {
			const entry = record(entryValue);
			if (
				!entry ||
				typeof entry.profileId !== "string" ||
				entry.profileId.length === 0 ||
				typeof entry.lifecycleState !== "number" ||
				!Number.isInteger(entry.lifecycleState) ||
				entry.lifecycleState < 0 ||
				entry.lifecycleState > 3 ||
				lifecycleEntries.has(entry.profileId)
			) {
				return unavailable(
					`Profile observables snapshot is unavailable: malformed lifecycle entry ${index}`,
				);
			}
			try {
				lifecycleEntries.set(entry.profileId, mapLifecycleState(entry.lifecycleState));
			} catch (error) {
				return unavailable(
					`Profile observables snapshot is unavailable: ${error instanceof Error ? error.message : String(error)}`,
				);
			}
		}
	const activeBrowserProfileId = parseNullableObservedString(
		snapshot.activeBrowserProfileId as string | null,
		"Active browser profile ID is unavailable",
	);
	const activeMahoProfileId = parseNullableObservedString(
		snapshot.activeMahoProfileId as string | null,
		"Active Maho profile ID is unavailable",
	);
	if (
		targetProfileAId === undefined ||
		targetProfileBId === undefined
	) {
		return {
			activeBrowserProfileId,
			activeMahoProfileId,
			lifecycle: unavailableObservation(
				"Profile lifecycle is unavailable until both configured profile IDs are known",
			),
		};
	}
	try {
		return {
			activeBrowserProfileId,
			activeMahoProfileId,
			lifecycle: {
				status: "observed",
				value: {
					profiles: {
						A: captureProfileValueLifecycle(targetProfileAId, lifecycleEntries, "Profile A"),
						B: captureProfileValueLifecycle(targetProfileBId, lifecycleEntries, "Profile B"),
					},
					deletedProfileIds: {
						status: "unavailable",
						reason:
							"Deleted profile history is not retained by the product registry",
					},
					pendingDeletionProfileIds,
				},
			},
		};
	} catch (error) {
		return {
			activeBrowserProfileId,
			activeMahoProfileId,
			lifecycle: unavailableObservation(
				`Profile lifecycle is unavailable: ${error instanceof Error ? error.message : String(error)}`,
			),
		};
	}
}

async function captureProfileObservablesSnapshot(
	reader: Task12ProfileObservablesSnapshotReader,
): Promise<unknown> {
	try {
		return await reader.getProfileObservablesSnapshot();
	} catch (error) {
		return {
			error: `Profile observables snapshot read failed: ${error instanceof Error ? error.message : String(error)}`,
		};
	}
}

function parseCurrentBrowserSpaceSnapshot(
	value: unknown,
): Task12CurrentBrowserSpaceSnapshotDto | null {
	const response = record(value);
	if (!response || !Object.hasOwn(response, "snapshot")) return null;
	const snapshot = record(response.snapshot);
	if (!snapshot) return null;
	const selectedSpace = record(snapshot.selectedSpace);
	if (
		!Number.isFinite(snapshot.focusedBrowserSessionId) ||
		!selectedSpace ||
		typeof selectedSpace.id !== "string" ||
		typeof selectedSpace.name !== "string" ||
		!stringArray(snapshot.assignedTabIds) ||
		!stringArray(snapshot.assignedWindowIds)
	) {
		return null;
	}
	return {
		focusedBrowserSessionId: snapshot.focusedBrowserSessionId as number,
		selectedSpace: { id: selectedSpace.id, name: selectedSpace.name },
		assignedTabIds: [...snapshot.assignedTabIds],
		assignedWindowIds: [...snapshot.assignedWindowIds],
	};
}

async function captureCurrentBrowserSpaceSnapshot(
	cdp: CdpTransport,
	reader?: Task12CurrentBrowserSpaceSnapshotReader,
): Promise<Task12AtomicSpaceObservations> {
	const unavailable = "Current-browser Space snapshot is unavailable";
	try {
		const value =
			reader === undefined
				? undefined
				: await reader.getCurrentBrowserSpaceSnapshot();
		const evaluated =
			reader === undefined
				? record(
						cdpResult(
							await cdp.send("Runtime.evaluate", {
								expression: CURRENT_BROWSER_SPACE_SNAPSHOT_EXPRESSION,
								awaitPromise: true,
								returnByValue: true,
							}),
							"Runtime.evaluate",
						),
					)
				: undefined;
		if (evaluated?.exceptionDetails !== undefined)
			return unavailableAtomicSpaceObservations(
				`${unavailable}: expression threw`,
			);
		const remote = record(evaluated?.result);
		if (reader === undefined && !Object.hasOwn(remote ?? {}, "value")) {
			return unavailableAtomicSpaceObservations(
				`${unavailable}: not exposed by value`,
			);
		}
		const snapshot = parseCurrentBrowserSpaceSnapshot(
			reader === undefined ? remote?.value : value,
		);
		if (!snapshot)
			return unavailableAtomicSpaceObservations(
				`${unavailable}: null or malformed response`,
			);
		return {
			focusedWindowId: {
				status: "observed",
				value: String(snapshot.focusedBrowserSessionId),
			},
			selectedSpace: { status: "observed", value: snapshot.selectedSpace },
			spaceAssignments: {
				status: "observed",
				value: {
					tabIds: [...snapshot.assignedTabIds],
					windowIds: [...snapshot.assignedWindowIds],
				},
			},
		};
	} catch (error) {
		return unavailableAtomicSpaceObservations(
			`${unavailable}: ${error instanceof Error ? error.message : String(error)}`,
		);
	}
}

async function evaluateObservation<T>(
	cdp: CdpTransport,
	expression: string,
	label: string,
): Promise<Observation<T>> {
	try {
		const result = record(
			cdpResult(
				await cdp.send("Runtime.evaluate", {
					expression,
					awaitPromise: true,
					returnByValue: true,
				}),
				"Runtime.evaluate",
			),
		);
		const remote = record(result?.result);
		if (remote?.exceptionDetails !== undefined)
			return { status: "unavailable", reason: `${label} expression threw` };
		if (!Object.hasOwn(remote ?? {}, "value"))
			return {
				status: "unavailable",
				reason: `${label} is not exposed by value`,
			};
		return { status: "observed", value: remote?.value as T };
	} catch (error) {
		return {
			status: "unavailable",
			reason: `${label}: ${error instanceof Error ? error.message : String(error)}`,
		};
	}
}

export async function captureTask12ReadOnlyState(
	cdp: CdpTransport,
	expressions: Task12StateExpressions,
	timestamp: string,
	dom: Pick<
		Task12DomCapture,
		"url" | "title" | "selectedPane" | "selectedTarget"
	>,
	profileObservablesSnapshotReader: Task12ProfileObservablesSnapshotReader,
	currentBrowserSpaceSnapshotReader?: Task12CurrentBrowserSpaceSnapshotReader,
): Promise<Task12CapturedState> {
	const [
		atomicSpace,
		profileA,
		profileB,
		globalState,
		profileObservablesSnapshot,
		focusedTarget,
	] = await Promise.all([
		captureCurrentBrowserSpaceSnapshot(cdp, currentBrowserSpaceSnapshotReader),
		evaluateObservation<
			Task12Snapshot["profiles"]["A"] extends Observation<infer T> ? T : never
		>(cdp, expressions.profileA, "profile A"),
		evaluateObservation<
			Task12Snapshot["profiles"]["B"] extends Observation<infer T> ? T : never
		>(cdp, expressions.profileB, "profile B"),
		evaluateObservation<
			Task12Snapshot["globalState"] extends Observation<infer T> ? T : never
		>(cdp, expressions.globalState, "global state"),
		captureProfileObservablesSnapshot(profileObservablesSnapshotReader),
		evaluateObservation<boolean>(cdp, "document.hasFocus()", "focused target"),
	]);
	const lifecycleProfileAId =
		profileA.status === "observed" ? profileA.value.id : undefined;
	const lifecycleProfileBId =
		profileB.status === "observed" ? profileB.value.id : undefined;
	const profileObservables = projectProfileObservablesSnapshot(
		profileObservablesSnapshot,
		lifecycleProfileAId,
		lifecycleProfileBId,
	);

	let target: Task12BrowserState["target"] = {
		status: "unavailable",
		reason: "Target.getTargetInfo is unavailable",
	};
	let window: Task12BrowserState["window"] = {
		status: "unavailable",
		reason: "Browser.getWindowForTarget is unavailable",
	};
	try {
		const targetResult = record(
			cdpResult(await cdp.send("Target.getTargetInfo"), "Target.getTargetInfo"),
		);
		const info = record(targetResult?.targetInfo);
		if (
			typeof info?.targetId === "string" &&
			typeof info.type === "string" &&
			typeof info.url === "string" &&
			typeof info.title === "string"
		) {
			target = {
				status: "observed",
				value: {
					targetId: info.targetId,
					type: info.type,
					url: info.url,
					title: info.title,
				},
			};
			try {
				const windowResult = record(
					cdpResult(
						await cdp.send("Browser.getWindowForTarget", {
							targetId: info.targetId,
						}),
						"Browser.getWindowForTarget",
					),
				);
				if (
					typeof windowResult?.windowId === "number" &&
					record(windowResult.bounds)
				) {
					window = {
						status: "observed",
						value: {
							windowId: String(windowResult.windowId),
							bounds: windowResult.bounds as JsonValue,
						},
					};
				}
			} catch (error) {
				window = {
					status: "unavailable",
					reason: error instanceof Error ? error.message : String(error),
				};
			}
		}
	} catch (error) {
		target = {
			status: "unavailable",
			reason: error instanceof Error ? error.message : String(error),
		};
	}

	return {
		snapshot: {
			timestamp,
			activeBrowserProfileId: profileObservables.activeBrowserProfileId,
			activeMahoProfileId: profileObservables.activeMahoProfileId,
			focusedWindowId: atomicSpace.focusedWindowId,
			selectedSpace: atomicSpace.selectedSpace,
			spaceAssignments: atomicSpace.spaceAssignments,
			profiles: { A: profileA, B: profileB },
			globalState,
			lifecycle: profileObservables.lifecycle,
		},
		browser: {
			url: observedString(dom.url, "Document URL is unavailable"),
			title: observedString(dom.title, "Document title is unavailable"),
			focusedTarget,
			target,
			window,
			selectedPane: dom.selectedPane,
			selectedTarget: dom.selectedTarget,
		},
	};
}

export type OwnedResource =
	| {
			kind: "profileId" | "profileName" | "tabId" | "tempFile";
			identity: string;
			cleanup: () => Promise<void>;
	  }
	| {
			kind: "process";
			identity: CleanupProcessIdentity;
			cleanup: () => Promise<void>;
	  };

function emptyInventory(): CleanupInventory {
	return {
		profileIds: [],
		profileNames: [],
		tabIds: [],
		tempFiles: [],
		processes: [],
	};
}

function addResource(
	inventory: CleanupInventory,
	resource: OwnedResource,
): void {
	switch (resource.kind) {
		case "profileId":
			inventory.profileIds.push(resource.identity);
			break;
		case "profileName":
			inventory.profileNames.push(resource.identity);
			break;
		case "tabId":
			inventory.tabIds.push(resource.identity);
			break;
		case "tempFile":
			inventory.tempFiles.push(resource.identity);
			break;
		case "process":
			inventory.processes.push({
				pid: resource.identity.pid,
				startToken: resource.identity.startToken,
			});
			break;
	}
}

export interface SignalHooks {
	onSigint(listener: () => void): () => void;
}

export class Task12CleanupRegistry {
	private readonly owned: OwnedResource[] = [];
	private cleanupPromise: Promise<CleanupReceipt> | undefined;

	registerOwned(resource: OwnedResource): void {
		this.owned.push(resource);
	}

	inventory(): CleanupInventory {
		const inventory = emptyInventory();
		for (const resource of this.owned) addResource(inventory, resource);
		return inventory;
	}

	cleanup(completedAt: string): Promise<CleanupReceipt> {
		if (this.cleanupPromise) return this.cleanupPromise;
		this.cleanupPromise = this.performCleanup(completedAt);
		return this.cleanupPromise;
	}

	private async performCleanup(completedAt: string): Promise<CleanupReceipt> {
		const removed = emptyInventory();
		const remaining = emptyInventory();
		const errors: string[] = [];
		for (const resource of [...this.owned].reverse()) {
			try {
				await resource.cleanup();
				addResource(removed, resource);
			} catch (error) {
				addResource(remaining, resource);
				errors.push(
					`${resource.kind}:${resource.kind === "process" ? `${resource.identity.pid}@${resource.identity.startToken}` : resource.identity}: ${error instanceof Error ? error.message : String(error)}`,
				);
			}
		}
		return { completedAt, removed, remaining, errors };
	}

	installSigintCleanup(
		hooks: SignalHooks,
		completedAt: () => string,
		onReceipt: (receipt: CleanupReceipt) => void | Promise<void>,
	): () => void {
		return hooks.onSigint(() => {
			void this.cleanup(completedAt()).then(onReceipt);
		});
	}

	async withCleanupFinally<T>(
		action: () => Promise<T>,
		completedAt: () => string,
		onReceipt: (receipt: CleanupReceipt) => void | Promise<void>,
	): Promise<T> {
		try {
			return await action();
		} finally {
			await onReceipt(await this.cleanup(completedAt()));
		}
	}
}

export function verifyOwnedCleanup(
	preExisting: CleanupInventory,
	registry: Task12CleanupRegistry,
	receipt: CleanupReceipt,
): { plan: CleanupPlan; verification: CleanupVerification } {
	const plan = buildCleanupPlan(preExisting, registry.inventory());
	return { plan, verification: verifyCleanupReceipt(plan, receipt) };
}
