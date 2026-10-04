import {
  BrowserUpdateState,
  PageCallbackRouter,
  PageHandlerFactory,
  PageHandlerRemote,
} from '../mojo.js';
import type {
  AccountStatus,
  BillingInfo,
  BrowserVersionInfo,
  SettingValue,
  Invoice,
  ProfileTarget,
  SelectedProfileContext,
  ProfileMetadata,
  ProfileSearchSettings,
  ProfileDownloadSettings,
  ProfileArchiveSettings,
  ProfileMetadataUpdate,
  ProfileTargetError,
} from '../maho_settings.mojom-webui.js';
import {ProfileTargetErrorCode} from '../maho_settings.mojom-webui.js';
import {PANE_DEFINITIONS, resolveReachablePaneKey} from '../schema/panes.js';
import {SETTING_METADATA} from '../schema/setting_schema.js';
import {notifyListeners} from '../../maho_common/react/store_utils.js';
import type {Listener} from '../../maho_common/react/store_utils.js';
import {bindMojoPageHandler} from '../../maho_common/react/use_mojo.js';
import {toast} from 'sonner';

export interface SettingsDelta {
  readonly key: string;
  readonly value: string;
  readonly revision: number | bigint;
}

export interface SettingsState {
  readonly accountError: string;
  readonly accountLoading: boolean;
  readonly accountStatus: AccountStatus | null;
  readonly bootstrapError: string | null;
  readonly billingError: string;
  readonly billingInfo: BillingInfo | null;
  readonly billingLoading: boolean;
  readonly invoices: Invoice[];
  readonly invoicesLoading: boolean;
  readonly invoicesError: string;
  readonly currentPaneKey: string;
  readonly lastActionError: string | null;
  readonly savingKeys: ReadonlySet<string>;
  readonly settings: SettingValue[];
  readonly settingsRevision: bigint;
  readonly browserVersion: string;
  readonly browserChannel: string;
  readonly updateState: number;
  readonly updateSupported: boolean;
  readonly updateGuidance: string;
  readonly selectedProfileTarget: ProfileTarget | null;
  readonly selectedProfileContext: SelectedProfileContext | null;
  readonly selectedProfileMetadata: ProfileMetadata | null;
  readonly selectedProfileSearch: ProfileSearchSettings | null;
  readonly selectedProfileDownload: ProfileDownloadSettings | null;
  readonly selectedProfileArchive: ProfileArchiveSettings | null;
  readonly selectedProfileLoading: boolean;
  readonly selectedProfileError: string | null;
  readonly selectedProfileMutationError: ProfileTargetError | null;
  readonly selectedProfileMutationErrorSettingKey: string | null;
  readonly selectedProfileSavingKeys: ReadonlySet<string>;
  readonly selectedProfileLoadGeneration: bigint;
}

function createSignedOutAccountStatus(): AccountStatus {
  return {
    signedIn: false,
    email: '',
    displayName: '',
    userId: '',
    tier: '',
    subscriptionStatus: '',
    subscriptionExpiresAt: 0n,
  };
}

function formatActionError(error: unknown): string {
  return error instanceof Error ? error.message : String(error);
}

function cloneSetting(setting: SettingValue): SettingValue {
  return {...setting};
}

function cloneSettings(settings: SettingValue[]): SettingValue[] {
  return settings.map(cloneSetting);
}

function settingsEqual(a: SettingValue[], b: SettingValue[]): boolean {
  if (a.length !== b.length) {
    return false;
  }
  const mapA = new Map(a.map(s => [s.key, s.value]));
  for (const s of b) {
    if (mapA.get(s.key) !== s.value) {
      return false;
    }
  }
  return true;
}

const SELECTED_PROFILE_SETTING_ROUTES = {
  'general.prompt_for_download': 'download-prompt',
  'search.suggestions': 'search-suggestions',
  'tabs.archive_timeout': 'archive',
} as const;

type SelectedProfileSettingRoute =
    typeof SELECTED_PROFILE_SETTING_ROUTES[keyof typeof SELECTED_PROFILE_SETTING_ROUTES];

// Resolve aliases and enforce runtime feature gates. Before bootstrap, Mail is
// tentatively allowed so a valid enabled-Mail deep link does not flash away;
// bootstrap re-resolves with the authoritative mail.enabled setting.
function resolvePaneKey(key: string, mailEnabled = true): string {
  return resolveReachablePaneKey(key, mailEnabled);
}

function isMailEnabled(settings: SettingValue[]): boolean {
  return settings.some(
      setting => setting.key === 'mail.enabled' && setting.value === 'true');
}

function paneKeyFromCurrentUrl(): string {
  const param = new URLSearchParams(window.location.search).get('pane');
  return param ? resolvePaneKey(param) : PANE_DEFINITIONS[0]!.key;
}

// Writes |paneKey| into the current URL as ?pane=<key>.
// If |replace| is true, replaces the history entry. Otherwise, pushes a new entry.
function writePaneToUrl(paneKey: string, replace = false): void {
  const url = new URL(window.location.href);
  url.searchParams.set('pane', paneKey);
  if (replace) {
    window.history.replaceState(null, '', url.href);
  } else {
    window.history.pushState(null, '', url.href);
  }
}

