import { afterEach, describe, expect, test } from "bun:test";

import {
	createKernProcArgs2Reader,
	createMacKernProcArgsReader,
	createStableProcessArgvReader,
	KernProcArgs2Error,
	parseKernProcArgs2,
	type KernProcArgs2Sysctl,
} from "./task12_macos_argv.js";
import { createMacProcessInventory } from "./task12_process_ownership.js";

const encoder = new TextEncoder();
const children: Bun.Subprocess[] = [];

function procArgs(
	argv: readonly string[],
	options: {
		executable?: string;
		padding?: number;
		environment?: readonly string[];
	} = {},
): Uint8Array {
	const executable = options.executable ?? "/usr/bin/example";
	const padding = options.padding ?? 2;
	const strings = [
		executable,
		...Array<string>(padding).fill(""),
		...argv,
		...(options.environment ?? []),
	];
	const encoded = encoder.encode(`${strings.join("\0")}\0`);
	const bytes = new Uint8Array(4 + encoded.length);
	new DataView(bytes.buffer).setInt32(0, argv.length, true);
	bytes.set(encoded, 4);
	return bytes;
}

function resizingSysctl(
	responses: readonly Uint8Array[],
	options: { queryCode?: number; readCodes?: readonly number[] } = {},
): { sysctl: KernProcArgs2Sysctl; calls: string[] } {
	const calls: string[] = [];
	let queryIndex = 0;
	let readIndex = 0;
	return {
		calls,
		sysctl: {
			querySize(pid) {
				calls.push(`size:${pid}`);
				const bytes = responses[Math.min(queryIndex, responses.length - 1)];
				queryIndex += 1;
				return { code: options.queryCode ?? 0, size: bytes.length };
			},
			read(pid, destination) {
				calls.push(`read:${pid}:${destination.length}`);
				const bytes = responses[Math.min(readIndex, responses.length - 1)];
				const code = options.readCodes?.[readIndex] ?? 0;
				readIndex += 1;
				if (code === 0) destination.set(bytes.subarray(0, destination.length));
				return { code, size: bytes.length };
			},
		},
	};
}

afterEach(async () => {
	for (const child of children.splice(0)) {
		child.kill();
		await child.exited;
	}
});

describe("parseKernProcArgs2", () => {
	test("preserves exact argv boundaries and excludes environment entries", () => {
		const argv = [
			"/usr/bin/example",
			"value with spaces",
			'literal "double" quotes',
			"literal 'single' quotes",
			String.raw`backslash\\value\tail`,
			"",
		];
		expect(
			parseKernProcArgs2(
				procArgs(argv, { padding: 3, environment: ["SECRET=excluded"] }),
			),
		).toEqual(argv);
	});

	test("rejects malformed argc and truncated strings", () => {
		const negativeArgc = procArgs(["ok"]);
		new DataView(negativeArgc.buffer).setInt32(0, -1, true);
		expect(() => parseKernProcArgs2(negativeArgc)).toThrow(KernProcArgs2Error);
		expect(() => parseKernProcArgs2(new Uint8Array([1, 0, 0]))).toThrow(
			/argc/i,
		);
		expect(() =>
			parseKernProcArgs2(procArgs(["one", "two"]).subarray(0, -1)),
		).toThrow(/unterminated|truncated/i);
	});
});

describe("createKernProcArgs2Reader", () => {
	test("queries size and reads one bounded allocation", async () => {
		const fixture = procArgs(["example", "--flag"]);
		const { sysctl, calls } = resizingSysctl([fixture]);
		expect(await createKernProcArgs2Reader(sysctl)(42)).toEqual([
			"example",
			"--flag",
		]);
		expect(calls).toEqual(["size:42", `read:42:${fixture.length}`]);
	});

	test("performs exactly one growth retry", async () => {
		const small = procArgs(["small"]);
		const grown = procArgs(["grown", "value with spaces"]);
		const { sysctl, calls } = resizingSysctl([small, grown], {
			readCodes: [12, 0],
		});
		expect(await createKernProcArgs2Reader(sysctl)(42)).toEqual([
			"grown",
			"value with spaces",
		]);
		expect(calls).toEqual([
			"size:42",
			`read:42:${small.length}`,
			"size:42",
			`read:42:${grown.length}`,
		]);
	});

	test("rejects invalid PIDs, failures, repeated growth, and oversized allocation", async () => {
		const fixture = procArgs(["example"]);
		await expect(
			createKernProcArgs2Reader(resizingSysctl([fixture]).sysctl)(0),
		).rejects.toThrow(/pid/i);
		await expect(
			createKernProcArgs2Reader(
				resizingSysctl([fixture], { queryCode: 1 }).sysctl,
			)(42),
		).rejects.toThrow(/size.*code 1/i);
		await expect(
			createKernProcArgs2Reader(
				resizingSysctl([fixture, fixture], { readCodes: [12, 12] }).sysctl,
			)(42),
		).rejects.toThrow(/retry/i);
		const oversized: KernProcArgs2Sysctl = {
			querySize: () => ({ code: 0, size: 1025 }),
			read: () => ({ code: 0, size: 0 }),
		};
		await expect(
			createKernProcArgs2Reader(oversized, { maxBytes: 1024 })(42),
		).rejects.toThrow(/maximum/i);
	});
});

