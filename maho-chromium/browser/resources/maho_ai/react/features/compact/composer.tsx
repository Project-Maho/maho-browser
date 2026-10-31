import {FormEvent, KeyboardEvent as ReactKeyboardEvent, useEffect, useRef, useState} from 'react';
import type {ReactNode} from 'react';

import type {
  AIModelOptionRecord,
  AIProviderOptionRecord,
  AIReasoningOptionRecord,
  AISelectionRequest,

  AppState,
  ComposerAttachment,
  HistoryItemRecord,
  OpenTabRecord,
} from '../../../types.js';
import {
  AI_REASONING_EFFORT,
  createHistoryAttachment,
  createTabAttachment,
  getHistoryItemTitle,
  getOpenTabTitle,
  hasCurrentPageAttachment,
  isHistoryAttachment,
  isTabAttachment,
  isSessionReadOnly,
} from '../../../types.js';
import {cn} from '@lib/utils';
import {toast} from 'sonner';
import {Button} from '@ui/button';
import {Input} from '@ui/input';
import {
  Popover,
  PopoverContent,
  PopoverTrigger,
} from '@ui/popover';
import {
  Tooltip,
  TooltipContent,
  TooltipTrigger,
} from '@ui/tooltip';
import {AlertCircle, ArrowUp, Check, ChevronDown, Globe, Mic, Plus, Search, Square} from '@icons/lucide';
import {Alert, AlertDescription} from '@ui/alert';
import {ComposerTextarea} from '../../components/composer-textarea.js';

export interface ComposerHandlers {
  onCancel: () => void;
  onComposerBlur?: () => void;
  onLoadOpenTabs: () => void;
  onPromptChange: (value: string) => void;
  onRefreshOpenTabs: () => void;
  onResetHistorySearch: () => void;
  onSearchHistory: (query: string) => void;
  onStartSession: () => void;
  onSubmit: () => void;
  onToggleBrowserContext: () => void;
  onToggleHistoryAttachment: (attachment: ComposerAttachment) => void;
  onToggleTabAttachment: (attachment: ComposerAttachment) => void;
  onAttachFiles: (files: FileList | File[]) => void;
  onRequestFileChooser: () => void;
  onRemoveAttachment: (attachment: ComposerAttachment) => void;
  onSetAISelection: (selection: AISelectionRequest) => Promise<boolean>;
  onOpenSettings: () => void;
  onOpenVoice: () => void;
}

const pickerRowClassName =
    'group grid w-full grid-cols-[auto_minmax(0,1fr)_auto] items-center gap-2 rounded-lg px-1.5 py-1.5 text-left text-xs text-foreground transition-colors outline-none hover:bg-surface-hover focus-visible:bg-surface-selected focus-visible:ring-1 focus-visible:ring-ring data-[focused=true]:bg-surface-selected disabled:cursor-default disabled:opacity-40';
const pickerStatusClassName =
    'grid w-full grid-cols-[auto_minmax(0,1fr)_auto] items-center gap-2 rounded-lg px-1.5 py-1.5 text-left text-xs text-muted-foreground';
const pickerSectionClassName = 'grid gap-1';
const pickerSectionLabelClassName =
    'px-1 text-[10px] font-semibold uppercase tracking-[0.14em] text-muted-foreground/80';
const aiSelectionRowClassName =
    'group flex w-full items-center justify-between gap-2 rounded-lg px-2.5 py-1.5 text-left text-xs text-foreground transition-colors outline-none hover:bg-surface-hover focus-visible:bg-surface-selected focus-visible:ring-1 focus-visible:ring-ring disabled:pointer-events-none disabled:opacity-45';
const aiSelectionLabelClassName =
    'px-1 text-[10px] font-semibold uppercase tracking-[0.16em] text-muted-foreground';

function getCompactModelLabel(modelId: string): string {
  return modelId.split('/').pop() || modelId;
}

function getModelLabel(model: AIModelOptionRecord): string {
  return model.label.trim() || getCompactModelLabel(model.id);
}

function getActiveProvider(state: AppState): AIProviderOptionRecord|null {
  return state.aiSettings.providerOptions.find(
      provider => provider.id === state.aiSettings.activeProviderId) || null;
}

function getSelectedProvider(state: AppState): AIProviderOptionRecord|null {
  return getActiveProvider(state) || state.aiSettings.providerOptions[0] || null;
}

function getSelectedModel(
    state: AppState, provider: AIProviderOptionRecord|null): AIModelOptionRecord|null {
  if (!provider) {
    return null;
  }
  return provider.modelOptions.find(model => model.id === state.aiSettings.activeModelId) ||
      provider.modelOptions[0] || null;
}

function getSelectedReasoning(state: AppState): AIReasoningOptionRecord|null {
  return state.aiSettings.reasoningOptions.find(
      option => option.effort === state.aiSettings.activeReasoningEffort) ||
      state.aiSettings.reasoningOptions[0] || null;
}

function buildSelection(
    request: {
      readonly model: AIModelOptionRecord;
      readonly provider: AIProviderOptionRecord;
      readonly reasoning: AIReasoningOptionRecord;
    }): AISelectionRequest {
  return {
    modelId: request.model.id,
    providerId: request.provider.id,
    reasoningEffort: request.reasoning.effort,
  };
}

function AISelectionStatus({title}: {readonly title: string}) {
  return (
    <div className="rounded-lg px-2.5 py-1.5 text-xs text-muted-foreground">
      {title}
    </div>
  );
}

function AISelectionCheck({active}: {readonly active: boolean}) {
  if (!active) {
    return <span aria-hidden="true" className="size-3.5" />;
  }
  return <Check aria-hidden="true" className="size-3.5 text-primary" />;
}

