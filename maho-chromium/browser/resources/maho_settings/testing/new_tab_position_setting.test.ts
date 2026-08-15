import {describe, expect, test} from 'bun:test';
import {readFileSync} from 'node:fs';
import {resolve} from 'node:path';

import {LIVE_PANE_SECTION_MANIFEST} from '../schema/panes.js';
import {
  NEW_TAB_POSITION_OPTIONS,
  SETTING_METADATA,
} from '../schema/setting_schema.js';

const SETTING_KEY = 'sidebar.new_tab_position';

function repoFile(path: string): string {
  return readFileSync(resolve(import.meta.dir, '..', '..', '..', '..', '..', path), 'utf8');
}

const handlerSource = repoFile(
    'maho-chromium/browser/ui/webui/maho_settings/maho_settings_page_handler.cc');

describe('new-tab position setting', () => {
  test('exposes exactly the two contract values as a select row', () => {
    expect(NEW_TAB_POSITION_OPTIONS.map(option => option.value)).toEqual([
      'top',
      'bottom',
    ]);
    expect(SETTING_METADATA[SETTING_KEY]).toMatchObject({
      // maho-core decides the insertion index, so the value is core-global and
      // never a per-profile Chromium pref.
      scope: 'core-global',
      selectedProfile: false,
      control: 'select',
      options: NEW_TAB_POSITION_OPTIONS,
    });
  });

  test('renders in the Tabs pane ahead of the lifecycle rows', () => {
    const tabsSections = LIVE_PANE_SECTION_MANIFEST.tabs!;
    const owningSection = tabsSections.find(
        section => section.card.settingKeys.includes(SETTING_KEY));
    expect(owningSection).toBeDefined();
    expect(owningSection!.card.settingKeys).toEqual([SETTING_KEY]);
    expect(tabsSections.indexOf(owningSection!)).toBe(0);
  });

  test('is not mapped to a Chromium pref', () => {
    // A kSettingsMap entry would make the toggle write a pref that nothing
    // reads, which is exactly the class of defect this routing avoids.
    expect(handlerSource).not.toContain(`{"${SETTING_KEY}", prefs::`);
    expect(handlerSource).not.toContain(`{"${SETTING_KEY}", maho::sidebar_prefs::`);
  });

  test('writes and reads the core settings field', () => {
    expect(handlerSource).toContain('general.Set("newTabPosition", value);');
    expect(handlerSource).toContain(
        'general->FindString("newTabPosition")');
    expect(handlerSource).toContain(`add("${SETTING_KEY}", *new_tab_position);`);
  });

  test('rejects values outside the contract before touching core', () => {
    const branch = handlerSource.slice(
        handlerSource.indexOf(`} else if (key == "${SETTING_KEY}") {`));
    const rejection = branch.indexOf('return FfiResult::kInvalid;');
    const coreWrite = branch.indexOf('maho_core_update_settings');
    expect(rejection).toBeGreaterThan(-1);
    expect(rejection).toBeLessThan(coreWrite);
  });
});
