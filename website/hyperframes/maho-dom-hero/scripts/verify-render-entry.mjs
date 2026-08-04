import { readFileSync } from "node:fs";
import { resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { assert, compositionIds, verifyCompositionSource, verifyWaterLiliesAsset } from "./contract-lib.mjs";

const defaultProject = resolve(fileURLToPath(new URL("..", import.meta.url)));
const argumentsList = process.argv.slice(2);
const projectFlag = argumentsList.indexOf("--project");
const projectRoot = projectFlag === -1 ? defaultProject : resolve(argumentsList[projectFlag + 1] ?? "");

try {
  assert(argumentsList.length === 0 || (projectFlag === 0 && argumentsList.length === 2), "Usage: node scripts/verify-render-entry.mjs [--project <path>]");
  const manifest = JSON.parse(readFileSync(resolve(projectRoot, "render-entry-manifest.json"), "utf8"));
  const expected = compositionIds.map((id) => `compositions/${id}.html`);
  assert(JSON.stringify(manifest.canonicalEntries) === JSON.stringify(expected), "delivery manifest must list only canonical composition paths");
  assert(!manifest.canonicalEntries.includes("index.html"), "delivery provenance must not name a disposable entry root");
  const asset = verifyWaterLiliesAsset(projectRoot);
  const entries = compositionIds.map((id) => verifyCompositionSource(resolve(projectRoot, "compositions", `${id}.html`), id));
  console.log(JSON.stringify({ result: "PASS", project: projectRoot, asset, entries }));
} catch (error) {
  console.error(`render-entry verification failed: ${error instanceof Error ? error.message : String(error)}`);
  process.exitCode = 1;
}