// Mandatory scope copy for the quick switcher: it edits the global Default
// route, so background consumers that inherit Default move with it. The plan
// (models-settings-redesign §5.2) requires this copy on every Default-model
// selection surface.
const DEFAULT_MODEL_SCOPE_COPY =
    'Changes the default model. Also used by Memory, Tab Tidy, and other background features unless they have their own model override.';

function AISelectionScopeCopy() {
  return (
    <p className="px-2 pb-1 pt-0.5 text-[10px] leading-snug text-muted-foreground"
        data-ai-selection-scope-copy>
      {DEFAULT_MODEL_SCOPE_COPY}
    </p>
  );
}

interface ModelPickerEntry {
  readonly model: AIModelOptionRecord;
  readonly provider: AIProviderOptionRecord;
}

// Every whitespace-separated token must appear in the model label, id, or
// provider label, so "opus 4" and "claude 6" both narrow the list.
function modelMatchesQuery(entry: ModelPickerEntry, query: string): boolean {
  const tokens = query.trim().toLowerCase().split(/\s+/).filter(Boolean);
  if (tokens.length === 0) {
    return true;
  }
  const haystack =
      `${getModelLabel(entry.model)} ${entry.model.id} ${entry.provider.label}`.toLowerCase();
  return tokens.every(token => haystack.includes(token));
}

// Searchable model picker: a search field above a flat list of every model
// across configured providers. Picking a model from another provider switches
// the provider with it.
function ModelSearchLevel({activeModel, activeProvider, onSelect, providers, value}: {
  readonly activeModel: AIModelOptionRecord|null;
  readonly activeProvider: AIProviderOptionRecord|null;
  readonly onSelect: (entry: ModelPickerEntry) => void;
  readonly providers: readonly AIProviderOptionRecord[];
  readonly value: string;
}) {
  const [open, setOpen] = useState(false);
  const [query, setQuery] = useState('');
  const [highlighted, setHighlighted] = useState(0);
  const listRef = useRef<HTMLDivElement>(null);
  const entries: ModelPickerEntry[] = providers.flatMap(
      provider => provider.modelOptions.map(model => ({model, provider})));
  const showProvider = providers.filter(p => p.modelOptions.length > 0).length > 1;
  const filtered = entries.filter(entry => modelMatchesQuery(entry, query));
  const highlightIndex = Math.min(highlighted, Math.max(filtered.length - 1, 0));
  const isActive = (entry: ModelPickerEntry) =>
      entry.provider.id === activeProvider?.id && entry.model.id === activeModel?.id;
  const optionId = (index: number) => `ai-model-option-${index}`;

  const onOpenChange = (next: boolean) => {
    setOpen(next);
    setQuery('');
    if (next) {
      const activeIndex = entries.findIndex(isActive);
      setHighlighted(activeIndex >= 0 ? activeIndex : 0);
    }
  };

  const select = (entry: ModelPickerEntry) => {
    onOpenChange(false);
    onSelect(entry);
  };

  const moveHighlight = (index: number) => {
    setHighlighted(index);
    listRef.current?.querySelector<HTMLElement>(`#${optionId(index)}`)
        ?.scrollIntoView({block: 'nearest'});
  };

  const onSearchKeyDown = (event: ReactKeyboardEvent<HTMLInputElement>) => {
    if (filtered.length === 0) {
      return;
    }
    if (event.key === 'ArrowDown') {
      event.preventDefault();
      moveHighlight((highlightIndex + 1) % filtered.length);
    } else if (event.key === 'ArrowUp') {
      event.preventDefault();
      moveHighlight((highlightIndex - 1 + filtered.length) % filtered.length);
    } else if (event.key === 'Enter' && !event.nativeEvent.isComposing) {
      event.preventDefault();
      const entry = filtered[highlightIndex];
      if (entry) {
        select(entry);
      }
    }
  };

  return (
    <Popover open={open} onOpenChange={onOpenChange}>
      <PopoverTrigger asChild>
        <button
          className={cn(
              'inline-flex h-6 min-w-0 max-w-[9rem] shrink items-center gap-0.5 rounded-md panel-chip pl-[7px] pr-[5px]',
              'text-[11px] font-medium tracking-[-0.004em] outline-none',
              'focus-visible:ring-1 focus-visible:ring-ring',
              open && 'bg-surface-selected text-foreground')}
          aria-label="Default model"
          title="Default model"
          data-ai-selection-level="model"
          type="button">
          <span className="truncate">{value}</span>
          <ChevronDown aria-hidden="true" className="size-2.5 shrink-0 opacity-55" />
        </button>
      </PopoverTrigger>
      <PopoverContent
          align="start"
          aria-label="model options"
          className="flex w-60 flex-col overflow-hidden rounded-xl panel-card-raised p-1"
          onOpenAutoFocus={event => {
            // Land focus in the search field instead of the first option.
            event.preventDefault();
            (event.currentTarget as HTMLElement | null)
                ?.querySelector<HTMLInputElement>('input[data-ai-model-search]')?.focus();
          }}
          side="top">
        <div className="flex h-8 shrink-0 items-center gap-2 px-2">
          <Search aria-hidden="true" className="size-3.5 shrink-0 text-muted-foreground" />
          <input
            aria-activedescendant={filtered.length > 0 ? optionId(highlightIndex) : undefined}
            aria-autocomplete="list"
            aria-controls="ai-model-listbox"
            aria-expanded="true"
            aria-label="Search models"
            autoComplete="off"
            className="h-full min-w-0 flex-1 bg-transparent text-[13px] text-foreground outline-none placeholder:text-muted-foreground"
            data-ai-model-search
            placeholder="Search models"
            role="combobox"
            spellCheck={false}
            type="text"
            value={query}
            onChange={event => {
              setQuery(event.target.value);
              setHighlighted(0);
            }}
            onKeyDown={onSearchKeyDown}
          />
        </div>
        <div
          ref={listRef}
          aria-label="Models"
          className="grid max-h-72 gap-px overflow-y-auto pt-1"
          id="ai-model-listbox"
          role="listbox">
          {entries.length === 0 ? (
            <div className="px-2.5 py-2 text-xs text-muted-foreground">No models available</div>
          ) : filtered.length === 0 ? (
            <div className="px-2.5 py-2 text-xs text-muted-foreground">No matching models</div>
          ) : (
            filtered.map((entry, index) => {
              const active = isActive(entry);
              return (
                <button
                  key={`${entry.provider.id}/${entry.model.id}`}
                  aria-selected={active}
                  className={cn(
                      'flex h-8 w-full shrink-0 items-center gap-2 rounded-lg px-2.5 text-left text-[13px] text-foreground outline-none',
                      index === highlightIndex && 'bg-surface-selected')}
                  data-ai-selection-model={entry.model.id}
                  data-ai-selection-model-provider={entry.provider.id}
                  id={optionId(index)}
                  role="option"
                  tabIndex={-1}
                  title={getModelLabel(entry.model)}
                  type="button"
                  onClick={() => select(entry)}
                  onMouseMove={() => {
                    if (index !== highlightIndex) {
                      setHighlighted(index);
                    }
                  }}>
                  <span className="min-w-0 flex-1 truncate">{getModelLabel(entry.model)}</span>
                  {showProvider ? (
                    <span className="max-w-[40%] shrink-0 truncate text-[11px] text-muted-foreground">
                      {entry.provider.label}
                    </span>
                  ) : null}
                  <AISelectionCheck active={active} />
                </button>
              );
            })
          )}
        </div>
        <div className="mt-1 border-t border-border/60 pt-1">
          <AISelectionScopeCopy />
        </div>
      </PopoverContent>
    </Popover>
  );
}

