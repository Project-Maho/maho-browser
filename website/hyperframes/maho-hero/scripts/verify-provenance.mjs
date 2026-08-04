import { createHash } from "node:crypto";
import { existsSync, readFileSync, statSync } from "node:fs";
import { readdirSync } from "node:fs";
import { dirname, relative, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { spawnSync } from "node:child_process";

const scriptDirectory = dirname(fileURLToPath(import.meta.url));
const projectRoot = resolve(scriptDirectory, "..");
const defaultManifestPath = resolve(projectRoot, "source-manifest.json");
const taskEvidenceDirectory = resolve(projectRoot, "../../../.omo/evidence/maho-hyperframes-hero-production/task-2");
const narrowNoBreakSpace = "\u202f";
const sourceExtensions = new Set([".css", ".html", ".js", ".mjs", ".cjs"]);
const ignoredDirectories = new Set([
  ".git",
  ".hf-validate",
  ".transcode-cache",
  "assets",
  "node_modules",
  "render-scratch",
]);
const requiredIgnoreRules = [
  "/assets/originals/",
  "/.hf-validate/",
  "/.transcode-cache/",
  "/render-scratch/",
  "/node_modules/",
];
const expectedHashes = new Map([
  ["spaces", "9a870e7d3e85c9774c4fd05cd6a1217a7dd09ff4a8b6da2be74719e21de92f8d"],
  ["split-view", "33d0ef70beec495400439a0fdb97cca8de29790253b4f218aad97ea182768fbc"],
  ["command-ai", "9b985f895753d064b38921c96d60bd7f67a5c7f865ed7a36790c60369cadbbb7"],
  ["water-lilies", "a258814b929382e989bbacf27b0f4f1d9a9442d126e407b56a6355a7a9984556"],
]);

function assert(condition, message) {
  if (!condition) {
    throw new Error(message);
  }
}

function usageError(message) {
  throw new Error(`${message}\nUsage: node scripts/verify-provenance.mjs [--manifest <file> | --scan-root <project-root-or-task-2-evidence-directory>]`);
}

function parseArguments(argumentsList) {
  if (argumentsList.length === 0) {
    return { manifestPath: defaultManifestPath, scanRoot: projectRoot };
  }
  if (argumentsList.length === 1 && (argumentsList[0] === "--manifest" || argumentsList[0] === "--scan-root")) {
    usageError(`${argumentsList[0]} requires a path value`);
  }
  if (argumentsList.length !== 2) {
    usageError("expected exactly one supported option and its value");
  }

  const [option, argument] = argumentsList;
  if (option !== "--manifest" && option !== "--scan-root") {
    usageError(`unsupported option: ${option}`);
  }
  if (argument.length === 0 || argument.startsWith("--")) {
    usageError(`${option} requires a path value`);
  }

  const resolvedPath = resolve(process.cwd(), argument);
  assert(existsSync(resolvedPath), `${option} path does not exist: ${resolvedPath}`);
  if (option === "--manifest") {
    assert(statSync(resolvedPath).isFile(), `--manifest path is not a file: ${resolvedPath}`);
    return { manifestPath: resolvedPath, scanRoot: projectRoot };
  }

  assert(statSync(resolvedPath).isDirectory(), `--scan-root path is not a directory: ${resolvedPath}`);
  assert(
    resolvedPath === projectRoot || resolvedPath === taskEvidenceDirectory,
    `--scan-root must be the project root or Todo 2 evidence directory: ${resolvedPath}`,
  );
  return { manifestPath: defaultManifestPath, scanRoot: resolvedPath };
}

function asRecord(value, name) {
  assert(value !== null && typeof value === "object" && !Array.isArray(value), `${name} must be an object`);
  return value;
}

function sha256(filePath) {
  return createHash("sha256").update(readFileSync(filePath)).digest("hex");
}

function mediaProbe(filePath) {
  const result = spawnSync("ffprobe", ["-v", "error", "-of", "json", "-show_format", "-show_streams", filePath], {
    encoding: "utf8",
  });
  assert(result.status === 0, `ffprobe failed for ${filePath}: ${result.stderr.trim()}`);
  return asRecord(JSON.parse(result.stdout), `ffprobe result for ${filePath}`);
}

function validateMedia(item) {
  const localPath = resolve(projectRoot, item.localPath);
  assert(relative(projectRoot, localPath).startsWith("assets/originals/"), `${item.id} localPath must stay under assets/originals`);
  assert(existsSync(localPath), `${item.id} local media is missing: ${item.localPath}`);
  assert(sha256(localPath) === item.sha256, `${item.id} local media SHA-256 differs from source-manifest.json`);

  const probe = mediaProbe(localPath);
  const streams = Array.isArray(probe.streams) ? probe.streams.map((stream) => asRecord(stream, `${item.id} stream`)) : [];
  const videoStreams = streams.filter((stream) => stream.codec_type === "video");
  const audioStreams = streams.filter((stream) => stream.codec_type === "audio");
  const video = videoStreams[0];
  assert(videoStreams.length === 1 && video !== undefined, `${item.id} must have exactly one video stream`);
  assert(video.codec_name === item.media.codec, `${item.id} codec differs from manifest`);
  assert(video.width === item.media.width && video.height === item.media.height, `${item.id} dimensions differ from manifest`);
  assert(video.avg_frame_rate === item.media.averageFrameRate, `${item.id} average frame rate differs from manifest`);
  assert(audioStreams.length === item.media.audioStreamCount, `${item.id} audio stream count differs from manifest`);

  const format = asRecord(probe.format, `${item.id} format`);
  const duration = Number(format.duration);
  assert(Number.isFinite(duration), `${item.id} format duration is invalid`);
  assert(Math.abs(duration - item.media.durationSeconds) < 0.00001, `${item.id} duration differs from manifest`);
}

function runtimeFiles(directory) {
  return readdirSync(directory, { withFileTypes: true }).flatMap((entry) => {
    const entryPath = resolve(directory, entry.name);
    if (entry.isDirectory()) {
      return ignoredDirectories.has(entry.name) ? [] : runtimeFiles(entryPath);
    }
    const extension = entry.name.slice(entry.name.lastIndexOf("."));
    return entry.isFile() && sourceExtensions.has(extension) ? [entryPath] : [];
  });
}

function remoteRuntimeUrls(files) {
  return files.flatMap((filePath) => {
    const urls = readFileSync(filePath, "utf8").match(/https?:\/\/[^\s"'<>`]+/g) ?? [];
    return urls.map((url) => `${relative(projectRoot, filePath)}: ${url}`);
  });
}

function validateIgnoreRules(manifest) {
  const ignoreFile = readFileSync(resolve(projectRoot, ".gitignore"), "utf8");
  for (const rule of requiredIgnoreRules) {
    assert(ignoreFile.includes(rule), `.gitignore is missing ${rule}`);
  }
  for (const item of [...manifest.sources, manifest.background]) {
    const result = spawnSync("git", ["check-ignore", "--quiet", "--", item.localPath], {
      cwd: projectRoot,
      encoding: "utf8",
    });
    assert(result.status === 0, `${item.localPath} is not project-locally ignored`);
  }
}

function run(argumentsList) {
  const { manifestPath, scanRoot } = parseArguments(argumentsList);
  const manifest = asRecord(JSON.parse(readFileSync(manifestPath, "utf8")), "source-manifest.json");
  assert(manifest.schemaVersion === 1, "source-manifest.json schemaVersion must be 1");
  assert(Array.isArray(manifest.sources) && manifest.sources.length === 3, "source-manifest.json must contain exactly three sources");
  const entries = [...manifest.sources, asRecord(manifest.background, "background")].map((item) => asRecord(item, "manifest entry"));
  assert(new Set(entries.map((item) => item.id)).size === entries.length, "source-manifest.json IDs must be unique");

  for (const item of entries) {
    assert(expectedHashes.get(item.id) === item.sha256, `${item.id} hash is not the immutable Todo 1 hash`);
    assert(typeof item.externalPath === "string" && typeof item.localPath === "string", `${item.id} paths must be strings`);
    if (item.id !== "water-lilies") {
      assert(item.externalPath.includes(narrowNoBreakSpace), `${item.id} externalPath must preserve U+202F`);
      assert(!existsSync(item.externalPath.replaceAll(narrowNoBreakSpace, " ")), `${item.id} regular-space filename must not be accepted`);
    }
    asRecord(item.media, `${item.id} media`);
    validateMedia(item);
  }

  validateIgnoreRules(manifest);
  const violations = remoteRuntimeUrls(runtimeFiles(scanRoot));
  assert(violations.length === 0, `remote runtime URLs are forbidden:\n${violations.join("\n")}`);
  assert(existsSync(resolve(projectRoot, "node_modules/gsap/dist/gsap.min.js")), "local pinned GSAP runtime is missing");
  console.log("provenance verification passed");
}

try {
  run(process.argv.slice(2));
} catch (error) {
  const message = error instanceof Error ? error.message : String(error);
  console.error(`provenance verification failed: ${message}`);
  process.exitCode = 1;
}
