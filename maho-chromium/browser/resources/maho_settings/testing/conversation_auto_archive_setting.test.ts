import {describe, expect, test} from 'bun:test';
import {readFileSync} from 'node:fs';
import {resolve} from 'node:path';

import {LIVE_PANE_SECTION_MANIFEST} from '../schema/panes.js';
import {
  CONVERSATION_AUTO_ARCHIVE_OPTIONS,
  SETTING_METADATA,
} from '../schema/setting_schema.js';

const SETTING_KEY = 'conversation.auto_archive_after_days';

function repoFile(path: string): string {
  return readFileSync(resolve(import.meta.dir, '..', '..', '..', '..', '..', path), 'utf8');
}

const handlerSource = repoFile(
    'maho-chromium/browser/ui/webui/maho_settings/maho_settings_page_handler.cc');

describe('conversation auto-archive setting', () => {
  test('exposes only Off, 3, 7, and 30 days as a core-global select', () => {
    expect(CONVERSATION_AUTO_ARCHIVE_OPTIONS).toEqual([
      {value: '-1', label: 'Off'},
      {value: '3', label: '3 days'},
      {value: '7', label: '7 days'},
      {value: '30', label: '30 days'},
    ]);
    expect(SETTING_METADATA[SETTING_KEY]).toMatchObject({
      scope: 'core-global',
      selectedProfile: false,
      control: 'select',
      options: CONVERSATION_AUTO_ARCHIVE_OPTIONS,
    });
  });

  test('is owned by the Maho AI pane', () => {
    const owningSection = LIVE_PANE_SECTION_MANIFEST['maho-ai']?.find(
        section => section.card.settingKeys.includes(SETTING_KEY));
    expect(owningSection?.card.settingKeys).toEqual([SETTING_KEY]);
  });

  test('routes reads and writes through the dedicated policy FFI', () => {
    expect(handlerSource).toContain(
        'maho_core_get_conversation_auto_archive_policy(core)');
    expect(handlerSource).toContain(
        'maho_core_set_conversation_auto_archive_policy(core, days)');
    expect(handlerSource).toContain(`add("${SETTING_KEY}",`);
  });

  test('maps an absent or disabled policy to the Off sentinel', () => {
    expect(handlerSource).toContain(
        'kConversationAutoArchiveAfterDaysMahoKey');
    expect(handlerSource).toContain('base::NumberToString(policy < 0 ? -1 : policy)');
  });
});
