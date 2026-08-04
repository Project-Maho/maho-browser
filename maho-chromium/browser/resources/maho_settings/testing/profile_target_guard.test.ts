import {
  selectedProfileLegacyRowDisposition,
  selectedProfilePaneDisposition,
} from '../schema/panes.js';

function check(condition: boolean, message: string): void {
  if (!condition) throw new Error(message);
  console.log(`PASS ${message}`);
}

check(
    selectedProfilePaneDisposition('host-only', false, null) === 'pane',
    'host-only panes preserve normal content when no target is selected');
check(
    selectedProfilePaneDisposition('host-only', true, true) === 'pane',
    'host-only panes preserve normal content for a proven host target');
check(
    selectedProfilePaneDisposition('host-only', true, false) === 'limitation',
    'host-only panes render the limitation for a proven non-host target');
check(
    selectedProfilePaneDisposition('host-only', true, null) === 'limitation' &&
        selectedProfilePaneDisposition('host-only', true, undefined) === 'limitation',
    'host-only panes fail closed while selected context is missing or stale');
for (const support of ['supported', 'partial', 'not-applicable'] as const) {
  check(
      selectedProfilePaneDisposition(support, true, false) === 'pane',
      `${support} panes are not over-guarded at whole-pane level`);
}

check(
    selectedProfileLegacyRowDisposition('profile', false, true, false, null) === 'control',
    'legacy profile rows preserve normal content when no target is selected');
check(
    selectedProfileLegacyRowDisposition('profile', false, true, true, true) === 'control',
    'legacy profile rows preserve normal content for a proven host target');
check(
    selectedProfileLegacyRowDisposition('profile', false, true, true, false) === 'limitation',
    'unsupported legacy profile rows are gated for a proven non-host target');
check(
    selectedProfileLegacyRowDisposition('profile', true, true, true, false) === 'control',
    'allowlisted profile rows remain actionable through typed selected-target routing');
check(
    selectedProfileLegacyRowDisposition('profile', false, true, true, null) === 'limitation' &&
        selectedProfileLegacyRowDisposition('profile', false, true, true, undefined) === 'limitation' &&
        selectedProfileLegacyRowDisposition('profile', true, true, true, null) === 'limitation' &&
        selectedProfileLegacyRowDisposition('profile', true, true, true, undefined) === 'limitation',
    'all profile rows fail closed while selected context is loading or unresolved');
check(
    selectedProfileLegacyRowDisposition('core-global', false, true, true, false) === 'control',
    'legacy global rows remain available under a non-host target');