function createInitialState(): SettingsState {
  return {
    accountError: '',
    accountLoading: false,
    accountStatus: null,
    bootstrapError: null,
    billingError: '',
    billingInfo: null,
    billingLoading: false,
    invoices: [],
    invoicesLoading: false,
    invoicesError: '',
    currentPaneKey: paneKeyFromCurrentUrl(),
    lastActionError: null,
    savingKeys: new Set<string>(),
    settings: [],
    settingsRevision: 0n,
    browserVersion: '',
    browserChannel: '',
    updateState: BrowserUpdateState.kIdle,
    updateSupported: false,
    updateGuidance: '',
    selectedProfileTarget: null,
    selectedProfileContext: null,
    selectedProfileMetadata: null,
    selectedProfileSearch: null,
    selectedProfileDownload: null,
    selectedProfileArchive: null,
    selectedProfileLoading: false,
    selectedProfileError: null,
    selectedProfileMutationError: null,
    selectedProfileMutationErrorSettingKey: null,
    selectedProfileSavingKeys: new Set<string>(),
    selectedProfileLoadGeneration: 0n,
  };
}

export class MahoSettingsStore {
  private readonly callbackRouter: PageCallbackRouter;
  private readonly listeners = new Set<Listener>();
  private readonly accountStatusChangedListenerId: number;
  private readonly pageHandler: PageHandlerRemote;
  private readonly settingsChangedListenerId: number;
  private readonly updateStateChangedListenerId: number;
  private state: SettingsState = createInitialState();
  private accountGeneration = 0;
  // RAF-coalesced notify: the C++ handler emits a full settings snapshot on
  // every change, and each notify re-renders the whole pane tree. Multiple
  // patches within one frame collapse into a single render.
  private notifyScheduled = false;
  private rafHandle: number | null = null;
  private bootstrapComplete = false;
  private toastTimers = new Map<string, ReturnType<typeof setTimeout>>();
  private pendingValues = new Map<string, string>();
  private readonly selectedProfileMutationOwners = new Map<string, symbol>();
  private readonly popstateListener: () => void;
  private refreshSettingsInFlight: Promise<void> | null = null;

  constructor() {
    const {router, handler} = bindMojoPageHandler(
        PageCallbackRouter, PageHandlerRemote, PageHandlerFactory);
    this.callbackRouter = router;
    this.pageHandler = handler;

    this.settingsChangedListenerId = this.callbackRouter.settingsChanged.addListener((settings: SettingValue[]) => {
      if (!this.bootstrapComplete) {
        return;
      }
      this.replaceSettings(settings);
    });

    this.accountStatusChangedListenerId = this.callbackRouter.accountStatusChanged.addListener((status: AccountStatus) => {
      // GetBillingInfo persists the tier and then re-broadcasts account status,
      // so refetching billing on every broadcast would loop forever
      // (broadcast → refreshBilling → GetBillingInfo → broadcast → …). Only
      // refetch when the signed-in identity actually changes.
      const prev = this.state.accountStatus;
      const isNewSignIn =
          status.signedIn && (!prev?.signedIn || prev.userId !== status.userId);

      this.patch(state => ({
        ...state,
        accountError: '',
        accountLoading: false,
        accountStatus: status,
        billingError: status.signedIn ? state.billingError : '',
        billingInfo: status.signedIn ? state.billingInfo : null,
        billingLoading: status.signedIn ? state.billingLoading : false,
        invoices: status.signedIn ? state.invoices : [],
        invoicesError: status.signedIn ? state.invoicesError : '',
        invoicesLoading: status.signedIn ? state.invoicesLoading : false,
      }));

      if (isNewSignIn) {
        void this.refreshBilling();
        void this.refreshInvoices();
      }
    });

    this.updateStateChangedListenerId =
        this.callbackRouter.onBrowserUpdateStateChanged.addListener(
            (state: number) => {
              this.patch(current => ({...current, updateState: state}));
            });

    writePaneToUrl(this.state.currentPaneKey, true);

    this.popstateListener = () => {
      const paneKey = paneKeyFromCurrentUrl();
      if (paneKey !== this.state.currentPaneKey) {
        this.patch(state => ({...state, currentPaneKey: paneKey}));
      }
    };
    window.addEventListener('popstate', this.popstateListener);
  }

  getSnapshot(): SettingsState {
    return this.state;
  }

  getCallbackRouter(): PageCallbackRouter {
    return this.callbackRouter;
  }

  getHandler(): PageHandlerRemote {
    return this.pageHandler;
  }

  subscribe(listener: Listener): () => void {
    this.listeners.add(listener);
    return () => {
      this.listeners.delete(listener);
    };
  }

  dispose(): void {
    this.callbackRouter.removeListener(this.settingsChangedListenerId);
    this.callbackRouter.removeListener(this.accountStatusChangedListenerId);
    this.callbackRouter.removeListener(this.updateStateChangedListenerId);
    (this.callbackRouter?.$ as {close?: () => void} | undefined)?.close?.();
    (this.pageHandler?.$ as {close?: () => void} | undefined)?.close?.();
    window.removeEventListener('popstate', this.popstateListener);
    for (const timer of this.toastTimers.values()) {
      clearTimeout(timer);
    }
    this.toastTimers.clear();
    if (this.rafHandle !== null) {
      cancelAnimationFrame(this.rafHandle);
      this.rafHandle = null;
    }
    this.notifyScheduled = false;
    this.listeners.clear();
  }

