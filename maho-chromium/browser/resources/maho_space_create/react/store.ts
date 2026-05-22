import {
  PageCallbackRouter,
  PageHandlerFactory,
  PageHandlerRemote,
} from '../maho_space_create.mojom-webui.js';
import type {Page, PageHandler} from '../maho_space_create.mojom-webui.js';
import {
  clampBrightness,
  cloneState,
  createInitialState,
  normalizePresetIndex,
  parseThemeSelection,
  serializeThemeSelection,
} from './types.js';
import type {ThemeMode, ThemePickerState, ThemeSelection, ThemeSelectionWire} from './types.js';

type Listener = () => void;

const PREVIEW_DEBOUNCE_MS = 50;

function notifyListeners(listeners: Set<Listener>): void {
  for (const listener of listeners) {
    listener();
  }
}

export class ThemePickerStore {
  private readonly callbackRouter = new PageCallbackRouter();
  private readonly listeners = new Set<Listener>();
  private readonly handler = new PageHandlerRemote();
  private state: ThemePickerState = createInitialState();
  private previewTimer: number | null = null;

  constructor() {
    const factory = PageHandlerFactory.getRemote();
    factory.createPageHandler(
        this.callbackRouter.$.bindNewPipeAndPassRemote(),
        this.handler.$.bindNewPipeAndPassReceiver());

    const page = this.callbackRouter as unknown as Page;
    const remoteHandler = this.handler as unknown as PageHandler;
    void page;
    void remoteHandler;

    this.callbackRouter.initialize.addListener((initial: ThemeSelectionWire) => {
      this.loadInitial(initial.theme_json);
    });
  }

  getSnapshot(): ThemePickerState {
    return cloneState(this.state);
  }

  subscribe(listener: Listener): () => void {
    this.listeners.add(listener);
    return () => {
      this.listeners.delete(listener);
    };
  }

  setMode(mode: ThemeMode): void {
    this.patch(draft => {
      draft.selection.mode = mode;
    });
  }

  setPreset(index: number): void {
    this.patch(draft => {
      draft.selection.preset_index = normalizePresetIndex(index);
    });
  }

  setBrightness(brightness: number): void {
    this.patch(draft => {
      draft.selection.brightness = clampBrightness(brightness);
    });
  }

  loadInitial(json: string): void {
    const initialSelection = parseThemeSelection(json);
    this.patch(draft => {
      draft.selection = initialSelection;
      draft.loading = false;
    }, {emitPreview: false});
  }

  previewNextPreset(): void {
    this.setPreset(this.state.selection.preset_index + 1);
  }

  async commitTheme(): Promise<void> {
    await this.handler.commitTheme({theme_json: this.serializeSelection()});
  }

  async cancelTheme(): Promise<void> {
    this.clearPreviewTimer();
    await this.handler.cancelTheme();
  }

  private serializeSelection(): string {
    return serializeThemeSelection(this.state.selection);
  }

  private patch(
      mutator: (draft: ThemePickerState) => void,
      options: {emitPreview?: boolean} = {}): void {
    const draft = cloneState(this.state);
    mutator(draft);
    this.state = draft;
    notifyListeners(this.listeners);
    if (options.emitPreview !== false) {
      this.schedulePreview();
    }
  }

  private schedulePreview(): void {
    this.clearPreviewTimer();
    const payload = this.serializeSelection();
    this.previewTimer = window.setTimeout(() => {
      void this.handler.previewTheme({theme_json: payload});
      this.previewTimer = null;
    }, PREVIEW_DEBOUNCE_MS);
  }

  private clearPreviewTimer(): void {
    if (this.previewTimer !== null) {
      window.clearTimeout(this.previewTimer);
      this.previewTimer = null;
    }
  }
}

export function getDialLevel(selection: ThemeSelection): number {
  return Math.max(0, Math.min(8, Math.round(selection.brightness / 12.5)));
}
