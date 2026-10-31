// Copyright 2026 Maho Browser. All rights reserved.

(function() {
'use strict';

type SelectionStatusCode =
  | 'ready'
  | 'removed_target'
  | 'zero_match'
  | 'invalid_selector'
  | 'over_broad_selector'
  | 'no_safe_candidate';

type SelectionStatus = {
  readonly code: SelectionStatusCode;
  readonly message: string;
  readonly selector: string | null;
  readonly matchCount: number;
};

type MahoBoostNS = {
  SelectorComponent: typeof SelectorComponent;
  __mahoBoost: MahoBoostNS;
  __mahoBoostCSS: string;
};

const STATES = {
  SELECTING: 'selecting' as const,
  SELECTED: 'selected' as const,
};

const POLICIES = {
  EXACT: 'exact' as const,
  RELATED: 'related' as const,
};

type SelectorState = typeof STATES[keyof typeof STATES];
type MatchPolicy = typeof POLICIES[keyof typeof POLICIES];

type ValidSelection = {
  readonly kind: 'valid';
  readonly selector: string;
  readonly matchCount: number;
  readonly matches: readonly Element[];
};

type InvalidSelection = {
  readonly kind: 'invalid';
  readonly status: SelectionStatus;
};

type SelectionResult = ValidSelection | InvalidSelection;

const MAX_RELATED_MATCHES = 100;
const MAX_SELECTOR_VALUE_LENGTH = 160;
const MAX_STABLE_CLASSES = 4;
const INTERACTIVE_SELECTOR = [
  'button',
  'a[href]',
  'input',
  'select',
  'textarea',
  'summary',
  'label',
  '[role="button"]',
  '[role="link"]',
  '[role="checkbox"]',
  '[role="radio"]',
  '[role="menuitem"]',
  '[role="option"]',
  '[role="switch"]',
  '[role="tab"]',
].join(',');
const CONTENT_CONTAINER_SELECTOR = [
  'article',
  'section',
  'main',
  'nav',
  'header',
  'footer',
  'aside',
  'li',
  'figure',
  'form',
  'dialog',
  '[role="article"]',
  '[role="region"]',
  '[role="main"]',
  '[role="navigation"]',
  '[role="listitem"]',
].join(',');
const SEMANTIC_ATTRIBUTES = ['aria-label', 'name', 'role', 'href'] as const;
const SAFE_IDENTIFIER = /^[A-Za-z0-9_-]+$/;
const SAFE_ATTRIBUTE_NAME = /^[A-Za-z0-9_-]+$/;
const SAFE_ATTRIBUTE_VALUE = /^[A-Za-z0-9_.:/?=&%#+@!~*$;-]+$/;
const GENERATED_CLASS = /^(?:css|jsx|sc|emotion|styled|react|vue|svelte)[-_]/i;
const HASH_LIKE_TOKEN = /(?:^|[-_])[a-f0-9]{10,}(?:$|[-_])/i;
const COMPOUND_SELECTOR = /^(?:[a-z]+)?(?:#[A-Za-z0-9_-]+|\.[A-Za-z0-9_-]+|\[(?:data-[A-Za-z0-9_-]+|aria-label|name|role|href)="[A-Za-z0-9_.:/?=&%#+@!~*$;-]+"\])+$/;
// Zen-style positional compound: tag/id/classes plus one digits-only structural
// pseudo-class only (:nth-child(n)/:first-child/:last-child) — safe from CSS injection.
const POSITIONAL_COMPOUND = /^(?:[a-z][a-z0-9]*)?(?:#[A-Za-z0-9_-]+|\.[A-Za-z0-9_-]+)*(?::nth-child\(\d{1,5}\)|:first-child|:last-child)?$/;
const MAX_PATH_DEPTH = 12;

class SelectorInitializationError extends Error {
  override readonly name = 'SelectorInitializationError';

  constructor() {
    super('Selector component requires a document with an active window');
  }
}

function uniqueStrings(values: readonly string[]): string[] {
  return [...new Set(values)];
}

function isGrammarCompatible(selector: string): boolean {
  const compounds = selector.split('>').map((compound) => compound.trim());
  if (compounds.length < 1 || compounds.length > MAX_PATH_DEPTH) {
    return false;
  }
  return compounds.every(
    (compound) =>
      compound.length > 0 &&
      (COMPOUND_SELECTOR.test(compound) || POSITIONAL_COMPOUND.test(compound)),
  );
}

function statusFor(
  code: Exclude<SelectionStatusCode, 'ready'>,
  selector: string | null = null,
  matchCount = 0
): SelectionStatus {
  const messages: Record<Exclude<SelectionStatusCode, 'ready'>, string> = {
    removed_target: 'The target changed or was removed. Select it again.',
    zero_match: 'The selector no longer matches this target. Select it again.',
    invalid_selector: 'This page produced an invalid selector. Choose a different target.',
    over_broad_selector: 'This selector matches too many elements. Choose a more specific target.',
    no_safe_candidate: 'No stable selector is available. Choose an element with a stable id, data attribute, or accessible name.',
  };
  return { code, message: messages[code], selector, matchCount };
}

class SelectorComponent {
  static STATES = STATES;

  private readonly doc: Document;
  private readonly win: Window;
  private initialized = false;
  private shadowRoot: ShadowRoot | null = null;
  private container: HTMLDivElement | null = null;

  private currentState: SelectorState | null = null;
  private currentPolicy: MatchPolicy = POLICIES.EXACT;
  private relatedValueIndex = 1;
  private selectedElement: Element | null = null;
  private lastOverElement: Element | null = null;
  private pendingHoverElement: Element | null = null;
  private hoverFrameId: number | null = null;
  private positionFrameId: number | null = null;
  private exactSelection: SelectionResult = {
    kind: 'invalid',
    status: statusFor('no_safe_candidate'),
  };
  private relatedSelection: SelectionResult = {
    kind: 'invalid',
    status: statusFor('no_safe_candidate'),
  };

  private readonly contentIDs: readonly string[];
  private readonly onSelect: (selector: string) => void;
  private readonly onStatus: ((status: SelectionStatus) => void) | null;
  private readonly labels: {
    readonly selectThis: string;
    readonly selectRelated: string;
    readonly cancel: string;
  };

  safeAreaPadding = { left: 0, right: 0, top: 0, bottom: 0 };

  private hoverDiv: HTMLDivElement | null = null;
  private selectorComponentEl: HTMLDivElement | null = null;
  private selectThisButton: HTMLInputElement | null = null;
  private selectRelatedButton: HTMLInputElement | null = null;
  private cancelButton: HTMLInputElement | null = null;

  constructor(
    doc: Document,
    additionalContentIDs: readonly string[],
    onSelect: (selector: string) => void,
    labels?: { readonly selectThis: string; readonly selectRelated: string; readonly cancel: string },
    onStatus?: (status: SelectionStatus) => void
  ) {
    const win = doc.defaultView;
    if (!win) {
      throw new SelectorInitializationError();
    }
    this.doc = doc;
    this.win = win;
    this.onSelect = onSelect;
    this.onStatus = onStatus ?? null;
    this.labels = labels ?? {
      selectThis: 'Zap this',
      selectRelated: 'Related',
      cancel: 'Cancel',
    };
    this.contentIDs = [...additionalContentIDs, 'select-controls', 'select-controls-container'];
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

    const template = this.doc.createElement('template');
    template.innerHTML = `
      <div id="select-component" data-status="selecting">
        <div id="select-controls">
          <input type="button" id="select-this" value="${this.labels.selectThis}" disabled/>
          <input type="button" id="select-related" value="${this.labels.selectRelated}" disabled/>
          <input type="button" id="select-cancel" value="${this.labels.cancel}"/>
        </div>
        <div id="selector-preview">
          <p id="selector-element-preview-text" role="status" aria-live="polite"></p>
        </div>
      </div>
      <div id="hover-div"></div>
      <div id="highlight-container"></div>
      <div id="highlight-shadow" style="display:none;"></div>
    `;
    this.shadowRoot.appendChild(template.content.cloneNode(true));
    this.initializeElements();
    this.setState(STATES.SELECTING);
    this.initialized = true;
  }

  private getElementById(id: string): HTMLElement | null {
    return this.shadowRoot?.getElementById(id) ?? null;
  }

  private getInputById(id: string): HTMLInputElement | null {
    const element = this.getElementById(id);
    return element instanceof HTMLInputElement ? element : null;
  }

  private initializeElements(): void {
    const hoverDiv = this.getElementById('hover-div');
    this.hoverDiv = hoverDiv instanceof HTMLDivElement ? hoverDiv : null;
    const component = this.getElementById('select-component');
    this.selectorComponentEl = component instanceof HTMLDivElement ? component : null;

    this.cancelButton = this.getInputById('select-cancel');
    this.cancelButton?.addEventListener('click', () => this.cancelSelect());

    this.selectThisButton = this.getInputById('select-this');
    this.selectThisButton?.addEventListener('click', () => {
      this.handleSelectAction(POLICIES.EXACT);
    });

    this.selectRelatedButton = this.getInputById('select-related');
    this.selectRelatedButton?.addEventListener('click', () => {
      this.handleSelectAction(POLICIES.RELATED);
    });
    this.selectRelatedButton?.addEventListener('mouseenter', (event) => {
      this.currentPolicy = POLICIES.RELATED;
      this.updateRelatedSelectionForPointer(event);
    });
    this.selectRelatedButton?.addEventListener(
      'mousemove', (event) => this.updateRelatedSelectionForPointer(event));
    this.selectRelatedButton?.addEventListener('mouseleave', () => {
      this.currentPolicy = POLICIES.EXACT;
      this.updateHighlight();
      this.updatePathTextField();
    });
  }

  setState(newState: SelectorState, data: Element | null = null): void {
    this.currentState = newState;
    switch (newState) {
      case STATES.SELECTED:
        this.selectedElement = data;
        this.currentPolicy = POLICIES.EXACT;
        this.refreshSelections();
        this.hideHoverDiv();
        this.showSelectorComponent();
        this.updateHighlight();
        this.updatePathTextField();
        break;
      case STATES.SELECTING:
        this.selectedElement = null;
        this.currentPolicy = POLICIES.EXACT;
        this.showHoverDiv();
        this.hideSelectorComponent();
        this.removeHighlight();
        this.setConfirmEnabled(false, false);
        break;
    }
  }

  private refreshSelections(): void {
    this.exactSelection = this.findSelection(this.selectedElement, POLICIES.EXACT);
    this.relatedSelection = this.findRelatedSelection(
      this.selectedElement, this.relatedValueIndex);
    this.setConfirmEnabled(
      this.exactSelection.kind === 'valid',
      this.relatedSelection.kind === 'valid'
    );
  }

  private updateRelatedSelectionForPointer(event: MouseEvent): void {
    const button = this.selectRelatedButton;
    if (!button) return;
    const rect = button.getBoundingClientRect();
    const fraction = rect.width > 0
      ? this.clamp((event.clientX - rect.left) / rect.width, 0, 1)
      : 0;
    // Track the cursor: move the gray fill boundary of the related-scope bar so it
    // follows the pointer (the :hover gradient reads --related-elements-value).
    button.style.setProperty('--related-elements-value', `${fraction * 100}%`);
    this.relatedValueIndex = 1 + Math.round(fraction * 6);
    this.relatedSelection = this.findRelatedSelection(
      this.selectedElement, this.relatedValueIndex);
    if (button) button.disabled = this.relatedSelection.kind !== 'valid';
    this.updateHighlight();
    this.updatePathTextField();
  }

  private setConfirmEnabled(exactEnabled: boolean, relatedEnabled: boolean): void {
    if (this.selectThisButton) this.selectThisButton.disabled = !exactEnabled;
    if (this.selectRelatedButton) this.selectRelatedButton.disabled = !relatedEnabled;
  }

  private handleSelectAction(policy: MatchPolicy): void {
    const initialSelection = this.selectionFor(policy);
    const target = this.selectedElement;
    const level = this.clamp(Math.round(this.relatedValueIndex), 1, 7);
    const anchor = policy === POLICIES.RELATED && (level === 1 || level === 5) &&
        this.isPageTarget(target) && this.isPageTarget(target.parentElement)
      ? target.parentElement : target;
    const selection = initialSelection.kind === 'valid'
      ? this.validateCandidate(initialSelection.selector, anchor, policy)
      : policy === POLICIES.RELATED
        ? this.findRelatedSelection(target, this.relatedValueIndex)
        : this.findSelection(target, policy);

    if (selection.kind === 'invalid') {
      this.renderStatus(selection.status);
      this.setConfirmEnabled(
        policy === POLICIES.EXACT ? false : this.exactSelection.kind === 'valid',
        policy === POLICIES.RELATED ? false : this.relatedSelection.kind === 'valid'
      );
      return;
    }

    this.removeHighlight();
    this.resetHoverDiv();
    this.setState(STATES.SELECTING);
    this.onSelect(selection.selector);
  }

  private cancelSelect(): void {
    this.setState(STATES.SELECTING);
  }

  updateHighlight(): void {
    this.removeHighlight();
    const selection = this.selectionFor(this.currentPolicy);
    if (selection.kind === 'valid') {
      this.showHighlight(selection.matches);
    }
  }

  showHighlight(selection: NodeListOf<Element> | readonly Element[]): void {
    const container = this.getElementById('highlight-container');
    if (!container) return;
    container.style.display = 'initial';

    let counter = 0;
    for (const element of selection) {
      if (counter >= MAX_RELATED_MATCHES) break;
      counter += 1;
      const rect = element.getBoundingClientRect();
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
    container.replaceChildren();
    const shadow = this.getElementById('highlight-shadow');
    if (shadow) shadow.style.display = 'none';
  }

  private updatePathTextField(): void {
    const selection = this.selectionFor(this.currentPolicy);
    if (selection.kind === 'invalid') {
      this.renderStatus(selection.status);
      return;
    }
    const maxPathLength = 64;
    const shownSelector = selection.selector.substring(0, maxPathLength);
    this.renderStatus({
      code: 'ready',
      message: `${selection.matchCount} match${selection.matchCount === 1 ? '' : 'es'} · ${shownSelector}`,
      selector: selection.selector,
      matchCount: selection.matchCount,
    });
  }

  private renderStatus(status: SelectionStatus): void {
    const element = this.getElementById('selector-element-preview-text');
    if (element) {
      element.textContent = status.message;
      element.setAttribute('data-status', status.code);
      if (status.selector) element.setAttribute('data-selector', status.selector);
      else element.removeAttribute('data-selector');
    }
    this.selectorComponentEl?.setAttribute('data-status', status.code);
    this.onStatus?.(status);
  }

  tearDown(): void {
    if (this.hoverFrameId !== null) {
      this.win.cancelAnimationFrame(this.hoverFrameId);
      this.hoverFrameId = null;
    }
    if (this.positionFrameId !== null) {
      this.win.cancelAnimationFrame(this.positionFrameId);
      this.positionFrameId = null;
    }
    this.pendingHoverElement = null;
    this.lastOverElement = null;
    this.container?.remove();
    this.container = null;
    this.shadowRoot = null;
    this.hoverDiv = null;
    this.selectorComponentEl = null;
    this.selectThisButton = null;
    this.selectRelatedButton = null;
    this.cancelButton = null;
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
    const padding = 10;
    const top = this.clamp(
      bounds.bottom + distance,
      padding + this.safeAreaPadding.top,
      this.win.innerHeight - rect.height - padding - this.safeAreaPadding.bottom
    );
    const left = this.clamp(
      bounds.left + bounds.width / 2 - rect.width / 2,
      padding + this.safeAreaPadding.left,
      this.win.innerWidth - rect.width - padding - this.safeAreaPadding.right
    );

    this.selectorComponentEl.setAttribute('data-is-appearing', 'false');
    Object.assign(this.selectorComponentEl.style, {
      top: `${top}px`, left: `${left}px`,
    });
    const originX = this.clamp(bounds.left + bounds.width / 2 - left, 0, rect.width);
    const originY = this.clamp(bounds.bottom - top, 0, rect.height);
    this.selectorComponentEl.style.transformOrigin = `${originX}px ${originY}px`;

    if (this.positionFrameId !== null) {
      this.win.cancelAnimationFrame(this.positionFrameId);
    }
    this.positionFrameId = this.win.requestAnimationFrame(() => {
      this.positionFrameId = null;
      this.selectorComponentEl?.setAttribute('data-is-appearing', 'true');
    });
  }

  handleEvent(event: Event, prevent: boolean): void {
    const isMahoContent = this.isMahoEvent(event);

    switch (event.type) {
      case 'click':
        if (event instanceof MouseEvent) this.handleClick(event, isMahoContent);
        break;
      case 'keydown':
        if (!isMahoContent && event instanceof KeyboardEvent) this.handleKeyDown(event);
        break;
      case 'mousemove':
      case 'pointermove':
        if (event instanceof MouseEvent) this.handleMouseMove(event, isMahoContent);
        break;
      case 'scroll':
      case 'resize':
        this.handlePageChange();
        return;
    }

    if (!isMahoContent && prevent) {
      if (event.cancelable) event.preventDefault();
      event.stopImmediatePropagation();
    }
  }

  private isMahoEvent(event: Event): boolean {
    return event.composedPath().some((node) => {
      if (!(node instanceof Element)) return false;
      if (node.matches('[data-maho-boost-selector], [data-maho-boost-zap]')) return true;
      return node.id !== '' && this.contentIDs.includes(node.id);
    });
  }

  private handlePageChange(): void {
    if (this.currentState !== STATES.SELECTED) return;
    if (!this.isPageTarget(this.selectedElement)) {
      this.renderStatus(statusFor('removed_target'));
      this.setConfirmEnabled(false, false);
      this.removeHighlight();
      return;
    }
    this.updateHighlight();
    this.setSelectorComponentPosition();
  }

  private handleMouseMove(event: MouseEvent, isMahoContent: boolean): void {
    if (isMahoContent) {
      this.pendingHoverElement = null;
      this.hideHoverDiv();
      return;
    }
    if (this.currentState !== STATES.SELECTING) return;

    const target = this.normalizeEventTarget(event);
    if (target === this.lastOverElement || target === this.pendingHoverElement) return;
    this.pendingHoverElement = target;
    if (this.hoverFrameId !== null) return;

    this.hoverFrameId = this.win.requestAnimationFrame(() => {
      this.hoverFrameId = null;
      const nextTarget = this.pendingHoverElement;
      this.pendingHoverElement = null;
      if (nextTarget === this.lastOverElement) return;
      this.lastOverElement = nextTarget;
      if (!nextTarget) {
        this.hideHoverDiv();
        return;
      }
      this.showHoverDiv();
      this.updateHoverGeometry(nextTarget);
    });
  }

  private updateHoverGeometry(target: Element): void {
    if (!this.hoverDiv) return;
    const bounds = target.getBoundingClientRect();
    const padding = 5;
    Object.assign(this.hoverDiv.style, {
      top: `${bounds.top - padding}px`,
      left: `${bounds.left - padding}px`,
      width: `${bounds.width + padding * 2}px`,
      height: `${bounds.height + padding * 2}px`,
    });
  }

  private handleClick(event: MouseEvent, isMahoContent: boolean): void {
    if (this.currentState !== STATES.SELECTING || isMahoContent) return;
    const target = this.normalizeEventTarget(event);
    if (target) this.setState(STATES.SELECTED, target);
  }

  private handleKeyDown(event: KeyboardEvent): void {
    if (event.key !== 'Enter' || this.currentState !== STATES.SELECTED) return;
    if (event.cancelable) event.preventDefault();
    event.stopImmediatePropagation();
    this.handleSelectAction(this.currentPolicy);
  }

  private normalizeEventTarget(event: Event): Element | null {
    const pathElements = event.composedPath().filter(
      (node): node is Element => node instanceof Element && this.isPageTarget(node)
    );
    if (pathElements.length === 0) return null;
    if (event.composedPath().some(
      (node) => node instanceof Element &&
        node.matches('[data-maho-boost-selector], [data-maho-boost-zap]')
    )) return null;

    const interactive = pathElements.find((element) => element.matches(INTERACTIVE_SELECTOR));
    if (interactive) return interactive;

    const contentContainer = pathElements.find(
      (element) => element.matches(CONTENT_CONTAINER_SELECTOR) && this.hasStableTerm(element)
    );
    if (contentContainer) return contentContainer;

    return pathElements[0] ?? null;
  }

  private isPageTarget(element: Element | null): element is Element {
    if (!element) return false;
    return element !== this.doc.documentElement &&
      element !== this.doc.body &&
      element.ownerDocument === this.doc &&
      element.getRootNode() === this.doc &&
      element.isConnected &&
      !element.closest('[data-maho-boost-selector], [data-maho-boost-zap]');
  }

  private hasStableTerm(element: Element): boolean {
    return this.directCandidates(element, true).length > 0;
  }

  private clamp(value: number, min: number, max: number): number {
    return Math.min(Math.max(value, min), max);
  }

  getSelection(): NodeListOf<Element> | readonly Element[] {
    const selection = this.selectionFor(this.currentPolicy);
    return selection.kind === 'valid' ? selection.matches : [];
  }

  getSelectionPath(
    doc: Document,
    relatedValueIndex: number,
    selectedElement: Element | null
  ): string | null {
    if (doc !== this.doc) return null;
    const selection = relatedValueIndex === 0
      ? this.findSelection(selectedElement, POLICIES.EXACT)
      : this.findRelatedSelection(selectedElement, relatedValueIndex);
    return selection.kind === 'valid' ? selection.selector : null;
  }

  private selectionFor(policy: MatchPolicy): SelectionResult {
    return policy === POLICIES.EXACT ? this.exactSelection : this.relatedSelection;
  }

  private findSelection(target: Element | null, policy: MatchPolicy): SelectionResult {
    if (!this.isPageTarget(target)) {
      return { kind: 'invalid', status: statusFor('removed_target') };
    }

    const candidates = this.candidatesFor(target);
    if (candidates.length === 0) {
      return { kind: 'invalid', status: statusFor('no_safe_candidate') };
    }

    const failures = new Set<SelectionStatusCode>();
    for (const candidate of candidates) {
      const result = this.validateCandidate(candidate, target, policy);
      if (result.kind === 'valid') return result;
      failures.add(result.status.code);
    }

    const failureOrder: readonly Exclude<SelectionStatusCode, 'ready'>[] = [
      'invalid_selector',
      'zero_match',
      'over_broad_selector',
      'no_safe_candidate',
      'removed_target',
    ];
    const code = failureOrder.find((failure) => failures.has(failure)) ?? 'no_safe_candidate';
    return { kind: 'invalid', status: statusFor(code) };
  }

  private findRelatedSelection(
    target: Element | null,
    relatedValueIndex: number
  ): SelectionResult {
    if (!this.isPageTarget(target)) {
      return { kind: 'invalid', status: statusFor('removed_target') };
    }

    const candidates = this.relatedCandidatesFor(target, relatedValueIndex);
    // Levels 1 and 5 widen the scope to the parent itself, so the resulting
    // selector legitimately matches the parent rather than the hovered target.
    const level = this.clamp(Math.round(relatedValueIndex), 1, 7);
    const anchor = (level === 1 || level === 5) &&
        this.isPageTarget(target.parentElement)
      ? target.parentElement
      : target;
    const failures = new Set<SelectionStatusCode>();
    for (const candidate of candidates) {
      const result = this.validateCandidate(candidate, anchor, POLICIES.RELATED);
      if (result.kind === 'valid') return result;
      failures.add(result.status.code);
    }

    if (candidates.length === 0) {
      return this.findSelection(target, POLICIES.RELATED);
    }
    const failureOrder: readonly Exclude<SelectionStatusCode, 'ready'>[] = [
      'invalid_selector',
      'zero_match',
      'over_broad_selector',
      'no_safe_candidate',
      'removed_target',
    ];
    const code = failureOrder.find((failure) => failures.has(failure)) ??
      'no_safe_candidate';
    return { kind: 'invalid', status: statusFor(code) };
  }

  // Mirrors Zen's relation levels (ZenSelectorComponent.getSelectionPath cases
  // 1-7): each level widens the scope by dropping identification specificity,
  // so consecutive positions on the Related control must yield distinct scopes.
  //   1 exact parent path        2 element type + exact parent
  //   3 element type + parent type   4 element type
  //   5 parent type              6 any child of the exact parent
  //   7 element with similar classes
  private identify(element: Element, specificity: number): string {
    const tag = element.tagName.toLowerCase();
    if (specificity >= 2) return tag;
    const classes = [...element.classList].filter(
      (className) => this.isStableClass(className));
    if (specificity === 1) {
      return classes.length > 0
        ? `${tag}${classes.map((className) => `.${className}`).join('')}`
        : tag;
    }
    const id = this.isStableIdentifier(element.id) ? `#${element.id}` : '';
    const classTerm = classes.map((className) => `.${className}`).join('');
    return `${tag}${id}${classTerm}`;
  }

  private relatedCandidatesFor(target: Element, relatedValueIndex: number): string[] {
    const parent = this.isPageTarget(target.parentElement) ? target.parentElement : null;
    const exactParentPaths = parent
      ? [...this.directCandidates(parent, true), this.identify(parent, 0)]
      : [];

    let candidates: string[] = [];
    switch (this.clamp(Math.round(relatedValueIndex), 1, 7)) {
      case 1:
        candidates = exactParentPaths;
        break;
      case 2:
        candidates = exactParentPaths.map(
          (parentTerm) => `${parentTerm} > ${this.identify(target, 2)}`);
        break;
      case 3:
        candidates = parent
          ? [`${this.identify(parent, 2)} > ${this.identify(target, 2)}`]
          : [];
        break;
      case 4:
        candidates = [this.identify(target, 2)];
        break;
      case 5:
        candidates = parent ? [this.identify(parent, 2)] : [];
        break;
      case 6:
        candidates = exactParentPaths.map((parentTerm) => `${parentTerm} > *`);
        break;
      case 7:
        candidates = [this.identify(target, 1)];
        break;
    }
    return uniqueStrings(candidates.filter(isGrammarCompatible));
  }

  private validateCandidate(
    selector: string,
    target: Element | null,
    policy: MatchPolicy
  ): SelectionResult {
    if (!this.isPageTarget(target)) {
      return { kind: 'invalid', status: statusFor('removed_target', selector) };
    }
    if (!isGrammarCompatible(selector)) {
      return { kind: 'invalid', status: statusFor('invalid_selector', selector) };
    }

    let matches: readonly Element[];
    try {
      matches = [...this.doc.querySelectorAll(selector)];
    } catch (error) {
      if (error instanceof DOMException) {
        return { kind: 'invalid', status: statusFor('invalid_selector', selector) };
      }
      throw error;
    }

    if (matches.length === 0 || !matches.includes(target)) {
      return {
        kind: 'invalid',
        status: statusFor('zero_match', selector, matches.length),
      };
    }
    if (matches.length > MAX_RELATED_MATCHES ||
        (policy === POLICIES.EXACT && matches.length !== 1)) {
      return {
        kind: 'invalid',
        status: statusFor('over_broad_selector', selector, matches.length),
      };
    }
    if (policy === POLICIES.RELATED && matches.length < 2) {
      return {
        kind: 'invalid',
        status: statusFor('no_safe_candidate', selector, matches.length),
      };
    }
    return { kind: 'valid', selector, matchCount: matches.length, matches };
  }

  private candidatesFor(target: Element): readonly string[] {
    const direct = this.directCandidates(target, true);
    const parent = target.parentElement;
    const structural: string[] = [];
    if (this.isPageTarget(parent)) {
      const parentCandidates = this.directCandidates(parent, true).slice(0, 4);
      const childCandidates = this.directCandidates(target, true).slice(0, 4);
      for (const parentCandidate of parentCandidates) {
        for (const childCandidate of childCandidates) {
          const candidate = `${parentCandidate} > ${childCandidate}`;
          if (isGrammarCompatible(candidate)) structural.push(candidate);
        }
      }
    }
    const positional = this.buildPositionalPath(target);
    const fallback = positional ? [positional] : [];
    return uniqueStrings([...direct, ...structural, ...fallback]);
  }

  private buildPositionalPath(target: Element): string | null {
    const parts: string[] = [];
    let element: Element | null = target;
    while (element && parts.length < MAX_PATH_DEPTH) {
      const parent: Element | null = element.parentElement;
      let part = element.tagName.toLowerCase();
      if (parent) {
        const index = Array.prototype.indexOf.call(parent.children, element) + 1;
        if (index === 1) {
          part += ':first-child';
        } else if (index === parent.children.length) {
          part += ':last-child';
        } else {
          part += `:nth-child(${index})`;
        }
      }
      parts.unshift(part);
      const candidate = parts.join(' > ');
      try {
        const matches = this.doc.querySelectorAll(candidate);
        if (matches.length === 1 && matches[0] === target) {
          return candidate;
        }
      } catch (error) {
        if (!(error instanceof DOMException)) throw error;
        return null;
      }
      element = parent;
    }
    return null;
  }

  private directCandidates(element: Element, includeId: boolean): string[] {
    const candidates: string[] = [];
    const tag = element.tagName.toLowerCase();

    if (includeId && this.isStableIdentifier(element.id)) {
      candidates.push(`#${element.id}`);
    }

    const dataAttributes = [...element.attributes]
      .filter((attribute) => attribute.name.startsWith('data-'))
      .filter((attribute) => this.isStableAttribute(attribute.name, attribute.value))
      .sort((left, right) => left.name.localeCompare(right.name));
    for (const attribute of dataAttributes) {
      const term = `[${attribute.name}="${attribute.value}"]`;
      candidates.push(term, `${tag}${term}`);
    }

    for (const name of SEMANTIC_ATTRIBUTES) {
      const value = element.getAttribute(name);
      if (value === null || !this.isStableAttribute(name, value)) continue;
      const term = `[${name}="${value}"]`;
      candidates.push(term, `${tag}${term}`);
    }

    const classes = [...element.classList]
      .filter((className) => this.isStableClass(className))
      .slice(0, MAX_STABLE_CLASSES);
    for (let size = classes.length; size >= 1; size -= 1) {
      const classTerm = classes.slice(0, size).map((className) => `.${className}`).join('');
      candidates.push(`${tag}${classTerm}`, classTerm);
    }

    return uniqueStrings(candidates.filter(isGrammarCompatible));
  }

  private isStableIdentifier(value: string): boolean {
    return value.length > 0 &&
      value.length <= MAX_SELECTOR_VALUE_LENGTH &&
      SAFE_IDENTIFIER.test(value) &&
      !HASH_LIKE_TOKEN.test(value);
  }

  private isStableClass(value: string): boolean {
    return this.isStableIdentifier(value) && !GENERATED_CLASS.test(value);
  }

  private isStableAttribute(name: string, value: string): boolean {
    return name.length > 0 &&
      name.length <= MAX_SELECTOR_VALUE_LENGTH &&
      value.length > 0 &&
      value.length <= MAX_SELECTOR_VALUE_LENGTH &&
      SAFE_ATTRIBUTE_NAME.test(name) &&
      SAFE_ATTRIBUTE_VALUE.test(value) &&
      !HASH_LIKE_TOKEN.test(value);
  }
}

(window as unknown as MahoBoostNS).__mahoBoost =
  (window as unknown as MahoBoostNS).__mahoBoost || {} as MahoBoostNS;
(window as unknown as MahoBoostNS).__mahoBoost.SelectorComponent = SelectorComponent;

})();
