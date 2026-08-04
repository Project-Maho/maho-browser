import {mkdtemp, open, readFile, readdir, rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {dirname, join, resolve} from 'node:path';
import {fileURLToPath} from 'node:url';

export const TASK12_ENTRY_RELATIVE_PATH =
  'maho-chromium/browser/resources/maho_settings/testing/qa_two_profile_settings_entry.ts';
export const EXPECTED_BLOCKED_JSON_LINE = JSON.stringify({
  status: 'blocked-current-app',
  reasons: ['STALE_APP', 'CDP_9222_UNAVAILABLE', 'RELAY_18765_UNAVAILABLE'],
});

const TESTING_RELATIVE_PATH =
  'maho-chromium/browser/resources/maho_settings/testing';
const RUNTIME_ARTIFACT_PATTERN = /task12.*(?:runtime|artifact|evidence)/i;

interface ChildResult {
  exitCode: number;
  stdout: string;
  stderr: string;
}

function requireContract(condition: unknown, message: string): asserts condition {
  if (!condition) {
    throw new Error(message);
  }
}

async function findWorkspaceRoot(): Promise<string> {
  let current = dirname(fileURLToPath(import.meta.url));
  while (true) {
    const candidate = join(current, TASK12_ENTRY_RELATIVE_PATH);
    if (await Bun.file(candidate).exists()) {
      return current;
    }
    const parent = dirname(current);
    requireContract(parent !== current, 'workspace root not found');
    current = parent;
  }
}

async function snapshotRuntimeArtifacts(root: string): Promise<string[]> {
  const matches: string[] = [];

  async function visit(directory: string, relativeDirectory: string): Promise<void> {
    const entries = await readdir(directory, {withFileTypes: true});
    for (const entry of entries) {
      const relativePath = join(relativeDirectory, entry.name);
      if (entry.isDirectory()) {
        if (entry.name === '.git' || entry.name === 'node_modules') {
          continue;
        }
        await visit(join(directory, entry.name), relativePath);
      } else if (RUNTIME_ARTIFACT_PATTERN.test(relativePath)) {
        matches.push(relativePath);
      }
    }
  }

  await visit(join(root, TESTING_RELATIVE_PATH), TESTING_RELATIVE_PATH);
  return matches.sort();
}

async function runChild(
  command: string[],
  cwd: string,
  tempDirectory: string,
  label: string,
): Promise<ChildResult> {
  const stdoutPath = join(tempDirectory, `${label}.stdout`);
  const stderrPath = join(tempDirectory, `${label}.stderr`);
  const stdout = await open(stdoutPath, 'wx+');
  const stderr = await open(stderrPath, 'wx+');

  try {
    const process = Bun.spawn(command, {
      cwd,
      stdin: 'ignore',
      stdout: stdout.fd,
      stderr: stderr.fd,
    });
    const exitCode = await process.exited;
    await Promise.all([stdout.sync(), stderr.sync()]);
    return {
      exitCode,
      stdout: await readFile(stdoutPath, 'utf8'),
      stderr: await readFile(stderrPath, 'utf8'),
    };
  } finally {
    await Promise.all([stdout.close(), stderr.close()]);
  }
}

function verifyEntryResult(result: ChildResult, label: string): void {
  requireContract(result.exitCode === 1, `${label}: exit ${result.exitCode}`);
  requireContract(result.stderr === '', `${label}: stderr was not empty`);
  requireContract(
    result.stdout === `${EXPECTED_BLOCKED_JSON_LINE}\n`,
    `${label}: stdout did not contain exactly the expected JSON line`,
  );
}

function verifySilentImport(result: ChildResult, label: string): void {
  requireContract(result.exitCode === 0, `${label}: exit ${result.exitCode}`);
  requireContract(result.stdout === '', `${label}: stdout was not empty`);
  requireContract(result.stderr === '', `${label}: stderr was not empty`);
}

export async function verifyTwoProfileSettingsCli(): Promise<void> {
  const workspaceRoot = await findWorkspaceRoot();
  const testingCwd = resolve(workspaceRoot, TESTING_RELATIVE_PATH);
  const entryPath = resolve(workspaceRoot, TASK12_ENTRY_RELATIVE_PATH);
  const implementationPath = resolve(testingCwd, 'qa_two_profile_settings_cli');
  const before = await snapshotRuntimeArtifacts(workspaceRoot);
  const tempDirectory = await mkdtemp(join(tmpdir(), 'task12-cli-verifier-'));

  try {
    verifyEntryResult(
      await runChild([process.execPath, entryPath], workspaceRoot, tempDirectory, 'entry-root'),
      'entry from workspace root',
    );
    verifyEntryResult(
      await runChild([process.execPath, entryPath], testingCwd, tempDirectory, 'entry-testing'),
      'entry from testing cwd',
    );
    verifySilentImport(
      await runChild(
        [process.execPath, '-e', `await import(${JSON.stringify(`${implementationPath}.js`)})`],
        workspaceRoot,
        tempDirectory,
        'import-js',
      ),
      'implementation .js import',
    );
    verifySilentImport(
      await runChild(
        [process.execPath, '-e', `await import(${JSON.stringify(`${implementationPath}.ts`)})`],
        workspaceRoot,
        tempDirectory,
        'import-ts',
      ),
      'implementation .ts import',
    );

    const after = await snapshotRuntimeArtifacts(workspaceRoot);
    requireContract(
      JSON.stringify(after) === JSON.stringify(before),
      `Task12 runtime artifact inventory changed: before=${JSON.stringify(before)} after=${JSON.stringify(after)}`,
    );
  } finally {
    await rm(tempDirectory, {recursive: true, force: true});
  }
}

if (import.meta.main) {
  try {
    await verifyTwoProfileSettingsCli();
    console.log('PASS Task12 two-profile settings CLI contract');
  } catch (error) {
    console.error(error instanceof Error ? error.message : String(error));
    process.exitCode = 1;
  }
}
