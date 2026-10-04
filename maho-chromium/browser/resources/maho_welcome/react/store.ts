import {
  PageCallbackRouter,
  PageHandlerFactory,
  PageHandlerRemote,
  IMPORT_BOOKMARKS,
  IMPORT_PASSWORDS,
  IMPORT_HISTORY,
  IMPORT_COOKIES,
  IMPORT_AUTOFILL,
  IMPORT_WORKSPACES,
  IMPORT_FAVICONS,
} from '../maho_welcome.mojom-webui.js';
import type {BrowserProfile, EssentialSite, SearchEngineInfo} from '../maho_welcome.mojom-webui.js';
import type {
  PasswordProviderKind,
  VaultOperationResult,
} from '../../maho_settings/maho_settings.mojom-webui.js';
import {openCheckoutOverlay} from '../../maho_common/react/lemonsqueezy.js';
import {notifyListeners} from '../../maho_common/react/store_utils.js';
import type {Listener} from '../../maho_common/react/store_utils.js';
import type {MailAccount} from '../../maho_common/react/mail/mail_onboarding_api.js';
import type {PlanTier} from '../../maho_common/react/ui/plan-picker.js';
import {
  createInitialState,
  PASSWORD_PROVIDER_KIND,
  VAULT_LOCK_STATE,
  WelcomePage,
} from './types.js';
import type {
  AppearanceMode,
  AiSetupState,
  AuthState,
  ImportStage,
  ImportState,
  PasswordSetupState,
  SyncKeyBackupState,
  WelcomeState,
} from './types.js';

interface MutableWelcomeState {
  currentPage: WelcomePage;
  direction: 'forward' | 'backward';
  appearance: AppearanceMode;
  availableBrowsers: BrowserProfile[];
  importState: ImportState;
  importItemSelections: Map<number, number>;
  setAsDefaultRequested: boolean;
  importStarted: boolean;
  importStage: ImportStage;
  searchEngines: SearchEngineInfo[];
  selectedEngine: string | null;
  essentialSites: EssentialSite[];
  selectedEssentials: Set<string>;
  auth: AuthState;
  isVaultRepairMode: boolean;
  aiSetup: AiSetupState;
  mailAccounts: readonly MailAccount[];
  passwordSetup: PasswordSetupState;
  syncKeyBackup: SyncKeyBackupState;
  selectedTheme: string | null;
  localizedStrings: Map<string, string>;
  isLocalDev: boolean;
}

const DEFAULT_IMPORT_ITEMS =
    IMPORT_HISTORY | IMPORT_BOOKMARKS | IMPORT_PASSWORDS |
    IMPORT_COOKIES | IMPORT_AUTOFILL | IMPORT_WORKSPACES | IMPORT_FAVICONS;

export class MahoWelcomeStore {
  private readonly callbackRouter = new PageCallbackRouter();
  private readonly listeners = new Set<Listener>();
  private readonly pageHandler = new PageHandlerRemote();
  private state: WelcomeState = createInitialState();
  private authReturnPage: WelcomePage | null = null;
  // Generation of the latest BYOK readiness operation; only the newest
  // operation may apply its result.
  private byokCheckGeneration = 0;

  constructor() {
    const factory = PageHandlerFactory.getRemote();
    factory.createPageHandler(
        this.callbackRouter.$.bindNewPipeAndPassRemote(),
        this.pageHandler.$.bindNewPipeAndPassReceiver());

    this.callbackRouter.onImportProgress.addListener(
        (type: number, itemsImported: number, error: string, complete: boolean) => {
          void type;
          void itemsImported;
          this.patch(draft => {
            draft.importState = {
              phase: complete ? 'complete' : 'running',
              statusMessage: error,
              browserIndex: draft.importState.browserIndex,
            };
            if (complete) {
              draft.currentPage = WelcomePage.SourceSelect;
              draft.importStage = 'B';
              draft.direction = 'forward';
            }
          });
        });
  }

  getSnapshot(): WelcomeState {
    return this.state;
  }

  subscribe(listener: Listener): () => void {
    this.listeners.add(listener);
    return () => {
      this.listeners.delete(listener);
    };
  }

  dispose(): void {
    (this.callbackRouter.$ as {close?: () => void}).close?.();
    (this.pageHandler.$ as {close?: () => void}).close?.();
  }

