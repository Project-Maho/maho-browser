import React, {FormEvent, KeyboardEvent as ReactKeyboardEvent, useEffect, useRef, useState} from 'react';

import type {
  AppState,
  ComposerAttachment,
  HistoryItemRecord,
  OpenTabRecord,
} from '../../../types.js';
import {
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
  DropdownMenu,
  DropdownMenuContent,
  DropdownMenuItem,
  DropdownMenuLabel,
  DropdownMenuSeparator,
  DropdownMenuTrigger,
} from '@ui/dropdown-menu';
import {
  Tooltip,
  TooltipContent,
  TooltipTrigger,
} from '@ui/tooltip';
import {Globe, Plus, Square, Send, Search, AlertCircle, Mic} from '@icons/lucide';
import {Alert, AlertDescription} from '@ui/alert';
import {ComposerTextarea} from '../../components/composer-textarea.js';

export interface ComposerHandlers {
  onCancel: () => void;
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
  onSetActiveAI: (providerId: string, modelId: string) => Promise<void>;
  onOpenSettings: () => void;
  onOpenVoice: () => void;
}

const pickerRowClassName =
    'group grid w-full grid-cols-[auto_minmax(0,1fr)_auto] items-center gap-3 rounded-xl px-3 py-2 text-left text-sm text-foreground transition-colors outline-none hover:bg-accent/60 focus-visible:bg-accent/70 focus-visible:ring-1 focus-visible:ring-ring data-[focused=true]:bg-accent/70 disabled:cursor-default disabled:opacity-40';
const pickerStatusClassName =
    'grid w-full grid-cols-[auto_minmax(0,1fr)_auto] items-center gap-3 rounded-xl px-3 py-2 text-left text-sm text-muted-foreground';
const pickerSectionClassName = 'grid gap-1.5';
const pickerSectionLabelClassName =
    'px-1 text-[11px] font-semibold uppercase tracking-[0.16em] text-muted-foreground';

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
          active && 'bg-accent/40 text-foreground',
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
        'grid gap-2 rounded-[1.35rem] border border-border bg-card/95 p-2 shadow-[0_24px_80px_-48px_var(--color-accent-glow)] backdrop-blur-xl supports-[backdrop-filter]:bg-card/90',
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
          onPasteFiles={files => handlers.onAttachFiles(files)}
          onSubmit={handlers.onSubmit}
          onValueChange={handlers.onPromptChange}
          placeholder={readOnly ?
            'This archived session is view-only. Start a new live session to continue.' :
            'Ask anything about this page…'}
          readOnly={readOnly}
          rows={1}
          value={state.composer.prompt}
        />
      </div>
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
              className="w-[min(22rem,calc(100vw-1.5rem))] rounded-2xl border-border bg-popover/95 p-2 shadow-2xl backdrop-blur-xl"
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
        {(() => {
          const aiSettings = state.aiSettings;
          const allProviders = state.aiProviders || [];
          const activeProvider = allProviders.find(p => p.id === aiSettings?.activeProviderId);
          const providers = activeProvider ? [activeProvider] : [];
          const activeModel = aiSettings?.activeModelId;
          return (
            <DropdownMenu>
              <DropdownMenuTrigger asChild>
                <Button
                  type="button"
                  aria-label="Select model"
                  className="ml-auto h-8 max-w-40 shrink-0 rounded-full border-border bg-secondary/70 px-3 text-xs font-medium text-muted-foreground shadow-none hover:bg-accent/60 hover:text-foreground"
                  size="sm"
                  variant="outline">
                  <span className="truncate">
                    {activeModel ? activeModel.split('/').pop() : 'Model'}
                  </span>
                  <span className="shrink-0 text-[10px] text-muted-foreground">▾</span>
                </Button>
              </DropdownMenuTrigger>
              <DropdownMenuContent align="end" side="top">
                {providers.length === 0 ? (
                  <>
                    {activeModel ? (
                      <DropdownMenuLabel>
                        <span className="text-muted-foreground">Current: </span>
                        {activeModel.split('/').pop() || activeModel}
                      </DropdownMenuLabel>
                    ) : null}
                    <DropdownMenuItem
                      onSelect={() => handlers.onOpenSettings()}>
                      Configure in Settings →
                    </DropdownMenuItem>
                  </>
                ) : (
                  providers.map((provider, providerIdx) => (
                    <React.Fragment key={provider.id}>
                      {providerIdx > 0 && <DropdownMenuSeparator />}
                      <DropdownMenuLabel>{provider.displayName}</DropdownMenuLabel>
                      {provider.models.map(model => (
                        <DropdownMenuItem
                          key={model}
                          disabled={aiSettings?.activeProviderId === provider.id && aiSettings?.activeModelId === model}
                          onSelect={() => handlers.onSetActiveAI(provider.id, model)}>
                          {model.split('/').pop() || model}
                          {aiSettings?.activeProviderId === provider.id && aiSettings?.activeModelId === model && ' ✓'}
                        </DropdownMenuItem>
                      ))}
                    </React.Fragment>
                  ))
                )}
              </DropdownMenuContent>
            </DropdownMenu>
          );
        })()}
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
    </section>
  );
}
