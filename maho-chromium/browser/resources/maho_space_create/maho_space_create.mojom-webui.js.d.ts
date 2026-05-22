export type ThemeMode = 'sparkle' | 'sun' | 'moon';

export interface ThemeSelection {
  mode: ThemeMode;
  preset_index: number;
  brightness: number;
}

export interface ThemeSelectionWire {
  theme_json: string;
}

export interface Page {
  initialize(initial: ThemeSelectionWire): void;
}

export interface PageHandler {
  previewTheme(selection: ThemeSelectionWire): Promise<void>;
  commitTheme(selection: ThemeSelectionWire): Promise<void>;
  cancelTheme(): Promise<void>;
}

export class PageCallbackRouter {
  $: {
    bindNewPipeAndPassRemote(): unknown;
  };
  initialize: {
    addListener(listener: (initial: ThemeSelectionWire) => void): void;
  };
}

export class PageHandlerRemote implements PageHandler {
  $: {
    bindNewPipeAndPassReceiver(): unknown;
  };
  previewTheme(selection: ThemeSelectionWire): Promise<void>;
  commitTheme(selection: ThemeSelectionWire): Promise<void>;
  cancelTheme(): Promise<void>;
}

export class PageHandlerFactory {
  static getRemote(): PageHandlerFactory;
  createPageHandler(page: unknown, handler: unknown): void;
}
