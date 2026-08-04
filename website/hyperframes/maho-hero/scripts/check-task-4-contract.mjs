import { existsSync, readFileSync, readdirSync } from "node:fs";
import { dirname, extname, relative, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import vm from "node:vm";
import { runRuntimeControls } from "./task-4-runtime-controls.mjs";

const scriptDirectory = dirname(fileURLToPath(import.meta.url));
const projectRoot = resolve(scriptDirectory, "..");
const sharedRoot = resolve(projectRoot, "shared");
const contractPath = resolve(sharedRoot, "frame-contract.json");
const cssPath = resolve(sharedRoot, "frame.css");
const runtimePath = resolve(sharedRoot, "frame.js");
const compositionsRoot = resolve(projectRoot, "compositions");
const fixtureRoot = resolve(scriptDirectory, "fixtures/task-4");

const expected = Object.freeze({
  compositionIds: ["spaces", "split-view", "command-ai"],
  canvas: { width: 1920, height: 1080, fps: 30 },
  source: { width: 2818, height: 2060 },
  crop: { x: 68, y: 52, width: 2682, height: 1924 },
  browser: { width: 1440, height: 1033, left: 240, top: 23.5, right: 1680, bottom: 1056.5, centerX: 960, centerY: 540 },
  background: "../assets/originals/water-lilies.jpg",
  gsap: "../node_modules/gsap/dist/gsap.min.js",
  radius: 17,
});

function assert(condition, message) {
  if (!condition) throw new Error(message);
}

function readRequired(filePath) {
  assert(existsSync(filePath), `missing required Task 4 file: ${relative(projectRoot, filePath)}`);
  return readFileSync(filePath, "utf8");
}

function countMatches(source, pattern) {
  return [...source.matchAll(pattern)].length;
}

function assertNoRemoteOrNondeterministicSource(source, scope) {
  const forbidden = [
    [/https?:\/\//, "remote URL"],
    [/\bDate(?:\.now|\s*\()/, "Date clock"],
    [/\bperformance\.now\s*\(/, "performance clock"],
    [/\bMath\.random\s*\(/, "unseeded randomness"],
    [/\b(?:fetch|XMLHttpRequest|WebSocket|EventSource)\b/, "runtime network API"],
    [/\b(?:setTimeout|setInterval|requestAnimationFrame)\s*\(/, "wall-clock scheduling"],
  ];
  for (const [pattern, label] of forbidden) {
    assert(!pattern.test(source), `${scope} contains forbidden ${label}`);
  }
}

function validateContractData(contract) {
  assert(JSON.stringify(contract.compositionIds) === JSON.stringify(expected.compositionIds), "composition IDs differ from the three approved entries");
  for (const section of ["canvas", "source", "crop", "browser"]) {
    for (const [key, value] of Object.entries(expected[section])) {
      assert(contract[section]?.[key] === value, `${section}.${key} must equal ${value}`);
    }
  }
  assert(contract.assets?.background === expected.background, "Water Lilies path must be local and composition-relative");
  assert(contract.assets?.gsap === expected.gsap, "GSAP path must use the pinned local runtime");
  assert(contract.surface?.radius === expected.radius, "browser squircle radius must match the approved Maho lg token");
  assert(typeof contract.surface?.edge === "string" && contract.surface.edge.length > 0, "browser edge token is missing");
  assert(typeof contract.surface?.shadow === "string" && contract.surface.shadow.length > 0, "browser shadow token is missing");
  const renderedVideo = contract.renderedVideo;
  assert(Math.abs(renderedVideo.width - (contract.source.width * contract.browser.width) / contract.crop.width) < 0.000001, "rendered video width does not derive from the approved crop");
  assert(Math.abs(renderedVideo.height - (contract.source.height * contract.browser.height) / contract.crop.height) < 0.000001, "rendered video height does not derive from the approved crop");
  assert(Math.abs(renderedVideo.left + (contract.crop.x * contract.browser.width) / contract.crop.width) < 0.000001, "rendered video left offset does not derive from crop.x");
  assert(Math.abs(renderedVideo.top + (contract.crop.y * contract.browser.height) / contract.crop.height) < 0.000001, "rendered video top offset does not derive from crop.y");
}

function validateCss(css, contract) {
  const required = [
    `--maho-canvas-width: ${contract.canvas.width}px`,
    `--maho-canvas-height: ${contract.canvas.height}px`,
    `--maho-source-width: ${contract.source.width}px`,
    `--maho-source-height: ${contract.source.height}px`,
    `--maho-crop-x: ${contract.crop.x}px`,
    `--maho-crop-y: ${contract.crop.y}px`,
    `--maho-crop-width: ${contract.crop.width}px`,
    `--maho-crop-height: ${contract.crop.height}px`,
    `--maho-browser-width: ${contract.browser.width}px`,
    `--maho-browser-height: ${contract.browser.height}px`,
    `--maho-browser-left: ${contract.browser.left}px`,
    `--maho-browser-top: ${contract.browser.top}px`,
    `--maho-browser-radius: ${contract.surface.radius}px`,
    `--maho-browser-edge: ${contract.surface.edge}`,
    `--maho-browser-shadow: ${contract.surface.shadow}`,
    `--maho-video-width: ${contract.renderedVideo.width}px`,
    `--maho-video-height: ${contract.renderedVideo.height}px`,
    `--maho-video-left: ${contract.renderedVideo.left}px`,
    `--maho-video-top: ${contract.renderedVideo.top}px`,
    ".maho-background-layer",
    "position: absolute",
    "inset: 0",
    ".maho-browser-camera",
    "will-change: transform, opacity",
    ".maho-browser-mask",
    "corner-shape: superellipse(1)",
    ".maho-source-video",
  ];
  for (const value of required) assert(css.includes(value), `frame.css is missing: ${value}`);
  const rootRule = css.match(/\.maho-frame\s*\{([^}]*)\}/s)?.[1] ?? "";
  assert(!/\bbackground(?:-image|-color)?\s*:/.test(rootRule), "composition root must not own a background");
  assert(!/\b(?:animation|transition)\s*:/.test(css), "shared frame CSS must not add wall-clock motion");
}

function loadRuntime(runtimeSource) {
  const context = { console };
  context.globalThis = context;
  vm.runInNewContext(runtimeSource, context, { filename: runtimePath });
  return context.MahoHeroFrame;
}

async function validateRuntime(runtimeSource, contract) {
  assertNoRemoteOrNondeterministicSource(runtimeSource, "shared/frame.js");
  assert(!/\b(?:width|height|top|left|right|bottom)\s*:/.test(runtimeSource.match(/const cameraKeys[\s\S]*?;/)?.[0] ?? ""), "camera allowlist contains geometry properties");
  const runtime = loadRuntime(runtimeSource);
  assert(runtime !== undefined, "frame.js must expose globalThis.MahoHeroFrame");
  assert(Object.isFrozen(runtime) && Object.isFrozen(runtime.contract), "runtime contract and helper surface must be frozen");
  assert(JSON.stringify(runtime.contract) === JSON.stringify(contract), "runtime constants differ from frame-contract.json");
  assert(typeof runtime.registerCameraTimeline === "function", "timeline registration helper is missing");
  assert(typeof runtime.assetPath === "function", "local asset helper is missing");

  return runRuntimeControls(runtime);
}

function validateEntrySource(source, scope) {
  assertNoRemoteOrNondeterministicSource(source, scope);
  const entryForbidden = [
    [/\basync\b/, "async timeline construction"],
    [/\bPromise\b|\.then\s*\(/, "Promise timeline construction"],
    [/\bqueueMicrotask\s*\(/, "microtask timeline construction"],
    [/\b(?:addEventListener|on[a-z]+\s*=)/i, "event-driven timeline construction"],
    [/\.(?:play|pause)\s*\(/, "play/pause control"],
  ];
  for (const [pattern, label] of entryForbidden) assert(!pattern.test(source), `${scope} contains forbidden ${label}`);
  const id = source.match(/data-composition-id=["']([^"']+)["']/)?.[1];
  assert(expected.compositionIds.includes(id), `${scope} has unsupported composition ID`);
  for (const [attribute, value] of [["data-width", "1920"], ["data-height", "1080"], ["data-fps", "30"]]) {
    assert(new RegExp(`${attribute}=["']${value}["']`).test(source), `${scope} is missing ${attribute}=${value}`);
  }
  assert(/class=["'][^"']*\bmaho-frame\b/.test(source), `${scope} must use the shared frame root class`);
  assert(/<link\b[^>]*href=["']\.\.\/shared\/frame\.css["'][^>]*>/s.test(source), `${scope} must consume shared/frame.css`);
  assert(/<script\b[^>]*src=["']\.\.\/node_modules\/gsap\/dist\/gsap\.min\.js["'][^>]*><\/script>/s.test(source), `${scope} must consume the pinned local GSAP runtime`);
  assert(/<script\b[^>]*src=["']\.\.\/shared\/frame\.js["'][^>]*><\/script>/s.test(source), `${scope} must consume shared/frame.js`);
  const backgroundPattern = /<img\b[^>]*class=["'][^"']*\bmaho-background-layer\b[^"']*\bmaho-background-image\b[^"']*["'][^>]*data-maho-background\b[^>]*src=["']\.\.\/assets\/originals\/water-lilies\.jpg["'][^>]*>/s;
  assert(backgroundPattern.test(source), `${scope} must declare the full-bleed Water Lilies child image`);
  const rootTag = source.match(/<div\b[^>]*data-composition-id[^>]*>/s)?.[0] ?? "";
  const rootTagEnd = source.indexOf(rootTag) + rootTag.length;
  assert(rootTag.length > 0 && source.slice(rootTagEnd).trimStart().startsWith("<img"), `${scope} Water Lilies must be the root's explicit first child`);
  assert(!/\b(?:style|class)=["'][^"']*background/i.test(rootTag), `${scope} places background semantics on the root`);
  const wrapperTag = source.match(new RegExp(`<div\\b[^>]*data-maho-camera-wrapper=["']${id}["'][^>]*>`, "s"))?.[0] ?? "";
  assert(/class=["'][^"']*\bmaho-browser-camera\b/.test(wrapperTag), `${scope} must declare the camera wrapper class`);
  assert(!/data-(?:start|duration|track-index)=/.test(wrapperTag), `${scope} camera wrapper must remain untimed`);
  assert(new RegExp(`<video\\b[^>]*class=["'][^"']*\\bmaho-source-video\\b[^"']*["'][^>]*src=["']${expectedVideoPath(id).replaceAll(".", "\\.")}["'][^>]*>`, "s").test(source), `${scope} must use the approved local source video`);
  assert(countMatches(source, /registerCameraTimeline\s*\(/g) === 1, `${scope} must register exactly one timeline through the helper`);
  assert(!/\b(?:const|let|var)\s+\w+\s*=\s*MahoHeroFrame\.registerCameraTimeline/.test(source), `${scope} must not capture the timeline registration result`);
  assert(!/\bgsap\.(?:timeline|to|from|fromTo|set)\s*\(/.test(source), `${scope} must not bypass the shared timeline helper`);
  assert(!/window\.__timelines/.test(source), `${scope} must not register timelines directly`);
  assert(!/\b(?:width|height|top|left|right|bottom)\s*:/.test(source.match(/registerCameraTimeline[\s\S]*/)?.[0] ?? ""), `${scope} camera tween contains forbidden geometry property`);
  validateEntryTweenObjects(source, scope);
}

function validateEntryTweenObjects(source, scope) {
  const hasMotion = (vars) => /\b(?:x|y|scale|opacity)\s*:/.test(vars);
  for (const match of source.matchAll(/camera\.to\s*\(\s*\{([^}]*)\}/gs)) {
    assert(hasMotion(match[1]), `${scope} camera.to requires at least one camera property`);
  }
  for (const match of source.matchAll(/camera\.fromTo\s*\(\s*\{([^}]*)\}\s*,\s*\{([^}]*)\}/gs)) {
    assert(hasMotion(match[1]) && hasMotion(match[2]), `${scope} camera.fromTo vars each require at least one camera property`);
  }
}

function compositionFiles() {
  if (!existsSync(compositionsRoot)) return [];
  return readdirSync(compositionsRoot, { withFileTypes: true })
    .filter((entry) => entry.isFile() && extname(entry.name) === ".html")
    .map((entry) => resolve(compositionsRoot, entry.name));
}

function negativeFixtures() {
  return readdirSync(fixtureRoot)
    .filter((name) => name.endsWith(".fixture.txt"))
    .sort()
    .map((name) => resolve(fixtureRoot, name));
}

function expectedVideoPath(compositionId) {
  return `../assets/originals/${compositionId}.mov`;
}

async function run(argumentsList) {
  const contractSource = readRequired(contractPath);
  const contract = JSON.parse(contractSource);
  const css = readRequired(cssPath);
  const runtimeSource = readRequired(runtimePath);
  assertNoRemoteOrNondeterministicSource(contractSource, "shared/frame-contract.json");
  assertNoRemoteOrNondeterministicSource(css, "shared/frame.css");
  validateContractData(contract);
  validateCss(css, contract);
  const runtimeControls = await validateRuntime(runtimeSource, contract);
  const entries = compositionFiles();
  for (const filePath of entries) validateEntrySource(readFileSync(filePath, "utf8"), relative(projectRoot, filePath));
  if (argumentsList.includes("--foundation-only")) assert(entries.length === 0, "Todo 4 must not create composition HTML");

  if (argumentsList.includes("--negative-fixtures")) {
    const rejected = negativeFixtures().map((filePath) => {
      const source = readRequired(filePath);
      let error = "";
      try { validateEntrySource(source, relative(projectRoot, filePath)); } catch (caught) { error = caught instanceof Error ? caught.message : String(caught); }
      assert(error.length > 0, `${relative(projectRoot, filePath)} unexpectedly passed`);
      return { fixture: relative(projectRoot, filePath), error };
    });
    return { result: "PASS", mode: "negative-fixtures", runtimeControls, rejected };
  }

  const fixtureIndex = argumentsList.indexOf("--fixture");
  if (fixtureIndex >= 0) {
    const fixtureArgument = argumentsList[fixtureIndex + 1];
    assert(typeof fixtureArgument === "string", "--fixture requires a path");
    const fixturePath = resolve(process.cwd(), fixtureArgument);
    validateEntrySource(readRequired(fixturePath), relative(projectRoot, fixturePath));
    return { result: "PASS", mode: "fixture", fixture: relative(projectRoot, fixturePath), runtimeControls };
  }

  return { result: "PASS", mode: "contract", foundationOnly: argumentsList.includes("--foundation-only"), sharedFiles: 3, compositionEntriesInspected: entries.length, compositionIds: expected.compositionIds, runtimeControls };
}

try {
  console.log(JSON.stringify(await run(process.argv.slice(2)), null, 2));
} catch (error) {
  console.error(JSON.stringify({ result: "FAIL", error: error instanceof Error ? error.message : String(error) }, null, 2));
  process.exitCode = 1;
}
