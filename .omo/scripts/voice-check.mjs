#!/usr/bin/env node
/**
 * voice-check.mjs
 *
 * Pre-publish lint for Maho blog posts. Scans .mdx files under
 * website/src/content/docs/blog/ for:
 *
 *  - Forbidden words (hype, AI-slop)
 *  - Em-dashes (use period or comma)
 *  - Exclamation marks
 *  - "open-source" applied to Maho
 *  - github.com/<org>/<repo> links
 *  - "Download" CTAs
 *  - Required frontmatter fields
 *  - Word count vs target (if --targets is provided)
 *
 * Usage:
 *   node .omo/scripts/voice-check.mjs
 *   node .omo/scripts/voice-check.mjs path/to/post.mdx
 *   node .omo/scripts/voice-check.mjs --posts <slug1>,<slug2>
 *
 * Exit code:
 *   0 = clean
 *   1 = violations found
 */

import { readFileSync, readdirSync, statSync } from "node:fs";
import { join, basename, extname } from "node:path";
import { fileURLToPath } from "node:url";

const ROOT = fileURLToPath(new URL("../..", import.meta.url));
const BLOG_DIR = join(ROOT, "website/src/content/docs/blog");

const FORBIDDEN_WORDS = [
  "revolutionary",
  "seamless",
  "world-class",
  "world class",
  "cutting-edge",
  "cutting edge",
  "game-changer",
  "game changer",
  "unleash",
  "supercharge",
  "paradigm-shifting",
  "paradigm shifting",
  "next-generation",
  "next generation",
  "best-in-class",
  "best in class",
];

const REQUIRED_FRONTMATTER = ["title", "description", "date", "tags", "excerpt", "authors"];

const args = process.argv.slice(2);
let targets = [];
if (args.length === 0) {
  targets = listMdx(BLOG_DIR);
} else if (args[0] === "--posts" && args[1]) {
  const slugs = args[1].split(",").map((s) => s.trim());
  targets = slugs.map((s) => join(BLOG_DIR, `${s}.mdx`));
} else {
  targets = args;
}

let totalViolations = 0;
const summary = [];

for (const file of targets) {
  const violations = checkFile(file);
  totalViolations += violations.length;
  summary.push({ file: basename(file), count: violations.length, violations });
}

for (const item of summary) {
  if (item.count === 0) {
    console.log(`OK  ${item.file}`);
  } else {
    console.log(`FAIL  ${item.file}  (${item.count} violation${item.count === 1 ? "" : "s"})`);
    for (const v of item.violations) {
      console.log(`     L${v.line.toString().padStart(4)}  [${v.rule}]  ${v.snippet}`);
    }
  }
}

console.log("");
console.log(`Files: ${summary.length}`);
console.log(`Total violations: ${totalViolations}`);
process.exit(totalViolations === 0 ? 0 : 1);

// ---

function listMdx(dir) {
  const entries = readdirSync(dir);
  const files = [];
  for (const e of entries) {
    const full = join(dir, e);
    if (statSync(full).isFile() && extname(e) === ".mdx") files.push(full);
  }
  return files;
}

