import React, {useCallback, useEffect, useLayoutEffect, useMemo, useRef, useSyncExternalStore} from 'react';
import {createRoot} from 'react-dom/client';

import {ThemePickerStore} from './store.js';
import type {ThemeSelection, ThemeMode, ZenStop, CanvasGeometry} from './types.js';
import {HARMONIES, HARMONY_MAX_DOTS, OPACITY_MIN, OPACITY_MAX} from './types.js';
import type {ColorHarmony} from './types.js';

const MODE_OPTIONS: Array<{mode: ThemeMode; icon: string; label: string; title: string}> = [
  {mode: 'sparkle', icon: '✦', label: 'Auto', title: 'Auto — gradient theme that adapts to system'},
  {mode: 'sun', icon: '☀', label: 'Light', title: 'Light — bright solid theme'},
  {mode: 'moon', icon: '🌙', label: 'Dark', title: 'Dark — dark solid theme'},
];

const SWATCHS = Array.from({length: 9}, (_, index) => index);

const HARMONIES_ORDER: ColorHarmony[] = [
  'analogous', 'complementary', 'singleAnalogous',
  'splitComplementary', 'triadic', 'floating',
];

const HARMONY_LABELS: Record<ColorHarmony, string> = {
  analogous: 'Analogous',
  complementary: 'Complementary',
  singleAnalogous: 'Single Analogous',
  splitComplementary: 'Split Complementary',
  triadic: 'Triadic',
  floating: 'Floating',
};

function harmonyTooltip(harmony: ColorHarmony, applicableCount: number): string {
  const label = HARMONY_LABELS[harmony];
  const angles = HARMONIES[harmony];
  const angleHint = angles.length === 0 ? 'free placement'
      : angles.map(a => `${a}\u00b0`).join(' / ');
  if (applicableCount <= 1) {
    return `Color harmony: ${label} (${angleHint}) · add or remove dots to switch`;
  }
  return `Color harmony: ${label} (${angleHint}) · click to cycle`;
}

function App({store}: {store: ThemePickerStore}) {
  const snapshot = useSyncExternalStore(
      listener => store.subscribe(listener), () => store.getSnapshot());

  return (
    <ThemePicker store={store} loading={snapshot.loading} />
  );
}

function ThemePicker({store, loading}: {store: ThemePickerStore; loading: boolean}) {
  const snapshot = useSyncExternalStore(
      listener => store.subscribe(listener), () => store.getSnapshot());
  const {selection} = snapshot;

  return (
    <main className="tp-root">
      <PreviewCanvas
        selection={selection}
        store={store}
        onSelectMode={mode => store.setMode(mode)}
      />
      <SwatchRow
        selectedPreset={selection.preset_index}
        onApplyPreset={index => store.applyPreset(index)}
        onStepPreset={delta => store.applyPreset(selection.preset_index + delta)}
      />
      <BottomRow
        opacity={selection.opacity}
        grain={selection.grain}
        onSetOpacity={value => store.setOpacity(value)}
        onSetGrain={value => store.setGrain(value)}
      />
      <Footer
        disabled={loading}
        onCancel={() => void store.cancelTheme()}
        onDone={() => void store.commitTheme()}
      />
    </main>
  );
}

function ModeChips(
    {selectedMode, onSelect}: {selectedMode: ThemeMode; onSelect: (mode: ThemeMode) => void}) {
  return (
    <div className="tp-mode-chips" data-maho-el="D2">
      {MODE_OPTIONS.map(option => (
        <button
          key={option.mode}
          type="button"
          className={option.mode === selectedMode ? 'tp-chip is-selected' : 'tp-chip'}
          data-maho-el={
            option.mode === 'sparkle' ? 'KP-5' : option.mode === 'sun' ? 'KP-6' : 'KP-7'}
          aria-label={option.title}
          aria-pressed={option.mode === selectedMode}
          title={option.title}
          onClick={e => { e.stopPropagation(); onSelect(option.mode); }}>
          <span className="tp-chip__icon" aria-hidden="true">{option.icon}</span>
          <span className="tp-chip__label" aria-hidden="true">{option.label}</span>
        </button>
      ))}
    </div>
  );
}

