import assert from "node:assert/strict";
import crypto from "node:crypto";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { afterEach, describe, test } from "bun:test";
import {
  EvidenceError,
  auditCompliance,
  createManifest,
  parsePlanFile,
  recordTask,
} from "../maho-motion-evidence/manifest.mjs";
import { runScopeCheck, runSourceGuards } from "../maho-motion-evidence/guards.mjs";

const temporaryDirectories: string[] = [];

afterEach(() => {
  for (const directory of temporaryDirectories.splice(0)) fs.rmSync(directory, { recursive: true, force: true });
});

function temporaryDirectory(): string {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), "maho-motion-evidence-"));
  temporaryDirectories.push(directory);
  return directory;
}

function writeFile(filePath: string, content: string): void {
  fs.mkdirSync(path.dirname(filePath), { recursive: true });
  fs.writeFileSync(filePath, content);
}

function sha256(content: string): string {
  return crypto.createHash("sha256").update(content).digest("hex");
}

function fixturePlan(): string {
  const mustHave = Array.from({ length: 82 }, (_, index) => `- Required scope ${index + 1}`).join("\n");
  const mustNot = Array.from({ length: 18 }, (_, index) => `- Forbidden scope ${index + 1}`).join("\n");
  const tasks = Array.from({ length: 16 }, (_, index) => {
    const number = index + 1;
    return `- [ ] ${number}. Task ${number}\n  Acceptance criteria: Task ${number} acceptance\n  QA scenarios: Happy - command ${number}. Failure - failure ${number}.\n  Commit: N`;
  }).join("\n\n");
  const finals = Array.from({ length: 4 }, (_, index) => `- [ ] F${index + 1}. Final ${index + 1}`).join("\n");
  return `# maho-landing-live-hero-motion-system - Work Plan\n\n## Scope\n### Must have\n${mustHave}\n\n### Must NOT have\n${mustNot}\n\n## Verification strategy\n- verifier\n\n## Todos\n${tasks}\n\n## Final verification wave\n${finals}\n\n## Commit strategy\n`;
}

function fixture(): { readonly root: string; readonly planPath: string; readonly evidenceDir: string } {
  const root = temporaryDirectory();
  const planPath = path.join(root, "plan.md");
  const evidenceDir = path.join(root, "evidence");
  fs.mkdirSync(evidenceDir);
  writeFile(planPath, fixturePlan());
  return { root, planPath, evidenceDir };
}

function artifactSet(evidenceDir: string, taskId: string): Record<string, string> {
  const artifacts = {
    acceptanceArtifact: path.join(evidenceDir, `${taskId}-acceptance.txt`),
    happyArtifact: path.join(evidenceDir, `${taskId}-happy.txt`),
    failureArtifact: path.join(evidenceDir, `${taskId}-failure.txt`),
  };
  for (const artifact of Object.values(artifacts)) writeFile(artifact, "evidence\n");
  return artifacts;
}

