'use strict';

// Evidence capture + sentinel-leak detection plumbing for the security
// fixtures. Later Vault/agent tasks feed real transcripts, permission
// payloads, HAR, AX trees, screenshots, and OCR text through these helpers and
// then assert (via scanForSentinel / assertSentinelOnlyIn) that the sentinel
// leaked into NONE of the redacted egress artifacts.
//
// Rules: these helpers write ONLY test artifacts under the given directory and
// never real credentials. The sentinel is a test-only marker.

const { execFileSync } = require('node:child_process');
const fs = require('node:fs');
const path = require('node:path');

const SENTINEL = 'S3NTINEL-maho-vault-9F4C';
const SENTINEL_BYTES = Buffer.from(SENTINEL);

function writeArtifact(dir, name, content) {
  fs.mkdirSync(dir, { recursive: true });
  const filePath = path.join(dir, name);
  if (Buffer.isBuffer(content)) {
    fs.writeFileSync(filePath, content);
  } else if (typeof content === 'string') {
    fs.writeFileSync(filePath, content);
  } else {
    fs.writeFileSync(filePath, JSON.stringify(content, null, 2));
  }
  return filePath;
}

// Named capture helpers — one per artifact class the plan enumerates. Each is a
// thin, explicit wrapper so call sites read as intent and the file names are
// stable for the leak scanner.
const captureModelEvents = (dir, events) => writeArtifact(dir, 'model-events.json', events);
const capturePermissionPayloads = (dir, payloads) =>
  writeArtifact(dir, 'permission-payloads.json', payloads);
const captureWebuiEvents = (dir, events) => writeArtifact(dir, 'webui-events.json', events);
const captureAuditLog = (dir, entries) =>
  writeArtifact(dir, 'audit-log.jsonl', entries.map((e) => JSON.stringify(e)).join('\n') + '\n');
const capturePageContext = (dir, ctx) => writeArtifact(dir, 'page-context.json', ctx);
const captureAxTree = (dir, tree) => writeArtifact(dir, 'ax-tree.json', tree);
const captureHar = (dir, har) => writeArtifact(dir, 'network.har', har);
const captureScreenshot = (dir, name, buffer) => writeArtifact(dir, name, buffer);
const captureOcrText = (dir, name, text) => writeArtifact(dir, name, text);

// Run OCR on a PNG/JPEG using tesseract when available; returns extracted text.
// Throws a descriptive error if tesseract is not installed so callers can
// record the class as unavailable rather than silently passing.
function runOcr(imagePath) {
  try {
    const out = execFileSync('tesseract', [imagePath, 'stdout'], {
      stdio: ['ignore', 'pipe', 'ignore'],
    });
    return out.toString('utf8');
  } catch (err) {
    throw new Error(`OCR unavailable or failed for ${imagePath}: ${err.message}`);
  }
}

function walkFiles(root, base = root, acc = []) {
  for (const entry of fs.readdirSync(root, { withFileTypes: true })) {
    const abs = path.join(root, entry.name);
    if (entry.isDirectory()) {
      walkFiles(abs, base, acc);
    } else if (entry.isFile()) {
      acc.push({ abs, rel: path.relative(base, abs) });
    }
  }
  return acc;
}

// Scan every file under `dir` (text or binary) for the sentinel. Returns
// [{ file, count }] for files that contain at least one occurrence.
function scanForSentinel(dir) {
  const hits = [];
  for (const { abs, rel } of walkFiles(dir)) {
    const buf = fs.readFileSync(abs);
    let count = 0;
    let from = 0;
    for (;;) {
      const idx = buf.indexOf(SENTINEL_BYTES, from);
      if (idx === -1) break;
      count += 1;
      from = idx + SENTINEL_BYTES.length;
    }
    if (count > 0) hits.push({ file: rel, count });
  }
  return hits;
}

// Assert the sentinel appears ONLY in the allowlisted (authorized) files.
// Throws with the offending files listed otherwise.
function assertSentinelOnlyIn(dir, allowedRelPaths) {
  const allowed = new Set(allowedRelPaths);
  const leaks = scanForSentinel(dir).filter((h) => !allowed.has(h.file));
  if (leaks.length > 0) {
    const detail = leaks.map((l) => `${l.file} (x${l.count})`).join(', ');
    throw new Error(`sentinel leaked into unauthorized artifacts: ${detail}`);
  }
  return true;
}

module.exports = {
  SENTINEL,
  writeArtifact,
  captureModelEvents,
  capturePermissionPayloads,
  captureWebuiEvents,
  captureAuditLog,
  capturePageContext,
  captureAxTree,
  captureHar,
  captureScreenshot,
  captureOcrText,
  runOcr,
  scanForSentinel,
  assertSentinelOnlyIn,
};
