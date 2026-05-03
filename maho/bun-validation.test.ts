import { expect, test } from "bun:test";

test("Rust workspace tests pass", () => {
  const result = Bun.spawnSync([
    "cargo",
    "test",
    "--workspace",
    "--lib",
    "--tests",
  ], {
    stdout: "pipe",
    stderr: "pipe",
  });

  if (result.exitCode !== 0) {
    throw new Error(
      `cargo test failed with exit code ${result.exitCode}\n\nSTDOUT:\n${result.stdout.toString()}\n\nSTDERR:\n${result.stderr.toString()}`,
    );
  }

  expect(result.exitCode).toBe(0);
}, 60000);
