import { createHash } from "node:crypto";
import { existsSync, lstatSync, readFileSync, readdirSync, realpathSync, statSync } from "node:fs";
import { basename, extname, relative, resolve } from "node:path";

export const compositionIds = ["spaces", "split-view", "command-ai"];

export function assert(condition, message) {
  if (!condition) throw new Error(message);
}

export function sha256(filePath) {
  return createHash("sha256").update(readFileSync(filePath)).digest("hex");
}

export function projectFiles(directory) {
  return readdirSync(directory, { withFileTypes: true }).flatMap((entry) => {
    if (entry.name === "node_modules" || entry.name.startsWith(".")) return [];
    const filePath = resolve(directory, entry.name);
    if (entry.isDirectory()) return projectFiles(filePath);
    return entry.isFile() ? [filePath] : [];
  });
}

function rootTag(source) {
  const match = source.match(/<div\b[^>]*\bdata-composition-id=(['"])([^'"]+)\1[^>]*>/i);
  assert(match, "composition needs a div data-composition-id root");
  return match[0];
}

function rootClassSelectors(source, tag) {
  const classMatch = tag.match(/\bclass=(['"])([^'"]+)\1/i);
  if (!classMatch) return [];
  const classes = classMatch[2].split(/\s+/).filter(Boolean);
  const styleBlocks = source.match(/<style\b[^>]*>[\s\S]*?<\/style>/gi) ?? [];
  return classes.flatMap((className) =>
    styleBlocks.filter((block) => new RegExp(`(^|[},])\\s*\\.${className}(?:[\\s>+~.#[:]|\\{)`, "m").test(block)),
  );
}

export function verifyCompositionSource(filePath, expectedId) {
  const source = readFileSync(filePath, "utf8");
  const tag = rootTag(source);
  const actualId = tag.match(/\bdata-composition-id=(['"])([^'"]+)\1/i)?.[2];
  assert(actualId === expectedId, `${basename(filePath)} must declare data-composition-id="${expectedId}"`);
  assert(!source.includes("../"), `${basename(filePath)} contains parent traversal`);
  assert(!/(?:https?:)?\/\//i.test(source), `${basename(filePath)} contains a remote URL`);
  assert(!/<video\b/i.test(source), `${basename(filePath)} contains a video element`);
  assert(!/<img\b[^>]*(?:capture|screenshot|screen[-_ ]?shot)[^>]*>/i.test(source), `${basename(filePath)} contains a capture image`);
  assert(!/\b(?:tl|timeline)\.play\s*\(/i.test(source), `${basename(filePath)} plays a timeline`);
  assert(/gsap\.timeline\s*\(\s*\{\s*paused\s*:\s*true\s*}\s*\)/i.test(source), `${basename(filePath)} lacks a paused GSAP timeline`);
  assert(rootClassSelectors(source, tag).length === 0, `${basename(filePath)} styles its root through a class selector`);
  assert(/\bdata-start="0"/.test(tag), `${basename(filePath)} root must start at zero`);
  assert(/\bdata-width="1920"/.test(tag) && /\bdata-height="1080"/.test(tag), `${basename(filePath)} root must be 1920x1080`);
  assert(/\bnode_modules\/gsap\/dist\/gsap\.min\.js/.test(source), `${basename(filePath)} must load local GSAP`);
  return { id: expectedId, path: filePath, sha256: sha256(filePath) };
}

export function verifyEntryRoot(projectRoot, entryRoot, expectedId = process.env.MAHO_DOM_ENTRY_ID) {
  const absoluteEntry = resolve(entryRoot);
  const id = expectedId;
  assert(id && compositionIds.includes(id), "entry root must name a canonical composition");
  const canonical = resolve(projectRoot, "compositions", `${id}.html`);
  assert(existsSync(absoluteEntry), `entry root is missing: ${absoluteEntry}`);
  const canonicalStat = statSync(canonical);
  const entryStat = statSync(absoluteEntry);
  assert(canonicalStat.dev === entryStat.dev && canonicalStat.ino === entryStat.ino, "entry root must be a hard link to the canonical composition");
  assert(canonicalStat.nlink >= 2, "canonical composition must have a linked verification entry root");
  assert(sha256(canonical) === sha256(absoluteEntry), "entry root hash differs from canonical composition");
  return { id, canonical, inode: canonicalStat.ino, sha256: sha256(canonical) };
}

export function verifyLocalLinks(projectRoot, entryRoot) {
  for (const name of ["assets", "node_modules"]) {
    const linkPath = resolve(entryRoot, name);
    assert(lstatSync(linkPath).isSymbolicLink(), `${name} must be a read-only local symlink`);
    const target = realpathSync(linkPath);
    assert(relative(projectRoot, target) && !relative(projectRoot, target).startsWith(".."), `${name} symlink must stay within the project`);
  }
}

export function verifyProjectFiles(projectRoot) {
  const forbidden = projectFiles(projectRoot).filter((filePath) => {
    const name = basename(filePath).toLowerCase();
    return /(?:capture|screenshot|screen[-_ ]?shot)/.test(name) || [".mov", ".mp4", ".webm"].includes(extname(name));
  });
  assert(forbidden.length === 0, `forbidden capture or video source: ${forbidden.join(", ")}`);
}

export function verifyWaterLiliesAsset(projectRoot, assetPath) {
  const workspaceRoot = resolve(projectRoot, "../../..");
  const manifest = JSON.parse(readFileSync(resolve(projectRoot, "asset-manifest.json"), "utf8"));
  assert(manifest.schemaVersion === 1, "asset manifest schemaVersion must be 1");
  assert(Array.isArray(manifest.assets) && manifest.assets.length === 1, "asset manifest must allow exactly one visual asset");
  const asset = manifest.assets[0];
  assert(asset.id === "water-lilies", "Water Lilies must be the sole permitted visual asset");
  assert(asset.localPath === "assets/water-lilies.jpg", "Water Lilies must use the canonical local path");
  assert(asset.permittedUse === "external-browser-background-only", "Water Lilies may only be an external browser background");
  assert(
    JSON.stringify(asset.prohibitedUses) === JSON.stringify(["inner-browser-content", "recording", "screenshot", "page-image"]),
    "Water Lilies must forbid inner-browser and capture uses",
  );
  const sourcePath = resolve(workspaceRoot, asset.source.path);
  const referenceManifestPath = resolve(workspaceRoot, asset.source.referenceManifest);
  const referenceManifest = JSON.parse(readFileSync(referenceManifestPath, "utf8"));
  const immutableItem = referenceManifest.items.find((item) => item.path === asset.source.path);
  assert(immutableItem?.sha256 === asset.sha256, "Water Lilies hash must match Task 1 immutable manifest");
  assert(sha256(sourcePath) === asset.sha256, "immutable Water Lilies source hash changed");
  const resolvedAsset = assetPath ?? resolve(projectRoot, asset.localPath);
  assert(existsSync(resolvedAsset), `Water Lilies asset is missing: ${resolvedAsset}`);
  assert(sha256(resolvedAsset) === asset.sha256, "local Water Lilies hash differs from immutable source");
  if (!assetPath) {
    const visualAssets = readdirSync(resolve(projectRoot, "assets"))
      .filter((name) => [".jpg", ".jpeg", ".png", ".webp", ".gif", ".svg"].includes(extname(name).toLowerCase()))
      .sort();
    assert(JSON.stringify(visualAssets) === JSON.stringify(["water-lilies.jpg"]), "Water Lilies must be the sole local visual asset");
  }
  return { id: asset.id, localPath: asset.localPath, sha256: asset.sha256, permittedUse: asset.permittedUse };
}
