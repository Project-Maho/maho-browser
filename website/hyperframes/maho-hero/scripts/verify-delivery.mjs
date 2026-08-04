import { createHash } from "node:crypto";
import { existsSync, readFileSync, readdirSync, statSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";

const scriptDirectory = dirname(fileURLToPath(import.meta.url));
const projectRoot = resolve(scriptDirectory, "..");
const workspaceRoot = resolve(projectRoot, "../../..");
const outputRoot = resolve(process.argv[2] ?? resolve(projectRoot, "../../public/demo/hero/hyperframes"));
const manifestPath = resolve(outputRoot, "output-manifest.json");
const sourceManifestPath = resolve(projectRoot, "source-manifest.json");
const gateCPath = resolve(workspaceRoot, ".omo/evidence/maho-hyperframes-hero-production/task-9/gate-c-authorization.json");
const gateDPath = resolve(workspaceRoot, ".omo/evidence/maho-hyperframes-hero-production/task-10/gate-d-render-authorization.json");
const task8SummaryPath = resolve(workspaceRoot, ".omo/evidence/maho-hyperframes-hero-production/task-8/evidence-summary.json");
const task8DoneClaimPath = resolve(workspaceRoot, ".omo/evidence/maho-hyperframes-hero-production/task-8/DoneClaim.json");

function assert(condition, message) {
  if (!condition) throw new Error(message);
}

function sha256(filePath) {
  return createHash("sha256").update(readFileSync(filePath)).digest("hex");
}

function json(filePath) {
  assert(existsSync(filePath), `missing required file: ${filePath}`);
  return JSON.parse(readFileSync(filePath, "utf8"));
}

function probe(filePath) {
  const result = spawnSync("ffprobe", [
    "-v", "error", "-show_entries", "format=duration:stream=codec_name,codec_type,width,height,avg_frame_rate", "-of", "json", filePath,
  ], { encoding: "utf8" });
  assert(result.status === 0, `ffprobe failed for ${filePath}: ${result.stderr}`);
  return JSON.parse(result.stdout);
}

function verifyGateHashes(manifest) {
  const gateC = json(gateCPath);
  const gateD = json(gateDPath);
  const task8SummaryHash = sha256(task8SummaryPath);
  assert(gateC.gate === "C" && gateD.gate === "D", "Gate C/D evidence records are invalid");
  assert(gateC.validatedInputs.task8.evidenceSummarySha256 === task8SummaryHash, "Task 8 validator summary hash is stale");
  assert(gateC.validatedInputs.task8.doneClaimSha256 === sha256(task8DoneClaimPath), "Task 8 DoneClaim hash is stale");
  assert(gateC.validatedInputs.shared.sourceManifestSha256 === sha256(sourceManifestPath), "source manifest hash is stale against Gate C");
  for (const [field, relativePath] of Object.entries({
    frameCssSha256: "shared/frame.css",
    frameJsSha256: "shared/frame.js",
    frameContractSha256: "shared/frame-contract.json",
  })) {
    assert(gateC.validatedInputs.shared[field] === sha256(resolve(projectRoot, relativePath)), `${field} is stale against Gate C`);
  }
  assert(gateD.gateCReceipt.endsWith("task-9/gate-c-authorization.json"), "Gate D is not bound to Gate C");
  for (const output of manifest.outputs) {
    const gateCEntry = gateC.validatedInputs.compositions.find((entry) => entry.id === output.id);
    assert(gateCEntry?.sha256 === output.compositionSha256, `Gate C composition hash mismatch for ${output.id}`);
    assert(gateD.gateCCompositionHashes[output.id] === output.compositionSha256, `Gate D composition hash mismatch for ${output.id}`);
    assert(sha256(resolve(workspaceRoot, output.composition)) === output.compositionSha256, `current composition hash is stale for ${output.id}`);
  }
}

function verifySourceHashes(manifest) {
  const sourceManifest = json(sourceManifestPath);
  for (const output of manifest.outputs) {
    const source = sourceManifest.sources.find((entry) => entry.id === output.id);
    assert(source, `source manifest has no ${output.id} source`);
    assert(sha256(resolve(projectRoot, source.localPath)) === source.sha256, `source hash changed for ${output.id}`);
  }
  assert(sha256(resolve(projectRoot, sourceManifest.background.localPath)) === sourceManifest.background.sha256, "Water Lilies hash changed");
}

function verifyDirectory(manifest) {
  const expectedFiles = new Set(["output-manifest.json", ...manifest.outputs.flatMap((output) => [output.mp4, output.poster])]);
  const actualFiles = readdirSync(outputRoot).filter((name) => statSync(resolve(outputRoot, name)).isFile()).sort();
  const forbidden = actualFiles.filter((name) => /(?:master|mobile)/i.test(name));
  assert(forbidden.length === 0, `forbidden master/mobile output: ${forbidden.join(", ")}`);
  assert(JSON.stringify(actualFiles) === JSON.stringify([...expectedFiles].sort()), `delivery directory must contain exactly the manifest and six approved assets: ${actualFiles.join(", ")}`);
}

function verifyOutputs(manifest) {
  const contract = manifest.deliveryContract;
  for (const output of manifest.outputs) {
    const videoPath = resolve(outputRoot, output.mp4);
    const posterPath = resolve(outputRoot, output.poster);
    assert(statSync(videoPath).size > 0 && statSync(posterPath).size > 0, `empty delivery asset for ${output.id}`);
    assert(sha256(videoPath) === output.mp4Sha256, `MP4 hash mismatch for ${output.id}`);
    assert(sha256(posterPath) === output.posterSha256, `poster hash mismatch for ${output.id}`);
    const video = probe(videoPath);
    const videoStreams = video.streams.filter((stream) => stream.codec_type === "video");
    const audioStreams = video.streams.filter((stream) => stream.codec_type === "audio");
    assert(videoStreams.length === 1 && audioStreams.length === 0, `MP4 must contain exactly one silent video stream for ${output.id}`);
    const stream = videoStreams[0];
    assert(stream.codec_name === contract.videoCodec, `MP4 codec must be ${contract.videoCodec} for ${output.id}`);
    assert(stream.width === contract.canvas.width && stream.height === contract.canvas.height, `MP4 dimensions must be 1920x1080 for ${output.id}`);
    assert(stream.avg_frame_rate === contract.fps, `MP4 fps must be ${contract.fps} for ${output.id}`);
    assert(Math.abs(Number(video.format.duration) - output.durationSeconds) <= contract.durationToleranceSeconds, `MP4 duration exceeds one frame tolerance for ${output.id}`);
    const poster = probe(posterPath);
    const posterStream = poster.streams.find((stream) => stream.codec_type === "video");
    assert(posterStream?.codec_name === contract.posterCodec, `poster codec must be ${contract.posterCodec} for ${output.id}`);
    assert(posterStream.width === contract.canvas.width && posterStream.height === contract.canvas.height, `poster dimensions must be 1920x1080 for ${output.id}`);
  }
}

try {
  assert(existsSync(outputRoot), `missing delivery directory: ${outputRoot}`);
  const forbidden = readdirSync(outputRoot).filter((name) => /(?:master|mobile)/i.test(name));
  assert(forbidden.length === 0, `forbidden master/mobile output: ${forbidden.join(", ")}`);
  const manifest = json(manifestPath);
  assert(manifest.outputs.length === 3, "manifest must contain exactly three independent outputs");
  verifyGateHashes(manifest);
  verifySourceHashes(manifest);
  verifyDirectory(manifest);
  verifyOutputs(manifest);
  console.log(JSON.stringify({ result: "PASS", outputRoot, outputs: manifest.outputs.map((output) => output.id) }, null, 2));
} catch (error) {
  console.error(JSON.stringify({ result: "FAIL", error: error instanceof Error ? error.message : String(error) }, null, 2));
  process.exitCode = 1;
}
