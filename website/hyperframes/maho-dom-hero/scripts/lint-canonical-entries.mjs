import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import { resolve } from "node:path";
import { compositionIds } from "./contract-lib.mjs";

const projectRoot = resolve(fileURLToPath(new URL("..", import.meta.url)));
const helper = resolve(projectRoot, "scripts", "with-entry-root.mjs");

try {
  for (const id of compositionIds) {
    const result = spawnSync(
      process.execPath,
      [helper, id, "--", "bunx", "--bun", "hyperframes@0.7.72", "lint", ".", "--json"],
      { cwd: projectRoot, encoding: "utf8" },
    );
    if (result.stdout) process.stdout.write(result.stdout);
    if (result.stderr) process.stderr.write(result.stderr);
    if (result.status !== 0) throw new Error(`HyperFrames lint failed for ${id}`);
  }
  console.log(JSON.stringify({ result: "PASS", entries: compositionIds }));
} catch (error) {
  console.error(`canonical lint failed: ${error instanceof Error ? error.message : String(error)}`);
  process.exitCode = 1;
}
