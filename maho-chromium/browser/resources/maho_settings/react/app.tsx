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
import {BrowserUpdateState} from '../mojo.js';
import type {PaneDefinition, SettingMetadata} from '../models.js';
import {DOMAIN_DEFINITIONS} from '../schema/domains.js';
import {
  LIVE_PANE_SECTION_MANIFEST,
  PANE_DEFINITION_MAP,
  PANE_DEFINITIONS,
  panesForSettings,
} from '../schema/panes.js';
import {
  SETTING_METADATA,
} from '../schema/setting_schema.js';
import {Button} from '@ui/button';
import {Input} from '@ui/input';
import {Textarea} from '@ui/textarea';
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from '@ui/select';
import {ToggleGroup, ToggleGroupItem} from '@ui/toggle-group';
import {Toaster} from '@ui/sonner';
import {ArrowLeft, ArrowUpRight} from '@icons/lucide';
import {cn} from '@lib/utils';
import {applyTheme, watchAutoTheme} from '@theme/apply_theme';
import {DomainPaneContent, PaneShell, SectionCard, Toggle} from './domain_panes.js';
import {DiscordCommunitySettingsSection} from './discord_community_settings_section.js';
import {MahoSettingsStore} from './store.js';

function cloneSetting(setting: SettingValue): SettingValue {
  return {
    ...setting,
    key: setting.key,
    value: setting.value,
  };
}

