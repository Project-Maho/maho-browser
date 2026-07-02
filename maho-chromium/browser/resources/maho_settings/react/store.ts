import {
  PageCallbackRouter,
  PageHandlerFactory,
  PageHandlerRemote,
} from '../mojo.js';
import type {
  AccountStatus,
  BillingInfo,
  SettingValue,
  Invoice,
} from '../maho_settings.mojom-webui.js';
import {PANE_DEFINITION_MAP, PANE_DEFINITIONS} from '../schema/panes.js';
import {SETTING_METADATA} from '../schema/setting_schema.js';
import {notifyListeners} from '../../maho_common/react/store_utils.js';
import type {Listener} from '../../maho_common/react/store_utils.js';
import {bindMojoPageHandler} from '../../maho_common/react/use_mojo.js';
import {toast} from 'sonner';

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

function resolvePaneKey(key: string): string {
  const pane = PANE_DEFINITION_MAP[key];
  if (pane && pane.kind !== 'hidden') {
    return key;
  }

  return PANE_DEFINITIONS[0]!.key;
}

function paneKeyFromCurrentUrl(): string {
  const param = new URLSearchParams(window.location.search).get('pane');
  return param ? resolvePaneKey(param) : PANE_DEFINITIONS[0]!.key;
}

// Writes |paneKey| into the current URL as ?pane=<key>, replacing any
// existing query so that GetVisibleURL always reflects the active pane.
// replaceState is used (not pushState) to avoid polluting back-button history.
function writePaneToUrl(paneKey: string): void {
  const url = new URL(window.location.href);
  url.searchParams.set('pane', paneKey);
  window.history.replaceState(null, '', url.href);
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
  };
}

export class MahoSettingsStore {
  private readonly callbackRouter: PageCallbackRouter;
  private readonly listeners = new Set<Listener>();
  private readonly pageHandler: PageHandlerRemote;
  private state: SettingsState = createInitialState();
  private bootstrapComplete = false;
  private toastTimers = new Map<string, any>();

  constructor() {
    const {router, handler} = bindMojoPageHandler(
        PageCallbackRouter, PageHandlerRemote, PageHandlerFactory);
    this.callbackRouter = router;
    this.pageHandler = handler;

    this.callbackRouter.settingsChanged.addListener((settings: SettingValue[]) => {
      if (!this.bootstrapComplete) {
        return;
      }
      this.replaceSettings(settings);
    });

    this.callbackRouter.accountStatusChanged.addListener((status: AccountStatus) => {
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

      if (status.signedIn) {
        void this.refreshBilling();
        void this.refreshInvoices();
      }
    });

    writePaneToUrl(this.state.currentPaneKey);

    window.addEventListener('popstate', () => {
      const paneKey = paneKeyFromCurrentUrl();
      if (paneKey !== this.state.currentPaneKey) {
        this.patch(state => ({...state, currentPaneKey: paneKey}));
      }
    });
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

  async bootstrap(): Promise<void> {
    try {
      const {settings} = await this.pageHandler.getSettings();
      this.bootstrapComplete = true;
      this.replaceSettings(settings);
    } catch {
      this.bootstrapComplete = true;
      this.patch(state => ({
        ...state,
        bootstrapError: 'Unable to load settings right now. Try reopening the page.',
      }));
    }
  }

  private replaceSettings(settings: SettingValue[]): void {
    if (settingsEqual(this.state.settings, settings)) {
      return;
    }
    this.patch(state => ({
      ...state,
      settings: cloneSettings(settings),
    }));
  }
  selectPane(key: string): void {
    const nextPaneKey = resolvePaneKey(key);
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
      const {info} = await this.pageHandler.getBillingInfo();
      this.patch(state => ({
        ...state,
        billingInfo: info,
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
      this.patch(state => ({
        ...state,
        invoices,
        invoicesLoading: false,
      }));
    } catch (error) {
      this.patch(state => ({
        ...state,
        invoicesError: formatActionError(error),
        invoicesLoading: false,
      }));
    }
  }

  isSaving(key: string): boolean {
    return this.state.savingKeys.has(key);
  }

  async commitSettingValue(key: string, value: string): Promise<boolean> {
    if (this.state.savingKeys.has(key)) {
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
    }
  }

  private patch(mutator: (state: SettingsState) => SettingsState): void {
    this.state = mutator(this.state);
    notifyListeners(this.listeners);
  }
}
