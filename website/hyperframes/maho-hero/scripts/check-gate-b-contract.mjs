import { createHash } from "node:crypto";
import { existsSync, readFileSync, readdirSync, statSync } from "node:fs";
import { dirname, extname, relative, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const scriptDirectory = dirname(fileURLToPath(import.meta.url));
const projectRoot = resolve(scriptDirectory, "..");
const reviewRoot = resolve(projectRoot, "review/gate-b");
const defaultStoryboard = resolve(projectRoot, "STORYBOARD.md");
const frameContract = resolve(projectRoot, "frame.md");
const checksumFile = resolve(reviewRoot, "SHA256SUMS");

const scenes = [
  {
    title: "Spaces",
    src: "compositions/spaces.html",
    id: "spaces",
    duration: "12.733333s",
    camera: "sidebar drift to approximately 1.10x during Space changes; settle near 1.03x at final state",
    stills: ["spaces-0000ms-source.png", "spaces-10000ms-source.png", "spaces-12600ms-source.png"],
    contactSheet: "spaces-contact-sheet.png",
    contactDimensions: [2112, 595],
  },
  {
    title: "Split View",
    src: "compositions/split-view.html",
    id: "split-view",
    duration: "6.668333s",
    camera: "push to recorded drag origin/drop region; zoom back to the complete split result",
    stills: ["split-view-0000ms-source.png", "split-view-3300ms-source.png", "split-view-6600ms-source.png"],
    contactSheet: "split-view-contact-sheet.png",
    contactDimensions: [2112, 595],
  },
  {
    title: "Command Bar + AI",
    src: "compositions/command-ai.html",
    id: "command-ai",
    duration: "26.733333s",
    camera: "push near 1.09x around Command Bar; pan focus right when AI Panel opens while retaining browser context; finish near 1.02x",
    stills: [
      "command-ai-0000ms-source.png",
      "command-ai-6500ms-source.png",
      "command-ai-20000ms-source.png",
      "command-ai-26600ms-source.png",
    ],
    contactSheet: "command-ai-contact-sheet.png",
    contactDimensions: [2816, 595],
  },
];

function assert(condition, message) {
  if (!condition) throw new Error(message);
}

function parseArguments(argumentsList) {
  if (argumentsList.length === 0) return defaultStoryboard;
  assert(argumentsList.length === 2 && argumentsList[0] === "--storyboard", "usage: node scripts/check-gate-b-contract.mjs [--storyboard <file>]");
  const storyboardPath = resolve(process.cwd(), argumentsList[1]);
  assert(existsSync(storyboardPath), `storyboard fixture does not exist: ${storyboardPath}`);
  return storyboardPath;
}

function frameBlocks(storyboard) {
  const matches = [...storyboard.matchAll(/^## Frame \d+ — .+$/gm)];
  assert(matches.length === 3, `expected exactly three Frame blocks, found ${matches.length}`);
  return matches.map((match, index) => storyboard.slice(match.index, matches[index + 1]?.index));
}

function assertIncludes(text, requiredValues, scope) {
  for (const value of requiredValues) assert(text.includes(value), `${scope} is missing: ${value}`);
}

function validateStoryboard(storyboard) {
  const blocks = frameBlocks(storyboard);
  for (const [index, scene] of scenes.entries()) {
    const block = blocks[index];
    assert(block.startsWith(`## Frame ${index + 1} — ${scene.title}`), `Frame ${index + 1} must be ${scene.title}`);
    assertIncludes(block, [
      `- status: outline`,
      `- src: ${scene.src}`,
      `- source_id: ${scene.id}`,
      `- source_duration: ${scene.duration} locked full duration`,
      `- playback: source time 0.000s at 1x for the complete locked duration`,
      `- external_background_t0: Water Lilies external child background visible at t=0`,
      `- source_crop: x=68 y=52 width=2682 height=1924`,
      `- browser_object: 1440x1033 centered on the 1920x1080 canvas`,
      scene.camera,
      `- rendered_transition: none; no cross-scene rendered transition`,
      `**Visual purpose.**`,
      `**Representative checkpoints.**`,
      `**HyperFrames rule citations.**`,
    ], scene.title);
  }
  const plannedSources = [...storyboard.matchAll(/^- src: (.+)$/gm)].map((match) => match[1]);
  assert(JSON.stringify(plannedSources) === JSON.stringify(scenes.map((scene) => scene.src)), "unauthorized master-video or cross-scene frame source detected");
}

function validateFrameContract() {
  const contract = readFileSync(frameContract, "utf8");
  assertIncludes(contract, [
    "Approval: pending.",
    "Canvas: `1920x1080`",
    "visible at `t=0` in every independent output",
    "`x=68 y=52 width=2682 height=1924`",
    "fixed `1440x1033`, geometrically centered",
    "No master video, cross-scene frame, or rendered cross-scene transition is allowed.",
    "wrapper transforms only",
    "GATE B REVIEW ANNOTATION — NOT RUNTIME",
  ], "frame.md");
}

function pngDimensions(filePath) {
  const bytes = readFileSync(filePath);
  assert(bytes.length >= 24 && bytes.subarray(1, 4).toString("ascii") === "PNG", `${relative(projectRoot, filePath)} is not a PNG`);
  return [bytes.readUInt32BE(16), bytes.readUInt32BE(20)];
}

function sha256(filePath) {
  return createHash("sha256").update(readFileSync(filePath)).digest("hex");
}

function checksumEntries() {
  assert(existsSync(checksumFile), "review/gate-b/SHA256SUMS is missing");
  return new Map(readFileSync(checksumFile, "utf8").trim().split("\n").map((line) => {
    const match = line.match(/^([a-f0-9]{64})  (.+)$/);
    assert(match, `malformed SHA256SUMS line: ${line}`);
    return [match[2], match[1]];
  }));
}

function validateReviewPacket() {
  const checksums = checksumEntries();
  const reviewReadmePath = resolve(reviewRoot, "README.md");
  assert(existsSync(reviewReadmePath), "review/gate-b/README.md is missing");
  const reviewReadme = readFileSync(reviewReadmePath, "utf8");
  assertIncludes(reviewReadme, [
    "Gate B approval is pending.",
    "any requested frame, camera, crop, checkpoint, or source change invalidates this packet",
  ], "review/gate-b/README.md");
  const reviewedFiles = [resolve(projectRoot, "STORYBOARD.md"), frameContract];
  for (const scene of scenes) {
    for (const still of scene.stills) {
      const stillPath = resolve(reviewRoot, "stills", still);
      assert(existsSync(stillPath), `missing source still: ${still}`);
      assert(JSON.stringify(pngDimensions(stillPath)) === JSON.stringify([2818, 2060]), `${still} must remain 2818x2060`);
      reviewedFiles.push(stillPath);
    }
    const sheetPath = resolve(reviewRoot, scene.contactSheet);
    assert(existsSync(sheetPath), `missing contact sheet: ${scene.contactSheet}`);
    assert(JSON.stringify(pngDimensions(sheetPath)) === JSON.stringify(scene.contactDimensions), `${scene.contactSheet} dimensions differ from contract`);
    reviewedFiles.push(sheetPath);
  }
  for (const filePath of reviewedFiles) {
    const packetPath = relative(projectRoot, filePath);
    const fileHash = sha256(filePath);
    assert(checksums.get(packetPath) === fileHash, `missing or stale checksum: ${packetPath}`);
    assert(reviewReadme.includes(fileHash), `review README is missing SHA-256: ${packetPath}`);
  }
}

function walk(directory) {
  if (!existsSync(directory)) return [];
  return readdirSync(directory, { withFileTypes: true }).flatMap((entry) => {
    const entryPath = resolve(directory, entry.name);
    return entry.isDirectory() ? walk(entryPath) : [entryPath];
  });
}

function validateBoundaries() {
  const compositionHtml = walk(resolve(projectRoot, "compositions")).filter((filePath) => extname(filePath) === ".html");
  assert(compositionHtml.length === 0, "composition HTML exists before Gate B approval");
  const finalOutputRoot = resolve(projectRoot, "../../public/demo/hero/hyperframes");
  assert(!existsSync(finalOutputRoot) || walk(finalOutputRoot).length === 0, "final MP4/poster/output exists before Gate B approval");
  const taskFiles = [defaultStoryboard, frameContract, resolve(projectRoot, "scripts/check-gate-b-contract.mjs"), ...walk(reviewRoot).filter((filePath) => extname(filePath) === ".md")];
  const remoteUrls = taskFiles.flatMap((filePath) => (readFileSync(filePath, "utf8").match(/https?:\/\/[^\s)`]+/g) ?? []).map((url) => `${relative(projectRoot, filePath)}: ${url}`));
  assert(remoteUrls.length === 0, `runtime remote URL introduced:\n${remoteUrls.join("\n")}`);
}

function run(argumentsList) {
  const storyboardPath = parseArguments(argumentsList);
  assert(statSync(storyboardPath).isFile(), "storyboard path must be a file");
  validateStoryboard(readFileSync(storyboardPath, "utf8"));
  validateFrameContract();
  validateReviewPacket();
  validateBoundaries();
  return {
    result: "PASS",
    storyboard: relative(projectRoot, storyboardPath),
    frameBlocks: scenes.length,
    sourceStills: scenes.reduce((count, scene) => count + scene.stills.length, 0),
    contactSheets: scenes.length,
    gateBApproval: "pending",
    compositionHtmlExists: false,
  };
}

try {
  console.log(JSON.stringify(run(process.argv.slice(2)), null, 2));
} catch (error) {
  const message = error instanceof Error ? error.message : String(error);
  console.error(JSON.stringify({ result: "FAIL", error: message }, null, 2));
  process.exitCode = 1;
}
