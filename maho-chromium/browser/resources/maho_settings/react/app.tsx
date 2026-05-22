import React, {
  useCallback,
  useEffect,
  useMemo,
  useRef,
  useState,
  useSyncExternalStore,
} from 'react';
import {createRoot} from 'react-dom/client';

import type {SettingValue} from '../mojo.js';
import type {AccentColor, PaneDefinition, SettingMetadata} from '../models.js';
import {DOMAIN_DEFINITIONS} from '../schema/domains.js';
import {
  LIVE_PANE_SECTION_MANIFEST,
  PANE_DEFINITION_MAP,
  PANE_DEFINITIONS,
} from '../schema/panes.js';
import {
  ACCENT_COLORS,
  SETTING_METADATA,
} from '../schema/setting_schema.js';
import {Button, TextArea, TextInput, joinClassNames} from '../../maho_common/react/primitives/index.js';
import {DomainPaneContent} from './domain_panes.js';
import {MahoSettingsStore} from './store.js';

const LIVE_SETTINGS_PANE_KEYS = new Set(['notifications','tabs','advanced','privacy','general','appearance']);

function cloneSetting(setting: SettingValue): SettingValue {
  return {
    ...setting,
    key: setting.key,
    value: setting.value,
  };
}

function isMigratedLivePane(pane: PaneDefinition): boolean {
  return LIVE_SETTINGS_PANE_KEYS.has(pane.key);
}

function groupSettings(settings: SettingValue[]): Record<string, SettingValue[]> {
  const grouped: Record<string, SettingValue[]> = {};
  for (const setting of settings) {
    const category = setting.key.split('.')[0] || 'general';
    if (!grouped[category]) {
      grouped[category] = [];
    }
    grouped[category]!.push(setting);
  }
  return grouped;
}

function selectSettings(settings: SettingValue[], keys: string[]): SettingValue[] {
  const byKey = new Map(settings.map(setting => [setting.key, setting]));
  return keys.map(key => byKey.get(key))
      .filter((setting): setting is SettingValue => !!setting)
      .map(setting => cloneSetting(setting));
}

function findMissingSettingKeys(settings: SettingValue[], keys: string[]): string[] {
  const present = new Set(settings.map(setting => setting.key));
  return keys.filter(key => !present.has(key));
}

function getSettingMetadata(item: SettingValue): SettingMetadata | null {
  return SETTING_METADATA[item.key] ?? null;
}

function normalizeInputValue(
    value: string, fallback: string, meta: SettingMetadata): string {
  if (meta.control !== 'number') {
    const trimmed = value.trim();
    return trimmed || fallback;
  }

  const parsed = Number.parseInt(value, 10);
  if (Number.isNaN(parsed)) {
    return fallback;
  }

  const min = meta.min ?? parsed;
  const max = meta.max ?? parsed;
  return String(Math.min(max, Math.max(min, parsed)));
}

function parseAccentColor(value: string): {r: number; g: number; b: number} | null {
  try {
    if (!value) {
      return null;
    }
    const parsed = JSON.parse(value) as AccentColor;
    return {r: parsed.r, g: parsed.g, b: parsed.b};
  } catch {
    return null;
  }
}

