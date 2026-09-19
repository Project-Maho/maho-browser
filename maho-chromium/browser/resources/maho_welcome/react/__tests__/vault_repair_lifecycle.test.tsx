// Copyright 2026 Maho Browser. All rights reserved.

import {beforeEach, describe, expect, it, vi} from 'vitest';

import {MahoWelcomeStore} from '../store.js';
import {
  PASSWORD_PROVIDER_KIND,
  VAULT_LOCK_STATE,
  WelcomePage,
} from '../types.js';
import {getAuthSidebarCopy} from '../app.js';

const mockHandler = vi.hoisted(() => ({
  $: {bindNewPipeAndPassReceiver: vi.fn()},
  getSearchEngines: vi.fn().mockResolvedValue({engines: []}),
  getEssentialSites: vi.fn().mockResolvedValue({sites: []}),
  getAvailableBrowsers: vi.fn().mockResolvedValue({browsers: []}),
  getLocalizedStrings: vi.fn().mockResolvedValue({strings: {}}),
  getRelayAccountStatusFromWelcome: vi.fn(),
  getPasswordProviderOptions: vi.fn().mockResolvedValue({
    options: [{
      provider: 0,
      displayName: 'Maho Native',
      description: 'Local Vault',
      isAvailable: true,
    }],
  }),
  getPasswordProviderStatus: vi.fn(),
  setPasswordProvider: vi.fn().mockResolvedValue({success: true}),
  getVaultStatus: vi.fn(),
  initializeVault: vi.fn().mockImplementation(async () => {
    mockHandler.getVaultStatus.mockResolvedValue({
      result: {
        success: true,
        errorCode: null,
        errorMessage: null,
        status: {
          lockState: 2,
          selectedProvider: 0,
          effectiveProvider: 0,
          agentPolicyDefault: 0,
          itemCount: 0n,
          autoLockMinutes: 15,
          failedUnlockCount: 0,
          retryAtTimestamp: null,
        },
        item: null,
      },
    });
    return {
      result: {
        success: true,
        errorCode: null,
        errorMessage: null,
        status: {
          lockState: 2,
          selectedProvider: 0,
          effectiveProvider: 0,
          agentPolicyDefault: 0,
          itemCount: 0n,
          autoLockMinutes: 15,
          failedUnlockCount: 0,
          retryAtTimestamp: null,
        },
        item: null,
      },
    };
  }),
  unlockVault: vi.fn(),
  loginFromWelcome: vi.fn().mockResolvedValue({ok: true, errorMessage: ''}),
  finishOnboarding: vi.fn(),
  favoriteEssentialSites: vi.fn(),
}));

const mockCallbackRouter = vi.hoisted(() => ({
  $: {bindNewPipeAndPassRemote: vi.fn()},
  onImportProgress: {addListener: vi.fn()},
  onMailAccountsChanged: {addListener: vi.fn()},
  removeListener: vi.fn(),
}));

vi.mock('../../maho_welcome.mojom-webui.js', () => ({
  PageCallbackRouter: vi.fn().mockImplementation(() => mockCallbackRouter),
  PageHandlerRemote: vi.fn().mockImplementation(() => mockHandler),
  PageHandlerFactory: {
    getRemote: vi.fn().mockReturnValue({createPageHandler: vi.fn()}),
  },
  IMPORT_BOOKMARKS: 0x01,
  IMPORT_PASSWORDS: 0x02,
  IMPORT_HISTORY: 0x04,
  IMPORT_COOKIES: 0x08,
  IMPORT_AUTOFILL: 0x10,
  IMPORT_WORKSPACES: 0x20,
  IMPORT_FAVICONS: 0x40,
}));

