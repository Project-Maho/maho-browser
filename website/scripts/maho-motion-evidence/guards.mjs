import crypto from "node:crypto";
import fs from "node:fs";
import path from "node:path";
import { EvidenceError } from "./manifest.mjs";

const SOURCE_EXTENSION = /\.(?:ts|tsx|astro|mjs)$/;
const FEATURE_PREFIX = "website/src/components/landing/feature-showcase/";
const RUNTIME_RAF_PATHS = new Set([
  `${FEATURE_PREFIX}motion/MotionRuntime.ts`,
  `${FEATURE_PREFIX}CaptureController.tsx`,
  `${FEATURE_PREFIX}captureReadiness.ts`,
]);
const RUNTIME_OWNER_PATHS = new Set([
  `${FEATURE_PREFIX}motion/MotionRuntime.ts`,
  `${FEATURE_PREFIX}motion/LandingMotionCoordinator.ts`,
  `${FEATURE_PREFIX}MahoBrowserStage.tsx`,
]);
const LEGACY_SCENE_IDENTIFIER = /\b(?:optional-ai|optionalAi|OptionalAIScene|useShowcaseLoop|maho-feature-optional-ai)\b/;
const LEGACY_SCENE_LABEL = /\b(?:title|eyebrow|badge)\s*(?:=|:)\s*["'`]Optional AI["'`]/;
const SCOPE_ALLOWLIST = Object.freeze([
  "website/",
  "docs/brand/DESIGN.md",
  ".omo/plans/maho-landing-live-hero-motion-system.md",
  ".omo/evidence/maho-landing-live-hero-motion-system/",
]);

function readRequiredText(filePath, code) {
  if (!filePath || !fs.existsSync(filePath) || !fs.statSync(filePath).isFile()) throw new EvidenceError(`${code}_MISSING`, String(filePath));
  return fs.readFileSync(filePath, "utf8");
}

function listedPaths(filePath, code) {
  const content = readRequiredText(filePath, code);
  const entries = content.split("\n").map((line) => line.trim()).filter(Boolean);
  if (!entries.length) throw new EvidenceError(`${code}_EMPTY`, String(filePath));
  if (entries.some((entry) => path.isAbsolute(entry) || entry.includes("\0") || entry.startsWith("../"))) throw new EvidenceError(`${code}_INVALID`, "paths must be workspace-relative");
  return entries;
}

function sourcePath(workspaceRoot, relativePath) {
  return path.resolve(workspaceRoot, relativePath);
}

function violation(code, filePath) { return `${code}: ${filePath}`; }
function isTypeSource(relativePath) { return /\.(?:ts|tsx|astro)$/.test(relativePath); }
function isProductionContentSource(relativePath) { return relativePath.startsWith("website/src/") && isTypeSource(relativePath); }

function sourceViolations(relativePath, content) {
  const violations = [];
  if (isTypeSource(relativePath) && /\bas\s+(?:any|unknown)\b|@ts-ignore|@ts-expect-error/.test(content)) violations.push(violation("SOURCE_GUARD_FORBIDDEN_TYPE_ESCAPE", relativePath));
  if (isProductionContentSource(relativePath) && (LEGACY_SCENE_IDENTIFIER.test(content) || LEGACY_SCENE_LABEL.test(content))) violations.push(violation("SOURCE_GUARD_LEGACY_SCENE_REFERENCE", relativePath));
  if (relativePath.startsWith(FEATURE_PREFIX) && /\bparseFloat\s*\(/.test(content)) violations.push(violation("SOURCE_GUARD_PERMISSIVE_PROGRESS_PARSE", relativePath));
  if (relativePath.startsWith(FEATURE_PREFIX) && /(?:will-change\s*:|willChange\s*:)/.test(content) && !relativePath.endsWith("motion/MotionRuntime.ts")) violations.push(violation("SOURCE_GUARD_PERMANENT_WILL_CHANGE", relativePath));
  if (relativePath.startsWith(FEATURE_PREFIX) && /\brequestAnimationFrame\s*\(/.test(content) && !RUNTIME_RAF_PATHS.has(relativePath)) violations.push(violation("SOURCE_GUARD_INDEPENDENT_RAF", relativePath));
  if (relativePath.startsWith(FEATURE_PREFIX) && !RUNTIME_RAF_PATHS.has(relativePath) && /requestAnimationFrame[\s\S]{0,260}\b(?:set[A-Z][A-Za-z0-9_]*|dispatch)\s*\(/.test(content)) violations.push(violation("SOURCE_GUARD_FRAME_TICK_REACT_SETTER", relativePath));
  if (/(?:HeroBrowserVisual|ShowcaseLiveDesktop)\.tsx$/.test(relativePath) && /\b(?:progress|actionProgress)\s*=\s*\{/.test(content)) violations.push(violation("SOURCE_GUARD_LIVE_PROGRESS_PROP", relativePath));
  if (RUNTIME_OWNER_PATHS.has(relativePath) && !/\bdispose\s*\(/.test(content)) violations.push(violation("SOURCE_GUARD_MISSING_DISPOSE", relativePath));
  if (relativePath.endsWith("MahoBrowserStage.tsx") && !/return\s*\(\)\s*=>\s*runtime\.dispose\s*\(\)/.test(content)) violations.push(violation("SOURCE_GUARD_MISSING_RUNTIME_CLEANUP", relativePath));
  return violations;
}

function artifactPath(evidenceDir, name) { return path.join(evidenceDir, name); }

export function runSourceGuards({ evidenceDir, changedFileListPath, workspaceRoot }) {
  if (!evidenceDir || !workspaceRoot) throw new EvidenceError("SOURCE_GUARD_INPUT_REQUIRED", "evidence and workspace root are required");
  if (!fs.existsSync(evidenceDir) || !fs.statSync(evidenceDir).isDirectory()) throw new EvidenceError("EVIDENCE_DIRECTORY_MISSING", evidenceDir);
  const paths = listedPaths(changedFileListPath, "CHANGED_FILE_LIST");
  const sourcePaths = paths.filter((entry) => SOURCE_EXTENSION.test(entry) && !entry.includes("/__tests__/"));
  if (!sourcePaths.length) throw new EvidenceError("SOURCE_GUARD_SOURCE_LIST_EMPTY", "no TS/TSX/Astro/MJS source paths supplied");
  const violations = [];
  for (const relativePath of sourcePaths) {
    if (!relativePath.startsWith("website/")) throw new EvidenceError("SOURCE_GUARD_PATH_OUTSIDE_WEBSITE", relativePath);
    const absolutePath = sourcePath(workspaceRoot, relativePath);
    if (!fs.existsSync(absolutePath) || !fs.statSync(absolutePath).isFile()) throw new EvidenceError("SOURCE_GUARD_SOURCE_MISSING", relativePath);
    violations.push(...sourceViolations(relativePath, fs.readFileSync(absolutePath, "utf8")));
  }
  if (violations.length) throw new EvidenceError("SOURCE_GUARD_FAILED", violations.sort().join("\n"));
  const result = { status: "passed", files: sourcePaths.sort() };
  fs.writeFileSync(artifactPath(evidenceDir, "final-f2-quality.json"), `${JSON.stringify(result, null, 2)}\n`);
  return result;
}

function parseDirtyHashes(filePath) {
  const content = readRequiredText(filePath, "DIRTY_HASHES_JSON");
  let raw;
  try { raw = JSON.parse(content); } catch (error) { throw new EvidenceError("DIRTY_HASHES_JSON_INVALID", error instanceof Error ? error.message : "invalid JSON"); }
  if (raw === null || typeof raw !== "object" || Array.isArray(raw)) throw new EvidenceError("DIRTY_HASHES_JSON_INVALID", "must be an object");
  const hashes = new Map();
  for (const [filePathKey, hash] of Object.entries(raw)) {
    if (typeof hash !== "string" || !/^[a-f0-9]{64}$/i.test(hash) || filePathKey.endsWith("/") || path.isAbsolute(filePathKey)) throw new EvidenceError("DIRTY_HASHES_JSON_INVALID", filePathKey);
    hashes.set(filePathKey, hash);
  }
  return hashes;
}

function parseStatus(content, code) {
  const records = [];
  for (const line of content.split("\n").filter(Boolean)) {
    const match = line.match(/^(.)(.) (.+)$/);
    if (!match) throw new EvidenceError(`${code}_INVALID`, line);
    const [, indexStatus, worktreeStatus, filePath] = match;
    if (filePath.includes(" -> ") || path.isAbsolute(filePath)) throw new EvidenceError(`${code}_INVALID`, line);
    records.push(Object.freeze({ indexStatus, worktreeStatus, filePath }));
  }
  return records;
}

function sha256File(filePath) { return crypto.createHash("sha256").update(fs.readFileSync(filePath)).digest("hex"); }
function isAllowlisted(filePath) { return SCOPE_ALLOWLIST.some((prefix) => filePath === prefix || filePath.startsWith(prefix)); }
function forbiddenPath(filePath) { return /^(?:maho-chromium|chromium|maho\/(?:crates|ios-shell|android-shell|relay|web-ai)|mail\/|\.git\/)/.test(filePath); }

export function runScopeCheck({ evidenceDir, baselinePath, dirtyHashesPath, finalStatusPath, changedFileListPath, workspaceRoot }) {
  for (const [name, value] of Object.entries({ evidenceDir, baselinePath, dirtyHashesPath, finalStatusPath, changedFileListPath, workspaceRoot })) if (!value) throw new EvidenceError("SCOPE_INPUT_REQUIRED", name);
  if (!fs.existsSync(evidenceDir) || !fs.statSync(evidenceDir).isDirectory()) throw new EvidenceError("EVIDENCE_DIRECTORY_MISSING", evidenceDir);
  const baselineRecords = parseStatus(readRequiredText(baselinePath, "BASELINE_STATUS"), "BASELINE_STATUS");
  const finalRecords = parseStatus(readRequiredText(finalStatusPath, "FINAL_STATUS"), "FINAL_STATUS");
  const dirtyHashes = parseDirtyHashes(dirtyHashesPath);
  const changedPaths = listedPaths(changedFileListPath, "CHANGED_FILE_LIST");
  for (const record of finalRecords) {
    if (record.indexStatus !== " " && record.indexStatus !== "?") throw new EvidenceError("STAGED_FILE_PRESENT", record.filePath);
    if (record.indexStatus === "D" || record.worktreeStatus === "D") throw new EvidenceError("SCOPE_DELETE_PRESENT", record.filePath);
  }
  const baselinePaths = new Set(baselineRecords.map(({ filePath }) => filePath));
  for (const [relativePath, expectedHash] of dirtyHashes) {
    const currentPath = sourcePath(workspaceRoot, relativePath);
    if (!fs.existsSync(currentPath) || !fs.statSync(currentPath).isFile()) throw new EvidenceError("BASELINE_FILE_MISSING", relativePath);
    if (sha256File(currentPath) !== expectedHash) throw new EvidenceError("BASELINE_HASH_DRIFT", relativePath);
  }
  for (const relativePath of changedPaths) {
    if (forbiddenPath(relativePath)) throw new EvidenceError("SCOPE_PATH_FORBIDDEN", relativePath);
    if (baselinePaths.has(relativePath)) continue;
    if (!isAllowlisted(relativePath)) throw new EvidenceError("SCOPE_PATH_FORBIDDEN", relativePath);
  }
  const changedSet = new Set(changedPaths);
  for (const record of finalRecords) {
    if (record.filePath.startsWith("website/") && SOURCE_EXTENSION.test(record.filePath) && !changedSet.has(record.filePath)) {
      throw new EvidenceError("UNTRACKED_CHANGED_SOURCE_MISSING", record.filePath);
    }
  }
  const result = { status: "passed", changedPaths: changedPaths.sort(), baselinePaths: [...baselinePaths].sort() };
  fs.writeFileSync(artifactPath(evidenceDir, "final-f4-scope.json"), `${JSON.stringify(result, null, 2)}\n`);
  return result;
}
