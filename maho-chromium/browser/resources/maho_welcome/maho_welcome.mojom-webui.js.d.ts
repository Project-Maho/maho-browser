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
  finishOnboarding(): void;
}

export class PageHandlerFactory {
  static getRemote(): PageHandlerFactory;
  createPageHandler(page: unknown, handler: unknown): void;
}

export const IMPORT_BOOKMARKS: number;
export const IMPORT_PASSWORDS: number;
export const IMPORT_HISTORY: number;
export const IMPORT_COOKIES: number;
export const IMPORT_AUTOFILL: number;
export const IMPORT_WORKSPACES: number;
export const IMPORT_FAVICONS: number;

