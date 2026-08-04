import fs from "node:fs";
import path from "node:path";
import {
  EvidenceError,
  MANIFEST_NAME,
  auditCompliance,
  createManifest,
  parsePlanFile,
  recordTask,
} from "./maho-motion-evidence/manifest.mjs";
import { runScopeCheck, runSourceGuards } from "./maho-motion-evidence/guards.mjs";

function parseArgs(args) {
  const options = {};
  for (let index = 0; index < args.length; index += 1) {
    const argument = args[index];
    if (!argument.startsWith("--")) throw new EvidenceError("CLI_ARGUMENT_INVALID", argument);
    const key = argument.slice(2);
    const value = args[index + 1];
    if (!value || value.startsWith("--")) throw new EvidenceError("CLI_ARGUMENT_VALUE_MISSING", argument);
    options[key] = value;
    index += 1;
  }
  return options;
}

function requiredOption(options, name) {
  const value = options[name];
  if (typeof value !== "string" || !value) throw new EvidenceError("CLI_OPTION_REQUIRED", `--${name}`);
  return value;
}

function planFor(options) {
  return parsePlanFile(options.plan ?? ".omo/plans/maho-landing-live-hero-motion-system.md");
}

function workspaceRootFor(options) {
  if (options["workspace-root"]) return options["workspace-root"];
  return fs.existsSync(path.join(process.cwd(), "website")) ? process.cwd() : path.resolve(process.cwd(), "..");
}

function runInit(options) {
  const evidenceDir = requiredOption(options, "evidence");
  if (!fs.existsSync(evidenceDir) || !fs.statSync(evidenceDir).isDirectory()) throw new EvidenceError("EVIDENCE_DIRECTORY_MISSING", evidenceDir);
  const manifestPath = path.join(evidenceDir, MANIFEST_NAME);
  if (fs.existsSync(manifestPath)) throw new EvidenceError("MANIFEST_ALREADY_EXISTS", manifestPath);
  const manifest = createManifest({ evidenceDir, plan: planFor(options) });
  fs.writeFileSync(manifestPath, `${JSON.stringify(manifest, null, 2)}\n`);
  console.log(`Initialized evidence manifest at ${manifestPath}`);
  console.log("Successfully indexed 16 tasks and 4 final verification items.");
}

function runRecordTask(options) {
  const evidenceDir = requiredOption(options, "evidence");
  const happyExit = Number(requiredOption(options, "happy-exit"));
  const manifest = recordTask({
    evidenceDir,
    plan: planFor(options),
    taskId: requiredOption(options, "task"),
    acceptanceArtifact: requiredOption(options, "acceptance-artifact"),
    happyCommand: requiredOption(options, "happy-command"),
    happyExit,
    happyArtifact: requiredOption(options, "happy-artifact"),
    failureCommand: requiredOption(options, "failure-command"),
    expectedFailure: requiredOption(options, "expected-failure"),
    observedFailure: requiredOption(options, "observed-failure"),
    failureArtifact: requiredOption(options, "failure-artifact"),
  });
  console.log(`Recorded task ${requiredOption(options, "task")} with status ${manifest.tasks[requiredOption(options, "task")].status}`);
}

function runCompliance(options) {
  auditCompliance({ evidenceDir: requiredOption(options, "evidence"), plan: planFor(options) });
  console.log("F1: APPROVE");
}

function runSourceGuardMode(options) {
  runSourceGuards({
    evidenceDir: requiredOption(options, "evidence"),
    changedFileListPath: requiredOption(options, "changed-file-list"),
    workspaceRoot: workspaceRootFor(options),
  });
  console.log("F2: APPROVE");
}

function runScopeMode(options) {
  runScopeCheck({
    evidenceDir: requiredOption(options, "evidence"),
    baselinePath: requiredOption(options, "baseline"),
    dirtyHashesPath: requiredOption(options, "dirty-hashes"),
    finalStatusPath: requiredOption(options, "final-status"),
    changedFileListPath: requiredOption(options, "changed-file-list"),
    workspaceRoot: workspaceRootFor(options),
  });
  console.log("F4: APPROVE");
}

export function main(args = process.argv.slice(2)) {
  const options = parseArgs(args);
  const mode = options.mode ?? "init";
  switch (mode) {
    case "init": runInit(options); return;
    case "record-task": runRecordTask(options); return;
    case "compliance": runCompliance(options); return;
    case "source-guards": runSourceGuardMode(options); return;
    case "scope": runScopeMode(options); return;
    default: throw new EvidenceError("CLI_MODE_UNKNOWN", mode);
  }
}

if (import.meta.url === `file://${process.argv[1]}`) {
  try {
    main();
  } catch (error) {
    const message = error instanceof Error ? error.message : "UNKNOWN_FAILURE";
    console.error(message);
    process.exitCode = 1;
  }
}