  async bootstrap(): Promise<void> {
    const [{engines}, {sites}, {browsers}, {strings}] = await Promise.all([
      this.pageHandler.getSearchEngines(),
      this.pageHandler.getEssentialSites(),
      this.pageHandler.getAvailableBrowsers(),
      this.pageHandler.getLocalizedStrings(),
    ]);
    const {signedIn, welcomeCompleted, hasStoredSession, isLocalDev} =
        await this.pageHandler.getRelayAccountStatusFromWelcome();

    const isVaultRepairNeeded = Boolean(hasStoredSession && await this.checkVaultRepairNeeded());
    const isReauthMode = welcomeCompleted || (hasStoredSession && !signedIn);

      this.patch(draft => {
        draft.searchEngines = engines;
        draft.selectedEngine = engines.find(e => e.isDefault)?.keyword ?? null;
        draft.essentialSites = sites;
        draft.availableBrowsers = browsers;
        draft.importItemSelections = new Map();
        for (const browser of browsers) {
          draft.importItemSelections.set(
              browser.index,
              DEFAULT_IMPORT_ITEMS & browser.servicesSupported);
        }
        draft.localizedStrings = new Map([
          ['IDS_MAHO_WELCOME_SYNC_BACKUP_TITLE', 'Back up your sync recovery key'],
          ['IDS_MAHO_WELCOME_SYNC_BACKUP_BODY', 'Save your recovery phrase to a local file. You need this phrase to access your encrypted sync data on another device.'],
          ['IDS_MAHO_WELCOME_SYNC_BACKUP_START_SYNC', 'Start syncing'],
          ['IDS_MAHO_WELCOME_SYNC_BACKUP_SAVE_FILE', 'Save file'],
          ['IDS_MAHO_WELCOME_SYNC_BACKUP_FAIL_RETRY', 'Download failed. Please try saving your sync key again.'],
          ['IDS_MAHO_WELCOME_NO_PASSWORD_MANAGER', 'No password manager'],
          ['IDS_MAHO_WELCOME_LOCAL_VAULT_EXPLANATION', 'Local Vault keeps your passwords securely encrypted on this device.'],
          ...Object.entries(strings),
        ]);
        draft.isLocalDev = isLocalDev;
        draft.isVaultRepairMode = isVaultRepairNeeded;

      draft.auth = {
        ...draft.auth,
        status: signedIn ? 'success' : 'idle',
        isReauthMode,
        hasStoredSession,
      };
      if (isVaultRepairNeeded) {
        draft.currentPage = signedIn ? WelcomePage.PasswordSetup : WelcomePage.Auth;
      } else if (isReauthMode) {
        draft.currentPage = WelcomePage.Auth;
      }
    });
    applyAppearanceMode(this.state.appearance);
    if (isVaultRepairNeeded && signedIn) {
      void this.refreshPasswordSetup();
    }
  }

  getString(key: string): string {
    return this.state.localizedStrings.get(key) ?? key;
  }

  async openMigrationDialog(): Promise<void> {
    const {result} = await this.pageHandler.openMigrationDialog();
    if (!result.wasCancelled) {
      this.patch(draft => {
        draft.importStarted = true;
        draft.importStage = 'B';
        draft.importState = {
          phase: 'complete',
          statusMessage: '',
          browserIndex: draft.importState.browserIndex,
        };
      });
    }
  }

  skipImport(): void {
    this.patch(draft => {
      draft.importStage = 'B';
      draft.currentPage = WelcomePage.SourceSelect;
      draft.direction = 'forward';
    });
  }

  nextPage(): void {
    if (this.state.currentPage === WelcomePage.Auth) {
      if (this.state.auth.status !== 'success') {
        return;
      }
      const nextPage = this.authReturnPage ?? WelcomePage.Splash;
      this.authReturnPage = null;
      this.patch(d => { d.currentPage = nextPage; d.direction = 'forward'; });
      return;
    }

    if (this.state.currentPage === WelcomePage.AIProvider) {
      if (this.state.aiSetup.provider === 'byok' &&
          this.state.aiSetup.status !== 'success') {
        // BYOK stays on the AI page until the selected provider's credentials
        // are confirmed by the native readiness check; Next must not advance
        // unconfigured.
        return;
      }
      this.patch(d => {
        if (d.aiSetup.provider === 'maho-managed' && d.auth.status !== 'success') {
          d.currentPage = WelcomePage.Auth;
          d.auth = { ...d.auth, mode: 'signin', isReauthMode: false };
          this.authReturnPage = WelcomePage.PlanSelect;
        } else if (d.aiSetup.provider === 'maho-managed') {
          d.currentPage = WelcomePage.PlanSelect;
        } else {
          // Account escrow: the Vault key follows the signed-in account and is
          // provisioned by the sign-in Sync bootstrap, so the passphrase /
          // recovery-kit ceremony is skipped in the default flow.
          d.currentPage = WelcomePage.Completion;
        }
        d.direction = 'forward';
      });
      return;
    }

    if (this.state.currentPage === WelcomePage.SourceSelect &&
        this.state.importStage === 'B') {
      this.applyDefaultBrowserChoice();
      this.patch(draft => {
        draft.importStage = 'A';
        draft.direction = 'forward';
        draft.currentPage = WelcomePage.SearchEngine;
      });
      return;
    }

    if (this.state.currentPage === WelcomePage.PasswordSetup ||
        this.state.currentPage === WelcomePage.SyncKeyBackup) {
      return;
    }

    this.patch(draft => {
      if (draft.currentPage < WelcomePage.Completion) {
        draft.direction = 'forward';
        draft.currentPage = (draft.currentPage + 1) as WelcomePage;
      }
    });
  }

