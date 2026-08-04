import crypto from "node:crypto";
import fs from "node:fs";
import path from "node:path";
import { NAMED_CHECKS_V1, PLAN_CHECKS_V1 } from "./plan-checks.mjs";

export const PLAN_SLUG = "maho-landing-live-hero-motion-system";
export const MANIFEST_NAME = "maho-motion-evidence.v1.json";

export class EvidenceError extends Error {
  constructor(code, detail) {
    super(`${code}: ${detail}`);
    this.code = code;
  }
}

export function normalizeCriterionText(text) {
  return text.replace(/\r\n/g, "\n").replace(/\s+/g, " ").trim();
}

export function sha256(text) {
  return crypto.createHash("sha256").update(text).digest("hex");
}

function criterionId(prefix, index) {
  return `${prefix}${String(index + 1).padStart(3, "0")}`;
}

function requiredMatch(content, expression, code) {
  const match = content.match(expression);
  if (!match) throw new EvidenceError(code, "required plan section is missing");
  return match[1];
}

function scopeCriteria(section, prefix) {
  const entries = section.split("\n").filter((line) => line.trimStart().startsWith("- "));
  return entries.map((line, index) => Object.freeze({
    id: criterionId(prefix, index),
    text: normalizeCriterionText(line.trimStart().slice(2)),
    textSha256: sha256(normalizeCriterionText(line.trimStart().slice(2))),
  }));
}

function taskBlock(content, index) {
  const next = index === 16 ? "## Final verification wave" : `- \\[ \\] ${index + 1}\\.`;
  const expression = new RegExp(`- \\[ \\] ${index}\\. ([\\s\\S]*?)(?=${next})`);
  const match = content.match(expression);
  if (!match) throw new EvidenceError("PLAN_TASK_STRUCTURE_DRIFT", `missing T${String(index).padStart(2, "0")}`);
  return match[1];
}

function finalBlock(content, index) {
  const next = index === 4 ? "## Commit strategy" : `- \\[ \\] F${index + 1}\\.`;
  const expression = new RegExp(`- \\[ \\] F${index}\\. ([\\s\\S]*?)(?=${next})`);
  const match = content.match(expression);
  if (!match) throw new EvidenceError("PLAN_FINAL_STRUCTURE_DRIFT", `missing F${index}`);
  return normalizeCriterionText(match[1]);
}

function acceptanceBlock(taskText, taskId) {
  const match = taskText.match(/Acceptance criteria[^:]*:\s*([\s\S]*?)\n\s*QA scenarios/);
  if (!match) throw new EvidenceError("PLAN_TASK_STRUCTURE_DRIFT", `missing acceptance for ${taskId}`);
  return normalizeCriterionText(match[1]);
}

export function validatePlanChecks(criteria) {
  const criterionIds = new Set(criteria.map(({ id }) => id));
  const mappingIds = Object.keys(PLAN_CHECKS_V1);
  const unknown = mappingIds.filter((id) => !criterionIds.has(id));
  const unmapped = criteria.filter(({ id }) => !(id in PLAN_CHECKS_V1)).map(({ id }) => id);
  if (unknown.length || unmapped.length) {
    throw new EvidenceError("UNMAPPED_SCOPE_CRITERION", `unmapped=${unmapped.join(",") || "none"}; extra=${unknown.join(",") || "none"}`);
  }
  for (const [id, mapping] of Object.entries(PLAN_CHECKS_V1)) {
    if (!mapping.taskIds.length && !mapping.namedChecks.length) throw new EvidenceError("PLAN_CHECK_MAPPING_INVALID", `${id} has no check or task`);
    for (const taskId of mapping.taskIds) {
      if (!/^T(?:0[1-9]|1[0-6])$/.test(taskId)) throw new EvidenceError("PLAN_CHECK_MAPPING_INVALID", `${id} references ${taskId}`);
    }
    for (const namedCheck of mapping.namedChecks) {
      if (!(namedCheck in NAMED_CHECKS_V1)) throw new EvidenceError("PLAN_CHECK_MAPPING_INVALID", `${id} references ${namedCheck}`);
    }
  }
}

