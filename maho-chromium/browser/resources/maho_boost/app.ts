// Copyright 2026 Maho Browser. All rights reserved.

import {
  CaseMode,
  PageObserverCallbackRouter,
  PageHandlerFactory,
  PageHandlerRemote,
  SizeMode,
  WindowMode,
} from './maho_boost.mojom-webui.js';
import type {
  BoostInfo,
  BoostUpdate,
  ColorBoostUpdate,
  Point,
  TypographyBoostUpdate,
} from './maho_boost.mojom-webui.js';

declare global {
  interface MahoCodeMirrorChangeSpec {
    from: number;
    to?: number;
    insert: string;
  }
  interface MahoCodeMirrorEditorView {
    state: {
      doc: { length: number; toString(): string };
      selection: { main: { head: number } };
    };
    dispatch(spec: {
      changes: MahoCodeMirrorChangeSpec;
      selection?: {anchor: number};
    }): void;
    requestMeasure(): void;
    focus(): void;
  }
  type MahoCodeMirrorViewUpdate = {
    docChanged: boolean;
    state: { doc: { toString(): string } };
  };
  type MahoCodeMirrorExtension = unknown;
  interface MahoCodeMirrorEditorViewConstructor {
    new(config: {
      doc?: string;
      extensions?: MahoCodeMirrorExtension[];
      parent: Element;
    }): MahoCodeMirrorEditorView;
    updateListener: {
      of(listener: (update: MahoCodeMirrorViewUpdate) => void): MahoCodeMirrorExtension;
    };
  }
  interface Window {
    MahoCodeMirror: {
      EditorView: MahoCodeMirrorEditorViewConstructor;
      basicSetup: MahoCodeMirrorExtension;
      css(): MahoCodeMirrorExtension;
      oneDark: MahoCodeMirrorExtension;
    };
  }
}

// MARK: Localization table (zen-boosts.ftl:5-59)

const LOCALE_EN_US: Record<string, string> = {
  'zen-boost-edit-rename': 'Rename Boost',
  'zen-boost-edit-shuffle': 'Shuffle Vibes',
  'zen-boost-edit-reset': 'Reset All Edits',
  'zen-boost-edit-delete': 'Delete Boost',
  'zen-boost-size': 'Size',
  'zen-boost-case': 'Case',
  'zen-boost-zap': 'Zap',
  'zen-boost-code': 'Code',
  'zen-boost-back': 'Back',
  'zen-boost-shuffle.title': 'Shuffle Boost Settings',
  'zen-boost-invert.title': 'Smart Invert Colors',
  'zen-boost-controls.title': 'Advanced Color Controls',
  'zen-boost-disable.title': 'Disable Color Adjustments',
  'zen-boost-text-case-toggle.title': 'Toggle Text Case',
  'zen-boost-css-picker.title': 'Pick Selector',
  'zen-boost-css-inspector.title': 'Open Inspector',
  'zen-bootst-color-contrast': 'Contrast',
  'zen-bootst-color-brightness': 'Brightness',
  'zen-bootst-color-original-saturation': 'Original Saturation',
  'zen-add-zap-helper': 'Click elements on the page to Zap them',
  'zen-remove-zap-helper': '← Click to Unzap',
  'zen-select-this': 'Insert selector for this',
  'zen-select-related': 'Insert selector for related',
  'zen-select-cancel': 'Cancel',
  'zen-zap-this': 'Zap this',
  'zen-zap-related': 'Zap all related elements',
  'zen-zap-cancel': 'Cancel',
  'zen-zap-done': 'Done',
  'zen-unzap-tooltip.0': 'No elements zapped',
  'zen-unzap-tooltip.1': '{elementCount} element zapped',
  'zen-unzap-tooltip.other': '{elementCount} elements zapped',
  'zen-boost-save': 'Export Boost',
  'zen-boost-load': 'Import Boost',
  'zen-panel-ui-boosts-exported-message': 'Boost exported!',
  'zen-site-data-boosts': 'Boosts',
  'zen-site-data-create-boost.title': 'Create new boost',
  'zen-boost-rename-boost-prompt': 'Rename Boost?',
};

// MARK: Constants and mappings (ZenBoostsEditor.mjs:267-286, 616-649, 1066-1133)

const COMMON_FONTS = [
  'Arial',
  'Times New Roman',
  'Courier New',
  'Georgia',
  'Comic Sans MS',
  'Verdana',
  'Trebuchet MS',
  'Impact',
  'Palatino Linotype',
  'Tahoma',
  'Helvetica',
  'Garamond',
  'Century Gothic',
  'Arial Black',
  'Papyrus',
] as const;

const CASE_SEQUENCE = [
  CaseMode.kNone,
  CaseMode.kUpper,
  CaseMode.kLower,
  CaseMode.kCapitalize,
] as const;

const SIZE_SEQUENCE = [
  SizeMode.k100,
  SizeMode.k110,
  SizeMode.k125,
  SizeMode.k150,
  SizeMode.k90,
] as const;

const SIZE_MODE_TO_FACTOR = new Map<SizeMode, number>([
  [SizeMode.k90, 0.9],
  [SizeMode.k100, 1.0],
  [SizeMode.k110, 1.1],
  [SizeMode.k125, 1.25],
  [SizeMode.k150, 1.5],
]);

const CASE_MODE_TO_ATTR = new Map<CaseMode, string>([
  [CaseMode.kNone, 'none'],
  [CaseMode.kUpper, 'uppercase'],
  [CaseMode.kLower, 'lowercase'],
  [CaseMode.kCapitalize, 'capitalize'],
]);

const CASE_MODE_TO_BUTTON_MODE = new Map<CaseMode, string>([
  [CaseMode.kNone, 'none'],
  [CaseMode.kUpper, 'orange'],
  [CaseMode.kLower, 'orange-red'],
  [CaseMode.kCapitalize, 'red'],
]);

const SIZE_MODE_TO_BUTTON_MODE = new Map<SizeMode, string>([
  [SizeMode.k100, 'none'],
  [SizeMode.k110, 'orange'],
  [SizeMode.k125, 'orange-red'],
  [SizeMode.k150, 'red'],
  [SizeMode.k90, 'blue'],
]);

const MAGIC_THEME_FALLBACK_ACCENT = '#7e57c2';
const DEFAULT_BOOST_NAME = 'My Boost';
const DEFAULT_FONT_OPTION_LABEL = 'Default';
const COLOR_PICKER_PADDING = 50;
const GENERAL_UPDATE_DEBOUNCE_MS = 150;
const SLIDER_UPDATE_DEBOUNCE_MS = 100;
const FILE_ACCEPT = '.json';

type DebounceKey =
  | 'color-picker-primary'
  | 'color-picker-secondary'
  | 'magic-theme'
  | 'invert'
  | 'disable'
  | 'contrast'
  | 'brightness'
  | 'saturation'
  | 'case'
  | 'size'
  | 'font'
  | 'name'
  | 'custom-css'
  | 'import-selector';

interface EditorDom {
  root: HTMLElement;
  editorRoot: HTMLDivElement;
  codeEditorRoot: HTMLDivElement;
  closeButton: HTMLButtonElement;
  shuffleButton: HTMLButtonElement;
  nameContainer: HTMLDivElement;
  nameText: HTMLParagraphElement;
  gradient: HTMLDivElement;
  magicThemeButton: HTMLButtonElement;
  primaryDot: HTMLDivElement;
  secondaryDot: HTMLDivElement;
  colorCircle: HTMLDivElement;
  invertButton: HTMLButtonElement;
  controlsButton: HTMLButtonElement;
  disableButton: HTMLButtonElement;
  fontGrid: HTMLDivElement;
  fontSelect: HTMLSelectElement;
  sizeButton: HTMLButtonElement;
  sizeText: HTMLParagraphElement;
  sizeValue: HTMLParagraphElement;
  caseButton: HTMLButtonElement;
  caseText: HTMLParagraphElement;
  zapButton: HTMLButtonElement;
  zapValue: HTMLParagraphElement;
  codeButton: HTMLButtonElement;
  backButton: HTMLButtonElement;
  codeEditor: HTMLDivElement;
  cssPickerButton: HTMLButtonElement;
  cssInspectorButton: HTMLButtonElement;
  advancedColorPanel: HTMLDivElement;
  contrastSlider: HTMLInputElement;
  brightnessSlider: HTMLInputElement;
  saturationSlider: HTMLInputElement;
  contextMenu: HTMLDivElement;
  renameMenuItem: HTMLButtonElement;
  shuffleMenuItem: HTMLButtonElement;
  resetMenuItem: HTMLButtonElement;
  loadMenuItem: HTMLButtonElement;
  saveMenuItem: HTMLButtonElement;
  deleteMenuItem: HTMLButtonElement;
  importFileInput: HTMLInputElement;
}

interface DotGeometry {
  pixelX: number;
  pixelY: number;
  angleRad: number;
  dotAngleDeg: number;
  dotDistance: number;
  dotPos: Point;
}

// MARK: DOM utilities (supporting all sections)