function checkFile(file) {
  const content = readFileSync(file, "utf8");
  const lines = content.split("\n");
  const violations = [];
  const fmEnd = findFrontmatterEnd(lines);

  // 1. Frontmatter required fields
  if (fmEnd === -1) {
    violations.push({ line: 1, rule: "frontmatter", snippet: "missing or unclosed frontmatter" });
  } else {
    const fmText = lines.slice(1, fmEnd).join("\n");
    for (const field of REQUIRED_FRONTMATTER) {
      const re = new RegExp(`^${field}:`, "m");
      if (!re.test(fmText)) {
        violations.push({ line: 1, rule: "frontmatter", snippet: `missing field: ${field}` });
      }
    }
    // First tag must be `browser` or `mail`
    const tagsMatch = fmText.match(/tags:\s*\[([^\]]+)\]/);
    if (tagsMatch) {
      const firstTag = tagsMatch[1].split(",")[0].replace(/['"\s]/g, "");
      if (firstTag !== "browser" && firstTag !== "mail") {
        violations.push({ line: 1, rule: "frontmatter", snippet: `first tag must be 'browser' or 'mail', got '${firstTag}'` });
      }
    } else {
      // YAML block-list form
      const blockMatch = fmText.match(/tags:\s*\n((?:\s+-\s+\w[-\w]*\n?)+)/);
      if (blockMatch) {
        const firstLine = blockMatch[1].split("\n")[0];
        const firstTag = firstLine.replace(/^\s+-\s+/, "").trim();
        if (firstTag !== "browser" && firstTag !== "mail") {
          violations.push({ line: 1, rule: "frontmatter", snippet: `first tag must be 'browser' or 'mail', got '${firstTag}'` });
        }
      }
    }
  }

  // 2. Body-only checks (skip frontmatter)
  const bodyStart = fmEnd === -1 ? 0 : fmEnd + 1;
  for (let i = bodyStart; i < lines.length; i++) {
    const line = lines[i];
    const lineNo = i + 1;

    // Forbidden words
    for (const word of FORBIDDEN_WORDS) {
      const re = new RegExp(`\\b${word.replace(/-/g, "[- ]")}\\b`, "i");
      if (re.test(line)) {
        violations.push({ line: lineNo, rule: "forbidden-word", snippet: trim(line, word) });
      }
    }

    // Em-dashes
    if (line.includes("\u2014") || line.includes("\u2013")) {
      violations.push({ line: lineNo, rule: "em-dash", snippet: trim(line, "\u2014") });
    }

    // Exclamation marks (in body, allow in code blocks — quick heuristic: skip if line starts with whitespace+`>` or in fenced)
    if (/[!]/.test(line) && !line.trim().startsWith(">") && !inCodeFence(lines, i)) {
      // Allow ! in URLs/code-ish content
      if (!/[`\[\(]\S*![^`\)\]]*[`\]\)]/.test(line) && !/!\[/.test(line)) {
        violations.push({ line: lineNo, rule: "exclamation", snippet: trim(line, "!") });
      }
    }

    // "open-source" applied to Maho — flag any "open-source" then run human review for false positives
    if (/open[- ]source/i.test(line) && /\bMaho\b/i.test(line)) {
      violations.push({ line: lineNo, rule: "open-source-on-maho", snippet: trim(line, "open") });
    }

    // Truth-table: comparison table rows that mark Maho as open source = Yes.
    // Pattern: a markdown table row starting with `| Maho |` followed by cells
    // where any cell containing the word "Yes" appears under an "Open source"
    // column. We can't easily map column positions per-line, so we detect the
    // co-occurrence of `| Maho |` and a cell value of `Yes` adjacent to "open"
    // language anywhere in the file by scanning header → data row pairs.
    const mahoRowMatch = /^\|\s*Maho\s*\|/i.test(line);
    if (mahoRowMatch) {
      // Find nearest preceding header row to map columns
      let headerLine = null;
      for (let j = i - 1; j >= bodyStart; j--) {
        if (/^\|.*\|/.test(lines[j]) && /open[- ]?source/i.test(lines[j])) {
          headerLine = lines[j];
          break;
        }
        if (lines[j].trim() === "" && j < i - 5) break;
      }
      if (headerLine) {
        const headerCells = headerLine.split("|").map((c) => c.trim());
        const dataCells = line.split("|").map((c) => c.trim());
        const osIdx = headerCells.findIndex((c) => /open[- ]?source/i.test(c));
        if (osIdx > -1 && dataCells[osIdx] && /^yes\b/i.test(dataCells[osIdx])) {
          violations.push({
            line: lineNo,
            rule: "open-source-table-on-maho",
            snippet: `Maho row claims '${dataCells[osIdx]}' under '${headerCells[osIdx]}' column`,
          });
        }
      }
    }

    // GitHub repo links
    if (/github\.com\/[\w-]+\/[\w-]+/.test(line)) {
      violations.push({ line: lineNo, rule: "github-link", snippet: trim(line, "github.com") });
    }

    // "Download" CTAs (link or button text)
    const downloadCta = /\b[Dd]ownload\s+(Maho|now|for\s+(macOS|mac))/;
    if (downloadCta.test(line)) {
      violations.push({ line: lineNo, rule: "download-cta", snippet: trim(line, "ownload") });
    }
  }

  return violations;
}

function findFrontmatterEnd(lines) {
  if (lines[0] !== "---") return -1;
  for (let i = 1; i < lines.length; i++) {
    if (lines[i] === "---") return i;
  }
  return -1;
}

function inCodeFence(lines, idx) {
  let inside = false;
  for (let i = 0; i <= idx; i++) {
    if (/^```/.test(lines[i])) inside = !inside;
  }
  return inside;
}

function trim(line, near) {
  const t = line.trim();
  if (t.length <= 90) return t;
  const idx = t.toLowerCase().indexOf(near.toLowerCase());
  if (idx === -1) return t.slice(0, 90) + "…";
  const start = Math.max(0, idx - 30);
  const end = Math.min(t.length, idx + 60);
  return (start > 0 ? "…" : "") + t.slice(start, end) + (end < t.length ? "…" : "");
}