  prevPage(): void {
    this.patch(draft => {
      if (draft.currentPage === WelcomePage.PlanSelect) {
        draft.currentPage = WelcomePage.AIProvider;
        draft.direction = 'backward';
        return;
      }
      if (draft.currentPage === WelcomePage.PasswordSetup &&
          draft.aiSetup.provider === 'maho-managed') {
        // Managed users reach PasswordSetup via PlanSelect (enum 11, appended
        // after PasswordSetup=8), so the numeric `- 1` fallback would wrongly
        // skip back to AIProvider. Return to PlanSelect explicitly (plan §4d).
        draft.currentPage = WelcomePage.PlanSelect;
        draft.direction = 'backward';
        return;
      }
      if (draft.currentPage > WelcomePage.Splash) {
        draft.direction = 'backward';
        if (draft.currentPage === WelcomePage.SourceSelect &&
            draft.importStage === 'B') {
          draft.importStage = 'A';
        } else {
          draft.currentPage = (draft.currentPage - 1) as WelcomePage;
        }
      }
    });
  }

  toggleImportItem(browserIndex: number, itemFlag: number): void {
    this.patch(draft => {
      const current = draft.importItemSelections.get(browserIndex) ?? 0;
      const next =
          (current & itemFlag) ? (current & ~itemFlag) : (current | itemFlag);
      draft.importItemSelections.set(browserIndex, next);
    });
  }

  startImport(browserIndex: number): void {
    const items = this.state.importItemSelections.get(browserIndex) ?? 0;
    if (items === 0) {
      return;
    }

    this.patch(draft => {
      draft.importState = {
        phase: 'running',
        statusMessage: 'Starting import…',
        browserIndex,
      };
      draft.currentPage = WelcomePage.ImportProgress;
      draft.direction = 'forward';
      draft.importStarted = true;
    });

    this.pageHandler.startImport(browserIndex, items);
  }

  setSetAsDefault(value: boolean): void {
    this.patch(draft => {
      draft.setAsDefaultRequested = value;
    });
  }

  setAiProvider(provider: 'maho-managed' | 'byok' | 'skip'): void {
    this.patch(draft => {
      draft.aiSetup = {
        provider: '',
        status: 'submitting',
        errorMessage: '',
        skipped: false,
      };
    });

    if (provider === 'skip') {
      this.patch(draft => {
        draft.aiSetup = { provider: '', status: 'success', errorMessage: '', skipped: true };
        draft.currentPage = WelcomePage.Completion;
        draft.direction = 'forward';
      });
      return;
    }

    if (provider === 'maho-managed') {
      this.patch(draft => {
        draft.aiSetup = { provider: 'maho-managed', status: 'success', errorMessage: '', skipped: false };
      });
      this.pageHandler.setTranslationProvider(provider).catch(() => {});
      return;
    }

    if (provider === 'byok') {
      this.patch(draft => {
        draft.aiSetup = { provider: 'byok', status: 'idle', errorMessage: '', skipped: false };
      });
      void this.checkByokConfigured();
      return;
    }
  }

  async checkByokConfigured(): Promise<boolean> {
    const generation = ++this.byokCheckGeneration;
    try {
      const {configured} = await this.pageHandler.getByokCredentialConfigured();
      if (generation !== this.byokCheckGeneration) {
        return configured;
      }
      this.patch(draft => {
        if (draft.aiSetup.provider !== 'byok') {
          return;
        }
        draft.aiSetup = {
          ...draft.aiSetup,
          status: configured ? 'success' : 'idle',
          errorMessage: '',
        };
      });
      return configured;
    } catch (error) {
      if (generation !== this.byokCheckGeneration) {
        return false;
      }
      this.patch(draft => {
        if (draft.aiSetup.provider !== 'byok') {
          return;
        }
        draft.aiSetup = {
          ...draft.aiSetup,
          status: 'error',
          errorMessage: error instanceof Error
            ? error.message
            : 'Could not verify the AI provider configuration.',
        };
      });
      return false;
    }
  }