function requireElement(id: string): HTMLElement {
  const element = document.getElementById(id);
  if (!element) {
    throw new Error(`Missing required element #${id}`);
  }
  return element;
}

function requireButton(id: string): HTMLButtonElement {
  const element = requireElement(id);
  if (!(element instanceof HTMLButtonElement)) {
    throw new Error(`#${id} is not a button`);
  }
  return element;
}

function requireDiv(id: string): HTMLDivElement {
  const element = requireElement(id);
  if (!(element instanceof HTMLDivElement)) {
    throw new Error(`#${id} is not a div`);
  }
  return element;
}

function requireParagraph(id: string): HTMLParagraphElement {
  const element = requireElement(id);
  if (!(element instanceof HTMLParagraphElement)) {
    throw new Error(`#${id} is not a paragraph`);
  }
  return element;
}

function requireInput(id: string): HTMLInputElement {
  const element = requireElement(id);
  if (!(element instanceof HTMLInputElement)) {
    throw new Error(`#${id} is not an input`);
  }
  return element;
}

function requireSelect(id: string): HTMLSelectElement {
  const element = requireElement(id);
  if (!(element instanceof HTMLSelectElement)) {
    throw new Error(`#${id} is not a select`);
  }
  return element;
}

function requireClassDiv(selector: string): HTMLDivElement {
  const element = document.querySelector(selector);
  if (!(element instanceof HTMLDivElement)) {
    throw new Error(`Missing required div ${selector}`);
  }
  return element;
}

function clamp(value: number, min: number, max: number): number {
  return Math.min(Math.max(value, min), max);
}

function normalizeDegrees(value: number): number {
  return ((value % 360) + 360) % 360;
}

function sizeFactorFromMode(mode: SizeMode): number {
  return SIZE_MODE_TO_FACTOR.get(mode) ?? 1.0;
}

function caseAttrFromMode(mode: CaseMode): string {
  return CASE_MODE_TO_ATTR.get(mode) ?? 'none';
}

function buttonModeForCase(mode: CaseMode): string {
  return CASE_MODE_TO_BUTTON_MODE.get(mode) ?? 'none';
}

function buttonModeForSize(mode: SizeMode): string {
  return SIZE_MODE_TO_BUTTON_MODE.get(mode) ?? 'none';
}

function nextCaseMode(mode: CaseMode): CaseMode {
  const currentIndex = CASE_SEQUENCE.indexOf(mode);
  const nextIndex = (currentIndex + 1) % CASE_SEQUENCE.length;
  return CASE_SEQUENCE[nextIndex] ?? CaseMode.kNone;
}

function nextSizeMode(mode: SizeMode): SizeMode {
  const currentIndex = SIZE_SEQUENCE.indexOf(mode);
  const nextIndex = (currentIndex + 1) % SIZE_SEQUENCE.length;
  return SIZE_SEQUENCE[nextIndex] ?? SizeMode.k100;
}

function parseSliderValue(input: HTMLInputElement): number {
  const value = Number(input.value);
  return Number.isFinite(value) ? value : 0;
}

function setOptionalTitle(element: HTMLElement, id: string): void {
  const title = LOCALE_EN_US[`${id}.title`];
  if (title) {
    element.title = title;
  }
}

function applyLocalization(root: ParentNode): void {
  const nodes = root.querySelectorAll<HTMLElement>('[data-l10n-id]');
  for (const element of nodes) {
    const id = element.getAttribute('data-l10n-id');
    if (!id) {
      continue;
    }

    const text = LOCALE_EN_US[id];
    if (text) {
      element.textContent = text;
    }
    setOptionalTitle(element, id);
  }
}

function isPrimaryActivation(event: MouseEvent): boolean {
  return event.button === 0;
}

function textForZapCount(count: number): string {
  if (count <= 0) {
    return '';
  }
  return String(count);
}

function positionFloatingPanel(
  panel: HTMLElement,
  anchorRect: DOMRect,
  offsetX: number,
  offsetY: number,
): void {
  const left = clamp(anchorRect.left + offsetX, 8, Math.max(8, window.innerWidth - 8));
  const top = clamp(anchorRect.top + offsetY, 8, Math.max(8, window.innerHeight - 8));
  panel.style.left = `${left}px`;
  panel.style.top = `${top}px`;
}

// MARK: Editor implementation (ZenBoostsEditor.mjs:31-58, 1548-1631)

class ZenBoostEditorApp {
  private readonly pageHandler: PageHandlerRemote;
  private readonly callbackRouter: PageObserverCallbackRouter;
  private readonly dom: EditorDom;
  private readonly debounceTimers = new Map<DebounceKey, number>();

  private currentBoost: BoostInfo | null = null;
  private systemFonts: string[] = [];
  private lastDotSetPos: {x: number; y: number} | null = null;
  private isMouseDown = false;
  private wasDragging = false;
  private dragTargetId = '';
  private mouseDownPosition: Point = {x: 0, y: 0};
  private isZapModeOn = false;
  private isPickerModeOn = false;
  private closeLifecycleStarted = false;
  private suppressNextUnloadLifecycle = false;
  private renameInput: HTMLInputElement | null = null;
  private codeMirrorView: MahoCodeMirrorEditorView | null = null;
  private isSyncingCodeMirror = false;

  constructor() {
    this.callbackRouter = new PageObserverCallbackRouter();
    this.pageHandler = new PageHandlerRemote();
    PageHandlerFactory.getRemote().createPageHandler(
        this.callbackRouter.$.bindNewPipeAndPassRemote(),
        this.pageHandler.$.bindNewPipeAndPassReceiver());

    this.dom = this.createDomRefs();
    this.initializeCodeEditor();
    this.installLocalization();
    this.bindObserverCallbacks();
    this.bindUiEvents();
  }

  // MARK: Bootstrap and load lifecycle (ZenBoostsEditor.mjs:54-58, 1548-1559)

  async bootstrap(): Promise<void> {
    await this.pageHandler.getDomain();
    await this.initFonts();
    await this.loadActiveBoost();
    this.updateViewMode(WindowMode.kBoost);
    window.requestAnimationFrame(() => this.updateAllVisuals());
  }

  private async loadActiveBoost(): Promise<void> {
    let boost = (await this.pageHandler.getActiveBoost()).boost;
    if (!boost) {
      boost = (await this.pageHandler.createTempBoost()).boost;
    }

    if (!boost) {
      throw new Error('Unable to load or create an active boost.');
    }

    this.currentBoost = boost;
    this.updateAllVisuals();
  }

  private createDomRefs(): EditorDom {
    const importFileInput = document.createElement('input');
    importFileInput.type = 'file';
    importFileInput.accept = FILE_ACCEPT;
    importFileInput.hidden = true;

    return {
      root: document.documentElement,
      editorRoot: requireDiv('zen-boost-editor-root'),
      codeEditorRoot: requireDiv('zen-boost-code-editor-root'),
      closeButton: requireButton('zen-boost-close'),
      shuffleButton: requireButton('zen-boost-shuffle'),
      nameContainer: requireDiv('zen-boost-name-container'),
      nameText: requireParagraph('zen-boost-name-text'),
      gradient: requireClassDiv('.zen-boost-color-picker-gradient'),
      magicThemeButton: requireButton('zen-boost-magic-theme'),
      primaryDot: requireDiv('zen-boost-color-picker-dot-primary'),
      secondaryDot: requireDiv('zen-boost-color-picker-dot-secondary'),
      colorCircle: requireClassDiv('.zen-boost-color-picker-circle'),
      invertButton: requireButton('zen-boost-invert'),
      controlsButton: requireButton('zen-boost-controls'),
      disableButton: requireButton('zen-boost-disable'),
      fontGrid: requireDiv('zen-boost-font-grid'),
      fontSelect: requireSelect('zen-boost-font-select'),
      sizeButton: requireButton('zen-boost-size'),
      sizeText: requireParagraph('zen-boost-size-text'),
      sizeValue: requireParagraph('zen-boost-size-value'),
      caseButton: requireButton('zen-boost-case'),
      caseText: requireParagraph('zen-boost-case-text'),
      zapButton: requireButton('zen-boost-zap'),
      zapValue: requireParagraph('zen-boost-zap-value'),
      codeButton: requireButton('zen-boost-code'),
      backButton: requireButton('zen-boost-back'),
      codeEditor: requireDiv('zen-boost-code-editor'),
      cssPickerButton: requireButton('zen-boost-css-picker'),
      cssInspectorButton: requireButton('zen-boost-css-inspector'),
      advancedColorPanel: requireDiv('zen-boost-advanced-color-options-panel'),
      contrastSlider: requireInput('zen-boost-color-contrast'),
      brightnessSlider: requireInput('zen-boost-color-brightness'),
      saturationSlider: requireInput('zen-boost-color-saturation'),
      contextMenu: requireDiv('zenBoostContextMenu'),
      renameMenuItem: requireButton('zen-boost-edit-rename'),
      shuffleMenuItem: requireButton('zen-boost-edit-shuffle'),
      resetMenuItem: requireButton('zen-boost-edit-reset'),
      loadMenuItem: requireButton('zen-boost-load'),
      saveMenuItem: requireButton('zen-boost-save'),
      deleteMenuItem: requireButton('zen-boost-edit-delete'),
      importFileInput,
    };
  }

