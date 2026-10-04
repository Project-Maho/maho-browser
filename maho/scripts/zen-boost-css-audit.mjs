#!/usr/bin/env node
// # Run
// bun run maho/scripts/zen-boost-css-audit.mjs
// node maho/scripts/zen-boost-css-audit.mjs
//
// Parses Zen reference CSS and Maho ported CSS, asserts every Zen rule
// is present in Maho (modulo the translation table from plan §4 W3).
// Exit 0 if delta is empty or translation-table only; exit 1 otherwise.

import { readFileSync } from 'node:fs';
import { resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = dirname(fileURLToPath(import.meta.url));
const ROOT = resolve(__dirname, '..', '..');
const RESOURCES = resolve(ROOT, 'maho-chromium/browser/resources/maho_boost');

const ZEN_BOOSTS = resolve(RESOURCES, 'zen-port/zen-boosts.css');
const MAHO_BOOSTS = resolve(RESOURCES, 'boost.css');
const ZEN_ADVANCED = resolve(RESOURCES, 'zen-port/zen-advanced-color-options.css');
const MAHO_ADVANCED = resolve(RESOURCES, 'zen-advanced-color-options.css');

const TRANSLATION_TABLE = [
  [/@media\s+not\s+\(-moz-platform:\s*linux\)/g, ':root#zenBoostWindow:not([data-platform="linux"])'],
  [/@media\s+\(-moz-platform:\s*macos\)/g, ':root[data-platform="macos"]'],
  [/@media\s+\(-moz-platform:\s*linux\)/g, ':root[data-platform="linux"]'],
  [/@media\s+\(-moz-platform:\s*windows\)/g, ':root[data-platform="windows"]'],
  [/-moz-window-dragging:\s*drag/g, '-webkit-app-region: drag'],
  [/-moz-window-dragging:\s*no-drag/g, '-webkit-app-region: no-drag'],
  [/::-moz-range-thumb/g, '::-webkit-slider-thumb'],
  [/\(-moz-windows-mica\)/g, null],
  [/chrome:\/\/global\/skin\/icons\/arrow-down\.svg/g, 'icons/arrow-down.svg'],
];

const INTENTIONALLY_DROPPED = [
  '-moz-windows-mica',
  'macanimationtype',
];

function extractRules(css) {
  const rules = [];
  const cleaned = css
    .replace(/\/\*[\s\S]*?\*\//g, '')
    .replace(/\n\s*\n/g, '\n');

  let depth = 0;
  let currentSelector = '';
  let currentBlock = '';
  let blockStart = -1;

  for (let i = 0; i < cleaned.length; i++) {
    const ch = cleaned[i];
    if (ch === '{') {
      if (depth === 0) {
        currentSelector = cleaned.slice(blockStart === -1 ? 0 : blockStart, i).trim();
        blockStart = i + 1;
      }
      depth++;
    } else if (ch === '}') {
      depth--;
      if (depth === 0) {
        currentBlock = cleaned.slice(blockStart, i).trim();
        if (currentSelector) {
          rules.push({ selector: normalizeSelector(currentSelector), body: currentBlock });
        }
        blockStart = i + 1;
        currentSelector = '';
        currentBlock = '';
      }
    }
  }
  return rules;
}

function normalizeSelector(sel) {
  return sel
    .replace(/\s+/g, ' ')
    .replace(/\s*>\s*/g, ' > ')
    .replace(/\s*,\s*/g, ', ')
    .trim();
}

function translateSelector(zenSelector) {
  let translated = zenSelector;
  let wasTranslated = false;

  for (const [pattern, replacement] of TRANSLATION_TABLE) {
    if (pattern instanceof RegExp) {
      const before = translated;
      if (replacement === null) {
        if (pattern.test(translated)) {
          return { selector: null, dropped: true, reason: 'intentionally-dropped-mozilla-feature' };
        }
        pattern.lastIndex = 0;
      } else {
        pattern.lastIndex = 0;
        translated = translated.replace(pattern, replacement);
        pattern.lastIndex = 0;
        if (translated !== before) wasTranslated = true;
      }
    }
  }
  return { selector: translated, dropped: false, wasTranslated };
}

function isIntentionallyDropped(selector) {
  return INTENTIONALLY_DROPPED.some(d => selector.includes(d));
}

function auditPair(zenPath, mahoPath) {
  const zenCss = readFileSync(zenPath, 'utf8');
  const mahoCss = readFileSync(mahoPath, 'utf8');

  const zenRules = extractRules(zenCss);
  const mahoRules = extractRules(mahoCss);

  const mahoSelectors = new Set(mahoRules.map(r => r.selector));

  const missing = [];
  const translated = [];

  for (const zenRule of zenRules) {
    const result = translateSelector(zenRule.selector);

    if (result.dropped || isIntentionallyDropped(zenRule.selector)) {
      translated.push({
        zen: zenRule.selector,
        maho: null,
        reason: 'intentionally-dropped',
      });
      continue;
    }

    const target = result.selector;
    if (mahoSelectors.has(target)) {
      if (result.wasTranslated) {
        translated.push({
          zen: zenRule.selector,
          maho: target,
          reason: 'translation-table',
        });
      }
    } else {
      if (result.wasTranslated) {
        const originalPresent = mahoSelectors.has(zenRule.selector);
        if (originalPresent) {
          translated.push({
            zen: zenRule.selector,
            maho: zenRule.selector,
            reason: 'kept-original-selector',
          });
          continue;
        }
      }
      missing.push({
        zen_selector: zenRule.selector,
        expected_maho_selector: target,
      });
    }
  }

  const zenSelectors = new Set(zenRules.map(r => {
    const t = translateSelector(r.selector);
    return t.dropped ? null : t.selector;
  }).filter(Boolean));

  const extra = mahoRules
    .filter(r => !zenSelectors.has(r.selector))
    .map(r => r.selector);

  return { missing, translated, extra };
}

const boostsResult = auditPair(ZEN_BOOSTS, MAHO_BOOSTS);
const advancedResult = auditPair(ZEN_ADVANCED, MAHO_ADVANCED);

const output = {
  'zen-boosts.css': {
    missing_in_maho: boostsResult.missing,
    translated_rules: boostsResult.translated,
    extra_in_maho: boostsResult.extra,
  },
  'zen-advanced-color-options.css': {
    missing_in_maho: advancedResult.missing,
    translated_rules: advancedResult.translated,
    extra_in_maho: advancedResult.extra,
  },
};

const totalMissing = boostsResult.missing.length + advancedResult.missing.length;

console.log(JSON.stringify(output, null, 2));

if (totalMissing > 0) {
  console.error(`\nFAIL: ${totalMissing} Zen rule(s) missing in Maho.`);
  process.exit(1);
} else {
  console.error('\nPASS: All Zen rules accounted for in Maho (modulo translation table).');
  process.exit(0);
}