function isMigratedLivePane(pane: PaneDefinition): boolean {
  return pane.kind === 'live' && !pane.contentKind;
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

function normalizeInputValue(value: string, fallback: string): string {
  const trimmed = value.trim();
  return trimmed || fallback;
}

const SETTINGS_SHELL_CLASS = 'min-h-screen w-full bg-background text-foreground';
const SETTINGS_WINDOW_CLASS = 'h-screen w-full';
const SETTINGS_WORKSPACE_CLASS = 'grid h-full min-h-0 min-w-0 overflow-hidden';
const SETTINGS_WORKSPACE_COLUMNS_CLASS = 'grid-cols-1 grid-rows-[auto_minmax(0,1fr)] sm:grid-cols-[240px_minmax(0,1fr)] sm:grid-rows-[minmax(0,1fr)]';
const SETTINGS_SIDEBAR_CLASS = 'border-b border-border/60 glass sm:border-r sm:border-b-0';
const SETTINGS_SIDEBAR_SCROLL_CLASS = 'flex max-h-56 flex-col overflow-y-auto p-4 sm:h-full sm:max-h-none';
const SETTINGS_BRAND_CLASS = 'mb-4 grid gap-1 px-2 pt-2';
const SETTINGS_SIDEBAR_TITLE_CLASS = 'text-2xl font-semibold tracking-tight';
const SETTINGS_NAV_CLASS = 'flex flex-col gap-1 pb-3';
const SETTINGS_DOMAIN_LABEL_CLASS =
    'mt-4 mb-1 px-2 text-xs font-semibold uppercase tracking-[0.12em] text-muted-foreground first:mt-0';
const SETTINGS_CONTENT_CLASS = 'h-full min-w-0 overflow-hidden';
const SETTINGS_CONTENT_SCROLL_CLASS = 'h-full overflow-y-auto p-4 sm:p-6 lg:p-7';
const SIDEBAR_BUTTON_CLASS =
    'h-auto w-full justify-start border border-transparent px-3 py-2.5 text-left text-sm font-medium text-foreground shadow-none [&:not([aria-current=page])]:hover:border-border/60 [&:not([aria-current=page])]:hover:bg-surface-hover [&:not([aria-current=page])]:hover:text-foreground aria-[current=page]:translate-x-px aria-[current=page]:border-border aria-[current=page]:bg-surface-selected aria-[current=page]:text-foreground aria-[current=page]:shadow-sm';
const SIDEBAR_BUTTON_TEXT_CLASS = 'flex min-w-0 flex-1';
const SIDEBAR_BUTTON_LABEL_CLASS = 'truncate';
const BROWSER_CONTENT_CLASS = 'h-full overflow-hidden';
const BROWSER_PANE_CLASS = 'flex h-full min-h-0 min-w-0 flex-col';
const BROWSER_TOOLBAR_CLASS = 'flex shrink-0 items-center border-b border-border/60 glass px-4 py-2';
const BROWSER_SHELL_CLASS = 'min-h-0 min-w-0 flex-1 overflow-hidden bg-background';
const BROWSER_FRAME_CLASS = 'block h-full w-full border-0 bg-background';
const SETTING_ROW_CLASS =
    'grid gap-4 border-t border-border px-6 py-4 sm:grid-cols-[minmax(0,1fr)_minmax(270px,344px)] sm:items-center';
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
    const cleanupAutoTheme = watchAutoTheme((resolved) => applyTheme(resolved));
    const handleBeforeUnload = () => {
      store.dispose();
    };
    window.addEventListener('beforeunload', handleBeforeUnload);
    return () => {
      cleanupAutoTheme();
      window.removeEventListener('beforeunload', handleBeforeUnload);
      store.dispose();
    };
  }, [store]);

  const currentPane = useMemo(() => {
    const pane = PANE_DEFINITION_MAP[state.currentPaneKey];
    if (pane && pane.kind !== 'hidden') {
      return pane;
    }
    return PANE_DEFINITIONS[0]!;
  }, [state.currentPaneKey]);

  // Mail panes appear only when the mail.enabled setting is on, so the nav is
  // derived from live settings instead of a static list.
  const mailEnabled = useMemo(
      () => state.settings.some(
          s => s.key === 'mail.enabled' && s.value === 'true'),
      [state.settings]);

  const visiblePanes = useMemo(
      () => panesForSettings(mailEnabled), [mailEnabled]);

  const panesByDomain = useMemo(() => {
    const grouped = new Map<string, PaneDefinition[]>();
    for (const pane of visiblePanes) {
      const list = grouped.get(pane.domain) || [];
      list.push(pane);
      grouped.set(pane.domain, list);
    }
    return grouped;
  }, [visiblePanes]);

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
                            onSelect={() => {
                              if (pane.externalAction === 'openExtensions') {
                                store.getHandler().openExtensionsPage();
                              } else {
                                store.selectPane(pane.key);
                              }
                            }}
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
            <div className={currentPane.kind === 'embed' ? BROWSER_CONTENT_CLASS : SETTINGS_CONTENT_SCROLL_CLASS}>
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
    <section
      className={BROWSER_PANE_CLASS}
      data-pane={pane.key}
      id={`section-${pane.key}`}>
      <header className={BROWSER_TOOLBAR_CLASS}>
        <Button
          className="w-fit justify-start rounded-lg border border-border bg-background/70 px-3 text-sm font-medium text-foreground hover:bg-surface-hover"
          type="button"
          variant="ghost"
          onClick={onExit}>
          <ArrowLeft aria-hidden="true" className={SETTING_ICON_CLASS} />
          Back to Maho settings
        </Button>
      </header>
      <div className={BROWSER_SHELL_CLASS}>
        <iframe
          className={BROWSER_FRAME_CLASS}
          role="document"
          src={embedUrl}
          title={pane.title}
        />
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
      {pane.externalAction && (
        <ArrowUpRight className="ml-auto size-4 shrink-0 opacity-60" />
      )}
    </Button>
  );
}

const UPDATE_STATE_LABELS: Record<number, string> = {
  [BrowserUpdateState.kIdle]: 'Up to date',
  [BrowserUpdateState.kChecking]: 'Checking for updates…',
  [BrowserUpdateState.kUpdateAvailable]: 'Update available',
  [BrowserUpdateState.kDownloading]: 'Downloading update…',
  [BrowserUpdateState.kVerifying]: 'Verifying update…',
  [BrowserUpdateState.kReadyToInstall]: 'Update ready — restart to install',
  [BrowserUpdateState.kUpToDate]: 'Up to date',
  [BrowserUpdateState.kError]: 'Update check failed',
};

