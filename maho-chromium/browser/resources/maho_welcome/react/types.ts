import {
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
  PasswordProviderOption,
  PasswordProviderStatus,
  VaultLockState,
  VaultStatus,
} from '../../maho_settings/maho_settings.mojom-webui.js';

export enum WelcomePage {
  Auth = 0,
  Splash = 1,
  Appearance = 2,
  SourceSelect = 3,
  ImportProgress = 4,
  SearchEngine = 5,
  Essentials = 6,
  PasswordSetup = 7,
  Completion = 8,
}

export const TOTAL_PAGES = 9;
export const PASSWORD_PROVIDER_KIND = {
  MahoNative: 0,
  Bitwarden: 1,
  OnePassword: 2,
} as const satisfies Record<string, PasswordProviderKind>;
export const VAULT_LOCK_STATE = {
  Uninitialized: 0,
  Locked: 1,
  Unlocked: 2,
  AutoLocked: 3,
} as const satisfies Record<string, VaultLockState>;
const PASSWORD_PROVIDER_ORDER = [
  PASSWORD_PROVIDER_KIND.MahoNative,
  PASSWORD_PROVIDER_KIND.Bitwarden,
  PASSWORD_PROVIDER_KIND.OnePassword,
] as const satisfies readonly PasswordProviderKind[];

export type AppearanceMode = 'system' | 'light' | 'dark';
export type ImportStage = 'A' | 'B';

export interface AuthState {
  readonly mode: 'signin' | 'signup';
  readonly email: string;
  readonly password: string;
  readonly displayName: string;
  readonly status: 'idle' | 'submitting' | 'success' | 'error';
  readonly errorMessage: string;
  // True when onboarding was already completed → skip the wizard and go
  // straight to browser after auth. NOTE: this does NOT mean the user ever
  // had a relay session; use hasStoredSession for genuine session-expiry UI.
  readonly isReauthMode: boolean;
  readonly hasStoredSession: boolean;
}

// Mojo-generated constants are imported directly from maho_welcome.mojom-webui.js.


export type ImportPhase = 'idle' | 'running' | 'complete' | 'error';

export interface ImportState {
  readonly phase: ImportPhase;
  readonly statusMessage: string;
  readonly browserIndex: number | null;
}

export interface PasswordSetupState {
  readonly status: 'loading' | 'ready' | 'submitting' | 'error';
  readonly errorMessage: string;
  readonly providerOptions: readonly PasswordProviderOption[];
  readonly providerStatus: PasswordProviderStatus | null;
  readonly vaultStatus: VaultStatus | null;
}

export function orderProviderOptions(
    options: readonly PasswordProviderOption[]): readonly PasswordProviderOption[] {
  return PASSWORD_PROVIDER_ORDER.map(
      provider => options.find(option => option.provider === provider))
      .filter((option): option is PasswordProviderOption => option !== undefined);
}

export function getProviderHelperText(
    option: PasswordProviderOption, unavailableHelperText: string): string {
  return option.isAvailable
    ? option.description
    : `${option.description} ${unavailableHelperText}`;
}

export interface WelcomeState {
  readonly currentPage: WelcomePage;
  readonly direction: 'forward' | 'backward';
  readonly appearance: AppearanceMode;

  readonly availableBrowsers: readonly BrowserProfile[];
  readonly importState: ImportState;
  readonly importItemSelections: ReadonlyMap<number, number>;
  readonly setAsDefaultRequested: boolean;
  readonly importStarted: boolean;
  readonly importStage: ImportStage;

  readonly searchEngines: readonly SearchEngineInfo[];
  readonly selectedEngine: string | null;

  readonly essentialSites: readonly EssentialSite[];
  readonly selectedEssentials: ReadonlySet<string>;

  readonly auth: AuthState;
  readonly passwordSetup: PasswordSetupState;

  readonly selectedTheme: string | null;
  readonly localizedStrings: ReadonlyMap<string, string>;
  readonly isLocalDev: boolean;
}

export function createInitialState(): WelcomeState {
  return {
    currentPage: WelcomePage.Auth,
    direction: 'forward',
    appearance: 'system',
    availableBrowsers: [],
    importState: {phase: 'idle', statusMessage: '', browserIndex: null},
    importItemSelections: new Map(),
    setAsDefaultRequested: false,
    importStarted: false,
    importStage: 'A',
    searchEngines: [],
    selectedEngine: null,
    essentialSites: [],
    selectedEssentials: new Set(),
    auth: {
      mode: 'signin',
      email: '',
      password: '',
      displayName: '',
      status: 'idle',
      errorMessage: '',
      isReauthMode: false,
      hasStoredSession: false,
    },
    passwordSetup: {
      status: 'loading',
      errorMessage: '',
      providerOptions: [],
      providerStatus: null,
      vaultStatus: null,
    },
    selectedTheme: null,
    localizedStrings: new Map(),
    isLocalDev: false,
  };
}