function AISelectionLevel({children, label, value}: {
  readonly children: ReactNode;
  readonly label: 'provider'|'model'|'thinking';
  readonly value: string;
}) {
  const [open, setOpen] = useState(false);
  return (
    <Popover open={open} onOpenChange={setOpen}>
      <PopoverTrigger asChild>
        <button
          className={cn(
              'inline-flex h-6 min-w-0 max-w-[9rem] shrink items-center gap-0.5 rounded-md panel-chip pl-[7px] pr-[5px]',
              'text-[11px] font-medium tracking-[-0.004em] outline-none',
              'focus-visible:ring-1 focus-visible:ring-ring',
              open && 'bg-surface-selected text-foreground')}
          aria-label={label === 'model' ? 'Default model' : label}
          title={label === 'model' ? 'Default model' : undefined}
          data-ai-selection-level={label}
          type="button">
          <span className="truncate">{value}</span>
          <ChevronDown aria-hidden="true" className="size-2.5 shrink-0 opacity-55" />
        </button>
      </PopoverTrigger>
      <PopoverContent
          align={label === 'thinking' ? 'end' : 'start'}
          aria-label={`${label} options`}
          className="max-h-72 min-w-48 w-auto overflow-y-auto rounded-xl panel-card-raised p-1.5"
          onClick={() => setOpen(false)}
          side="top">
        {children}
      </PopoverContent>
    </Popover>
  );
}

export function AISelectionLevels({handlers, state}: {
  readonly handlers: ComposerHandlers;
  readonly state: AppState;
}) {
  const provider = getSelectedProvider(state);
  const model = getSelectedModel(state, provider);
  const reasoning = getSelectedReasoning(state);
  const itemClassName =
      'flex h-8 w-full items-center justify-between gap-3 rounded-lg px-2.5 text-left text-xs text-foreground hover:bg-surface-hover disabled:opacity-45';
  const commit = (selection: Partial<AISelectionRequest>) => commitAISelection({
    handlers,
    selection: {
      providerId: provider?.id || '',
      modelId: model?.id || '',
      reasoningEffort: reasoning?.effort ?? AI_REASONING_EFFORT.kMedium,
      ...selection,
    },
  });
  return (
    <section aria-label="AI selection" className="flex min-w-0 items-center justify-end gap-px">
      <AISelectionLevel label="provider" value={provider?.label || 'Not configured'}>
        {state.aiSettings.providerOptions.length > 0 ? (
          state.aiSettings.providerOptions.map(option => (
            <button key={option.id} className={itemClassName}
              data-ai-selection-provider={option.id}
              disabled={option.modelOptions.length === 0} type="button"
              onClick={() => commit({providerId: option.id, modelId: option.modelOptions[0]?.id || ''})}>
              <span className="truncate">{option.label}</span>
              <AISelectionCheck active={option.id === provider?.id} />
            </button>
          ))
        ) : (
          <div className="p-2 text-xs text-muted-foreground">No providers configured</div>
        )}
      </AISelectionLevel>
      <ModelSearchLevel
        activeModel={model}
        activeProvider={provider}
        providers={state.aiSettings.providerOptions}
        value={model ? getModelLabel(model) : (provider ? 'Model' : 'Not configured')}
        onSelect={entry => commit({providerId: entry.provider.id, modelId: entry.model.id})}
      />
      <AISelectionLevel label="thinking" value={reasoning?.label || 'Thinking'}>
        {state.aiSettings.reasoningOptions.map(option => (
          <button key={option.effort} className={itemClassName}
            data-ai-selection-reasoning={String(option.effort)} type="button"
            onClick={() => commit({reasoningEffort: option.effort})}>
            <span className="truncate">{option.label}</span>
            <AISelectionCheck active={option.effort === reasoning?.effort} />
          </button>
        ))}
      </AISelectionLevel>
    </section>
  );
}