function BrowserVersionCard({store}: {store: MahoSettingsStore}) {
  const state = useSyncExternalStore(
      listener => store.subscribe(listener), () => store.getSnapshot());
  const isDev = !state.updateSupported;
  const checking = state.updateState === BrowserUpdateState.kChecking;
  const readyToInstall =
      state.updateState === BrowserUpdateState.kReadyToInstall;
  const versionLine = `Maho ${state.browserVersion || '…'}${
      state.browserChannel ? ` · ${state.browserChannel}` : ''}`;

  return (
    <div className="mb-6">
      <SectionCard title="About Maho" description="Version and update status.">
        <div className="grid gap-4 border-t border-border px-6 py-4 sm:grid-cols-[minmax(0,1fr)_auto] sm:items-center">
          <div className="min-w-0">
            <div className="text-sm font-medium text-foreground">{versionLine}</div>
            <div className="mt-1 text-sm text-muted-foreground">
              {isDev
                ? 'Developer build — automatic updates are disabled.'
                : UPDATE_STATE_LABELS[state.updateState] ?? ''}
            </div>
          </div>
          {!isDev ? (
            <div className="flex shrink-0 gap-2">
              {readyToInstall ? (
                <Button
                  variant="default"
                  onClick={() => store.applyBrowserUpdateAndRestart()}>
                  Restart to update
                </Button>
              ) : (
                <Button
                  variant="outline"
                  disabled={checking}
                  onClick={() => store.checkForBrowserUpdates()}>
                  {checking ? 'Checking…' : 'Check for updates'}
                </Button>
              )}
            </div>
          ) : null}
        </div>
      </SectionCard>
    </div>
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
    <PaneShell pane={pane}>
      {bootstrapError ? <p className="mb-4 text-sm leading-6 text-muted-foreground">{bootstrapError}</p> : null}
      {!bootstrapError && lastActionError ? (
        <p className="mb-4 text-sm leading-6 text-muted-foreground">{lastActionError}</p>
      ) : null}
      {pane.key === 'general' ? <DiscordCommunitySettingsSection /> : null}
      {pane.key === 'general' ? <BrowserVersionCard store={store} /> : null}
      {cards.map((card, cardIndex) => (
        <div key={`${pane.key}-${card.title}`} className={cardIndex < cards.length - 1 ? 'mb-6' : undefined}>
          <SectionCard title={card.title} description={card.description}>
            {card.missingSettingKeys.length > 0 ? (
              <div className="text-sm text-yellow-500 bg-yellow-500/10 p-3 mx-6 my-4 rounded-md">
                <h4 className="font-semibold">Settings unavailable</h4>
                <p className="text-xs">
                  {`The browser did not provide these schema-declared settings: ${card.missingSettingKeys.join(', ')}.`}
                </p>
              </div>
            ) : null}
            {card.settings.length === 0 && card.missingSettingKeys.length === 0 ? (
              <div className="text-sm text-muted-foreground text-center py-6">
                No settings available
              </div>
            ) : null}
            {card.settings.map(setting => (
              <SettingRow
                key={setting.key}
                item={setting}
                store={store}
              />
            ))}
          </SectionCard>
        </div>
      ))}
    </PaneShell>
  );
}

function SettingRow(
    {item, store}: {
      item: SettingValue;
      store: MahoSettingsStore;
    }) {
  const meta = getSettingMetadata(item);

  return (
    <section className={SETTING_ROW_CLASS}>
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
    const nextValue = normalizeInputValue(draftValue, item.value);
    setDraftValue(nextValue);
    await store.commitSettingValue(item.key, nextValue);
  }, [draftValue, item.key, item.value, store]);

  return (
    <div className="flex items-center gap-2">
      <Input
        aria-label={meta.label}
        disabled={isSaving}
        placeholder={meta.placeholder || ''}
        type="text"
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
      {meta.unit ? (
        <span className="text-sm text-muted-foreground">{meta.unit}</span>
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
      <span className={SLIDER_VALUE_CLASS}>
        {meta.unit ? `${draftValue}${meta.unit}` : draftValue}
      </span>
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
(window as any).settingsStore = settingsStore;
createRoot(rootElement).render(
    <React.StrictMode>
      <SettingsApp store={settingsStore} />
    </React.StrictMode>);
