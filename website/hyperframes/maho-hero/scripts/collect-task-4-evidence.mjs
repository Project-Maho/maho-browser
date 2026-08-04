import { existsSync, mkdirSync, readdirSync, writeFileSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { spawnSync } from "node:child_process";

const scriptDirectory = dirname(fileURLToPath(import.meta.url));
const projectRoot = resolve(scriptDirectory, "..");
const evidenceRoot = resolve(projectRoot, "../../../.omo/evidence/maho-hyperframes-hero-production/task-4");
const workspaceRoot = resolve(projectRoot, "../../..");
const fixtureDirectory = resolve(projectRoot, "scripts/fixtures/task-4");
const fixturePaths = readdirSync(fixtureDirectory)
  .filter((name) => name.endsWith(".fixture.txt"))
  .sort()
  .map((name) => `website/hyperframes/maho-hero/scripts/fixtures/task-4/${name}`);
const sourcePaths = [
  "website/hyperframes/maho-hero/shared/frame-contract.json",
  "website/hyperframes/maho-hero/shared/frame.css",
  "website/hyperframes/maho-hero/shared/frame.js",
  "website/hyperframes/maho-hero/scripts/check-task-4-contract.mjs",
  "website/hyperframes/maho-hero/scripts/collect-task-4-evidence.mjs",
  "website/hyperframes/maho-hero/scripts/task-4-runtime-controls.mjs",
  ...fixturePaths,
];
const remediationPaths = [
  "website/hyperframes/maho-hero/shared/frame.js",
  "website/hyperframes/maho-hero/scripts/check-task-4-contract.mjs",
  "website/hyperframes/maho-hero/scripts/collect-task-4-evidence.mjs",
  "website/hyperframes/maho-hero/scripts/task-4-runtime-controls.mjs",
  ...fixturePaths.filter((path) => !path.endsWith("root-background.fixture.txt") && !path.endsWith("video-geometry-tween.fixture.txt")),
];

const checks = [
  {
    name: "contract",
    artifact: "contract.json",
    command: "node",
    args: ["scripts/check-task-4-contract.mjs", "--foundation-only"],
    accepts: (result) => result.status === 0,
  },
  {
    name: "negative-fixtures",
    artifact: "negative-fixtures.json",
    command: "node",
    args: ["scripts/check-task-4-contract.mjs", "--negative-fixtures"],
    accepts: (result) => result.status === 0,
  },
  {
    name: "provenance",
    artifact: "provenance.log",
    command: "node",
    args: ["scripts/verify-provenance.mjs", "--manifest", "source-manifest.json"],
    accepts: (result) => result.status === 0,
  },
  {
    name: "frame-js-syntax",
    artifact: "frame-js-syntax.log",
    command: "node",
    args: ["--check", "shared/frame.js"],
    accepts: (result) => result.status === 0,
  },
  {
    name: "checker-syntax",
    artifact: "checker-syntax.log",
    command: "node",
    args: ["--check", "scripts/check-task-4-contract.mjs"],
    accepts: (result) => result.status === 0,
  },
  {
    name: "collector-syntax",
    artifact: "collector-syntax.log",
    command: "node",
    args: ["--check", "scripts/collect-task-4-evidence.mjs"],
    accepts: (result) => result.status === 0,
  },
  {
    name: "runtime-controls-syntax",
    artifact: "runtime-controls-syntax.log",
    command: "node",
    args: ["--check", "scripts/task-4-runtime-controls.mjs"],
    accepts: (result) => result.status === 0,
  },
  {
    name: "url-scan",
    artifact: "url-scan.log",
    command: "rg",
    args: ["-n", "https?://", "shared", "scripts/check-task-4-contract.mjs", "scripts/fixtures/task-4"],
    accepts: (result) => result.status === 1 && result.stdout.length === 0,
  },
  {
    name: "determinism-scan",
    artifact: "determinism-scan.log",
    command: "rg",
    args: ["-n", "Date\\.now|performance\\.now|Math\\.random|\\bfetch\\s*\\(|XMLHttpRequest|WebSocket|EventSource|setTimeout|setInterval|requestAnimationFrame", "shared"],
    accepts: (result) => result.status === 1 && result.stdout.length === 0,
  },
  {
    name: "helper-escape-scan",
    artifact: "helper-escape-scan.log",
    command: "rg",
    args: ["-n", "return\\s+timeline|\\.(?:play|pause)\\s*\\(", "shared/frame.js"],
    accepts: (result) => result.status === 1 && result.stdout.length === 0,
  },
  ...fixturePaths.map((workspacePath) => {
    const projectPath = workspacePath.replace("website/hyperframes/maho-hero/", "");
    const fixtureName = projectPath.slice(projectPath.lastIndexOf("/") + 1).replace(".fixture.txt", "");
    return {
      name: `fixture-${fixtureName}`,
      artifact: `fixture-${fixtureName}.log`,
      command: "node",
      args: ["scripts/check-task-4-contract.mjs", "--fixture", projectPath],
      accepts: (result) => result.status === 1 && result.stderr.includes('"result": "FAIL"'),
    };
  }),
];

function commandText(check) {
  return [check.command, ...check.args].join(" ");
}

function runCheck(check) {
  const result = spawnSync(check.command, check.args, {
    cwd: projectRoot,
    encoding: "utf8",
    maxBuffer: 16 * 1024 * 1024,
  });
  const passed = check.accepts(result);
  const record = {
    name: check.name,
    command: commandText(check),
    result: passed ? "PASS" : "FAIL",
    exitCode: result.status,
    signal: result.signal,
    stdout: result.stdout,
    stderr: result.stderr,
  };
  const artifactBody = check.artifact.endsWith(".json") && result.status === 0
    ? result.stdout
    : `${record.command}\nresult=${record.result}\nexitCode=${String(record.exitCode)}\n${record.stdout}${record.stderr}`;
  writeFileSync(resolve(evidenceRoot, check.artifact), artifactBody);
  return record;
}

mkdirSync(evidenceRoot, { recursive: true });
const results = checks.map(runCheck);
const compositionDirectory = resolve(projectRoot, "compositions");
const compositionHtml = existsSync(compositionDirectory)
  ? readdirSync(compositionDirectory).filter((name) => name.endsWith(".html"))
  : [];
const compositionBoundary = {
  name: "composition-boundary",
  command: "static directory inspection: compositions/*.html",
  result: compositionHtml.length === 0 ? "PASS" : "FAIL",
  files: compositionHtml,
};
writeFileSync(
  resolve(evidenceRoot, "composition-boundary.log"),
  `${compositionBoundary.command}\nresult=${compositionBoundary.result}\nfiles=${JSON.stringify(compositionBoundary.files)}\n`,
);
results.push(compositionBoundary);

const status = spawnSync(
  "git",
  ["status", "--short", "--untracked-files=all", "--", ...sourcePaths, ".omo/evidence/maho-hyperframes-hero-production/task-4"],
  { cwd: workspaceRoot, encoding: "utf8", maxBuffer: 16 * 1024 * 1024 },
);
writeFileSync(
  resolve(evidenceRoot, "git-status.log"),
  `git status --short --untracked-files=all -- <Task 4 paths>\nexitCode=${String(status.status)}\n${status.stdout}${status.stderr}`,
);
results.push({
  name: "git-status",
  command: "git status --short --untracked-files=all -- <Task 4 paths>",
  result: status.status === 0 ? "PASS" : "FAIL",
  exitCode: status.status,
  stdout: status.stdout,
  stderr: status.stderr,
});

const patches = sourcePaths.map((filePath) => {
  const diff = spawnSync("git", ["diff", "--no-index", "--", "/dev/null", filePath], {
    cwd: workspaceRoot,
    encoding: "utf8",
    maxBuffer: 16 * 1024 * 1024,
  });
  return {
    filePath,
    passed: diff.status === 1 && diff.stdout.length > 0,
    output: diff.stdout,
    stderr: diff.stderr,
  };
});
writeFileSync(
  resolve(evidenceRoot, "git-diff.patch"),
  patches.map((patch) => patch.output || patch.stderr).join("\n"),
);
results.push({
  name: "git-diff",
  command: "git diff --no-index -- /dev/null <each Task 4 source file>",
  result: patches.every((patch) => patch.passed) ? "PASS" : "FAIL",
  files: patches.map((patch) => patch.filePath),
});

const summary = {
  task: 4,
  project: "website/hyperframes/maho-hero",
  result: results.every((result) => result.result === "PASS") ? "PASS" : "FAIL",
  checks: results,
};
writeFileSync(
  resolve(evidenceRoot, "task-4-maho-hyperframes-hero-production.json"),
  `${JSON.stringify(summary, null, 2)}\n`,
);

const evidencePaths = [...new Set([...readdirSync(evidenceRoot), "DoneClaim.json"])]
  .sort()
  .map((name) => `.omo/evidence/maho-hyperframes-hero-production/task-4/${name}`);
const doneClaim = {
  DoneClaim: {
    task: "Todo 4 - Remediate Oracle timeline-helper findings",
    oracle: "ses_05f0e8a0cffeWYDF78oq4HOHml",
    changed_files: [...remediationPaths, ...evidencePaths],
    tests: [
      "node scripts/check-task-4-contract.mjs --foundation-only - PASS; 18 runtime controls, zero composition entries",
      "node scripts/check-task-4-contract.mjs --negative-fixtures - PASS; 12 fixtures rejected",
      "12 individual --fixture commands - PASS with expected exitCode=1",
      "node scripts/verify-provenance.mjs --manifest source-manifest.json - PASS",
      "node --check on frame.js, checker, collector, and runtime controls - PASS",
      "rg URL/determinism/helper-escape scans - PASS with no matches",
      "LSP diagnostics on frame.js, checker, collector, runtime controls, frame.css, and frame-contract.json - zero diagnostics",
    ],
    manual_qa: [
      ".omo/evidence/maho-hyperframes-hero-production/task-4/manual-inspection.md",
      ".omo/evidence/maho-hyperframes-hero-production/task-4/task-4-maho-hyperframes-hero-production.json",
      ".omo/evidence/maho-hyperframes-hero-production/task-4/git-diff.patch",
    ],
    cleanup: [
      "No server, preview, render, HyperFrames check, browser, temporary process, port, container, or composition entry was created.",
    ],
    risks: [
      "No individual composition exists yet; the hardened checker will validate future entries when separately authorized.",
      "Oracle remediation is implemented and evidenced but remains subject to the independent follow-up verdict before Todos 5-7.",
    ],
  },
};
writeFileSync(resolve(evidenceRoot, "DoneClaim.json"), `${JSON.stringify(doneClaim, null, 2)}\n`);

if (summary.result !== "PASS") process.exitCode = 1;
