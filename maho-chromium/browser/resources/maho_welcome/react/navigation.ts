import {WelcomePage} from './types.js';
import type {ImportStage} from './types.js';

export type NavigationTargetKind =
  | 'body'
  | 'button'
  | 'input'
  | 'select'
  | 'combobox'
  | 'other';

export interface GlobalKeyNavigationInput {
  readonly key: string;
  readonly defaultPrevented: boolean;
  readonly repeat: boolean;
  readonly currentPage: WelcomePage;
  readonly importStage: ImportStage;
  readonly targetKind: NavigationTargetKind;
}

export interface GlobalKeyNavigationActions {
  readonly submitAuth: () => void;
  readonly nextPage: () => void;
  readonly complete: () => void;
  readonly openMigrationDialog: () => void;
  readonly skipImport: () => void;
}

export interface FooterPrimaryNavigationInput {
  readonly currentPage: WelcomePage;
  readonly importStage: ImportStage;
  readonly hasAvailableBrowsers: boolean;
  readonly selectedBrowserIndex: number | null;
}

export interface FooterPrimaryNavigationActions {
  readonly submitAuth: () => Promise<void>;
  readonly startImport: (browserIndex: number) => void;
  readonly openMigrationDialog: () => void;
  readonly setSetAsDefault: (value: boolean) => void;
  readonly nextPage: () => void;
  readonly complete: () => void;
  readonly openMainBrowser: () => void;
}

export function isStandalonePage(page: WelcomePage): boolean {
  return page === WelcomePage.Splash || page === WelcomePage.PlanSelect;
}

export function shouldShowFooterPrimary(page: WelcomePage): boolean {
  return page !== WelcomePage.PasswordSetup;
}

export function shouldRenderContentActions(page: WelcomePage): boolean {
  return page !== WelcomePage.PasswordSetup;
}

export function shouldShowSidebarSkip(page: WelcomePage): boolean {
  return page !== WelcomePage.PasswordSetup;
}

export function applyGlobalKeyNavigation(
    input: GlobalKeyNavigationInput,
    actions: GlobalKeyNavigationActions): boolean {
  if (input.defaultPrevented || input.repeat) {
    return false;
  }

  if (input.currentPage === WelcomePage.Auth) {
    if (input.key !== 'Enter' || input.targetKind === 'button') {
      return false;
    }
    actions.submitAuth();
    return true;
  }

  if (input.currentPage === WelcomePage.PasswordSetup &&
      (input.key === 'Enter' || input.key === 'Escape')) {
    return false;
  }

  if (input.key === 'Enter') {
    if (isStandalonePage(input.currentPage)) {
      actions.nextPage();
      return true;
    }
    if (input.currentPage === WelcomePage.Completion) {
      actions.complete();
      return true;
    }
    if (input.currentPage === WelcomePage.SourceSelect && input.importStage === 'A') {
      actions.openMigrationDialog();
      return true;
    }
    actions.nextPage();
    return true;
  }

  if (input.key === 'Escape') {
    if (input.currentPage !== WelcomePage.Completion &&
        !isStandalonePage(input.currentPage)) {
      if (input.currentPage === WelcomePage.SourceSelect) {
        actions.skipImport();
        return true;
      }
      actions.nextPage();
      return true;
    }
  }

  return false;
}

export async function applyFooterPrimaryNavigation(
    input: FooterPrimaryNavigationInput,
    actions: FooterPrimaryNavigationActions): Promise<void> {
  if (input.currentPage === WelcomePage.Auth) {
    await actions.submitAuth();
    return;
  }
  if (input.currentPage === WelcomePage.SourceSelect && input.importStage === 'A') {
    if (input.hasAvailableBrowsers && input.selectedBrowserIndex !== null) {
      actions.startImport(input.selectedBrowserIndex);
    } else {
      actions.openMigrationDialog();
    }
    return;
  }
  if (input.currentPage === WelcomePage.SourceSelect && input.importStage === 'B') {
    actions.setSetAsDefault(true);
    actions.nextPage();
    return;
  }
  if (input.currentPage === WelcomePage.Completion) {
    actions.complete();
    actions.openMainBrowser();
    return;
  }
  if (input.currentPage === WelcomePage.PasswordSetup) {
    return;
  }
  actions.nextPage();
}
