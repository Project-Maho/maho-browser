#!/usr/bin/env bun

import {readFileSync} from 'node:fs';
import {resolve} from 'node:path';

import {LIVE_PANE_SECTION_MANIFEST} from '../schema/panes.js';
import {SETTING_METADATA} from '../schema/setting_schema.js';

function check(condition: boolean, message: string): void {
  if (!condition) throw new Error(message);
  console.log(`PASS ${message}`);
}

function repoFile(path: string): string {
  return readFileSync(resolve(process.cwd(), path), 'utf8');
}

const expectedRoutedRows = [
  'tabs.archive_timeout',
] as const;
const expectedLimitedRows = [
  'appearance.sidebar_width',
  'advanced.preload_pages',
  'appearance.theme',
] as const;
const partialPaneRows = ['advanced', 'appearance', 'tabs']
    .flatMap(pane => LIVE_PANE_SECTION_MANIFEST[pane] ?? [])
    .flatMap(section => section.card.settingKeys)
    .filter(key => SETTING_METADATA[key]?.scope === 'profile');

check(
    JSON.stringify([...partialPaneRows].sort()) ===
        JSON.stringify([...expectedRoutedRows, ...expectedLimitedRows].sort()),
    'partial panes expose exactly the reviewed profile-scoped row inventory');
for (const key of expectedRoutedRows) {
  check(SETTING_METADATA[key]?.selectedProfile === true, `${key} is selected-profile eligible`);
}
for (const key of expectedLimitedRows) {
  check(SETTING_METADATA[key]?.selectedProfile === false, `${key} remains outside the first-release allowlist`);
}

const store = repoFile('maho-chromium/browser/resources/maho_settings/react/store.ts');
check(store.includes('SELECTED_PROFILE_SETTING_ROUTES'), 'store declares closed selected-profile row routes');
check(!store.includes("'appearance.theme':"), 'theme is managed globally, not routed to a selected profile');
check(!store.includes("'advanced.preload_pages':"), 'preload is managed globally, not routed to a selected profile');
check(store.includes("'tabs.archive_timeout': 'archive'"), 'archive timeout routes to selected profile archive mutation');
check(!store.includes("'appearance.sidebar_width':"), 'sidebar width has no selected-target mutation route');
check(store.includes('commitSelectedProfileSettingValue('), 'commitSettingValue delegates selected-target rows before legacy writes');
check(store.includes('if (this.state.selectedProfileTarget)'), 'active selected targets never fall through to legacy row writes');
check(store.includes('selectedProfileMutationOwners'), 'saving markers are owned by request identity');
check(store.includes('requestMayClearSaving =') &&
    store.includes('this.selectedProfileMutationRequestMatches(target, context)'),
    'saving cleanup uses the snapshot stale-guard inputs');

const app = repoFile('maho-chromium/browser/resources/maho_settings/react/app.tsx');
check(app.includes('meta?.selectedProfile'), 'allowlisted profile rows remain actionable through typed routing');
check(app.includes('selectedProfileLegacyRowDisposition('), 'unsupported profile rows use metadata-driven limitation treatment');
check(app.includes('selectedProfileMutationError?.message'), 'structured selected-target errors render on routed rows');
check(app.includes('ScopeApplicabilityLabel scope={meta.scope}'), 'rows retain per-row applicability labels');
check(app.includes('pane={paneForShell}'), 'mixed panes suppress blanket all-profile shell claims');

console.log('PASS partial-pane profile rows use typed selected-target routing or truthful limitation without legacy fallback');
