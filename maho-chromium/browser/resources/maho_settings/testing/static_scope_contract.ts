import {spawnSync} from 'node:child_process';
import {readFileSync} from 'node:fs';
import {resolve} from 'node:path';

import {
  assertExhaustiveOwnershipMetadata,
  scopeApplicabilityLabel,
} from '../models.js';
import type {SelectedProfileSupport, SettingScope} from '../models.js';
import {ALL_PANE_DEFINITIONS, panesForSettings, resolveReachablePaneKey} from '../schema/panes.js';
import {
  SERVICE_DEFINITIONS,
  SERVICE_METADATA,
  SETTING_DEFINITIONS,
  SETTING_METADATA,
} from '../schema/setting_schema.js';

function check(condition: boolean, message: string): void {
  if (!condition) {
    throw new Error(message);
  }
}

function repoFile(path: string): string {
  return readFileSync(resolve(process.cwd(), path), 'utf8');
}

function unique<T>(values: readonly T[]): T[] {
  return [...new Set(values)];
}

function exposedSettingKeys(): string[] {
  const handler = repoFile('maho-chromium/browser/ui/webui/maho_settings/maho_settings_page_handler.cc');
  const keys = [
    ...handler.matchAll(/\{"([a-z][a-z0-9_-]*\.[a-z0-9_.-]+)"\s*,/g),
    ...handler.matchAll(/add\("([a-z][a-z0-9_-]*\.[a-z0-9_.-]+)"/g),
  ].map(match => match[1]!);
  const namedKeys = [...handler.matchAll(/constexpr char k(?:HardwareAcceleration|MemorySaverMode|MemorySaverTimeout|PasswordProviderMode|PasswordsEnabled|MahoMailEnabled|VaultAutoLockMinutes|VaultDeviceAuthRequired)MahoKey\[\]\s*=\s*"([^"]+)"/g)]
      .map(match => match[1]!);
  return unique([...keys, ...namedKeys]).sort();
}

if (process.argv.includes('--missing-scope-fixture')) {
  assertExhaustiveOwnershipMetadata(
      [{key: 'fixture.missing_scope', selectedProfile: false}],
      'selectedProfile');
  throw new Error('missing-scope fixture unexpectedly passed');
}

const expectedSettingScopes: Readonly<Record<string, SettingScope>> = {
  'advanced.hardware_acceleration': 'device',
  'advanced.memory_saver_mode': 'device',
  'advanced.memory_saver_timeout': 'device',
  'advanced.preload_pages': 'profile',
  'appearance.density': 'core-global',
  'appearance.sidebar_width': 'profile',
  'appearance.theme': 'profile',
  'atc.maho_mini_click_override_enabled': 'process',
  'atc.maho_mini_global_shortcut_enabled': 'process',
  'atc.open_external_links_in_maho_mini': 'process',
  'atc.peek_enabled': 'profile',
  'atc.peek_link_routing_enabled': 'profile',
  'atc.peek_popup_routing_enabled': 'profile',
  'autofill.password_provider': 'core-global',
  'autofill.passwords_enabled': 'core-global',
  'autofill.vault_auto_lock_minutes': 'core-global',
  'autofill.vault_require_device_auth': 'core-global',
  'conversation.auto_archive_after_days': 'core-global',
  'general.homepage': 'profile',
  'general.prompt_for_download': 'profile',
  'general.restore_on_startup': 'profile',
  'general.translate_target_language': 'profile',
  'mail.enabled': 'profile',
  'notifications.quiet_permission_ui': 'profile',
  'privacy.block_third_party_cookies': 'profile',
  'privacy.do_not_track': 'profile',
  'privacy.safe_browsing': 'profile',
  'search.suggestions': 'profile',
  'tabs.archive_timeout': 'profile',
  'tabs.auto_delete_empty_folders_on_tidy': 'core-global',
  'sidebar.new_tab_position': 'core-global',
  'tabs.pinned_close_behavior': 'core-global',
  'tabs.today_tab_timeout': 'core-global',
};

const selectedProfileSettingAllowlist = new Set([
  'general.prompt_for_download',
  'general.translate_target_language',
  'search.suggestions',
  'tabs.archive_timeout',
]);

const expectedServiceInventory = [
  'account.session',
  'autofill.data',
  'billing.subscription',
  'browser.chromium_settings',
  'browser.extensions',
  'browser.history',
  'browser.site_data',
  'content_blocker.engine',
  'downloads.default_directory',
  'passwords.vault',
  'profile.archive_timeout',
  'profile.avatar',
  'profile.name',
  'search.default_engine',
  'shortcuts.recording',
  'updates.browser',
] as const;

const expectedSelectedProfileServices = new Set([
  'downloads.default_directory',
  'profile.archive_timeout',
  'profile.avatar',
  'profile.name',
  'search.default_engine',
]);

const exposedKeys = exposedSettingKeys();
check(exposedKeys.length > 0, 'failed to discover exposed setting keys');
check(
    JSON.stringify(exposedKeys) === JSON.stringify(Object.keys(expectedSettingScopes).sort()),
    `exposed setting inventory changed without ownership metadata:\nactual=${exposedKeys.join(',')}\nexpected=${Object.keys(expectedSettingScopes).sort().join(',')}`,
);
assertExhaustiveOwnershipMetadata(
    SETTING_DEFINITIONS.map(({key, metadata}) => ({key, ...metadata})),
    'selectedProfile');
check(SETTING_DEFINITIONS.length === Object.keys(SETTING_METADATA).length, 'setting metadata keys must be unique');
for (const key of exposedKeys) {
  const metadata = SETTING_METADATA[key];
  check(!!metadata, `exposed setting ${key} lacks metadata`);
  check(metadata.scope === expectedSettingScopes[key], `${key} has scope ${metadata.scope}; expected ${expectedSettingScopes[key]}`);
  check(metadata.selectedProfile === selectedProfileSettingAllowlist.has(key), `${key} selected-profile eligibility drifted`);
}

assertExhaustiveOwnershipMetadata(SERVICE_DEFINITIONS, 'selectedProfile');
check(SERVICE_DEFINITIONS.length === Object.keys(SERVICE_METADATA).length, 'service metadata keys must be unique');
const actualServiceInventory = SERVICE_DEFINITIONS.map(service => service.key).sort();
check(
    JSON.stringify(actualServiceInventory) === JSON.stringify([...expectedServiceInventory].sort()),
    `service inventory changed without an explicit contract update:\nactual=${actualServiceInventory.join(',')}\nexpected=${[...expectedServiceInventory].sort().join(',')}`,
);
for (const service of SERVICE_DEFINITIONS) {
  check(service.selectedProfile === expectedSelectedProfileServices.has(service.key), `${service.key} selected-profile eligibility drifted`);
}
for (const unsupported of [
  'autofill.data',
  'browser.chromium_settings',
  'browser.extensions',
  'browser.history',
  'browser.site_data',
  'passwords.vault',
]) {
  check(SERVICE_METADATA[unsupported]?.selectedProfile === false, `${unsupported} must remain unsupported for non-host targets`);
}

const paneKeys = ALL_PANE_DEFINITIONS.map(pane => pane.key);
check(paneKeys.length === unique(paneKeys).length, 'pane keys must be unique');
assertExhaustiveOwnershipMetadata(ALL_PANE_DEFINITIONS, 'selectedProfileSupport');
const expectedPaneSupport: Readonly<Record<string, SelectedProfileSupport>> = {
  account: 'not-applicable', advanced: 'partial', appearance: 'partial', autofill: 'host-only', billing: 'not-applicable',
  'chromium-settings': 'host-only', 'content-blocker': 'partial', extensions: 'host-only', general: 'supported',
  mail: 'host-only',
  'maho-ai': 'host-only', 'maho-ai-developers': 'host-only', 'maho-mini': 'host-only', notifications: 'supported',
  passwords: 'host-only', profiles: 'partial', 'saved-passwords': 'host-only', shortcuts: 'not-applicable', tabs: 'partial',
  'mail-accounts': 'host-only', 'mail-signatures': 'host-only', 'mail-rules': 'host-only', 'mail-calendar': 'host-only',
  'mail-behavior': 'host-only', 'mail-ai': 'host-only', 'mail-security': 'host-only', import: 'host-only',
};
for (const pane of ALL_PANE_DEFINITIONS) {
  check(pane.selectedProfileSupport === expectedPaneSupport[pane.key], `${pane.key} selected-profile pane support drifted`);
}
check(ALL_PANE_DEFINITIONS.find(pane => pane.key === 'shortcuts')?.scope === 'process', 'shortcut recording must be process scope');
check(ALL_PANE_DEFINITIONS.find(pane => pane.key === 'account')?.scope === 'account', 'account pane must be account scope');
check(ALL_PANE_DEFINITIONS.find(pane => pane.key === 'billing')?.scope === 'account', 'billing pane must be account scope');
check(ALL_PANE_DEFINITIONS.find(pane => pane.key === 'profiles')?.scope === 'core-global', 'profile catalog must be core-global scope');
check(!panesForSettings(false).some(pane => pane.domain === 'mail' && pane.key !== 'mail'), 'Mail panes became reachable while disabled');
check(resolveReachablePaneKey('mail-accounts', false) === 'general', 'disabled Mail deep link must redirect to General');
check(resolveReachablePaneKey('mail-accounts', true) === 'mail-accounts', 'enabled Mail deep link must remain reachable');
check(resolveReachablePaneKey('privacy', false) === 'content-blocker', 'legacy pane alias must remain deterministic');
check(resolveReachablePaneKey('features', false) === 'mail', 'legacy Features deep link must land on the top-level Mail pane');
check(panesForSettings(false).some(pane => pane.key === 'mail'), 'top-level Mail pane must stay reachable while Mail is disabled');

const storeSource = repoFile('maho-chromium/browser/resources/maho_settings/react/store.ts');
check(storeSource.includes('resolvePaneKey(this.state.currentPaneKey, mailEnabled)'), 'bootstrap must resolve the current pane against the Mail gate');
check(storeSource.includes('writePaneToUrl(reachablePaneKey, true)'), 'disabled Mail deep links must replace the URL');

check(scopeApplicabilityLabel('profile') === null, 'profile rows must not claim global applicability');
for (const scope of ['device', 'process', 'account', 'core-global'] as const) {
  check(scopeApplicabilityLabel(scope) === 'Applies to all profiles.', `${scope} must render the global applicability label`);
}

const fixture = spawnSync(
    process.execPath,
    [import.meta.path, '--missing-scope-fixture'],
    {encoding: 'utf8'});
check(fixture.status === 1, `missing-scope fixture exited ${fixture.status}; expected 1`);
check(fixture.stderr.includes('Missing scope for fixture.missing_scope'), `missing-scope fixture did not name the error:\n${fixture.stderr}`);

console.log(`PASS ownership metadata covers ${exposedKeys.length} exposed settings, ${SERVICE_DEFINITIONS.length} services, and ${ALL_PANE_DEFINITIONS.length} panes`);
console.log('PASS exact service inventory includes profile.name, updates.browser, and every required service');
console.log('PASS first-release selected-profile allowlist is closed over rows and services');
console.log('PASS password, Vault, autofill, site data, history, extensions, and Chromium iframe remain host-only');
console.log('PASS disabled Mail direct deep links redirect to General after bootstrap');
console.log('PASS mixed scopes render Applies to all profiles. only for non-profile ownership');
console.log('PASS missing-scope fixture exits 1 and names Missing scope for fixture.missing_scope');
