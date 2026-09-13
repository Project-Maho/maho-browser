// Copyright 2026 Maho Browser. All rights reserved.
//
// Standalone shim of the auto-generated maho_welcome.mojom-webui.js bindings.
// Keep in sync with maho_welcome.mojom AND maho_welcome.mojom-webui.js.d.ts.

/** @typedef {{keyword: string, name: string, iconUrl: string, isDefault: boolean}} SearchEngineInfo */
/** @typedef {{url: string, name: string, iconPath: string}} EssentialSite */
/** @typedef {{index: number, name: string, servicesSupported: number}} BrowserProfile */
/** @typedef {{wasCancelled: boolean, importedItemsBitmask: number}} MigrationDialogResult */

// Mojo-generated bitmask constants (single source of truth: maho_welcome.mojom).
// Bits MUST match Rust ImportServices in maho/crates/maho-import/src/lib.rs.
export const IMPORT_HISTORY    = 0x01;
export const IMPORT_BOOKMARKS  = 0x02;
export const IMPORT_PASSWORDS  = 0x04;
export const IMPORT_AUTOFILL   = 0x08;
export const IMPORT_COOKIES    = 0x10;
export const IMPORT_WORKSPACES = 0x20;
export const IMPORT_FAVICONS   = 0x40;
export const IMPORT_ESSENTIAL  = 0x80;

export class PageCallbackRouter {
  constructor() {
    this.$ = {bindNewPipeAndPassRemote: () => {}};
    this.onImportProgress = {addListener: (_listener) => {}};
  }
}

export class PageHandlerRemote {
  constructor() {
    this.$ = {bindNewPipeAndPassReceiver: () => {}};
  }
  getAvailableBrowsers() { return Promise.resolve({browsers: []}); }
  startImport(_browserIndex, _items) {}
  isDefaultBrowser() { return Promise.resolve({isDefault: false}); }
  setAsDefaultBrowser() {}
  getSearchEngines() { return Promise.resolve({engines: []}); }
  setDefaultSearchEngine(_keyword) {}
  getEssentialSites() { return Promise.resolve({sites: []}); }
  favoriteEssentialSites(_urls) {}
  previewTheme(_themeJson) {}
  applyTheme(_themeJson) {}
  clearThemePreview() {}
  openMigrationDialog() {
    return Promise.resolve({result: {wasCancelled: true, importedItemsBitmask: 0}});
  }
  getLocalizedStrings() { return Promise.resolve({strings: {}}); }
  saveSyncKeyBackup(_content) {
    return Promise.resolve({saved: false, errorMessage: 'Native file saving is unavailable in this preview.'});
  }
  setTranslationProvider(_provider) { return Promise.resolve({ok: true}); }
  getByokCredentialConfigured() { return Promise.resolve({configured: false}); }
  openByokSettingsDialog() { return Promise.resolve({configured: false}); }
  getSubscriptionCheckoutUrl(_tier) { return Promise.resolve({checkoutUrl: ''}); }
  finishOnboarding() {}
  openFDASettings() {}
}

export class PageHandlerFactory {
  static getRemote() {
    return {createPageHandler: (_page, _handler) => {}};
  }
}
