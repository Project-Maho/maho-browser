import {FormEvent, useEffect, useState} from 'react';

import type {
  AppState,
  ComposerAttachment,
  HistoryItemRecord,
  BookmarkItemRecord,
  OpenTabRecord,
} from '../../../types.js';
import {
  createHistoryAttachment,
  createBookmarkAttachment,
  createTabAttachment,
  getHistoryItemDetail,
  getHistoryItemTitle,
  getBookmarkItemDetail,
  getBookmarkItemTitle,
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
import {Icon} from '../../ui/icon.js';
import {IconButton} from '../../ui/icon-button.js';
import {ComposerTextarea} from '../../ui/textarea.js';

export interface ComposerHandlers {
  onCancel: () => void;
  onLoadOpenTabs: () => void;
  onPromptChange: (value: string) => void;
  onRefreshOpenTabs: () => void;
  onResetHistorySearch: () => void;
  onSearchHistory: (query: string) => void;
  onResetBookmarksSearch: () => void;
  onSearchBookmarks: (query: string) => void;
  onStartSession: () => void;
  onSubmit: () => void;
  onToggleBrowserContext: () => void;
  onToggleHistoryAttachment: (attachment: ComposerAttachment) => void;
  onToggleTabAttachment: (attachment: ComposerAttachment) => void;
  onToggleBookmarkAttachment: (attachment: ComposerAttachment) => void;
}

function PickerStatus(
    {
      badge,
      detail,
      title,
    }: {
      badge: string;
      detail?: string;
      title: string;
    }) {
  return (
    <div className="compact-context-picker__status">
      <span className="compact-context-picker__row-copy">
        <span className="compact-context-picker__row-title">{title}</span>
        {detail ? <span className="compact-context-picker__row-detail">{detail}</span> : null}
      </span>
      <span className="compact-context-picker__badge">{badge}</span>
    </div>
  );
}

function PickerOptionRow(
    {
      active,
      detail,
      disabled,
      metaLabel,
      onClick,
      title,
    }: {
      active: boolean;
      detail: string;
      disabled: boolean;
      metaLabel: string;
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
      <span className="compact-context-picker__row-copy">
        <span className="compact-context-picker__row-title">{title}</span>
        <span className="compact-context-picker__row-detail">{detail}</span>
      </span>
      <span className="compact-context-picker__row-meta">
        <span className="compact-context-picker__badge">{metaLabel}</span>
        <span className={cn(
            'compact-context-picker__badge',
            'compact-context-picker__badge--action',
            active && 'compact-context-picker__badge--active')}>
          {active ? 'Attached' : 'Attach'}
        </span>
      </span>
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
        detail={getOpenTabDetail(tab)}
        disabled={readOnly}
        metaLabel={tab.isActive ? 'Active' : `Tab ${tab.index + 1}`}
        title={getOpenTabTitle(tab)}
        onClick={() => handlers.onToggleTabAttachment(attachment)}
      />
    );
  };

  const statusText =
      openTabs.loading ?
        'Loading…' :
        (openTabs.loaded ? `${filteredItems.length} open` : 'Load to browse');

  return (
    <div className="compact-context-picker__section">
      <div className="compact-context-picker__section-head">
        <div className="compact-context-picker__section-label">Open tabs</div>
        <div className="compact-context-picker__section-actions">
          <span className="compact-context-picker__section-meta">{statusText}</span>
          <Button
            className="compact-context-picker__mini-action"
            disabled={openTabs.loading}
            variant="overflow-item"
            onClick={openTabs.loaded ? handlers.onRefreshOpenTabs : handlers.onLoadOpenTabs}>
            {openTabs.loaded ? 'Refresh' : 'Load'}
          </Button>
        </div>
      </div>

      {openTabs.loading ? (
        <PickerStatus badge="Loading" detail="Fetching live tabs from this window." title="Loading open tabs" />
      ) : null}

      {openTabs.error ? (
        <PickerStatus badge="Unavailable" detail={openTabs.error} title="Couldn’t load tabs" />
      ) : null}

      {openTabs.loaded && !openTabs.loading && !openTabs.error && filteredItems.length === 0 ? (
        <PickerStatus
          badge="Empty"
          detail={searchQuery ? "Try a broader title, domain, or keyword." : "This window did not report any attachable tabs."}
          title={searchQuery ? "No tab matches" : "No open tabs available"}
        />
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
    const visitedLabel = getHistoryVisitedLabel(item);

    return (
      <PickerOptionRow
        key={attachment.id}
        active={attached}
        detail={getHistoryItemDetail(item)}
        disabled={readOnly}
        metaLabel={visitedLabel || 'Visited'}
        title={getHistoryItemTitle(item)}
        onClick={() => handlers.onToggleHistoryAttachment(attachment)}
      />
    );
  };

  const resultCountText =
      historySearch.loading ?
        'Searching…' :
        (hasQuery ? `${historySearch.items.length} found` : 'Search above');

  return (
    <div className="compact-context-picker__section">
      <div className="compact-context-picker__section-head">
        <div className="compact-context-picker__section-label">{hasQuery ? 'History results' : 'History'}</div>
        <span className="compact-context-picker__section-meta">{resultCountText}</span>
      </div>

      {historySearch.loading ? (
        <PickerStatus
          badge="Loading"
          detail={`Pulling matches for “${historySearch.query.trim()}”.`}
          title="Searching history"
        />
      ) : null}

      {historySearch.error ? (
        <PickerStatus badge="Unavailable" detail={historySearch.error} title="Couldn’t search history" />
      ) : null}

      {!hasQuery && !historySearch.loading && !historySearch.error ? (
        <PickerStatus
          badge="Search"
          detail="Use the search field above to attach pages from browsing history."
          title="Search your history"
        />
      ) : null}

      {hasQuery && !historySearch.loading && !historySearch.error && historySearch.items.length === 0 ? (
        <PickerStatus
          badge="Empty"
          detail="Try a broader title, domain, or keyword."
          title="No history matches"
        />
      ) : null}

      {historySearch.items.length > 0 ? (
        <div className="compact-context-picker__results">
          {historySearch.items.map(renderHistoryRow)}
        </div>
      ) : null}
    </div>
  );
}

function BookmarksSection(
    {
      handlers,
      hasQuery,
      bookmarksSearch,
      readOnly,
      selectedAttachments,
    }: {
      handlers: ComposerHandlers;
      hasQuery: boolean;
      bookmarksSearch: AppState['bookmarksSearch'];
      readOnly: boolean;
      selectedAttachments: ComposerAttachment[];
    }) {
  const selectedIds = new Set(
      selectedAttachments.filter(a => a.kind === 'bookmark').map(item => item.id));

  const renderBookmarkRow = (item: BookmarkItemRecord) => {
    const attachment = createBookmarkAttachment(item);
    const attached = selectedIds.has(attachment.id);

    return (
      <PickerOptionRow
        key={attachment.id}
        active={attached}
        detail={getBookmarkItemDetail(item)}
        disabled={readOnly}
        metaLabel="Bookmark"
        title={getBookmarkItemTitle(item)}
        onClick={() => handlers.onToggleBookmarkAttachment(attachment)}
      />
    );
  };

  const resultCountText =
      bookmarksSearch.loading ?
        'Searching…' :
        (hasQuery ? `${bookmarksSearch.items.length} found` : 'Search above');

  return (
    <div className="compact-context-picker__section">
      <div className="compact-context-picker__section-head">
        <div className="compact-context-picker__section-label">
          {hasQuery ? 'Bookmark results' : 'Bookmarks'}
        </div>
        <span className="compact-context-picker__section-meta">{resultCountText}</span>
      </div>

      {bookmarksSearch.loading ? (
        <PickerStatus
          badge="Loading"
          detail={`Pulling matches for “${bookmarksSearch.query.trim()}”.`}
          title="Searching bookmarks"
        />
      ) : null}

      {bookmarksSearch.error ? (
        <PickerStatus badge="Unavailable" detail={bookmarksSearch.error} title="Couldn’t search bookmarks" />
      ) : null}

      {!hasQuery && !bookmarksSearch.loading && !bookmarksSearch.error ? (
        <PickerStatus
          badge="Search"
          detail="Use the search field above to attach pages from bookmarks."
          title="Search your bookmarks"
        />
      ) : null}

      {hasQuery && !bookmarksSearch.loading && !bookmarksSearch.error && bookmarksSearch.items.length === 0 ? (
        <PickerStatus
          badge="Empty"
          detail="Try a broader title, domain, or keyword."
          title="No bookmark matches"
        />
      ) : null}

      {bookmarksSearch.items.length > 0 ? (
        <div className="compact-context-picker__results">
          {bookmarksSearch.items.map(renderBookmarkRow)}
        </div>
      ) : null}
    </div>
  );
}

export function Composer({handlers, state}: {handlers: ComposerHandlers; state: AppState}) {
  const [pickerOpen, setPickerOpen] = useState(false);
  const [draftSearchQuery, setDraftSearchQuery] = useState(state.historySearch.query);
  const session = state.currentSessionId ? state.sessionsById[state.currentSessionId] : null;
  const readOnly = isSessionReadOnly(session);
  const currentPageAttached = hasCurrentPageAttachment(state.composer.attachments);
  const canToggleCurrentPage = !readOnly || currentPageAttached;
  const canCancel =
      !readOnly && !!state.currentSessionId &&
      !!state.turnPendingBySessionId[state.currentSessionId];
  const submitDisabled = readOnly || !state.composer.prompt.trim();
  const submitLabel = state.currentSessionId ? 'Send' : 'Start';
  const searchQuery = state.historySearch.query.trim();
  const trimmedDraftSearchQuery = draftSearchQuery.trim();
  const hasSearchQuery = searchQuery.length > 0;
  const canSubmitSearch =
      !!trimmedDraftSearchQuery && trimmedDraftSearchQuery !== searchQuery;

  useEffect(() => {
    setDraftSearchQuery(state.historySearch.query);
  }, [state.historySearch.query]);

  const handleSearchSubmit = (event: FormEvent<HTMLFormElement>) => {
    event.preventDefault();
    if (!trimmedDraftSearchQuery) {
      if (searchQuery) {
        handlers.onResetHistorySearch();
        handlers.onResetBookmarksSearch();
      }
      return;
    }

    handlers.onSearchHistory(trimmedDraftSearchQuery);
    handlers.onSearchBookmarks(trimmedDraftSearchQuery);
  };

  return (
    <section className={cn(
        'compact-composer',
        'compact-composer--docked',
        readOnly && 'compact-composer--read-only')}>
      <div className="compact-composer__body">
        <ComposerTextarea
          onSubmit={handlers.onSubmit}
          onValueChange={handlers.onPromptChange}
          placeholder={readOnly ?
            'This archived session is view-only. Start a new live session to continue.' :
            'Ask anything about this page…'}
          readOnly={readOnly}
          rows={3}
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
            <div className="compact-context-picker__search-area">
              <div className="compact-context-picker__header">
                <span className="compact-context-picker__badge">
                  {state.composer.attachments.length} attached
                </span>
              </div>

              <form className="compact-context-picker__search-form" onSubmit={handleSearchSubmit}>
                <input
                  className="console-input compact-context-picker__search-input"
                  disabled={readOnly}
                  placeholder="Search tabs, history, and bookmarks"
                  type="search"
                  value={draftSearchQuery}
                  onChange={event => {
                    const nextValue = event.currentTarget.value;
                    setDraftSearchQuery(nextValue);
                    if (!nextValue.trim() && searchQuery) {
                      handlers.onResetHistorySearch();
                      handlers.onResetBookmarksSearch();
                    }
                  }}
                />
                {canSubmitSearch ? (
                  <Button
                    className="compact-context-picker__mini-action"
                    disabled={readOnly || state.historySearch.loading || state.bookmarksSearch.loading}
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
                      handlers.onResetBookmarksSearch();
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

              <BookmarksSection
                handlers={handlers}
                hasQuery={hasSearchQuery}
                bookmarksSearch={state.bookmarksSearch}
                readOnly={readOnly}
                selectedAttachments={state.composer.attachments}
              />

              <div className="compact-context-picker__section">
                <div className="compact-context-picker__section-head">
                  <div className="compact-context-picker__section-label">Current page</div>
                  <span className="compact-context-picker__section-meta">Live snapshot</span>
                </div>
                <PickerOptionRow
                  active={currentPageAttached}
                  detail="Attach the active page snapshot from the browser."
                  disabled={!canToggleCurrentPage}
                  metaLabel="Live"
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
            </div>
          </PopoverContent>
        </Popover>
        {state.composer.attachments.length > 0 ? (
          <div className="compact-composer__attachments" aria-label="Attached context">
            {state.composer.attachments.map(attachment => (
              <span
                key={attachment.id}
                className="compact-composer__attachment-chip"
                title={attachment.detail || attachment.label}>
                <span className="compact-composer__attachment-chip-label">{attachment.label}</span>
              </span>
            ))}
          </div>
        ) : null}
        <Button
          aria-label={canCancel ?
            'Stop current turn' :
            (state.currentSessionId ? 'Send prompt' : 'Start session')}
          className={cn(canCancel && 'compact-composer__send--stop')}
          disabled={canCancel ? false : submitDisabled}
          variant="composer-send"
          onClick={canCancel ? handlers.onCancel : handlers.onSubmit}>
          <span className="compact-composer__send-icon">
            <Icon symbol={canCancel ? 'stop' : 'composerSpark'} />
          </span>
          <span className="compact-composer__send-label">
            {canCancel ? 'Stop' : submitLabel}
          </span>
        </Button>
      </div>
    </section>
  );
}
