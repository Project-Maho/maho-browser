#!/usr/bin/env node
/**
 * gen-tokens.mjs — Generate per-surface token files from the single core contract.
 *
 * Source of truth: docs/brand/tokens.core.json (DTCG)
 * Emits (STRUCTURE only — space/radius/semantic/motion; NOT role values):
 *   1. maho-chromium/browser/ui/theme/maho_design_tokens.h    (C++ constexpr)
 *   2. maho-chromium/browser/resources/maho_common/react/theme/_core-tokens.css
 *   3. maho/web-ai/src/_core-tokens.css
 *
 * role.* slots are intentionally NOT emitted — each surface keeps its own
 * background/accent/font values (see docs/brand/token-conflict-matrix-2026-07.md).
 *
 * Usage:  node scripts/gen-tokens.mjs [--check]
 *   --check : verify on-disk generated files match fresh output (CI drift guard); exit 1 on drift.
 */
import { readFileSync, writeFileSync, existsSync, mkdirSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { execSync } from 'node:child_process';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const SRC = join(ROOT, 'docs/brand/tokens.core.json');
const CHECK = process.argv.includes('--check');

const core = JSON.parse(readFileSync(SRC, 'utf8'));
let sha = 'unknown';
try { sha = execSync('git rev-parse --short HEAD', { cwd: ROOT }).toString().trim(); } catch {}

const banner = (cmt1, cmt2) =>
  `${cmt1} AUTO-GENERATED from docs/brand/tokens.core.json @ ${sha} ${cmt2}\n` +
  `${cmt1} Do NOT edit by hand. Run: node scripts/gen-tokens.mjs ${cmt2}\n` +
  `${cmt1} role.* (background/accent/fonts) are per-surface — NOT generated here. ${cmt2}\n`;

const vals = (node) =>
  Object.entries(node).filter(([k, v]) => !k.startsWith('$') && v && typeof v === 'object' && '$value' in v);

// ---- 1. C++ header (Native Views) ----
function genCppHeader() {
  const L = [];
  L.push(banner('//', ''));
  L.push('#ifndef MAHO_BROWSER_UI_THEME_MAHO_DESIGN_TOKENS_H_');
  L.push('#define MAHO_BROWSER_UI_THEME_MAHO_DESIGN_TOKENS_H_\n');
  L.push('namespace maho::tokens {\n');
  L.push('// Spacing scale (dp, 4px base) — unified across surfaces.');
  for (const [k, v] of vals(core.space)) L.push(`inline constexpr int kSpace${k} = ${parseInt(v.$value)};`);
  L.push('\n// Radius scale (dp).');
  for (const [k, v] of vals(core.radius)) {
    const n = k[0].toUpperCase() + k.slice(1);
    L.push(`inline constexpr int kRadius${n} = ${parseInt(v.$value)};`);
  }
  L.push('\n// Motion durations (ms).');
  for (const [k, v] of vals(core.motion.duration)) {
    const n = k[0].toUpperCase() + k.slice(1);
    L.push(`inline constexpr int kDuration${n}Ms = ${parseInt(v.$value)};`);
  }
  L.push('\n}  // namespace maho::tokens\n');
  L.push('#endif  // MAHO_BROWSER_UI_THEME_MAHO_DESIGN_TOKENS_H_');
  return L.join('\n') + '\n';
}

// ---- 2 & 3. CSS custom properties (WebUI + web-ai) ----
function genCss() {
  const L = [];
  L.push(banner('/*', '*/'));
  L.push(':root {');
  L.push('  /* Spacing scale (4px base) — unified. Replaces --maho-{settings,ai}-space-*. */');
  for (const [k, v] of vals(core.space)) L.push(`  --space-${k}: ${v.$value};`);
  L.push('  /* Radius scale. */');
  for (const [k, v] of vals(core.radius)) L.push(`  --radius-${k}: ${v.$value};`);
  L.push('  /* Semantic colors (Apple-system baseline; surfaces may re-tint but keep names). */');
  for (const [k, v] of vals(core.semantic)) {
    L.push(`  --color-${k}: ${v.$value};`);
    const sub = v.$extensions && v.$extensions['maho.subtle-dark'];
    if (sub) L.push(`  --color-${k}-subtle: ${sub};`);
  }
  L.push('  /* Motion. */');
  for (const [k, v] of vals(core.motion.duration)) L.push(`  --duration-${k}: ${v.$value};`);
  for (const [k, v] of vals(core.motion.easing)) L.push(`  --ease-${k}: cubic-bezier(${v.$value.join(', ')});`);
  L.push('}');
  return L.join('\n') + '\n';
}

const outputs = [
  { path: 'maho-chromium/browser/ui/theme/maho_design_tokens.h', content: genCppHeader() },
  { path: 'maho-chromium/browser/resources/maho_common/react/theme/_core-tokens.css', content: genCss() },
  { path: 'maho/web-ai/src/_core-tokens.css', content: genCss() },
];

let drift = false;
for (const o of outputs) {
  const abs = join(ROOT, o.path);
  if (CHECK) {
    const cur = existsSync(abs) ? readFileSync(abs, 'utf8') : '';
    // ignore sha line differences: compare content minus banner sha
    const strip = (s) => s.replace(/@ [0-9a-f]+ /g, '@ SHA ');
    if (strip(cur) !== strip(o.content)) { console.error(`DRIFT: ${o.path}`); drift = true; }
    else console.log(`ok: ${o.path}`);
  } else {
    mkdirSync(dirname(abs), { recursive: true });
    writeFileSync(abs, o.content);
    console.log(`wrote: ${o.path}`);
  }
}
if (CHECK && drift) process.exit(1);
console.log(CHECK ? 'check passed' : 'done');