function setupReturningNativeProfile({signedIn, vaultLockState}: {
  signedIn: boolean;
  vaultLockState: number;
}) {
  mockHandler.getRelayAccountStatusFromWelcome.mockResolvedValue({
    signedIn,
    tier: 'free',
    subscriptionStatus: '',
    welcomeCompleted: false,
    hasStoredSession: true,
    isLocalDev: false,
  });
  mockHandler.getPasswordProviderStatus.mockResolvedValue({
    status: {
      provider: PASSWORD_PROVIDER_KIND.MahoNative,
      displayName: 'Maho Native',
      description: 'Local Vault',
      isAvailable: true,
      isEnabled: true,
      capabilities: {
        canListSavedPasswords: true,
        canSearchSavedPasswords: true,
        canAddSavedPasswords: true,
        canDeleteSavedPasswords: true,
        canEditSavedPasswords: true,
      },
    },
  });
  mockHandler.getVaultStatus.mockResolvedValue({
    result: {
      success: true,
      errorCode: null,
      errorMessage: null,
      status: {
        lockState: vaultLockState,
        selectedProvider: PASSWORD_PROVIDER_KIND.MahoNative,
        effectiveProvider: PASSWORD_PROVIDER_KIND.MahoNative,
        agentPolicyDefault: 0,
        itemCount: 0n,
        autoLockMinutes: 15,
        failedUnlockCount: 0,
        retryAtTimestamp: null,
      },
      item: null,
    },
  });
}

describe('Vault repair lifecycle vs reauthentication', () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  it('routes an already-signed-in user with uninitialized Vault directly to PasswordSetup', async () => {
    setupReturningNativeProfile({
      signedIn: true,
      vaultLockState: VAULT_LOCK_STATE.Uninitialized,
    });

    const store = new MahoWelcomeStore();
    await store.bootstrap();
    const state = store.getSnapshot();

    expect(state.isVaultRepairMode).toBe(true);
    expect(state.auth.status).toBe('success');
    expect(state.currentPage).toBe(WelcomePage.PasswordSetup);
  });

  it('completes onboarding instead of proceeding to SyncKeyBackup when PasswordSetup is finished during repair', async () => {
    setupReturningNativeProfile({
      signedIn: true,
      vaultLockState: VAULT_LOCK_STATE.Uninitialized,
    });

    const store = new MahoWelcomeStore();
    await store.bootstrap();

    const initialized = await store.initializeVault('master123!', 'recovery123!');
    expect(initialized).toBe(true);

    const advanced = await store.advanceFromPasswordSetup(PASSWORD_PROVIDER_KIND.MahoNative);
    expect(advanced).toBe(true);

    expect(mockHandler.finishOnboarding).toHaveBeenCalledTimes(1);
    expect(store.getSnapshot().currentPage).toBe(WelcomePage.PasswordSetup);
  });

  it('completes onboarding when skipPasswordSetup is chosen during repair', async () => {
    setupReturningNativeProfile({
      signedIn: true,
      vaultLockState: VAULT_LOCK_STATE.Uninitialized,
    });

    const store = new MahoWelcomeStore();
    await store.bootstrap();

    const skipped = await store.skipPasswordSetup();
    expect(skipped).toBe(true);

    expect(mockHandler.finishOnboarding).toHaveBeenCalledTimes(1);
    expect(store.getSnapshot().currentPage).toBe(WelcomePage.PasswordSetup);
  });

  it('shows Auth page with Session expired when session is expired and Vault is uninitialized', async () => {
    setupReturningNativeProfile({
      signedIn: false,
      vaultLockState: VAULT_LOCK_STATE.Uninitialized,
    });

    const store = new MahoWelcomeStore();
    await store.bootstrap();
    const state = store.getSnapshot();

    expect(state.isVaultRepairMode).toBe(true);
    expect(state.auth.status).toBe('idle');
    expect(state.auth.isReauthMode).toBe(true);
    expect(state.currentPage).toBe(WelcomePage.Auth);

    const copy = getAuthSidebarCopy(state.auth);
    expect(copy.title).toBe('Session expired');
  });

  it('does not display Session expired when user is already signed in with stored session', () => {
    const copy = getAuthSidebarCopy({
      mode: 'signin',
      email: 'user@maho.dev',
      password: '',
      displayName: 'User',
      status: 'success',
      errorMessage: '',
      isReauthMode: false,
      hasStoredSession: true,
    });

    expect(copy.title).not.toBe('Session expired');
    expect(copy.title).toBe('Sign in to Maho');
  });

  it('does not flag Vault repair when Vault is already initialized', async () => {
    setupReturningNativeProfile({
      signedIn: true,
      vaultLockState: VAULT_LOCK_STATE.Locked,
    });

    const store = new MahoWelcomeStore();
    await store.bootstrap();
    const state = store.getSnapshot();

    expect(state.isVaultRepairMode).toBe(false);
  });
});