export function parsePlanText(content) {
  const mustHave = scopeCriteria(requiredMatch(content, /### Must have\n([\s\S]*?)\n### Must NOT have/, "PLAN_SCOPE_STRUCTURE_DRIFT"), "M");
  const mustNot = scopeCriteria(requiredMatch(content, /### Must NOT have[^\n]*\n([\s\S]*?)\n## Verification strategy/, "PLAN_SCOPE_STRUCTURE_DRIFT"), "N");
  const criteria = Object.freeze([...mustHave, ...mustNot]);
  validatePlanChecks(criteria);
  const tasks = {};
  for (let index = 1; index <= 16; index += 1) {
    const id = `T${String(index).padStart(2, "0")}`;
    const acceptance = acceptanceBlock(taskBlock(content, index), id);
    tasks[id] = Object.freeze({ id: `${id}-A001`, text: acceptance, textSha256: sha256(acceptance) });
  }
  const final = {};
  for (let index = 1; index <= 4; index += 1) {
    const id = `F${index}`;
    const text = finalBlock(content, index);
    final[id] = Object.freeze({ id, text, textSha256: sha256(text) });
  }
  return Object.freeze({ planSha256: sha256(content), criteria, tasks: Object.freeze(tasks), final: Object.freeze(final) });
}

export function parsePlanFile(planPath) {
  if (!planPath || !fs.existsSync(planPath)) throw new EvidenceError("PLAN_FILE_MISSING", String(planPath));
  return parsePlanText(fs.readFileSync(planPath, "utf8"));
}

function taskRecord(task) {
  return {
    status: "pending",
    acceptance: [{ id: task.id, textSha256: task.textSha256, artifacts: [] }],
    happy: { id: task.id.replace("-A", "-H"), command: "", exitCode: null, artifacts: [] },
    failure: { id: task.id.replace("-A", "-F"), command: "", expectedFailure: "", observedFailure: "", artifacts: [] },
  };
}

export function createManifest({ evidenceDir, plan, createdAt = new Date().toISOString() }) {
  const tasks = {};
  for (const [id, task] of Object.entries(plan.tasks)) tasks[id] = taskRecord(task);
  const final = {};
  for (const [id, item] of Object.entries(plan.final)) final[id] = { status: "pending", id: item.id, textSha256: item.textSha256, artifacts: [] };
  return {
    schemaVersion: 1,
    planSlug: PLAN_SLUG,
    planSha256: plan.planSha256,
    createdAt,
    baseline: { statusPath: path.join(evidenceDir, "task-1-pre-status.txt"), dirtyHashesPath: path.join(evidenceDir, "task-1-preexisting-dirty-hashes.json") },
    tasks,
    final,
  };
}

function isObject(value) { return value !== null && typeof value === "object" && !Array.isArray(value); }
function isStringArray(value) { return Array.isArray(value) && value.every((item) => typeof item === "string"); }
function requireObject(value, label) { if (!isObject(value)) throw new EvidenceError("MANIFEST_SCHEMA_INVALID", `${label} must be an object`); return value; }
function requireString(value, label) { if (typeof value !== "string") throw new EvidenceError("MANIFEST_SCHEMA_INVALID", `${label} must be a string`); return value; }
function requireKeys(value, keys, label) {
  const actual = Object.keys(value).sort().join(",");
  const expected = [...keys].sort().join(",");
  if (actual !== expected) throw new EvidenceError("MANIFEST_SCHEMA_INVALID", `${label} keys differ`);
}

export function parseJsonFile(filePath, code) {
  if (!filePath || !fs.existsSync(filePath)) throw new EvidenceError(`${code}_MISSING`, String(filePath));
  try { return JSON.parse(fs.readFileSync(filePath, "utf8")); } catch (error) { throw new EvidenceError(`${code}_INVALID`, error instanceof Error ? error.message : "invalid JSON"); }
}

export function validateManifest(raw, plan) {
  const manifest = requireObject(raw, "manifest");
  requireKeys(manifest, ["schemaVersion", "planSlug", "planSha256", "createdAt", "baseline", "tasks", "final"], "manifest");
  if (manifest.schemaVersion !== 1) throw new EvidenceError("MANIFEST_SCHEMA_INVALID", "schemaVersion must be 1");
  if (manifest.planSlug !== PLAN_SLUG) throw new EvidenceError("MANIFEST_SCHEMA_INVALID", "planSlug mismatch");
  requireString(manifest.planSha256, "planSha256"); requireString(manifest.createdAt, "createdAt");
  const baseline = requireObject(manifest.baseline, "baseline"); requireKeys(baseline, ["statusPath", "dirtyHashesPath"], "baseline"); requireString(baseline.statusPath, "baseline.statusPath"); requireString(baseline.dirtyHashesPath, "baseline.dirtyHashesPath");
  const tasks = requireObject(manifest.tasks, "tasks"); const final = requireObject(manifest.final, "final");
  const exactTaskIds = Object.keys(plan.tasks); const exactFinalIds = Object.keys(plan.final);
  if (Object.keys(tasks).join(",") !== exactTaskIds.join(",") || Object.keys(final).join(",") !== exactFinalIds.join(",")) throw new EvidenceError("MANIFEST_SCHEMA_INVALID", "task or final IDs differ from plan");
  for (const id of exactTaskIds) {
    const record = requireObject(tasks[id], `tasks.${id}`); const acceptance = record.acceptance;
    requireKeys(record, ["status", "acceptance", "happy", "failure"], `tasks.${id}`);
    if (!(["pending", "passed", "failed"].includes(record.status)) || !Array.isArray(acceptance) || acceptance.length !== 1) throw new EvidenceError("MANIFEST_SCHEMA_INVALID", `tasks.${id} shape`);
    const item = requireObject(acceptance[0], `tasks.${id}.acceptance`);
    requireKeys(item, ["id", "textSha256", "artifacts"], `tasks.${id}.acceptance`);
    if (item.id !== plan.tasks[id].id || item.textSha256 !== plan.tasks[id].textSha256 || !isStringArray(item.artifacts)) throw new EvidenceError("CRITERION_HASH_DRIFT", id);
    const happy = requireObject(record.happy, `tasks.${id}.happy`); requireKeys(happy, ["id", "command", "exitCode", "artifacts"], `tasks.${id}.happy`);
    if (happy.id !== `${id}-H001` || typeof happy.command !== "string" || !(happy.exitCode === null || Number.isInteger(happy.exitCode)) || !isStringArray(happy.artifacts)) throw new EvidenceError("MANIFEST_SCHEMA_INVALID", `tasks.${id}.happy`);
    const failure = requireObject(record.failure, `tasks.${id}.failure`); requireKeys(failure, ["id", "command", "expectedFailure", "observedFailure", "artifacts"], `tasks.${id}.failure`);
    if (failure.id !== `${id}-F001` || typeof failure.command !== "string" || typeof failure.expectedFailure !== "string" || typeof failure.observedFailure !== "string" || !isStringArray(failure.artifacts)) throw new EvidenceError("MANIFEST_SCHEMA_INVALID", `tasks.${id}.failure`);
  }
  for (const id of exactFinalIds) {
    const record = requireObject(final[id], `final.${id}`);
    requireKeys(record, ["status", "id", "textSha256", "artifacts"], `final.${id}`);
    if (!(["pending", "passed", "failed"].includes(record.status)) || record.id !== plan.final[id].id || record.textSha256 !== plan.final[id].textSha256 || !isStringArray(record.artifacts)) throw new EvidenceError("CRITERION_HASH_DRIFT", id);
  }
  return manifest;
}

function manifestPath(evidenceDir) { return path.join(evidenceDir, MANIFEST_NAME); }
function readManifest(evidenceDir, plan) { return validateManifest(parseJsonFile(manifestPath(evidenceDir), "MANIFEST"), plan); }
function writeManifest(evidenceDir, manifest) { fs.writeFileSync(manifestPath(evidenceDir), `${JSON.stringify(manifest, null, 2)}\n`); }

export function validateArtifact({ evidenceDir, artifactPath }) {
  if (typeof artifactPath !== "string" || !artifactPath) throw new EvidenceError("ARTIFACT_REQUIRED", "artifact path is required");
  const evidenceRoot = path.resolve(evidenceDir); const resolved = path.resolve(artifactPath);
  if (path.relative(evidenceRoot, resolved).startsWith("..") || path.relative(evidenceRoot, resolved) === "") throw new EvidenceError("ARTIFACT_OUTSIDE_EVIDENCE", artifactPath);
  if (!fs.existsSync(resolved) || !fs.statSync(resolved).isFile() || fs.statSync(resolved).size === 0) throw new EvidenceError("ARTIFACT_MISSING", artifactPath);
  return resolved;
}

export function recordTask({ evidenceDir, plan, taskId, acceptanceArtifact, happyCommand, happyExit, happyArtifact, failureCommand, expectedFailure, observedFailure, failureArtifact }) {
  const manifest = readManifest(evidenceDir, plan);
  if (!(taskId in plan.tasks)) throw new EvidenceError("TASK_ID_UNKNOWN", String(taskId));
  if (!Number.isInteger(happyExit)) throw new EvidenceError("HAPPY_EXIT_INVALID", String(happyExit));
  for (const [name, value] of Object.entries({ happyCommand, failureCommand, expectedFailure, observedFailure })) if (typeof value !== "string" || !value) throw new EvidenceError("TASK_RECORD_INVALID", `${name} is required`);
  const acceptance = validateArtifact({ evidenceDir, artifactPath: acceptanceArtifact }); const happy = validateArtifact({ evidenceDir, artifactPath: happyArtifact }); const failure = validateArtifact({ evidenceDir, artifactPath: failureArtifact });
  const record = manifest.tasks[taskId];
  if (record.acceptance[0].textSha256 !== plan.tasks[taskId].textSha256) throw new EvidenceError("CRITERION_HASH_DRIFT", taskId);
  record.acceptance[0].artifacts = [acceptance]; record.happy = { id: `${taskId}-H001`, command: happyCommand, exitCode: happyExit, artifacts: [happy] }; record.failure = { id: `${taskId}-F001`, command: failureCommand, expectedFailure, observedFailure, artifacts: [failure] };
  record.status = happyExit === 0 && expectedFailure === observedFailure ? "passed" : "failed";
  writeManifest(evidenceDir, manifest);
  if (record.status !== "passed") throw new EvidenceError("TASK_EVIDENCE_REJECTED", `${taskId} happy=${happyExit} expected=${expectedFailure} observed=${observedFailure}`);
  return manifest;
}

function recordArtifactsExist(record, id, evidenceDir) {
  const paths = [...record.acceptance[0].artifacts, ...record.happy.artifacts, ...record.failure.artifacts];
  if (paths.length < 3) throw new EvidenceError("ARTIFACT_REQUIRED", id);
  for (const artifactPath of paths) validateArtifact({ evidenceDir, artifactPath });
}

export function auditCompliance({ evidenceDir, plan }) {
  const manifest = readManifest(evidenceDir, plan);
  if (manifest.planSha256 !== plan.planSha256) throw new EvidenceError("PLAN_HASH_DRIFT", "manifest plan hash differs");
  validatePlanChecks(plan.criteria);
  for (const [id, record] of Object.entries(manifest.tasks)) {
    if (record.status !== "passed") throw new EvidenceError("TASK_NOT_PASSED", id);
    if (record.happy.exitCode !== 0 || record.failure.expectedFailure !== record.failure.observedFailure) throw new EvidenceError("TASK_EVIDENCE_REJECTED", id);
    recordArtifactsExist(record, id, evidenceDir);
  }
  for (const [name, taskId] of Object.entries(NAMED_CHECKS_V1)) {
    const record = manifest.tasks[taskId];
    if (!record || record.status !== "passed") throw new EvidenceError("NAMED_CHECK_ABSENT", `${name}:${taskId}`);
    recordArtifactsExist(record, taskId, evidenceDir);
  }
  manifest.final.F1 = { ...manifest.final.F1, status: "passed", artifacts: [path.join(evidenceDir, "final-f1-compliance.json")] };
  fs.writeFileSync(manifest.final.F1.artifacts[0], `${JSON.stringify({ status: "passed", planSha256: plan.planSha256 }, null, 2)}\n`);
  writeManifest(evidenceDir, manifest);
  return manifest;
}