function commitAISelection(
    request: {
      readonly handlers: ComposerHandlers;
      readonly onSelectionCommitted?: () => void;
      readonly selection: AISelectionRequest;
    }): void {
  void request.handlers.onSetAISelection(request.selection).then(accepted => {
    if (accepted) {
      request.onSelectionCommitted?.();
    } else {
      // The store refreshed the settings, so the picker snapped back; say so
      // instead of dying silently (P1-1).
      toast.error('Model change was not applied — the provider refused it.');
    }
  });
}

export function AISelectionDropdownContent(
    {
      handlers,
      onSelectionCommitted,
      state,
    }: {
      readonly handlers: ComposerHandlers;
      readonly onSelectionCommitted?: () => void;
      readonly state: AppState;
    }) {
  const providerOptions = state.aiSettings.providerOptions;
  const activeProvider = getActiveProvider(state);
  const selectedProvider = getSelectedProvider(state);
  const selectedModel = getSelectedModel(state, selectedProvider);
  const selectedReasoning = getSelectedReasoning(state);

  if (providerOptions.length === 0) {
    return (
      <section aria-label="AI selection" className="grid gap-1.5 p-1">
        <div className={aiSelectionLabelClassName}>AI model</div>
        <AISelectionStatus title="No AI providers available" />
      </section>
    );
  }

  return (
    <section aria-label="AI selection" className="grid gap-2 p-1">
      <div className="grid gap-1">
        <div className={aiSelectionLabelClassName}>Provider</div>
        {providerOptions.map(provider => {
          const firstModel = provider.modelOptions[0] || null;
          const active = activeProvider?.id === provider.id;
          const disabled = active || firstModel === null;
          return (
            <button
              key={provider.id}
              aria-disabled={disabled}
              aria-pressed={active}
              className={cn(aiSelectionRowClassName, active && 'bg-surface-selected')}
              data-ai-selection-provider={provider.id}
              disabled={disabled}
              type="button"
              onClick={() => {
                if (!firstModel || !selectedReasoning) {
                  return;
                }
                commitAISelection({
                  handlers,
                  onSelectionCommitted,
                  selection: buildSelection({
                    model: firstModel,
                    provider,
                    reasoning: selectedReasoning,
                  }),
                });
              }}>
              <span className="min-w-0 truncate font-medium">{provider.label}</span>
              {firstModel ? <AISelectionCheck active={active} /> : (
                <span className="shrink-0 text-[10px] uppercase tracking-[0.08em] text-muted-foreground">
                  No models
                </span>
              )}
            </button>
          );
        })}
      </div>

      <div className="h-px bg-muted" />

      <div className="grid gap-1">
        <div className={aiSelectionLabelClassName}>Default model</div>
        <AISelectionScopeCopy />
        {selectedProvider && selectedProvider.modelOptions.length > 0 ? (
          selectedProvider.modelOptions.map(model => {
            const active = activeProvider?.id === selectedProvider.id &&
                state.aiSettings.activeModelId === model.id;
            return (
              <button
                key={model.id}
                aria-pressed={active}
                className={cn(aiSelectionRowClassName, active && 'bg-surface-selected')}
                data-ai-selection-model={model.id}
                disabled={active || !selectedReasoning}
                type="button"
                onClick={() => {
                  if (!selectedReasoning) {
                    return;
                  }
                  commitAISelection({
                    handlers,
                    onSelectionCommitted,
                    selection: buildSelection({
                      model,
                      provider: selectedProvider,
                      reasoning: selectedReasoning,
                    }),
                  });
                }}>
                <span className="min-w-0 truncate font-medium">{getModelLabel(model)}</span>
                <AISelectionCheck active={active} />
              </button>
            );
          })
        ) : (
          <AISelectionStatus title="Select a provider with models" />
        )}
      </div>

      <div className="h-px bg-muted" />

      <div className="grid gap-1">
        <div className={aiSelectionLabelClassName}>Reasoning</div>
        {state.aiSettings.reasoningOptions.length > 0 ? (
          state.aiSettings.reasoningOptions.map(reasoning => {
            const active = reasoning.effort === state.aiSettings.activeReasoningEffort;
            return (
              <button
                key={reasoning.effort}
                aria-pressed={active}
                className={cn(aiSelectionRowClassName, active && 'bg-surface-selected')}
                data-ai-selection-reasoning={String(reasoning.effort)}
                disabled={active || !selectedProvider || !selectedModel}
                type="button"
                onClick={() => {
                  if (!selectedProvider || !selectedModel) {
                    return;
                  }
                  commitAISelection({
                    handlers,
                    onSelectionCommitted,
                    selection: buildSelection({
                      model: selectedModel,
                      provider: selectedProvider,
                      reasoning,
                    }),
                  });
                }}>
                <span className="min-w-0 truncate font-medium">{reasoning.label}</span>
                <AISelectionCheck active={active} />
              </button>
            );
          })
        ) : (
          <AISelectionStatus title="No reasoning options available" />
        )}
      </div>
    </section>
  );
}