function CanvasActions(
    {selection, store}: {selection: ThemeSelection; store: ThemePickerStore}) {
  const dotCount = selection.stops.length;
  const applicableHarmonies = HARMONIES_ORDER.filter(
      h => HARMONIES[h].length + 1 === dotCount);
  const currentIdx = applicableHarmonies.indexOf(selection.harmony);
  const nextIdx = currentIdx === -1 ? 0
      : (currentIdx + 1) % Math.max(applicableHarmonies.length, 1);
  const nextHarmony = applicableHarmonies[nextIdx];
  const algoDisabled = applicableHarmonies.length <= 1;
  const max = HARMONY_MAX_DOTS[selection.harmony];
  const atMax = selection.stops.length >= max;
  const atMin = selection.stops.length <= 1;
  const lastIdx = selection.stops.length - 1;
  return (
    <div className="tp-canvas-actions">
      <button
        type="button"
        className="tp-dot-btn"
        disabled={atMax}
        aria-label="Add color stop"
        onClick={e => { e.stopPropagation(); store.addStop(); }}>
        +
      </button>
      <button
        type="button"
        className="tp-dot-btn"
        disabled={atMin}
        aria-label="Remove color stop"
        onClick={e => { e.stopPropagation(); store.removeStop(lastIdx); }}>
        −
      </button>
      <button
        type="button"
        className="tp-algo-toggle"
        disabled={algoDisabled}
        aria-label={harmonyTooltip(selection.harmony, applicableHarmonies.length)}
        title={harmonyTooltip(selection.harmony, applicableHarmonies.length)}
        onClick={e => { e.stopPropagation(); if (nextHarmony) store.setHarmony(nextHarmony); }}>
        <svg viewBox="0 0 16 16" fill="none" aria-hidden="true">
          <path d="M8 2.5 L2.5 12.5 L13.5 12.5 Z" stroke="currentColor" strokeWidth="1.2" fill="none"/>
          <circle cx="8" cy="2.5" r="1.7" fill="currentColor"/>
          <circle cx="2.5" cy="12.5" r="1.7" fill="currentColor"/>
          <circle cx="13.5" cy="12.5" r="1.7" fill="currentColor"/>
        </svg>
      </button>
    </div>
  );
}

function ZenDots(
    {selection, canvasRef, store}: {
      selection: ThemeSelection;
      canvasRef: React.RefObject<HTMLElement | null>;
      store: ThemePickerStore;
    }) {
  const geomRef = useRef<CanvasGeometry>({width: 380, height: 380, padding: 20});

  useLayoutEffect(() => {
    const node = canvasRef.current;
    if (!node) return;
    const rect = node.getBoundingClientRect();
    geomRef.current = {width: rect.width, height: rect.height, padding: 20};
  });

  return (
    <div className="tp-zen-dots" data-maho-el="D9">
      {selection.stops.map((stop, index) => (
        <ZenDot
          key={index}
          index={index}
          stop={stop}
          canvasRef={canvasRef}
          geomRef={geomRef}
          store={store}
        />
      ))}
    </div>
  );
}

function ZenDot(
    {index, stop, canvasRef, geomRef, store}: {
      index: number;
      stop: ZenStop;
      canvasRef: React.RefObject<HTMLElement | null>;
      geomRef: React.RefObject<CanvasGeometry>;
      store: ThemePickerStore;
    }) {
  const draggingRef = useRef(false);
  const indexRef = useRef(index);
  indexRef.current = index;

  // Zen parity: only primary is draggable. Secondaries are derivatives auto-positioned
  // by recomputeSecondaryPositions. See zen-gradient-generator.css:270 (`pointer-events: none`).
  const isDraggable = stop.isPrimary;

  const updateFromEvent = useCallback((event: PointerEvent) => {
    const node = canvasRef.current;
    if (!node) return;
    const rect = node.getBoundingClientRect();
    const pixelX = event.clientX - rect.left;
    const pixelY = event.clientY - rect.top;
    const geom: CanvasGeometry = {width: rect.width, height: rect.height, padding: 20};
    store.setStopPosition(indexRef.current, pixelX, pixelY, geom);
  }, [canvasRef, store]);

  useEffect(() => {
    const move = (event: PointerEvent) => {
      if (draggingRef.current) updateFromEvent(event);
    };
    const up = () => {
      draggingRef.current = false;
    };
    window.addEventListener('pointermove', move);
    window.addEventListener('pointerup', up);
    window.addEventListener('pointercancel', up);
    return () => {
      window.removeEventListener('pointermove', move);
      window.removeEventListener('pointerup', up);
      window.removeEventListener('pointercancel', up);
    };
  }, [updateFromEvent]);

  const handlePointerDown = useCallback((event: React.PointerEvent<HTMLSpanElement>) => {
    if (!isDraggable) return;
    event.stopPropagation();
    event.preventDefault();
    draggingRef.current = true;
    updateFromEvent(event.nativeEvent);
  }, [updateFromEvent, isDraggable]);

  const geom = geomRef.current;
  const canvasWidth = geom.width;
  const canvasHeight = geom.height;
  const xPercent = (stop.position.x / canvasWidth) * 100;
  const yPercent = (stop.position.y / canvasHeight) * 100;
  const hueDegrees = stop.hue * 360;
  const saturationPercent = Math.max(10, stop.saturation * 100);
  const lightnessPercent = stop.lightness;
  // Zen exact dot sizing — see zen-gradient-generator.css:257-291. (2x scale for Maho.)
  const size = stop.isPrimary ? 76 : 40;
  const borderWidth = stop.isPrimary ? 12 : 6;

  return (
    <span
      className={stop.isPrimary ? 'tp-zen-dot tp-zen-dot--primary' : 'tp-zen-dot'}
      onPointerDown={handlePointerDown}
      style={{
        left: `${xPercent}%`,
        top: `${yPercent}%`,
        width: `${size}px`,
        height: `${size}px`,
        borderWidth: `${borderWidth}px`,
        background: `hsl(${hueDegrees.toFixed(1)}, ${saturationPercent.toFixed(1)}%, ${lightnessPercent.toFixed(1)}%)`,
        pointerEvents: isDraggable ? 'auto' : 'none',
        cursor: isDraggable ? undefined : 'default',
      }}
    />
  );
}

