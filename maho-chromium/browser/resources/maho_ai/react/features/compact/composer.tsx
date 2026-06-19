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
  getHistoryItemDetail,
  getHistoryItemTitle,
  getHistoryVisitedLabel,
  getOpenTabDetail,
  getOpenTabTitle,
  hasCurrentPageAttachment,
  isHistoryAttachment,
  isTabAttachment,
  isSessionReadOnly,
} from '../../../types.js';
import {cn} from '../../lib/cn.js';
import {Button} from '../../ui/button.js';
import {
  Popover,
  PopoverContent,
  PopoverTrigger,
} from '../../ui/popover.js';
import {
  DropdownMenu,
  DropdownMenuContent,
  DropdownMenuItem,
  DropdownMenuLabel,
  DropdownMenuSeparator,
  DropdownMenuTrigger,
} from '../../ui/dropdown-menu.js';
import {Icon} from '../../ui/icon.js';
import type {IconSymbol} from '../../ui/icon.js';
import {IconButton} from '../../ui/icon-button.js';
import {ComposerTextarea} from '../../ui/textarea.js';

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
      className="compact-context-picker__favicon"
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
    <div className="compact-context-picker__status">
      <span className="compact-context-picker__row-icon">
        <Icon symbol="globe" />
      </span>
      <span className="compact-context-picker__row-title">{title}</span>
    </div>
  );
}