function Favicon({pageUrl}: {pageUrl: string}) {
  if (!pageUrl) return null;
  let src = '';
  try {
    const url = new URL('chrome://favicon2/');
    url.searchParams.set('pageUrl', pageUrl);
    url.searchParams.set('size', '16');
    url.searchParams.set('allowGoogleServerFallback', '0');
    src = url.toString();
  } catch {
    return null;
  }
  return (
    <img
      alt=""
      aria-hidden="true"
      className="size-4 rounded-[3px]"
      height={16}
      src={src}
      width={16}
    />
  );
}

function PickerStatus(
    {
      title,
    }: {
      badge?: string;
      detail?: string;
      title: string;
    }) {
  return (
    <div className={pickerStatusClassName}>
      <span className="flex size-4 items-center justify-center text-muted-foreground">
        <Globe className="size-4" />
      </span>
      <span className="truncate text-xs text-inherit">{title}</span>
    </div>
  );
}

function PickerOptionRow(
    {
      active,
      disabled,
      faviconUrl,
      metaLabel,
      onClick,
      title,
    }: {
      active: boolean;
      detail?: string;
      disabled: boolean;
      faviconUrl?: string;
      metaLabel?: string;
      onClick: () => void;
      title: string;
    }) {
  return (
    <button
      aria-pressed={active}
      data-picker-row="true"
      className={cn(
          pickerRowClassName,
          active && 'bg-surface-selected text-foreground',
          disabled && 'opacity-40')}
      disabled={disabled}
      type="button"
      onClick={onClick}>
      <span className="flex size-4 items-center justify-center text-muted-foreground">
        {faviconUrl ? <Favicon pageUrl={faviconUrl} /> : <Globe className="size-4" />}
      </span>
      <span className="truncate text-xs font-medium text-inherit">{title}</span>
      {metaLabel ? (
        <span className="inline-flex h-5 items-center rounded-full bg-secondary px-2 text-[10px] font-semibold uppercase tracking-[0.08em] text-muted-foreground">
          {metaLabel}
        </span>
      ) : null}
    </button>
  );
}

function TabsSection(
    {
      handlers,
      openTabs,
      readOnly,
      selectedAttachments,
      searchQuery,
    }: {
      handlers: ComposerHandlers;
      openTabs: AppState['openTabs'];
      readOnly: boolean;
      selectedAttachments: ComposerAttachment[];
      searchQuery: string;
    }) {
  const selectedIds = new Set(selectedAttachments.filter(isTabAttachment).map(item => item.id));

  const filteredItems = searchQuery
    ? openTabs.items.filter(tab =>
        tab.title.toLowerCase().includes(searchQuery.toLowerCase()) ||
        tab.url.toLowerCase().includes(searchQuery.toLowerCase())
      )
    : openTabs.items;

  const renderTabRow = (tab: OpenTabRecord) => {
    const attachment = createTabAttachment(tab);
    const attached = selectedIds.has(attachment.id);

    return (
      <PickerOptionRow
        key={attachment.id}
        active={attached}
        disabled={readOnly}
        faviconUrl={tab.url}
        metaLabel={tab.isActive ? 'Active' : undefined}
        title={getOpenTabTitle(tab)}
        onClick={() => handlers.onToggleTabAttachment(attachment)}
      />
    );
  };

  return (
    <div className={pickerSectionClassName}>
      <div className="flex items-center justify-between gap-2 px-1">
        <div className={pickerSectionLabelClassName}>Open tabs</div>
      </div>

      {openTabs.loading ? (
        <PickerStatus title="Loading open tabs…" />
      ) : null}

      {openTabs.error ? (
        <PickerStatus title="Couldn’t load tabs" />
      ) : null}

      {!openTabs.loaded && !openTabs.loading && !openTabs.error ? (
        <button
          aria-label="Load open tabs"
          className={pickerRowClassName}
          data-picker-row="true"
          disabled={readOnly}
          type="button"
          onClick={handlers.onLoadOpenTabs}>
          <span className="flex size-4 items-center justify-center text-muted-foreground"><Globe className="size-4" /></span>
          <span className="truncate text-xs font-medium">Load open tabs</span>
        </button>
      ) : null}

      {openTabs.loaded && !openTabs.loading && !openTabs.error && filteredItems.length === 0 ? (
        <PickerStatus title={searchQuery ? "No tab matches" : "No open tabs"} />
      ) : null}

      {filteredItems.length > 0 ? (
        <div className="grid max-h-[min(240px,36vh)] gap-1 overflow-y-auto pr-1">
          {filteredItems.map(renderTabRow)}
        </div>
      ) : null}
    </div>
  );
}

function HistorySection(
    {
      handlers,
      hasQuery,
      historySearch,
      readOnly,
      selectedAttachments,
    }: {
      handlers: ComposerHandlers;
      hasQuery: boolean;
      historySearch: AppState['historySearch'];
      readOnly: boolean;
      selectedAttachments: ComposerAttachment[];
    }) {
  const selectedIds = new Set(selectedAttachments.filter(isHistoryAttachment).map(item => item.id));

  const renderHistoryRow = (item: HistoryItemRecord) => {
    const attachment = createHistoryAttachment(item);
    const attached = selectedIds.has(attachment.id);

    return (
      <PickerOptionRow
        key={attachment.id}
        active={attached}
        disabled={readOnly}
        faviconUrl={item.url}
        title={getHistoryItemTitle(item)}
        onClick={() => handlers.onToggleHistoryAttachment(attachment)}
      />
    );
  };

  return (
    <div className={pickerSectionClassName}>
      <div className="flex items-center justify-between gap-2 px-1">
        <div className={pickerSectionLabelClassName}>{hasQuery ? 'History results' : 'History'}</div>
      </div>

      {historySearch.loading ? (
        <PickerStatus title="Searching history…" />
      ) : null}

      {historySearch.error ? (
        <PickerStatus title="Couldn’t search history" />
      ) : null}

      {!hasQuery && !historySearch.loading && !historySearch.error ? (
        <PickerStatus title="Search to find history" />
      ) : null}

      {hasQuery && !historySearch.loading && !historySearch.error && historySearch.items.length === 0 ? (
        <PickerStatus title="No history matches" />
      ) : null}

      {historySearch.items.length > 0 ? (
        <div className="grid max-h-[min(240px,36vh)] gap-1 overflow-y-auto pr-1">
          {historySearch.items.map(renderHistoryRow)}
        </div>
      ) : null}
    </div>
  );
}

