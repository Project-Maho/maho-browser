import { mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { assert, verifyWaterLiliesAsset } from "./contract-lib.mjs";

const projectRoot = resolve(fileURLToPath(new URL("..", import.meta.url)));
const root = mkdtempSync(resolve(tmpdir(), "maho-water-lilies-"));

try {
  const happy = verifyWaterLiliesAsset(projectRoot);
  const wrongAsset = resolve(root, "water-lilies.jpg");
  writeFileSync(wrongAsset, "wrong Water Lilies hash control\n");
  let rejected = false;
  try {
    verifyWaterLiliesAsset(projectRoot, wrongAsset);
  } catch {
    rejected = true;
  }
  assert(rejected, "wrong-hash Water Lilies control unexpectedly passed");
  console.log(JSON.stringify({ result: "PASS", happy, negativeControl: "wrong-hash" }));
} catch (error) {
  console.error(`asset boundary negative control failed: ${error instanceof Error ? error.message : String(error)}`);
  process.exitCode = 1;
} finally {
  rmSync(root, { force: true, recursive: true });
}