describe("maho motion evidence manifest", () => {
  test("indexes the full schema and rejects plan scope drift", () => {
    // Given: a complete plan fixture and an evidence directory
    const { evidenceDir, planPath } = fixture();
    // When: init parses the plan into the v1 manifest
    const manifest = createManifest({ evidenceDir, plan: parsePlanFile(planPath), createdAt: "2026-01-01T00:00:00.000Z" });
    // Then: every required task and final is represented deterministically
    assert.deepEqual(Object.keys(manifest.tasks), Array.from({ length: 16 }, (_, index) => `T${String(index + 1).padStart(2, "0")}`));
    assert.deepEqual(Object.keys(manifest.final), ["F1", "F2", "F3", "F4"]);

    writeFile(planPath, fixturePlan().replace("- Required scope 82", ""));
    assert.throws(() => parsePlanFile(planPath), (error: unknown) => error instanceof EvidenceError && error.code === "UNMAPPED_SCOPE_CRITERION");
  });

  test("rejects malformed manifests, criterion drift, and missing evidence artifacts", () => {
    // Given: an initialized manifest whose task evidence is incomplete
    const { evidenceDir, planPath } = fixture();
    const plan = parsePlanFile(planPath);
    const manifestPath = path.join(evidenceDir, "maho-motion-evidence.v1.json");
    writeFile(manifestPath, JSON.stringify({ schemaVersion: 1 }));
    // When / Then: malformed persisted JSON cannot be trusted
    assert.throws(() => recordTask({ evidenceDir, plan, taskId: "T01", ...artifactSet(evidenceDir, "T01"), happyCommand: "bun test", happyExit: 0, failureCommand: "bun test", expectedFailure: "expected", observedFailure: "expected" }), /MANIFEST_SCHEMA_INVALID/);

    writeFile(manifestPath, JSON.stringify(createManifest({ evidenceDir, plan, createdAt: "2026-01-01T00:00:00.000Z" })));
    const artifacts = artifactSet(evidenceDir, "T01");
    fs.rmSync(artifacts.failureArtifact);
    assert.throws(() => recordTask({ evidenceDir, plan, taskId: "T01", ...artifacts, happyCommand: "bun test", happyExit: 0, failureCommand: "bun test", expectedFailure: "expected", observedFailure: "expected" }), /ARTIFACT_MISSING/);

    writeFile(planPath, fixturePlan().replace("Task 1 acceptance", "Task 1 acceptance changed"));
    assert.throws(() => auditCompliance({ evidenceDir, plan: parsePlanFile(planPath) }), /CRITERION_HASH_DRIFT/);

    const persisted = JSON.parse(fs.readFileSync(manifestPath, "utf8"));
    persisted.tasks.T01.acceptance[0].textSha256 = "drifted";
    writeFile(manifestPath, JSON.stringify(persisted));
    assert.throws(() => auditCompliance({ evidenceDir, plan }), /CRITERION_HASH_DRIFT/);
  });

  test("records only exact successful task evidence inside the evidence directory", () => {
    // Given: initialized evidence and the exact task artifacts
    const { evidenceDir, planPath } = fixture();
    const plan = parsePlanFile(planPath);
    writeFile(path.join(evidenceDir, "maho-motion-evidence.v1.json"), JSON.stringify(createManifest({ evidenceDir, plan, createdAt: "2026-01-01T00:00:00.000Z" })));
    const artifacts = artifactSet(evidenceDir, "T01");
    // When: record-task receives matching failure evidence and a successful happy command
    const manifest = recordTask({ evidenceDir, plan, taskId: "T01", ...artifacts, happyCommand: "bun test", happyExit: 0, failureCommand: "bun test", expectedFailure: "expected", observedFailure: "expected" });
    // Then: the task is passed with only validated artifacts
    assert.equal(manifest.tasks.T01.status, "passed");
    assert.throws(() => recordTask({ evidenceDir, plan, taskId: "T01", ...artifacts, happyCommand: "bun test", happyExit: 1, failureCommand: "bun test", expectedFailure: "expected", observedFailure: "observed" }), /TASK_EVIDENCE_REJECTED/);
    assert.match(fs.readFileSync(path.join(evidenceDir, "maho-motion-evidence.v1.json"), "utf8"), /"failed"/);
    assert.throws(() => recordTask({ evidenceDir, plan, taskId: "T01", acceptanceArtifact: path.join(path.dirname(evidenceDir), "outside.txt"), happyArtifact: artifacts.happyArtifact, failureArtifact: artifacts.failureArtifact, happyCommand: "bun test", happyExit: 0, failureCommand: "bun test", expectedFailure: "expected", observedFailure: "expected" }), /ARTIFACT_OUTSIDE_EVIDENCE/);
  });

  test("approves only a complete artifact-backed manifest", () => {
    // Given: every indexed task has matching happy and expected-failure evidence
    const { evidenceDir, planPath } = fixture();
    const plan = parsePlanFile(planPath);
    writeFile(path.join(evidenceDir, "maho-motion-evidence.v1.json"), JSON.stringify(createManifest({ evidenceDir, plan, createdAt: "2026-01-01T00:00:00.000Z" })));
    for (const taskId of Object.keys(plan.tasks)) {
      recordTask({ evidenceDir, plan, taskId, ...artifactSet(evidenceDir, taskId), happyCommand: "bun test", happyExit: 0, failureCommand: "bun test", expectedFailure: "expected", observedFailure: "expected" });
    }
    // When: compliance reparses the plan and audits all persisted records
    const manifest = auditCompliance({ evidenceDir, plan });
    // Then: F1 is only approved with complete task evidence
    assert.equal(manifest.final.F1.status, "passed");
    assert.equal(fs.existsSync(path.join(evidenceDir, "final-f1-compliance.json")), true);
  });
});

