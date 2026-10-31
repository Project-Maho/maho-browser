import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

const mojomPath = path.join(__dirname, '..', '..', 'ui', 'webui', 'maho_settings', 'maho_settings.mojom');
const dtsPath = path.join(__dirname, 'maho_settings.mojom-webui.js.d.ts');

if (!fs.existsSync(mojomPath)) {
  console.error(`Mojom file not found at ${mojomPath}`);
  process.exit(1);
}
if (!fs.existsSync(dtsPath)) {
  console.error(`d.ts file not found at ${dtsPath}`);
  process.exit(1);
}

const mojomContentRaw = fs.readFileSync(mojomPath, 'utf8');
const dtsContent = fs.readFileSync(dtsPath, 'utf8');

// Strip comments from mojom to avoid matching words in comments as methods
const mojomContent = mojomContentRaw.replace(/\/\/.*/g, '').replace(/\/\*[\s\S]*?\*\//g, '');

function toCamelCase(str) {
  return str.charAt(0).toLowerCase() + str.slice(1);
}

function snakeToCamel(str) {
  return str.replace(/_([a-zA-Z0-9])/g, (_, c) => c.toUpperCase());
}

function extractStructFields(structBody) {
  const fields = [];
  for (const rawStatement of structBody.split(";")) {
    const statement = rawStatement.replace(/=[^=]*$/, '').trim();
    if (statement.length === 0) continue;
    const nameMatch = statement.match(/([A-Za-z_][A-Za-z0-9_]*)\s*$/);
    if (nameMatch) fields.push(nameMatch[1]);
  }
  return fields;
}

function extractEnumMembers(enumBody) {
  const members = [];
  for (const rawEntry of enumBody.split(",")) {
    const entry = rawEntry.trim();
    if (entry.length === 0) continue;
    const nameMatch = entry.match(/^([A-Za-z_][A-Za-z0-9_]*)/);
    if (nameMatch) members.push(nameMatch[1]);
  }
  return members;
}

function getBlockContent(content, searchPattern) {
  let index = -1;
  if (searchPattern instanceof RegExp) {
    const match = content.match(searchPattern);
    if (match) {
      index = match.index;
    }
  } else {
    index = content.indexOf(searchPattern);
  }
  if (index === -1) return "";
  const openBrace = content.indexOf("{", index);
  if (openBrace === -1) return "";
  let braceCount = 1;
  let i = openBrace + 1;
  while (braceCount > 0 && i < content.length) {
    if (content[i] === "{") braceCount++;
    else if (content[i] === "}") braceCount--;
    i++;
  }
  return content.slice(openBrace + 1, i - 1);
}

let errors = [];

// 1. Check Structs (declaration + every field, camelCased)
const structRegex = /struct\s+([A-Za-z0-9_]+)/g;
let match;
while ((match = structRegex.exec(mojomContent)) !== null) {
  const structName = match[1];
  if (!dtsContent.includes(`export interface ${structName}`) && !dtsContent.includes(`export class ${structName}`)) {
    errors.push(`Missing struct/class declaration in d.ts: ${structName}`);
    continue;
  }
  const structBody = getBlockContent(mojomContent, new RegExp(`\\bstruct\\s+${structName}\\b`));
  const dtsStructBlock = getBlockContent(dtsContent, new RegExp(`\\b(?:interface|class)\\s+${structName}\\b`));
  for (const field of extractStructFields(structBody)) {
    const camelField = snakeToCamel(field);
    const fieldRegex = new RegExp(`\\b${camelField}\\s*\\??\\s*:`);
    if (!fieldRegex.test(dtsStructBlock)) {
      errors.push(`Missing field in d.ts ${structName}: ${camelField} (from mojom ${structName}.${field})`);
    }
  }
}

// 2. Check Enums (declaration + every member)
const enumRegex = /enum\s+([A-Za-z0-9_]+)/g;
while ((match = enumRegex.exec(mojomContent)) !== null) {
  const enumName = match[1];
  if (!dtsContent.includes(`export enum ${enumName}`)) {
    errors.push(`Missing enum declaration in d.ts: ${enumName}`);
    continue;
  }
  const enumBody = getBlockContent(mojomContent, new RegExp(`\\benum\\s+${enumName}\\b`));
  const dtsEnumBlock = getBlockContent(dtsContent, new RegExp(`\\benum\\s+${enumName}\\b`));
  for (const member of extractEnumMembers(enumBody)) {
    const memberRegex = new RegExp(`\\b${member}\\b`);
    if (!memberRegex.test(dtsEnumBlock)) {
      errors.push(`Missing enum member in d.ts ${enumName}: ${member}`);
    }
  }
}

// 3. Extract PageHandler methods from mojom
const pageHandlerBlock = getBlockContent(mojomContent, /\binterface\s+PageHandler\b/);
if (!pageHandlerBlock) {
  console.error("Could not find PageHandler interface in mojom!");
  process.exit(1);
}
const methodRegex = /\b([A-Za-z0-9_]+)\s*\(/g;
let pageHandlerMethods = [];
while ((match = methodRegex.exec(pageHandlerBlock)) !== null) {
  pageHandlerMethods.push(match[1]);
}

// Check PageHandler methods in d.ts PageHandlerRemote
const pageHandlerRemoteBlock = getBlockContent(dtsContent, /\bclass\s+PageHandlerRemote\b/);
if (!pageHandlerRemoteBlock) {
  console.error("Could not find PageHandlerRemote class in d.ts!");
  process.exit(1);
}

for (const method of pageHandlerMethods) {
  const camelMethod = toCamelCase(method);
  const regex = new RegExp(`\\b${camelMethod}\\s*\\(`);
  if (!regex.test(pageHandlerRemoteBlock)) {
    errors.push(`Missing method in PageHandlerRemote in d.ts: ${camelMethod} (from mojom ${method})`);
  }
}

// 4. Extract Page (Callback) methods from mojom
const pageBlock = getBlockContent(mojomContent, /\binterface\s+Page\b/);
if (!pageBlock) {
  console.error("Could not find Page interface in mojom!");
  process.exit(1);
}
let pageMethods = [];
while ((match = methodRegex.exec(pageBlock)) !== null) {
  pageMethods.push(match[1]);
}

// Check PageCallbackRouter properties in d.ts
const pageCallbackRouterBlock = getBlockContent(dtsContent, /\bclass\s+PageCallbackRouter\b/);
if (!pageCallbackRouterBlock) {
  console.error("Could not find PageCallbackRouter class in d.ts!");
  process.exit(1);
}

for (const method of pageMethods) {
  const camelProp = toCamelCase(method);
  const regex = new RegExp(`\\b${camelProp}\\s*:\\s*\\{`);
  if (!regex.test(pageCallbackRouterBlock)) {
    errors.push(`Missing callback property in PageCallbackRouter in d.ts: ${camelProp} (from mojom ${method})`);
  }
}

// 5. Assert PageCallbackRouter listener contract matches the Mojo JS runtime:
// per-event addListener returns a numeric id, and the router exposes a single
// top-level removeListener(id: number). See mojo/public/js/interface_support.js.
if (!/\baddListener\s*\([\s\S]*?\)\s*:\s*number\b/.test(pageCallbackRouterBlock)) {
  errors.push("PageCallbackRouter listener shape wrong: no addListener(...) returning number");
}
if (/\baddListener\s*\([\s\S]*?\)\s*:\s*void\b/.test(pageCallbackRouterBlock)) {
  errors.push("PageCallbackRouter listener shape wrong: addListener must return number (numeric id), not void");
}
if (/\bremoveListener\s*\(\s*listener\b/.test(pageCallbackRouterBlock)) {
  errors.push("PageCallbackRouter listener shape wrong: no per-event removeListener(listener); use top-level removeListener(id)");
}
if (!/\bremoveListener\s*\(\s*id\s*:\s*number\s*\)\s*:\s*boolean\b/.test(pageCallbackRouterBlock)) {
  errors.push("PageCallbackRouter listener shape wrong: missing top-level removeListener(id: number): boolean");
}

if (errors.length > 0) {
  console.error("Drift check failed! The following elements are missing or mismatched between mojom and d.ts:");
  for (const err of errors) {
    console.error(` - ${err}`);
  }
  process.exit(1);
}

console.log("Drift check passed! mojom and d.ts are aligned.");
process.exit(0);