describe("stable process argv reader", () => {
	test("accepts argv only when before/after tokens match expected metadata", async () => {
		const calls: string[] = [];
		const stable = createStableProcessArgvReader(
			async (pid) => {
				calls.push(`argv:${pid}`);
				return ["exact"];
			},
			async (pid) => {
				calls.push(`token:${pid}`);
				return "expected";
			},
		);
		expect(await stable(42, "expected")).toEqual(["exact"]);
		expect(calls).toEqual(["token:42", "argv:42", "token:42"]);
	});

	test("deterministically rejects PID reuse during argv acquisition", async () => {
		const tokens = ["expected", "replacement"];
		const stable = createStableProcessArgvReader(
			async () => ["stale"],
			async () => tokens.shift(),
		);
		await expect(stable(42, "expected")).rejects.toThrow(
			/PID reuse|start token/i,
		);
	});

	test("inventory passes captured metadata token into the stable reader", async () => {
		const expected = "Sat Aug  1 10:00:00 2026";
		const received: Array<[number, string]> = [];
		const inventory = createMacProcessInventory(
			{
				run: async () => ({
					exitCode: 0,
					stdout: `42 1 ${expected}\n`,
					stderr: "",
				}),
			},
			async (pid, startToken) => {
				received.push([pid, startToken]);
				return ["exact"];
			},
		);
		await inventory.list();
		expect(received).toEqual([[42, expected]]);
	});
});

describe("live macOS KERN_PROCARGS2 reader", () => {
	test("reads exact special-character child argv with a stable start token", async () => {
		if (process.platform !== "darwin") return;
		const special = [
			"two words",
			'"double quotes"',
			"'single quotes'",
			String.raw`slashes\\stay\literal`,
			"",
		];
		const script =
			'import signal; print("READY", flush=True); signal.pause()';
		const child = Bun.spawn(
			["/usr/bin/python3", "-c", script, "--", ...special],
			{ stdin: "ignore", stdout: "pipe", stderr: "pipe" },
		);
		children.push(child);
		const stderr = new Response(child.stderr).text();
		const stream = child.stdout;
		if (typeof stream === "number") throw new Error("child stdout was not piped");
		const streamReader = stream.getReader();
		const decoder = new TextDecoder();
		let ready = "";
		try {
			while (!ready.includes("\n")) {
				const chunk = await streamReader.read();
				if (chunk.done) {
					throw new Error(
						`child exited ${await child.exited} before READY; stderr: ${await stderr}`,
					);
				}
				ready += decoder.decode(chunk.value, { stream: true });
			}
		} finally {
			streamReader.releaseLock();
		}
		expect(ready.slice(0, ready.indexOf("\n") + 1)).toBe("READY\n");

		const reader = createMacKernProcArgsReader();
		try {
			const argv = await reader.read(child.pid);
			expect(argv.slice(-special.length)).toEqual(special);
			const ownArgv = await reader.read(process.pid);
			expect(ownArgv).toContain(import.meta.path);
		} finally {
			reader.dispose();
			child.kill();
			await child.exited;
			children.splice(children.indexOf(child), 1);
		}
	});

	test("fails before loading FFI off macOS", () => {
		if (process.platform === "darwin") return;
		expect(() => createMacKernProcArgsReader()).toThrow(/darwin|macOS/i);
	});
});
