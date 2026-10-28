#!/usr/bin/env node
// Regenerates ublock-resources.json: uBlock Origin's redirect resources and
// scriptlets as an adblock-rust `Resource` array. content_blocker.rs embeds the
// file with `include_str!` and installs it on every adblock engine.
//
// Pinned source: uBlock Origin 1.75.0 (GPL-3.0, (c) Raymond Hill)
//   tag commit 21f0e686506bb21b514b451c8c6cb9bf8c82d232
//   https://codeload.github.com/gorhill/uBlock/tar.gz/refs/tags/1.75.0
//   tarball sha256 a518c7d6e6b3f81d1a738befeb617abba59f93bd6b1e626da079a1003e9db52b
//
// Usage:
//   curl -sSL https://codeload.github.com/gorhill/uBlock/tar.gz/refs/tags/1.75.0 | tar xz
//   node generate-ublock-resources.mjs uBlock-1.75.0 > ublock-resources.json
//
// Web-accessible resources follow adblock-rust's resource assembler; scriptlets
// follow Brave's builtinScriptlets transform. Scriptlets uBO marks requiresTrust
// get permission bit 0, which no Maho filter list is granted, so ordinary lists
// cannot inject them.

import fs from 'node:fs';
import path from 'node:path';
import { pathToFileURL } from 'node:url';

const root = process.argv[2];
if (!root) {
  console.error('usage: node generate-ublock-resources.mjs <uBlock checkout>');
  process.exit(2);
}
const load = rel => import(pathToFileURL(path.resolve(root, rel)).href);
const { default: redirects } = await load('src/js/redirect-resources.js');
const { builtinScriptlets } = await load('src/js/resources/scriptlets.js');

// adblock-rust MimeType::from_extension.
const MIME = {
  css: 'text/css', gif: 'image/gif', html: 'text/html', js: 'application/javascript',
  json: 'application/json', mp3: 'audio/mp3', mp4: 'video/mp4', png: 'image/png',
  txt: 'text/plain', xml: 'text/xml',
};
const STRIP_CR = new Set(['application/javascript', 'text/html', 'text/plain']);

const out = [];
const taken = new Set();
const push = resource => {
  const idents = [resource.name, ...resource.aliases];
  const clash = idents.filter(ident => taken.has(ident));
  if (clash.length) {
    // adblock-rust rejects the whole resource on a clash; drop it here instead.
    console.error(`skipped ${resource.name}: ${clash.join(', ')} already defined`);
    return;
  }
  idents.forEach(ident => taken.add(ident));
  out.push(resource);
};

for (const [name, props] of redirects) {
  if (props.params) continue; // parameterized (click2load.html): no static content
  const mime = MIME[path.extname(name).slice(1)] ?? 'application/octet-stream';
  let bytes = fs.readFileSync(path.resolve(root, 'src/web_accessible_resources', name));
  if (STRIP_CR.has(mime)) bytes = Buffer.from(bytes.toString('utf8').replaceAll('\r', ''));
  push({ name, aliases: [props.alias ?? []].flat(), kind: { mime }, content: bytes.toString('base64') });
}

const fnNames = new Map(builtinScriptlets.map(s => [s.fn, s.name]));
for (const s of builtinScriptlets) {
  const dependencies = (s.dependencies ?? []).map(dep => {
    const name = typeof dep === 'function' ? fnNames.get(dep) : dep;
    if (!name) throw new Error(`${s.name}: unregistered dependency ${dep.name ?? dep}`);
    return name;
  });
  push({
    name: s.name,
    aliases: s.aliases ?? [],
    kind: { mime: s.name.endsWith('.fn') ? 'fn/javascript' : 'application/javascript' },
    content: Buffer.from(s.fn.toString()).toString('base64'),
    dependencies,
    ...(s.requiresTrust ? { permission: 1 } : {}),
  });
}

process.stdout.write(`[\n${out.map(r => JSON.stringify(r)).join(',\n')}\n]\n`);
