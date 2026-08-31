import { existsSync, mkdirSync, readdirSync, readFileSync, writeFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import yaml from 'yaml';

const __dirname = dirname(fileURLToPath(import.meta.url));
const projectRoot = resolve(__dirname, '..');

// Support running from website/ or repository root
const candidates = [
  resolve(projectRoot, '..', 'changelog', 'releases'),
  resolve(projectRoot, 'changelog', 'releases'),
  resolve(process.cwd(), 'changelog', 'releases'),
];

const releasesDir = candidates.find((dir) => existsSync(dir));

if (!releasesDir) {
  console.error('Error: changelog/releases directory not found. Checked:');
  for (const c of candidates) {
    console.error(`  - ${c}`);
  }
  process.exit(1);
}

const outDir = join(projectRoot, 'src', 'data');
const outFile = join(outDir, 'changelog.json');

const SEMVER_REGEX = /^\d+\.\d+\.\d+(-[a-zA-Z0-9.]+)?$/;
const DATE_REGEX = /^\d{4}-\d{2}-\d{2}$/;
const VALID_CHANNELS = new Set(['stable', 'beta', 'nightly']);
const VALID_SECTION_TYPES = new Set(['added', 'fixed', 'changed', 'removed']);
const ISSUE_REF_REGEX = /#\d+\b/;

function validateFrontmatter(data, fileName) {
  if (!data || typeof data !== 'object') {
    throw new Error(`[${fileName}] Frontmatter must be a YAML object.`);
  }

  // 1. version
  if (!data.version || typeof data.version !== 'string' || !SEMVER_REGEX.test(data.version)) {
    throw new Error(
      `[${fileName}] Invalid or missing "version": expected semantic version string (e.g. "1.0.0"), received: ${JSON.stringify(data.version)}`,
    );
  }

  // 2. date
  if (!data.date || typeof data.date !== 'string' || !DATE_REGEX.test(data.date)) {
    throw new Error(
      `[${fileName}] Invalid or missing "date": expected ISO 8601 date "YYYY-MM-DD", received: ${JSON.stringify(data.date)}`,
    );
  }

  // 3. channel
  if (!data.channel || typeof data.channel !== 'string' || !VALID_CHANNELS.has(data.channel)) {
    throw new Error(
      `[${fileName}] Invalid or missing "channel": expected one of ${Array.from(VALID_CHANNELS).join(', ')}, received: ${JSON.stringify(data.channel)}`,
    );
  }

  // 4. highlights
  if (!Array.isArray(data.highlights) || data.highlights.length === 0) {
    throw new Error(
      `[${fileName}] Invalid or missing "highlights": expected a non-empty array of summary strings.`,
    );
  }

  for (let i = 0; i < data.highlights.length; i++) {
    const item = data.highlights[i];
    if (typeof item !== 'string' || item.trim() === '') {
      throw new Error(`[${fileName}] highlights[${i}] must be a non-empty string.`);
    }
    if (ISSUE_REF_REGEX.test(item)) {
      throw new Error(
        `[${fileName}] highlights[${i}] contains raw issue token (${item.match(ISSUE_REF_REGEX)[0]}). Issue references must be plain descriptions.`,
      );
    }
  }

  // 5. sections
  if (!Array.isArray(data.sections) || data.sections.length === 0) {
    throw new Error(
      `[${fileName}] Invalid or missing "sections": expected a non-empty array of section objects.`,
    );
  }

  for (let s = 0; s < data.sections.length; s++) {
    const sec = data.sections[s];
    if (!sec || typeof sec !== 'object') {
      throw new Error(`[${fileName}] sections[${s}] must be an object.`);
    }

    if (!sec.type || typeof sec.type !== 'string' || !VALID_SECTION_TYPES.has(sec.type)) {
      throw new Error(
        `[${fileName}] sections[${s}].type must be one of: ${Array.from(VALID_SECTION_TYPES).join(', ')}. Received: ${JSON.stringify(sec.type)}`,
      );
    }

    if (!sec.title || typeof sec.title !== 'string' || sec.title.trim() === '') {
      throw new Error(`[${fileName}] sections[${s}].title must be a non-empty string.`);
    }

    if (!Array.isArray(sec.items) || sec.items.length === 0) {
      throw new Error(`[${fileName}] sections[${s}].items must be a non-empty array of strings.`);
    }

    for (let i = 0; i < sec.items.length; i++) {
      const item = sec.items[i];
      if (typeof item !== 'string' || item.trim() === '') {
        throw new Error(`[${fileName}] sections[${s}].items[${i}] must be a non-empty string.`);
      }
      if (ISSUE_REF_REGEX.test(item)) {
        throw new Error(
          `[${fileName}] sections[${s}].items[${i}] contains raw issue token (${item.match(ISSUE_REF_REGEX)[0]}). Issue references must be plain descriptions.`,
        );
      }
    }
  }
}

function parseReleaseFile(filePath, fileName) {
  const content = readFileSync(filePath, 'utf8');
  const lines = content.split(/\r?\n/);

  if (lines[0]?.trim() !== '---') {
    throw new Error(`[${fileName}] File must begin with frontmatter delimiter '---'.`);
  }

  let endIdx = -1;
  for (let i = 1; i < lines.length; i++) {
    if (lines[i]?.trim() === '---') {
      endIdx = i;
      break;
    }
  }

  if (endIdx === -1) {
    throw new Error(`[${fileName}] File has unclosed frontmatter.`);
  }

  const yamlString = lines.slice(1, endIdx).join('\n');
  const body = lines.slice(endIdx + 1).join('\n').trim();

  let data;
  try {
    data = yaml.parse(yamlString);
  } catch (err) {
    throw new Error(`[${fileName}] Failed to parse YAML frontmatter: ${err.message}`);
  }

  validateFrontmatter(data, fileName);

  return {
    version: String(data.version).trim(),
    date: String(data.date).trim(),
    channel: String(data.channel).trim(),
    highlights: data.highlights.map((h) => String(h).trim()),
    sections: data.sections.map((sec) => ({
      type: sec.type,
      title: String(sec.title).trim(),
      items: sec.items.map((it) => String(it).trim()),
    })),
    body,
  };
}

function main() {
  const start = Date.now();
  console.log(`Syncing changelog from ${releasesDir} ...`);

  const files = readdirSync(releasesDir)
    .filter((f) => f.endsWith('.md') && f.toLowerCase() !== 'readme.md')
    .sort();

  if (files.length === 0) {
    throw new Error(`No release markdown files found in ${releasesDir}`);
  }

  const releases = [];

  for (const file of files) {
    const filePath = join(releasesDir, file);
    const release = parseReleaseFile(filePath, file);
    releases.push(release);
  }

  // Sort descending by release date, then version
  releases.sort((a, b) => {
    const diff = new Date(b.date).getTime() - new Date(a.date).getTime();
    if (diff !== 0) return diff;
    return b.version.localeCompare(a.version, undefined, { numeric: true, sensitivity: 'base' });
  });

  mkdirSync(outDir, { recursive: true });
  writeFileSync(outFile, JSON.stringify({ releases }, null, 2) + '\n', 'utf8');

  console.log(
    `✓ Bundled ${releases.length} release(s) into ${outFile} (${Date.now() - start}ms)`,
  );
}

main();
