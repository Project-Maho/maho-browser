import '@testing-library/jest-dom/vitest';

import {cleanup} from '@testing-library/react';
import i18next from 'i18next';
import {initReactI18next} from 'react-i18next';
import {afterEach, vi} from 'vitest';

import en from '../i18n/locales/en.json';

// Tests assert on rendered English copy, so translations must resolve. The
// app's own i18n module is not reused here because importing it triggers a
// preference load over the Mojo bridge at module scope.
i18next.use(initReactI18next).init({
  resources: {en: {translation: en}},
  lng: 'en',
  fallbackLng: 'en',
  interpolation: {escapeValue: false},
});

afterEach(() => {
  cleanup();
  vi.clearAllMocks();
  // The mail client persists a list snapshot; one test must not seed the next.
  localStorage.clear();
});

// jsdom implements neither of these, and the mail tree calls both during
// layout: the virtualized thread list measures with ResizeObserver, and
// responsive components query matchMedia.
class ResizeObserverStub implements ResizeObserver {
  observe(): void {}
  unobserve(): void {}
  disconnect(): void {}
}

if (typeof window !== 'undefined') {
  globalThis.ResizeObserver ??= ResizeObserverStub;

  if (!window.matchMedia) {
    window.matchMedia = (query: string): MediaQueryList => ({
      matches: false,
      media: query,
      onchange: null,
      addListener: () => {},
      removeListener: () => {},
      addEventListener: () => {},
      removeEventListener: () => {},
      dispatchEvent: () => false,
    });
  }

  if (!Element.prototype.scrollIntoView) {
    Element.prototype.scrollIntoView = () => {};
  }

  Element.prototype.setPointerCapture ??= () => {};
  Element.prototype.releasePointerCapture ??= () => {};
  Element.prototype.hasPointerCapture ??= () => false;

  // jsdom ships no PointerEvent, so fireEvent.pointer* silently degrades to an
  // event type that drops clientX and pointerId - swipe and drag assertions then
  // read NaN offsets. MouseEvent already carries the coordinate fields.
  if (typeof globalThis.PointerEvent === 'undefined') {
    class PointerEventPolyfill extends MouseEvent {
      readonly pointerId: number;
      readonly pointerType: string;
      readonly isPrimary: boolean;

      constructor(type: string, params: PointerEventInit = {}) {
        super(type, params);
        this.pointerId = params.pointerId ?? 0;
        this.pointerType = params.pointerType ?? 'mouse';
        this.isPrimary = params.isPrimary ?? true;
      }
    }
    globalThis.PointerEvent =
        PointerEventPolyfill as unknown as typeof globalThis.PointerEvent;
  }
}