function SettingsApp({store}: {store: MahoSettingsStore}) {
  const state = useSyncExternalStore(
      listener => store.subscribe(listener), () => store.getSnapshot());

  useEffect(() => {
    void store.bootstrap();
  }, [store]);

  const currentPane = useMemo(() => {
    const pane = PANE_DEFINITION_MAP[state.currentPaneKey];
    if (pane && pane.kind !== 'hidden') {
      return pane;
    }
    return PANE_DEFINITIONS[0]!;
  }, [state.currentPaneKey]);

  const panesByDomain = useMemo(() => {
    const grouped = new Map<string, PaneDefinition[]>();
    for (const pane of PANE_DEFINITIONS) {
      const list = grouped.get(pane.domain) || [];
      list.push(pane);
      grouped.set(pane.domain, list);
    }
    return grouped;
  }, []);

  return (
    <div className="preferences-shell">
      <div className="preferences-window">
        <div className="preferences-workspace">
          <aside className="preferences-sidebar">
            <div className="preferences-sidebar__scroll">
              <section className="sidebar-brand-card">
                <h1 className="sidebar-title">Settings</h1>
              </section>
              <nav className="sidebar-navigation" aria-label="Settings panes">
                {DOMAIN_DEFINITIONS.map(domain => {
                  const domainPanes = panesByDomain.get(domain.id);
                  if (!domainPanes || domainPanes.length === 0) {
                    return null;
                  }

                  return (
                    <React.Fragment key={domain.id}>
                      <div className="sidebar-domain-label">{domain.label}</div>
                      {domainPanes.map(pane => (
                        <PaneButton
                          key={pane.key}
                          isActive={pane.key === currentPane.key}
                          pane={pane}
                          onSelect={() => store.selectPane(pane.key)}
                        />
                      ))}
                    </React.Fragment>
                  );
                })}
              </nav>
            </div>
          </aside>
          <main className="preferences-content">
            <div className="preferences-content__scroll">
              {currentPane.kind === 'embed' ? (
                <EmbedPane pane={currentPane} />
              ) : isMigratedLivePane(currentPane) ? (
                <LivePaneSection
                  bootstrapError={state.bootstrapError}
                  lastActionError={state.lastActionError}
                  pane={currentPane}
                  settings={state.settings}
                  store={store}
                />
              ) : (
                <DomainPaneContent pane={currentPane} settings={state.settings} store={store} />
              )}
            </div>
          </main>
        </div>
      </div>
    </div>
  );
}

function EmbedPane({pane}: {pane: PaneDefinition}) {
  const embedUrl = pane.embedUrl || 'chrome://settings/';

  return (
    <section className="preferences-section" data-pane={pane.key} id={`section-${pane.key}`}>
      <div className="preferences-card preferences-card--pane-shell preferences-card--browser">
        <header className="preferences-section__header">
          <h2 className="preferences-section__title">{pane.title}</h2>
        </header>
        <div className="cards-grid">
          <div className="browser-settings-shell browser-settings-shell--wide">
            <iframe
              className="browser-settings-webview"
              role="document"
              src={embedUrl}
            />
          </div>
        </div>
      </div>
    </section>
  );
}

function PaneButton(
    {isActive, pane, onSelect}: {
      isActive: boolean;
      pane: PaneDefinition;
      onSelect: () => void;
    }) {
  return (
    <button
      aria-current={isActive ? 'page' : undefined}
      className={joinClassNames('sidebar-nav-button', isActive ? 'is-active' : undefined)}
      data-pane={pane.key}
      type="button"
      onClick={onSelect}>
      <span className="sidebar-nav-button__text">
        <span className="sidebar-nav-button__label">{pane.navTitle}</span>
      </span>
    </button>
  );
}

function LivePaneSection(
    {bootstrapError, lastActionError, pane, settings, store}: {
      bootstrapError: string | null;
      lastActionError: string | null;
      pane: PaneDefinition;
      settings: SettingValue[];
      store: MahoSettingsStore;
    }) {
  const grouped = useMemo(() => groupSettings(settings), [settings]);
  const relevantSettings = useMemo(() => {
    return (pane.groupKeys || [pane.key]).flatMap(groupKey => grouped[groupKey] || []);
  }, [grouped, pane.groupKeys, pane.key]);

  const cards = useMemo(() => {
    const sections = LIVE_PANE_SECTION_MANIFEST[pane.key];
    if (!sections) {
      return [{
        description: '',
        missingSettingKeys: [],
        settings: relevantSettings,
        title: pane.title,
        wide: false,
      }];
    }

    return sections.map(section => ({
      description: section.card.description,
      missingSettingKeys: findMissingSettingKeys(relevantSettings, section.card.settingKeys),
      settings: selectSettings(relevantSettings, section.card.settingKeys),
      title: section.card.title,
      wide: !!section.card.wide,
    }));
  }, [pane.key, pane.title, relevantSettings]);

  return (
    <section className="preferences-section" data-pane={pane.key} id={`section-${pane.key}`}>
      <div className="preferences-card preferences-card--pane-shell">
        <header className="preferences-section__header">
          <h2 className="preferences-section__title">{pane.title}</h2>
          {bootstrapError ? <p className="preferences-section__description">{bootstrapError}</p> : null}
          {!bootstrapError && lastActionError ? (
            <p className="preferences-section__description">{lastActionError}</p>
          ) : null}
        </header>
        <div className="cards-grid">
          {cards.map(card => (
            <ContentCard
              key={`${pane.key}-${card.title}`}
              description={card.description}
              missingSettingKeys={card.missingSettingKeys}
              settings={card.settings}
              store={store}
              title={card.title}
              wide={card.wide}
            />
          ))}
        </div>
      </div>
    </section>
  );
}

