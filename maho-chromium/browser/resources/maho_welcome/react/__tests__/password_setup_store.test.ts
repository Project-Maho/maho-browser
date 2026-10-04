// Copyright 2026 Maho Browser. All rights reserved.

import {beforeEach, describe, expect, it, vi} from 'vitest';

import {MahoWelcomeStore} from '../store.js';
import {
  PASSWORD_PROVIDER_KIND,
  TOTAL_PAGES,
  VAULT_LOCK_STATE,
  WelcomePage,
} from '../types.js';

const PROVIDER_CAPABILITIES = {
  canListSavedPasswords: true,
  canSearchSavedPasswords: true,
  canAddSavedPasswords: true,
  canDeleteSavedPasswords: true,
  canEditSavedPasswords: true,
} as const;

const PROVIDER_OPTIONS = [{
  provider: PASSWORD_PROVIDER_KIND.MahoNative,
  displayName: 'Maho Native',
  description: 'Store passwords in the local Maho Vault.',
  isAvailable: true,
}] as const;

function createVaultStatus(lockState: number) {
  return {
    lockState,
    selectedProvider: PASSWORD_PROVIDER_KIND.MahoNative,
    effectiveProvider: PASSWORD_PROVIDER_KIND.MahoNative,
    agentPolicyDefault: 0,
    itemCount: 0n,
    autoLockMinutes: 15,
    failedUnlockCount: 0,
    retryAtTimestamp: null,
  };
}

function createVaultOperation(lockState: number) {
  return {
    success: true,
    errorCode: null,
    errorMessage: null,
    status: createVaultStatus(lockState),
    item: null,
  };
}

const mockHandler = vi.hoisted(() => ({
  $: {bindNewPipeAndPassReceiver: vi.fn()},
  getSearchEngines: vi.fn().mockResolvedValue({engines: []}),
  getEssentialSites: vi.fn().mockResolvedValue({sites: []}),
  getAvailableBrowsers: vi.fn().mockResolvedValue({browsers: []}),
  getLocalizedStrings: vi.fn().mockResolvedValue({strings: {}}),
  getRelayAccountStatusFromWelcome: vi.fn().mockResolvedValue({
    signedIn: true,
    tier: '',
    subscriptionStatus: '',
    welcomeCompleted: false,
    hasStoredSession: false,
    isLocalDev: false,
  }),
  getPasswordProviderOptions: vi.fn(),
  getPasswordProviderStatus: vi.fn(),
  setPasswordProvider: vi.fn(),
  getVaultStatus: vi.fn(),
  initializeVault: vi.fn(),
  unlockVault: vi.fn(),
  loginFromWelcome: vi.fn().mockResolvedValue({ok: true, errorMessage: ''}),
  finishOnboarding: vi.fn(),
}));

