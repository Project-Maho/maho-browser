import type {
  PasswordProviderKind,
  PasswordProviderOption,
  PasswordProviderStatus,
  VaultOperationResult,
} from '../maho_settings/maho_settings.mojom-webui.js';

export interface SearchEngineInfo {
  keyword: string;
  name: string;
  iconUrl: string;
  isDefault: boolean;
}

export interface EssentialSite {
  url: string;
  name: string;
  iconPath: string;
}

export interface BrowserProfile {
  index: number;
  name: string;
  servicesSupported: number;
}

export interface MigrationDialogResult {
  wasCancelled: boolean;
  importedItemsBitmask: number;
}

export class PageCallbackRouter {
  $: {
    bindNewPipeAndPassRemote(): unknown;
  };
  onImportProgress: {
    addListener(listener: (type: number, itemsImported: number, error: string, complete: boolean) => void): void;
  };
}

export class PageHandlerRemote {
  $: {
    bindNewPipeAndPassReceiver(): unknown;
  };
  getAvailableBrowsers(): Promise<{browsers: BrowserProfile[]}>;
  startImport(browserIndex: number, items: number): void;
  isDefaultBrowser(): Promise<{isDefault: boolean}>;
  setAsDefaultBrowser(): void;
  getSearchEngines(): Promise<{engines: SearchEngineInfo[]}>;
  setDefaultSearchEngine(keyword: string): void;
  getEssentialSites(): Promise<{sites: EssentialSite[]}>;
  favoriteEssentialSites(urls: string[]): void;
  previewTheme(themeJson: string): void;
  applyTheme(themeJson: string): void;
  clearThemePreview(): void;
  openMigrationDialog(): Promise<{result: MigrationDialogResult}>;
  openThemePickerDialog(currentThemeJson: string): Promise<{pickedThemeJson: string}>;
  getLocalizedStrings(): Promise<{strings: Record<string, string>}>;
  signupFromWelcome(email: string, password: string, displayName: string): Promise<{ok: boolean, errorMessage: string}>;
  loginFromWelcome(email: string, password: string): Promise<{ok: boolean, errorMessage: string}>;
  signInWithGoogleFromWelcome(): Promise<{ok: boolean, errorMessage: string}>;
  getRelayAccountStatusFromWelcome(): Promise<{signedIn: boolean, tier: string, subscriptionStatus: string, welcomeCompleted: boolean, hasStoredSession: boolean, isLocalDev: boolean}>;
  getPasswordProviderOptions(): Promise<{options: PasswordProviderOption[]}>;
  getPasswordProviderStatus(): Promise<{status: PasswordProviderStatus}>;
  setPasswordProvider(provider: PasswordProviderKind): Promise<{success: boolean}>;
  getVaultStatus(): Promise<{result: VaultOperationResult}>;
  initializeVault(masterPassphrase: string, recoverySecret: string): Promise<{
    result: VaultOperationResult;
  }>;
  unlockVault(masterPassphrase: string): Promise<{result: VaultOperationResult}>;
  generateSyncKey(): Promise<{syncKey: string, roomId: string, recoveryPhrase: string}>;
  startSync(roomId: string, recoveryPhrase: string): Promise<{ok: boolean, errorMessage: string}>;
  setTranslationProvider(provider: string): Promise<{ok: boolean}>;
  getSubscriptionCheckoutUrl(tier: string): Promise<{checkoutUrl: string}>;
  finishOnboarding(): void;
  devSkipOnboarding(): void;
}

export class PageHandlerFactory {
  static getRemote(): PageHandlerFactory;
  createPageHandler(page: unknown, handler: unknown): void;
}

export declare const IMPORT_BOOKMARKS: number;
export declare const IMPORT_PASSWORDS: number;
export declare const IMPORT_HISTORY: number;
export declare const IMPORT_COOKIES: number;
export declare const IMPORT_AUTOFILL: number;
export declare const IMPORT_WORKSPACES: number;
export declare const IMPORT_FAVICONS: number;
