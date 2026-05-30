// Copyright 2026 Maho Browser. All rights reserved.
// Port of ZenBoostsChild.sys.mjs — main content script entry point.
// Injected into active tab's isolated world when zap or picker mode is on.

(function() {
'use strict';

type MahoBoostNS = {
  SelectorComponent: new (
    doc: Document,
    ids: string[],
    onSelect: (s: string) => void,
    labels?: { selectThis: string; selectRelated: string; cancel: string }
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
let zappedElementsTempShown: string[] = [];
let handleZapEventBound: ((e: Event) => void) | null = null;

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
  if ((event as KeyboardEvent).key === 'Escape') {
    if (currentMode === 'zap') disableZapMode();
    else if (currentMode === 'picker') disablePickerMode();
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
    ns.outbox.push({ type: 'zap_click', selector });
  },
  removeZapSelector(selector: string): void {
    ns.outbox.push({ type: 'unzap_click', selector });
  },
  sendNotify,
  getZapSelectors(): string[] {
    return ns.zapSelectors;
  },
  tempShowZappedElement(selector: string): void {
    try {
      document.querySelectorAll(selector).forEach((el) => {
        (el as HTMLElement).setAttribute('maho-zap-unhide', 'true');
      });
    } catch { /* invalid selector */ }
    if (!zappedElementsTempShown.includes(selector)) {
      zappedElementsTempShown.push(selector);
    }
  },
  tempHideZappedElement(): void {
    zappedElementsTempShown.forEach((selector) => {
      try {
        document.querySelectorAll(selector).forEach((el) => {
          (el as HTMLElement).removeAttribute('maho-zap-unhide');
        });
      } catch { /* invalid selector */ }
    });
    zappedElementsTempShown = [];
  },
};

function startZapOverlay(): void {
  if (currentMode === 'zap') return;
  currentMode = 'zap';
  ns.currentMode = 'zap';

  overlay = new ns.ZapOverlay(document, boostContext);
  overlay.initialize();

  addEventListeners();
  sendNotify('zap-state-update');
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
      sendNotify('selector-picker-picked', cssSelector);
    },
    { selectThis: 'Pick this', selectRelated: 'Related', cancel: 'Cancel' }
  );
  overlay.initialize();

  addEventListeners();
  sendNotify('selector-picker-state-update', 'onenable');
}

function disableZapMode(): void {
  if (currentMode === 'none') return;
  currentMode = 'none';
  ns.currentMode = 'none';

  if (overlay) {
    (overlay as ZapOverlayInstance).tearDown();
    overlay = null;
  }

  removeEventListeners();
  sendNotify('zap-state-update');
}

function disablePickerMode(): void {
  if (currentMode === 'none') return;
  currentMode = 'none';
  ns.currentMode = 'none';

  if (overlay) {
    (overlay as SelectorComponentInstance).tearDown();
    overlay = null;
  }

  removeEventListeners();
  sendNotify('selector-picker-state-update', 'ondisable');
}

ns.enterZapMode = function(zapSelectors: string[]): void {
  ns.zapSelectors = zapSelectors;
  if (currentMode === 'picker') disablePickerMode();
  startZapOverlay();
};

ns.exitZapMode = function(): void {
  if (currentMode === 'zap') disableZapMode();
};

ns.enterPickerMode = function(): void {
  if (currentMode === 'zap') disableZapMode();
  startPickerOverlay();
};

ns.exitPickerMode = function(): void {
  if (currentMode === 'picker') disablePickerMode();
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
  disableZapMode(): void;
  addZapSelector(selector: string): void;
  removeZapSelector(selector: string): void;
  sendNotify(topic: string, msg?: string | null): void;
  getZapSelectors(): string[];
  tempShowZappedElement(selector: string): void;
  tempHideZappedElement(): void;
}

})();
