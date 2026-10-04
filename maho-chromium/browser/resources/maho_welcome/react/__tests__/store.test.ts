// Copyright 2026 Maho Browser. All rights reserved.

import {beforeEach, describe, expect, it, vi} from 'vitest';
import {
  IMPORT_BOOKMARKS,
  IMPORT_HISTORY,
  IMPORT_PASSWORDS,
} from '../../maho_welcome.mojom-webui.js';
import { WelcomePage } from '../types.js';
const IMPORT_SEARCH_ENGINES = 0;

const mockImportProgress = vi.hoisted(() => ({
  listener: undefined as
      | ((type: number, itemsImported: number, error: string, complete: boolean) => void)
      | undefined,
}));

const mockHandlerInstance = vi.hoisted(() => ({
  $: {bindNewPipeAndPassReceiver: vi.fn()},
  getAvailableBrowsers: vi.fn().mockResolvedValue({browsers: []}),
  startImport: vi.fn(),
  isDefaultBrowser: vi.fn().mockResolvedValue({isDefault: false}),
  setAsDefaultBrowser: vi.fn(),
  getSearchEngines: vi.fn().mockResolvedValue({
    engines: [
      {keyword: 'google', name: 'Google', iconUrl: '', isDefault: true},
      {keyword: 'ddg', name: 'DuckDuckGo', iconUrl: '', isDefault: false},
      {keyword: 'bing', name: 'Bing', iconUrl: '', isDefault: false},
    ],
  }),
  setDefaultSearchEngine: vi.fn(),
  getEssentialSites: vi.fn().mockResolvedValue({
    sites: [
      {url: 'https://maho.dev', name: 'Maho', iconPath: ''},
      {url: 'https://example.com', name: 'Example', iconPath: ''},
    ],
  }),
  favoriteEssentialSites: vi.fn(),
  previewTheme: vi.fn(),
  applyTheme: vi.fn(),
  clearThemePreview: vi.fn(),
  openMigrationDialog: vi.fn().mockResolvedValue({result: {wasCancelled: false, importedItemsBitmask: 0}}),
  getLocalizedStrings: vi.fn().mockResolvedValue({strings: {}}),
  loginFromWelcome: vi.fn().mockResolvedValue({ok: true, errorMessage: ''}),
  signupFromWelcome: vi.fn().mockResolvedValue({ok: true, errorMessage: ''}),
  getRelayAccountStatusFromWelcome:
      vi.fn().mockResolvedValue({signedIn: false, tier: '', subscriptionStatus: '', welcomeCompleted: false, hasStoredSession: false}),
  setTranslationProvider: vi.fn().mockResolvedValue({ok: true}),
  getByokCredentialConfigured: vi.fn().mockResolvedValue({configured: false}),
  openByokSettingsDialog: vi.fn().mockResolvedValue({configured: false}),
  saveSyncKeyBackup: vi.fn(),
  generateSyncKey: vi.fn(),
  setPasswordProvider: vi.fn(),
  getSubscriptionCheckoutUrl: vi.fn(),
  finishOnboarding: vi.fn(),
}));

const mockCallbackRouterInstance = vi.hoisted(() => ({
  $: {bindNewPipeAndPassRemote: vi.fn()},
  onImportProgress: {
    addListener: vi.fn(
        (listener: (type: number, itemsImported: number, error: string, complete: boolean) => void) => {
          mockImportProgress.listener = listener;
        }),
  },
}));

vi.mock('../../maho_welcome.mojom-webui.js', () => ({
  PageCallbackRouter: vi.fn().mockImplementation(() => mockCallbackRouterInstance),
  PageHandlerRemote: vi.fn().mockImplementation(() => mockHandlerInstance),
  PageHandlerFactory: {
    getRemote: vi.fn().mockReturnValue({
      createPageHandler: vi.fn(),
    }),
  },
  IMPORT_BOOKMARKS: 0x01,
  IMPORT_PASSWORDS: 0x02,
  IMPORT_HISTORY: 0x04,
  IMPORT_COOKIES: 0x08,
  IMPORT_AUTOFILL: 0x10,
  IMPORT_WORKSPACES: 0x20,
  IMPORT_FAVICONS: 0x100,
}));

vi.mock('//resources/maho_common/react/store_utils.js', () => ({
  notifyListeners: (listeners: Set<() => void>) => {
    for (const listener of listeners) {
      listener();
    }
  },
}));