  async bootstrap(): Promise<void> {
    try {
      const {settings} = await this.pageHandler.getSettings();
      this.bootstrapComplete = true;
      this.replaceSettings(settings);
      const mailEnabled = isMailEnabled(settings);
      const reachablePaneKey = resolvePaneKey(this.state.currentPaneKey, mailEnabled);
      if (reachablePaneKey !== this.state.currentPaneKey) {
        this.patch(state => ({...state, currentPaneKey: reachablePaneKey}));
        writePaneToUrl(reachablePaneKey, true);
      }
      void this.refreshBrowserVersionInfo();
    } catch {
      this.bootstrapComplete = true;
      this.patch(state => ({
        ...state,
        bootstrapError: 'Unable to load settings right now. Try reopening the page.',
      }));
    }
  }

  async refreshBrowserVersionInfo(): Promise<void> {
    try {
      const {info} = await this.pageHandler.getBrowserVersionInfo();
      this.patch(state => ({
        ...state,
        browserVersion: info.version,
        browserChannel: info.channel,
        updateState: info.updateState,
        updateSupported: info.updateSupported,
        updateGuidance: info.updateGuidance,
      }));
    } catch (error) {
      console.warn('Failed to load browser version info', error);
    }
  }

  checkForBrowserUpdates(): void {
    this.patch(state => ({...state, updateState: BrowserUpdateState.kChecking}));
    void this.pageHandler.checkForBrowserUpdates();
  }

  applyBrowserUpdateAndRestart(): void {
    void this.pageHandler.applyBrowserUpdateAndRestart();
  }

  async refreshSettings(): Promise<void> {
    if (this.refreshSettingsInFlight) {
      return this.refreshSettingsInFlight;
    }
    this.refreshSettingsInFlight = (async () => {
      try {
        const {settings} = await this.pageHandler.getSettings();
        this.replaceSettings(settings);
      } catch (error) {
        console.warn('Failed to refresh settings snapshot', error);
      } finally {
        this.refreshSettingsInFlight = null;
      }
    })();
    return this.refreshSettingsInFlight;
  }

  /**
   * Applies a revision-tracked settings delta ({ key, value, revision }).
   * If bootstrap is incomplete or there is a revision mismatch/gap, falls back
   * to requesting a full settings snapshot from the backend.
   * Returns true if the delta was applied directly, false if ignored (stale) or if
   * fallback snapshot was triggered.
   */
  applySettingsDelta(delta: SettingsDelta | { key: string; value: string; revision: number | bigint }): boolean {
    if (!this.bootstrapComplete) {
      void this.refreshSettings();
      return false;
    }

    const deltaRev = typeof delta.revision === 'bigint' ? delta.revision : BigInt(delta.revision);
    const currentRev = this.state.settingsRevision;

    // Stale or already applied delta: ignore
    if (deltaRev <= currentRev) {
      return false;
    }

    // Expected contiguous next revision
    if (deltaRev === currentRev + 1n) {
      const nextSettings = cloneSettings(this.state.settings);
      const index = nextSettings.findIndex(s => s.key === delta.key);
      if (index >= 0) {
        nextSettings[index] = { key: delta.key, value: delta.value };
      } else {
        nextSettings.push({ key: delta.key, value: delta.value });
      }

      const reachablePaneKey = resolvePaneKey(
          this.state.currentPaneKey, isMailEnabled(nextSettings));

      this.patch(state => ({
        ...state,
        currentPaneKey: reachablePaneKey,
        settings: nextSettings,
        settingsRevision: deltaRev,
      }));

      if (reachablePaneKey !== this.state.currentPaneKey) {
        writePaneToUrl(reachablePaneKey, true);
      }
      return true;
    }

    // Revision mismatch (gap detected where deltaRev > currentRev + 1n):
    // Fall back to full snapshot reload.
    void this.refreshSettings();
    return false;
  }

  private replaceSettings(settings: SettingValue[], revision?: number | bigint): void {
    const nextRevision = revision !== undefined
        ? (typeof revision === 'bigint' ? revision : BigInt(revision))
        : this.state.settingsRevision;
    if (settingsEqual(this.state.settings, settings) &&
        this.state.settingsRevision === nextRevision) {
      return;
    }
    const reachablePaneKey = resolvePaneKey(
        this.state.currentPaneKey, isMailEnabled(settings));
    this.patch(state => ({
      ...state,
      currentPaneKey: reachablePaneKey,
      settings: cloneSettings(settings),
      settingsRevision: nextRevision,
    }));
    if (reachablePaneKey !== this.state.currentPaneKey) {
      writePaneToUrl(reachablePaneKey, true);
    }
  }
  selectPane(key: string): void {
    const nextPaneKey = resolvePaneKey(key, isMailEnabled(this.state.settings));
    if (nextPaneKey === this.state.currentPaneKey) {
      return;
    }
    this.patch(state => ({
      ...state,
      currentPaneKey: nextPaneKey,
    }));
    writePaneToUrl(nextPaneKey);
  }