function PinnedTabsSection(
    {
      handlers,
      openTabs,
      readOnly,
      selectedAttachments,
    }: {
      handlers: ComposerHandlers;
      openTabs: AppState['openTabs'];
      readOnly: boolean;
      selectedAttachments: ComposerAttachment[];
    }) {
  const selectedIds = new Set(selectedAttachments.filter(isTabAttachment).map(item => item.id));
  const pinnedItems = openTabs.items.filter(tab => tab.isPinned);

  const renderPinnedRow = (tab: OpenTabRecord) => {
    const attachment = createTabAttachment(tab);
    const attached = selectedIds.has(attachment.id);
    return (
      <PickerOptionRow
        key={attachment.id}
        active={attached}
        disabled={readOnly}
        faviconUrl={tab.url}
        metaLabel={tab.isActive ? 'Active' : undefined}
        title={getOpenTabTitle(tab)}
        onClick={() => handlers.onToggleTabAttachment(attachment)}
      />
    );
  };

  return (
    <div className={pickerSectionClassName}>
      <div className="flex items-center justify-between gap-2 px-1">
        <div className={pickerSectionLabelClassName}>Pinned tabs</div>
      </div>

      {openTabs.loading ? (
        <PickerStatus title="Loading pinned tabs…" />
      ) : null}

      {openTabs.error ? (
        <PickerStatus title="Couldn’t load pinned tabs" />
      ) : null}

      {openTabs.loaded && !openTabs.loading && !openTabs.error && pinnedItems.length === 0 ? (
        <PickerStatus title="No pinned tabs" />
      ) : null}

      {pinnedItems.length > 0 ? (
        <div className="grid max-h-[min(240px,36vh)] gap-1 overflow-y-auto pr-1">
          {pinnedItems.map(renderPinnedRow)}
        </div>
      ) : null}
    </div>
  );
}

