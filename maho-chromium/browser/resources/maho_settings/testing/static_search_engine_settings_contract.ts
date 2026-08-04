#!/usr/bin/env bun
import {readFileSync} from "node:fs";
import {resolve} from "node:path";

const root = resolve(import.meta.dir, "../../..");

function source(path: string): string {
  return readFileSync(resolve(root, path), "utf8");
}

const mojom = source("ui/webui/maho_settings/maho_settings.mojom");
const handler = source("ui/webui/maho_settings/maho_settings_page_handler.cc");
const app = source("resources/maho_settings/react/app.tsx");
const editor = source("resources/maho_settings/react/profiles_editor.tsx");

const checks: ReadonlyArray<readonly [boolean, string]> = [
  [mojom.includes("struct SearchEngineInfo"), "settings Mojo exposes search engine data"],
  [mojom.includes("GetSearchEngines()"), "settings Mojo exposes search engine loading"],
  [mojom.includes("SetDefaultSearchEngine(string keyword)"), "settings Mojo exposes default engine changes"],
  [handler.includes("MahoSettingsPageHandler::GetSearchEngines"), "settings handler loads Chromium search engines"],
  [handler.includes("MahoSettingsPageHandler::SetDefaultSearchEngine"), "settings handler changes Chromium default search engine"],
  [!app.includes("<SearchEngineSettings store={store}") && editor.includes("setSelectedProfileDefaultSearchEngine"), "Search engine selector moved out of General into the profile editor"],
];

let failed = 0;
for (const [condition, label] of checks) {
  if (condition) {
    console.log(`PASS: ${label}`);
  } else {
    console.error(`FAIL: ${label}`);
    failed += 1;
  }
}

process.exit(failed === 0 ? 0 : 1);