  async refreshAccountStatus(): Promise<void> {
    this.patch(state => ({
      ...state,
      accountError: '',
      accountLoading: true,
    }));

    try {
      const {status} = await this.pageHandler.getAccountStatus();
      this.patch(state => ({
        ...state,
        accountLoading: false,
        accountStatus: status,
        billingError: status.signedIn ? state.billingError : '',
        billingInfo: status.signedIn ? state.billingInfo : null,
        billingLoading: status.signedIn ? state.billingLoading : false,
      }));
    } catch (error) {
      this.patch(state => ({
        ...state,
        accountError: formatActionError(error),
        accountLoading: false,
      }));
    }
  }

  async signInWithGoogle(): Promise<{error?: string; success: boolean}> {
    this.patch(state => ({
      ...state,
      accountError: '',
      accountLoading: true,
    }));

    try {
      const {errorMessage, success} = await this.pageHandler.signInWithGoogle();
      if (!success) {
        this.patch(state => ({
          ...state,
          accountError: errorMessage,
          accountLoading: false,
        }));
        return {success: false, error: errorMessage};
      }

      await this.refreshAccountStatus();
      await this.refreshBilling();
      await this.refreshInvoices();
      return {success: true};
    } catch (error) {
      const message = formatActionError(error);
      this.patch(state => ({
        ...state,
        accountError: message,
        accountLoading: false,
      }));
      return {success: false, error: message};
    }
  }

  async signIn(
      email: string,
      password: string): Promise<{error?: string; success: boolean}> {
    this.patch(state => ({
      ...state,
      accountError: '',
      accountLoading: true,
    }));

    try {
      const {errorMessage, success} = await this.pageHandler.login(email, password);
      if (!success) {
        this.patch(state => ({
          ...state,
          accountError: errorMessage,
          accountLoading: false,
        }));
        return {success: false, error: errorMessage};
      }

      await this.refreshAccountStatus();
      await this.refreshBilling();
      await this.refreshInvoices();
      return {success: true};
    } catch (error) {
      const message = formatActionError(error);
      this.patch(state => ({
        ...state,
        accountError: message,
        accountLoading: false,
      }));
      return {success: false, error: message};
    }
  }

  async signOut(): Promise<void> {
    this.accountGeneration += 1;
    this.patch(state => ({
      ...state,
      accountError: '',
      accountLoading: true,
    }));

    try {
      await this.pageHandler.logout();
    } finally {
      this.patch(state => ({
        ...state,
        accountError: '',
        accountLoading: false,
        accountStatus: createSignedOutAccountStatus(),
        billingError: '',
        billingInfo: null,
        billingLoading: false,
        invoices: [],
        invoicesLoading: false,
        invoicesError: '',
      }));
    }
  }

  async refreshBilling(): Promise<void> {
    if (!this.state.accountStatus?.signedIn) {
      this.patch(state => ({
        ...state,
        billingError: '',
        billingInfo: null,
        billingLoading: false,
      }));
      return;
    }

    this.patch(state => ({
      ...state,
      billingError: '',
      billingLoading: true,
    }));

    try {
      const {info, error} = await this.pageHandler.getBillingInfo();
      this.patch(state => ({
        ...state,
        billingInfo: info,
        billingError: error || '',
        billingLoading: false,
      }));
    } catch (error) {
      this.patch(state => ({
        ...state,
        billingError: formatActionError(error),
        billingLoading: false,
      }));
    }
  }

  async refreshInvoices(): Promise<void> {
    const generation = this.accountGeneration;
    if (!this.state.accountStatus?.signedIn) {
      this.patch(state => ({
        ...state,
        invoices: [],
        invoicesLoading: false,
        invoicesError: '',
      }));
      return;
    }
    this.patch(state => ({
      ...state,
      invoicesError: '',
      invoicesLoading: true,
    }));

    try {
      const {invoices} = await this.pageHandler.getInvoices();
      if (generation !== this.accountGeneration) return;
      this.patch(state => ({
        ...state,
        invoices,
        invoicesLoading: false,
      }));
    } catch (error) {
      if (generation !== this.accountGeneration) return;
      this.patch(state => ({
        ...state,
        invoicesError: formatActionError(error),
        invoicesLoading: false,
      }));
    }
  }

  async getBuyCreditsUrl(): Promise<string> {
    // Single pay-what-you-want credits product: the amount is chosen in the
    // LemonSqueezy checkout, so no pack size is selected here. The mojom method
    // still takes an int32 arg (unused); pass 0.
    const {checkoutUrl} = await this.pageHandler.getBuyCreditsUrl(0);
    return checkoutUrl;
  }

  async getSubscriptionCheckoutUrl(tier: 'pro' | 'max'): Promise<string> {
    const {checkoutUrl} =
        await this.pageHandler.getSubscriptionCheckoutUrl(tier);
    return checkoutUrl;
  }