const mockCallbackRouter = vi.hoisted(() => ({
  $: {bindNewPipeAndPassRemote: vi.fn()},
  onImportProgress: {addListener: vi.fn()},
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

function configurePasswordSetup(lockState: number): void {
  mockHandler.getPasswordProviderOptions.mockResolvedValue({options: PROVIDER_OPTIONS});
  mockHandler.getPasswordProviderStatus.mockResolvedValue({
    status: {
      ...PROVIDER_OPTIONS[0],
      isEnabled: true,
      capabilities: PROVIDER_CAPABILITIES,
    },
  });
  mockHandler.getVaultStatus.mockResolvedValue({result: createVaultOperation(lockState)});
  mockHandler.setPasswordProvider.mockResolvedValue({success: true});
}

async function moveToPasswordSetup(store: MahoWelcomeStore): Promise<void> {
  store.setAuthEmail('user@maho.dev');
  store.setAuthPassword('secure-password');
  await store.submitSignIn();
  // Account escrow: the default flow skips the vault ceremony, so the
  // page-level tests enter the page through its dedicated navigation.
  store.navigateToPasswordSetup();
}

describe('PasswordSetup store flow', () => {
  beforeEach(() => {
    vi.clearAllMocks();
    configurePasswordSetup(VAULT_LOCK_STATE.Unlocked);
  });

  it('keeps PasswordSetup before SyncKeyBackup and Completion', () => {
    expect(WelcomePage.PasswordSetup).toBe(8);
    expect(WelcomePage.SyncKeyBackup).toBe(9);
    expect(WelcomePage.Completion).toBe(10);
    expect(TOTAL_PAGES).toBe(12);
    expect(WelcomePage.PasswordSetup + 1).toBe(WelcomePage.SyncKeyBackup);
    expect(WelcomePage.SyncKeyBackup + 1).toBe(WelcomePage.Completion);
  });

  it('refreshes provider and Vault state from the Welcome handler', async () => {
    const store = new MahoWelcomeStore();

    await expect(store.refreshPasswordSetup()).resolves.toBe(true);

    expect(store.getSnapshot().passwordSetup.status).toBe('ready');
    expect(store.getSnapshot().passwordSetup.providerOptions).toEqual(PROVIDER_OPTIONS);
    expect(store.getSnapshot().passwordSetup.vaultStatus?.lockState)
      .toBe(VAULT_LOCK_STATE.Unlocked);
  });

  it('blocks generic nextPage on PasswordSetup but keeps Back available', async () => {
    const store = new MahoWelcomeStore();
    await moveToPasswordSetup(store);

    store.nextPage();
    expect(store.getSnapshot().currentPage).toBe(WelcomePage.PasswordSetup);

    store.prevPage();
    expect(store.getSnapshot().currentPage).toBe(WelcomePage.AIProvider);
  });

  it('persists the provider and advances to SyncKeyBackup when unlocked', async () => {
    const store = new MahoWelcomeStore();
    await moveToPasswordSetup(store);

    await expect(store.advanceFromPasswordSetup(PASSWORD_PROVIDER_KIND.MahoNative))
      .resolves.toBe(true);

    expect(mockHandler.setPasswordProvider)
      .toHaveBeenCalledWith(PASSWORD_PROVIDER_KIND.MahoNative);
    expect(store.getSnapshot().currentPage).toBe(WelcomePage.SyncKeyBackup);
  });

  it('stays on PasswordSetup when the Vault remains locked', async () => {
    configurePasswordSetup(VAULT_LOCK_STATE.Locked);
    const store = new MahoWelcomeStore();
    await moveToPasswordSetup(store);

    await expect(store.advanceFromPasswordSetup(PASSWORD_PROVIDER_KIND.MahoNative))
      .resolves.toBe(false);

    expect(store.getSnapshot().currentPage).toBe(WelcomePage.PasswordSetup);
    expect(store.getSnapshot().passwordSetup.status).toBe('error');
  });

  it('persists and completes the "None" choice without an unlocked Vault', async () => {
    configurePasswordSetup(VAULT_LOCK_STATE.Uninitialized);
    const store = new MahoWelcomeStore();
    await moveToPasswordSetup(store);

    await expect(store.skipPasswordSetup()).resolves.toBe(true);

    expect(mockHandler.setPasswordProvider)
      .toHaveBeenCalledWith(PASSWORD_PROVIDER_KIND.Disabled);
    expect(mockHandler.unlockVault).not.toHaveBeenCalled();
    expect(store.getSnapshot().currentPage).toBe(WelcomePage.SyncKeyBackup);
    expect(store.getSnapshot().passwordSetup.passwordManagerSkipped).toBe(true);
    expect(store.getSnapshot().passwordSetup.errorMessage).toBe('');
  });

  it('completes the "None" choice even when persistence fails, and says so', async () => {
    configurePasswordSetup(VAULT_LOCK_STATE.Uninitialized);
    mockHandler.setPasswordProvider.mockResolvedValue({success: false});
    const store = new MahoWelcomeStore();
    await moveToPasswordSetup(store);

    await expect(store.skipPasswordSetup()).resolves.toBe(false);

    expect(store.getSnapshot().currentPage).toBe(WelcomePage.SyncKeyBackup);
    expect(store.getSnapshot().passwordSetup.passwordManagerSkipped).toBe(true);
    expect(store.getSnapshot().passwordSetup.errorMessage).not.toBe('');
  });

  it('persists and completes an external provider without an unlocked Vault', async () => {
    configurePasswordSetup(VAULT_LOCK_STATE.Uninitialized);
    const store = new MahoWelcomeStore();
    await moveToPasswordSetup(store);

    await expect(store.advanceFromPasswordSetup(PASSWORD_PROVIDER_KIND.Bitwarden))
      .resolves.toBe(true);

    expect(mockHandler.setPasswordProvider)
      .toHaveBeenCalledWith(PASSWORD_PROVIDER_KIND.Bitwarden);
    expect(mockHandler.unlockVault).not.toHaveBeenCalled();
    expect(mockHandler.initializeVault).not.toHaveBeenCalled();
    expect(store.getSnapshot().currentPage).toBe(WelcomePage.SyncKeyBackup);
  });

  it('does not retain Vault secrets in Welcome state', async () => {
    const store = new MahoWelcomeStore();
    const masterPassphrase = 'master-only-in-call';
    const recoverySecret = 'recovery-only-in-call';
    mockHandler.initializeVault.mockResolvedValue({
      result: createVaultOperation(VAULT_LOCK_STATE.Unlocked),
    });

    await expect(store.initializeVault(masterPassphrase, recoverySecret)).resolves.toBe(true);

    expect(mockHandler.initializeVault)
      .toHaveBeenCalledWith(masterPassphrase, recoverySecret);
    const serialized = JSON.stringify(store.getSnapshot(), (_key, value: unknown) =>
      typeof value === 'bigint' ? value.toString() : value);
    expect(serialized).not.toContain(masterPassphrase);
    expect(serialized).not.toContain(recoverySecret);
  });
});