  async openByokSettings(): Promise<void> {
    if (this.state.aiSetup.provider !== 'byok') {
      return;
    }
    const generation = ++this.byokCheckGeneration;
    this.patch(draft => {
      draft.aiSetup = {...draft.aiSetup, status: 'submitting', errorMessage: ''};
    });
    try {
      const {configured} = await this.pageHandler.openByokSettingsDialog();
      if (generation !== this.byokCheckGeneration) {
        return;
      }
      this.patch(draft => {
        if (draft.aiSetup.provider !== 'byok') {
          return;
        }
        draft.aiSetup = {
          ...draft.aiSetup,
          status: configured ? 'success' : 'idle',
          errorMessage: '',
        };
      });
    } catch (error) {
      if (generation !== this.byokCheckGeneration) {
        return;
      }
      this.patch(draft => {
        if (draft.aiSetup.provider !== 'byok') {
          return;
        }
        draft.aiSetup = {
          ...draft.aiSetup,
          status: 'error',
          errorMessage: error instanceof Error
            ? error.message
            : 'Could not open AI settings.',
        };
      });
    }
  }

  continueWithFreePlan(): void {
    this.continueAfterPlanSelection();
  }

  async startSubscriptionCheckout(
      tier: Exclude<PlanTier, 'free'>): Promise<boolean> {
    this.patch(draft => {
      draft.aiSetup = {
        ...draft.aiSetup,
        status: 'submitting',
        errorMessage: '',
      };
    });

    try {
      const {checkoutUrl} =
          await this.pageHandler.getSubscriptionCheckoutUrl(tier);
      if (!checkoutUrl) {
        throw new Error('Checkout is unavailable. Please try again later.');
      }
      await openCheckoutOverlay({
        checkoutUrl,
        onSuccess: () => {
          // The overlay success event is only a UI hint; the subscription tier
          // is confirmed by the LS webhook -> relay -> pref pipeline. Refresh
          // relay account status rather than trusting local state, then advance.
          void this.refreshRelayAccountStatus();
          this.continueAfterPlanSelection();
        },
      });
      this.patch(draft => {
        draft.aiSetup = {
          ...draft.aiSetup,
          status: 'success',
          errorMessage: '',
        };
      });
      return true;
    } catch (error) {
      this.patch(draft => {
        draft.aiSetup = {
          ...draft.aiSetup,
          status: 'error',
          errorMessage: error instanceof Error
            ? error.message
            : 'Checkout is unavailable. Please try again later.',
        };
      });
      return false;
    }
  }

  async checkVaultRepairNeeded(): Promise<boolean> {
    try {
      if (typeof this.pageHandler.getVaultStatus !== 'function' ||
          typeof this.pageHandler.getPasswordProviderStatus !== 'function') {
        return false;
      }
      const [{status}, {result}] = await Promise.all([
        this.pageHandler.getPasswordProviderStatus(),
        this.pageHandler.getVaultStatus(),
      ]);
      const isNative = status?.provider === PASSWORD_PROVIDER_KIND.MahoNative;
      const isUninitialized = result?.status?.lockState === VAULT_LOCK_STATE.Uninitialized;
      return Boolean(isNative && isUninitialized);
    } catch {
      return false;
    }
  }

  navigateToPasswordSetup(): void {
    this.patch(draft => {
      draft.currentPage = WelcomePage.PasswordSetup;
      draft.direction = 'forward';
    });
  }

  goToAuth(): void {
    this.authReturnPage = null;
    this.patch(draft => {
      draft.currentPage = WelcomePage.Auth;
      draft.auth = { ...draft.auth, mode: 'signin', isReauthMode: false };
      draft.direction = 'forward';
    });
  }

  goToAuthSignup(): void {
    this.authReturnPage = null;
    this.patch(draft => {
      draft.currentPage = WelcomePage.Auth;
      draft.auth = { ...draft.auth, mode: 'signup', isReauthMode: false };
      draft.direction = 'forward';
    });
  }

  private continueAfterPlanSelection(): void {
    this.patch(draft => {
      draft.currentPage = WelcomePage.Completion;
      draft.direction = 'forward';
    });
  }

  mailListAccounts() {
    return this.pageHandler.mailListAccounts();
  }

  mailAddAccount(requestJson: string) {
    return this.pageHandler.mailAddAccount(requestJson);
  }

  mailTestConnection(paramsJson: string) {
    return this.pageHandler.mailTestConnection(paramsJson);
  }

