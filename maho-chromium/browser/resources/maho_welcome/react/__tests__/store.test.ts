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

describe('MahoWelcomeStore', () => {
  let MahoWelcomeStore: typeof import('../store.js').MahoWelcomeStore;

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
});
