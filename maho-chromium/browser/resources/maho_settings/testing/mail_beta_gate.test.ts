import {describe, expect, test} from 'bun:test';

import {
  LIVE_PANE_SECTION_MANIFEST,
  panesForSettings,
  resolveReachablePaneKey,
} from '../schema/panes.js';
import {SETTING_METADATA} from '../schema/setting_schema.js';

const MAIL_DETAIL_PANE_KEYS = [
  'mail-accounts',
  'mail-signatures',
  'mail-rules',
  'mail-calendar',
  'mail-behavior',
  'mail-security',
];

describe('Maho Mail Beta settings gate', () => {
  test('keeps the top-level Mail pane available while Mail is disabled', () => {
    const disabledPanes = panesForSettings(false);

    expect(disabledPanes.map(pane => pane.key)).toContain('mail');
    expect(disabledPanes.map(pane => pane.key)).not.toContain('features');
    expect(disabledPanes.map(pane => pane.key)).not.toEqual(
        expect.arrayContaining(MAIL_DETAIL_PANE_KEYS));
  });

  test('renders Mail through its own pane instead of a Features live card', () => {
    expect(LIVE_PANE_SECTION_MANIFEST.features).toBeUndefined();
    expect(LIVE_PANE_SECTION_MANIFEST.mail).toBeUndefined();

    const mailPane = panesForSettings(false).find(pane => pane.key === 'mail');
    expect(mailPane).toMatchObject({
      contentKind: 'mail-overview',
      domain: 'mail',
      navTitle: 'Mail',
    });
    expect(mailPane?.groupKeys).toBeUndefined();

    expect(SETTING_METADATA['mail.enabled']).toMatchObject({
      control: 'toggle',
      label: 'Enable Maho Mail (Beta)',
      scope: 'profile',
      selectedProfile: false,
    });
    expect(SETTING_METADATA['ai.mail_read_allowed']).toMatchObject({
      control: 'toggle',
      label: 'Allow AI to read Mail',
      scope: 'profile',
      selectedProfile: false,
    });
  });

  test('redirects legacy Features deep links to the Mail pane', () => {
    expect(resolveReachablePaneKey('features', false)).toBe('mail');
    expect(resolveReachablePaneKey('features', true)).toBe('mail');
  });

  test('reveals exactly the six Mail detail panes when enabled', () => {
    const enabledKeys = panesForSettings(true).map(pane => pane.key);

    expect(enabledKeys).toEqual(expect.arrayContaining(MAIL_DETAIL_PANE_KEYS));
    expect(enabledKeys).toContain('mail');
  });
});