// Zen exact wave constants (zen-browser/desktop@cd1616d1, ZenGradientGenerator.mjs:74-76).
// X range 51.373..367.037 in viewBox 0 -7.605 455 70 → thumb travels 11.29%..80.67% of shell.
const ZEN_LINE_PATH = 'M 51.373 27.395 L 367.037 27.395';
const ZEN_SINE_PATH = 'M 51.373 27.395 C 60.14 -8.503 68.906 -8.503 77.671 27.395 C 86.438 63.293 95.205 63.293 103.971 27.395 C 112.738 -8.503 121.504 -8.503 130.271 27.395 C 139.037 63.293 147.803 63.293 156.57 27.395 C 165.335 -8.503 174.101 -8.503 182.868 27.395 C 191.634 63.293 200.4 63.293 209.167 27.395 C 217.933 -8.503 226.7 -8.503 235.467 27.395 C 244.233 63.293 252.999 63.293 261.765 27.395 C 270.531 -8.503 279.297 -8.503 288.064 27.395 C 296.83 63.293 305.596 63.293 314.363 27.395 C 323.13 -8.503 331.896 -8.503 340.662 27.395 M 314.438 27.395 C 323.204 -8.503 331.97 -8.503 340.737 27.395 C 349.503 63.293 358.27 63.293 367.037 27.395';
const ZEN_REFERENCE_Y = 27.3;
const ZEN_THUMB_START_PCT = 0;
const ZEN_THUMB_END_PCT = 100;

interface ZenPathPoint {
  type: 'M' | 'L' | 'C';
  x1?: number; y1?: number;
  x2?: number; y2?: number;
  x: number; y: number;
}

function parseZenPath(pathStr: string): ZenPathPoint[] {
  const points: ZenPathPoint[] = [];
  const tokens = pathStr.match(/[MCL]\s*[\d\s.\-,]+/g);
  if (!tokens) return points;
  for (const token of tokens) {
    const type = token.charAt(0) as 'M' | 'L' | 'C';
    const nums = token.slice(1).trim().split(/[\s,]+/).map(Number);
    if (type === 'M' || type === 'L') {
      points.push({type, x: nums[0], y: nums[1]});
    } else if (type === 'C') {
      for (let i = 0; i < nums.length; i += 6) {
        points.push({
          type: 'C',
          x1: nums[i], y1: nums[i + 1],
          x2: nums[i + 2], y2: nums[i + 3],
          x: nums[i + 4], y: nums[i + 5],
        });
      }
    }
  }
  return points;
}

const ZEN_SINE_POINTS = parseZenPath(ZEN_SINE_PATH);

function interpolateWavePath(progress: number): string {
  const p = Math.max(0, Math.min(1, progress));
  const lerpY = (y: number) => ZEN_REFERENCE_Y + (y - ZEN_REFERENCE_Y) * p;
  return ZEN_SINE_POINTS.map(pt => {
    if (pt.type === 'M') return `M ${pt.x} ${lerpY(pt.y).toFixed(3)}`;
    if (pt.type === 'L') return `L ${pt.x} ${lerpY(pt.y).toFixed(3)}`;
    return `C ${pt.x1} ${lerpY(pt.y1!).toFixed(3)} ${pt.x2} ${lerpY(pt.y2!).toFixed(3)} ${pt.x} ${lerpY(pt.y).toFixed(3)}`;
  }).join(' ');
}

