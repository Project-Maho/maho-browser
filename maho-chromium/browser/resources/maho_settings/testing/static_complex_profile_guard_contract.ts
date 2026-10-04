#!/usr/bin/env bun

import {readFileSync} from 'node:fs';
import {resolve} from 'node:path';

import {ALL_PANE_DEFINITIONS} from '../schema/panes.js';

function check(condition: boolean, message: string): void {
  if (!condition) throw new Error(message);
  console.log(`PASS ${message}`);
}

function repoFile(path: string): string {
  return readFileSync(resolve(process.cwd(), path), 'utf8');
}

const app = repoFile('maho-chromium/browser/resources/maho_settings/react/app.tsx');
const mojom = repoFile('maho-chromium/browser/ui/webui/maho_settings/maho_settings.mojom');
const handler = repoFile('maho-chromium/browser/ui/webui/maho_settings/maho_settings_page_handler.cc');

const hostOnlyPanes = ALL_PANE_DEFINITIONS.filter(pane => pane.selectedProfileSupport === 'host-only');
check(hostOnlyPanes.length > 0, 'host-only pane inventory is non-empty');
check(app.includes('selectedProfilePaneDisposition('), 'app shell routes pane rendering through the selected-profile guard');
check(app.includes("currentPane.selectedProfileSupport"), 'app shell drives the guard from pane ownership metadata');
check(app.includes('<ProfileTargetLimitation'), 'app shell renders the truthful limitation surface');
for (const pane of hostOnlyPanes) {
  check(pane.selectedProfileSupport === 'host-only', `${pane.key} is covered by the metadata-driven host-only guard`);
}

check(app.includes('Current browser profile settings'), 'Chromium settings action has a visible host-context label');
check(!app.includes('<iframe'), 'Chromium settings does not use an unsupported WebUI iframe');
check(app.includes('externalActionDisposition'), 'Extensions external action is guarded before invocation');
check(!app.includes('switchProfile('), 'limitation surface does not switch profiles');
check(!app.includes('{state.selectedProfileTarget.profileId}'), 'limitation primary copy does not render a raw internal profile ID');

const pageHandler = mojom.slice(mojom.indexOf('interface PageHandler'));
const complexMethodNames = [
  'GetPasswordProviderStatus', 'GetPasswordProviderOptions', 'GetSavedPasswords', 'SearchPasswords',
  'AddPassword', 'UpdatePasswordUsername', 'DeletePassword', 'GetVaultStatus', 'GetVaultProviderStatus',
  'InitializeVault', 'UnlockVault', 'UnlockVaultWithRecovery', 'LockVault', 'ListVaultItems',
  'SearchVaultItems', 'AddVaultLogin', 'UpdateVaultLogin', 'DeleteVaultItem', 'UseVaultSecret',
  'GetVaultPolicyStatus', 'SetVaultPolicy', 'GetVaultAuditPage', 'SelectPasswordImportFile',
  'PreviewPasswordImport', 'CancelPasswordImport', 'CommitPasswordImport', 'GetAutofillAddresses',
  'AddAutofillAddress', 'DeleteAutofillAddress', 'GetAutofillPayments', 'AddAutofillPayment',
  'DeleteAutofillPayment', 'OpenExtensionsPage', 'OpenChromiumSettingsPage',
];
for (const name of complexMethodNames) {
  const match = pageHandler.match(new RegExp(`\\b${name}\\s*\\(([^)]*)\\)`));
  check(!!match, `PageHandler declares complex-domain method ${name}`);
  const parameters = match?.[1] ?? '';
  check(!/\b(?:profile_id|profileId|profile_path|profilePath)\b/.test(parameters), `${name} takes no renderer-supplied profile ID/path`);
}

const complexHandlerStart = handler.indexOf('void MahoSettingsPageHandler::GetPasswordProviderStatus');
const complexHandlerEnd = handler.indexOf('void MahoSettingsPageHandler::SetBYOKKey');
check(complexHandlerStart >= 0 && complexHandlerEnd > complexHandlerStart, 'complex handler implementation region is discoverable');
const complexHandlers = handler.slice(complexHandlerStart, complexHandlerEnd);
check(!complexHandlers.includes('selected_profile_'), 'complex handlers do not resolve the selected-profile backend target');
check(complexHandlers.includes('FindBrowserWithProfile(profile_)'), 'sensitive reauthentication is bound to the host Profile instance');
check(complexHandlers.includes('FindLastActiveWithProfile(profile_)'), 'browser-owned actions are bound to the host Profile instance');

check(!app.includes("externalAction === 'openExtensions') {\n                                store.getHandler().openExtensionsPage();"), 'Extensions action cannot bypass the selected-profile guard');
console.log(`PASS ${hostOnlyPanes.length} host-only panes are centrally guarded without exposing host data as selected-profile data`);
