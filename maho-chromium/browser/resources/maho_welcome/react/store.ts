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
import {notifyListeners} from '../../maho_common/react/store_utils.js';
import type {Listener} from '../../maho_common/react/store_utils.js';
import {
  createInitialState,
  WelcomePage,
} from './types.js';
import type {ImportState, ImportStage, WelcomeState} from './types.js';

interface MutableWelcomeState {
  currentPage: WelcomePage;
  direction: 'forward' | 'backward';
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
  selectedTheme: string | null;
  localizedStrings: Map<string, string>;
}

const DEFAULT_IMPORT_ITEMS =
    IMPORT_HISTORY | IMPORT_BOOKMARKS | IMPORT_PASSWORDS |
    IMPORT_COOKIES | IMPORT_AUTOFILL | IMPORT_WORKSPACES | IMPORT_FAVICONS;

export class MahoWelcomeStore {
  private readonly callbackRouter = new PageCallbackRouter();
  private readonly listeners = new Set<Listener>();
  private readonly pageHandler = new PageHandlerRemote();
  private state: WelcomeState = createInitialState();

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

  async bootstrap(): Promise<void> {
    const [{engines}, {sites}, {browsers}, {strings}] = await Promise.all([
      this.pageHandler.getSearchEngines(),
      this.pageHandler.getEssentialSites(),
      this.pageHandler.getAvailableBrowsers(),
      this.pageHandler.getLocalizedStrings(),
    ]);
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
      draft.localizedStrings = new Map(Object.entries(strings));
    });
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
      draft.importStage = 'A';
      draft.currentPage = WelcomePage.SearchEngine;
      draft.direction = 'forward';
    });
  }

  nextPage(): void {
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

    this.patch(draft => {
      if (draft.currentPage < WelcomePage.Completion) {
        draft.direction = 'forward';
        draft.currentPage = (draft.currentPage + 1) as WelcomePage;
      }
    });
  }

  prevPage(): void {
    this.patch(draft => {
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
    currentPage: state.currentPage,
    direction: state.direction,
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
    selectedTheme: state.selectedTheme,
    localizedStrings: new Map(state.localizedStrings),
  };
}

function freezeMutableState(state: MutableWelcomeState): WelcomeState {
  return {
    currentPage: state.currentPage,
    direction: state.direction,
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
    selectedTheme: state.selectedTheme,
    localizedStrings: new Map(state.localizedStrings),
  };
}
