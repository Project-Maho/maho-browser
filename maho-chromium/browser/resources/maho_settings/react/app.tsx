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
import type {PaneDefinition, SettingMetadata} from '../models.js';
import {DOMAIN_DEFINITIONS} from '../schema/domains.js';
import {
  LIVE_PANE_SECTION_MANIFEST,
  PANE_DEFINITION_MAP,
  PANE_DEFINITIONS,
} from '../schema/panes.js';
import {
  SETTING_METADATA,
} from '../schema/setting_schema.js';
import {Button} from '@ui/button';
import {Input} from '@ui/input';
import {Textarea} from '@ui/textarea';
import {
  Card,
  CardContent,
  CardDescription,
  CardHeader,
  CardTitle,
} from '@ui/card';
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from '@ui/select';
import {ToggleGroup, ToggleGroupItem} from '@ui/toggle-group';
import {Toaster} from '@ui/sonner';
import {cn} from '@lib/utils';
import {applyTheme, watchAutoTheme} from '@theme/apply_theme';
import {DomainPaneContent, Toggle} from './domain_panes.js';
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

const SETTINGS_SHELL_CLASS = 'min-h-screen w-full bg-background text-foreground';
const SETTINGS_WINDOW_CLASS = 'h-screen w-full';
const SETTINGS_WORKSPACE_CLASS = 'grid h-full min-h-0 min-w-0';
const SETTINGS_WORKSPACE_COLUMNS_CLASS = 'grid-cols-[240px_minmax(0,1fr)]';
const SETTINGS_SIDEBAR_CLASS = 'border-r border-border bg-card';
const SETTINGS_SIDEBAR_SCROLL_CLASS = 'flex h-full flex-col overflow-y-auto p-4';
const SETTINGS_BRAND_CLASS = 'mb-4 grid gap-1 px-2 pt-2';
const SETTINGS_SIDEBAR_TITLE_CLASS = 'text-2xl font-semibold tracking-tight';
const SETTINGS_NAV_CLASS = 'flex flex-col gap-1 pb-3';
const SETTINGS_DOMAIN_LABEL_CLASS =
    'mt-4 mb-1 px-2 text-xs font-semibold uppercase tracking-[0.12em] text-muted-foreground first:mt-0';
const SETTINGS_CONTENT_CLASS = 'h-full min-w-0 overflow-hidden bg-background';
const SETTINGS_CONTENT_SCROLL_CLASS = 'h-full overflow-y-auto p-6 lg:p-7';
const SETTINGS_SECTION_CLASS = 'mb-8 scroll-mt-8';
const SETTINGS_SECTION_CARD_CLASS = 'overflow-hidden rounded-xl border border-border bg-card shadow-sm';
const SETTINGS_SECTION_HEADER_CLASS = 'flex flex-col gap-2 border-b border-border p-4 md:p-5';
const SETTINGS_SECTION_HEADER_EMBED_CLASS = 'border-b-0 pb-0 sm:flex-row sm:items-center sm:justify-between';
const SETTINGS_SECTION_TITLE_CLASS = 'text-2xl font-semibold tracking-tight';
const SETTINGS_SECTION_DESCRIPTION_CLASS = 'text-sm leading-6 text-muted-foreground';
const SETTINGS_CARDS_GRID_CLASS = 'grid gap-4 p-4 md:grid-cols-2 md:p-5';
const SIDEBAR_BUTTON_CLASS =
    'h-auto w-full justify-start border border-transparent px-3 py-2.5 text-left text-sm font-medium text-foreground shadow-none [&:not([aria-current=page])]:hover:border-border/60 [&:not([aria-current=page])]:hover:bg-background/80 [&:not([aria-current=page])]:hover:text-foreground aria-[current=page]:translate-x-px aria-[current=page]:border-border aria-[current=page]:bg-accent aria-[current=page]:text-accent-foreground aria-[current=page]:shadow-sm';
const SIDEBAR_BUTTON_TEXT_CLASS = 'flex min-w-0 flex-1';
const SIDEBAR_BUTTON_LABEL_CLASS = 'truncate';
const BROWSER_SHELL_CLASS = 'min-w-0 overflow-hidden rounded-xl border border-border bg-background/50 shadow-inner';
const BROWSER_FRAME_CLASS = 'block h-[78vh] min-h-[720px] w-full border-0 bg-background';
const SETTING_ROW_CLASS =
    'grid gap-4 border-b border-border py-4 sm:grid-cols-[minmax(0,1fr)_minmax(270px,344px)] sm:items-center';
const SETTING_ROW_FIRST_CLASS = 'pt-0';
const SETTING_COPY_CLASS = 'min-w-0 flex-1';
const SETTING_ROW_TITLE_CLASS = 'text-sm font-medium text-foreground';
const SETTING_ROW_DESCRIPTION_CLASS = 'mt-1 text-xs leading-5 text-muted-foreground';
const SETTING_CONTROL_CLASS = 'flex w-full items-center justify-start sm:justify-end';
const SETTING_ICON_CLASS = 'inline-block size-4 shrink-0 align-middle';
const SLIDER_SHELL_CLASS = 'flex w-full min-w-0 items-center gap-3';
const SLIDER_CLASS =
    'h-1.5 flex-1 cursor-pointer appearance-none rounded-full bg-muted accent-primary [&::-webkit-slider-thumb]:size-5 [&::-webkit-slider-thumb]:appearance-none [&::-webkit-slider-thumb]:rounded-full [&::-webkit-slider-thumb]:border-0 [&::-webkit-slider-thumb]:bg-primary [&::-webkit-slider-thumb]:shadow-[0_4px_12px_rgba(2,6,14,0.4)]';
