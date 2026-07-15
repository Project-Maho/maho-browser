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

export enum WelcomePage {
  Auth = 0,
  Splash = 1,
  Appearance = 2,
  SourceSelect = 3,
  ImportProgress = 4,
  SearchEngine = 5,
  Essentials = 6,
  Completion = 7,
}

export const TOTAL_PAGES = 8;

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
    selectedTheme: null,
    localizedStrings: new Map(),
    isLocalDev: false,
  };
}