function PickerOptionRow(
    {
      active,
      disabled,
      faviconUrl,
      iconSymbol,
      metaLabel,
      onClick,
      title,
    }: {
      active: boolean;
      detail?: string;
      disabled: boolean;
      faviconUrl?: string;
      iconSymbol?: IconSymbol;
      metaLabel?: string;
      onClick: () => void;
      title: string;
    }) {
  return (
    <button
      aria-pressed={active}
      className={cn(
          'compact-overflow-menu__item',
          'compact-context-picker__row',
          active && 'compact-context-picker__row--active',
          disabled && 'compact-context-picker__row--muted')}
      disabled={disabled}
      type="button"
      onClick={onClick}>
      <span className="compact-context-picker__row-icon">
        {faviconUrl ? <Favicon pageUrl={faviconUrl} /> : <Icon symbol={iconSymbol ?? 'globe'} />}
      </span>
      <span className="compact-context-picker__row-title">{title}</span>
      {metaLabel ? (
        <span className="compact-context-picker__badge">{metaLabel}</span>
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
    <div className="compact-context-picker__section">
      <div className="compact-context-picker__section-head">
        <div className="compact-context-picker__section-label">Open tabs</div>
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
          className="compact-context-picker__row"
          disabled={readOnly}
          type="button"
          onClick={handlers.onLoadOpenTabs}>
          <span className="compact-context-picker__row-icon"><Icon symbol="globe" /></span>
          <span className="compact-context-picker__row-title">Load open tabs</span>
        </button>
      ) : null}

      {openTabs.loaded && !openTabs.loading && !openTabs.error && filteredItems.length === 0 ? (
        <PickerStatus title={searchQuery ? "No tab matches" : "No open tabs"} />
      ) : null}

      {filteredItems.length > 0 ? (
        <div className="compact-context-picker__results">
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
    <div className="compact-context-picker__section">
      <div className="compact-context-picker__section-head">
        <div className="compact-context-picker__section-label">{hasQuery ? 'History results' : 'History'}</div>
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
        <div className="compact-context-picker__results">
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
    <div className="compact-context-picker__section">
      <div className="compact-context-picker__section-head">
        <div className="compact-context-picker__section-label">Pinned tabs</div>
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
        <div className="compact-context-picker__results">
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
        '.compact-context-picker__row:not([disabled])');
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
        '.compact-context-picker__row:not([disabled])');
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

  return (
    <section className={cn(
        'compact-composer',
        'compact-composer--docked',
        readOnly && 'compact-composer--read-only')}>
      <div className="compact-composer__body">
        <ComposerTextarea
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
      <div className="compact-composer__toolbar">
        <Popover
          open={pickerOpen}
          onOpenChange={open => {
            setPickerOpen(open);
            if (open && !state.openTabs.loaded && !state.openTabs.loading) {
              handlers.onLoadOpenTabs();
            }
          }}>
          <PopoverTrigger asChild>
            <IconButton
              ariaLabel="Add context or start a new session"
              symbol="plus"
              variant="composer-icon"
            />
          </PopoverTrigger>
          <PopoverContent
             align="start"
             className="compact-context-picker"
             side="top">
             <section
               ref={pickerRef}
               aria-label="Context picker"
               onKeyDown={handlePickerKeyDown}>
              <div className="compact-context-picker__search-area">
               <form className="compact-context-picker__search-form" onSubmit={handleSearchSubmit}>
                 <span aria-hidden="true" className="compact-context-picker__search-icon">
                  <svg aria-hidden="true" fill="none" focusable="false" height="14" stroke="currentColor" strokeLinecap="round" strokeLinejoin="round" strokeWidth="1.5" viewBox="0 0 24 24" width="14" xmlns="http://www.w3.org/2000/svg">
                    <circle cx="11" cy="11" r="7" />
                    <path d="m20 20-3.5-3.5" />
                  </svg>
                </span>
                <input
                  className="console-input compact-context-picker__search-input"
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
                     className="compact-context-picker__mini-action"
                     disabled={readOnly || state.historySearch.loading}
                     type="submit"
                     variant="overflow-item">
                     Search
                   </Button>
                 ) : (hasSearchQuery || trimmedDraftSearchQuery) ? (
                   <Button
                     className="compact-context-picker__mini-action"
                     disabled={readOnly}
                     type="button"
                     variant="overflow-item"
                     onClick={() => {
                       setDraftSearchQuery('');
                       handlers.onResetHistorySearch();
                     }}>
                     Clear
                   </Button>
                 ) : null}
               </form>
             </div>

            <div className="compact-context-picker__menu">
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

              <div className="compact-context-picker__section">
                <div className="compact-context-picker__section-head">
                  <div className="compact-context-picker__section-label">Current page</div>
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

              <div className="compact-context-picker__section">
                <div className="compact-context-picker__section-head">
                  <div className="compact-context-picker__section-label">Files</div>
                </div>
                <button
                  className={cn(
                      'compact-context-picker__row',
                      readOnly && 'compact-context-picker__row--muted')}
                  disabled={readOnly}
                  type="button"
                  onClick={() => {
                    setPickerOpen(false);
                    handlers.onRequestFileChooser();
                  }}>
                  <span className="compact-context-picker__row-icon">
                    <Icon symbol="plus" />
                  </span>
                  <span className="compact-context-picker__row-title">Upload file from computer</span>
                </button>
             </div>
            </div>
            </section>
          </PopoverContent>
        </Popover>
        {state.composer.attachments.length > 0 ? (
          <section className="compact-composer__attachments" aria-label="Attached context">
            {state.composer.attachments.map(attachment => {
              const isImage = attachment.kind === 'file' &&
                  !!attachment.mimeType?.startsWith('image/') &&
                  !!attachment.dataUrl;
              return (
                <span
                  key={attachment.id}
                  className={cn(
                      'compact-composer__attachment-chip',
                      isImage && 'compact-composer__attachment-chip--image')}
                  title={attachment.detail || attachment.label}>
                  {isImage ? (
                    <img
                      alt=""
                      aria-hidden="true"
                      className="compact-composer__attachment-thumb"
                      src={attachment.dataUrl}
                    />
                  ) : null}
                  <span className="compact-composer__attachment-chip-label">{attachment.label}</span>
                  <button
                    aria-label={`Remove ${attachment.label}`}
                    className="compact-composer__attachment-chip-remove"
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
                <button type="button" className="compact-composer__model-selector-btn" aria-label="Select model">
                  <span className="compact-composer__model-name">
                    {activeModel ? activeModel.split('/').pop() : 'Model'}
                  </span>
                  <span className="compact-composer__model-selector-arrow">▾</span>
                </button>
              </DropdownMenuTrigger>
              <DropdownMenuContent align="end" side="top">
                {providers.length === 0 ? (
                  <div className="compact-overflow-menu__empty">No models configured</div>
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
        <Button
          aria-label={canCancel ?
            'Stop current turn' :
            (state.currentSessionId ? 'Send prompt' : 'Start session')}
          className={cn(canCancel && 'compact-composer__send--stop')}
          disabled={canCancel ? false : submitDisabled}
          variant="composer-send"
          onClick={canCancel ? handlers.onCancel : handlers.onSubmit}>
          <span className="compact-composer__send-icon">
            <Icon symbol={canCancel ? 'stop' : 'send'} />
          </span>
        </Button>
      </div>
    </section>
  );
}