function ContentCard(
    {description, missingSettingKeys, settings, store, title, wide}: {
      description: string;
      missingSettingKeys: string[];
      settings: SettingValue[];
      store: MahoSettingsStore;
      title: string;
      wide: boolean;
    }) {
  return (
    <article className={joinClassNames('content-card', wide ? 'content-card--wide' : undefined)}>
      <div className="content-card__header">
        <div className="content-card__title-group">
          <h3 className="content-card__title">{title}</h3>
          {description ? <p className="content-card__copy">{description}</p> : null}
        </div>
      </div>
      <div className="preferences-card__rows">
        {missingSettingKeys.length > 0 ? (
          <section className="setting-row setting-row--first">
            <div className="setting-row__copy">
              <h4 className="setting-row__title">Settings unavailable</h4>
              <p className="setting-row__description">
                {`The browser did not provide these schema-declared settings: ${missingSettingKeys.join(', ')}.`}
              </p>
            </div>
          </section>
        ) : null}
        {settings.length === 0 && missingSettingKeys.length === 0 ? (
          <section className="setting-row setting-row--first">
            <div className="setting-row__copy">
              <h4 className="setting-row__title">No settings available</h4>
              <p className="setting-row__description">
                This section is defined, but there are no current settings to display.
              </p>
            </div>
          </section>
        ) : null}
        {settings.map((setting, index) => (
          <SettingRow
            key={setting.key}
            isFirstRow={index === 0 && missingSettingKeys.length === 0}
            item={setting}
            store={store}
          />
        ))}
      </div>
    </article>
  );
}

function SettingRow(
    {isFirstRow, item, store}: {
      isFirstRow: boolean;
      item: SettingValue;
      store: MahoSettingsStore;
    }) {
  const meta = getSettingMetadata(item);

  return (
    <section className={joinClassNames('setting-row', isFirstRow ? 'setting-row--first' : undefined)}>
      <div className="setting-row__copy">
        <h4 className="setting-row__title">{meta ? meta.label : item.key}</h4>
        <p className="setting-row__description">
          {meta ? meta.description : `Metadata missing for ${item.key}.`}
        </p>
      </div>
      <div className="setting-row__control">
        {meta ? (
          <SettingControl item={item} meta={meta} store={store} />
        ) : (
          <p className="setting-row__description">
            {`Unsupported setting metadata for ${item.key}. Current value: ${item.value || '(empty)'}`}
          </p>
        )}
      </div>
    </section>
  );
}

function SettingControl(
    {item, meta, store}: {
      item: SettingValue;
      meta: SettingMetadata;
      store: MahoSettingsStore;
    }) {
  switch (meta.control) {
    case 'toggle':
      return <ToggleControl item={item} meta={meta} store={store} />;
    case 'select':
      return <SelectControl item={item} meta={meta} store={store} />;
    case 'color-swatch':
      return <ColorSwatchControl item={item} store={store} />;
    case 'slider':
      return <SliderControl item={item} meta={meta} store={store} />;
    case 'textarea':
      return <TextareaControl item={item} meta={meta} store={store} />;
    case 'number':
    case 'text':
    default:
      return <InputControl item={item} meta={meta} store={store} />;
  }
}

function ToggleControl(
    {item, meta, store}: {
      item: SettingValue;
      meta: SettingMetadata;
      store: MahoSettingsStore;
    }) {
  const enabled = item.value === 'true';
  const isSaving = store.isSaving(item.key);

  return (
    <Button
      aria-checked={enabled}
      aria-label={meta.label}
      className={joinClassNames('setting-switch', enabled ? 'is-on' : undefined)}
      disabled={isSaving}
      role="switch"
      type="button"
      onClick={() => {
        void store.commitSettingValue(item.key, enabled ? 'false' : 'true');
      }}>
      <span className="setting-switch__track">
        <span className="setting-switch__thumb" />
      </span>
      <span className="setting-switch__status">{enabled ? 'On' : 'Off'}</span>
    </Button>
  );
}

