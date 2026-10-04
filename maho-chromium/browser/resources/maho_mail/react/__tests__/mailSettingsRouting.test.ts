import {describe, expect, it} from 'vitest';

import appSource from '../app.tsx?raw';

describe('Mail settings routing', () => {
  it('routes the sidebar settings action to connected Mail accounts in the current tab', () => {
    expect(appSource).toContain('"mail-accounts"');
    expect(appSource).toContain('window.location.href = `chrome://maho-settings?pane=${pane}`');
    expect(appSource).not.toContain('window.open(`chrome://maho-settings?pane=${pane}`)');
  });
});