  private initializeCodeEditor(): void {
    this.codeMirrorView = new window.MahoCodeMirror.EditorView({
      doc: '',
      extensions: [
        window.MahoCodeMirror.basicSetup,
        window.MahoCodeMirror.css(),
        window.MahoCodeMirror.oneDark,
        window.MahoCodeMirror.EditorView.updateListener.of((update) => {
          if (!update.docChanged || this.isSyncingCodeMirror || !this.currentBoost) {
            return;
          }

          this.currentBoost.customCss = update.state.doc.toString();
          this.markChanged();
          this.scheduleUpdate('custom-css', GENERAL_UPDATE_DEBOUNCE_MS, (): BoostUpdate => this.buildBoostUpdate({
            customCss: this.currentBoost?.customCss ?? '',
          }));
        }),
      ],
      parent: this.dom.codeEditor,
    });

    if (!document.body.contains(this.dom.importFileInput)) {
      document.body.appendChild(this.dom.importFileInput);
    }
  }

  private installLocalization(): void {
    applyLocalization(document);
    this.dom.fontSelect.options[0]!.text = DEFAULT_FONT_OPTION_LABEL;
    this.dom.closeButton.title = 'Close';
    this.dom.magicThemeButton.title = 'Magic Theme';
  }

  // MARK: Mojo observers (ZenBoostsEditor.mjs:15-21, 162-185)

  private bindObserverCallbacks(): void {
    this.callbackRouter.onBoostsChanged.addListener(() => {
      void this.handleBoostsChanged();
    });
    this.callbackRouter.onActiveChanged.addListener((boostId: string | null) => {
      void this.handleActiveChanged(boostId);
    });
    this.callbackRouter.onZapStateUpdate.addListener((isOn: boolean, anyZapped: boolean) => {
      this.handleZapStateUpdate(isOn, anyZapped);
    });
    this.callbackRouter.onZapListUpdate.addListener(() => {
      void this.handleZapListUpdate();
    });
    this.callbackRouter.onPickerStateUpdate.addListener((isOn: boolean) => {
      this.handlePickerStateUpdate(isOn);
    });
    this.callbackRouter.onPickerSelectorPicked.addListener((selector: string) => {
      void this.handlePickerSelectorPicked(selector);
    });
    this.callbackRouter.onEditorKilled.addListener(() => {
      this.suppressNextUnloadLifecycle = false;
      window.close();
    });
  }

  private async handleBoostsChanged(): Promise<void> {
    if (!this.currentBoost) {
      return;
    }

    const {boost} = await this.pageHandler.getBoost(this.currentBoost.id);
    if (boost) {
      this.applyBoost(boost, true);
      return;
    }

    await this.loadActiveBoost();
  }

  private async handleActiveChanged(_boostId: string | null): Promise<void> {
    const {boost} = await this.pageHandler.getActiveBoost();
    if (boost) {
      this.applyBoost(boost, false);
      return;
    }

    const {boost: tempBoost} = await this.pageHandler.createTempBoost();
    if (tempBoost) {
      this.applyBoost(tempBoost, false);
    }
  }

  private handleZapStateUpdate(isOn: boolean, anyZapped: boolean): void {
    this.isZapModeOn = isOn;
    this.dom.zapButton.setAttribute('enabled', isOn || anyZapped ? 'true' : 'false');
    this.dom.zapButton.setAttribute('hideicon', anyZapped ? 'true' : 'false');
  }

  private async handleZapListUpdate(): Promise<void> {
    this.markChanged();
    if (!this.currentBoost) {
      return;
    }

    const {boost} = await this.pageHandler.getBoost(this.currentBoost.id);
    if (boost) {
      this.applyBoost(boost, true);
    }
  }

  private handlePickerStateUpdate(isOn: boolean): void {
    this.isPickerModeOn = isOn;
    this.dom.cssPickerButton.setAttribute('enabled', isOn ? 'true' : 'false');
  }

  private async handlePickerSelectorPicked(selector: string): Promise<void> {
    if (!this.currentBoost) {
      return;
    }

    await this.disableAllPickers();

    if (!this.codeMirrorView) {
      return;
    }

    const docLength = this.codeMirrorView.state.doc.length;
    const block = `\n${selector} {\n\n}`;
    this.codeMirrorView.dispatch({
      changes: {from: docLength, insert: block},
      selection: {anchor: docLength + block.length - 2},
    });
    this.codeMirrorView.focus();
  }

  // MARK: Event wiring (ZenBoostsEditor.mjs:60-133, 521-610)

  private bindUiEvents(): void {
    this.dom.contrastSlider.addEventListener('input', () => {
      this.onColorOptionChange();
      this.scheduleSliderUpdate('contrast');
    });
    this.dom.brightnessSlider.addEventListener('input', () => {
      this.onColorOptionChange();
      this.scheduleSliderUpdate('brightness');
    });
    this.dom.saturationSlider.addEventListener('input', () => {
      this.onColorOptionChange();
      this.scheduleSliderUpdate('saturation');
    });

    this.dom.caseButton.addEventListener('click', () => this.onBoostCasePressed());
    this.dom.sizeButton.addEventListener('click', () => this.onBoostSizePressed());
    this.dom.zapButton.addEventListener('click', () => {
      void this.onZapButtonPressed();
    });
    this.dom.codeButton.addEventListener('click', () => {
      void this.onCodeButtonPressed();
    });
    this.dom.backButton.addEventListener('click', () => {
      void this.onCodeBackButtonPressed();
    });
    this.dom.disableButton.addEventListener('click', () => this.onToggleDisable(true));
    this.dom.invertButton.addEventListener('click', () => this.onToggleInvert(true));
    this.dom.controlsButton.addEventListener('click', (event: MouseEvent) => {
      this.openAdvancedColorOptions(event);
    });
    this.dom.nameContainer.addEventListener('click', (event: MouseEvent) => {
      this.onNameContainerClick(event);
    });
    this.dom.closeButton.addEventListener('click', () => {
      void this.handleCloseRequest();
    });
    this.dom.shuffleButton.addEventListener('click', () => {
      void this.shuffleBoost();
    });
    this.dom.cssPickerButton.addEventListener('click', () => {
      void this.onPickerButtonPressed();
    });
    this.dom.cssInspectorButton.addEventListener('click', () => {
      void this.onInspectorButtonPressed();
    });
    this.dom.magicThemeButton.addEventListener('click', (event: MouseEvent) => {
      this.onThemePickerClick(event);
    });

    this.dom.gradient.addEventListener('mousedown', (event: MouseEvent) => {
      this.onMouseDown(event);
    });
    this.dom.gradient.addEventListener('click', (event: MouseEvent) => {
      this.onThemePickerClick(event);
    });
    document.addEventListener('mousemove', (event: MouseEvent) => {
      this.onMouseMove(event);
    });
    document.addEventListener('mouseup', (event: MouseEvent) => {
      this.onMouseUp(event);
    });

    this.dom.fontSelect.addEventListener('change', () => {
      this.onFontChange(this.dom.fontSelect.value);
    });

    this.dom.renameMenuItem.addEventListener('click', () => this.startInlineRename());
    this.dom.shuffleMenuItem.addEventListener('click', () => {
      void this.shuffleBoost();
    });
    this.dom.resetMenuItem.addEventListener('click', () => {
      void this.resetBoost();
    });
    this.dom.loadMenuItem.addEventListener('click', () => this.triggerImport());
    this.dom.saveMenuItem.addEventListener('click', () => {
      void this.exportBoost();
    });
    this.dom.deleteMenuItem.addEventListener('click', () => {
      void this.deleteBoost();
    });
    this.dom.importFileInput.addEventListener('change', () => {
      void this.handleImportFilePicked();
    });

    document.addEventListener('keydown', (event: KeyboardEvent) => {
      if (event.key === 'Escape') {
        event.preventDefault();
        if (!this.dom.contextMenu.hidden) {
          this.closeContextMenu();
          return;
        }
        if (!this.dom.advancedColorPanel.hidden) {
          this.closeAdvancedColorOptions();
          return;
        }
        void this.handleCloseRequest();
        return;
      }

      if ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === 'w') {
        event.preventDefault();
        void this.handleCloseRequest();
      }
    });

    document.addEventListener('mousedown', (event: MouseEvent) => {
      const target = event.target;
      if (!(target instanceof Node)) {
        return;
      }

      if (!this.dom.contextMenu.hidden && !this.dom.contextMenu.contains(target)) {
        this.closeContextMenu();
      }
      if (!this.dom.advancedColorPanel.hidden &&
          !this.dom.advancedColorPanel.contains(target) &&
          target !== this.dom.controlsButton) {
        this.closeAdvancedColorOptions();
      }
    });

    window.addEventListener('resize', () => {
      this.updateAllVisuals();
    });

