import {
  PageCallbackRouter,
  PageHandlerFactory,
  PageHandlerRemote,
} from '../mojo.js';
import type {SettingValue} from '../mojo.js';
import {PANE_DEFINITION_MAP, PANE_DEFINITIONS} from '../schema/panes.js';
import {notifyListeners} from '../../maho_common/react/store_utils.js';
import type {Listener} from '../../maho_common/react/store_utils.js';
import {bindMojoPageHandler} from '../../maho_common/react/use_mojo.js';

export interface SettingsState {
  readonly bootstrapError: string | null;
  readonly currentPaneKey: string;
  readonly lastActionError: string | null;
  readonly savingKeys: ReadonlySet<string>;
  readonly settings: SettingValue[];
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
    bootstrapError: null,
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

    try {
      const {success} = await this.pageHandler.setSetting(key, value);
      if (!success) {
        this.patch(state => ({
          ...state,
          lastActionError: `Unable to update ${key}. The browser rejected the change.`,
        }));
        console.warn(`Unable to update setting: ${key}`);
      }
      return success;
    } catch {
      this.patch(state => ({
        ...state,
        lastActionError: `Unable to update ${key}. Check your browser connection and try again.`,
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