function InputControl(
    {item, meta, store}: {
      item: SettingValue;
      meta: SettingMetadata;
      store: MahoSettingsStore;
    }) {
  const [draftValue, setDraftValue] = useState(item.value);
  const isSaving = store.isSaving(item.key);

  useEffect(() => {
    setDraftValue(item.value);
  }, [item.key, item.value]);

  const commitValue = useCallback(async () => {
    const nextValue = normalizeInputValue(draftValue, item.value, meta);
    setDraftValue(nextValue);
    await store.commitSettingValue(item.key, nextValue);
  }, [draftValue, item.key, item.value, meta, store]);

  return (
    <label className="setting-input-shell">
      <TextInput
        aria-label={meta.label}
        className="setting-input"
        disabled={isSaving}
        inputMode={meta.control === 'number' ? 'numeric' : undefined}
        max={meta.control === 'number' && meta.max !== undefined ? String(meta.max) : undefined}
        min={meta.control === 'number' && meta.min !== undefined ? String(meta.min) : undefined}
        placeholder={meta.placeholder || ''}
        step={meta.control === 'number' && meta.step !== undefined ? String(meta.step) : undefined}
        type={meta.control === 'number' ? 'number' : 'text'}
        value={draftValue}
        onBlur={() => {
          void commitValue();
        }}
        onChange={event => setDraftValue(event.currentTarget.value)}
        onKeyDown={event => {
          if (event.key === 'Enter') {
            event.currentTarget.blur();
          }
        }}
      />
      {meta.control === 'number' ? (
        <span className="setting-input-shell__suffix">px</span>
      ) : null}
    </label>
  );
}

function SelectControl(
    {item, meta, store}: {
      item: SettingValue;
      meta: SettingMetadata;
      store: MahoSettingsStore;
    }) {
  if (meta.segmented) {
    return <SegmentedControl item={item} meta={meta} store={store} />;
  }

  const options = meta.options || [];
  const isSaving = store.isSaving(item.key);
  const hasKnownValue = options.some(option => option.value === item.value);

  return (
    <div className="setting-input-shell setting-input-shell--select">
      {!hasKnownValue ? (
        <p className="setting-row__description">
          {`Current value ${item.value || '(empty)'} is not recognized by the metadata registry.`}
        </p>
      ) : null}
      <select
        aria-invalid={!hasKnownValue || undefined}
        aria-label={meta.label}
        className="setting-input setting-select"
        disabled={isSaving}
        value={hasKnownValue ? item.value : (options[0]?.value || '')}
        onChange={event => {
          void store.commitSettingValue(item.key, event.currentTarget.value);
        }}>
        {options.map(option => (
          <option key={option.value} value={option.value}>{option.label}</option>
        ))}
      </select>
      <span aria-hidden="true" className="setting-input-shell__suffix setting-input-shell__suffix--icon">▾</span>
    </div>
  );
}

function SegmentedControl(
    {item, meta, store}: {
      item: SettingValue;
      meta: SettingMetadata;
      store: MahoSettingsStore;
    }) {
  const options = meta.options || [];
  const isSaving = store.isSaving(item.key);
  const hasKnownValue = options.some(option => option.value === item.value);

  return (
    <div>
      {!hasKnownValue ? (
        <p className="setting-row__description">
          {`Current value ${item.value || '(empty)'} is not recognized by the metadata registry.`}
        </p>
      ) : null}
      <div aria-label={meta.label} className="setting-segmented" role="tablist">
        {options.map(option => {
        const isActive = option.value === item.value;
        return (
          <Button
            key={option.value}
            aria-selected={isActive}
            className={joinClassNames('setting-segment', isActive ? 'is-active' : undefined)}
            disabled={isSaving}
            role="tab"
            type="button"
            onClick={() => {
              if (isActive) {
                return;
              }
              void store.commitSettingValue(item.key, option.value);
            }}>
            {option.displayLabel || option.label}
          </Button>
        );
        })}
      </div>
    </div>
  );
}