  isSaving(key: string): boolean {
    const route = SELECTED_PROFILE_SETTING_ROUTES[
        key as keyof typeof SELECTED_PROFILE_SETTING_ROUTES];
    if (this.state.selectedProfileTarget && route) {
      const selectedProfileSavingKey =
          route === 'archive' ? 'archive' :
          route === 'search-suggestions' ? 'search-suggestions' :
          route === 'download-prompt' ? 'download-prompt' :
          'preferences';
      return this.state.selectedProfileSavingKeys.has(selectedProfileSavingKey);
    }
    return this.state.savingKeys.has(key);
  }

  /**
   * Commits the given setting value to the backend page handler.
   * If a commit for this setting is already in progress, the value is queued.
   * Only the latest queued value will be committed once the active request finishes.
   * Returns a promise resolving to true if the immediate save succeeded, or false if
   * the immediate save failed or the value was queued (in which case a subsequent queued
   * save will run).
   */
  async commitSettingValue(key: string, value: string): Promise<boolean> {
    if (this.state.selectedProfileTarget) {
      const route = SELECTED_PROFILE_SETTING_ROUTES[
          key as keyof typeof SELECTED_PROFILE_SETTING_ROUTES];
      if (route) {
        return this.commitSelectedProfileSettingValue(key, route, value);
      }
      if (key === 'appearance.sidebar_width' &&
          this.state.selectedProfileContext?.isHostProfile !== true) {
        return false;
      }
    }

    if (this.state.savingKeys.has(key)) {
      this.pendingValues.set(key, value);
      return false;
    }

    this.patch(state => ({
      ...state,
      lastActionError: null,
      savingKeys: new Set([...state.savingKeys, key]),
    }));

    const existingTimer = this.toastTimers.get(key);
    if (existingTimer !== undefined) {
      clearTimeout(existingTimer);
      this.toastTimers.delete(key);
    }

    try {
      const {success} = await this.pageHandler.setSetting(key, value);
      const meta = SETTING_METADATA[key];
      const label = meta ? meta.label : key;

      const timer = setTimeout(() => {
        if (success) {
          toast.success("Saved", { description: label });
        } else {
          toast.error(`Unable to update ${key}. The browser rejected the change.`, { description: label });
        }
        this.toastTimers.delete(key);
      }, 500);
      this.toastTimers.set(key, timer);

      if (!success) {
        this.patch(state => ({
          ...state,
          lastActionError: `Unable to update ${key}. The browser rejected the change.`,
        }));
        console.warn(`Unable to update setting: ${key}`);
      }
      return success;
    } catch (err: any) {
      const meta = SETTING_METADATA[key];
      const label = meta ? meta.label : key;
      const errMsg = err?.message || `Unable to update ${key}. Check your browser connection and try again.`;

      const timer = setTimeout(() => {
        toast.error(errMsg, { description: label });
        this.toastTimers.delete(key);
      }, 500);
      this.toastTimers.set(key, timer);

      this.patch(state => ({
        ...state,
        lastActionError: errMsg,
      }));
      return false;
    } finally {
      this.patch(state => {
        const savingKeys = new Set(state.savingKeys);
        savingKeys.delete(key);
        return {
          ...state,
          savingKeys,
        };
      });
      if (this.pendingValues.has(key)) {
        const nextValue = this.pendingValues.get(key)!;
        this.pendingValues.delete(key);
        void this.commitSettingValue(key, nextValue);
      }
    }
  }

  private async commitSelectedProfileSettingValue(
      settingKey: string,
      route: SelectedProfileSettingRoute,
      value: string): Promise<boolean> {
    switch (route) {
      case 'download-prompt':
        return this.setSelectedProfileDownloadPrompt(value === 'true', settingKey);
      case 'search-suggestions':
        return this.setSelectedProfileSearchSuggestionsEnabled(
            value === 'true', settingKey);
      case 'archive':
        return this.setSelectedProfileArchiveTimeout(Number(value), settingKey);
    }
  }

  private patch(mutator: (state: SettingsState) => SettingsState): void {
    const previous = this.state.accountStatus;
    this.state = mutator(this.state);
    const current = this.state.accountStatus;
    if (previous?.signedIn !== current?.signedIn || previous?.userId !== current?.userId) {
      this.accountGeneration += 1;
      this.state = {...this.state, invoices: [], invoicesError: '', invoicesLoading: false};
    }
    this.scheduleNotify();
  }

