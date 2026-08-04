import { existsSync, readFileSync, readdirSync, statSync, unlinkSync, writeFileSync } from "node:fs";
import { basename, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { assert, compositionIds, verifyCompositionSource, verifyEntryRoot, verifyLocalLinks, verifyProjectFiles, verifyWaterLiliesAsset } from "./contract-lib.mjs";

const scriptRoot = resolve(fileURLToPath(new URL("..", import.meta.url)));

function parseArguments(argumentsList) {
  let projectRoot = scriptRoot;
  let fixturePath;
  let entryRoot;
  for (let index = 0; index < argumentsList.length; index += 2) {
    const option = argumentsList[index];
    const value = argumentsList[index + 1];
    assert(value, `${option} requires a value`);
    if (option === "--project") projectRoot = resolve(value);
    else if (option === "--fixture") fixturePath = resolve(value);
    else if (option === "--entry-root") entryRoot = resolve(value);
    else throw new Error(`unsupported option: ${option}`);
  }
  return { projectRoot, fixturePath, entryRoot };
}

function canonicalEntries(projectRoot) {
  const manifestPath = resolve(projectRoot, "render-entry-manifest.json");
  assert(existsSync(manifestPath), "render-entry-manifest.json is missing");
  const manifest = JSON.parse(readFileSync(manifestPath, "utf8"));
  const expected = compositionIds.map((id) => `compositions/${id}.html`);
  assert(JSON.stringify(manifest.canonicalEntries) === JSON.stringify(expected), "manifest must allow exactly the three canonical direct entries");
  const directory = resolve(projectRoot, "compositions");
  const entries = readdirSync(directory).filter((name) => name.endsWith(".html")).sort();
  assert(JSON.stringify(entries) === JSON.stringify(expected.map((entry) => basename(entry)).sort()), "compositions may contain only canonical delivery entries");
  return compositionIds.map((id) => verifyCompositionSource(resolve(directory, `${id}.html`), id));
}

function verifyFixture(fixturePath) {
  const source = readFileSync(fixturePath, "utf8");
  const temporaryId = "spaces";
  const normalized = source.replace(/data-composition-id=(['"])[^'"]+\1/, `data-composition-id="${temporaryId}"`);
  const tempPath = `${fixturePath}.normalized.html`;
  try {
    writeFileSync(tempPath, normalized);
    verifyCompositionSource(tempPath, temporaryId);
  } finally {
    if (existsSync(tempPath)) unlinkSync(tempPath);
  }
}

try {
  const { projectRoot, fixturePath, entryRoot } = parseArguments(process.argv.slice(2));
  if (fixturePath) {
    verifyFixture(fixturePath);
    console.log(JSON.stringify({ result: "PASS", fixture: fixturePath }));
  } else {
    assert(statSync(projectRoot).isDirectory(), `project root is not a directory: ${projectRoot}`);
    verifyProjectFiles(projectRoot);
    const asset = verifyWaterLiliesAsset(projectRoot);
    const entries = canonicalEntries(projectRoot);
    if (entryRoot) {
      const linked = verifyEntryRoot(projectRoot, entryRoot);
      verifyLocalLinks(projectRoot, resolve(entryRoot, ".."));
      console.log(JSON.stringify({ result: "PASS", asset, entries, linked }));
    } else {
      console.log(JSON.stringify({ result: "PASS", asset, entries }));
    }
  }
} catch (error) {
  console.error(`DOM contract verification failed: ${error instanceof Error ? error.message : String(error)}`);
  process.exitCode = 1;
}
