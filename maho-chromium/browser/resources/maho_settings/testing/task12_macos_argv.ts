import { dlopen, FFIType } from "bun:ffi";

const CTL_KERN = 1;
const KERN_PROCARGS2 = 49;
const ENOMEM = 12;
const DEFAULT_MAX_BYTES = 1024 * 1024;

export class KernProcArgs2Error extends Error {
	constructor(message: string) {
		super(message);
		this.name = "KernProcArgs2Error";
	}
}

export interface KernProcArgs2SysctlResult {
	readonly code: number;
	readonly size: number;
}

export interface KernProcArgs2Sysctl {
	querySize(pid: number): KernProcArgs2SysctlResult;
	read(pid: number, destination: Uint8Array): KernProcArgs2SysctlResult;
}

function requireValidSize(size: number, maxBytes: number): void {
	if (!Number.isSafeInteger(size) || size <= 0) {
		throw new KernProcArgs2Error(`KERN_PROCARGS2 returned invalid size ${size}`);
	}
	if (size > maxBytes) {
		throw new KernProcArgs2Error(
			`KERN_PROCARGS2 size ${size} exceeds maximum ${maxBytes}`,
		);
	}
}

function findNull(buffer: Uint8Array, start: number): number {
	const end = buffer.indexOf(0, start);
	if (end < 0) {
		throw new KernProcArgs2Error("KERN_PROCARGS2 contains an unterminated string");
	}
	return end;
}

export function parseKernProcArgs2(buffer: Uint8Array): readonly string[] {
	if (buffer.byteLength < 4) {
		throw new KernProcArgs2Error("KERN_PROCARGS2 is truncated before argc");
	}
	const argc = new DataView(
		buffer.buffer,
		buffer.byteOffset,
		buffer.byteLength,
	).getInt32(0, true);
	if (argc < 0) {
		throw new KernProcArgs2Error(`KERN_PROCARGS2 has invalid argc ${argc}`);
	}

	const decoder = new TextDecoder("utf-8", { fatal: true });
	let offset = findNull(buffer, 4) + 1;
	while (offset < buffer.length && buffer[offset] === 0) offset += 1;

	const argv: string[] = [];
	try {
		for (let index = 0; index < argc; index += 1) {
			if (offset >= buffer.length) {
				throw new KernProcArgs2Error(
					`KERN_PROCARGS2 argv is truncated at argument ${index}`,
				);
			}
			const end = findNull(buffer, offset);
			argv.push(decoder.decode(buffer.subarray(offset, end)));
			offset = end + 1;
		}
	} catch (error) {
		if (error instanceof KernProcArgs2Error) throw error;
		throw new KernProcArgs2Error(
			`KERN_PROCARGS2 contains invalid UTF-8: ${String(error)}`,
		);
	}
	return argv;
}

export function createKernProcArgs2Reader(
	sysctl: KernProcArgs2Sysctl,
	options: { readonly maxBytes?: number } = {},
): (pid: number) => Promise<readonly string[]> {
	const maxBytes = options.maxBytes ?? DEFAULT_MAX_BYTES;
	return async (pid) => {
		if (!Number.isSafeInteger(pid) || pid <= 0) {
			throw new KernProcArgs2Error(`Invalid PID ${pid}`);
		}

		for (let attempt = 0; attempt < 2; attempt += 1) {
			const queried = sysctl.querySize(pid);
			if (queried.code !== 0) {
				throw new KernProcArgs2Error(
					`KERN_PROCARGS2 size query failed with code ${queried.code}`,
				);
			}
			requireValidSize(queried.size, maxBytes);

			const destination = new Uint8Array(queried.size);
			const result = sysctl.read(pid, destination);
			const grew = result.code === ENOMEM || result.size > destination.length;
			if (grew) {
				if (attempt === 0) continue;
				throw new KernProcArgs2Error(
					"KERN_PROCARGS2 grew again after the bounded retry",
				);
			}
			if (result.code !== 0) {
				throw new KernProcArgs2Error(
					`KERN_PROCARGS2 read failed with code ${result.code}`,
				);
			}
			requireValidSize(result.size, maxBytes);
			return parseKernProcArgs2(destination.subarray(0, result.size));
		}
		throw new KernProcArgs2Error("KERN_PROCARGS2 retry exhausted");
	};
}

export function createStableProcessArgvReader(
	readArgv: (pid: number) => Promise<readonly string[]>,
	readStartToken: (pid: number) => Promise<string | undefined>,
): (pid: number, expectedStartToken: string) => Promise<readonly string[]> {
	return async (pid, expectedStartToken) => {
		const before = await readStartToken(pid);
		if (before !== expectedStartToken) {
			throw new KernProcArgs2Error(
				`PID reuse or start token mismatch before argv read for PID ${pid}`,
			);
		}
		const argv = await readArgv(pid);
		const after = await readStartToken(pid);
		if (after !== expectedStartToken) {
			throw new KernProcArgs2Error(
				`PID reuse or start token mismatch after argv read for PID ${pid}`,
			);
		}
		return argv;
	};
}

export function createMacKernProcArgsReader(): {
	read(pid: number): Promise<readonly string[]>;
	dispose(): void;
} {
	if (process.platform !== "darwin") {
		throw new KernProcArgs2Error(
			"KERN_PROCARGS2 is available only on macOS (darwin)",
		);
	}

	const library = dlopen("/usr/lib/libSystem.B.dylib", {
		sysctl: {
			args: [
				FFIType.ptr,
				FFIType.uint32_t,
				FFIType.ptr,
				FFIType.ptr,
				FFIType.ptr,
				FFIType.uint64_t,
			],
			returns: FFIType.int,
		},
	});
	let disposed = false;
	const requireOpen = () => {
		if (disposed) throw new KernProcArgs2Error("KERN_PROCARGS2 reader disposed");
	};
	const sysctl: KernProcArgs2Sysctl = {
		querySize(pid) {
			requireOpen();
			const mib = new Int32Array([CTL_KERN, KERN_PROCARGS2, pid]);
			const size = new BigUint64Array(1);
			const code = library.symbols.sysctl(mib, mib.length, null, size, null, 0);
			return { code, size: Number(size[0]) };
		},
		read(pid, destination) {
			requireOpen();
			const mib = new Int32Array([CTL_KERN, KERN_PROCARGS2, pid]);
			const size = new BigUint64Array([BigInt(destination.length)]);
			const code = library.symbols.sysctl(
				mib,
				mib.length,
				destination,
				size,
				null,
				0,
			);
			return { code, size: Number(size[0]) };
		},
	};
	const read = createKernProcArgs2Reader(sysctl);
	return {
		read,
		dispose() {
			if (disposed) return;
			disposed = true;
			library.close();
		},
	};
}