  mailDeleteAccount(accountId: string) {
    return this.pageHandler.mailDeleteAccount(accountId);
  }

  mailBeginOAuth(provider: string) {
    return this.pageHandler.mailBeginOAuth(provider);
  }

  async mailOAuthCancel(state: string): Promise<boolean> {
    const {accepted} = await this.pageHandler.mailOAuthCancel(state);
    return accepted;
  }

  subscribeMailAccountsChanged(listener: () => void): () => void {
    const id = this.callbackRouter.onMailAccountsChanged.addListener(listener);
    return () => { this.callbackRouter.removeListener(id); };
  }

  patchMailAccounts(accounts: readonly MailAccount[]): void {
    this.patch(draft => { draft.mailAccounts = [...accounts]; });
  }

  async getAiProviderConfigured(): Promise<boolean> {
    const {configured} = await this.pageHandler.getAiProviderConfigured();
    return configured;
  }

  async setTranslationProvider(provider: string): Promise<boolean> {
    const {ok} = await this.pageHandler.setTranslationProvider(provider);
    return ok;
  }

  async refreshPasswordSetup(): Promise<boolean> {
    this.patch(draft => {
      draft.passwordSetup = {
        ...draft.passwordSetup,
        status: 'loading',
        errorMessage: '',
      };
    });

    try {
      const [{options}, {status}, {result}] = await Promise.all([
        this.pageHandler.getPasswordProviderOptions(),
        this.pageHandler.getPasswordProviderStatus(),
        this.pageHandler.getVaultStatus(),
      ]);
      if (!result.success || !result.status) {
        this.setPasswordSetupError(
            result.errorMessage || 'Failed to load the local Vault status.');
        return false;
      }

      this.patch(draft => {
        draft.passwordSetup = {
          status: 'ready',
          errorMessage: '',
          providerOptions: [...options],
          providerStatus: status,
          vaultStatus: result.status,
          passwordManagerSkipped: draft.passwordSetup.passwordManagerSkipped,
        };
      });
      return true;
    } catch (error) {
      this.setPasswordSetupError(
          error instanceof Error ? error.message : 'Failed to load password setup.');
      return false;
    }
  }

  async setPasswordProvider(provider: PasswordProviderKind): Promise<boolean> {
    this.setPasswordSetupSubmitting();
    try {
      const {success} = await this.pageHandler.setPasswordProvider(provider);
      if (!success) {
        this.setPasswordSetupError('Failed to save the password provider.');
        return false;
      }
      return this.refreshPasswordSetup();
    } catch (error) {
      this.setPasswordSetupError(
          error instanceof Error ? error.message : 'Failed to save the password provider.');
      return false;
    }
  }

  async initializeVault(
      masterPassphrase: string, recoverySecret: string): Promise<boolean> {
    return this.runVaultOperation(
        () => this.pageHandler.initializeVault(masterPassphrase, recoverySecret),
        'Vault initialization failed.');
  }

  async unlockVault(masterPassphrase: string): Promise<boolean> {
    return this.runVaultOperation(
        () => this.pageHandler.unlockVault(masterPassphrase),
        'Vault unlock failed.');
  }

  async advanceFromPasswordSetup(provider: PasswordProviderKind): Promise<boolean> {
    if (this.state.currentPage !== WelcomePage.PasswordSetup) {
      return false;
    }
    if (!await this.setPasswordProvider(provider)) {
      return false;
    }
    // Only the Maho Native provider depends on the local Vault. External
    // providers (and the disabled choice) must complete onboarding without one.
    if (provider === PASSWORD_PROVIDER_KIND.MahoNative &&
        this.state.passwordSetup.vaultStatus?.lockState !==
            VAULT_LOCK_STATE.Unlocked) {
      this.setPasswordSetupError('Unlock your local Vault to continue.');
      return false;
    }

    if (this.state.isVaultRepairMode) {
      this.complete();
      return true;
    }

    this.patch(draft => {
      draft.currentPage = WelcomePage.SyncKeyBackup;
      draft.direction = 'forward';
      draft.passwordSetup = {
        ...draft.passwordSetup,
        status: 'ready',
        errorMessage: '',
        passwordManagerSkipped: false,
      };
    });
    return true;
  }

