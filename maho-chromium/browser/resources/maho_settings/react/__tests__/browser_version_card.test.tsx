import {act} from 'react';
import {expect, it, vi} from 'vitest';
import {MahoSettingsStore} from '../store.js';
import {BrowserUpdateState} from '../../mojo.js';

it('renders Store guidance and keeps checking available without offering install', {timeout: 30000}, async () => {
  Object.assign(globalThis, {IS_REACT_ACT_ENVIRONMENT: true});
  vi.stubGlobal('matchMedia', () => ({
    matches: false, addEventListener() {}, removeEventListener() {},
  }));
  vi.useFakeTimers();
  const userAgent = vi.spyOn(navigator, 'userAgent', 'get')
      .mockReturnValue('Mozilla/5.0 (Windows NT 10.0; Win64; x64)');
  const check = vi.fn();
  let store: MahoSettingsStore | undefined;
  const info = {
    version: '1.0.0', channel: 'Stable',
    updateState: BrowserUpdateState.kUpdateAvailable,
    updateSupported: true, updateGuidance: 'external-update',
  };
  const bootstrap = vi.spyOn(MahoSettingsStore.prototype, 'bootstrap')
      .mockImplementation(async function(this: MahoSettingsStore) {
        store = this;
        Object.assign(this.getHandler(), {
          getBrowserVersionInfo: async () => ({info}),
          checkForBrowserUpdates: check,
        });
        await this.refreshBrowserVersionInfo();
      });
  const host = document.createElement('maho-settings-app');
  document.body.append(host);
  try {
    // The app bootstraps on import and needs the root to exist first; the import
    // itself pulls the whole module graph (~4s), hence this test's raised timeout.
    await act(async () => { await import('../app.js'); });
    await act(async () => { vi.advanceTimersToNextFrame(); });
    const link = host.querySelector<HTMLAnchorElement>(
        'a[href="https://apps.microsoft.com/detail/9NF9QS2TJFXT"]');
    expect(link).not.toBeNull();
    expect(link?.parentElement?.textContent).toContain(info.updateGuidance);
    expect(link?.rel).toBe('noopener noreferrer');
    const checkButton = Array.from(host.querySelectorAll('button'))
        .find(button => button.textContent === 'Check for updates');
    expect(checkButton).toBeDefined();
    expect(checkButton?.disabled).toBe(false);
    expect(host.textContent).not.toContain('Restart to update');
    await act(async () => {
      checkButton?.click();
      vi.advanceTimersToNextFrame();
    });
    expect(check).toHaveBeenCalledOnce();
    expect(host.querySelector('a[href^="https://apps.microsoft.com/"]')).toBeNull();
    expect(host.textContent).toContain('Checking for updates');
    info.updateState = BrowserUpdateState.kUpToDate;
    await act(async () => {
      await store?.refreshBrowserVersionInfo();
      vi.advanceTimersToNextFrame();
    });
    expect(host.textContent).toContain('Up to date');
    expect(host.querySelector('a[href^="https://apps.microsoft.com/"]')).toBeNull();

    // Linux also receives guidance (package/archive installs), but must never
    // be sent to the Microsoft Store.
    userAgent.mockReturnValue('Mozilla/5.0 (X11; Linux x86_64)');
    info.updateState = BrowserUpdateState.kUpdateAvailable;
    info.updateGuidance = 'linux-package-guidance';
    await act(async () => {
      await store?.refreshBrowserVersionInfo();
      vi.advanceTimersToNextFrame();
    });
    expect(host.textContent).toContain('linux-package-guidance');
    expect(host.querySelector('a[href^="https://apps.microsoft.com/"]')).toBeNull();
  } finally {
    userAgent.mockRestore();
    store?.dispose();
    bootstrap.mockRestore();
    vi.useRealTimers();
    vi.unstubAllGlobals();
    host.remove();
  }
});
