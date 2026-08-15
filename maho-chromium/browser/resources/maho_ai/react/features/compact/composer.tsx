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
import {Check, ChevronDown, Globe, Plus, Square, Send, Search, AlertCircle, Mic} from '@icons/lucide';
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
    'group grid w-full grid-cols-[auto_minmax(0,1fr)_auto] items-center gap-3 rounded-xl px-3 py-2 text-left text-sm text-foreground transition-colors outline-none hover:bg-surface-hover focus-visible:bg-surface-selected focus-visible:ring-1 focus-visible:ring-ring data-[focused=true]:bg-surface-selected disabled:cursor-default disabled:opacity-40';
const pickerStatusClassName =
    'grid w-full grid-cols-[auto_minmax(0,1fr)_auto] items-center gap-3 rounded-xl px-3 py-2 text-left text-sm text-muted-foreground';
const pickerSectionClassName = 'grid gap-1.5';
const pickerSectionLabelClassName =
    'px-1 text-[11px] font-semibold uppercase tracking-[0.16em] text-muted-foreground';
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

function AISelectionLevel({children, label, value}: {
  readonly children: ReactNode;
  readonly label: 'provider'|'model'|'thinking';
  readonly value: string;
}) {
  const [open, setOpen] = useState(false);
  return (
    <div className="relative min-w-0 flex-1">
      <button
        aria-expanded={open}
        className="flex h-8 w-full min-w-0 items-center justify-between gap-1 rounded-lg px-2 text-xs font-medium text-muted-foreground outline-none transition-colors hover:bg-surface-hover hover:text-foreground focus-visible:ring-1 focus-visible:ring-ring"
        data-ai-selection-level={label}
        type="button"
        onClick={() => setOpen(!open)}>
        <span className="truncate">{value}</span>
        <ChevronDown aria-hidden="true" className="size-3 shrink-0" />
      </button>
      {open ? (
        <div
          className={cn(
              'absolute bottom-[calc(100%+6px)] z-50 grid max-h-72 min-w-48 gap-1 overflow-y-auto rounded-xl border border-border bg-popover p-1.5 shadow-[var(--shadow-overlay)]',
              label === 'thinking' ? 'right-0' : 'left-0')}
          onClick={() => setOpen(false)}>
          {children}
        </div>
      ) : null}
    </div>
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
      reasoningEffort: reasoning?.effort || AI_REASONING_EFFORT.kMedium,
      ...selection,
    },
  });
  return (
    <section aria-label="AI selection" className="flex min-w-0 items-center gap-0.5">
      <AISelectionLevel label="provider" value={provider?.label || 'Provider'}>
        {state.aiSettings.providerOptions.map(option => (
          <button key={option.id} className={itemClassName}
            data-ai-selection-provider={option.id}
            disabled={option.modelOptions.length === 0} type="button"
            onClick={() => commit({providerId: option.id, modelId: option.modelOptions[0]?.id || ''})}>
            <span className="truncate">{option.label}</span>
            <AISelectionCheck active={option.id === provider?.id} />
          </button>
        ))}
      </AISelectionLevel>
      <div className="h-4 w-px shrink-0 bg-border/70" />
      <AISelectionLevel label="model" value={model ? getModelLabel(model) : 'Model'}>
        {provider?.modelOptions.map(option => (
          <button key={option.id} className={itemClassName}
            data-ai-selection-model={option.id} type="button"
            onClick={() => commit({modelId: option.id})}>
            <span className="truncate">{getModelLabel(option)}</span>
            <AISelectionCheck active={option.id === model?.id} />
          </button>
        ))}
      </AISelectionLevel>
      <div className="h-4 w-px shrink-0 bg-border/70" />
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
        <div className={aiSelectionLabelClassName}>Model</div>
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
      <span className="truncate text-sm text-inherit">{title}</span>
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
      <span className="truncate text-sm font-medium text-inherit">{title}</span>
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
          <span className="truncate text-sm font-medium">Load open tabs</span>
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

export function Composer({handlers, state}: {handlers: ComposerHandlers; state: AppState}) {
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

  return (
    <section className={cn(
        'grid gap-2 rounded-[1.35rem] glass p-2 shadow-[var(--shadow-overlay)]',
        readOnly && 'opacity-90')}>
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
      <div className="min-h-0">
        <ComposerTextarea
          className="min-h-[72px] max-h-80 resize-none border-0 bg-transparent px-2 py-2 text-sm leading-6 shadow-none focus-visible:ring-0"
          focusRequest={state.composer.focusRequest}
          onBlur={handlers.onComposerBlur}
          onPasteFiles={files => handlers.onAttachFiles(files)}
          onSubmit={handlers.onSubmit}
          onValueChange={handlers.onPromptChange}
          placeholder={readOnly ?
            'This archived session is view-only. Start a new live session to continue.' :
            'Ask a question about this page...'}
          readOnly={readOnly}
          rows={1}
          value={state.composer.prompt}
        />
      </div>
      <AISelectionLevels handlers={handlers} state={state} />
      <div className="flex min-w-0 items-center gap-2">
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
                  className="h-8 w-8 shrink-0 rounded-full">
                  <Plus className="size-4" />
                </Button>
              </PopoverTrigger>
            </TooltipTrigger>
            <TooltipContent>Add context</TooltipContent>
          </Tooltip>
          <PopoverContent
              align="start"
              className="w-[min(22rem,calc(100vw-1.5rem))] rounded-2xl border-border bg-popover p-2 shadow-[var(--shadow-overlay)]"
              side="top">
            <section
                ref={pickerRef}
                aria-label="Context picker"
                className="grid gap-2"
                onKeyDown={handlePickerKeyDown}>
              <div className="grid gap-2 p-1">
                <form className="relative grid grid-cols-[minmax(0,1fr)_auto] items-center gap-2" onSubmit={handleSearchSubmit}>
                  <span aria-hidden="true" className="pointer-events-none absolute left-3 top-1/2 -translate-y-1/2 text-muted-foreground">
                    <Search className="size-3.5" />
                  </span>
                  <Input
                    className="h-9 border-border bg-input/90 pl-9 pr-3 text-sm shadow-none focus-visible:ring-1"
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
                      className="h-9 rounded-xl px-3 text-xs font-semibold"
                      disabled={readOnly || state.historySearch.loading}
                      type="submit"
                      variant="ghost"
                     size="sm">
                     Search
                   </Button>
                  ) : (hasSearchQuery || trimmedDraftSearchQuery) ? (
                    <Button
                      className="h-9 rounded-xl px-3 text-xs font-semibold"
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

              <div className="grid gap-2">
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
                    <span className="truncate text-sm font-medium">Upload file from computer</span>
                  </button>
                </div>
              </div>
            </section>
           </PopoverContent>
        </Popover>
        {state.composer.attachments.length > 0 ? (
          <section
            aria-label="Attached context"
            className="flex min-w-0 flex-1 items-center gap-2 overflow-x-auto py-0.5">
            {state.composer.attachments.map(attachment => {
              const isImage = attachment.kind === 'file' &&
                  !!attachment.mimeType?.startsWith('image/') &&
                  !!attachment.dataUrl;
              return (
                <span
                  key={attachment.id}
                  className={cn(
                      'inline-flex h-6 max-w-[min(100%,220px)] items-center gap-1 rounded-full bg-secondary/80 px-2 text-xs text-foreground shadow-sm',
                      isImage && 'h-8 pl-0.5 pr-2')}
                  title={attachment.detail || attachment.label}>
                  {isImage ? (
                    <img
                      alt=""
                      aria-hidden="true"
                      className="size-7 rounded-md object-cover"
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

        <div className="ml-auto flex shrink-0 items-center gap-1">
          {!voiceHidden ? (
            <Tooltip>
              <TooltipTrigger asChild>
                <Button
                  aria-label="Start voice input"
                  variant="ghost"
                  size="icon"
                  className="h-8 w-8 shrink-0 rounded-full"
                  disabled={canCancel || state.voice.active}
                  onClick={handlers.onOpenVoice}>
                  <Mic className="size-4" />
                </Button>
              </TooltipTrigger>
              <TooltipContent>Voice input</TooltipContent>
            </Tooltip>
          ) : null}
          <Tooltip>
            <TooltipTrigger asChild>
              <Button
                aria-label={canCancel ?
                   'Stop current turn' :
                   (state.currentSessionId ? 'Send prompt' : 'Start session')}
                className={cn(
                    'size-9 shrink-0 rounded-full shadow-sm transition-transform active:scale-95',
                    canCancel && 'border border-warning/40 bg-warning/15 text-warning hover:bg-warning/25 hover:text-warning')}
                disabled={canCancel ? false : submitDisabled}
                variant="default"
                size="icon"
                onClick={canCancel ? handlers.onCancel : handlers.onSubmit}>
                <span className="inline-flex items-center justify-center">
                  {canCancel ? <Square className="size-3 fill-current" /> : <Send className="size-4" />}
                </span>
              </Button>
            </TooltipTrigger>
            <TooltipContent>
              {canCancel ? 'Stop' : (state.currentSessionId ? 'Send' : 'Start session')}
            </TooltipContent>
          </Tooltip>
        </div>
      </div>
    </section>
  );
}
