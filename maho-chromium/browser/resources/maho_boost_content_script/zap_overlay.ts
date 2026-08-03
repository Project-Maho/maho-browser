// Copyright 2026 Maho Browser. All rights reserved.
// Port of ZenZapOverlayChild.sys.mjs — zap overlay highlight + capture.

(function() {
'use strict';

type MahoBoostNS = {
  SelectorComponent: new (
    doc: Document,
    ids: readonly string[],
    onSelect: (s: string) => void,
    labels?: { readonly selectThis: string; readonly selectRelated: string; readonly cancel: string },
    onStatus?: (status: SelectionStatus) => void
  ) => SelectorComponentInstance;
  ZapOverlay: typeof ZapOverlay;
  __mahoBoost: MahoBoostNS;
  __mahoBoostCSS: string;
};

interface SelectorComponentInstance {
  initialize(): void;
  tearDown(): void;
  handleEvent(event: Event, prevent: boolean): void;
  setState(state: 'selecting' | 'selected', data?: Element | null): void;
  showHighlight(selection: NodeListOf<Element> | readonly Element[]): void;
  removeHighlight(): void;
  safeAreaPadding: { left: number; right: number; top: number; bottom: number };
}

interface SelectionStatus {
  readonly code: string;
  readonly message: string;
  readonly selector: string | null;
  readonly matchCount: number;
}

const ns = (window as unknown as MahoBoostNS).__mahoBoost;

class ZapOverlay {
  private doc: Document;
  private win: Window;
  private initialized = false;
  private container: HTMLDivElement | null = null;
  private shadowRoot: ShadowRoot | null = null;

  private zapContentIDs = ['zap-list', 'zap-controls-container'];
  private selectorComponent: SelectorComponentInstance | null = null;
  private onZapDoneClick: (() => void) | null = null;
  private zapDoneButton: HTMLInputElement | null = null;
  private boostContext: BoostContext;
  private unzapHoverFrameId: number | null = null;

  constructor(doc: Document, boostContext: BoostContext) {
    this.doc = doc;
    this.win = doc.defaultView!;
    this.boostContext = boostContext;

    this.selectorComponent = new ns.SelectorComponent(
      doc,
      this.zapContentIDs,
      (selector: string) => this.handleSelectComponentSelect(selector),
      { selectThis: 'Zap this', selectRelated: 'Related', cancel: 'Cancel' },
      (status: SelectionStatus) => this.handleSelectionStatus(status)
    );
    this.selectorComponent.safeAreaPadding.bottom = 65;
  }

  initialize(): void {
    if (this.initialized) return;

    this.selectorComponent?.initialize();

    this.container = this.doc.createElement('div');
    this.container.setAttribute('data-maho-boost-zap', 'true');
    this.container.style.cssText =
      'position:fixed;top:0;left:0;width:0;height:0;z-index:2147483640;pointer-events:none;';
    this.doc.documentElement.appendChild(this.container);
    this.shadowRoot = this.container.attachShadow({ mode: 'closed' });

    const style = this.doc.createElement('style');
    style.textContent = (this.win as unknown as MahoBoostNS).__mahoBoostCSS || '';
    this.shadowRoot.appendChild(style);

    const tpl = this.doc.createElement('template');
    tpl.innerHTML = `
      <div id="zap-controls-container">
        <div id="zap-list"></div>
        <input type="button" id="zap-done" value="Done"/>
      </div>
      <div id="zap-border"></div>
    `;
    this.shadowRoot.appendChild(tpl.content.cloneNode(true));
    this.initializeElements();
    this.initialized = true;
  }

  private getElementById(id: string): HTMLElement | null {
    return this.shadowRoot?.getElementById(id) ?? null;
  }

  private initializeElements(): void {
    const zapDoneButton = this.getElementById('zap-done');
    this.zapDoneButton = zapDoneButton instanceof HTMLInputElement ? zapDoneButton : null;
    this.onZapDoneClick = () => this.boostContext.disableZapMode();
    this.zapDoneButton?.addEventListener('click', this.onZapDoneClick);
    this.updateZappedList();
  }

  private handleSelectComponentSelect(cssSelector: string): void {
    this.boostContext.addZapSelector(cssSelector);
    this.onZapUpdate();
  }

  private handleSelectionStatus(status: SelectionStatus): void {
    if (status.code !== 'ready') {
      this.boostContext.sendNotify('zap-selection-status', status.code);
    }
  }

  onZapUpdate(): void {
    this.updateZappedList();
    this.boostContext.sendNotify('zap-list-update');
  }

  private updateZappedList(): void {
    const zapList = this.getElementById('zap-list');
    if (!zapList) return;
    zapList.replaceChildren();

    const zapSelectors = this.boostContext.getZapSelectors();

    zapSelectors.forEach((selector: string, idx: number) => {
      const unzapButton = this.doc.createElement('input');
      unzapButton.type = 'button';
      unzapButton.id = 'maho-zap-unzap';
      const index = idx + 1;
      let matchCount = 0;
      try {
        matchCount = selector === '' ? 0 : this.doc.querySelectorAll(selector).length;
      } catch (error) {
        if (!(error instanceof DOMException)) throw error;
        unzapButton.setAttribute('data-invalid-selector', 'true');
      }
      unzapButton.value = String(index);
      unzapButton.title = unzapButton.hasAttribute('data-invalid-selector')
        ? 'Invalid selector; remove this Zap to continue'
        : `${matchCount} element(s) hidden`;
      unzapButton.setAttribute('data-index', String(index));
      unzapButton.setAttribute('data-selector', selector);
      unzapButton.addEventListener('mouseenter', () => this.unzapButtonHover(unzapButton));
      unzapButton.addEventListener('mouseleave', () => this.unzapButtonUnhover(unzapButton));
      unzapButton.addEventListener('click', () => this.unzapButtonClick(unzapButton));
      zapList.appendChild(unzapButton);
    });

    if (!zapSelectors.length) {
      const helper = this.doc.createElement('p');
      helper.textContent = 'Click on any element to hide it';
      helper.classList.add('pcenter');
      zapList.appendChild(helper);
    } else {
      const helper = this.doc.createElement('p');
      helper.textContent = 'Click a number to unhide';
      zapList.appendChild(helper);
    }
  }

  private unzapButtonHover(button: HTMLInputElement): void {
    const selector = button.getAttribute('data-selector');
    if (!selector) return;
    this.boostContext.tempShowZappedElement(selector);
    button.value = '×';

    if (this.unzapHoverFrameId !== null) {
      this.win.cancelAnimationFrame(this.unzapHoverFrameId);
    }
    this.unzapHoverFrameId = this.win.requestAnimationFrame(() => {
      this.unzapHoverFrameId = null;
      try {
        const selection = this.doc.querySelectorAll(selector);
        if (selection.length) this.selectorComponent?.showHighlight(selection);
      } catch (error) {
        if (error instanceof DOMException) {
          this.boostContext.sendNotify('zap-selection-status', 'invalid_selector');
          return;
        }
        throw error;
      }
    });

    this.selectorComponent?.setState('selecting');
  }

  private unzapButtonUnhover(button: HTMLInputElement): void {
    button.value = button.getAttribute('data-index') || '';
    this.boostContext.tempHideZappedElement();
    this.selectorComponent?.removeHighlight();
  }

  private unzapButtonClick(button: HTMLInputElement): void {
    const selector = button.getAttribute('data-selector');
    if (!selector) return;

    this.boostContext.tempHideZappedElement();
    this.selectorComponent?.removeHighlight();
    this.selectorComponent?.setState('selecting');
    this.boostContext.removeZapSelector(selector);
    this.onZapUpdate();
  }

  tearDown(): void {
    if (this.unzapHoverFrameId !== null) {
      this.win.cancelAnimationFrame(this.unzapHoverFrameId);
      this.unzapHoverFrameId = null;
    }
    this.boostContext.tempHideZappedElement();
    this.selectorComponent?.tearDown();
    this.selectorComponent = null;

    if (this.zapDoneButton && this.onZapDoneClick) {
      this.zapDoneButton.removeEventListener('click', this.onZapDoneClick);
    }
    this.onZapDoneClick = null;
    this.zapDoneButton = null;

    if (this.container?.parentNode) {
      this.container.parentNode.removeChild(this.container);
    }
    this.container = null;
    this.shadowRoot = null;
    this.initialized = false;
  }

  handleEvent(event: Event, prevent: boolean): void {
    switch (event.type) {
      case 'click':
        this.handleClick(event);
        break;
      case 'mouseover':
        this.handleHoverDelegation(event);
        break;
      case 'mouseout':
        this.handleUnhoverDelegation(event);
        break;
    }
    this.selectorComponent?.handleEvent(event, prevent);
  }

  private handleClick(event: Event): void {
    const target = event.target;
    if (target instanceof HTMLInputElement && target.id === 'maho-zap-unzap') {
      this.unzapButtonClick(target);
    }
  }

  private handleHoverDelegation(event: Event): void {
    const target = event.target;
    if (target instanceof HTMLInputElement && target.id === 'maho-zap-unzap') {
      this.unzapButtonHover(target);
    }
  }

  private handleUnhoverDelegation(event: Event): void {
    const target = event.target;
    if (target instanceof HTMLInputElement && target.id === 'maho-zap-unzap') {
      this.unzapButtonUnhover(target);
    }
  }
}

interface BoostContext {
  disableZapMode(): void;
  addZapSelector(selector: string): void;
  removeZapSelector(selector: string): void;
  sendNotify(topic: string, msg?: string | null): void;
  getZapSelectors(): string[];
  tempShowZappedElement(selector: string): void;
  tempHideZappedElement(): void;
}

ns.ZapOverlay = ZapOverlay;

})();