  async selectProfileFromCatalog(profileId: string): Promise<void> {
    this.selectedProfileMutationOwners.clear();
    const loadGeneration = this.state.selectedProfileLoadGeneration + 1n;
    const requestedTarget: ProfileTarget = {profileId, targetToken: ''};
    this.patch(state => ({
      ...state,
      selectedProfileTarget: requestedTarget,
      selectedProfileContext: null,
      selectedProfileMetadata: null,
      selectedProfileSearch: null,
      selectedProfileDownload: null,
      selectedProfileArchive: null,
      selectedProfileLoading: true,
      selectedProfileError: null,
      selectedProfileMutationError: null,
      selectedProfileMutationErrorSettingKey: null,
      selectedProfileSavingKeys: new Set<string>(),
      selectedProfileLoadGeneration: loadGeneration,
    }));
    try {
      const {result: {context, error}} =
          await this.pageHandler.getSelectedProfileContext(requestedTarget);
      if (this.state.selectedProfileLoadGeneration !== loadGeneration ||
          this.state.selectedProfileTarget?.profileId !== profileId) return;
      if (error) {
        this.patch(state => ({...state, selectedProfileError: error.message, selectedProfileLoading: false}));
        return;
      }
      if (!context || context.profileId !== profileId || !context.targetToken) {
        this.patch(state => ({
          ...state,
          selectedProfileError: 'Selected profile context is unavailable or invalid.',
          selectedProfileLoading: false,
        }));
        return;
      }
      this.patch(state => ({
        ...state,
        selectedProfileTarget: {profileId: context.profileId, targetToken: context.targetToken},
        selectedProfileContext: context,
        selectedProfileError: null,
        selectedProfileLoading: false,
      }));
    } catch (error) {
      if (this.state.selectedProfileLoadGeneration !== loadGeneration) return;
      this.patch(state => ({
        ...state,
        selectedProfileError: formatActionError(error),
        selectedProfileLoading: false,
      }));
    }
  }

  async selectProfileTarget(target: ProfileTarget): Promise<void> {
    const loadGeneration = this.state.selectedProfileLoadGeneration + 1n;
    this.selectedProfileMutationOwners.clear();
    const targetChanged = !this.selectedProfileTargetMatches(target);

    // Update state immediately so UI shows loading/selection. Profile-scoped
    // snapshots are retained only when refreshing the exact same target.
    this.patch(state => ({
      ...state,
      selectedProfileTarget: target,
      selectedProfileContext: targetChanged ? null : state.selectedProfileContext,
      selectedProfileMetadata: targetChanged ? null : state.selectedProfileMetadata,
      selectedProfileSearch: targetChanged ? null : state.selectedProfileSearch,
      selectedProfileDownload: targetChanged ? null : state.selectedProfileDownload,
      selectedProfileArchive: targetChanged ? null : state.selectedProfileArchive,
      selectedProfileLoading: true,
      selectedProfileError: null,
      selectedProfileMutationError: null,
      selectedProfileMutationErrorSettingKey: null,
      selectedProfileSavingKeys: new Set<string>(),
      selectedProfileLoadGeneration: loadGeneration,
    }));

    try {
      const {result: {context, error}} =
          await this.pageHandler.getSelectedProfileContext(target);

      // Stale guard: if a newer request was started while this was in flight, drop
      if (this.state.selectedProfileLoadGeneration !== loadGeneration) {
        return;
      }
      if (this.state.selectedProfileTarget?.profileId !== target.profileId ||
          this.state.selectedProfileTarget?.targetToken !== target.targetToken) {
        return;
      }

      if (error) {
        this.patch(state => ({
          ...state,
          selectedProfileContext: null,
          selectedProfileError: error.message,
          selectedProfileLoading: false,
        }));
        return;
      }

      if (!context) {
        this.patch(state => ({
          ...state,
          selectedProfileContext: null,
          selectedProfileError: 'Selected profile is unavailable.',
          selectedProfileLoading: false,
        }));
        return;
      }

      if (context.profileId !== target.profileId ||
          context.targetToken !== target.targetToken) {
        this.patch(state => ({
          ...state,
          selectedProfileContext: null,
          selectedProfileError: 'Selected profile context does not match the requested target.',
          selectedProfileLoading: false,
        }));
        return;
      }

      this.patch(state => ({
        ...state,
        selectedProfileContext: context,
        selectedProfileError: null,
        selectedProfileLoading: false,
      }));
    } catch (err) {
      // stale guard again after async error
      if (this.state.selectedProfileLoadGeneration !== loadGeneration) {
        return;
      }
      this.patch(state => ({
        ...state,
        selectedProfileContext: null,
        selectedProfileError: formatActionError(err),
        selectedProfileLoading: false,
      }));
    }
  }

  private selectedProfileTargetMatches(target: ProfileTarget): boolean {
    return this.state.selectedProfileTarget?.profileId === target.profileId &&
        this.state.selectedProfileTarget.targetToken === target.targetToken;
  }

  private selectedProfileRequestMatches(
      target: ProfileTarget,
      context: SelectedProfileContext,
      responseContext?: SelectedProfileContext | null): boolean {
    const currentContext = this.state.selectedProfileContext;
    if (!this.selectedProfileTargetMatches(target) || !currentContext ||
        currentContext.profileId !== context.profileId ||
        currentContext.targetToken !== context.targetToken ||
        currentContext.contextRevision !== context.contextRevision ||
        currentContext.profileRevision !== context.profileRevision) {
      return false;
    }
    return responseContext === undefined || responseContext !== null &&
        responseContext.profileId === context.profileId &&
        responseContext.targetToken === context.targetToken &&
        responseContext.contextRevision === context.contextRevision &&
        responseContext.profileRevision === context.profileRevision;
  }