function PreviewCanvas(
    {
      selection,
      store,
      onSelectMode,
    }: {
      selection: ThemeSelection;
      store: ThemePickerStore;
      onSelectMode: (mode: ThemeMode) => void;
    }) {
  const canvasRef = useRef<HTMLElement | null>(null);

  const handleCanvasClick = useCallback((e: React.MouseEvent<HTMLElement>) => {
    const target = e.target as HTMLElement;
    if (target.closest('.tp-canvas-actions') ||
        target.closest('.tp-mode-chips') ||
        target.closest('.tp-zen-dot')) {
      return;
    }
    const node = canvasRef.current;
    if (!node) return;
    const rect = node.getBoundingClientRect();
    const pixelX = e.clientX - rect.left;
    const pixelY = e.clientY - rect.top;
    store.addStop({x: pixelX, y: pixelY});
  }, [store]);

  return (
    <section
      ref={canvasRef}
      className="tp-canvas tp-canvas--zen"
      data-maho-el="D1 KP-3"
      onClick={handleCanvasClick}>
      <ModeChips selectedMode={selection.mode} onSelect={onSelectMode} />
      <CanvasActions selection={selection} store={store} />
      <ZenDots selection={selection} canvasRef={canvasRef} store={store} />
    </section>
  );
}

function SwatchRow(
    {
      selectedPreset,
      onApplyPreset,
      onStepPreset,
    }: {
      selectedPreset: number;
      onApplyPreset: (index: number) => void;
      onStepPreset: (delta: number) => void;
    }) {
  return (
    <section className="tp-swatches-row" data-maho-el="D4 KP-11">
      <button
        type="button"
        className="tp-chevron tp-chevron--left"
        aria-label="Previous swatch"
        onClick={() => onStepPreset(-1)}>
        <span className="tp-chevron__icon" aria-hidden="true">‹</span>
      </button>
      <div className="tp-swatches" role="list" aria-label="Theme swatches">
        {SWATCHS.map(index => (
          <button
            key={index}
            type="button"
            role="listitem"
            aria-label={`Theme preset ${index + 1}`}
            aria-pressed={selectedPreset === index}
            className={selectedPreset === index ? `tp-swatch tp-swatch--${index} is-selected` : `tp-swatch tp-swatch--${index}`}
            onClick={() => onApplyPreset(index)}>
          </button>
        ))}
      </div>
      <button
        type="button"
        className="tp-chevron tp-chevron--right"
        aria-label="Next swatch"
        data-maho-el="D5"
        onClick={() => onStepPreset(1)}>
        <span className="tp-chevron__icon" aria-hidden="true">›</span>
      </button>
    </section>
  );
}