const mockLemon = vi.hoisted(() => ({
  overlay: vi.fn(async (_opts: {
    checkoutUrl: string;
    onSuccess?: (orderId: string) => void;
    onClose?: () => void;
    onError?: (error: Error) => void;
  }) => {}),
}));

vi.mock('../../../maho_common/react/lemonsqueezy.js', () => ({
  openCheckoutOverlay: mockLemon.overlay,
  loadLemonJS: vi.fn().mockResolvedValue(undefined),
}));

describe('MahoWelcomeStore', () => {
  let MahoWelcomeStore: typeof import('../store.js').MahoWelcomeStore;

  async function selectByok(store: import('../store.js').MahoWelcomeStore) {
    store.setAuthEmail('byok-fixture@example.test');
    store.setAuthPassword('test-only-password');
    mockHandlerInstance.getRelayAccountStatusFromWelcome.mockResolvedValueOnce({
      signedIn: true, tier: 'free', subscriptionStatus: '',
      welcomeCompleted: false, hasStoredSession: false,
    });
    await store.submitSignIn();
    for (let step = 0; step < 7; step++) store.nextPage();
    expect(store.getSnapshot().currentPage).toBe(WelcomePage.AIProvider);
    store.setAiProvider('byok');
    await store.checkByokConfigured();
  }

  beforeEach(async () => {
    vi.clearAllMocks();
    mockImportProgress.listener = undefined;
    mockHandlerInstance.getAvailableBrowsers.mockResolvedValue({
      browsers: [
        {
          index: 0,
          name: 'Google Chrome',
          servicesSupported:
              IMPORT_HISTORY | IMPORT_BOOKMARKS | IMPORT_PASSWORDS |
              IMPORT_SEARCH_ENGINES,
        },
        {
          index: 1,
          name: 'Safari',
          servicesSupported: IMPORT_HISTORY | IMPORT_BOOKMARKS,
        },
      ],
    });
    mockHandlerInstance.getSearchEngines.mockResolvedValue({
      engines: [
        {keyword: 'google', name: 'Google', iconUrl: '', isDefault: true},
        {keyword: 'ddg', name: 'DuckDuckGo', iconUrl: '', isDefault: false},
        {keyword: 'bing', name: 'Bing', iconUrl: '', isDefault: false},
      ],
    });
    mockHandlerInstance.getEssentialSites.mockResolvedValue({
      sites: [
        {url: 'https://maho.dev', name: 'Maho', iconPath: ''},
        {url: 'https://example.com', name: 'Example', iconPath: ''},
      ],
    });
    mockHandlerInstance.getRelayAccountStatusFromWelcome.mockResolvedValue({
      signedIn: false,
      tier: '',
      subscriptionStatus: '',
      welcomeCompleted: false,
      hasStoredSession: false,
    });
    const mod = await import('../store.js');
    MahoWelcomeStore = mod.MahoWelcomeStore;
  });

  it('starts with Zen import/default state defaults', () => {
    const store = new MahoWelcomeStore();
    const state = store.getSnapshot();
    expect(state.currentPage).toBe(WelcomePage.Auth);
    expect(state.direction).toBe('forward');
    expect(state.availableBrowsers).toEqual([]);
    expect(state.importState).toEqual(
        {phase: 'idle', statusMessage: '', browserIndex: null});
    expect(state.importItemSelections.size).toBe(0);
    expect(state.setAsDefaultRequested).toBe(false);
    expect(state.importStarted).toBe(false);
    expect(state.importStage).toBe('A');
    expect(state.searchEngines).toEqual([]);
    expect(state.selectedEngine).toBeNull();
    expect(state.essentialSites).toEqual([]);
    expect(state.selectedEssentials.size).toBe(0);
    expect(state.selectedTheme).toBeNull();
  });

  it.each([true, false])('returns native backup save result %s', async saved => {
    const store = new MahoWelcomeStore();
    mockHandlerInstance.saveSyncKeyBackup.mockResolvedValueOnce({
      saved, errorMessage: '',
    });
    try {
      await expect(store.saveSyncKeyBackup('fixture-backup')).resolves.toBe(saved);
      expect(mockHandlerInstance.saveSyncKeyBackup)
          .toHaveBeenCalledWith('fixture-backup');
      expect(store.getSnapshot().syncKeyBackup.savedToFile).toBe(false);
    } finally {
      store.dispose();
    }
  });

  it('propagates native backup write errors and permits a subsequent request', async () => {
    const store = new MahoWelcomeStore();
    mockHandlerInstance.saveSyncKeyBackup
        .mockResolvedValueOnce({saved: false, errorMessage: 'fixture_write_failed'})
        .mockResolvedValueOnce({saved: true, errorMessage: ''});
    try {
      await expect(store.saveSyncKeyBackup('fixture-backup'))
          .rejects.toThrow('fixture_write_failed');
      expect(store.getSnapshot().syncKeyBackup.savedToFile).toBe(false);
      await expect(store.saveSyncKeyBackup('fixture-backup')).resolves.toBe(true);
      expect(mockHandlerInstance.saveSyncKeyBackup).toHaveBeenCalledTimes(2);
    } finally {
      store.dispose();
    }
  });

  it.each([false, true])(
      'gates backup completion and scrubs recovery state only when saved=%s', async saved => {
    const store = new MahoWelcomeStore();
    const generated = {
      syncKey: 'fixture-sync-key',
      roomId: 'fixture-room',
      recoveryPhrase: 'fixture recovery phrase',
    };
    mockHandlerInstance.setPasswordProvider.mockResolvedValueOnce({success: true});
    mockHandlerInstance.generateSyncKey.mockResolvedValueOnce(generated);
    try {
      await selectByok(store);
      mockHandlerInstance.getByokCredentialConfigured.mockResolvedValueOnce({
        configured: true,
      });
      await store.checkByokConfigured();
      // Account escrow: the default flow skips the vault ceremony, so the page
      // is entered through its dedicated navigation before the skip path.
      store.navigateToPasswordSetup();
      expect(store.getSnapshot().currentPage).toBe(WelcomePage.PasswordSetup);
      await expect(store.skipPasswordSetup()).resolves.toBe(true);
      await expect(store.generateSyncKey()).resolves.toEqual(generated);
      store.setSyncKeyBackupSavedToFile(saved);

      store.nextPage();
      expect(store.getSnapshot().currentPage).toBe(WelcomePage.SyncKeyBackup);
      expect(store.advanceFromSyncKeyBackup()).toBe(saved);

      const result = store.getSnapshot();
      expect(result.currentPage).toBe(
          saved ? WelcomePage.Completion : WelcomePage.SyncKeyBackup);
      expect(result.syncKeyBackup.savedToFile).toBe(saved);
      expect(result.syncKeyBackup.syncKey).toBe(saved ? null : generated.syncKey);
      expect(result.syncKeyBackup.roomId).toBe(saved ? null : generated.roomId);
      expect(result.syncKeyBackup.recoveryPhrase)
          .toBe(saved ? null : generated.recoveryPhrase);
      expect(result.syncKeyBackup.status).toBe(saved ? 'idle' : 'error');
    } finally {
      store.dispose();
    }
  });

  it('advances through all pages in the flow once signed in', async () => {
    const store = new MahoWelcomeStore();
    // Simulate successful sign-in
    store.setAuthEmail('user@maho.dev');
    store.setAuthPassword('securepwd');
    mockHandlerInstance.loginFromWelcome.mockResolvedValueOnce({ok: true, errorMessage: ''});
    mockHandlerInstance.getRelayAccountStatusFromWelcome.mockResolvedValueOnce({
      signedIn: true,
      tier: 'pro',
      subscriptionStatus: 'active',
      welcomeCompleted: false,
    });

    const expectedPages = [
      WelcomePage.Splash,
      WelcomePage.Appearance,
      WelcomePage.SourceSelect,
      WelcomePage.ImportProgress,
      WelcomePage.SearchEngine,
      WelcomePage.Essentials,
      WelcomePage.AIProvider,
      WelcomePage.Completion,
    ];

    // Cannot advance without sign in
    store.nextPage();
    expect(store.getSnapshot().currentPage).toBe(WelcomePage.Auth);

    // Perform sign in
    await store.submitSignIn();

    // Verify it can now advance
    for (const expected of expectedPages) {
      store.nextPage();
      expect(store.getSnapshot().currentPage).toBe(expected);
    }
  });

  it('returns Managed AI guests to plan selection after authentication', async () => {
    const store = new MahoWelcomeStore();
    (store as unknown as {patch: (mutator: (draft: Record<string, unknown>) => void) => void}).
        patch(draft => {
          draft.currentPage = WelcomePage.AIProvider;
          draft.aiSetup = {
            provider: 'maho-managed',
            status: 'success',
            errorMessage: '',
            skipped: false,
          };
        });

    store.nextPage();
    expect(store.getSnapshot().currentPage).toBe(WelcomePage.Auth);

    store.setAuthEmail('guest@maho.dev');
    store.setAuthPassword('securepwd');
    mockHandlerInstance.loginFromWelcome.mockResolvedValueOnce({ok: true, errorMessage: ''});
    mockHandlerInstance.getRelayAccountStatusFromWelcome.mockResolvedValueOnce({
      signedIn: true,
      tier: 'free',
      subscriptionStatus: '',
      welcomeCompleted: false,
      hasStoredSession: false,
    });
    await store.submitSignIn();
    store.nextPage();

    expect(store.getSnapshot().currentPage).toBe(WelcomePage.PlanSelect);
  });

  it('keeps BYOK selection on AI setup until credentials are configured', async () => {
    const store = new MahoWelcomeStore();
    store.setAuthEmail('byok-fixture@example.test');
    store.setAuthPassword('test-only-password');
    mockHandlerInstance.getRelayAccountStatusFromWelcome.mockResolvedValueOnce({
      signedIn: true,
      tier: 'free',
      subscriptionStatus: '',
      welcomeCompleted: false,
      hasStoredSession: false,
    });
    await store.submitSignIn();
    for (let step = 0; step < 7; step++) {
      store.nextPage();
    }
    expect(store.getSnapshot().currentPage).toBe(WelcomePage.AIProvider);

    store.setAiProvider('byok');

    expect(store.getSnapshot().currentPage).toBe(WelcomePage.AIProvider);
    expect(store.getSnapshot().aiSetup.status).not.toBe('success');
    store.nextPage();
    expect(store.getSnapshot().currentPage).toBe(WelcomePage.AIProvider);
  });

  it('keeps BYOK Next blocked when the dialog reports unconfigured credentials', async () => {
    const store = new MahoWelcomeStore();
    await selectByok(store);
    mockHandlerInstance.openByokSettingsDialog.mockResolvedValueOnce({configured: false});

    await store.openByokSettings();

    expect(mockHandlerInstance.openByokSettingsDialog).toHaveBeenCalledTimes(1);
    expect(store.getSnapshot().aiSetup.status).toBe('idle');
    expect(store.getSnapshot().currentPage).toBe(WelcomePage.AIProvider);
    store.nextPage();
    expect(store.getSnapshot().currentPage).toBe(WelcomePage.AIProvider);
  });

  it('unblocks BYOK Next only after the dialog confirms configured credentials', async () => {
    const store = new MahoWelcomeStore();
    await selectByok(store);
    mockHandlerInstance.openByokSettingsDialog.mockResolvedValueOnce({configured: true});

    await store.openByokSettings();

    expect(store.getSnapshot().aiSetup.status).toBe('success');
    store.nextPage();
    expect(store.getSnapshot().currentPage).toBe(WelcomePage.Completion);
  });

  it('reports BYOK setup failed when the dialog request rejects and keeps Next blocked', async () => {
    const store = new MahoWelcomeStore();
    await selectByok(store);
    mockHandlerInstance.openByokSettingsDialog.mockRejectedValueOnce(new Error('dialog failed'));

    await store.openByokSettings();

    expect(store.getSnapshot().aiSetup.status).toBe('error');
    expect(store.getSnapshot().aiSetup.errorMessage).toBe('dialog failed');
    store.nextPage();
    expect(store.getSnapshot().currentPage).toBe(WelcomePage.AIProvider);
  });

  it('reflects an already-configured BYOK selection when readiness is re-checked', async () => {
    const store = new MahoWelcomeStore();
    await selectByok(store);
    mockHandlerInstance.getByokCredentialConfigured.mockResolvedValueOnce({configured: true});

    await store.checkByokConfigured();

    expect(store.getSnapshot().aiSetup.status).toBe('success');
    store.nextPage();
    expect(store.getSnapshot().currentPage).toBe(WelcomePage.Completion);
  });

  it('drops a stale BYOK credential result after the user switches provider', async () => {
    const store = new MahoWelcomeStore();
    await selectByok(store);
    mockHandlerInstance.getByokCredentialConfigured.mockResolvedValueOnce({configured: false});

    const inFlight = store.checkByokConfigured();
    store.setAiProvider('maho-managed');
    await inFlight;

    // The stale unconfigured reply must not clobber the newly selected
    // managed provider's success state.
    expect(store.getSnapshot().aiSetup.provider).toBe('maho-managed');
    expect(store.getSnapshot().aiSetup.status).toBe('success');
  });

  it('ignores a superseded BYOK check reply that resolves after a newer one', async () => {
    const store = new MahoWelcomeStore();
    await selectByok(store);
    const stale: {resolve?: (value: {configured: boolean}) => void} = {};
    mockHandlerInstance.getByokCredentialConfigured
        .mockImplementationOnce(
            () => new Promise<{configured: boolean}>(resolve => {
              stale.resolve = resolve;
            }))
        .mockImplementationOnce(() => Promise.resolve({configured: true}));

    const staleFlight = store.checkByokConfigured();
    const freshFlight = store.checkByokConfigured();
    await freshFlight;
    if (!stale.resolve) throw new Error('Stale check was not started');
    stale.resolve({configured: false});
    await staleFlight;

    // The superseded false reply lands last and must not un-verify the
    // newer confirmed state.
    expect(store.getSnapshot().aiSetup.status).toBe('success');
    store.nextPage();
    expect(store.getSnapshot().currentPage).toBe(WelcomePage.Completion);
  });

  it('ignores a stale BYOK check reply that lands while the settings dialog is open', async () => {
    const store = new MahoWelcomeStore();
    await selectByok(store);
    const stale: {resolve?: (value: {configured: boolean}) => void} = {};
    mockHandlerInstance.getByokCredentialConfigured.mockImplementationOnce(
        () => new Promise<{configured: boolean}>(resolve => {
          stale.resolve = resolve;
        }));
    mockHandlerInstance.openByokSettingsDialog.mockResolvedValueOnce({configured: false});

    const staleCheck = store.checkByokConfigured();
    await store.openByokSettings();
    if (!stale.resolve) throw new Error('Stale check was not started');
    stale.resolve({configured: true});
    await staleCheck;

    // The pre-dialog check reply must not unlock Next behind the dialog's
    // unconfigured close result.
    expect(store.getSnapshot().aiSetup.status).toBe('idle');
    store.nextPage();
    expect(store.getSnapshot().currentPage).toBe(WelcomePage.AIProvider);
  });

  it('continues from the Free plan without opening checkout', () => {
    const store = new MahoWelcomeStore();
    (store as unknown as {patch: (mutator: (draft: Record<string, unknown>) => void) => void}).
        patch(draft => {
          draft.currentPage = WelcomePage.PlanSelect;
        });

    store.continueWithFreePlan();

    expect(store.getSnapshot().currentPage).toBe(WelcomePage.Completion);
    expect(mockHandlerInstance.getSubscriptionCheckoutUrl).not.toHaveBeenCalled();
  });

  it('goes backward without leaving Splash', async () => {
    const store = new MahoWelcomeStore();
    // Set authenticated so we can advance
    store.setAuthEmail('user@maho.dev');
    store.setAuthPassword('securepwd');
    mockHandlerInstance.loginFromWelcome.mockResolvedValueOnce({ok: true, errorMessage: ''});
    mockHandlerInstance.getRelayAccountStatusFromWelcome.mockResolvedValueOnce({
      signedIn: true,
      tier: 'pro',
      subscriptionStatus: 'active',
      welcomeCompleted: false,
    });
    await store.submitSignIn();

    store.nextPage(); // Auth -> Splash
    store.prevPage(); // Splash -> Splash (cannot go back to Auth)
    expect(store.getSnapshot().currentPage).toBe(WelcomePage.Splash);

    store.nextPage(); // Splash -> Appearance
    store.nextPage(); // Appearance -> SourceSelect
    store.nextPage(); // SourceSelect -> ImportProgress
    store.prevPage(); // ImportProgress -> SourceSelect
    expect(store.getSnapshot().currentPage).toBe(WelcomePage.SourceSelect);
    expect(store.getSnapshot().direction).toBe('backward');
  });

  it('bootstraps browsers, search engines, essentials, and import defaults', async () => {
    const store = new MahoWelcomeStore();
    await store.bootstrap();
    const state = store.getSnapshot();

    expect(state.availableBrowsers).toHaveLength(2);
    expect(state.availableBrowsers[0].name).toBe('Google Chrome');
    expect(state.importItemSelections.get(0)).toBe(
        IMPORT_HISTORY | IMPORT_BOOKMARKS | IMPORT_PASSWORDS);
    expect(state.importItemSelections.get(1)).toBe(
        IMPORT_HISTORY | IMPORT_BOOKMARKS);
    expect(state.searchEngines).toHaveLength(3);
    expect(state.selectedEngine).toBe('google');
    expect(state.essentialSites).toHaveLength(2);
  });

  it('bootstrap derives isReauthMode and hasStoredSession across the three flows', async () => {
    const flows = [
      {welcomeCompleted: false, hasStoredSession: false},
      {welcomeCompleted: true, hasStoredSession: false},
      {welcomeCompleted: true, hasStoredSession: true},
    ];

    for (const flow of flows) {
      mockHandlerInstance.getRelayAccountStatusFromWelcome.mockResolvedValueOnce({
        signedIn: false,
        tier: '',
        subscriptionStatus: '',
        welcomeCompleted: flow.welcomeCompleted,
        hasStoredSession: flow.hasStoredSession,
      });

      const store = new MahoWelcomeStore();
      await store.bootstrap();
      const {auth} = store.getSnapshot();

      expect(auth.isReauthMode).toBe(flow.welcomeCompleted);
      expect(auth.hasStoredSession).toBe(flow.hasStoredSession);
    }
  });

  it('stores and applies the default-browser radio choice', () => {
    const store = new MahoWelcomeStore();

    store.applyDefaultBrowserChoice();
    expect(mockHandlerInstance.setAsDefaultBrowser).not.toHaveBeenCalled();

    store.setSetAsDefault(true);
    expect(store.getSnapshot().setAsDefaultRequested).toBe(true);
    store.applyDefaultBrowserChoice();
    expect(mockHandlerInstance.setAsDefaultBrowser).toHaveBeenCalledTimes(1);
  });

  it('refreshes completed onboarding after a returning account signs in', async () => {
    const store = new MahoWelcomeStore();
    await store.bootstrap();
    expect(store.getSnapshot().auth.isReauthMode).toBe(false);
    store.setAuthEmail('returning-fixture@example.test');
    store.setAuthPassword('test-only-password');
    mockHandlerInstance.getRelayAccountStatusFromWelcome.mockResolvedValueOnce({
      signedIn: true,
      tier: 'free',
      subscriptionStatus: '',
      welcomeCompleted: true,
      hasStoredSession: true,
    });

    await store.submitSignIn();

    expect(store.getSnapshot().auth.isReauthMode).toBe(true);
    expect(store.getSnapshot().auth.hasStoredSession).toBe(true);
  });

  it('marks when import is started', () => {
    const store = new MahoWelcomeStore();
    store.markImportStarted();
    expect(store.getSnapshot().importStarted).toBe(true);
  });

  it('toggles selected import items in state', async () => {
    const store = new MahoWelcomeStore();
    await store.bootstrap();
    store.toggleImportItem(0, IMPORT_PASSWORDS);
    expect(store.getSnapshot().importItemSelections.get(0)).toBe(
        IMPORT_HISTORY | IMPORT_BOOKMARKS);
    store.toggleImportItem(0, IMPORT_SEARCH_ENGINES);
    expect(store.getSnapshot().importItemSelections.get(0)).toBe(
        IMPORT_HISTORY | IMPORT_BOOKMARKS | IMPORT_SEARCH_ENGINES);
  });

  it('starts import with the selected items mask and tracks progress', async () => {
    const store = new MahoWelcomeStore();
    await store.bootstrap();
    store.startImport(0);

    expect(store.getSnapshot().importState).toEqual({
      phase: 'running',
      statusMessage: 'Starting import…',
      browserIndex: 0,
    });
    expect(mockHandlerInstance.startImport).toHaveBeenCalledWith(
        0, IMPORT_HISTORY | IMPORT_BOOKMARKS | IMPORT_PASSWORDS);

    mockImportProgress.listener?.(1, 50, '', false);
    expect(store.getSnapshot().importState).toEqual({
      phase: 'running',
      statusMessage: '',
      browserIndex: 0,
    });

    mockImportProgress.listener?.(0, 0, '', true);
    expect(store.getSnapshot().importState).toEqual({
      phase: 'complete',
      statusMessage: '',
      browserIndex: 0,
    });
  });

  it('does not start import when nothing is selected', async () => {
    const store = new MahoWelcomeStore();
    await store.bootstrap();
    store.toggleImportItem(1, IMPORT_HISTORY);
    store.toggleImportItem(1, IMPORT_BOOKMARKS);
    store.startImport(1);
    expect(mockHandlerInstance.startImport).not.toHaveBeenCalled();
    expect(store.getSnapshot().importState.phase).toBe('idle');
  });

  it('updates search engine inline and calls the handler', () => {
    const store = new MahoWelcomeStore();
    store.setSearchEngine('bing');
    expect(store.getSnapshot().selectedEngine).toBe('bing');
    expect(mockHandlerInstance.setDefaultSearchEngine).toHaveBeenCalledWith(
        'bing');
  });

  it('toggles essentials and favorites selected sites', () => {
    const store = new MahoWelcomeStore();
    store.toggleEssential('https://maho.dev');
    store.toggleEssential('https://example.com');
    expect(store.getSnapshot().selectedEssentials.has('https://maho.dev'))
        .toBe(true);

    store.favoriteSelectedEssentials();
    expect(mockHandlerInstance.favoriteEssentialSites).toHaveBeenCalledWith([
      'https://maho.dev',
      'https://example.com',
    ]);
  });

  it('stores, previews, and applies themes', () => {
    const store = new MahoWelcomeStore();
    store.selectTheme('ocean');
    store.previewTheme('{"mode":"custom"}');
    store.applyTheme('{"mode":"custom"}');

    expect(store.getSnapshot().selectedTheme).toBe('ocean');
    expect(mockHandlerInstance.previewTheme).toHaveBeenCalledWith(
        '{"mode":"custom"}');
    expect(mockHandlerInstance.applyTheme).toHaveBeenCalledWith(
        '{"mode":"custom"}');
  });

  it('completes onboarding after favoriting essentials', () => {
    const store = new MahoWelcomeStore();
    store.toggleEssential('https://maho.dev');
    store.complete();
    expect(mockHandlerInstance.favoriteEssentialSites).toHaveBeenCalledWith([
      'https://maho.dev',
    ]);
    expect(mockHandlerInstance.finishOnboarding).toHaveBeenCalledTimes(1);
  });

  it('notifies subscribers on state change and supports unsubscribe', async () => {
    const store = new MahoWelcomeStore();
    const first = vi.fn();
    const second = vi.fn();

    const unsubscribeFirst = store.subscribe(first);
    store.subscribe(second);
    // Simulate auth success so nextPage changes state
    store.setAuthEmail('user@maho.dev');
    store.setAuthPassword('securepwd');
    mockHandlerInstance.loginFromWelcome.mockResolvedValueOnce({ok: true, errorMessage: ''});
    mockHandlerInstance.getRelayAccountStatusFromWelcome.mockResolvedValueOnce({
      signedIn: true,
      tier: 'pro',
      subscriptionStatus: 'active',
      welcomeCompleted: false,
    });
    await store.submitSignIn();

    store.nextPage();
    expect(first).toHaveBeenCalled();
    expect(second).toHaveBeenCalled();

    unsubscribeFirst();
    store.nextPage();
    expect(second).toHaveBeenCalled();
  });

  it('manages Auth login and signup actions', async () => {
    const store = new MahoWelcomeStore();
    store.setAuthMode('signup');
    expect(store.getSnapshot().auth.mode).toBe('signup');

    store.setAuthEmail('newuser@maho.dev');
    store.setAuthPassword('securepwd123');
    store.setAuthDisplayName('New User');
    expect(store.getSnapshot().auth.email).toBe('newuser@maho.dev');
    expect(store.getSnapshot().auth.password).toBe('securepwd123');
    expect(store.getSnapshot().auth.displayName).toBe('New User');

    mockHandlerInstance.signupFromWelcome.mockResolvedValueOnce({ok: true, errorMessage: ''});
    mockHandlerInstance.getRelayAccountStatusFromWelcome.mockResolvedValueOnce({
      signedIn: true,
      tier: 'pro',
      subscriptionStatus: 'active',
      welcomeCompleted: false,
    });

    const success = await store.submitSignUp();
    expect(success).toBe(true);
    expect(mockHandlerInstance.signupFromWelcome).toHaveBeenCalledWith('newuser@maho.dev', 'securepwd123', 'New User');
    expect(store.getSnapshot().auth.status).toBe('success');

    // Test failure scenario
    mockHandlerInstance.loginFromWelcome.mockResolvedValueOnce({ok: false, errorMessage: 'Invalid password'});
    store.setAuthMode('signin');
    const failed = await store.submitSignIn();
    expect(failed).toBe(false);
    expect(store.getSnapshot().auth.status).toBe('error');
    expect(store.getSnapshot().auth.errorMessage).toBe('Invalid password');
  });

  it('records explicit password manager skip and advances to SyncKeyBackup', async () => {
    const store = new MahoWelcomeStore();

    (store as unknown as {patch: (fn: (draft: Record<string, unknown>) => void) => void}).patch(draft => {
      draft.currentPage = WelcomePage.PasswordSetup;
    });

    expect(store.getSnapshot().currentPage).toBe(WelcomePage.PasswordSetup);

    await store.skipPasswordSetup();
    expect(store.getSnapshot().currentPage).toBe(WelcomePage.SyncKeyBackup);
    expect(store.getSnapshot().passwordSetup.passwordManagerSkipped).toBe(true);
  });

  it('does not open checkout when the generated subscription URL is empty', async () => {
    const store = new MahoWelcomeStore();
    mockHandlerInstance.getSubscriptionCheckoutUrl.mockResolvedValue({checkoutUrl: ''});

    const ok = await store.startSubscriptionCheckout('pro');

    expect(ok).toBe(false);
    expect(mockHandlerInstance.getSubscriptionCheckoutUrl).toHaveBeenCalledWith('pro');
    expect(mockLemon.overlay).not.toHaveBeenCalled();
    expect(store.getSnapshot().aiSetup.status).toBe('error');
  });

  it('refreshes relay account status and advances to PasswordSetup on checkout success', async () => {
    const store = new MahoWelcomeStore();
    mockHandlerInstance.getSubscriptionCheckoutUrl.mockResolvedValue({
      checkoutUrl:
          'https://maho.lemonsqueezy.com/buy/pro?checkout[custom][user_id]=u1',
    });

    await store.startSubscriptionCheckout('pro');

    expect(mockLemon.overlay).toHaveBeenCalledTimes(1);
    const opts = mockLemon.overlay.mock.calls[0]![0];
    expect(typeof opts.onSuccess).toBe('function');

    mockHandlerInstance.getRelayAccountStatusFromWelcome.mockClear();
    opts.onSuccess!('order_123');
    // Let the fire-and-forget account-status refresh microtask settle.
    await Promise.resolve();
    await Promise.resolve();

    expect(mockHandlerInstance.getRelayAccountStatusFromWelcome).toHaveBeenCalled();
    expect(store.getSnapshot().currentPage).toBe(WelcomePage.Completion);
  });

  it('prevPage returns managed users from PasswordSetup to PlanSelect', () => {
    const store = new MahoWelcomeStore();
    (store as unknown as {patch: (fn: (draft: Record<string, unknown>) => void) => void}).patch(draft => {
      draft.currentPage = WelcomePage.PasswordSetup;
      (draft.aiSetup as {provider: string}).provider = 'maho-managed';
    });

    store.prevPage();

    expect(store.getSnapshot().currentPage).toBe(WelcomePage.PlanSelect);
  });

  it('prevPage returns non-managed users from PasswordSetup to AIProvider', () => {
    const store = new MahoWelcomeStore();
    (store as unknown as {patch: (fn: (draft: Record<string, unknown>) => void) => void}).patch(draft => {
      draft.currentPage = WelcomePage.PasswordSetup;
      (draft.aiSetup as {provider: string}).provider = 'byok';
    });

    store.prevPage();

    expect(store.getSnapshot().currentPage).toBe(WelcomePage.AIProvider);
  });
});