const SLIDER_VALUE_CLASS = 'min-w-12 text-right text-sm font-medium text-muted-foreground';
const TEXTAREA_SHELL_CLASS = 'w-full min-w-[300px]';

function SettingsApp({store}: {store: MahoSettingsStore}) {
  const state = useSyncExternalStore(
      listener => store.subscribe(listener), () => store.getSnapshot());

  useEffect(() => {
    void store.bootstrap();
    applyTheme("auto");
    return watchAutoTheme((resolved) => applyTheme(resolved));
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
    <div className={SETTINGS_SHELL_CLASS}>
      <div className={SETTINGS_WINDOW_CLASS}>
        <div className={cn(
            SETTINGS_WORKSPACE_CLASS,
            currentPane.kind === 'embed' ? 'grid-cols-1' : SETTINGS_WORKSPACE_COLUMNS_CLASS)}>
          {currentPane.kind === 'embed' ? null : (
            <aside className={SETTINGS_SIDEBAR_CLASS}>
              <div className={SETTINGS_SIDEBAR_SCROLL_CLASS}>
                <section className={SETTINGS_BRAND_CLASS}>
                  <h1 className={SETTINGS_SIDEBAR_TITLE_CLASS}>Settings</h1>
                </section>
                <nav className={SETTINGS_NAV_CLASS} aria-label="Settings panes">
                  {DOMAIN_DEFINITIONS.map(domain => {
                    const domainPanes = panesByDomain.get(domain.id);
                    if (!domainPanes || domainPanes.length === 0) {
                      return null;
                    }

                    return (
                      <React.Fragment key={domain.id}>
                        <div className={SETTINGS_DOMAIN_LABEL_CLASS}>{domain.label}</div>
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
          )}
          <main className={SETTINGS_CONTENT_CLASS}>
            <div className={SETTINGS_CONTENT_SCROLL_CLASS}>
              {currentPane.kind === 'embed' ? (
                <EmbedPane pane={currentPane} onExit={() => store.selectPane('general')} />
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
      <Toaster position="bottom-right" />
    </div>
  );
}

function EmbedPane({pane, onExit}: {pane: PaneDefinition; onExit: () => void}) {
  const embedUrl = pane.embedUrl || 'chrome://settings/';

  return (
    <section className={SETTINGS_SECTION_CLASS} data-pane={pane.key} id={`section-${pane.key}`}>
      <div className={SETTINGS_SECTION_CARD_CLASS}>
        <header className={cn(SETTINGS_SECTION_HEADER_CLASS, SETTINGS_SECTION_HEADER_EMBED_CLASS)}>
          <Button
            className="w-fit justify-start rounded-lg border border-border bg-background/70 px-3 text-sm font-medium text-foreground hover:bg-accent hover:text-accent-foreground"
            type="button"
            variant="ghost"
            onClick={onExit}>
            <img alt="" aria-hidden="true" className={SETTING_ICON_CLASS} src="chrome://maho-settings/icons/arrow-right.svg" style={{transform: 'rotate(180deg)'}} />
            Back to Maho settings
          </Button>
          <h2 className={SETTINGS_SECTION_TITLE_CLASS}>{pane.title}</h2>
        </header>
        <div className={cn(SETTINGS_CARDS_GRID_CLASS, 'grid-cols-1 pt-4 md:grid-cols-1')}>
          <div className={BROWSER_SHELL_CLASS}>
            <iframe
              className={BROWSER_FRAME_CLASS}
              role="document"
              src={embedUrl}
              title={pane.title}
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
    <Button
      variant="ghost"
      aria-current={isActive ? 'page' : undefined}
      className={SIDEBAR_BUTTON_CLASS}
      data-pane={pane.key}
      type="button"
      onClick={onSelect}>
      <span className={SIDEBAR_BUTTON_TEXT_CLASS}>
        <span className={SIDEBAR_BUTTON_LABEL_CLASS}>{pane.navTitle}</span>
      </span>
    </Button>
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
    <section className={SETTINGS_SECTION_CLASS} data-pane={pane.key} id={`section-${pane.key}`}>
      <div className={SETTINGS_SECTION_CARD_CLASS}>
        <header className={SETTINGS_SECTION_HEADER_CLASS}>
          <h2 className={SETTINGS_SECTION_TITLE_CLASS}>{pane.title}</h2>
          {bootstrapError ? <p className={SETTINGS_SECTION_DESCRIPTION_CLASS}>{bootstrapError}</p> : null}
          {!bootstrapError && lastActionError ? (
            <p className={SETTINGS_SECTION_DESCRIPTION_CLASS}>{lastActionError}</p>
          ) : null}
        </header>
        <div className={SETTINGS_CARDS_GRID_CLASS}>
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
    <Card className={cn('h-full border-border/70 bg-background/40 shadow-none', wide ? 'md:col-span-full' : undefined)}>
      <CardHeader>
        <CardTitle>{title}</CardTitle>
        {description ? <CardDescription>{description}</CardDescription> : null}
      </CardHeader>
      <CardContent className="space-y-4">
        {missingSettingKeys.length > 0 ? (
          <div className="text-sm text-yellow-500 bg-yellow-500/10 p-3 rounded-md">
            <h4 className="font-semibold">Settings unavailable</h4>
            <p className="text-xs">
              {`The browser did not provide these schema-declared settings: ${missingSettingKeys.join(', ')}.`}
            </p>
          </div>
        ) : null}
        {settings.length === 0 && missingSettingKeys.length === 0 ? (
          <div className="text-sm text-muted-foreground text-center py-4">
            No settings available
          </div>
        ) : null}
        {settings.map((setting, index) => (
          <SettingRow
            key={setting.key}
            isFirstRow={index === 0 && missingSettingKeys.length === 0}
            item={setting}
            store={store}
          />
        ))}
      </CardContent>
    </Card>
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
    <section className={cn(SETTING_ROW_CLASS, isFirstRow ? SETTING_ROW_FIRST_CLASS : undefined)}>
      <div className={SETTING_COPY_CLASS}>
        <h4 className={SETTING_ROW_TITLE_CLASS}>{meta ? meta.label : item.key}</h4>
        <p className={SETTING_ROW_DESCRIPTION_CLASS}>
          {meta ? meta.description : `Metadata missing for ${item.key}.`}
        </p>
      </div>
      <div className={SETTING_CONTROL_CLASS}>
        {meta ? (
          <SettingControl item={item} meta={meta} store={store} />
        ) : (
          <p className={SETTING_ROW_DESCRIPTION_CLASS}>
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
    case 'toggle': {
      const enabled = item.value === 'true';
      return (
        <Toggle
          enabled={enabled}
          label={meta.label}
          onToggle={async (next) => {
            await store.commitSettingValue(item.key, next ? 'true' : 'false');
          }}
        />
      );
    }
    case 'select':
      return <SelectControl item={item} meta={meta} store={store} />;
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
    <div className="flex items-center gap-2">
      <Input
        aria-label={meta.label}
        disabled={isSaving}
        inputMode={meta.control === 'number' ? 'numeric' : undefined}
        max={meta.control === 'number' && meta.max !== undefined ? String(meta.max) : undefined}
        min={meta.control === 'number' && meta.min !== undefined ? String(meta.min) : undefined}
        placeholder={meta.placeholder || ''}
        step={meta.control === 'number' && meta.step !== undefined ? String(meta.step) : undefined}
        type={meta.control === 'number' ? 'number' : 'text'}
        value={draftValue}
        className="w-24 h-9"
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
        <span className="text-sm text-muted-foreground">px</span>
      ) : null}
    </div>
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
  const currentValue = hasKnownValue ? item.value : (options[0]?.value || '');

  return (
    <div className="w-48">
      {!hasKnownValue ? (
        <p className="text-xs text-destructive mb-1">
          {`Current value ${item.value || '(empty)'} is not recognized.`}
        </p>
      ) : null}
      <Select
        disabled={isSaving}
        value={currentValue}
        onValueChange={(val: string) => {
          void store.commitSettingValue(item.key, val);
        }}
      >
        <SelectTrigger>
          <SelectValue />
        </SelectTrigger>
        <SelectContent>
          {options.map(option => (
            <SelectItem key={option.value} value={option.value}>
              {option.label}
            </SelectItem>
          ))}
        </SelectContent>
      </Select>
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
    <div className="flex flex-col gap-1">
      {!hasKnownValue ? (
        <p className="text-xs text-destructive">
          {`Current value ${item.value || '(empty)'} is not recognized.`}
        </p>
      ) : null}
      <ToggleGroup
        type="single"
        disabled={isSaving}
        value={item.value}
        onValueChange={(val: string) => {
          if (val) {
            void store.commitSettingValue(item.key, val);
          }
        }}
      >
        {options.map(option => (
          <ToggleGroupItem key={option.value} value={option.value}>
            {option.displayLabel || option.label}
          </ToggleGroupItem>
        ))}
      </ToggleGroup>
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
    <div className={SLIDER_SHELL_CLASS}>
      <input
        aria-label={meta.label}
        className={SLIDER_CLASS}
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
      <span className={SLIDER_VALUE_CLASS}>{`${draftValue}px`}</span>
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
    <div className={TEXTAREA_SHELL_CLASS}>
      <Textarea
        aria-label={meta.label}
        disabled={isSaving}
        placeholder={meta.placeholder || ''}
        rows={4}
        spellCheck={false}
        value={draftValue}
        className="font-mono text-sm"
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

const settingsStore = new MahoSettingsStore();
createRoot(rootElement).render(
    <React.StrictMode>
      <SettingsApp store={settingsStore} />
    </React.StrictMode>);