function BottomRow(
    {opacity, grain, onSetOpacity, onSetGrain}: {
      opacity: number;
      grain: number;
      onSetOpacity: (value: number) => void;
      onSetGrain: (value: number) => void;
    }) {
  const waveRef = useRef<HTMLDivElement | null>(null);
  const dialRef = useRef<HTMLButtonElement | null>(null);
  const waveDraggingRef = useRef(false);
  const dialDraggingRef = useRef(false);
  const opacityCallbackRef = useRef(onSetOpacity);
  opacityCallbackRef.current = onSetOpacity;
  const grainCallbackRef = useRef(onSetGrain);
  grainCallbackRef.current = onSetGrain;

  const updateFromWaveClient = useCallback((clientX: number) => {
    const node = waveRef.current;
    if (!node) return;
    const rect = node.getBoundingClientRect();
    const xPercent = ((clientX - rect.left) / rect.width) * 100;
    const ratio = Math.max(0, Math.min(1,
        (xPercent - ZEN_THUMB_START_PCT) / (ZEN_THUMB_END_PCT - ZEN_THUMB_START_PCT)));
    const value = OPACITY_MIN + ratio * (OPACITY_MAX - OPACITY_MIN);
    opacityCallbackRef.current(value);
  }, []);

  const updateFromDialClient = useCallback((clientX: number, clientY: number) => {
    const node = dialRef.current;
    if (!node) return;
    const rect = node.getBoundingClientRect();
    const cx = rect.left + rect.width / 2;
    const cy = rect.top + rect.height / 2;
    const dx = clientX - cx;
    const dy = clientY - cy;
    let angleDeg = Math.atan2(dx, -dy) * 180 / Math.PI;
    if (angleDeg < 0) angleDeg += 360;
    grainCallbackRef.current(Math.round((angleDeg / 360) * 100));
  }, []);

  useEffect(() => {
    const move = (event: PointerEvent) => {
      if (waveDraggingRef.current) updateFromWaveClient(event.clientX);
      if (dialDraggingRef.current) updateFromDialClient(event.clientX, event.clientY);
    };
    const up = () => {
      waveDraggingRef.current = false;
      dialDraggingRef.current = false;
    };
    window.addEventListener('pointermove', move);
    window.addEventListener('pointerup', up);
    window.addEventListener('pointercancel', up);
    return () => {
      window.removeEventListener('pointermove', move);
      window.removeEventListener('pointerup', up);
      window.removeEventListener('pointercancel', up);
    };
  }, [updateFromWaveClient, updateFromDialClient]);

  const handleWaveDown = useCallback((event: React.PointerEvent<HTMLDivElement>) => {
    waveDraggingRef.current = true;
    updateFromWaveClient(event.clientX);
  }, [updateFromWaveClient]);

  const handleDialDown = useCallback((event: React.PointerEvent<HTMLButtonElement>) => {
    dialDraggingRef.current = true;
    updateFromDialClient(event.clientX, event.clientY);
  }, [updateFromDialClient]);

  const opacityNormalized = (opacity - OPACITY_MIN) / (OPACITY_MAX - OPACITY_MIN);
  const thumbLeftPercent = ZEN_THUMB_START_PCT + opacityNormalized * (ZEN_THUMB_END_PCT - ZEN_THUMB_START_PCT);
  const wavePath = interpolateWavePath(opacityNormalized);
  const dialRotation = (Math.max(0, Math.min(100, grain)) / 100) * 360;

  const dialRadius = 36;
  const activeStep = grain === 0 ? -1 : Math.round((grain / 100) * 16) % 16;

  const thumbW = 10 + opacityNormalized * 15;
  const thumbH = 40 + opacityNormalized * 15;

  return (
    <section className="tp-bottom-row">
      <div
        ref={waveRef}
        className="tp-wave-shell"
        onPointerDown={handleWaveDown}>
        <svg
          className="tp-wave"
          data-maho-el="D6"
          viewBox="51.373 -7.605 315.664 70"
          preserveAspectRatio="none"
          fill="none"
          xmlns="http://www.w3.org/2000/svg"
          aria-hidden="true">
          <path className="tp-wave__path tp-wave__path--back" d={wavePath} />
          <path className="tp-wave__path tp-wave__path--front" d={wavePath} />
        </svg>
        <span
          className="tp-wave-thumb"
          aria-hidden="true"
          style={{
            '--tp-thumb-w': `${thumbW}px`,
            '--tp-thumb-h': `${thumbH}px`,
            left: `${thumbLeftPercent}%`,
          } as React.CSSProperties}
        />
      </div>
      <button
        type="button"
        ref={dialRef}
        className="tp-dial"
        data-maho-el="D7"
        aria-label="Adjust grain"
        onPointerDown={handleDialDown}>
        <div className="tp-dial-ticks">
          {Array.from({length: 16}).map((_, i) => (
            <span
              key={i}
              className={`tp-dial-tick ${i === activeStep ? 'is-active' : ''}`}
              style={{transform: `rotate(${i * 22.5}deg) translateY(-${dialRadius}px)`}}
            />
          ))}
        </div>
        <span
          className={`tp-dial__indicator ${grain === 0 ? 'is-hidden' : ''}`}
          data-maho-el="KP-10"
          style={{transform: `rotate(${dialRotation}deg)`}}
        />
      </button>
    </section>
  );
}

function Footer(
    {
      disabled,
      onCancel,
      onDone,
    }: {
      disabled: boolean;
      onCancel: () => void;
      onDone: () => void;
    }) {
  return (
    <footer className="tp-footer" data-maho-el="D8">
      <button type="button" className="tp-cancel-btn" disabled={disabled} onClick={onCancel}>
        Cancel
      </button>
      <button type="button" className="tp-done-btn" disabled={disabled} onClick={onDone}>
        Done
      </button>
    </footer>
  );
}

const rootElement = document.getElementById('root');

if (!rootElement) {
  throw new Error('Missing #root for maho_space_create.');
}

const store = new ThemePickerStore();
createRoot(rootElement).render(<App store={store} />);