describe("maho motion evidence static guards", () => {
  test("reports named source violations and passes clean source", () => {
    // Given: an allowed changed source file containing a forbidden type escape hatch
    const { root, evidenceDir } = fixture();
    const sourcePath = path.join(root, "website/src/components/landing/feature-showcase/ShowcaseLiveDesktop.tsx");
    const changedList = path.join(root, "changed.txt");
    const cases = [
      { path: "ShowcaseLiveDesktop.tsx", content: "const value = input as any;\n", code: "SOURCE_GUARD_FORBIDDEN_TYPE_ESCAPE" },
      { path: "ShowcaseLiveDesktop.tsx", content: "requestAnimationFrame(() => undefined);\n", code: "SOURCE_GUARD_INDEPENDENT_RAF" },
      { path: "AgentScene.tsx", content: "requestAnimationFrame(() => setActive(true));\n", code: "SOURCE_GUARD_FRAME_TICK_REACT_SETTER" },
      { path: "HeroBrowserVisual.tsx", content: "<Stage progress={0.4} />;\n", code: "SOURCE_GUARD_LIVE_PROGRESS_PROP" },
      { path: "captureParams.ts", content: "const value = parseFloat(input);\n", code: "SOURCE_GUARD_PERMISSIVE_PROGRESS_PARSE" },
      { path: "AgentScene.tsx", content: "const legacy = 'optional-ai';\n", code: "SOURCE_GUARD_LEGACY_SCENE_REFERENCE" },
      { path: "AgentScene.tsx", content: "const legacy = { title: 'Optional AI' };\n", code: "SOURCE_GUARD_LEGACY_SCENE_REFERENCE" },
      { path: "SplitViewScene.tsx", content: "const style = { willChange: 'transform' };\n", code: "SOURCE_GUARD_PERMANENT_WILL_CHANGE" },
      { path: "motion/MotionRuntime.ts", content: "export class MotionRuntime {}\n", code: "SOURCE_GUARD_MISSING_DISPOSE" },
    ];
    for (const guardCase of cases) {
      const relativePath = `website/src/components/landing/feature-showcase/${guardCase.path}`;
      writeFile(path.join(root, relativePath), guardCase.content);
      writeFile(changedList, `${relativePath}\n`);
      assert.throws(() => runSourceGuards({ evidenceDir, changedFileListPath: changedList, workspaceRoot: root }), new RegExp(guardCase.code));
    }

    const pricingPath = "website/src/components/LandingPage.tsx";
    writeFile(path.join(root, pricingPath), "const pricing = 'Optional AI subscription via OpenRouter for those who want managed inference.';\n");
    writeFile(changedList, `${pricingPath}\n`);
    assert.doesNotThrow(() => runSourceGuards({ evidenceDir, changedFileListPath: changedList, workspaceRoot: root }));

    const verifierPath = "website/scripts/maho-motion-evidence/guard-literals.mjs";
    writeFile(path.join(root, verifierPath), "const matcher = /as any|optional-ai/;\n");
    writeFile(changedList, `${verifierPath}\n`);
    assert.doesNotThrow(() => runSourceGuards({ evidenceDir, changedFileListPath: changedList, workspaceRoot: root }));

    const captureControllerPath = "website/src/components/landing/feature-showcase/CaptureController.tsx";
    writeFile(path.join(root, captureControllerPath), "const requestFrame = (callback) => window.requestAnimationFrame(callback);\nawait readiness;\nsetReadinessState({ status: 'ready' });\n");
    writeFile(changedList, `${captureControllerPath}\n`);
    assert.doesNotThrow(() => runSourceGuards({ evidenceDir, changedFileListPath: changedList, workspaceRoot: root }));

    writeFile(sourcePath, "export const ShowcaseLiveDesktop = () => null;\n");
    writeFile(changedList, "website/src/components/landing/feature-showcase/ShowcaseLiveDesktop.tsx\n");
    assert.equal(runSourceGuards({ evidenceDir, changedFileListPath: changedList, workspaceRoot: root }).status, "passed");
  });
});

describe("maho motion evidence scope", () => {
  test("fails closed for malformed baselines, untracked source omissions, staged files, and foreign paths", () => {
    // Given: a clean, exact baseline and changed-file list
    const { root, evidenceDir } = fixture();
    const websiteFile = path.join(root, "website/src/ok.ts");
    writeFile(websiteFile, "export const ok = true;\n");
    const baseline = path.join(root, "baseline.txt");
    const dirtyHashes = path.join(root, "hashes.json");
    const finalStatus = path.join(root, "status.txt");
    const changed = path.join(root, "changed.txt");
    writeFile(baseline, "");
    writeFile(dirtyHashes, "{}");
    writeFile(finalStatus, "");
    writeFile(changed, "website/src/ok.ts\n");
    // When: scope validates the supplied inputs
    assert.equal(runScopeCheck({ evidenceDir, baselinePath: baseline, dirtyHashesPath: dirtyHashes, finalStatusPath: finalStatus, changedFileListPath: changed, workspaceRoot: root }).status, "passed");

    writeFile(dirtyHashes, "{");
    assert.throws(() => runScopeCheck({ evidenceDir, baselinePath: baseline, dirtyHashesPath: dirtyHashes, finalStatusPath: finalStatus, changedFileListPath: changed, workspaceRoot: root }), /DIRTY_HASHES_JSON_INVALID/);
    writeFile(dirtyHashes, "{}");
    writeFile(finalStatus, "M  website/src/ok.ts\n");
    assert.throws(() => runScopeCheck({ evidenceDir, baselinePath: baseline, dirtyHashesPath: dirtyHashes, finalStatusPath: finalStatus, changedFileListPath: changed, workspaceRoot: root }), /STAGED_FILE_PRESENT/);
    writeFile(finalStatus, "");
    writeFile(finalStatus, "?? website/src/missing.ts\n");
    assert.throws(() => runScopeCheck({ evidenceDir, baselinePath: baseline, dirtyHashesPath: dirtyHashes, finalStatusPath: finalStatus, changedFileListPath: changed, workspaceRoot: root }), /UNTRACKED_CHANGED_SOURCE_MISSING/);
    writeFile(finalStatus, "");
    writeFile(changed, "maho/crates/maho-core/src/lib.rs\n");
    assert.throws(() => runScopeCheck({ evidenceDir, baselinePath: baseline, dirtyHashesPath: dirtyHashes, finalStatusPath: finalStatus, changedFileListPath: changed, workspaceRoot: root }), /SCOPE_PATH_FORBIDDEN/);
    assert.equal(sha256("evidence\n").length, 64);
  });
});