  /**
   * Records the explicit "no password manager" choice. The choice is persisted
   * as the disabled provider so it survives onboarding, and onboarding always
   * completes — an unlocked Vault is never required for this path.
   */
  async skipPasswordSetup(): Promise<boolean> {
    if (this.state.currentPage !== WelcomePage.PasswordSetup) {
      return false;
    }

    let persisted = false;
    let errorMessage = '';
    try {
      const {success} = await this.pageHandler.setPasswordProvider(
          PASSWORD_PROVIDER_KIND.Disabled);
      persisted = success;
      if (!success) {
        errorMessage =
            'Password management stays off, but the choice could not be saved.';
      }
    } catch (error) {
      errorMessage = error instanceof Error ?
          error.message :
          'Password management stays off, but the choice could not be saved.';
    }

    if (this.state.currentPage !== WelcomePage.PasswordSetup) {
      return persisted;
    }

    if (this.state.isVaultRepairMode) {
      this.complete();
      return persisted;
    }

    this.patch(draft => {
      draft.currentPage = WelcomePage.SyncKeyBackup;
      draft.direction = 'forward';
      draft.passwordSetup = {
        ...draft.passwordSetup,
        status: 'ready',
        errorMessage,
        passwordManagerSkipped: true,
      };
    });
    return persisted;
  }

  async generateSyncKey(): Promise<{syncKey: string; roomId: string; recoveryPhrase: string} | null> {
    this.patch(draft => {
      draft.syncKeyBackup = {...draft.syncKeyBackup, status: 'generating', errorMessage: ''};
    });
    try {
      const {syncKey, roomId, recoveryPhrase} = await this.pageHandler.generateSyncKey();
      this.patch(draft => {
        draft.syncKeyBackup = {
          ...draft.syncKeyBackup,
          status: 'ready',
          errorMessage: '',
          syncKey,
          roomId,
          recoveryPhrase,
        };
      });
      return {syncKey, roomId, recoveryPhrase};
    } catch {
      this.patch(draft => {
        draft.syncKeyBackup = {
          ...draft.syncKeyBackup,
          status: 'error',
          errorMessage: 'Failed to generate sync key.',
        };
      });
      return null;
    }
  }

  async startSync(): Promise<boolean> {
    const {roomId, recoveryPhrase} = this.state.syncKeyBackup;
    if (!roomId || !recoveryPhrase) {
      return false;
    }
    try {
      const {ok, errorMessage} = await this.pageHandler.startSync(roomId, recoveryPhrase);
      this.patch(draft => {
        draft.syncKeyBackup = {
          ...draft.syncKeyBackup,
          errorMessage: ok ? '' : (errorMessage || 'Failed to start sync. Please try again.'),
        };
      });
      return ok;
    } catch {
      this.patch(draft => {
        draft.syncKeyBackup = {
          ...draft.syncKeyBackup,
          errorMessage: 'Failed to start sync. Please try again.',
        };
      });
      return false;
    }
  }

  setSyncKeyBackupSavedToFile(saved: boolean): void {
    this.patch(draft => {
      draft.syncKeyBackup = {
        ...draft.syncKeyBackup,
        savedToFile: saved,
        status: saved ? 'saved' : draft.syncKeyBackup.status,
      };
    });
  }

  async saveSyncKeyBackup(content: string): Promise<boolean> {
    const {saved, errorMessage} = await this.pageHandler.saveSyncKeyBackup(content);
    if (!saved && errorMessage) {
      throw new Error(errorMessage);
    }
    return saved;
  }

  advanceFromSyncKeyBackup(): boolean {
    if (this.state.currentPage !== WelcomePage.SyncKeyBackup) {
      return false;
    }
    if (!this.state.syncKeyBackup.savedToFile) {
      this.patch(draft => {
        draft.syncKeyBackup = {
          ...draft.syncKeyBackup,
          status: 'error',
          errorMessage: 'Save your sync key backup file to continue.',
        };
      });
      return false;
    }

    this.patch(draft => {
      draft.currentPage = WelcomePage.Completion;
      draft.direction = 'forward';
      draft.syncKeyBackup = {
        status: 'idle',
        errorMessage: '',
        recoveryPhrase: null,
        roomId: null,
        syncKey: null,
        savedToFile: true,
      };
    });
    return true;
  }

  setAppearance(mode: AppearanceMode): void {
    this.patch(draft => {
      draft.appearance = mode;
    });
    applyAppearanceMode(mode);
  }

  markImportStarted(): void {
    this.patch(draft => {
      draft.importStarted = true;
    });
  }

  applyDefaultBrowserChoice(): void {
    if (this.state.setAsDefaultRequested) {
      this.pageHandler.setAsDefaultBrowser();
    }
  }

  setSearchEngine(keyword: string): void {
    this.patch(draft => {
      draft.selectedEngine = keyword;
    });
    this.pageHandler.setDefaultSearchEngine(keyword);
  }

