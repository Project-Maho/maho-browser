export enum CaseMode {
  kNone = 0,
  kUpper = 1,
  kLower = 2,
  kCapitalize = 3,
}

export enum SizeMode {
  k90 = 0,
  k100 = 1,
  k110 = 2,
  k125 = 3,
  k150 = 4,
}

export enum WindowMode {
  kBoost = 0,
  kCode = 1,
}

export interface Point {
  x: number;
  y: number;
}

export interface ColorBoost {
  dotPos: Point;
  dotDistance: number;
  dotAngleDeg: number;
  secondaryDotPos: Point;
  secondaryDotAngleDegDelta: number;
  magicTheme: boolean;
  colorBoostEnabled: boolean;
  smartInvert: boolean;
  contrast: number;
  brightness: number;
  saturation: number;
}

export interface TypographyBoost {
  fontFamily: string;
  caseMode: CaseMode;
  sizeMode: SizeMode;
}

export interface BoostInfo {
  id: string;
  name: string;
  domain: string;
  color: ColorBoost;
  typography: TypographyBoost;
  zapSelectors: string[];
  customCss: string;
  changeWasMade: boolean;
}

export interface ColorBoostUpdate {
  colorBoostEnabled?: boolean | null;
  dotAngleDeg?: number | null;
  secondaryDotAngleDegDelta?: number | null;
  brightness?: number | null;
  saturation?: number | null;
  contrast?: number | null;
  magicTheme?: boolean | null;
  smartInvert?: boolean | null;
  dotPos?: Point | null;
  dotDistance?: number | null;
  secondaryDotPos?: Point | null;
}

export interface TypographyBoostUpdate {
  setFontFamily: boolean;
  fontFamily: string;
  caseMode?: CaseMode | null;
  setSizeMode: boolean;
  sizeMode: SizeMode;
}

export interface BoostUpdate {
  name?: string | null;
  color?: ColorBoostUpdate | null;
  typography?: TypographyBoostUpdate | null;
  zapSelectors?: string[] | null;
  customCss?: string | null;
}

export interface PageObserver {
  onBoostsChanged(): void;
  onActiveChanged(boostId: string | null): void;
  onZapStateUpdate(isOn: boolean, anyZapped: boolean): void;
  onZapListUpdate(): void;
  onPickerStateUpdate(isOn: boolean): void;
  onPickerSelectorPicked(selector: string): void;
  onEditorKilled(): void;
}

export interface PageHandler {
  getDomain(): Promise<{domain: string}>;
  hostCloseFinished(): Promise<void>;
  setHostCloseState(boostId: string | null, dirty: boolean): Promise<void>;
  listBoosts(): Promise<{boosts: BoostInfo[]}>;
  getBoost(boostId: string): Promise<{boost: BoostInfo | null}>;
  getActiveBoost(): Promise<{boost: BoostInfo | null}>;
  createTempBoost(): Promise<{boost: BoostInfo | null}>;
  commitBoost(boostId: string): Promise<void>;
  discardBoost(boostId: string): Promise<void>;
  updateBoost(boostId: string, changes: BoostUpdate): Promise<{boost: BoostInfo | null}>;
  deleteBoost(boostId: string): Promise<{success: boolean}>;
  setActiveBoost(boostId: string | null): Promise<void>;
  composeCss(boostId: string): Promise<{css: string}>;
  shuffleBoost(boostId: string): Promise<{boost: BoostInfo | null}>;
  resetBoost(boostId: string): Promise<{boost: BoostInfo | null}>;
  exportBoost(boostId: string): Promise<{json: string}>;
  importBoost(json: string): Promise<{boost: BoostInfo | null}>;
  closeDialog(): Promise<void>;
  requestModeResize(mode: WindowMode): Promise<void>;
  enterZapMode(boostId: string): Promise<void>;
  exitZapMode(): Promise<void>;
  enterPickerMode(boostId: string): Promise<void>;
  exitPickerMode(): Promise<void>;
  appendZapSelector(boostId: string, selector: string): Promise<void>;
  removeZapSelector(boostId: string, selector: string): Promise<void>;
  openInspector(): Promise<void>;
  getSystemFonts(): Promise<{fonts: string[]}>;
}

export class PageCallbackRouter implements PageObserver {
  $: {
    bindNewPipeAndPassRemote(): unknown;
  };
  onBoostsChanged: {
    addListener(listener: () => void): void;
  };
  onActiveChanged: {
    addListener(listener: (boostId: string | null) => void): void;
  };
  onZapStateUpdate: {
    addListener(listener: (isOn: boolean, anyZapped: boolean) => void): void;
  };
  onZapListUpdate: {
    addListener(listener: () => void): void;
  };
  onPickerStateUpdate: {
    addListener(listener: (isOn: boolean) => void): void;
  };
  onPickerSelectorPicked: {
    addListener(listener: (selector: string) => void): void;
  };
  onEditorKilled: {
    addListener(listener: () => void): void;
  };
}

export class PageHandlerRemote implements PageHandler {
  $: {
    bindNewPipeAndPassReceiver(): unknown;
  };
  hostCloseFinished(): Promise<void>;
  setHostCloseState(boostId: string | null, dirty: boolean): Promise<void>;
  getDomain(): Promise<{domain: string}>;
  listBoosts(): Promise<{boosts: BoostInfo[]}>;
  getBoost(boostId: string): Promise<{boost: BoostInfo | null}>;
  getActiveBoost(): Promise<{boost: BoostInfo | null}>;
  createTempBoost(): Promise<{boost: BoostInfo | null}>;
  commitBoost(boostId: string): Promise<void>;
  discardBoost(boostId: string): Promise<void>;
  updateBoost(boostId: string, changes: BoostUpdate): Promise<{boost: BoostInfo | null}>;
  deleteBoost(boostId: string): Promise<{success: boolean}>;
  setActiveBoost(boostId: string | null): Promise<void>;
  composeCss(boostId: string): Promise<{css: string}>;
  shuffleBoost(boostId: string): Promise<{boost: BoostInfo | null}>;
  resetBoost(boostId: string): Promise<{boost: BoostInfo | null}>;
  exportBoost(boostId: string): Promise<{json: string}>;
  importBoost(json: string): Promise<{boost: BoostInfo | null}>;
  closeDialog(): Promise<void>;
  requestModeResize(mode: WindowMode): Promise<void>;
  enterZapMode(boostId: string): Promise<void>;
  exitZapMode(): Promise<void>;
  enterPickerMode(boostId: string): Promise<void>;
  exitPickerMode(): Promise<void>;
  appendZapSelector(boostId: string, selector: string): Promise<void>;
  removeZapSelector(boostId: string, selector: string): Promise<void>;
  openInspector(): Promise<void>;
  getSystemFonts(): Promise<{fonts: string[]}>;
}

export class PageHandlerFactory {
  static getRemote(): PageHandlerFactory;
  createPageHandler(page: unknown, handler: unknown): void;
}
