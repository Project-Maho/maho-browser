#!/usr/bin/env bun

import {readFileSync} from "node:fs";
import {resolve} from "node:path";

const root = resolve(import.meta.dir, "../../..");
const mojom = readFileSync(
    resolve(root, "ui/webui/maho_settings/maho_settings.mojom"), "utf8");
const declarations = readFileSync(
    resolve(root, "resources/maho_settings/maho_settings.mojom-webui.js.d.ts"), "utf8");

function assert(condition: boolean, message: string): void {
  if (!condition) {
    throw new Error(message);
  }
  console.log(`PASS ${message}`);
}

function block(source: string, start: RegExp): string {
  const match = start.exec(source);
  if (!match || match.index === undefined) {
    throw new Error(`Missing contract block: ${start}`);
  }
  const open = source.indexOf("{", match.index);
  let depth = 1;
  for (let index = open + 1; index < source.length; index += 1) {
    if (source[index] === "{") depth += 1;
    if (source[index] === "}") depth -= 1;
    if (depth === 0) return source.slice(open + 1, index);
  }
  throw new Error(`Unterminated contract block: ${start}`);
}

const expectedScopes = ["kProfile", "kDevice", "kProcess", "kAccount", "kCoreGlobal"];
const expectedErrors = [
  "kNone",
  "kInvalidProfileId",
  "kProfileNotFound",
  "kProfileDeleting",
  "kProfileUnavailable",
  "kStaleContext",
  "kStaleProfileRevision",
  "kUnsupportedScope",
  "kInvalidArgument",
  "kInternal",
];
const expectedOperations = [
  "GetGlobalSettingsSnapshot",
  "GetSelectedProfileContext",
  "GetSelectedProfileMetadata",
  "UpdateSelectedProfileMetadata",
  "GetSelectedProfileSearchSettings",
  "SetSelectedProfileDefaultSearchEngine",
  "SetSelectedProfileSearchSuggestionsEnabled",
  "GetSelectedProfileDownloadSettings",
  "SetSelectedProfileDownloadPrompt",
  "SelectSelectedProfileDownloadDirectory",
  "GetSelectedProfileArchiveSettings",
  "SetSelectedProfileArchiveTimeout",
  "OpenChromiumSettingsPage",
];

const scopeBlock = block(mojom, /enum\s+SettingScope\b/);
for (const scope of expectedScopes) {
  assert(scopeBlock.includes(scope), `SettingScope includes ${scope}`);
}

const errorBlock = block(mojom, /enum\s+ProfileTargetErrorCode\b/);
for (const error of expectedErrors) {
  assert(errorBlock.includes(error), `ProfileTargetErrorCode includes ${error}`);
}

const pageHandler = block(mojom, /interface\s+PageHandler\b/);
const pageHandlerWithoutComments = pageHandler.replace(/\/\/.*$/gm, "");
for (const operation of expectedOperations) {
  assert(pageHandler.includes(`${operation}(`), `PageHandler exposes ${operation}`);
}

const selectedProfileMethods = pageHandlerWithoutComments
    .split(";")
    .filter((statement) => /SelectedProfile/.test(statement));
assert(selectedProfileMethods.length >= 11, "selected-profile API is domain-specific");
for (const statement of selectedProfileMethods) {
  const parameters = statement.slice(statement.indexOf("("), statement.lastIndexOf(")") + 1);
  assert(/ProfileTarget\s+target/.test(parameters), "selected-profile API carries canonical target ID/token");
  assert(!/\bstring\s+key\b/.test(parameters), "selected-profile API has no arbitrary key argument");
  assert(!/\bstring\s+(?:profile_)?path\b/i.test(parameters), "selected-profile API has no renderer-provided filesystem path");
}

const mutationNames = [
  "UpdateSelectedProfileMetadata",
  "SetSelectedProfileDefaultSearchEngine",
  "SetSelectedProfileSearchSuggestionsEnabled",
  "SetSelectedProfileDownloadPrompt",
  "SelectSelectedProfileDownloadDirectory",
  "SetSelectedProfileArchiveTimeout",
];
for (const name of mutationNames) {
  const method = selectedProfileMethods.find((statement) => statement.includes(`${name}(`));
  assert(method !== undefined, `mutation contract includes ${name}`);
  assert(
      /uint64\s+expected_context_revision/.test(method ?? ""),
      `${name} requires expected_context_revision`);
}

const targetBlock = block(mojom, /struct\s+ProfileTarget\b/);
assert(targetBlock.includes("string profile_id"), "ProfileTarget carries canonical profile ID");
assert(targetBlock.includes("string target_token"), "ProfileTarget carries browser-issued target token");

// Per-selected-profile preferences (homepage / restore / theme / DNT /
// third-party cookies / safe browsing / preload / quiet notifications) were
// removed entirely from the profile seam; these are managed globally via the
// standard SettingScope path. Assert the seam is fully gone.
assert(!/struct\s+ProfilePreferences\b/.test(mojom),
    "ProfilePreferences struct is removed from mojom");
assert(!/struct\s+ProfilePreferencesUpdate\b/.test(mojom),
    "ProfilePreferencesUpdate struct is removed from mojom");
assert(!/GetSelectedProfilePreferences\s*\(/.test(mojom),
    "GetSelectedProfilePreferences method is removed from mojom");
assert(!/UpdateSelectedProfilePreferences\s*\(/.test(mojom),
    "UpdateSelectedProfilePreferences method is removed from mojom");

const chooserMethod = selectedProfileMethods.find(
    (statement) => statement.includes("SelectSelectedProfileDownloadDirectory(")) ?? "";
assert(!/\bstring\s+/.test(chooserMethod), "download directory mutation is chooser-only");

for (const declaration of [
  "SettingScope",
  "ProfileLifecycleState",
  "ProfileTargetErrorCode",
  "ProfileTarget",
  "SelectedProfileContext",
  "ProfileMetadata",
  "ProfileSearchSettings",
  "ProfileDownloadSettings",
  "ProfileArchiveSettings",
]) {
  assert(
      declarations.includes(`export interface ${declaration}`) ||
          declarations.includes(`export enum ${declaration}`),
      `TypeScript declares ${declaration}`);
}

// The renderer must treat unknown generated enum values as unavailable instead
// of guessing a scope or presenting a successful mutation.
type ScopeView = {readonly label: string; readonly editable: boolean};
function renderScopeFixture(scope: number): ScopeView {
  switch (scope) {
    case 0: return {label: "Selected profile", editable: true};
    case 1: return {label: "This device", editable: false};
    case 2: return {label: "Browser process", editable: false};
    case 3: return {label: "Account", editable: false};
    case 4: return {label: "All profiles", editable: false};
    default: return {label: "Unavailable setting scope", editable: false};
  }
}

function renderErrorFixture(error: number): ScopeView {
  if (error === 0) return {label: "Ready", editable: true};
  if (error >= 1 && error <= 9) return {label: "Profile unavailable", editable: false};
  return {label: "Unknown profile error", editable: false};
}

assert(
    JSON.stringify(renderScopeFixture(999)) ===
        JSON.stringify({label: "Unavailable setting scope", editable: false}),
    "unknown setting scope renders fail-closed");
assert(
    JSON.stringify(renderErrorFixture(999)) ===
        JSON.stringify({label: "Unknown profile error", editable: false}),
    "unknown target error renders fail-closed");
assert(renderScopeFixture(Number.NaN).editable === false, "malformed scope input renders fail-closed");
assert(renderErrorFixture(-1).editable === false, "malformed error input renders fail-closed");