  toggleEssential(url: string): void {
    this.patch(draft => {
      if (draft.selectedEssentials.has(url)) {
        draft.selectedEssentials.delete(url);
      } else {
        draft.selectedEssentials.add(url);
      }
    });
  }

  favoriteSelectedEssentials(): void {
    const urls = [...this.state.selectedEssentials];
    if (urls.length > 0) {
      this.pageHandler.favoriteEssentialSites(urls);
    }
  }

  selectTheme(themeId: string): void {
    this.patch(draft => {
      draft.selectedTheme = themeId;
    });
  }

  setAuthMode(mode: 'signin' | 'signup'): void {
    this.patch(draft => {
      draft.auth = {...draft.auth, mode, errorMessage: ''};
    });
  }

  setAuthEmail(email: string): void {
    this.patch(draft => {
      draft.auth = {...draft.auth, email};
    });
  }

  setAuthPassword(password: string): void {
    this.patch(draft => {
      draft.auth = {...draft.auth, password};
    });
  }

  setAuthDisplayName(displayName: string): void {
    this.patch(draft => {
      draft.auth = {...draft.auth, displayName};
    });
  }

  setIsReauthMode(isReauthMode: boolean): void {
    this.patch(draft => {
      draft.auth = {...draft.auth, isReauthMode};
    });
  }

  async submitGoogleSignIn(): Promise<boolean> {
    this.patch(draft => {
      draft.auth = {...draft.auth, status: 'submitting', errorMessage: ''};
    });
    const {ok, errorMessage} = await this.pageHandler.signInWithGoogleFromWelcome();
    if (ok) {
      this.patch(draft => {
        draft.auth = {...draft.auth, status: 'success', errorMessage: ''};
      });
      await this.refreshRelayAccountStatus();
    } else {
      this.patch(draft => {
        draft.auth = {
          ...draft.auth,
          status: 'error',
          errorMessage: errorMessage || 'Google sign in failed.',
        };
      });
    }
    return ok;
  }

  async submitSignIn(): Promise<boolean> {
    const {email, password} = this.state.auth;
    if (email.trim() === '' || password.trim() === '') {
      this.patch(draft => {
        draft.auth = {...draft.auth, status: 'error', errorMessage: 'Email and password are required.'};
      });
      return false;
    }
    this.patch(draft => {
      draft.auth = {...draft.auth, status: 'submitting', errorMessage: ''};
    });
    const {ok, errorMessage} = await this.pageHandler.loginFromWelcome(email.trim(), password);
    if (ok) {
      this.patch(draft => {
        draft.auth = {...draft.auth, status: 'success', errorMessage: ''};
      });
      await this.refreshRelayAccountStatus();
    } else {
      this.patch(draft => {
        draft.auth = {
          ...draft.auth,
          status: 'error',
          errorMessage: errorMessage || 'Sign in failed.',
        };
      });
    }
    return ok;
  }

  async submitSignUp(): Promise<boolean> {
    const {email, password, displayName} = this.state.auth;
    if (email.trim() === '' || password.trim() === '') {
      this.patch(draft => {
        draft.auth = {...draft.auth, status: 'error', errorMessage: 'Email and password are required.'};
      });
      return false;
    }
    this.patch(draft => {
      draft.auth = {...draft.auth, status: 'submitting', errorMessage: ''};
    });
    const {ok, errorMessage} = await this.pageHandler.signupFromWelcome(email.trim(), password, displayName.trim());
    if (ok) {
      this.patch(draft => {
        draft.auth = {...draft.auth, status: 'success', errorMessage: ''};
      });
      await this.refreshRelayAccountStatus();
    } else {
      this.patch(draft => {
        draft.auth = {
          ...draft.auth,
          status: 'error',
          errorMessage: errorMessage || 'Sign up failed.',
        };
      });
    }
    return ok;
  }

  async refreshRelayAccountStatus(): Promise<void> {
    const {signedIn, welcomeCompleted, hasStoredSession} =
        await this.pageHandler.getRelayAccountStatusFromWelcome();
    this.patch(draft => {
      draft.auth = {
        ...draft.auth,
        status: signedIn ? 'success' : 'idle',
        isReauthMode: welcomeCompleted,
        hasStoredSession,
      };
    });
  }

  previewTheme(themeJson: string): void {
    this.pageHandler.previewTheme(themeJson);
  }

  applyTheme(themeJson: string): void {
    this.pageHandler.applyTheme(themeJson);
  }

  async openThemePickerDialog(currentJson: string): Promise<string> {
    const {pickedThemeJson} = await this.pageHandler.openThemePickerDialog(currentJson);
    return pickedThemeJson;
  }

  complete(): void {
    this.favoriteSelectedEssentials();
    this.pageHandler.finishOnboarding();
  }

