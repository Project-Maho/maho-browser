#!/usr/bin/env bun

import {existsSync, readFileSync} from "node:fs";
import {join} from "node:path";

type StaticCheck = {
  readonly name: string;
  readonly run: () => void;
};

const ROOT = process.cwd();
const LEGACY_REVEAL_METHOD = `${"Reveal"}${"Password"}`;
const LEGACY_REVEAL_BRIDGE = `${"reveal"}${"Password"}`;

function readRepoFile(path: string): string {
  const absolutePath = join(ROOT, path);
  if (!existsSync(absolutePath)) {
    throw new Error(`Missing expected file: ${path}`);
  }
  return readFileSync(absolutePath, "utf8");
}

function assertIncludes(haystack: string, needle: string, label: string): void {
  if (!haystack.includes(needle)) {
    throw new Error(`${label}: expected to find ${JSON.stringify(needle)}`);
  }
}

function assertExcludes(haystack: string, needle: string, label: string): void {
  if (haystack.includes(needle)) {
    throw new Error(`${label}: forbidden copy/API found ${JSON.stringify(needle)}`);
  }
}

const checks: readonly StaticCheck[] = [
  {
    name: "guided import model lists exact source cards and formats",
    run: () => {
      const model = readRepoFile("maho-chromium/browser/resources/maho_settings/react/passwords_import_model.ts");
      for (const expected of [
        "1Password CSV or 1PUX",
        "Bitwarden CSV or JSON",
        "Apple Passwords CSV/file export",
        "KeePass / KeePassXC CSV",
        "onepassword_csv",
        "onepassword_1pux",
        "bitwarden_individual_csv",
        "bitwarden_organization_csv",
        "bitwarden_json",
        "apple_passwords_csv",
        "keepassxc_csv",
        "keepass_classic_csv",
      ]) {
        assertIncludes(model, expected, "source/format contract");
      }
      for (const forbidden of [
        "Export All Items to App",
        "native receiver",
        "app-to-app",
        "iCloud Passwords for Windows",
      ]) {
        assertExcludes(model, forbidden, "Apple file-only boundary");
      }
    },
  },
  {
    name: "guided import flow uses per-session preview commit cancel APIs safely",
    run: () => {
      const flow = [
        readRepoFile("maho-chromium/browser/resources/maho_settings/react/passwords_import_flow.tsx"),
        readRepoFile("maho-chromium/browser/resources/maho_settings/react/passwords_import_panels.tsx"),
      ].join("\n");
      for (const expected of [
        "selectPasswordImportFile",
        "previewPasswordImport",
        "commitPasswordImport",
        "cancelPasswordImport",
        "preview.imported",
        "preview.skipped",
        "preview.duplicates",
        "preview.blankPasswords",
        "preview.unsupportedFields",
        "preview.safeMessages",
        "preview.safeErrors",
        "Confirm import",
        "Imported passwords",
      ]) {
        assertIncludes(flow, expected, "preview/commit flow");
      }
      for (const forbidden of [LEGACY_REVEAL_BRIDGE, LEGACY_REVEAL_METHOD, "rawPassword", "totpSeed", "OTPAuth secret"]) {
        assertExcludes(flow, forbidden, "secret-free preview contract");
      }
    },
  },
  {
    name: "settings password surface uses status-only vault secret API",
    run: () => {
      const surface = [
        readRepoFile("maho-chromium/browser/ui/webui/maho_settings/maho_settings.mojom"),
        readRepoFile("maho-chromium/browser/ui/webui/maho_settings/maho_settings_page_handler.h"),
        readRepoFile("maho-chromium/browser/ui/webui/maho_settings/maho_settings_page_handler.cc"),
        readRepoFile("maho-chromium/browser/resources/maho_settings/maho_settings.mojom-webui.js.d.ts"),
        readRepoFile("maho-chromium/browser/resources/maho_settings/react/passwords_library.tsx"),
      ].join("\n");
      assertIncludes(surface, "UseVaultSecret", "status-only Mojo contract");
      assertIncludes(surface, "useVaultSecret", "status-only TS bridge contract");
      assertIncludes(surface, "No password was returned", "status-only UI copy");
      for (const forbidden of [LEGACY_REVEAL_METHOD, LEGACY_REVEAL_BRIDGE, "maho_core_reveal_password"]) {
        assertExcludes(surface, forbidden, "legacy plaintext WebUI reveal contract");
      }
    },
  },
  {
    name: "password provider dropdown keeps unavailable extensions selectable",
    run: () => {
      const settings = readRepoFile("maho-chromium/browser/resources/maho_settings/react/passwords_settings.tsx");
      const e2e = readRepoFile("maho-chromium/browser/resources/maho_settings/testing/e2e_pane_password_provider_dropdown.ts");
      assertIncludes(
          settings,
          "commitSettingValue('autofill.password_provider', nextValue)",
          "provider selection commit contract");
      assertIncludes(settings, "void loadPasswordSettings(false)", "provider status reload contract");
      assertIncludes(settings, "Extension not installed or disabled.", "unavailable provider option copy");
      assertExcludes(settings, "disabled: !opt.isAvailable", "provider options remain selectable");
      assertIncludes(e2e, "Unavailable Bitwarden provider option remains selectable", "Bitwarden selectable E2E coverage");
      assertIncludes(e2e, "Unavailable 1Password provider option remains selectable", "1Password selectable E2E coverage");
      assertIncludes(e2e, "Manage Extensions action is shown for Bitwarden", "Bitwarden warning E2E coverage");
      assertIncludes(e2e, "Manage Extensions action is shown for 1Password", "1Password warning E2E coverage");
    },
  },
  {
    name: "native picker is filtered before PreviewPasswordImport path API",
    run: () => {
      const mojom = readRepoFile("maho-chromium/browser/ui/webui/maho_settings/maho_settings.mojom");
      const handler = readRepoFile("maho-chromium/browser/ui/webui/maho_settings/maho_settings_page_handler.cc");
      assertIncludes(mojom, "SelectPasswordImportFile(PasswordImportSourceFormat source_format)", "mojom picker API");
      assertIncludes(handler, "ui::SelectFileDialog::FileTypeInfo", "native file picker filters");
      assertIncludes(handler, "PreviewPasswordImport", "existing preview API still present");
    },
  },
];

for (const check of checks) {
  check.run();
  console.log(`PASS ${check.name}`);
}
