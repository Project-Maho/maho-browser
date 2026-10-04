// Copyright 2026 Maho Browser. All rights reserved.
// Port of ZenBoostsChild.sys.mjs — main content script entry point.
// Injected into active tab's isolated world when zap or picker mode is on.

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
  ZapOverlay: new (doc: Document, ctx: BoostContext) => ZapOverlayInstance;
  __mahoBoost: MahoBoostNS;
  __mahoBoostCSS?: string;
  outbox: OutboxMessage[];
  zapSelectors: string[];
  currentMode: Mode;
  enterZapMode(zapSelectors: string[]): void;
  exitZapMode(): void;
  enterPickerMode(): void;
  exitPickerMode(): void;
  drain(): OutboxMessage[];
  setZapSelectors(selectors: string[]): void;
};

interface SelectorComponentInstance {
  initialize(): void;
  tearDown(): void;
  handleEvent(event: Event, prevent: boolean): void;
  setState(state: string, data?: Element | null): void;
  showHighlight(selection: NodeListOf<Element> | Element[]): void;
  removeHighlight(): void;
  safeAreaPadding: { left: number; right: number; top: number; bottom: number };
}

interface ZapOverlayInstance {
  initialize(): void;
  tearDown(): void;
  handleEvent(event: Event, prevent: boolean): void;
  onZapUpdate(): void;
}

interface OutboxMessage {
  type: string;
  selector?: string;
  msg?: string | null;
}

interface SelectionStatus {
  readonly code: string;
  readonly message: string;
  readonly selector: string | null;
  readonly matchCount: number;
}

type Mode = 'none' | 'zap' | 'picker';

const OVERLAY_EVENTS = [
  'click', 'pointerdown', 'pointermove', 'pointerup', 'scroll', 'resize',
] as const;

const PREVENTABLE_EVENTS = [
  'click', 'pointerdown', 'pointermove', 'pointerup',
  'mousemove', 'mousedown', 'mouseup', 'mouseenter', 'mouseover',
  'mouseout', 'mouseleave', 'touchstart', 'touchmove', 'touchend',
  'dblclick', 'auxclick', 'keypress', 'contextmenu',
  'pointerenter', 'pointerover', 'pointerout', 'pointerleave',
] as const;

const ALL_EVENTS_SET = new Set<string>([...OVERLAY_EVENTS, ...PREVENTABLE_EVENTS]);
const PREVENTABLE_SET = new Set<string>([...PREVENTABLE_EVENTS]);

const ns = ((window as unknown as MahoBoostNS).__mahoBoost ||
  {});
(window as unknown as { __mahoBoost: MahoBoostNS }).__mahoBoost = ns;

let currentMode: Mode = 'none';
let overlay: ZapOverlayInstance | SelectorComponentInstance | null = null;
let preventableEventsAdded = false;
let zappedElementsTempShown = new Set<string>();
let handleZapEventBound: ((e: Event) => void) | null = null;

const ZAP_UNHIDE_ATTRIBUTE = 'maho-zap-unhide';

ns.outbox = ns.outbox || [];
ns.zapSelectors = ns.zapSelectors || [];
ns.currentMode = 'none';

function sendNotify(topic: string, msg: string | null = null): void {
  ns.outbox.push({ type: 'notify', selector: topic, msg });
}

function handleZapEvent(event: Event): void {
  if (ALL_EVENTS_SET.has(event.type) && overlay) {
    (overlay as ZapOverlayInstance).handleEvent(
      event, PREVENTABLE_SET.has(event.type)
    );
  }
}

function handleKeyDown(event: Event): void {
  if (!(event instanceof KeyboardEvent)) return;
  const keyEvent = event;
  if (keyEvent.key === 'Escape') {
    if (keyEvent.cancelable) keyEvent.preventDefault();
    keyEvent.stopImmediatePropagation();
    if (currentMode === 'zap') disableZapMode();
    else if (currentMode === 'picker') disablePickerMode();
  } else if (keyEvent.key === 'Enter' && overlay) {
    (overlay as ZapOverlayInstance).handleEvent(keyEvent, true);
  }
}

function addEventListeners(): void {
  handleZapEventBound = handleZapEvent;

  for (const evt of OVERLAY_EVENTS) {
    document.addEventListener(evt, handleZapEventBound, true);
  }
  for (const evt of PREVENTABLE_EVENTS) {
    document.addEventListener(evt, handleZapEventBound, true);
  }
  document.addEventListener('keydown', handleKeyDown, true);
  preventableEventsAdded = true;
}

function removeEventListeners(): void {
  if (!handleZapEventBound) return;
  for (const evt of OVERLAY_EVENTS) {
    document.removeEventListener(evt, handleZapEventBound, true);
  }
  if (preventableEventsAdded) {
    for (const evt of PREVENTABLE_EVENTS) {
      document.removeEventListener(evt, handleZapEventBound, true);
    }
  }
  document.removeEventListener('keydown', handleKeyDown, true);
  preventableEventsAdded = false;
  handleZapEventBound = null;
}