  openMainBrowser(): void {
    this.pageHandler.finishOnboarding();
  }

  devSkipOnboarding(): void {
    this.pageHandler.devSkipOnboarding();
  }

  private setPasswordSetupSubmitting(): void {
    this.patch(draft => {
      draft.passwordSetup = {
        ...draft.passwordSetup,
        status: 'submitting',
        errorMessage: '',
      };
    });
  }

  private setPasswordSetupError(errorMessage: string): void {
    this.patch(draft => {
      draft.passwordSetup = {
        ...draft.passwordSetup,
        status: 'error',
        errorMessage,
      };
    });
  }

  private async runVaultOperation(
      operation: () => Promise<{result: VaultOperationResult}>,
      fallbackError: string): Promise<boolean> {
    this.setPasswordSetupSubmitting();
    try {
      const {result} = await operation();
      if (!result.success ||
          result.status?.lockState !== VAULT_LOCK_STATE.Unlocked) {
        this.patch(draft => {
          draft.passwordSetup = {
            ...draft.passwordSetup,
            status: 'error',
            errorMessage: result.errorMessage || fallbackError,
            vaultStatus: result.status,
          };
        });
        return false;
      }

      this.patch(draft => {
        draft.passwordSetup = {
          ...draft.passwordSetup,
          status: 'ready',
          errorMessage: '',
          vaultStatus: result.status,
        };
      });
      return true;
    } catch (error) {
      this.setPasswordSetupError(
          error instanceof Error ? error.message : fallbackError);
      return false;
    }
  }

  skip(): void {
    this.applyDefaultBrowserChoice();
    this.pageHandler.finishOnboarding();
  }

  private notify(): void {
    notifyListeners(this.listeners);
  }

  private patch(mutator: (draft: MutableWelcomeState) => void): void {
    const draft = makeMutableState(this.state);
    mutator(draft);
    this.state = freezeMutableState(draft);
    this.notify();
  }
}

function makeMutableState(state: WelcomeState): MutableWelcomeState {
  return {
    mailAccounts: state.mailAccounts,
    currentPage: state.currentPage,
    direction: state.direction,
    appearance: state.appearance,
    availableBrowsers: [...state.availableBrowsers],
    importState: state.importState,
    importItemSelections: new Map(state.importItemSelections),
    setAsDefaultRequested: state.setAsDefaultRequested,
    importStarted: state.importStarted,
    importStage: state.importStage,
    searchEngines: [...state.searchEngines],
    selectedEngine: state.selectedEngine,
    essentialSites: [...state.essentialSites],
    selectedEssentials: new Set(state.selectedEssentials),
    auth: state.auth,
    isVaultRepairMode: state.isVaultRepairMode,
    aiSetup: state.aiSetup,
    passwordSetup: state.passwordSetup,
    syncKeyBackup: state.syncKeyBackup,
    selectedTheme: state.selectedTheme,
    localizedStrings: new Map(state.localizedStrings),
    isLocalDev: state.isLocalDev,
  };
}

function freezeMutableState(state: MutableWelcomeState): WelcomeState {
  return {
    mailAccounts: state.mailAccounts,
    currentPage: state.currentPage,
    direction: state.direction,
    appearance: state.appearance,
    availableBrowsers: [...state.availableBrowsers],
    importState: state.importState,
    importItemSelections: new Map(state.importItemSelections),
    setAsDefaultRequested: state.setAsDefaultRequested,
    importStarted: state.importStarted,
    importStage: state.importStage,
    searchEngines: [...state.searchEngines],
    selectedEngine: state.selectedEngine,
    essentialSites: [...state.essentialSites],
    selectedEssentials: new Set(state.selectedEssentials),
    auth: state.auth,
    isVaultRepairMode: state.isVaultRepairMode,
    aiSetup: state.aiSetup,
    passwordSetup: state.passwordSetup,
    syncKeyBackup: state.syncKeyBackup,
    selectedTheme: state.selectedTheme,
    localizedStrings: new Map(state.localizedStrings),
    isLocalDev: state.isLocalDev,
  };
}

function applyAppearanceMode(mode: AppearanceMode): void {
  if (typeof document === 'undefined') {
    return;
  }
  const root = document.documentElement;
  const prefersDark = typeof window !== 'undefined' && window.matchMedia
      ? window.matchMedia('(prefers-color-scheme: dark)').matches
      : false;
  const resolved: 'light' | 'dark' = mode === 'system'
      ? (prefersDark ? 'dark' : 'light')
      : mode;
  root.setAttribute('data-theme', resolved);
  root.style.colorScheme = resolved;
}
