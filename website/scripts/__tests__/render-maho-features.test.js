import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { describe, test } from "bun:test";
import {
  CAPTURE_HOST,
  CAPTURE_REQUESTS,
  createCaptureUrl,
  runCommand,
  stopProcessGroup,
} from "../render-maho-features.mjs";

describe("feature capture lifecycle", () => {
  test("uses frozen requests and an explicit IPv4 capture URL", () => {
    // Given: the authoritative capture requests
    // When: a scene URL is created for an ephemeral preview port
    const url = createCaptureUrl({ baseUrl: `http://${CAPTURE_HOST}:4321`, scene: "spaces" });

    // Then: all consumers use the immutable request contract and IPv4 route
    assert.equal(Object.isFrozen(CAPTURE_REQUESTS), true);
    assert.equal(Object.isFrozen(CAPTURE_REQUESTS.spaces), true);
    assert.equal(
      url,
      "http://127.0.0.1:4321/capture/maho-feature/?profile=feature&scene=spaces&theme=%23b58ad8&p=0.32",
    );
  });

  test("reports captured output when a child command exits nonzero", async () => {
    // Given: a child process that emits both output streams and fails
    const command = {
      command: process.execPath,
      args: ["-e", "console.log('stdout evidence'); console.error('stderr evidence'); process.exit(7)"],
      cwd: process.cwd(),
    };

    // When: command supervision awaits its exit
    const failure = await runCommand(command).catch((error) => error);

    // Then: the failure preserves exit status and both captured streams
    assert.ok(failure instanceof Error);
    assert.match(failure.message, /exit code 7/);
    assert.match(failure.message, /stdout evidence/);
    assert.match(failure.message, /stderr evidence/);
  });

  test("stops a detached preview process group and awaits its exit", async () => {
    // Given: a process that remains alive until signaled
    const preview = spawn(process.execPath, ["-e", "setInterval(() => {}, 1_000)"], {
      detached: true,
      stdio: "ignore",
    });

    // When: preview cleanup runs
    const exit = await stopProcessGroup({ child: preview, timeoutMs: 1_000 });

    // Then: the process has exited from the requested graceful signal
    assert.equal(exit.signal, "SIGTERM");
    assert.equal(preview.signalCode, "SIGTERM");
  });
});
