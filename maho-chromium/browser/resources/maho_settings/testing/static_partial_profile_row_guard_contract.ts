#!/usr/bin/env bun

import {readFileSync} from 'node:fs';
import {resolve} from 'node:path';

import type {SettingScope} from '../models.js';
import {
  LEGACY_PROFILE_ROW_GUARD_PANE_KEYS,
  LIVE_PANE_SECTION_MANIFEST,
  selectedProfileLegacyRowDisposition,
} from '../schema/panes.js';
import {SETTING_METADATA} from '../schema/setting_schema.js';

function check(condition: boolean, message: string): void {
  if (!condition) throw new Error(message);
  console.log(`PASS ${message}`);
}

function repoFile(path: string): string {
  return readFileSync(resolve(process.cwd(), path), 'utf8');
}

const expectedPanes = ['advanced', 'appearance', 'tabs'];
check(
    JSON.stringify([...LEGACY_PROFILE_ROW_GUARD_PANE_KEYS].sort()) ===
        JSON.stringify(expectedPanes),
    'legacy profile-row gate covers exactly Tabs, Advanced, and Appearance');

const profileRows = new Map<string, string[]>();
for (const paneKey of LEGACY_PROFILE_ROW_GUARD_PANE_KEYS) {
  const sections = LIVE_PANE_SECTION_MANIFEST[paneKey] ?? [];
  const keys = sections.flatMap(section => section.card.settingKeys);
  const scopedRows = keys.filter(key => SETTING_METADATA[key]?.scope === 'profile');
  check(scopedRows.length > 0, `${paneKey} contains schema-classified profile rows`);
  profileRows.set(paneKey, scopedRows);
}
check(
    JSON.stringify([...profileRows.values()].flat().sort()) === JSON.stringify([
      'advanced.preload_pages',
      'appearance.sidebar_width',
      'appearance.theme',
      'tabs.archive_timeout',
    ]),
    'task-4 scope metadata identifies every host-backed profile row in the guarded panes');

const app = repoFile('maho-chromium/browser/resources/maho_settings/react/app.tsx');
check(
    app.includes('LEGACY_PROFILE_ROW_GUARD_PANE_KEYS.includes(') &&
        app.includes('pane.key as typeof LEGACY_PROFILE_ROW_GUARD_PANE_KEYS[number]'),
    'app derives row gating from the guarded pane inventory');
check(
    app.includes('selectedProfileLegacyRowDisposition('),
    'SettingRow routes guarded rows through the fail-closed decision helper');
check(
    app.includes("rowDisposition === 'limitation'"),
    'guarded profile rows replace the host-backed control with a limitation');
check(
    app.includes('<ProfileScopedSettingLimitation'),
    'the row gate renders truthful selected-profile limitation treatment');
check(
    app.includes('pane={paneForShell}'),
    'mixed live panes use applicability-safe shell metadata');
check(
    app.includes('const declaredSettingKeys = LIVE_PANE_SECTION_MANIFEST[pane.key]') &&
        app.includes('key => SETTING_METADATA[key]?.scope'),
    'mixed-pane applicability derives from schema manifest keys and ownership metadata');
check(
    /hasDeclaredProfileRows\s*&&\s*hasDeclaredNonProfileRows\s*\?\s*\{\.\.\.pane, scope: 'profile'\}\s*:\s*pane/.test(app),
    'pane-level global applicability stays suppressed when a profile row is missing at runtime');
check(
    app.includes('meta?.selectedProfile,\n      isLegacyProfileRowGuardPane'),
    'selected-profile eligibility distinguishes typed-routed rows from legacy-only rows');

// Four-state contract for allowlisted rows:
// no target => legacy control; host => typed control; ready non-host => typed
// control; unresolved/loading => limitation.
check(
    selectedProfileLegacyRowDisposition('profile', true, true, false, null) === 'control',
    'allowlisted row uses the legacy control when no target is selected');
check(
    selectedProfileLegacyRowDisposition('profile', true, true, true, true) === 'control',
    'allowlisted row uses typed routing for a ready host target');
check(
    selectedProfileLegacyRowDisposition('profile', true, true, true, false) === 'control',
    'allowlisted row uses typed routing for a ready non-host target');
check(
    selectedProfileLegacyRowDisposition('profile', true, true, true, null) === 'limitation' &&
        selectedProfileLegacyRowDisposition('profile', true, true, true, undefined) === 'limitation',
    'allowlisted row is non-actionable while selected context is unresolved or loading');

// Four-state contract for sidebar width:
// no target => legacy; host => legacy; ready non-host => limitation;
// unresolved/loading => limitation.
check(
    selectedProfileLegacyRowDisposition('profile', false, true, false, null) === 'control',
    'sidebar width uses the legacy control when no target is selected');
check(
    selectedProfileLegacyRowDisposition('profile', false, true, true, true) === 'control',
    'sidebar width uses the legacy control for a ready host target');
check(
    selectedProfileLegacyRowDisposition('profile', false, true, true, false) === 'limitation',
    'sidebar width is limited for a ready non-host target');
check(
    selectedProfileLegacyRowDisposition('profile', false, true, true, null) === 'limitation' &&
        selectedProfileLegacyRowDisposition('profile', false, true, true, undefined) === 'limitation',
    'sidebar width is limited while selected context is unresolved or loading');
check(
    selectedProfileLegacyRowDisposition('core-global', false, true, true, false) === 'control',
    'global rows remain available under a non-host target');
check(
    selectedProfileLegacyRowDisposition('profile', false, false, true, false) === 'control',
    'panes outside the legacy partial-pane inventory are not over-gated');
check(
    selectedProfileLegacyRowDisposition(
        'unknown' as SettingScope, false, true, true, false) === 'limitation' &&
        selectedProfileLegacyRowDisposition(undefined, false, true, true, false) === 'limitation',
    'missing or unknown scope metadata fails closed in guarded panes');

console.log('PASS partial-pane profile rows are typed-routed or gated under a non-host selection');
