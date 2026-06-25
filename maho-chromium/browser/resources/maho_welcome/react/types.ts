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
  Splash = 0,
  SourceSelect = 1,
  ImportProgress = 2,
  SearchEngine = 3,
  Essentials = 4,
  Completion = 5,
}

export const TOTAL_PAGES = 6;

export type ImportStage = 'A' | 'B';

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

  readonly selectedTheme: string | null;
  readonly localizedStrings: ReadonlyMap<string, string>;
}

export function createInitialState(): WelcomeState {
  return {
    currentPage: WelcomePage.Splash,
    direction: 'forward',
    availableBrowsers: [],
    importState: {phase: 'idle', statusMessage: '', browserIndex: null},
    importItemSelections: new Map(),
    setAsDefaultRequested: true,
    importStarted: false,
    importStage: 'A',
    searchEngines: [],
    selectedEngine: null,
    essentialSites: [],
    selectedEssentials: new Set(),
    selectedTheme: null,
    localizedStrings: new Map(),
  };
}
