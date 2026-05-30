// Copyright 2026 Maho Browser. All rights reserved.
// Port of ZenSelectorComponent.sys.mjs (787 lines) — CSS selector heuristic.
// Verbatim logic; adapted from Firefox JSWindowActor to standalone DOM class.

(function() {
'use strict';

type MahoBoostNS = {
  SelectorComponent: typeof SelectorComponent;
  __mahoBoost: MahoBoostNS;
  __mahoBoostCSS: string;
};

const STATES = {
  SELECTING: 'selecting' as const,
  SELECTED: 'selected' as const,
};

type SelectorState = typeof STATES[keyof typeof STATES];

class SelectorComponent {
  static STATES = STATES;

  private doc: Document;
  private win: Window;
  private initialized = false;
  private shadowRoot: ShadowRoot | null = null;
  private container: HTMLDivElement | null = null;

  private currentState: SelectorState | null = null;
  private selectedElement: Element | null = null;
  private lastOverElement: EventTarget | null = null;
  private relatedValueIndex = 0;

  private contentIDs: string[] = [];
  private onSelect: ((selector: string) => void) | null = null;
  private labels = { selectThis: 'Zap this', selectRelated: 'Related', cancel: 'Cancel' };

  safeAreaPadding = { left: 0, right: 0, top: 0, bottom: 0 };

  private hoverDiv: HTMLDivElement | null = null;
  private selectorComponentEl: HTMLDivElement | null = null;
  private selectThisButton: HTMLInputElement | null = null;
  private selectRelatedSlider: HTMLInputElement | null = null;
  private cancelButton: HTMLInputElement | null = null;

  constructor(
    doc: Document,
    additionalContentIDs: string[],
    onSelect: (selector: string) => void,
    labels?: { selectThis: string; selectRelated: string; cancel: string }
  ) {
    this.doc = doc;
    this.win = doc.defaultView!;
    this.onSelect = onSelect;
    if (labels) { this.labels = labels; }

    const baseSelectorIDs = ['select-controls', 'select-controls-container'];
    this.contentIDs = [...additionalContentIDs, ...baseSelectorIDs];
  }

  initialize(): void {
    if (this.initialized) return;

    this.container = this.doc.createElement('div');
    this.container.setAttribute('data-maho-boost-selector', 'true');
    this.container.style.cssText =
      'position:fixed;top:0;left:0;width:0;height:0;z-index:2147483640;pointer-events:none;';
    this.doc.documentElement.appendChild(this.container);
    this.shadowRoot = this.container.attachShadow({ mode: 'closed' });

    const style = this.doc.createElement('style');
    style.textContent = (this.win as unknown as MahoBoostNS).__mahoBoostCSS || '';
    this.shadowRoot.appendChild(style);

    const tpl = this.doc.createElement('template');
    tpl.innerHTML = `
      <div id="select-component">
        <div id="select-controls">
          <input type="button" id="select-this" value="${this.labels.selectThis}"/>
          <input type="button" id="select-related" value="${this.labels.selectRelated}"/>
          <input type="button" id="select-cancel" value="${this.labels.cancel}"/>
        </div>
        <div id="selector-preview">
          <p id="selector-element-preview-text"></p>
        </div>
      </div>
      <div id="hover-div"></div>
      <div id="highlight-container"></div>
      <div id="highlight-shadow" style="display:none;"></div>
    `;
    this.shadowRoot.appendChild(tpl.content.cloneNode(true));
    this.initializeElements();
    this.setState(STATES.SELECTING);
    this.initialized = true;
  }

  private getElementById(id: string): HTMLElement | null {
    return this.shadowRoot?.getElementById(id) ?? null;
  }

  private initializeElements(): void {
    this.hoverDiv = this.getElementById('hover-div') as HTMLDivElement;
    this.selectorComponentEl = this.getElementById('select-component') as HTMLDivElement;

    this.cancelButton = this.getElementById('select-cancel') as HTMLInputElement;
    this.cancelButton?.addEventListener('click', () => this.cancelSelect());

    this.selectThisButton = this.getElementById('select-this') as HTMLInputElement;
    this.selectThisButton?.addEventListener('click', () => this.handleSelectAction());

    this.selectRelatedSlider = this.getElementById('select-related') as HTMLInputElement;
    this.selectRelatedSlider?.addEventListener('click', () => this.handleSelectAction());

    this.selectRelatedSlider?.addEventListener('mousemove', (e: MouseEvent) => {
      const r = (e.currentTarget as HTMLElement).getBoundingClientRect();
      let value = (e.clientX - r.left) / r.width;
      value = Math.max(0, Math.min(1, value));
      (e.currentTarget as HTMLElement).style.setProperty(
        '--related-elements-value', `${value * 100}%`
      );
      const lastIndex = this.relatedValueIndex;
      this.relatedValueIndex = Math.round(value * 8);
      if (lastIndex !== this.relatedValueIndex) {
        this.updateHighlight();
        this.updatePathTextField();
      }
    });

    this.selectRelatedSlider?.addEventListener('mouseout', (e: MouseEvent) => {
      (e.currentTarget as HTMLElement).style.removeProperty('--related-elements-value');
      this.relatedValueIndex = 0;
      this.updateHighlight();
      this.updatePathTextField();
    });
  }

  setState(newState: SelectorState, data: Element | null = null): void {
    this.currentState = newState;
    switch (newState) {
      case STATES.SELECTED:
        this.selectedElement = data;
        this.relatedValueIndex = 0;
        this.hideHoverDiv();
        this.showSelectorComponent();
        this.updateHighlight();
        this.updatePathTextField();
        break;
      case STATES.SELECTING:
        this.selectedElement = null;
        this.showHoverDiv();
        this.hideSelectorComponent();
        this.removeHighlight();
        break;
    }
  }

  private handleSelectAction(): void {
    const cssPath = this.getSelectionPath(
      this.doc, this.relatedValueIndex, this.selectedElement
    );
    this.removeHighlight();
    this.resetHoverDiv();

    this.win.requestAnimationFrame(() => {
      this.setState(STATES.SELECTING);
      if (cssPath && this.onSelect) {
        this.onSelect(cssPath);
      }
    });
  }

  private cancelSelect(): void {
    this.setState(STATES.SELECTING);
  }

  updateHighlight(): void {
    this.removeHighlight();
    this.showHighlight(this.getSelection());
  }

  showHighlight(selection: NodeListOf<Element> | Element[]): void {
    const container = this.getElementById('highlight-container');
    if (!container) return;
    container.style.display = 'initial';

    let counter = 0;
    for (const element of selection) {
      if (counter >= 100) break;
      counter++;
      const rect = element.getBoundingClientRect();
      if (!rect) continue;

      const padding = 5;
      const div = this.doc.createElement('div');
      div.classList.add('highlight');
      Object.assign(div.style, {
        position: 'fixed',
        left: `${rect.left - padding}px`,
        top: `${rect.top - padding}px`,
        width: `${rect.width + padding * 2}px`,
        height: `${rect.height + padding * 2}px`,
      });
      container.appendChild(div);
    }

    const shadow = this.getElementById('highlight-shadow');
    if (shadow) shadow.style.display = 'initial';
  }

  removeHighlight(): void {
    const container = this.getElementById('highlight-container');
    if (!container) return;
    container.style.display = 'none';
    container.innerHTML = '';
    const shadow = this.getElementById('highlight-shadow');
    if (shadow) shadow.style.display = 'none';
  }

  private updatePathTextField(): void {
    const maxPathLength = 64;
    const selection = this.getSelection();
    const selectionPath = this.getSelectionPath(
      this.doc, this.relatedValueIndex, this.selectedElement
    );
    if (!selectionPath) return;

    const text = `<b>[${selection.length}]</b> ${selectionPath.substring(0, Math.min(maxPathLength, selectionPath.length))}`;
    const el = this.getElementById('selector-element-preview-text');
    if (el) el.innerHTML = text;
  }

  tearDown(): void {
    if (this.container && this.container.parentNode) {
      this.container.parentNode.removeChild(this.container);
    }
    this.container = null;
    this.shadowRoot = null;
    this.initialized = false;
  }

  private hideHoverDiv(): void {
    if (this.hoverDiv) this.hoverDiv.style.display = 'none';
  }

  private showHoverDiv(): void {
    if (this.hoverDiv) this.hoverDiv.style.display = 'initial';
  }

  private resetHoverDiv(): void {
    if (!this.hoverDiv) return;
    Object.assign(this.hoverDiv.style, {
      top: '0px', left: '0px', width: '0px', height: '0px',
    });
  }

  private hideSelectorComponent(): void {
    if (!this.selectorComponentEl) return;
    this.selectorComponentEl.style.visibility = 'hidden';
    this.selectorComponentEl.setAttribute('data-is-appearing', 'false');
  }

  private showSelectorComponent(): void {
    if (!this.selectorComponentEl) return;
    this.selectorComponentEl.style.visibility = 'visible';
    this.setSelectorComponentPosition();
  }

  private setSelectorComponentPosition(): void {
    if (!this.selectedElement || !this.selectorComponentEl) return;

    const bounds = this.selectedElement.getBoundingClientRect();
    const distance = 8;
    const rect = this.selectorComponentEl.getBoundingClientRect();
    const compW = rect.width;
    const compH = rect.height;
    const winW = this.win.innerWidth;
    const winH = this.win.innerHeight;
    const pad = 10;

    this.selectorComponentEl.setAttribute('data-is-appearing', 'false');

    const top = this.clamp(
      bounds.top + bounds.height + distance,
      pad + this.safeAreaPadding.top,
      winH - compH - pad - this.safeAreaPadding.bottom
    );
    const left = this.clamp(
      bounds.left + bounds.width / 2 - compW / 2,
      pad + this.safeAreaPadding.left,
      winW - compW - pad - this.safeAreaPadding.right
    );

    Object.assign(this.selectorComponentEl.style, {
      top: `${top}px`, left: `${left}px`,
    });

    const targetCenterX = bounds.left + bounds.width / 2;
    const targetBottomY = bounds.top + bounds.height;
    const originX = this.clamp(targetCenterX - left, 0, compW);
    const originY = this.clamp(targetBottomY - top, 0, compH);
    this.selectorComponentEl.style.transformOrigin = `${originX}px ${originY}px`;

    this.win.requestAnimationFrame(() => {
      this.selectorComponentEl?.setAttribute('data-is-appearing', 'true');
    });
  }

  handleEvent(event: Event, prevent: boolean): void {
    let isMahoContent = false;
    const target = event.target as HTMLElement;
    if (target?.closest) {
      const closestDiv = target.closest('div');
      const closestID = closestDiv?.id ?? '';
      isMahoContent = this.contentIDs.includes(closestID);
    }

    switch (event.type) {
      case 'click':
        this.handleClick(event as MouseEvent, isMahoContent);
        break;
      case 'mousemove':
        this.handleMouseMove(event as MouseEvent, isMahoContent);
        break;
      case 'scroll':
      case 'resize':
        this.handlePageChange();
        return;
    }

    if (isMahoContent) return;

    if (prevent) {
      if (!['contextmenu', 'pointerdown'].includes(event.type)) {
        event.preventDefault();
      }
      event.stopImmediatePropagation();
    }
  }

  private handlePageChange(): void {
    if (this.currentState !== STATES.SELECTED) return;
    this.updateHighlight();
    this.setSelectorComponentPosition();
  }

  private handleMouseMove(event: MouseEvent, isMahoContent: boolean): void {
    if (this.lastOverElement === event.target) return;
    if (!isMahoContent) {
      this.lastOverElement = event.target;
      if (this.currentState === STATES.SELECTING) {
        this.showHoverDiv();
      }
    } else {
      this.hideHoverDiv();
    }

    if (this.currentState !== STATES.SELECTING || !event.target) return;

    const bounds = (event.target as Element).getBoundingClientRect();
    const padding = 5;
    if (this.hoverDiv) {
      Object.assign(this.hoverDiv.style, {
        top: `${bounds.top - padding}px`,
        left: `${bounds.left - padding}px`,
        width: `${bounds.width + padding * 2}px`,
        height: `${bounds.height + padding * 2}px`,
      });
    }
  }

  private handleClick(event: MouseEvent, isMahoContent: boolean): void {
    const target = event.target as Element;
    if (
      target === this.doc.documentElement ||
      target === this.doc.body ||
      !this.doc.documentElement.contains(target)
    ) return;

    if (this.currentState === STATES.SELECTING && !isMahoContent) {
      this.setState(STATES.SELECTED, target);
    }
  }

  private clamp(x: number, min: number, max: number): number {
    return Math.min(Math.max(x, min), max);
  }

  getSelection(): NodeListOf<Element> | Element[] {
    const selector = this.getSelectionPath(
      this.doc, this.relatedValueIndex, this.selectedElement
    );
    if (!selector) return [];
    try { return this.doc.querySelectorAll(selector); }
    catch { return []; }
  }

  getSelectionPath(
    doc: Document,
    relatedValueIndex: number,
    selectedElement: Element | null
  ): string | null {
    const path: string[] = [];
    const cssescape = (str: string) => CSS.escape(str);

    const isValidNode = (element: Element | null): boolean => {
      if (!element) return false;
      const tag = element.tagName.toLowerCase();
      return tag !== 'body' && tag !== 'html';
    };

    const nthChild = (element: Element | null): string => {
      if (!element || !element.parentNode) return '';
      const parent = element.parentNode as Element;
      const children = parent.children;
      const index = Array.prototype.indexOf.call(children, element) + 1;
      if (index === 1) return ':first-child';
      if (index === children.length) return ':last-child';
      return `:nth-child(${index})`;
    };

    const getIdentification = (element: Element | null, specificity = 0): string => {
      if (!element) return '';
      const id = specificity < 2 && element.id ? `#${cssescape(element.id)}` : '';
      const cls = specificity < 1 && element.classList.length
        ? '.' + [...element.classList].map(c => cssescape(c)).join('.')
        : '';
      const tag = element.tagName ? element.tagName.toLowerCase() : '';
      return `${tag}${id}${cls}`;
    };

    const traverse = (element: Element | null, specificity = 0, pathArray: string[]): void => {
      let current = element;
      if (!isValidNode(current)) return;

      pathArray.push(nthChild(current));
      pathArray.push(getIdentification(current, specificity));

      if (current && current.parentNode) {
        pathArray.push(' > ');
        pathArray.push(getIdentification(current.parentNode as Element, 0));
        const tempBuild = build(pathArray);

        while (
          tempBuild &&
          doc.querySelectorAll(tempBuild).length > 1
        ) {
          const parent: ParentNode | null | undefined = current?.parentNode;
          if (!(parent instanceof Element) || !isValidNode(parent)) break;
          current = parent;
          pathArray.push(' > ');
          const grandparent: ParentNode | null = current.parentNode;
          if (!(grandparent instanceof Element)) break;
          pathArray.push(nthChild(grandparent));
          pathArray.push(getIdentification(grandparent, 0));
        }
      }
    };

    const build = (pathArray: string[]): string => [...pathArray].reverse().join('');

    const findBestExactSelector = (element: Element, targetDoc: Document): string | null => {
      const buildMap = new Map<number, string>();

      const pathExact: string[] = [];
      traverse(element, 0, pathExact);
      const pathExactBuilt = build(pathExact);
      const pathExactLen = pathExactBuilt
        ? targetDoc.querySelectorAll(pathExactBuilt).length : 0;
      buildMap.set(pathExactLen, pathExactBuilt);

      const pathType: string[] = [];
      pathType.push(getIdentification(element, 2));
      if (isValidNode(element.parentNode as Element)) {
        pathType.push(' > ');
        traverse(element.parentNode as Element, 0, pathType);
      }
      const pathTypeBuilt = build(pathType);
      const pathTypeLen = pathTypeBuilt
        ? targetDoc.querySelectorAll(pathTypeBuilt).length : 0;
      if (!buildMap.has(pathTypeLen)) buildMap.set(pathTypeLen, pathTypeBuilt);

      const pathParent: string[] = [];
      if (isValidNode(element.parentNode as Element)) {
        traverse(element.parentNode as Element, 0, pathParent);
      }
      const pathParentBuilt = build(pathParent);
      const pathParentLen = pathParentBuilt
        ? targetDoc.querySelectorAll(pathParentBuilt).length : 0;
      if (!buildMap.has(pathParentLen)) buildMap.set(pathParentLen, pathParentBuilt);

      let smallestLen = Number.MAX_VALUE;
      let smallestPath: string | null = null;
      buildMap.forEach((p, len) => {
        if (len < smallestLen) {
          smallestLen = len;
          smallestPath = p;
        }
      });
      return smallestPath;
    };

    if (!isValidNode(selectedElement)) return null;

    switch (relatedValueIndex) {
      default:
      case 0:
        return findBestExactSelector(selectedElement!, doc);
      case 1:
        if (!isValidNode(selectedElement!.parentNode as Element)) return null;
        traverse(selectedElement!.parentNode as Element, 0, path);
        break;
      case 2:
        path.push(getIdentification(selectedElement, 2));
        if (isValidNode(selectedElement!.parentNode as Element)) {
          path.push(' > ');
          path.push(getIdentification(selectedElement!.parentNode as Element, 0));
        }
        break;
      case 3:
        path.push(getIdentification(selectedElement, 2));
        if (isValidNode(selectedElement!.parentNode as Element)) {
          path.push(' > ');
          path.push(getIdentification(selectedElement!.parentNode as Element, 2));
        }
        break;
      case 4:
        path.push(getIdentification(selectedElement, 2));
        break;
      case 5:
        if (!isValidNode(selectedElement!.parentNode as Element)) return null;
        path.push(getIdentification(selectedElement!.parentNode as Element, 2));
        break;
      case 6:
        if (!isValidNode(selectedElement!.parentNode as Element)) return null;
        path.push('*');
        path.push(' > ');
        traverse(selectedElement!.parentNode as Element, 0, path);
        break;
      case 7:
        path.push(getIdentification(selectedElement, 1));
        break;
    }

    return build(path);
  }
}

(window as unknown as MahoBoostNS).__mahoBoost =
  (window as unknown as MahoBoostNS).__mahoBoost || {} as MahoBoostNS;
(window as unknown as MahoBoostNS).__mahoBoost.SelectorComponent = SelectorComponent;

})();