  private handleSelectedProfileError(code: ProfileTargetErrorCode): void {
    if (code === ProfileTargetErrorCode.kStaleContext ||
        code === ProfileTargetErrorCode.kStaleProfileRevision) {
      // Auto-refresh context; the new context will have fresh revisions.
      if (this.state.selectedProfileTarget) {
        void this.selectProfileTarget(this.state.selectedProfileTarget);
      }
    }
  }

  private selectedProfileMutationRequestMatches(
      target: ProfileTarget, context: SelectedProfileContext): boolean {
    return this.selectedProfileRequestMatches(target, context);
  }

  private applySelectedProfileMutation<T>(
      target: ProfileTarget,
      requestContext: SelectedProfileContext,
      responseContext: SelectedProfileContext | null,
      snapshot: T | null,
      error: ProfileTargetError | null,
      snapshotKey: 'selectedProfileMetadata' |
          'selectedProfileSearch' | 'selectedProfileDownload' |
          'selectedProfileArchive',
      errorSettingKey: string | null): boolean {
    if (!this.selectedProfileMutationRequestMatches(target, requestContext)) {
      return false;
    }
    if (error) {
      this.handleSelectedProfileError(error.code);
      this.patch(state => ({
        ...state,
        selectedProfileMutationError: error,
        selectedProfileMutationErrorSettingKey: errorSettingKey,
      }));
      return false;
    }
    if (!responseContext || !snapshot ||
        responseContext.profileId !== requestContext.profileId ||
        responseContext.targetToken !== requestContext.targetToken ||
        responseContext.contextRevision < requestContext.contextRevision ||
        responseContext.profileRevision < requestContext.profileRevision) {
      return false;
    }
    this.patch(state => ({
      ...state,
      selectedProfileContext: responseContext,
      selectedProfileMutationError: null,
      selectedProfileMutationErrorSettingKey: null,
      [snapshotKey]: snapshot,
    }));
    return true;
  }

  private async runSelectedProfileMutation<T>(
      savingKey: string,
      invoke: (target: ProfileTarget, context: SelectedProfileContext) =>
          Promise<{context: SelectedProfileContext | null; snapshot: T | null; error: ProfileTargetError | null}>,
      snapshotKey: 'selectedProfileMetadata' |
          'selectedProfileSearch' | 'selectedProfileDownload' |
          'selectedProfileArchive',
      errorSettingKey: string | null = null): Promise<boolean> {
    const {selectedProfileTarget: target, selectedProfileContext: context} = this.state;
    if (!target || !context || this.state.selectedProfileSavingKeys.has(savingKey)) {
      return false;
    }
    const owner = Symbol(savingKey);
    this.selectedProfileMutationOwners.set(savingKey, owner);
    this.patch(state => ({
      ...state,
      selectedProfileMutationError: null,
      selectedProfileMutationErrorSettingKey: null,
      selectedProfileSavingKeys: new Set([...state.selectedProfileSavingKeys, savingKey]),
    }));
    let requestMayClearSaving = false;
    try {
      const result = await invoke(target, context);
      const applied = this.applySelectedProfileMutation(
          target, context, result.context, result.snapshot, result.error,
          snapshotKey, errorSettingKey);
      requestMayClearSaving = applied ||
          this.selectedProfileMutationRequestMatches(target, context);
      return applied;
    } catch (error) {
      requestMayClearSaving =
          this.selectedProfileMutationRequestMatches(target, context);
      if (requestMayClearSaving) {
        this.patch(state => ({
          ...state,
          selectedProfileMutationError: {
            code: ProfileTargetErrorCode.kInternal,
            message: formatActionError(error),
            currentContextRevision: context.contextRevision,
            currentProfileRevision: context.profileRevision,
          },
          selectedProfileMutationErrorSettingKey: errorSettingKey,
        }));
      }
      return false;
    } finally {
      const stillOwnsSavingKey =
          this.selectedProfileMutationOwners.get(savingKey) === owner;
      if (stillOwnsSavingKey && requestMayClearSaving) {
        this.selectedProfileMutationOwners.delete(savingKey);
        this.patch(state => {
          const saving = new Set(state.selectedProfileSavingKeys);
          saving.delete(savingKey);
          return {...state, selectedProfileSavingKeys: saving};
        });
      }
    }
  }

  async updateSelectedProfileMetadata(
      update: Omit<ProfileMetadataUpdate, 'expectedProfileRevision'>): Promise<boolean> {
    return this.runSelectedProfileMutation(
        'metadata',
        async (target, context) => {
          const {result} = await this.pageHandler.updateSelectedProfileMetadata(
              target, context.contextRevision,
              {...update, expectedProfileRevision: context.profileRevision});
          return {context: result.context, snapshot: result.metadata, error: result.error};
        },
        'selectedProfileMetadata');
  }

  async setSelectedProfileDefaultSearchEngine(keyword: string): Promise<boolean> {
    return this.runSelectedProfileMutation(
        'search-engine',
        async (target, context) => {
          const {result} = await this.pageHandler.setSelectedProfileDefaultSearchEngine(
              target, context.contextRevision, keyword, context.profileRevision);
          return {context: result.context, snapshot: result.search, error: result.error};
        },
        'selectedProfileSearch');
  }