    window.addEventListener('unload', () => {
      void this.handleUnloadLifecycle();
    }, {once: true});
  }

  // MARK: Fonts (ZenBoostsEditor.mjs:288-347, 1204-1269)

  private async initFonts(): Promise<void> {
    const {fonts} = await this.pageHandler.getSystemFonts();
    this.systemFonts = fonts.slice();

    this.dom.fontGrid.replaceChildren();
    let availableCommon: string[] = COMMON_FONTS.filter(f => this.systemFonts.includes(f));
    if (availableCommon.length === 0) {
      availableCommon = [...COMMON_FONTS];
    }
    for (const font of availableCommon) {
      const button = document.createElement('button');
      button.type = 'button';
      button.classList.add('subviewbutton');
      button.textContent = 'Aa';
      button.title = font;
      button.setAttribute('font-data', font);
      button.style.fontFamily = `'${font}'`;
      button.addEventListener('click', () => this.onFontChange(font));
      this.dom.fontGrid.appendChild(button);
    }

    this.dom.fontSelect.replaceChildren();

    const defaultOption = document.createElement('option');
    defaultOption.value = '';
    defaultOption.text = DEFAULT_FONT_OPTION_LABEL;
    this.dom.fontSelect.appendChild(defaultOption);

    const separatorOption = document.createElement('option');
    separatorOption.disabled = true;
    separatorOption.text = '────────';
    this.dom.fontSelect.appendChild(separatorOption);

    for (const font of this.systemFonts) {
      const option = document.createElement('option');
      option.value = font;
      option.text = font;
      option.style.fontFamily = `'${font}'`;
      this.dom.fontSelect.appendChild(option);
    }
  }

  private onFontChange(font: string): void {
    if (!this.currentBoost) {
      return;
    }

    const currentFont = this.currentBoost.typography.fontFamily;
    this.currentBoost.typography.fontFamily = currentFont === font ? '' : font;
    this.markChanged();
    this.updateFontButtonVisuals();

    this.scheduleUpdate('font', GENERAL_UPDATE_DEBOUNCE_MS, (): BoostUpdate => this.buildBoostUpdate({
      typography: this.createTypographyPatch({
        setFontFamily: true,
        fontFamily: this.currentBoost?.typography.fontFamily ?? '',
      }),
    }));
  }

  private updateFontButtonVisuals(): void {
    if (!this.currentBoost) {
      return;
    }

    const fontFamily = this.currentBoost.typography.fontFamily;
    const buttons = Array.from(this.dom.fontGrid.children);
    for (const child of buttons) {
      if (!(child instanceof HTMLButtonElement)) {
        continue;
      }
      const isActive = child.getAttribute('font-data') === fontFamily;
      child.classList.toggle('zen-boost-font-button-active', isActive);
    }

    this.dom.fontSelect.value = fontFamily;
    if (fontFamily !== '') {
      this.dom.fontSelect.setAttribute('has-selection', 'true');
    } else {
      this.dom.fontSelect.removeAttribute('has-selection');
      this.dom.fontSelect.value = '';
    }
  }

  // MARK: Code mode and CodeMirror editor (ZenBoostsEditor.mjs:190-265, 352-422)

  private async onCodeButtonPressed(): Promise<void> {
    this.updateViewMode(WindowMode.kCode);
    await this.pageHandler.requestModeResize(WindowMode.kCode);
  }

  private async onCodeBackButtonPressed(): Promise<void> {
    await this.disableAllPickers();
    this.updateViewMode(WindowMode.kBoost);
    await this.pageHandler.requestModeResize(WindowMode.kBoost);
  }

  private updateViewMode(mode: WindowMode): void {
    const isCode = mode === WindowMode.kCode;
    this.dom.root.setAttribute('editor', isCode ? 'code' : 'boost');
    this.dom.editorRoot.style.display = isCode ? 'none' : 'flex';
    this.dom.codeEditorRoot.style.display = isCode ? 'flex' : 'none';

    if (isCode) {
      window.requestAnimationFrame(() => {
        this.codeMirrorView?.requestMeasure();
      });
    }
  }

  // MARK: Color picker interactions (ZenBoostsEditor.mjs:521-1026)

  private onMouseDown(event: MouseEvent): void {
    if (!isPrimaryActivation(event)) {
      return;
    }

    this.mouseDownPosition = {x: event.clientX, y: event.clientY};
    this.isMouseDown = true;
    this.wasDragging = false;
    this.dragTargetId = event.target instanceof Element ? event.target.id : '';
  }

  private onMouseMove(event: MouseEvent): void {
    if (!this.currentBoost || !this.isMouseDown) {
      return;
    }

    const minDragDistance = 4;
    const dx = event.clientX - this.mouseDownPosition.x;
    const dy = event.clientY - this.mouseDownPosition.y;
    const distance = Math.hypot(dx, dy);
    if (distance <= minDragDistance) {
      return;
    }

    this.wasDragging = true;
    event.preventDefault();
    this.markChanged();

    window.requestAnimationFrame(() => {
      if (!this.currentBoost) {
        return;
      }

      if (this.dragTargetId === 'zen-boost-color-picker-dot-secondary') {
        this.dom.secondaryDot.setAttribute('dragging', 'true');
        this.setSecondaryDotPos(event.clientX, event.clientY);
      } else if ((event.target instanceof Element ? event.target.id : '') !== 'zen-boost-magic-theme') {
        this.dom.primaryDot.setAttribute('dragging', 'true');
        this.setDotPos(event.clientX, event.clientY, false);
      }
    });
  }

  private onMouseUp(event: MouseEvent): void {
    if (!isPrimaryActivation(event)) {
      return;
    }

    this.isMouseDown = false;
    this.wasDragging = false;
    this.dom.primaryDot.removeAttribute('dragging');
    this.dom.secondaryDot.removeAttribute('dragging');
  }

  private onThemePickerClick(event: MouseEvent): void {
    if (!this.currentBoost) {
      return;
    }

    event.preventDefault();
    this.markChanged();
    this.currentBoost.color.colorBoostEnabled = true;

    if (event.target === this.dom.magicThemeButton) {
      this.currentBoost.color.magicTheme = !this.currentBoost.color.magicTheme;
      this.updateButtonToggleVisuals();
      this.scheduleUpdate('magic-theme', GENERAL_UPDATE_DEBOUNCE_MS, (): BoostUpdate => this.buildBoostUpdate({
        color: this.createColorPatch({
          colorBoostEnabled: this.currentBoost?.color.colorBoostEnabled ?? true,
          magicTheme: this.currentBoost?.color.magicTheme ?? false,
        }),
      }));
      this.wasDragging = false;
      return;
    }

    if (this.dragTargetId !== 'zen-boost-color-picker-dot-secondary') {
      this.setDotPos(event.clientX, event.clientY, !this.wasDragging);
    }

    this.updateButtonToggleVisuals();
    this.wasDragging = false;
  }

  private computeDotGeometry(clientX: number, clientY: number): DotGeometry {
    const rect = this.dom.gradient.getBoundingClientRect();
    const centerX = rect.left + rect.width / 2;
    const centerY = rect.top + rect.height / 2;
    const radius = (rect.width - COLOR_PICKER_PADDING) / 2;
    const dx = clientX - centerX;
    const dy = clientY - centerY;
    const distance = Math.min(Math.hypot(dx, dy), radius);
    const angleRad = Math.atan2(dy, dx);
    const pixelX = centerX + Math.cos(angleRad) * distance;
    const pixelY = centerY + Math.sin(angleRad) * distance;
    const dotAngleDeg = ((angleRad * 180 / Math.PI + 100) % 360 + 360) % 360;
    const dotDistance = radius > 0 ? distance / radius : 0;
    const dotPos = {
      x: (pixelX - rect.left) / rect.width,
      y: (pixelY - rect.top) / rect.height,
    };

    return {
      pixelX,
      pixelY,
      angleRad,
      dotAngleDeg,
      dotDistance,
      dotPos,
    };
  }

  private setDotPos(clientX: number | null, clientY: number | null, animate = true): void {
    if (!this.currentBoost) {
      return;
    }

    if (!animate && this.lastDotSetPos && clientX !== null && clientY !== null) {
      const dx = clientX - this.lastDotSetPos.x;
      const dy = clientY - this.lastDotSetPos.y;
      if (Math.hypot(dx, dy) < 15) {
        return;
      }
    }

    const rect = this.dom.gradient.getBoundingClientRect();
    const centerX = rect.left + rect.width / 2;
    const centerY = rect.top + rect.height / 2;
    const radius = (rect.width - COLOR_PICKER_PADDING) / 2;

    let primaryPixelX = centerX;
    let primaryPixelY = centerY;
    let secondaryPixelX = centerX;
    let secondaryPixelY = centerY;

    if (clientX === null || clientY === null) {
      this.currentBoost.color.dotAngleDeg = 0;
      this.currentBoost.color.dotDistance = 0;
      this.currentBoost.color.dotPos = {x: 0.5, y: 0.5};
      this.currentBoost.color.secondaryDotPos = {x: 0.5, y: 0.5};
    } else {
      const geometry = this.computeDotGeometry(clientX, clientY);
      primaryPixelX = geometry.pixelX;
      primaryPixelY = geometry.pixelY;
      this.currentBoost.color.dotAngleDeg = geometry.dotAngleDeg;
      this.currentBoost.color.dotDistance = geometry.dotDistance;
      this.currentBoost.color.dotPos = geometry.dotPos;

      const secondaryAngleRad = (
        geometry.angleRad +
        (this.currentBoost.color.secondaryDotAngleDegDelta * Math.PI) / 180
      ) % (Math.PI * 2);
      secondaryPixelX = centerX + Math.cos(secondaryAngleRad) * geometry.dotDistance * radius;
      secondaryPixelY = centerY + Math.sin(secondaryAngleRad) * geometry.dotDistance * radius;
      this.currentBoost.color.secondaryDotPos = {
        x: (secondaryPixelX - rect.left) / rect.width,
        y: (secondaryPixelY - rect.top) / rect.height,
      };

      this.currentBoost.color.colorBoostEnabled = true;
      this.currentBoost.color.magicTheme = false;
    }

    this.dom.primaryDot.setAttribute('animated', animate ? 'true' : 'false');
    this.dom.secondaryDot.setAttribute('animated', animate ? 'true' : 'false');
    this.dom.primaryDot.style.left = `${primaryPixelX - rect.left}px`;
    this.dom.primaryDot.style.top = `${primaryPixelY - rect.top}px`;
    this.dom.secondaryDot.style.left = `${secondaryPixelX - rect.left}px`;
    this.dom.secondaryDot.style.top = `${secondaryPixelY - rect.top}px`;

    this.updateButtonToggleVisuals();
    this.updateDot();
    this.updateCircleRadius();
    this.scheduleUpdate('color-picker-primary', GENERAL_UPDATE_DEBOUNCE_MS, (): BoostUpdate => this.buildBoostUpdate({
      color: this.createColorPatch({
        colorBoostEnabled: this.currentBoost?.color.colorBoostEnabled ?? true,
        dotAngleDeg: this.currentBoost?.color.dotAngleDeg ?? 0,
        dotDistance: this.currentBoost?.color.dotDistance ?? 0,
        dotPos: this.currentBoost?.color.dotPos ?? {x: 0.5, y: 0.5},
        secondaryDotPos: this.currentBoost?.color.secondaryDotPos ?? {x: 0.5, y: 0.5},
        magicTheme: this.currentBoost?.color.magicTheme ?? false,
      }),
    }));

    if (clientX !== null && clientY !== null) {
      this.lastDotSetPos = {x: clientX, y: clientY};
    }
  }

  private updateDot(): void {
    if (!this.currentBoost) {
      return;
    }

    const primaryValue =
      `hsl(${this.currentBoost.color.dotAngleDeg}deg, ${this.currentBoost.color.dotDistance * 100}%, 55%)`;
    const secondaryValue =
      `hsl(${normalizeDegrees(this.currentBoost.color.dotAngleDeg + this.currentBoost.color.secondaryDotAngleDegDelta)}deg, ${this.currentBoost.color.dotDistance * 100}%, 20%)`;

    this.dom.primaryDot.style.setProperty('--zen-theme-picker-dot-color', primaryValue);
    this.dom.secondaryDot.style.setProperty('--zen-theme-picker-dot-color', secondaryValue);
  }

  private setSecondaryDotPos(clientX: number | null, clientY: number | null): void {
    if (!this.currentBoost) {
      return;
    }

    const rect = this.dom.gradient.getBoundingClientRect();
    const centerX = rect.left + rect.width / 2;
    const centerY = rect.top + rect.height / 2;
    const radius = (rect.width - COLOR_PICKER_PADDING) / 2;

    let angle = 0;
    let pixelX = centerX;
    let pixelY = centerY;
    if (clientX === null || clientY === null) {
      angle = this.currentBoost.color.secondaryDotAngleDegDelta;
    } else {
      angle = Math.atan2(clientY - centerY, clientX - centerX);
      pixelX = centerX + Math.cos(angle) * this.currentBoost.color.dotDistance * radius;
      pixelY = centerY + Math.sin(angle) * this.currentBoost.color.dotDistance * radius;
    }

    this.currentBoost.color.secondaryDotAngleDegDelta = normalizeDegrees(
        angle * 180 / Math.PI + 100 - this.currentBoost.color.dotAngleDeg);
    this.currentBoost.color.secondaryDotPos = {
      x: (pixelX - rect.left) / rect.width,
      y: (pixelY - rect.top) / rect.height,
    };

    this.dom.secondaryDot.setAttribute('animated', 'false');
    this.dom.secondaryDot.style.left = `${pixelX - rect.left}px`;
    this.dom.secondaryDot.style.top = `${pixelY - rect.top}px`;

    this.updateButtonToggleVisuals();
    this.updateDot();
    this.updateCircleRadius();
    this.scheduleUpdate('color-picker-secondary', GENERAL_UPDATE_DEBOUNCE_MS, (): BoostUpdate => this.buildBoostUpdate({
      color: this.createColorPatch({
        secondaryDotAngleDegDelta: this.currentBoost?.color.secondaryDotAngleDegDelta ?? 0,
        secondaryDotPos: this.currentBoost?.color.secondaryDotPos ?? {x: 0.5, y: 0.5},
      }),
    }));
  }

  private updateCircleRadius(): void {
    if (!this.currentBoost) {
      return;
    }

    const rect = this.dom.gradient.getBoundingClientRect();
    const radius = (rect.width - COLOR_PICKER_PADDING) / 2;
    const cx = rect.width / 2;
    const cy = rect.height / 2;

    this.dom.colorCircle.setAttribute('animated', 'false');
    this.dom.colorCircle.style.width = `${this.currentBoost.color.dotDistance * radius * 2}px`;
    this.dom.colorCircle.style.height = `${this.currentBoost.color.dotDistance * radius * 2}px`;

    const primaryColor =
      `hsl(${this.currentBoost.color.dotAngleDeg}deg, ${this.currentBoost.color.dotDistance * 100}%, 55%)`;
    const secondaryColor =
      `hsl(${this.currentBoost.color.dotAngleDeg + this.currentBoost.color.secondaryDotAngleDegDelta}deg, ${this.currentBoost.color.dotDistance * 100}%, 20%)`;

    this.updateArcFill(cx, cy, radius, primaryColor, secondaryColor);
  }

  private updateArcFill(
    cx: number,
    cy: number,
    radius: number,
    color1: string,
    color2: string,
  ): void {
    if (!this.currentBoost) {
      return;
    }

    let svg = this.dom.gradient.querySelector<SVGSVGElement>('.zen-boost-color-picker-arc-svg');
    if (!svg) {
      this.initArcSvg();
      svg = this.dom.gradient.querySelector<SVGSVGElement>('.zen-boost-color-picker-arc-svg');
      if (!svg) {
        return;
      }
    }

    const angle1 = this.currentBoost.color.dotAngleDeg;
    const angle2 = this.currentBoost.color.dotAngleDeg + this.currentBoost.color.secondaryDotAngleDegDelta;
    const distance = this.currentBoost.color.dotDistance;
    const ringRadius = distance * radius;
    const thickness = 2;

    const toXY = (deg: number, targetRadius: number): [number, number] => {
      const rad = ((deg - 90) * Math.PI) / 180;
      return [cx + targetRadius * Math.cos(rad), cy + targetRadius * Math.sin(rad)];
    };

    const [x1, y1] = toXY(angle1, ringRadius);
    const [x2, y2] = toXY(angle2, ringRadius);
    const gradient = svg.querySelector<SVGLinearGradientElement>('#arc-gradient');
    const stop1 = svg.querySelector<SVGStopElement>('#ag-stop1');
    const stop2 = svg.querySelector<SVGStopElement>('#ag-stop2');
    const arcFill = svg.querySelector<SVGPathElement>('.arc-fill');
    if (!gradient || !stop1 || !stop2 || !arcFill) {
      return;
    }

    stop1.setAttribute('stop-color', color1);
    stop2.setAttribute('stop-color', color2);
    gradient.setAttribute('x1', String(x1));
    gradient.setAttribute('y1', String(y1));
    gradient.setAttribute('x2', String(x2));
    gradient.setAttribute('y2', String(y2));

    const outerRadius = ringRadius + thickness / 2;
    const innerRadius = Math.max(ringRadius - thickness / 2, 1);
    const delta = (angle2 - angle1 + 360) % 360;
    const largeArc = delta > 180 ? 1 : 0;
    const [ox1, oy1] = toXY(angle1, outerRadius);
    const [ox2, oy2] = toXY(angle2, outerRadius);
    const [ix2, iy2] = toXY(angle2, innerRadius);
    const [ix1, iy1] = toXY(angle1, innerRadius);
    const path = `M ${ox1} ${oy1} A ${outerRadius} ${outerRadius} 0 ${largeArc} 1 ${ox2} ${oy2} L ${ix2} ${iy2} A ${innerRadius} ${innerRadius} 0 ${largeArc} 0 ${ix1} ${iy1} Z`;
    arcFill.setAttribute('d', path);
  }

  private initArcSvg(): void {
    const width = this.dom.gradient.clientWidth;
    const height = this.dom.gradient.clientHeight;
    if (!width || !height) {
      return;
    }

    const namespace = 'http://www.w3.org/2000/svg';
    const svg = document.createElementNS(namespace, 'svg');
    svg.classList.add('zen-boost-color-picker-arc-svg');
    svg.setAttribute('width', String(width));
    svg.setAttribute('height', String(height));
    svg.setAttribute('viewBox', `0 0 ${width} ${height}`);
    svg.style.position = 'absolute';
    svg.style.top = '0';
    svg.style.left = '0';
    svg.style.pointerEvents = 'none';
    svg.style.zIndex = '3';

    const defs = document.createElementNS(namespace, 'defs');
    const gradient = document.createElementNS(namespace, 'linearGradient');
    gradient.setAttribute('id', 'arc-gradient');
    gradient.setAttribute('gradientUnits', 'userSpaceOnUse');

    const stop1 = document.createElementNS(namespace, 'stop');
    stop1.setAttribute('id', 'ag-stop1');
    stop1.setAttribute('offset', '0%');

    const stop2 = document.createElementNS(namespace, 'stop');
    stop2.setAttribute('id', 'ag-stop2');
    stop2.setAttribute('offset', '100%');

    gradient.appendChild(stop1);
    gradient.appendChild(stop2);
    defs.appendChild(gradient);
    svg.appendChild(defs);

    const path = document.createElementNS(namespace, 'path');
    path.classList.add('arc-fill');
    path.setAttribute('fill', 'url(#arc-gradient)');
    path.setAttribute('opacity', '0.65');
    svg.appendChild(path);

    this.dom.gradient.appendChild(svg);
  }

  // MARK: Button and slider state (ZenBoostsEditor.mjs:612-1197)

  private onBoostCasePressed(): void {
    if (!this.currentBoost) {
      return;
    }

    this.currentBoost.typography.caseMode = nextCaseMode(this.currentBoost.typography.caseMode);
    this.markChanged();
    this.updateCaseButtonVisuals();

    this.scheduleUpdate('case', GENERAL_UPDATE_DEBOUNCE_MS, (): BoostUpdate => this.buildBoostUpdate({
      typography: this.createTypographyPatch({
        caseMode: this.currentBoost?.typography.caseMode ?? CaseMode.kNone,
      }),
    }));
  }

  private onBoostSizePressed(): void {
    if (!this.currentBoost) {
      return;
    }

    this.currentBoost.typography.sizeMode = nextSizeMode(this.currentBoost.typography.sizeMode);
    this.markChanged();
    this.updateSizeButtonVisuals();

    this.scheduleUpdate('size', GENERAL_UPDATE_DEBOUNCE_MS, (): BoostUpdate => this.buildBoostUpdate({
      typography: this.createTypographyPatch({
        setSizeMode: true,
        sizeMode: this.currentBoost?.typography.sizeMode ?? SizeMode.k100,
      }),
    }));
  }

  private onColorOptionChange(): void {
    if (!this.currentBoost) {
      return;
    }

    this.currentBoost.color.contrast = parseSliderValue(this.dom.contrastSlider);
    this.currentBoost.color.brightness = parseSliderValue(this.dom.brightnessSlider);
    this.currentBoost.color.saturation = parseSliderValue(this.dom.saturationSlider);
    this.markChanged();
  }

  private openAdvancedColorOptions(event: MouseEvent): void {
    const target = event.currentTarget;
    if (!(target instanceof HTMLElement)) {
      return;
    }

    const panel = this.dom.advancedColorPanel;
    panel.hidden = false;
    panel.style.left = '0px';
    panel.style.top = '0px';

    const trigger = target.getBoundingClientRect();
    const panelRect = panel.getBoundingClientRect();
    const margin = 8;

    const triggerCenterX = trigger.left + trigger.width / 2;
    const idealLeft = triggerCenterX - panelRect.width / 2;
    const maxLeft = Math.max(margin, window.innerWidth - panelRect.width - margin);
    const left = Math.max(margin, Math.min(idealLeft, maxLeft));
    const top = trigger.bottom + 8;

    panel.style.left = `${left}px`;
    panel.style.top = `${top}px`;
    panel.style.setProperty('--arrow-x', `${triggerCenterX - left}px`);
  }

  private closeAdvancedColorOptions(): void {
    this.dom.advancedColorPanel.hidden = true;
  }

  private onToggleDisable(userAction: boolean): void {
    if (!this.currentBoost) {
      return;
    }

    this.currentBoost.color.colorBoostEnabled = !this.currentBoost.color.colorBoostEnabled;
    if (userAction) {
      this.markChanged();
    }

    this.updateButtonToggleVisuals();
    this.scheduleUpdate('disable', GENERAL_UPDATE_DEBOUNCE_MS, (): BoostUpdate => this.buildBoostUpdate({
      color: this.createColorPatch({
        colorBoostEnabled: this.currentBoost?.color.colorBoostEnabled ?? false,
      }),
    }));
  }

  private onToggleInvert(userAction: boolean): void {
    if (!this.currentBoost) {
      return;
    }

    this.currentBoost.color.smartInvert = !this.currentBoost.color.smartInvert;
    if (userAction) {
      this.markChanged();
    }

    this.updateButtonToggleVisuals();
    this.scheduleUpdate('invert', GENERAL_UPDATE_DEBOUNCE_MS, (): BoostUpdate => this.buildBoostUpdate({
      color: this.createColorPatch({
        smartInvert: this.currentBoost?.color.smartInvert ?? false,
      }),
    }));
  }

  private updateCaseButtonVisuals(): void {
    if (!this.currentBoost) {
      return;
    }

    const caseMode = this.currentBoost.typography.caseMode;
    this.dom.caseButton.setAttribute('case-mode', caseAttrFromMode(caseMode));
    this.dom.caseButton.setAttribute('mode', buttonModeForCase(caseMode));
    this.dom.caseText.style.display = caseMode === CaseMode.kNone ? 'initial' : 'none';
  }

  private updateSizeButtonVisuals(): void {
    if (!this.currentBoost) {
      return;
    }

    const sizeMode = this.currentBoost.typography.sizeMode;
    const factor = sizeFactorFromMode(sizeMode);
    this.dom.sizeButton.setAttribute('mode', buttonModeForSize(sizeMode));
    const showValue = sizeMode !== SizeMode.k100;
    this.dom.sizeText.style.display = showValue ? 'none' : 'initial';
    this.dom.sizeValue.style.display = showValue ? 'initial' : 'none';
    this.dom.sizeValue.textContent = `${Math.round(factor * 100)}%`;
  }

  private updateButtonToggleVisuals(): void {
    if (!this.currentBoost) {
      return;
    }

    this.dom.magicThemeButton.classList.toggle(
        'zen-boost-button-active', this.currentBoost.color.magicTheme);
    this.dom.invertButton.classList.toggle(
        'zen-boost-button-active', this.currentBoost.color.smartInvert);
    this.dom.disableButton.classList.toggle(
        'zen-boost-button-active-transparent', !this.currentBoost.color.colorBoostEnabled);
    this.dom.gradient.classList.toggle(
        'zen-boost-panel-disabled',
        !this.currentBoost.color.colorBoostEnabled || this.currentBoost.color.magicTheme);

    if (this.currentBoost.color.magicTheme) {
      this.dom.gradient.style.background =
          `linear-gradient(180deg, color-mix(in srgb, ${MAGIC_THEME_FALLBACK_ACCENT} 70%, white) 0%, ${MAGIC_THEME_FALLBACK_ACCENT} 100%)`;
    } else {
      this.dom.gradient.style.background = '';
    }
  }

  private updateColorControlSliderVisuals(): void {
    if (!this.currentBoost) {
      return;
    }

    this.dom.contrastSlider.value = String(this.currentBoost.color.contrast);
    this.dom.brightnessSlider.value = String(this.currentBoost.color.brightness);
    this.dom.saturationSlider.value = String(this.currentBoost.color.saturation);
  }

  private updateZapValueVisuals(): void {
    if (!this.currentBoost) {
      this.dom.zapValue.textContent = '';
      this.dom.zapButton.setAttribute('hideicon', 'false');
      return;
    }

    const count = this.currentBoost.zapSelectors.length;
    this.dom.zapValue.textContent = textForZapCount(count);
    this.dom.zapButton.setAttribute('hideicon', count > 0 ? 'true' : 'false');
  }

  private scheduleSliderUpdate(which: 'contrast' | 'brightness' | 'saturation'): void {
    const patch = (): BoostUpdate => {
      if (!this.currentBoost) {
        return this.buildBoostUpdate({});
      }

      if (which === 'contrast') {
        return this.buildBoostUpdate({color: this.createColorPatch({contrast: this.currentBoost.color.contrast})});
      }
      if (which === 'brightness') {
        return this.buildBoostUpdate({color: this.createColorPatch({brightness: this.currentBoost.color.brightness})});
      }
      return this.buildBoostUpdate({color: this.createColorPatch({saturation: this.currentBoost.color.saturation})});
    };

    this.scheduleUpdate(which, SLIDER_UPDATE_DEBOUNCE_MS, patch);
  }

  // MARK: Zap, picker, inspector controls (ZenBoostsEditor.mjs:424-519)

  private async onZapButtonPressed(): Promise<void> {
    if (!this.currentBoost) {
      return;
    }

    if (this.isZapModeOn) {
      await this.pageHandler.exitZapMode();
      return;
    }
    await this.pageHandler.enterZapMode(this.currentBoost.id);
  }

  private async onPickerButtonPressed(): Promise<void> {
    if (!this.currentBoost) {
      return;
    }

    if (this.isPickerModeOn) {
      await this.pageHandler.exitPickerMode();
      return;
    }
    await this.pageHandler.enterPickerMode(this.currentBoost.id);
  }

  private async disableAllPickers(): Promise<void> {
    await Promise.all([
      this.pageHandler.exitZapMode(),
      this.pageHandler.exitPickerMode(),
    ]);
  }

  private async onInspectorButtonPressed(): Promise<void> {
    await this.pageHandler.openInspector();
  }

  // MARK: Context menu, rename, import, export (ZenBoostsEditor.mjs:1298-1476)

  private onNameContainerClick(event: MouseEvent): void {
    this.updateContextMenuDisabledState();
    this.closeAdvancedColorOptions();

    const x = event.clientX;
    const y = event.clientY;
    const anchor = new DOMRect(x, y, 1, 1);
    positionFloatingPanel(this.dom.contextMenu, anchor, 0, 4);
    this.dom.contextMenu.hidden = false;
  }

  private closeContextMenu(): void {
    this.dom.contextMenu.hidden = true;
  }

  private updateContextMenuDisabledState(): void {
    const shouldDisable = !this.currentBoost?.changeWasMade;
    const buttons = [
      this.dom.renameMenuItem,
      this.dom.resetMenuItem,
      this.dom.deleteMenuItem,
    ];

    for (const button of buttons) {
      button.disabled = shouldDisable;
      if (shouldDisable) {
        button.setAttribute('disabled', '');
      } else {
        button.removeAttribute('disabled');
      }
    }
  }

  private startInlineRename(): void {
    if (!this.currentBoost || this.renameInput) {
      return;
    }

    this.closeContextMenu();
    const currentName = this.currentBoost.name;
    const input = document.createElement('input');
    input.type = 'text';
    input.value = currentName;
    input.maxLength = 10;
    input.setAttribute('aria-label', LOCALE_EN_US['zen-boost-rename-boost-prompt'] ?? 'Rename Boost');
    input.style.width = '100%';
    input.style.maxWidth = '100px';
    input.style.border = 'none';
    input.style.outline = 'none';
    input.style.background = 'transparent';
    input.style.font = 'inherit';
    input.style.fontWeight = '600';
    input.style.textAlign = 'center';
    input.style.color = 'inherit';

    this.renameInput = input;
    this.dom.nameText.hidden = true;
    this.dom.nameContainer.appendChild(input);
    input.focus();
    input.select();

    const finish = (commit: boolean): void => {
      if (!this.renameInput) {
        return;
      }

      const nextValue = this.renameInput.value.trim().slice(0, 10);
      this.renameInput.remove();
      this.renameInput = null;
      this.dom.nameText.hidden = false;

      if (!commit || !this.currentBoost || nextValue.length === 0 || nextValue === this.currentBoost.name) {
        return;
      }

      this.currentBoost.name = nextValue;
      this.markChanged();
      this.dom.nameText.textContent = nextValue;
      this.scheduleUpdate('name', GENERAL_UPDATE_DEBOUNCE_MS, (): BoostUpdate => this.buildBoostUpdate({
        name: this.currentBoost?.name ?? DEFAULT_BOOST_NAME,
      }));
    };

    input.addEventListener('keydown', (event: KeyboardEvent) => {
      if (event.key === 'Enter') {
        event.preventDefault();
        finish(true);
      } else if (event.key === 'Escape') {
        event.preventDefault();
        finish(false);
      }
    });
    input.addEventListener('blur', () => finish(true), {once: true});
  }

  private async shuffleBoost(): Promise<void> {
    this.closeContextMenu();
    if (!this.currentBoost) {
      return;
    }

    const {boost} = await this.pageHandler.shuffleBoost(this.currentBoost.id);
    if (!boost) {
      return;
    }

    this.applyBoost(boost, true);
    this.markChanged();
    this.updateAllVisuals();
  }

  private async resetBoost(): Promise<void> {
    this.closeContextMenu();
    if (!this.currentBoost) {
      return;
    }

    const confirmed = window.confirm('Reset all edits?');
    if (!confirmed) {
      return;
    }

    const {boost} = await this.pageHandler.resetBoost(this.currentBoost.id);
    if (!boost) {
      return;
    }

    this.applyBoost(boost, false);
    this.updateAllVisuals();
  }

  private triggerImport(): void {
    this.closeContextMenu();
    this.dom.importFileInput.value = '';
    this.dom.importFileInput.click();
  }

  private async handleImportFilePicked(): Promise<void> {
    const file = this.dom.importFileInput.files?.[0];
    if (!file) {
      return;
    }

    const text = await file.text();
    const {boost} = await this.pageHandler.importBoost(text);
    this.dom.importFileInput.value = '';
    if (!boost) {
      return;
    }

    this.applyBoost(boost, true);
    this.markChanged();
    this.updateAllVisuals();
    this.windowImportAnimation();
  }

  private async exportBoost(): Promise<void> {
    this.closeContextMenu();
    if (!this.currentBoost) {
      return;
    }

    const {json} = await this.pageHandler.exportBoost(this.currentBoost.id);
    const blob = new Blob([json], {type: 'application/json'});
    const url = URL.createObjectURL(blob);
    const anchor = document.createElement('a');
    anchor.href = url;
    anchor.download = `boost-${this.sanitizeFileName(this.currentBoost.name)}.json`;
    anchor.click();
    URL.revokeObjectURL(url);
    this.showToast('Boost exported');
  }

  private showToast(message: string): void {
    const toast = document.createElement('div');
    toast.textContent = message;
    toast.setAttribute('role', 'status');
    toast.style.position = 'fixed';
    toast.style.left = '50%';
    toast.style.bottom = '24px';
    toast.style.transform = 'translateX(-50%) translateY(8px)';
    toast.style.padding = '8px 14px';
    toast.style.borderRadius = '999px';
    toast.style.background = 'rgba(28, 30, 36, 0.92)';
    toast.style.color = '#f6f6f8';
    toast.style.font = '12px/1.4 system-ui, -apple-system, sans-serif';
    toast.style.fontWeight = '600';
    toast.style.boxShadow = '0 10px 28px rgba(0, 0, 0, 0.28)';
    toast.style.opacity = '0';
    toast.style.pointerEvents = 'none';
    toast.style.zIndex = '2147483647';
    toast.style.transition = 'opacity 180ms ease-out, transform 180ms ease-out';
    document.body.appendChild(toast);

    requestAnimationFrame(() => {
      toast.style.opacity = '1';
      toast.style.transform = 'translateX(-50%) translateY(0)';
    });

    window.setTimeout(() => {
      toast.style.opacity = '0';
      toast.style.transform = 'translateX(-50%) translateY(8px)';
      window.setTimeout(() => toast.remove(), 220);
    }, 2200);
  }

  private sanitizeFileName(value: string): string {
    const trimmed = value.trim();
    const safe = trimmed.replace(/[^a-z0-9-_]+/gi, '-').replace(/^-+|-+$/g, '');
    return safe || 'boost';
  }

  private async deleteBoost(): Promise<void> {
    this.closeContextMenu();
    if (!this.currentBoost) {
      return;
    }

    const confirmed = window.confirm('Delete boost?');
    if (!confirmed) {
      return;
    }

    const boostId = this.currentBoost.id;
    await this.pageHandler.deleteBoost(boostId);
    this.currentBoost = null;
    this.closeLifecycleStarted = true;
    await this.pageHandler.closeDialog();
  }

  private windowImportAnimation(): void {
    const windowWrapper = this.dom.root;
    if (window.matchMedia('(prefers-reduced-motion: reduce)').matches) {
      return;
    }

    const overlay = document.createElement('div');
    overlay.id = 'import-animation';

    const border = document.createElement('div');
    border.id = 'import-animation-border';

    const shadow = document.createElement('div');
    shadow.id = 'import-animation-shadow';

    window.requestAnimationFrame(() => {
      windowWrapper.appendChild(overlay);
      windowWrapper.appendChild(border);
      windowWrapper.appendChild(shadow);

      const animation1 = overlay.animate(
          [{top: '100%', opacity: 0.5}, {top: '-50%', opacity: 1}],
          {duration: 350, delay: 120, fill: 'forwards', easing: 'ease-out'});
      const animation2 = border.animate(
          [{['--background-top' as never]: '150%'}, {['--background-top' as never]: '-50%'}],
          {duration: 350, delay: 200, fill: 'forwards', easing: 'ease-out'});
      const animation3 = shadow.animate(
          [{opacity: 0}, {opacity: 1}, {opacity: 0}],
          {duration: 460, fill: 'forwards', easing: 'ease-out'});

      void Promise.all([animation1.finished, animation2.finished, animation3.finished]).finally(() => {
        overlay.remove();
        border.remove();
        shadow.remove();
      });
    });
  }

  // MARK: Boost patching and hydration (ZenBoostsEditor.mjs:1275-1282, 1561-1631)

  private markChanged(): void {
    if (!this.currentBoost) {
      return;
    }
    this.currentBoost.changeWasMade = true;
    this.updateContextMenuDisabledState();
  }

  private applyBoost(boost: BoostInfo, preserveDirty: boolean): void {
    const wasDirty = preserveDirty && this.currentBoost?.changeWasMade;
    this.currentBoost = {
      ...boost,
      color: {...boost.color, dotPos: {...boost.color.dotPos}, secondaryDotPos: {...boost.color.secondaryDotPos}},
      typography: {...boost.typography},
      zapSelectors: [...boost.zapSelectors],
      changeWasMade: wasDirty ? true : boost.changeWasMade,
    };
    this.updateAllVisuals();
  }

  private updateAllVisuals(): void {
    if (!this.currentBoost) {
      return;
    }

    this.dom.nameText.textContent = this.currentBoost.name || DEFAULT_BOOST_NAME;

    const rect = this.dom.gradient.getBoundingClientRect();
    if (rect.width > 0 && rect.height > 0) {
      const normalizedPrimary = this.normalizeStoredPoint(this.currentBoost.color.dotPos, rect.width, rect.height);
      this.currentBoost.color.dotPos = normalizedPrimary;
      this.dom.primaryDot.style.left = `${normalizedPrimary.x * rect.width}px`;
      this.dom.primaryDot.style.top = `${normalizedPrimary.y * rect.height}px`;

      const normalizedSecondary = this.normalizeStoredPoint(
          this.currentBoost.color.secondaryDotPos, rect.width, rect.height);
      this.currentBoost.color.secondaryDotPos = normalizedSecondary;
      this.dom.secondaryDot.style.left = `${normalizedSecondary.x * rect.width}px`;
      this.dom.secondaryDot.style.top = `${normalizedSecondary.y * rect.height}px`;
    }

    this.syncCodeMirrorDocument(this.currentBoost.customCss || '');
    this.updateFontButtonVisuals();
    this.updateCaseButtonVisuals();
    this.updateSizeButtonVisuals();
    this.updateColorControlSliderVisuals();
    this.updateButtonToggleVisuals();
    this.updateDot();
    this.updateCircleRadius();
    this.updateZapValueVisuals();
    this.dom.zapButton.setAttribute(
        'enabled', this.isZapModeOn || this.currentBoost.zapSelectors.length > 0 ? 'true' : 'false');
    this.dom.cssPickerButton.setAttribute('enabled', this.isPickerModeOn ? 'true' : 'false');
  }

  private syncCodeMirrorDocument(value: string): void {
    if (!this.codeMirrorView) {
      return;
    }

    const currentValue = this.codeMirrorView.state.doc.toString();
    if (currentValue === value) {
      return;
    }

    this.isSyncingCodeMirror = true;
    try {
      this.codeMirrorView.dispatch({
        changes: {
          from: 0,
          to: this.codeMirrorView.state.doc.length,
          insert: value,
        },
      });
    } finally {
      this.isSyncingCodeMirror = false;
    }
  }

  private normalizeStoredPoint(point: Point, width: number, height: number): Point {
    const normalized = {...point};
    if (normalized.x > 1 || normalized.x < 0 || normalized.y > 1 || normalized.y < 0) {
      normalized.x = width === 0 ? 0.5 : normalized.x / width;
      normalized.y = height === 0 ? 0.5 : normalized.y / height;
    }
    return normalized;
  }

  private scheduleUpdate(
    key: DebounceKey,
    delayMs: number,
    buildPatch: () => BoostUpdate,
  ): void {
    const existing = this.debounceTimers.get(key);
    if (existing !== undefined) {
      window.clearTimeout(existing);
    }

    const timer = window.setTimeout(() => {
      this.debounceTimers.delete(key);
      void this.flushPatch(buildPatch());
    }, delayMs);
    this.debounceTimers.set(key, timer);
  }

  private async flushPatch(patch: BoostUpdate): Promise<void> {
    if (!this.currentBoost) {
      return;
    }

    const hasWork = patch.name !== undefined ||
      patch.color !== undefined ||
      patch.typography !== undefined ||
      patch.zapSelectors !== undefined ||
      patch.customCss !== undefined;
    if (!hasWork) {
      return;
    }

    const {boost} = await this.pageHandler.updateBoost(this.currentBoost.id, patch);
    if (boost) {
      this.applyBoost(boost, true);
    }
  }

  private buildBoostUpdate(partial: Partial<BoostUpdate>): BoostUpdate {
    return {
      name: null,
      color: null,
      typography: null,
      zapSelectors: null,
      customCss: null,
      ...partial,
    };
  }

  private createColorPatch(values: {
    colorBoostEnabled?: boolean;
    dotAngleDeg?: number;
    secondaryDotAngleDegDelta?: number;
    brightness?: number;
    saturation?: number;
    contrast?: number;
    magicTheme?: boolean;
    smartInvert?: boolean;
    dotPos?: Point;
    dotDistance?: number;
    secondaryDotPos?: Point;
  }): ColorBoostUpdate {
    const patch: ColorBoostUpdate = {
      colorBoostEnabled: null,
      dotAngleDeg: null,
      secondaryDotAngleDegDelta: null,
      brightness: null,
      saturation: null,
      contrast: null,
      magicTheme: null,
      smartInvert: null,
      dotPos: null,
      dotDistance: null,
      secondaryDotPos: null,
    };
    if (values.colorBoostEnabled !== undefined) {
      patch.colorBoostEnabled = values.colorBoostEnabled;
    }
    if (values.dotAngleDeg !== undefined) {
      patch.dotAngleDeg = values.dotAngleDeg;
    }
    if (values.secondaryDotAngleDegDelta !== undefined) {
      patch.secondaryDotAngleDegDelta = values.secondaryDotAngleDegDelta;
    }
    if (values.brightness !== undefined) {
      patch.brightness = values.brightness;
    }
    if (values.saturation !== undefined) {
      patch.saturation = values.saturation;
    }
    if (values.contrast !== undefined) {
      patch.contrast = values.contrast;
    }
    if (values.magicTheme !== undefined) {
      patch.magicTheme = values.magicTheme;
    }
    if (values.smartInvert !== undefined) {
      patch.smartInvert = values.smartInvert;
    }
    if (values.dotPos !== undefined) {
      patch.dotPos = values.dotPos;
    }
    if (values.dotDistance !== undefined) {
      patch.dotDistance = values.dotDistance;
    }
    if (values.secondaryDotPos !== undefined) {
      patch.secondaryDotPos = values.secondaryDotPos;
    }
    return patch;
  }

  private createTypographyPatch(values: {
    setFontFamily?: boolean;
    fontFamily?: string;
    caseMode?: CaseMode;
    setSizeMode?: boolean;
    sizeMode?: SizeMode;
  }): TypographyBoostUpdate {
    return {
      setFontFamily: values.setFontFamily ?? false,
      fontFamily: values.fontFamily ?? '',
      caseMode: values.caseMode ?? null,
      setSizeMode: values.setSizeMode ?? false,
      sizeMode: values.sizeMode ?? SizeMode.k100,
    };
  }

  // MARK: Close lifecycle (ZenBoostsEditor.mjs:64-66, 1520-1540)

  private async handleCloseRequest(): Promise<void> {
    if (this.closeLifecycleStarted) {
      return;
    }

    this.closeLifecycleStarted = true;
    for (const timer of this.debounceTimers.values()) {
      window.clearTimeout(timer);
    }
    this.debounceTimers.clear();

    await this.disableAllPickers();

    if (this.currentBoost) {
      if (this.currentBoost.changeWasMade) {
        await this.pageHandler.commitBoost(this.currentBoost.id);
      } else {
        await this.pageHandler.discardBoost(this.currentBoost.id);
      }
    }

    this.suppressNextUnloadLifecycle = true;
    await this.pageHandler.closeDialog();
  }

  private async handleUnloadLifecycle(): Promise<void> {
    if (this.closeLifecycleStarted || this.suppressNextUnloadLifecycle) {
      return;
    }

    this.closeLifecycleStarted = true;
    await this.disableAllPickers();
    if (!this.currentBoost) {
      return;
    }

    if (this.currentBoost.changeWasMade) {
      await this.pageHandler.commitBoost(this.currentBoost.id);
      return;
    }
    await this.pageHandler.discardBoost(this.currentBoost.id);
  }
}

// MARK: Entry point (ZenBoostsEditor.mjs:54-58)

async function start(): Promise<void> {
  const app = new ZenBoostEditorApp();
  await app.bootstrap();
}

if (document.readyState === 'loading') {
  document.addEventListener('DOMContentLoaded', () => {
    void start();
  }, {once: true});
} else {
  void start();
}