const boostContext = {
  disableZapMode,
  addZapSelector(selector: string): void {
    if (selector === '' || ns.zapSelectors.includes(selector)) {
      return;
    }
    ns.zapSelectors.push(selector);
    ns.outbox.push({ type: 'zap_click', selector });
  },
  removeZapSelector(selector: string): void {
    const selectorIndex = ns.zapSelectors.indexOf(selector);
    if (selectorIndex === -1) {
      return;
    }
    ns.zapSelectors.splice(selectorIndex, 1);
    ns.outbox.push({ type: 'unzap_click', selector });
  },
  sendNotify,
  getZapSelectors(): string[] {
    return ns.zapSelectors;
  },
  tempShowZappedElement(selector: string): void {
    try {
      document.querySelectorAll(selector).forEach((el) => {
        el.setAttribute(ZAP_UNHIDE_ATTRIBUTE, 'true');
      });
    } catch (error) {
      if (error instanceof DOMException) {
        sendNotify('zap-selection-status', 'invalid_selector');
        return;
      }
      throw error;
    }
    zappedElementsTempShown.add(selector);
  },
  tempHideZappedElement(): void {
    for (const selector of zappedElementsTempShown) {
      try {
        document.querySelectorAll(selector).forEach((el) => {
          el.removeAttribute(ZAP_UNHIDE_ATTRIBUTE);
        });
      } catch (error) {
        if (!(error instanceof DOMException)) throw error;
      }
    }
    zappedElementsTempShown = new Set<string>();
  },
};

function startZapOverlay(): void {
  if (currentMode === 'zap') return;
  currentMode = 'zap';
  ns.currentMode = 'zap';

  overlay = new ns.ZapOverlay(document, boostContext);
  overlay.initialize();

  addEventListeners();
  sendNotify('zap-state-update', 'onenable');
}

function startPickerOverlay(): void {
  if (currentMode === 'picker') return;
  currentMode = 'picker';
  ns.currentMode = 'picker';

  overlay = new ns.SelectorComponent(
    document,
    [],
    (cssSelector: string) => {
      ns.outbox.push({ type: 'picker_selected', selector: cssSelector });
      disablePickerMode();
    },
    { selectThis: 'Pick this', selectRelated: 'Related', cancel: 'Cancel' },
    (status: SelectionStatus) => {
      if (status.code !== 'ready') {
        sendNotify('selector-picker-selection-status', status.code);
      }
    }
  );
  overlay.initialize();

  addEventListeners();
  sendNotify('selector-picker-state-update', 'onenable');
}

function disableZapMode(notifyHost: boolean = true): void {
  if (currentMode === 'none') return;
  boostContext.tempHideZappedElement();
  currentMode = 'none';
  ns.currentMode = 'none';

  if (overlay) {
    (overlay as ZapOverlayInstance).tearDown();
    overlay = null;
  }

  removeEventListeners();
  if (notifyHost) {
    sendNotify('zap-state-update', 'ondisable');
  }
}

function disablePickerMode(notifyHost: boolean = true): void {
  if (currentMode === 'none') return;
  boostContext.tempHideZappedElement();
  currentMode = 'none';
  ns.currentMode = 'none';

  if (overlay) {
    (overlay as SelectorComponentInstance).tearDown();
    overlay = null;
  }

  removeEventListeners();
  if (notifyHost) {
    sendNotify('selector-picker-state-update', 'ondisable');
  }
}

ns.enterZapMode = function(zapSelectors: string[]): void {
  ns.zapSelectors = zapSelectors;
  if (currentMode === 'picker') disablePickerMode(false);
  startZapOverlay();
};

ns.exitZapMode = function(): void {
  if (currentMode === 'zap') disableZapMode(false);
};

ns.enterPickerMode = function(): void {
  if (currentMode === 'zap') disableZapMode(false);
  startPickerOverlay();
};

ns.exitPickerMode = function(): void {
  if (currentMode === 'picker') disablePickerMode(false);
};

ns.drain = function(): OutboxMessage[] {
  const msgs = ns.outbox.splice(0);
  return msgs;
};

ns.setZapSelectors = function(selectors: string[]): void {
  ns.zapSelectors = selectors;
  if (currentMode === 'zap' && overlay) {
    (overlay as ZapOverlayInstance).onZapUpdate();
  }
};

interface BoostContext {
  disableZapMode(notifyHost?: boolean): void;
  addZapSelector(selector: string): void;
  removeZapSelector(selector: string): void;
  sendNotify(topic: string, msg?: string | null): void;
  getZapSelectors(): string[];
  tempShowZappedElement(selector: string): void;
  tempHideZappedElement(): void;
}

})();