export function Composer(
    {commandDisclosure, handlers, permissionControl, state}: {
      commandDisclosure?: ReactNode;
      handlers: ComposerHandlers;
      permissionControl?: ReactNode;
      state: AppState;
    }) {
  const [pickerOpen, setPickerOpen] = useState(false);

  const [draftSearchQuery, setDraftSearchQuery] = useState(state.historySearch.query);
  const [focusedRowIndex, setFocusedRowIndex] = useState(0);
  const pickerRef = useRef<HTMLDivElement>(null);
  const session = state.currentSessionId ? state.sessionsById[state.currentSessionId] : null;
  const readOnly = isSessionReadOnly(session);
  const currentPageAttached = hasCurrentPageAttachment(state.composer.attachments);
  const canToggleCurrentPage = !readOnly || currentPageAttached;
  const canCancel =
      !readOnly && !!state.currentSessionId &&
      !!state.turnPendingBySessionId[state.currentSessionId];
  const submitDisabled = readOnly || !state.composer.prompt.trim();
  const voiceHidden = readOnly || state.voice.status === 'unsupported';
  const searchQuery = state.historySearch.query.trim();
  const trimmedDraftSearchQuery = draftSearchQuery.trim();
  const hasSearchQuery = searchQuery.length > 0;
  const canSubmitSearch =
      !!trimmedDraftSearchQuery && trimmedDraftSearchQuery !== searchQuery;

  useEffect(() => {
    setDraftSearchQuery(state.historySearch.query);
  }, [state.historySearch.query]);

  useEffect(() => {
    if (!pickerOpen) {
      return;
    }
    setFocusedRowIndex(0);
  }, [pickerOpen, hasSearchQuery, state.openTabs.items.length, state.historySearch.items.length]);

  useEffect(() => {
    if (!pickerOpen || !pickerRef.current) {
      return;
    }
    const rows = pickerRef.current.querySelectorAll<HTMLButtonElement>(
        '[data-picker-row="true"]:not([disabled])');
    rows.forEach((row, index) => {
      if (index === focusedRowIndex) {
        row.setAttribute('data-focused', 'true');
        row.scrollIntoView({block: 'nearest'});
      } else {
        row.removeAttribute('data-focused');
      }
    });
  });

  const handlePickerKeyDown = (event: ReactKeyboardEvent<HTMLDivElement>) => {
    const target = event.target as HTMLElement;
    if (target.matches('input, textarea, select') || target.isContentEditable) {
      return;
    }
    if (!pickerRef.current) {
      return;
    }
    const rows = pickerRef.current.querySelectorAll<HTMLButtonElement>(
        '[data-picker-row="true"]:not([disabled])');
    if (rows.length === 0) {
      return;
    }
    if (event.key === 'ArrowDown') {
      event.preventDefault();
      setFocusedRowIndex(prev => Math.min(prev + 1, rows.length - 1));
    } else if (event.key === 'ArrowUp') {
      event.preventDefault();
      setFocusedRowIndex(prev => Math.max(prev - 1, 0));
    } else if (event.key === 'Enter' &&
        (event.target as HTMLElement)?.tagName !== 'BUTTON') {
      const target = rows[focusedRowIndex];
      if (target) {
        event.preventDefault();
        target.click();
      }
    }
  };

  const handleSearchSubmit = (event: FormEvent<HTMLFormElement>) => {
    event.preventDefault();
    if (!trimmedDraftSearchQuery) {
      if (searchQuery) {
        handlers.onResetHistorySearch();
      }
      return;
    }

    handlers.onSearchHistory(trimmedDraftSearchQuery);
  };

  const quotaExhausted = state.chatBlockedReason === 'quota-exhausted';

  const hasPrompt = state.composer.prompt.trim().length > 0;

  return (
    <section
      className={cn(
        'grid min-w-0 gap-1.5 rounded-[18px] panel-card-raised p-1.5 [container-type:inline-size]',
        'transition-shadow duration-200 focus-within:shadow-[var(--panel-shadow-raised),0_0_0_1px_var(--panel-accent-soft)]',
        readOnly && 'opacity-90')}
      data-composer>
        {state.composer.attachments.length > 0 ? (
          <section
            aria-label="Attached context"
            className="flex min-w-0 flex-1 items-center gap-1.5 overflow-x-auto py-0.5 [scrollbar-width:none] [&::-webkit-scrollbar]:hidden">
            {state.composer.attachments.map(attachment => {
              const isImage = attachment.kind === 'file' &&
                  !!attachment.mimeType?.startsWith('image/') &&
                  !!attachment.dataUrl;
              return (
                <span
                  key={attachment.id}
                  className={cn(
                      'inline-flex h-6 max-w-[min(100%,200px)] shrink-0 items-center gap-1 rounded-md panel-card px-1.5 text-[11px] text-foreground',
                      isImage && 'h-7 pl-0.5 pr-1.5')}
                  title={attachment.detail || attachment.label}>
                  {isImage ? (
                    <img
                      alt=""
                      aria-hidden="true"
                      className="size-6 rounded-[5px] object-cover"
                      src={attachment.dataUrl}
                    />
                  ) : null}
                  <span className="truncate font-medium text-inherit">{attachment.label}</span>
                  <button
                    aria-label={`Remove ${attachment.label}`}
                    className="inline-flex size-4 shrink-0 items-center justify-center rounded-full text-muted-foreground transition-colors hover:bg-background/80 hover:text-foreground focus-visible:bg-background/80 focus-visible:outline-none focus-visible:ring-1 focus-visible:ring-ring"
                    type="button"
                    onClick={() => handlers.onRemoveAttachment(attachment)}>
                    <svg aria-hidden="true" fill="none" focusable="false" height="10" stroke="currentColor" strokeLinecap="round" strokeLinejoin="round" strokeWidth="2" viewBox="0 0 24 24" width="10" xmlns="http://www.w3.org/2000/svg">
                      <line x1="18" y1="6" x2="6" y2="18" />
                      <line x1="6" y1="6" x2="18" y2="18" />
                    </svg>
                  </button>
                </span>
              );
            })}
          </section>
        ) : null}
      {quotaExhausted && (
        <Alert variant="warning" className="mb-0 rounded-xl border-warning/40 bg-warning/10 py-2">
          <AlertCircle className="h-4 w-4 text-warning" />
          <AlertDescription className="flex items-center gap-2 text-xs">
            <span>Out of credits.</span>
            <Button
              variant="link"
              size="sm"
              className="h-auto p-0 text-xs text-current underline"
              onClick={() => window.open('chrome://maho-settings?pane=billing', '_blank')}>
              Manage in Settings
            </Button>
          </AlertDescription>
        </Alert>
      )}
      <div className="flex min-w-0 items-end gap-1">
        <Popover
          open={pickerOpen}
          onOpenChange={open => {
            setPickerOpen(open);
            if (open && !state.openTabs.loaded && !state.openTabs.loading) {
              handlers.onLoadOpenTabs();
            }
          }}>
          <Tooltip>
            <TooltipTrigger asChild>
              <PopoverTrigger asChild>
                <Button
                  aria-label="Add context or start a new session"
                  variant="ghost"
                  size="icon"
                  className="size-7 shrink-0 rounded-lg panel-chip">
                  <Plus className="size-3.5" />
                </Button>
              </PopoverTrigger>
            </TooltipTrigger>
            <TooltipContent>Add context</TooltipContent>
          </Tooltip>
          <PopoverContent
              align="start"
              className="w-[min(22rem,calc(100vw-1.5rem))] rounded-[18px] panel-card-raised p-1.5"
              side="top">
            <section
                ref={pickerRef}
                aria-label="Context picker"
                className="grid gap-1.5"
                onKeyDown={handlePickerKeyDown}>
              <div className="grid gap-1.5">
                <form className="relative grid grid-cols-[minmax(0,1fr)_auto] items-center gap-1" onSubmit={handleSearchSubmit}>
                  <span aria-hidden="true" className="pointer-events-none absolute left-2 top-1/2 -translate-y-1/2 text-muted-foreground">
                    <Search className="size-3.5" />
                  </span>
                  <Input
                    className="h-7 rounded-lg border-panel-hairline bg-transparent pl-7 pr-2 text-xs shadow-none focus-visible:ring-1"
                    disabled={readOnly}
                    placeholder="Search tabs and history"
                    type="search"
                   value={draftSearchQuery}
                   onChange={event => {
                     const nextValue = event.currentTarget.value;
                     setDraftSearchQuery(nextValue);
                     if (!nextValue.trim() && searchQuery) {
                       handlers.onResetHistorySearch();
                     }
                    }}
                  />
                  {canSubmitSearch ? (
                    <Button
                      className="h-7 shrink-0 rounded-lg px-2 text-[11px] font-semibold panel-chip"
                      disabled={readOnly || state.historySearch.loading}
                      type="submit"
                      variant="ghost"
                     size="sm">
                     Search
                   </Button>
                  ) : (hasSearchQuery || trimmedDraftSearchQuery) ? (
                    <Button
                      className="h-7 shrink-0 rounded-lg px-2 text-[11px] font-semibold panel-chip"
                      disabled={readOnly}
                      type="button"
                      variant="ghost"
                     size="sm"
                     onClick={() => {
                       setDraftSearchQuery('');
                       handlers.onResetHistorySearch();
                     }}>
                     Clear
                   </Button>
                 ) : null}
                </form>
              </div>

              <div className="grid gap-1.5">
                <HistorySection
                  handlers={handlers}
                  hasQuery={hasSearchQuery}
                historySearch={state.historySearch}
                readOnly={readOnly}
                selectedAttachments={state.composer.attachments}
              />

              <PinnedTabsSection
                handlers={handlers}
                openTabs={state.openTabs}
                  readOnly={readOnly}
                  selectedAttachments={state.composer.attachments}
                />

                <div className={pickerSectionClassName}>
                  <div className="flex items-center justify-between gap-2 px-1">
                    <div className={pickerSectionLabelClassName}>Current page</div>
                  </div>
                  <PickerOptionRow
                    active={currentPageAttached}
                    disabled={!canToggleCurrentPage}
                    title="Current page"
                    onClick={handlers.onToggleBrowserContext}
                  />
                </div>

                <TabsSection
                  handlers={handlers}
                  openTabs={state.openTabs}
                  readOnly={readOnly}
                  selectedAttachments={state.composer.attachments}
                  searchQuery={searchQuery}
                />

                <div className={pickerSectionClassName}>
                  <div className="flex items-center justify-between gap-2 px-1">
                    <div className={pickerSectionLabelClassName}>Files</div>
                  </div>
                  <button
                    className={cn(
                        pickerRowClassName,
                        readOnly && 'opacity-40')}
                    data-picker-row="true"
                    disabled={readOnly}
                    type="button"
                    onClick={() => {
                      setPickerOpen(false);
                      handlers.onRequestFileChooser();
                    }}>
                    <span className="flex size-4 items-center justify-center text-muted-foreground">
                      <Plus className="size-4" />
                    </span>
                    <span className="truncate text-xs font-medium">Upload file from computer</span>
                  </button>
                </div>
              </div>
              <div className="border-t border-panel-hairline px-1 pt-1.5">
                {commandDisclosure}
              </div>
            </section>
           </PopoverContent>
        </Popover>
        <div className="relative min-w-0 flex-1">
        <ComposerTextarea
          className={cn(
            'max-h-72 min-h-7 resize-none border-0 bg-transparent px-1.5 py-1 text-[13px] leading-[1.5] tracking-[-0.005em] shadow-none placeholder:text-muted-foreground/70 focus-visible:ring-0 overflow-y-auto')}
          focusRequest={state.composer.focusRequest}
          onBlur={handlers.onComposerBlur}
          onPasteFiles={files => handlers.onAttachFiles(files)}
          onSubmit={handlers.onSubmit}
          onValueChange={handlers.onPromptChange}
          placeholder={readOnly ?
            'This archived session is view-only. Start a new live session to continue.' :
            'Ask Maho a task, @ for context'}
          readOnly={readOnly}
          rows={1}
          value={state.composer.prompt}
        />
        </div>
        {!voiceHidden ? (
          <Tooltip>
            <TooltipTrigger asChild>
              <Button
                aria-label="Start voice input"
                variant="ghost"
                size="icon"
                className="size-7 shrink-0 rounded-lg panel-chip"
                disabled={canCancel || state.voice.active}
                onClick={handlers.onOpenVoice}>
                <Mic className="size-3.5" />
              </Button>
            </TooltipTrigger>
            <TooltipContent>Voice input</TooltipContent>
          </Tooltip>
        ) : null}

        <div className="ml-auto flex min-w-0 shrink items-center gap-1">
          <Tooltip>
            <TooltipTrigger asChild>
              <Button
                aria-label={canCancel ?
                   'Stop current turn' :
                   (state.currentSessionId ? 'Send prompt' : 'Start session')}
                className={cn(
                    'size-7 shrink-0 rounded-lg transition-all duration-200 active:scale-95',
                    canCancel
                        ? 'border border-warning/40 bg-warning/15 text-warning hover:bg-warning/25 hover:text-warning'
                        : hasPrompt
                            ? 'bg-panel-accent text-white shadow-[0_0_0_3px_var(--panel-accent-soft)] hover:brightness-110'
                            : 'bg-transparent text-muted-foreground/60 shadow-none hover:bg-surface-hover')}
                disabled={canCancel ? false : submitDisabled}
                variant="ghost"
                size="icon"
                onClick={canCancel ? handlers.onCancel : handlers.onSubmit}>
                <span className="inline-flex items-center justify-center">
                  {canCancel ? <Square className="size-2.5 fill-current" /> : <ArrowUp className="size-3.5" />}
                </span>
              </Button>
            </TooltipTrigger>
            <TooltipContent>
              {canCancel ? 'Stop' : (state.currentSessionId ? 'Send' : 'Start session')}
            </TooltipContent>
          </Tooltip>
        </div>
      </div>
      <div className="flex min-w-0 items-center gap-1">
        {permissionControl}
        <div className="ml-auto flex min-w-0 shrink items-center">
          <AISelectionLevels handlers={handlers} state={state} />
        </div>
      </div>
    </section>
  );
}
