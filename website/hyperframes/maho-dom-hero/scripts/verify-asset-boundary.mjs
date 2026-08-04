import { resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { assert, verifyWaterLiliesAsset } from "./contract-lib.mjs";

const projectRoot = resolve(fileURLToPath(new URL("..", import.meta.url)));
const argumentsList = process.argv.slice(2);

try {
  assert(argumentsList.length === 0 || (argumentsList.length === 2 && argumentsList[0] === "--asset"), "Usage: node scripts/verify-asset-boundary.mjs [--asset <path>]");
  const assetPath = argumentsList.length === 0 ? undefined : resolve(argumentsList[1]);
  console.log(JSON.stringify({ result: "PASS", asset: verifyWaterLiliesAsset(projectRoot, assetPath) }));
} catch (error) {
  console.error(`asset boundary verification failed: ${error instanceof Error ? error.message : String(error)}`);
  process.exitCode = 1;
}