  async setSelectedProfileSearchSuggestionsEnabled(
      enabled: boolean, errorSettingKey: string | null = null): Promise<boolean> {
    return this.runSelectedProfileMutation(
        'search-suggestions',
        async (target, context) => {
          const {result} = await this.pageHandler.setSelectedProfileSearchSuggestionsEnabled(
              target, context.contextRevision, enabled, context.profileRevision);
          return {context: result.context, snapshot: result.search, error: result.error};
        },
        'selectedProfileSearch',
        errorSettingKey);
  }

  async setSelectedProfileDownloadPrompt(
      promptForDownload: boolean, errorSettingKey: string | null = null): Promise<boolean> {
    return this.runSelectedProfileMutation(
        'download-prompt',
        async (target, context) => {
          const {result} = await this.pageHandler.setSelectedProfileDownloadPrompt(
              target, context.contextRevision, promptForDownload,
              context.profileRevision);
          return {context: result.context, snapshot: result.download, error: result.error};
        },
        'selectedProfileDownload',
        errorSettingKey);
  }

  async selectSelectedProfileDownloadDirectory(): Promise<boolean> {
    return this.runSelectedProfileMutation(
        'download-directory',
        async (target, context) => {
          const {result} = await this.pageHandler.selectSelectedProfileDownloadDirectory(
              target, context.contextRevision, context.profileRevision);
          return {context: result.context, snapshot: result.download, error: result.error};
        },
        'selectedProfileDownload');
  }

  async setSelectedProfileArchiveTimeout(
      timeoutHours: number, errorSettingKey: string | null = null): Promise<boolean> {
    return this.runSelectedProfileMutation(
        'archive',
        async (target, context) => {
          const {result} = await this.pageHandler.setSelectedProfileArchiveTimeout(
              target, context.contextRevision, timeoutHours, context.profileRevision);
          return {context: result.context, snapshot: result.archive, error: result.error};
        },
        'selectedProfileArchive',
        errorSettingKey);
  }

  async refreshSelectedProfileMetadata(): Promise<void> {
    const {selectedProfileTarget, selectedProfileContext} = this.state;
    if (!selectedProfileTarget || !selectedProfileContext) return;
    try {
      const {result: {context, metadata, error}} =
          await this.pageHandler.getSelectedProfileMetadata(selectedProfileTarget);
      if (!this.selectedProfileRequestMatches(
              selectedProfileTarget, selectedProfileContext,
              error ? undefined : context)) return;
      if (error) {
        this.handleSelectedProfileError(error.code);
      } else if (metadata) {
        this.patch(state => ({...state, selectedProfileMetadata: metadata}));
      }
    } catch (_) { /* silently ignore refresh failures */ }
  }

  async refreshSelectedProfileSearch(): Promise<void> {
    const {selectedProfileTarget, selectedProfileContext} = this.state;
    if (!selectedProfileTarget || !selectedProfileContext) return;
    try {
      const {result: {context, search, error}} =
          await this.pageHandler.getSelectedProfileSearchSettings(selectedProfileTarget);
      if (!this.selectedProfileRequestMatches(
              selectedProfileTarget, selectedProfileContext,
              error ? undefined : context)) return;
      if (error) {
        this.handleSelectedProfileError(error.code);
      } else if (search) {
        this.patch(state => ({...state, selectedProfileSearch: search}));
      }
    } catch (_) { /* ignore */ }
  }

  async refreshSelectedProfileDownload(): Promise<void> {
    const {selectedProfileTarget, selectedProfileContext} = this.state;
    if (!selectedProfileTarget || !selectedProfileContext) return;
    try {
      const {result: {context, download, error}} =
          await this.pageHandler.getSelectedProfileDownloadSettings(selectedProfileTarget);
      if (!this.selectedProfileRequestMatches(
              selectedProfileTarget, selectedProfileContext,
              error ? undefined : context)) return;
      if (error) {
        this.handleSelectedProfileError(error.code);
      } else if (download) {
        this.patch(state => ({...state, selectedProfileDownload: download}));
      }
    } catch (_) { /* ignore */ }
  }

  async refreshSelectedProfileArchive(): Promise<void> {
    const {selectedProfileTarget, selectedProfileContext} = this.state;
    if (!selectedProfileTarget || !selectedProfileContext) return;
    try {
      const {result: {context, archive, error}} =
          await this.pageHandler.getSelectedProfileArchiveSettings(selectedProfileTarget);
      if (!this.selectedProfileRequestMatches(
              selectedProfileTarget, selectedProfileContext,
              error ? undefined : context)) return;
      if (error) {
        this.handleSelectedProfileError(error.code);
      } else if (archive) {
        this.patch(state => ({...state, selectedProfileArchive: archive}));
      }
    } catch (_) { /* ignore */ }
  }

  private scheduleNotify(): void {
    if (this.notifyScheduled) {
      return;
    }
    this.notifyScheduled = true;
    const flush = () => {
      this.notifyScheduled = false;
      this.rafHandle = null;
      notifyListeners(this.listeners);
    };
    if (typeof requestAnimationFrame === 'function') {
      this.rafHandle = requestAnimationFrame(flush);
    } else {
      setTimeout(flush, 0);
    }
  }
}
