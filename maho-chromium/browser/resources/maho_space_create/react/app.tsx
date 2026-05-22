import React, {useEffect, useRef, useSyncExternalStore} from 'react';
import {createRoot} from 'react-dom/client';

import {getDialLevel, ThemePickerStore} from './store.js';
import type {ThemeMode} from './types.js';

const MODE_OPTIONS: Array<{mode: ThemeMode; icon: string; label: string}> = [
  {mode: 'sparkle', icon: '✦', label: 'Sparkle mode'},
  {mode: 'sun', icon: '☀', label: 'Sun mode'},
  {mode: 'moon', icon: '🌙', label: 'Moon mode'},
];

const SWATCHS = Array.from({length: 9}, (_, index) => index);

function App({store}: {store: ThemePickerStore}) {
  const snapshot = useSyncExternalStore(
      listener => store.subscribe(listener), () => store.getSnapshot());
  const bootstrapped = useRef(false);

  useEffect(() => {
    if (!bootstrapped.current) {
      bootstrapped.current = true;
    }
  }, []);

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
      <ModeChips selectedMode={selection.mode} onSelect={mode => store.setMode(mode)} />
      <PreviewCanvas
        onDecreaseBrightness={() => store.setBrightness(selection.brightness - 10)}
        onIncreaseBrightness={() => store.setBrightness(selection.brightness + 10)}
        onTapCanvas={() => store.previewNextPreset()}
      />
      <SwatchRow
        selectedPreset={selection.preset_index}
        onSelectPreset={index => store.setPreset(index)}
        onStepPreset={delta => store.setPreset(selection.preset_index + delta)}
      />
      <BottomRow
        dialLevel={getDialLevel(selection)}
        onDialPress={() => store.setBrightness((selection.brightness + 12) % 101)}
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
          aria-label={option.label}
          aria-pressed={option.mode === selectedMode}
          onClick={() => onSelect(option.mode)}>
          <span className="tp-chip__icon" aria-hidden="true">{option.icon}</span>
        </button>
      ))}
    </div>
  );
}

function PreviewCanvas(
    {
      onDecreaseBrightness,
      onIncreaseBrightness,
      onTapCanvas,
    }: {
      onDecreaseBrightness: () => void;
      onIncreaseBrightness: () => void;
      onTapCanvas: () => void;
    }) {
  return (
    <section className="tp-canvas" data-maho-el="D1 KP-3" onClick={onTapCanvas}>
      <div className="tp-glow tp-glow--top" aria-hidden="true"></div>
      <div className="tp-glow tp-glow--bottom" aria-hidden="true"></div>
      <p className="tp-prompt">Tap to pick a color for this space</p>
      <div className="tp-brightness-row" data-maho-el="D3">
        <button
          type="button"
          className="tp-brightness-btn"
          aria-label="Decrease brightness"
          onClick={event => {
            event.stopPropagation();
            onDecreaseBrightness();
          }}>
          <span className="tp-brightness-symbol" aria-hidden="true">−</span>
        </button>
        <button
          type="button"
          className="tp-brightness-btn"
          aria-label="Increase brightness"
          onClick={event => {
            event.stopPropagation();
            onIncreaseBrightness();
          }}>
          <span className="tp-brightness-symbol" aria-hidden="true">+</span>
        </button>
      </div>
    </section>
  );
}

function SwatchRow(
    {
      selectedPreset,
      onSelectPreset,
      onStepPreset,
    }: {
      selectedPreset: number;
      onSelectPreset: (index: number) => void;
      onStepPreset: (delta: number) => void;
    }) {
  return (
    <section className="tp-swatches-row" data-maho-el="D4 KP-11">
      <div className="tp-chevron-group" data-maho-el="D5">
        <button
          type="button"
          className="tp-chevron"
          aria-label="Previous swatch"
          onClick={() => onStepPreset(-1)}>
          <span className="tp-chevron__icon" aria-hidden="true">‹</span>
        </button>
      </div>
      <div className="tp-swatches" role="list" aria-label="Theme swatches">
        {SWATCHS.map(index => (
          <button
            key={index}
            type="button"
            role="listitem"
            aria-label={`Theme preset ${index + 1}`}
            aria-pressed={selectedPreset === index}
            className={selectedPreset === index ? `tp-swatch tp-swatch--${index} is-selected` : `tp-swatch tp-swatch--${index}`}
            onClick={() => onSelectPreset(index)}>
          </button>
        ))}
      </div>
      <div className="tp-chevron-group">
        <button
          type="button"
          className="tp-chevron"
          aria-label="Next swatch"
          onClick={() => onStepPreset(1)}>
          <span className="tp-chevron__icon" aria-hidden="true">›</span>
        </button>
      </div>
    </section>
  );
}

function BottomRow(
    {dialLevel, onDialPress}: {dialLevel: number; onDialPress: () => void}) {
  return (
    <section className="tp-bottom-row">
      <svg
        className="tp-wave"
        data-maho-el="D6"
        viewBox="0 0 320 76"
        fill="none"
        xmlns="http://www.w3.org/2000/svg"
        aria-hidden="true">
        <path className="tp-wave__path" d="M6 39C31 17 57 17 82 39C107 61 133 61 158 39C183 17 209 17 234 39C259 61 285 61 314 39" />
      </svg>
      <button type="button" className="tp-dial" data-maho-el="D7" aria-label="Adjust brightness dial" onClick={onDialPress}>
        <span className={`tp-dial__indicator is-level-${dialLevel}`} data-maho-el="KP-10"></span>
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
