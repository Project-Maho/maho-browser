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

import type {MailAccount, MailOnboardingVisualFixture} from '../../maho_common/react/mail/mail_onboarding_api.js';

export enum WelcomePage {
  Auth = 0,
  Splash = 1,
  Appearance = 2,
  SourceSelect = 3,
  ImportProgress = 4,
  SearchEngine = 5,
  Essentials = 6,
  AIProvider = 7,
  PasswordSetup = 8,
  SyncKeyBackup = 9,
  Completion = 10,
  PlanSelect = 11,
}

export const TOTAL_PAGES = 12;
export const PASSWORD_PROVIDER_KIND = {
  MahoNative: 0,
  Bitwarden: 1,
  OnePassword: 2,
  // Persisted choice for "no password manager"; keeps the onboarding decision
  // durable instead of leaving the provider at its default.
  Disabled: 3,
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

const IMPORT_TYPE_LABELS: ReadonlyArray<readonly [number, string]> = [
  [IMPORT_BOOKMARKS, 'Bookmarks'],
  [IMPORT_HISTORY, 'History'],
  [IMPORT_PASSWORDS, 'Passwords'],
  [IMPORT_COOKIES, 'Cookies'],
  [IMPORT_AUTOFILL, 'Autofill'],
  [IMPORT_WORKSPACES, 'Spaces'],
  [IMPORT_FAVICONS, 'Site icons'],
];

// Names of the import types in |bits|, in display order. Bits without a
// user-facing name are reported once as "Other data".
export function describeImportTypes(bits: number): string[] {
  const names: string[] = [];
  let rest = bits;
  for (const [flag, label] of IMPORT_TYPE_LABELS) {
    if (bits & flag) {
      names.push(label);
      rest &= ~flag;
    }
  }
  if (rest !== 0) {
    names.push('Other data');
  }
  return names;
}

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
  readonly passwordManagerSkipped: boolean;
}

export type SyncKeyMode = 'escrow' | 'client_held';

export interface SyncKeyBackupState {
  readonly status: 'idle' | 'generating' | 'ready' | 'saving' | 'saved' | 'starting' | 'error';
  readonly errorMessage: string;
  readonly recoveryPhrase: string | null;
  readonly roomId: string | null;
  readonly syncKey: string | null;
  readonly savedToFile: boolean;
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

export interface AiSetupState {
  readonly provider: 'maho-managed' | 'byok' | '';
  readonly status: 'idle' | 'submitting' | 'success' | 'error';
  readonly errorMessage: string;
  readonly skipped: boolean;
}

export interface WelcomeState {
  readonly mailAccounts: readonly MailAccount[];
  readonly visualMailStage?: MailOnboardingVisualFixture;
  readonly currentPage: WelcomePage;
  readonly direction: 'forward' | 'backward';
  readonly appearance: AppearanceMode;

  readonly availableBrowsers: readonly BrowserProfile[];
  readonly importState: ImportState;
  readonly importItemSelections: ReadonlyMap<number, number>;
  // Type bits that failed in the last import that brought other data in.
  // Non-zero means the import was partial; cleared when a new import starts.
  readonly importFailedTypes: number;
  readonly setAsDefaultRequested: boolean;
  readonly importStarted: boolean;
  readonly importStage: ImportStage;

  readonly searchEngines: readonly SearchEngineInfo[];
  readonly searchEnginesLoaded: boolean;
  readonly selectedEngine: string | null;

  readonly essentialSites: readonly EssentialSite[];
  readonly selectedEssentials: ReadonlySet<string>;

  readonly auth: AuthState;
  // "Continue without an account": onboarding finishes without a relay
  // session, the local Vault gets a passphrase, and sync stays off until the
  // user signs in later.
  readonly isLocalOnly: boolean;
  readonly isVaultRepairMode: boolean;
  readonly aiSetup: AiSetupState;
  readonly passwordSetup: PasswordSetupState;
  readonly syncKeyBackup: SyncKeyBackupState;
  // "client_held" when the signed-in account holds its own Sync keys (the
  // relay has no seed): this device joins with the 24-word recovery phrase
  // instead of generating a new key.
  readonly syncKeyMode: SyncKeyMode;

  readonly selectedTheme: string | null;
  readonly localizedStrings: ReadonlyMap<string, string>;
  readonly isLocalDev: boolean;
}

export function createInitialState(): WelcomeState {
  return {
    mailAccounts: [],
    currentPage: WelcomePage.Auth,
    direction: 'forward',
    appearance: 'system',
    availableBrowsers: [],
    importState: {phase: 'idle', statusMessage: '', browserIndex: null},
    importItemSelections: new Map(),
    importFailedTypes: 0,
    setAsDefaultRequested: false,
    importStarted: false,
    importStage: 'A',
    searchEngines: [],
    searchEnginesLoaded: false,
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
    isLocalOnly: false,
    isVaultRepairMode: false,
    aiSetup: {
      provider: '',
      status: 'idle',
      errorMessage: '',
      skipped: false,
    },
    passwordSetup: {
      status: 'loading',
      errorMessage: '',
      providerOptions: [],
      providerStatus: null,
      vaultStatus: null,
      passwordManagerSkipped: false,
    },
    syncKeyBackup: {
      status: 'idle',
      errorMessage: '',
      recoveryPhrase: null,
      roomId: null,
      syncKey: null,
      savedToFile: false,
    },
    syncKeyMode: 'escrow',
    selectedTheme: null,
    localizedStrings: new Map([
      ['IDS_MAHO_WELCOME_SYNC_BACKUP_TITLE', 'Back up your sync recovery key'],
      ['IDS_MAHO_WELCOME_SYNC_BACKUP_BODY', 'Save your recovery phrase to a local file. You need this phrase to access your encrypted sync data on another device.'],
      ['IDS_MAHO_WELCOME_SYNC_BACKUP_START_SYNC', 'Start syncing'],
      ['IDS_MAHO_WELCOME_SYNC_BACKUP_SAVE_FILE', 'Save file'],
      ['IDS_MAHO_WELCOME_SYNC_BACKUP_FAIL_RETRY', 'Download failed. Please try saving your sync key again.'],
      ['IDS_MAHO_WELCOME_NO_PASSWORD_MANAGER', 'No password manager'],
      ['IDS_MAHO_WELCOME_LOCAL_VAULT_EXPLANATION', 'Local Vault keeps your passwords securely encrypted on this device.'],
    ]),
    isLocalDev: false,
  };
}
