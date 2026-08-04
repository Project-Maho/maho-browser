import { existsSync, linkSync, lstatSync, mkdtempSync, rmSync, symlinkSync } from "node:fs";
import { tmpdir } from "node:os";
import { resolve } from "node:path";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import { assert, compositionIds, verifyEntryRoot, verifyLocalLinks } from "./contract-lib.mjs";

const projectRoot = resolve(fileURLToPath(new URL("..", import.meta.url)));

function parseArguments(argumentsList) {
  const marker = argumentsList.indexOf("--");
  assert(marker === 1, "Usage: node scripts/with-entry-root.mjs <spaces|split-view|command-ai> -- <check-or-snapshot-command>");
  const id = argumentsList[0];
  assert(compositionIds.includes(id), `unknown canonical entry: ${id}`);
  const command = argumentsList.slice(marker + 1);
  assert(command.length > 0, "a check or snapshot command is required");
  assert(!command.some((value) => /(?:^|[\\/])(render|publish)$|\.mp4$|provenance/i.test(value)), "disposable entry roots cannot be delivery provenance or render sources");
  return { id, command };
}

function linkLocalRoot(root) {
  for (const name of ["assets", "compositions", "node_modules", "scripts", "shared", "hyperframes.json", "package.json"]) {
    const target = resolve(projectRoot, name);
    symlinkSync(target, resolve(root, name));
    const link = lstatSync(resolve(root, name));
    assert(link.isSymbolicLink(), `${name} must be a local symlink`);
  }
}

const { id, command } = parseArguments(process.argv.slice(2));
const root = mkdtempSync(resolve(tmpdir(), "maho-dom-entry-"));
const canonical = resolve(projectRoot, "compositions", `${id}.html`);
const motion = resolve(projectRoot, "compositions", `${id}.motion.json`);
const entryRoot = resolve(root, "index.html");

try {
  linkSync(canonical, entryRoot);
  assert(existsSync(motion), `missing canonical motion sidecar: ${id}.motion.json`);
  symlinkSync(motion, resolve(root, "index.motion.json"));
  linkLocalRoot(root);
  const before = verifyEntryRoot(projectRoot, entryRoot, id);
  verifyLocalLinks(projectRoot, root);
  const result = spawnSync(command[0], command.slice(1), {
    cwd: root,
    encoding: "utf8",
    env: { ...process.env, MAHO_DOM_ENTRY_ID: id },
  });
  if (result.stdout) process.stdout.write(result.stdout);
  if (result.stderr) process.stderr.write(result.stderr);
  const after = verifyEntryRoot(projectRoot, entryRoot, id);
  assert(before.inode === after.inode && before.sha256 === after.sha256, "canonical source changed while command ran");
  assert(result.status === 0, `entry-root command failed with status ${result.status ?? "signal"}`);
  console.log(JSON.stringify({ result: "PASS", id, before, after, rootRemoved: true }));
} catch (error) {
  console.error(`entry-root verification failed: ${error instanceof Error ? error.message : String(error)}`);
  process.exitCode = 1;
} finally {
  rmSync(root, { force: true, recursive: true });
}