function ColorSwatchControl(
    {item, store}: {
      item: SettingValue;
      store: MahoSettingsStore;
    }) {
  const currentColor = parseAccentColor(item.value);
  const isSaving = store.isSaving(item.key);

  return (
    <div className="setting-color-swatches">
      {ACCENT_COLORS.map(color => {
        const isActive = !!currentColor &&
            currentColor.r === color.r &&
            currentColor.g === color.g &&
            currentColor.b === color.b;
        return (
          <Button
            key={`${color.r}-${color.g}-${color.b}`}
            aria-label={color.label}
            className={joinClassNames('setting-color-swatch', isActive ? 'is-active' : undefined)}
            disabled={isSaving}
            style={{'--swatch-color': `rgb(${color.r}, ${color.g}, ${color.b})`} as React.CSSProperties}
            type="button"
            onClick={() => {
              const value = JSON.stringify({r: color.r, g: color.g, b: color.b, a: 1.0});
              void store.commitSettingValue(item.key, value);
            }}
          />
        );
      })}
    </div>
  );
}

function SliderControl(
    {item, meta, store}: {
      item: SettingValue;
      meta: SettingMetadata;
      store: MahoSettingsStore;
    }) {
  const [draftValue, setDraftValue] = useState(item.value || String(meta.min || 0));
  const debounceTimer = useRef<number | null>(null);
  const isSaving = store.isSaving(item.key);

  useEffect(() => {
    if (debounceTimer.current !== null) {
      window.clearTimeout(debounceTimer.current);
      debounceTimer.current = null;
    }
    setDraftValue(item.value || String(meta.min || 0));
  }, [item.key, item.value, meta.min]);

  useEffect(() => {
    return () => {
      if (debounceTimer.current !== null) {
        window.clearTimeout(debounceTimer.current);
      }
    };
  }, []);

  const scheduleCommit = useCallback((nextValue: string) => {
    if (debounceTimer.current !== null) {
      window.clearTimeout(debounceTimer.current);
    }
    debounceTimer.current = window.setTimeout(() => {
      debounceTimer.current = null;
      void store.commitSettingValue(item.key, nextValue);
    }, 150);
  }, [item.key, store]);

  return (
    <div className="setting-slider-shell">
      <input
        aria-label={meta.label}
        className="setting-slider"
        disabled={isSaving}
        max={meta.max !== undefined ? String(meta.max) : undefined}
        min={meta.min !== undefined ? String(meta.min) : undefined}
        step={meta.step !== undefined ? String(meta.step) : undefined}
        type="range"
        value={draftValue}
        onChange={event => {
          const nextValue = event.currentTarget.value;
          setDraftValue(nextValue);
          scheduleCommit(nextValue);
        }}
      />
      <span className="setting-slider__value">{`${draftValue}px`}</span>
    </div>
  );
}

function TextareaControl(
    {item, meta, store}: {
      item: SettingValue;
      meta: SettingMetadata;
      store: MahoSettingsStore;
    }) {
  const [draftValue, setDraftValue] = useState(item.value || '');
  const isSaving = store.isSaving(item.key);

  useEffect(() => {
    setDraftValue(item.value || '');
  }, [item.key, item.value]);

  return (
    <div className="setting-textarea-shell">
      <TextArea
        aria-label={meta.label}
        className="setting-textarea"
        disabled={isSaving}
        placeholder={meta.placeholder || ''}
        rows={4}
        spellCheck={false}
        value={draftValue}
        onBlur={() => {
          void store.commitSettingValue(item.key, draftValue);
        }}
        onChange={event => setDraftValue(event.currentTarget.value)}
      />
    </div>
  );
}

const rootElement = document.querySelector('maho-settings-app');

if (!(rootElement instanceof HTMLElement)) {
  throw new Error('Missing <maho-settings-app> root for maho_settings.');
}

const store = new MahoSettingsStore();
createRoot(rootElement).render(
    <React.StrictMode>
      <SettingsApp store={store} />
    </React.StrictMode>);
