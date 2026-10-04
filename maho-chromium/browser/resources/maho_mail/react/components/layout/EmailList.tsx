import React, { useCallback, useEffect, useMemo, useRef, useState } from "react";
import { useVirtualizer } from "@tanstack/react-virtual";
import {
  Archive,
  Mail,
  MailOpen,
  Star,
  Paperclip,
  Search,
  Sparkles,
  Loader2,
  Trash2,
  CheckCircle2,
  Pin,
  Clock,
  CheckSquare,
  Square,
  X,
  Bookmark,
  PenLine,
  RefreshCw,
} from "lucide-react";
import { EmptyState } from "../common/EmptyState";
import { LiveRegion } from "../common/LiveRegion";
import { ContextMenu } from "../common/ContextMenu";
import { SwipeableRow } from "../common/SwipeableRow";
import { DraggableEmail } from "../common/DragDrop";
import { AdvancedSearchPanel, type SearchFilters } from "../common/AdvancedSearchPanel";
import { SearchAutocomplete } from "../common/SearchAutocomplete";
import * as api from "../../api";
import type { EmailSummary, SearchResult, SmartCategory } from "../../types";
import { useToast } from "../ui/Toast";
import { useConfirm } from "../ui/ConfirmDialog";
import { Avatar } from "../ui/Avatar";
import { Badge } from "../ui/Badge";
import { EmailListSkeleton } from "../ui/Skeleton";
import { Select } from "../ui/Select";
import { formatRelativeDate } from "../ui/utils";
import { useTranslation } from "react-i18next";
import { useIsMobile } from "../../utils/platform";
import { cn, getErrorMessage } from "../../lib/utils";
import { usePullToRefresh } from "../../hooks/usePullToRefresh";
import { useLongPress } from "../../hooks/useLongPress";
import { useVirtualKeyboard } from "../../hooks/useVirtualKeyboard";
import { emitMailUpdated } from "../../hooks/usePopoutWindow";

type SearchScope = "current" | "folder" | "all";

const SEARCH_HISTORY_STORAGE_KEY = "maho-search-history";
const SEARCH_HISTORY_LIMIT = 8;

interface SearchScopeOption {
  value: SearchScope;
  label: string;
}

interface RecentSearchEntry {
  query: string;
  filters: SearchFilters;
}

interface SearchTip {
  id: string;
  label: string;
  description: string;
  query?: string;
  filters?: SearchFilters;
}

const HIGHLIGHT_CLASS_NAME = "rounded-sm bg-primary/15 px-0.5 text-foreground";

function isAuthRelatedError(error: unknown) {
  const message = getErrorMessage(error, "");
  return message.includes("Auth error") || message.includes("OAuth2") || message.includes("access token");
}

type EnqueueRetry = (accountId: string, action: () => Promise<void>, label: string) => void;

function escapeRegExp(value: string) {
  return value.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
}

function highlightText(text: string, query: string) {
  if (!text || !query) {
    return text;
  }

  const matches = Array.from(text.matchAll(new RegExp(escapeRegExp(query), "gi")));
  if (matches.length === 0) {
    return text;
  }

  const parts: React.ReactNode[] = [];
  let lastIndex = 0;

  matches.forEach((match, index) => {
    const start = match.index ?? 0;
    const matchedText = match[0];
    const end = start + matchedText.length;

    if (start > lastIndex) {
      parts.push(text.slice(lastIndex, start));
    }

    parts.push(
      <span key={`${matchedText}-${start}-${index}`} className={HIGHLIGHT_CLASS_NAME}>
        {matchedText}
      </span>,
    );

    lastIndex = end;
  });

  if (lastIndex < text.length) {
    parts.push(text.slice(lastIndex));
  }

  return parts;
}

function formatSearchCount(totalCount: number) {
  return `${totalCount.toLocaleString()} result${totalCount === 1 ? "" : "s"}`;
}

function removeEmailsFromSearchResult(current: SearchResult | null, emailIds: Iterable<string>) {
  if (!current) {
    return current;
  }

  const idsToRemove = new Set(emailIds);
  const nextEmails = current.emails.filter((email) => !idsToRemove.has(email.id));
  const removedCount = current.emails.length - nextEmails.length;

  return {
    ...current,
    emails: nextEmails,
    total_count: Math.max(0, current.total_count - removedCount),
  };
}

function getSearchScopeOptions(accountId?: string, folderId?: string): SearchScopeOption[] {
  const options: SearchScopeOption[] = [{ value: "current", label: "Current view" }];

  if (folderId) {
    options.push({ value: "folder", label: "This folder" });
  }

  if (accountId || folderId) {
    options.push({ value: "all", label: "All mail" });
  }

  return options;
}

function getSearchScopeLabel(scope: SearchScope, accountId?: string, folderId?: string) {
  switch (scope) {
    case "folder":
      return folderId ? "This folder" : "Current view";
    case "all":
      return accountId || folderId ? "All mail" : "Current view";
    case "current":
    default:
      return folderId ? "Current folder" : accountId ? "Current account" : "Current view";
  }
}

function resolveSearchScope(scope: SearchScope, accountId?: string, folderId?: string) {
  switch (scope) {
    case "folder":
      return { accountId, folderId };
    case "all":
      return { accountId: undefined, folderId: undefined };
    case "current":
    default:
      return { accountId, folderId };
  }
}

function sanitizeSearchFilters(filters: SearchFilters): SearchFilters {
  const next: SearchFilters = {};

  if (filters.from?.trim()) {
    next.from = filters.from.trim();
  }
  if (filters.to?.trim()) {
    next.to = filters.to.trim();
  }
  if (filters.hasAttachment) {
    next.hasAttachment = true;
  }
  if (filters.isUnread) {
    next.isUnread = true;
  }
  if (filters.isStarred) {
    next.isStarred = true;
  }
  if (filters.dateFrom?.trim()) {
    next.dateFrom = filters.dateFrom.trim();
  }
  if (filters.dateTo?.trim()) {
    next.dateTo = filters.dateTo.trim();
  }

  return next;
}

function hasActiveSearchFilters(filters: SearchFilters) {
  return Object.keys(sanitizeSearchFilters(filters)).length > 0;
}

function getSearchMode(query: string) {
  const trimmed = query.trim();
  const naturalLanguage = trimmed.startsWith("?");
  const normalized = naturalLanguage ? trimmed.slice(1).trim() : trimmed;
  return { naturalLanguage, normalized };
}

function normalizeSearchQuery(query: string) {
  const { naturalLanguage, normalized } = getSearchMode(query);

  if (!normalized) {
    return "";
  }

  return naturalLanguage ? `? ${normalized}` : normalized;
}

function isRecentSearchEntry(value: unknown): value is RecentSearchEntry {
  if (!value || typeof value !== "object") {
    return false;
  }

  const candidate = value as { query?: unknown; filters?: unknown };

  return typeof candidate.query === "string" && candidate.filters != null && typeof candidate.filters === "object";
}

function loadRecentSearches(): RecentSearchEntry[] {
  try {
    const rawValue = localStorage.getItem(SEARCH_HISTORY_STORAGE_KEY);
    if (!rawValue) {
      return [];
    }

    const parsed = JSON.parse(rawValue) as unknown;
    if (!Array.isArray(parsed)) {
      return [];
    }

    return parsed
      .filter(isRecentSearchEntry)
      .map((entry) => ({
        query: normalizeSearchQuery(entry.query),
        filters: sanitizeSearchFilters(entry.filters),
      }))
      .filter((entry) => entry.query.length > 0 || hasActiveSearchFilters(entry.filters))
      .slice(0, SEARCH_HISTORY_LIMIT);
  } catch {
    return [];
  }
}

function areRecentSearchesEqual(a: RecentSearchEntry, b: RecentSearchEntry) {
  return a.query === b.query && JSON.stringify(a.filters) === JSON.stringify(b.filters);
}

function buildRecentSearchEntry(query: string, filters: SearchFilters): RecentSearchEntry {
  return {
    query: normalizeSearchQuery(query),
    filters: sanitizeSearchFilters(filters),
  };
}

function formatFilterSummary(filters: SearchFilters) {
  const parts: string[] = [];

  if (filters.from) {
    parts.push(`From: ${filters.from}`);
  }
  if (filters.to) {
    parts.push(`To: ${filters.to}`);
  }
  if (filters.hasAttachment) {
    parts.push("Attachment");
  }
  if (filters.isUnread) {
    parts.push("Unread");
  }
  if (filters.isStarred) {
    parts.push("Starred");
  }
  if (filters.dateFrom) {
    parts.push(`After ${filters.dateFrom}`);
  }
  if (filters.dateTo) {
    parts.push(`Before ${filters.dateTo}`);
  }

  return parts;
}

function formatRecentSearchLabel(entry: RecentSearchEntry) {
  const filterSummary = formatFilterSummary(entry.filters);

  if (entry.query && filterSummary.length > 0) {
    return `${entry.query} • ${filterSummary.join(" • ")}`;
  }

  if (entry.query) {
    return entry.query;
  }

  return filterSummary.join(" • ");
}

function getRelativeDateIso(daysAgo: number) {
  const date = new Date();
  date.setHours(0, 0, 0, 0);
  date.setDate(date.getDate() - daysAgo);
  return date.toISOString().slice(0, 10);
}

const SEARCH_OPERATORS = [
  { prefix: "from:", example: "from:user@example.com" },
  { prefix: "to:", example: "to:user@example.com" },
  { prefix: "has:", example: "has:attachment" },
  { prefix: "is:", example: "is:unread" },
  { prefix: "before:", example: "before:2024-01-01" },
  { prefix: "after:", example: "after:2024-01-01" },
  { prefix: "subject:", example: "subject:meeting" },
  { prefix: "in:", example: "in:inbox" },
] as const;

function detectActiveOperators(query: string): string[] {
  return SEARCH_OPERATORS
    .filter((op) => query.toLowerCase().includes(op.prefix))
    .map((op) => op.prefix.slice(0, -1));
}

const SEARCH_TIPS: SearchTip[] = [
  {
    id: "from",
    label: "From",
    description: "Prefill the sender filter with an address you can replace.",
    filters: { from: "alice@example.com" },
  },
  {
    id: "to",
    label: "To",
    description: "Use the recipient filter when you know who the message was sent to.",
    filters: { to: "team@example.com" },
  },
  {
    id: "attachment",
    label: "Attachment",
    description: "Show only messages that include files.",
    filters: { hasAttachment: true },
  },
  {
    id: "unread",
    label: "Unread",
    description: "Surface messages that still need attention.",
    filters: { isUnread: true },
  },
  {
    id: "phrase",
    label: "Exact phrase",
    description: "Wrap a phrase in quotes to keep the words together.",
    query: '"quarterly review"',
  },
  {
    id: "date",
    label: "Date filters",
    description: "Try a recent date range with the existing advanced filters.",
    filters: { dateFrom: getRelativeDateIso(7) },
  },
];

interface EmailListProps {
  archiveFolders?: {id: string; account_id: string}[];
  onEmailsRemoved?: (ids: string[]) => void;
  searchRequest?: {query: string} | null;
  emails: EmailSummary[];
  selectedEmailId: string | null;
  onSelectEmail: (id: string) => void;
  onToggleStar: (id: string) => void;
  loading: boolean;
  accountId?: string;
  folderId?: string;
  archiveFolderId?: string;
  onLoadMore?: () => void;
  hasMore?: boolean;
  onPin?: (id: string, nextPinned: boolean) => void | Promise<void>;
  onSnooze?: (id: string) => void;
  onReply?: (id: string) => void;
  onForward?: (id: string) => void;
  smartCategory?: SmartCategory | null;
  onSmartCategoryChange?: (category: SmartCategory) => void;
  density?: 'comfortable' | 'compact';
  mailboxTitle?: string;
  onCompose?: () => void;
  onSync?: () => void;
  composeDisabled?: boolean;
  syncBusy?: boolean;
  syncError?: string | null;
  enqueueRetry?: EnqueueRetry;
}

interface EmailRowProps {
  email: EmailSummary;
  isPinned: boolean;
  selectedEmailId: string | null;
  selectedIds: Set<string>;
  batchMode: boolean;
  isMobile: boolean;
  highlightQuery?: string;
  onSelectEmail: (id: string) => void;
  onToggleStar: (id: string) => void;
  archiveFolderId?: string;
  folderId?: string;
  onPin?: (id: string, nextPinned: boolean) => void | Promise<void>;
  onSnooze?: (id: string) => void;
  onReply?: (id: string) => void;
  onForward?: (id: string) => void;
  onArchive: (id: string) => void;
  onDelete: (id: string) => void;
  onToggleRead: (email: EmailSummary) => void;
  toggleSelection: (id: string) => void;
  enterSelectionMode: (id: string) => void;
  density?: 'comfortable' | 'compact';
}

function handleRowKeyDown(
  event: React.KeyboardEvent<HTMLDivElement>,
  emailId: string,
  onSelectEmail: (id: string) => void,
) {
  if (event.key === "Enter" || event.key === " ") {
    event.preventDefault();
    onSelectEmail(emailId);
  }
}

const EmailRow = React.memo(function EmailRow({
  email,
  isPinned,
  selectedEmailId,
  selectedIds,
  batchMode,
  isMobile,
  highlightQuery,
  onSelectEmail,
  onToggleStar,
  archiveFolderId,
  folderId,
  onPin,
  onSnooze,
  onReply,
  onForward,
  onArchive,
  onDelete,
  onToggleRead,
  toggleSelection,
  enterSelectionMode,
  density = 'comfortable',
}: EmailRowProps) {
  const isSelected = selectedIds.has(email.id);
  const senderText = email.from_name ?? email.from_address;
  const actionButtonClassName = cn(
    "mail-pressable flex items-center justify-center rounded-md text-muted-foreground transition-colors focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-primary",
    isMobile ? "h-11 w-11" : "h-7 w-7",
  );
  const rowSpacingClassName = isMobile
    ? "min-h-[88px] gap-2.5 pr-4 py-4"
    : density === "compact"
      ? "gap-0 pr-3 py-1.5"
      : "gap-0 pr-3 py-3";
  const rowInteractionHandlers = useLongPress({
    onLongPress: () => {
      if (!isMobile || batchMode) {
        return;
      }

      enterSelectionMode(email.id);
    },
    disabled: !isMobile,
  });

  return (
    <DraggableEmail emailId={email.id}>
      <ContextMenu
        email={email}
        onReply={onReply}
        onForward={onForward}
        onToggleStar={onToggleStar}
        onPin={onPin}
        isPinned={isPinned}
        onSnooze={onSnooze}
        onArchive={archiveFolderId ? (id) => void onArchive(id) : undefined}
        onDelete={(id) => void onDelete(id)}
        onMarkRead={email.is_read ? undefined : () => void onToggleRead(email)}
        onMarkUnread={email.is_read ? () => void onToggleRead(email) : undefined}
      >
        <SwipeableRow
          onSwipeLeft={archiveFolderId ? () => void onArchive(email.id) : () => void onDelete(email.id)}
          onSwipeRight={() => void onToggleRead(email)}
          leftLabel={archiveFolderId ? "Archive" : "Delete"}
          leftIcon={archiveFolderId ? "archive" : "delete"}
          rightLabel={email.is_read ? "Mark unread" : "Mark read"}
          rightIcon={email.is_read ? "unread" : "read"}
          disabled={batchMode || !isMobile}
        >
          <div
            role="row"
            tabIndex={0}
            aria-selected={selectedEmailId === email.id}
            aria-label={`${senderText}, ${email.subject}, ${formatRelativeDate(email.date)}`}
            onClick={() => (batchMode ? toggleSelection(email.id) : onSelectEmail(email.id))}
            onKeyDown={(event) => handleRowKeyDown(event, email.id, onSelectEmail)}
            {...rowInteractionHandlers}
            className={`mail-row group relative flex w-full cursor-default items-start border-b border-border/40 pl-3 text-left ${rowSpacingClassName} ${
              isSelected || selectedEmailId === email.id
                ? "bg-primary/10"
                : "hover:bg-surface-hover"
            }`}
          >
            {!email.is_read && (
              <span
                aria-hidden="true"
                data-unread-dot
                className="absolute left-1 top-1/2 h-1.5 w-1.5 -translate-y-1/2 rounded-full bg-primary"
              />
            )}
            <div role="gridcell" className={cn("flex min-w-0 flex-1 items-start", isMobile ? "gap-2.5" : "gap-0")}>
            <button
              type="button"
              onClick={(e) => {
                e.stopPropagation();
                toggleSelection(email.id);
              }}
                className={`shrink-0 rounded transition-opacity focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-primary ${
                 isMobile ? "mt-0 h-11 w-11 p-0" : "-ml-1 mr-0.5 flex h-7 w-6 items-center justify-center p-0"} ${
                 batchMode || isSelected
                   ? "opacity-100"
                   : "opacity-0 group-hover:opacity-100 group-focus-within:opacity-100 focus-visible:opacity-100"
               } text-muted-foreground hover:text-foreground`}
             >
               {isSelected ? <CheckSquare size={isMobile ? 18 : 14} className="text-primary" /> : <Square size={isMobile ? 18 : 14} />}
             </button>
             <Avatar
               name={email.from_name || email.from_address}
               email={email.from_address}
               size={isMobile ? "md" : "sm"}
               className="mt-0.5 shrink-0"
             />
             <div className={cn("min-w-0 flex-1", isMobile ? "pt-0.5" : "ml-2.5")}>
               <div className="flex items-start gap-2">
                 <div className="min-w-0 flex-1">
                   <div className="flex items-center gap-2">
                    <span
                      className={`truncate text-sm ${
                        email.is_read
                          ? "font-medium text-muted-foreground"
                          : "font-semibold text-foreground"
                      }`}
                    >
                      {highlightText(senderText, highlightQuery ?? "")}
                    </span>
                    {isPinned && (
                      <Pin size={13} className="shrink-0 fill-primary text-primary" aria-label="Pinned" />
                    )}
                    {email.is_starred && (
                      <Star size={13} className="shrink-0 fill-amber-400 text-amber-400" aria-label="Starred" />
                    )}
                    {email.is_draft && (
                      <span className="shrink-0 rounded-sm bg-amber-500/15 px-1 text-[10px] font-semibold uppercase tracking-wide text-amber-500">
                        Draft
                      </span>
                    )}
                    {archiveFolderId && email.folder_id === archiveFolderId && folderId !== archiveFolderId && (
                      <Archive size={13} className="shrink-0 text-muted-foreground" aria-label="Archived" />
                    )}
                    {email.has_attachments && (
                      <Paperclip size={13} className="shrink-0 text-muted-foreground" />
                    )}
                  </div>
                </div>
                <div className={cn("ml-1 shrink-0 text-xs text-muted-foreground", isMobile ? "flex min-h-11 items-start" : "flex h-6 items-start")}>
                  <div className={cn("relative items-start justify-end", isMobile ? "flex min-h-11" : "flex h-6")}>
                    <div role="gridcell" className={cn(
                      "mail-row-actions absolute right-0 flex items-start justify-end",
                      isMobile ? "inset-y-0 opacity-100" : "-top-1 pointer-events-none opacity-0 group-hover:pointer-events-auto group-hover:opacity-100 group-focus-within:pointer-events-auto group-focus-within:opacity-100",
                    )}>
                      <div className={cn(
                        "flex max-w-[14rem] justify-end rounded-lg bg-popover/95 p-0.5 shadow-md ring-1 ring-border/60 backdrop-blur-md",
                        isMobile ? "pointer-events-auto gap-2 flex-wrap" : "pointer-events-auto gap-1 flex-nowrap",
                      )}>
                    <button
                      type="button"
                      onClick={(e) => {
                        e.stopPropagation();
                        void onToggleStar(email.id);
                      }}
                       className={cn(actionButtonClassName, "hover:bg-surface-hover hover:text-foreground")}
                      aria-label={email.is_starred ? "Unstar" : "Star"}
                      title={email.is_starred ? "Unstar" : "Star"}
                    >
                      <Star
                        size={14}
                        className={
                          email.is_starred
                            ? "fill-amber-400 text-amber-400"
                            : "text-muted-foreground"
                        }
                      />
                    </button>
                    {onPin && (
                      <button
                        type="button"
                        onClick={(e) => {
                          e.stopPropagation();
                          void onPin(email.id, !isPinned);
                        }}
                         className={cn(actionButtonClassName, `transition-colors ${
                           isPinned
                             ? "bg-primary/10 text-primary hover:bg-primary/15 hover:text-primary"
                             : "text-muted-foreground hover:bg-surface-hover hover:text-foreground"
                         }`)}
                        aria-label={isPinned ? "Unpin" : "Pin"}
                        title={isPinned ? "Unpin" : "Pin"}
                      >
                        <Pin size={14} className={isPinned ? "fill-primary text-primary" : undefined} />
                      </button>
                    )}
                    {onSnooze && (
                      <button
                        type="button"
                        onClick={(e) => {
                          e.stopPropagation();
                          onSnooze(email.id);
                        }}
                         className={cn(actionButtonClassName, "hover:bg-surface-hover hover:text-foreground")}
                        aria-label="Snooze"
                        title="Snooze"
                      >
                        <Clock size={14} />
                      </button>
                    )}
                    {archiveFolderId && (
                      <button
                        type="button"
                        onClick={(e) => {
                          e.stopPropagation();
                          void onArchive(email.id);
                        }}
                         className={cn(actionButtonClassName, "hover:bg-surface-hover hover:text-foreground")}
                        aria-label="Archive"
                        title="Archive"
                      >
                        <Archive size={14} />
                      </button>
                    )}
                    <button
                      type="button"
                      onClick={(e) => {
                        e.stopPropagation();
                        void onToggleRead(email);
                      }}
                       className={cn(actionButtonClassName, "hover:bg-surface-hover hover:text-foreground")}
                      aria-label={email.is_read ? "Mark unread" : "Mark read"}
                      title={email.is_read ? "Mark unread" : "Mark read"}
                    >
                      {email.is_read ? <MailOpen size={14} /> : <Mail size={14} />}
                    </button>
                    <button
                      type="button"
                      onClick={(e) => {
                        e.stopPropagation();
                        void onDelete(email.id);
                      }}
                       className={cn(actionButtonClassName, "hover:bg-destructive/10 hover:text-destructive")}
                      aria-label="Delete"
                      title="Delete"
                    >
                      <Trash2 size={14} />
                    </button>
                      </div>
                    </div>
                    <span className={cn(
                      "whitespace-nowrap text-right leading-4",
                      isMobile ? "hidden" : "mail-row-date pt-0.5 tabular-nums group-hover:opacity-0 group-focus-within:opacity-0",
                    )}>
                      {formatRelativeDate(email.date)}
                    </span>
                  </div>
                </div>
              </div>
              {isMobile && (
                <span className="mt-1 block text-[11px] text-muted-foreground">
                  {formatRelativeDate(email.date)}
                </span>
              )}
               <span
                 className={`mt-0.5 block truncate text-sm ${
                   email.is_read ? "text-muted-foreground" : "font-medium text-foreground"
                }`}
              >
                {highlightText(email.subject, highlightQuery ?? "")}
              </span>
              <span className="mt-0.5 block truncate text-xs text-muted-foreground">
                {highlightText(email.snippet, highlightQuery ?? "")}
              </span>
            </div>
            </div>
          </div>
        </SwipeableRow>
      </ContextMenu>
    </DraggableEmail>
  );
}, (prevProps, nextProps) => {
  const wasSelected = prevProps.selectedIds.has(prevProps.email.id);
  const isSelected = nextProps.selectedIds.has(nextProps.email.id);
    const wasActive = prevProps.selectedEmailId === prevProps.email.id;
    const isActive = nextProps.selectedEmailId === nextProps.email.id;

    return (
      prevProps.email === nextProps.email &&
      prevProps.isPinned === nextProps.isPinned &&
      prevProps.highlightQuery === nextProps.highlightQuery &&
      wasSelected === isSelected &&
      wasActive === isActive &&
      prevProps.batchMode === nextProps.batchMode &&
      prevProps.isMobile === nextProps.isMobile &&
      prevProps.density === nextProps.density &&
    prevProps.onSelectEmail === nextProps.onSelectEmail &&
    prevProps.onToggleStar === nextProps.onToggleStar &&
    prevProps.archiveFolderId === nextProps.archiveFolderId &&
    prevProps.folderId === nextProps.folderId &&
    prevProps.onPin === nextProps.onPin &&
    prevProps.onSnooze === nextProps.onSnooze &&
    prevProps.onReply === nextProps.onReply &&
    prevProps.onForward === nextProps.onForward &&
    prevProps.onArchive === nextProps.onArchive &&
    prevProps.onDelete === nextProps.onDelete &&
    prevProps.onToggleRead === nextProps.onToggleRead &&
    prevProps.toggleSelection === nextProps.toggleSelection
  );
});

export function EmailList({
  archiveFolders,
  onEmailsRemoved,
  searchRequest,
  emails,
  selectedEmailId,
  onSelectEmail,
  onToggleStar,
  loading,
  accountId,
  folderId,
  archiveFolderId,
  onLoadMore,
  hasMore,
  onPin,
  onSnooze,
  onReply,
  onForward,
  smartCategory,
  onSmartCategoryChange,
  density = 'comfortable',
  mailboxTitle = 'Inbox',
  onCompose,
  onSync,
  composeDisabled,
  syncBusy,
  syncError,
  enqueueRetry,
}: EmailListProps) {
  const { toast } = useToast();
  const confirm = useConfirm();
  const { t } = useTranslation();
  const isMobile = useIsMobile();
  const { keyboardVisible, keyboardHeight } = useVirtualKeyboard();
  const [searchQuery, setSearchQuery] = useState("");
  const [searchScope, setSearchScope] = useState<SearchScope>("current");
  const [searchResults, setSearchResults] = useState<SearchResult | null>(null);
  const [searchLoading, setSearchLoading] = useState(false);
  const [isSearchFocused, setIsSearchFocused] = useState(false);
  const [recentSearches, setRecentSearches] = useState<RecentSearchEntry[]>(() => loadRecentSearches());
  const debounceRef = useRef<ReturnType<typeof setTimeout> | null>(null);
  const searchContainerRef = useRef<HTMLDivElement>(null);
  const parentRef = useRef<HTMLDivElement>(null);
  const sentinelRef = useRef<HTMLDivElement>(null);
  const [selectedIds, setSelectedIds] = useState<Set<string>>(new Set());
  const [optimisticPinnedIds, setOptimisticPinnedIds] = useState<Set<string>>(new Set());
  const [isSelectionMode, setIsSelectionMode] = useState(false);
  const batchMode = isMobile ? isSelectionMode : selectedIds.size > 0;
  const [searchFilters, setSearchFilters] = useState<SearchFilters>({});
  const scopeOptions = useMemo(() => getSearchScopeOptions(accountId, folderId), [accountId, folderId]);
  const effectiveSearchScope = scopeOptions.some((option) => option.value === searchScope)
    ? searchScope
    : scopeOptions[0]?.value ?? "current";
  const displayEmails = searchResults?.emails ?? emails;
  const emailById = useMemo(() => new Map(displayEmails.map((email) => [email.id, email])), [displayEmails]);
  const { naturalLanguage, normalized } = useMemo(() => getSearchMode(searchQuery), [searchQuery]);
  const sanitizedSearchFilters = useMemo(() => sanitizeSearchFilters(searchFilters), [searchFilters]);
  const hasFilters = Object.keys(sanitizedSearchFilters).length > 0;
  const isSearchActive = (normalized.length > 0 || naturalLanguage || hasFilters) && searchResults !== null;
  const highlightQuery = !naturalLanguage && searchResults ? normalized : "";
  const searchScopeLabel = getSearchScopeLabel(effectiveSearchScope, accountId, folderId);
  const shouldShowIdlePanel = isSearchFocused && !searchLoading && normalized.length === 0 && !hasFilters && searchResults === null;
  const activeOperators = useMemo(() => detectActiveOperators(searchQuery), [searchQuery]);

  const [showSaveSearch, setShowSaveSearch] = useState(false);
  const [saveSearchName, setSaveSearchName] = useState("");
  const saveSearchInputRef = useRef<HTMLInputElement>(null);
  const [showAutocomplete, setShowAutocomplete] = useState(false);

  const refreshEmails = useCallback(async () => {
    if (onLoadMore) {
      await Promise.resolve(onLoadMore());
      return;
    }

    if (accountId && folderId) {
      await api.syncFolder(accountId, folderId);
      return;
    }

    if (accountId) {
      await api.syncFolders(accountId);
    }
  }, [accountId, folderId, onLoadMore]);

  const {
    bindings: pullToRefreshBindings,
    isPulling,
    isRefreshing,
    pullDistance,
    progress: pullProgress,
  } = usePullToRefresh({
    onRefresh: refreshEmails,
    disabled: !isMobile || searchLoading || loading,
  });

  const autocompleteRecentSearches = useMemo(
    () => recentSearches.map((e) => e.query).filter(Boolean),
    [recentSearches],
  );

  useEffect(() => {
    if (showSaveSearch && saveSearchInputRef.current) {
      saveSearchInputRef.current.focus();
    }
  }, [showSaveSearch]);

  const handleSaveSearch = useCallback(async () => {
    const name = saveSearchName.trim();
    if (!name) return;

    try {
      await api.saveSearch(name, searchQuery, accountId);
      toast("success", t("search.searchSaved"));
      setShowSaveSearch(false);
      setSaveSearchName("");
      window.dispatchEvent(new CustomEvent("maho-saved-searches-changed"));
    } catch (err) {
      toast("error", err instanceof Error ? err.message : "Failed to save search");
    }
  }, [saveSearchName, searchQuery, accountId, toast, t]);

  const persistRecentSearch = useCallback((query: string, filters: SearchFilters) => {
    const entry = buildRecentSearchEntry(query, filters);

    if (!entry.query && !hasActiveSearchFilters(entry.filters)) {
      return;
    }

    setRecentSearches((prev) => {
      const next = [entry, ...prev.filter((item) => !areRecentSearchesEqual(item, entry))].slice(0, SEARCH_HISTORY_LIMIT);
      localStorage.setItem(SEARCH_HISTORY_STORAGE_KEY, JSON.stringify(next));
      return next;
    });
  }, []);

  const applySearchState = useCallback((query: string, filters: SearchFilters) => {
    setSearchQuery(query);
    setSearchFilters(sanitizeSearchFilters(filters));
    setIsSearchFocused(true);
  }, []);

  const handleRecentSearchSelect = useCallback((entry: RecentSearchEntry) => {
    applySearchState(entry.query, entry.filters);
  }, [applySearchState]);

  const handleSearchTipSelect = useCallback((tip: SearchTip) => {
    applySearchState(tip.query ?? "", tip.filters ?? {});
  }, [applySearchState]);

  const searchGeneration = useRef(0);
  useEffect(() => {
    searchGeneration.current += 1;
    setSearchResults(null);
    return () => { searchGeneration.current += 1; };
  }, [searchQuery, effectiveSearchScope, searchFilters, accountId, folderId, searchRequest]);

  async function runSearch(query: string, offset = 0) {
    const generation = ++searchGeneration.current;
    const { naturalLanguage, normalized } = getSearchMode(query);
    if (normalized.length === 0 && !hasFilters) {
      setSearchResults(null);
      setSearchLoading(false);
      return;
    }

    const scope = resolveSearchScope(effectiveSearchScope, accountId, folderId);
    setSearchLoading(true);

    try {
      const result = naturalLanguage
        ? await api.naturalLanguageSearch({
            query: normalized,
            account_id: scope.accountId,
            folder_id: scope.folderId,
          })
        : await api.searchEmails({
          query: normalized,
          account_id: scope.accountId,
          folder_id: scope.folderId,
          limit: 50,
          offset,
          from: sanitizedSearchFilters.from,
          to: sanitizedSearchFilters.to,
          has_attachment: sanitizedSearchFilters.hasAttachment || undefined,
          is_unread: sanitizedSearchFilters.isUnread || undefined,
          is_starred: sanitizedSearchFilters.isStarred || undefined,
          date_from: sanitizedSearchFilters.dateFrom || undefined,
          date_to: sanitizedSearchFilters.dateTo || undefined,
          });
      if (generation !== searchGeneration.current) return;
      setSearchResults(current => offset && current ? {...result, emails: [...current.emails, ...result.emails]} : result);
      persistRecentSearch(query, sanitizedSearchFilters);
    } catch (err) {
      if (generation !== searchGeneration.current) return;
      if (!offset) setSearchResults(null);
      toast(
        "error",
        err instanceof Error ? err.message : "Failed to search emails",
      );
    } finally {
      if (generation === searchGeneration.current) setSearchLoading(false);
    }
  }

  const handleDelete = useCallback(async (emailId: string) => {
    try {
      const ok = await confirm({
        title: "Delete Email",
        message: "Are you sure you want to delete this email? This action cannot be undone.",
        confirmLabel: "Delete",
        danger: true,
      });

      if (!ok) return;

      await api.deleteEmail(emailId);
      onEmailsRemoved?.([emailId]);
      void emitMailUpdated(emailId, "delete");
      setSearchResults((current) => removeEmailsFromSearchResult(current, [emailId]));
      toast("success", "Email deleted");
    } catch (err) {
      const retryAccountId = emailById.get(emailId)?.account_id ?? accountId;
      if (retryAccountId && enqueueRetry && isAuthRelatedError(err)) {
        enqueueRetry(retryAccountId, () => api.deleteEmail(emailId), t("app.delete"));
        return;
      }

      toast("error", err instanceof Error ? err.message : "Failed to delete email");
    }
  }, [accountId, confirm, emailById, enqueueRetry, t, toast, onEmailsRemoved]);

  const handleArchive = useCallback(async (emailId: string) => {
    const targetFolderId = archiveFolders?.find(folder => folder.account_id === emailById.get(emailId)?.account_id)?.id ?? archiveFolderId;
    if (!targetFolderId) return;

    try {
      await api.moveEmail(emailId, targetFolderId);
      onEmailsRemoved?.([emailId]);
      void emitMailUpdated(emailId, "move");
      setSearchResults((current) => removeEmailsFromSearchResult(current, [emailId]));
      toast("success", "Archived");
    } catch (err) {
      console.error("[archive] moveEmail failed:", err, { emailId, archiveFolderId });
      const retryAccountId = emailById.get(emailId)?.account_id ?? accountId;
      if (retryAccountId && enqueueRetry && isAuthRelatedError(err)) {
        enqueueRetry(retryAccountId, () => api.moveEmail(emailId, targetFolderId), t("app.archive"));
        return;
      }

      const msg =
        err instanceof Error
          ? err.message
          : typeof err === "string"
            ? err
            : (err && typeof err === "object" && "message" in err && typeof err.message === "string")
              ? err.message
              : JSON.stringify(err);
      toast("error", `Failed to archive: ${msg}`);
    }
  }, [accountId, archiveFolderId, archiveFolders, emailById, enqueueRetry, t, toast, onEmailsRemoved]);

  const handleToggleRead = useCallback(async (email: EmailSummary) => {
    try {
      if (email.is_read) {
        await api.markUnread(email.id);
      } else {
        await api.markRead(email.id);
      }
      toast("success", email.is_read ? "Marked unread" : "Marked read");
    } catch (err) {
      const retryLabel = email.is_read ? t("app.markUnread") : t("app.markRead");
      if (enqueueRetry && isAuthRelatedError(err)) {
        enqueueRetry(
          email.account_id,
          () => email.is_read ? api.markUnread(email.id) : api.markRead(email.id),
          retryLabel,
        );
        return;
      }

      toast("error", err instanceof Error ? err.message : "Failed to update");
    }
  }, [enqueueRetry, t, toast]);

  const toggleSelection = useCallback((emailId: string) => {
    setSelectedIds((prev) => {
      const next = new Set(prev);
      if (next.has(emailId)) next.delete(emailId);
      else next.add(emailId);
      return next;
    });
  }, []);

  const enterSelectionMode = useCallback((emailId: string) => {
    setIsSelectionMode(true);
    setSelectedIds((prev) => {
      if (prev.has(emailId) && prev.size === 1) {
        return prev;
      }

      return new Set([emailId]);
    });
  }, []);

  const handlePin = useCallback(async (emailId: string, nextPinned: boolean) => {
    if (!onPin) return;

    setOptimisticPinnedIds((prev) => {
      const next = new Set(prev);
      if (nextPinned) {
        next.add(emailId);
      } else {
        next.delete(emailId);
      }
      return next;
    });

    try {
      await onPin(emailId, nextPinned);
    } catch (error) {
      setOptimisticPinnedIds((prev) => {
        const next = new Set(prev);
        if (nextPinned) {
          next.delete(emailId);
        } else {
          next.add(emailId);
        }
        return next;
      });

      throw error;
    }
  }, [onPin]);

  function selectAll() {
    setSelectedIds(new Set(displayEmails.map((e) => e.id)));
  }

  function clearSelection() {
    setSelectedIds(new Set());
    setIsSelectionMode(false);
  }

  async function handleBatchMarkRead() {
    try {
      await api.batchMarkRead({ email_ids: Array.from(selectedIds) });
      toast("success", `${selectedIds.size} emails marked read`);
      clearSelection();
    } catch (err) {
      toast("error", err instanceof Error ? err.message : "Batch action failed");
    }
  }

  async function handleBatchMarkUnread() {
    try {
      await api.batchMarkUnread({ email_ids: Array.from(selectedIds) });
      toast("success", `${selectedIds.size} emails marked unread`);
      clearSelection();
    } catch (err) {
      toast("error", err instanceof Error ? err.message : "Batch action failed");
    }
  }

  async function handleBatchDelete() {
    const ok = await confirm({
      title: "Delete Emails",
      message: `Delete ${selectedIds.size} selected emails? This cannot be undone.`,
      confirmLabel: "Delete",
      danger: true,
    });
    if (!ok) return;
    try {
      const ids = Array.from(selectedIds);
      await api.batchDelete({ email_ids: ids });
      setSearchResults((prev) => removeEmailsFromSearchResult(prev, ids));
      toast("success", `${selectedIds.size} emails deleted`);
      clearSelection();
    } catch (err) {
      toast("error", err instanceof Error ? err.message : "Batch action failed");
    }
  }

  async function handleBatchStar() {
    try {
      await api.batchToggleStar({ email_ids: Array.from(selectedIds) });
      toast("success", `${selectedIds.size} emails starred`);
      clearSelection();
    } catch (err) {
      toast("error", err instanceof Error ? err.message : "Batch action failed");
    }
  }

  async function handleBatchArchive() {
    if (!archiveFolderId) return;
    try {
      const ids = Array.from(selectedIds);
      await api.batchMove({ email_ids: ids, target_folder_id: archiveFolderId });
      setSearchResults((prev) => removeEmailsFromSearchResult(prev, ids));
      toast("success", `${selectedIds.size} emails archived`);
      clearSelection();
    } catch (err) {
      toast("error", err instanceof Error ? err.message : "Batch action failed");
    }
  }

  async function handleBatchMove() {
    if (!archiveFolderId) return;
    await handleBatchArchive();
  }

  const appliedSearchRequest = useRef<{query: string} | null>(null);
  useEffect(() => {
    if (searchRequest && appliedSearchRequest.current !== searchRequest) {
      applySearchState(searchRequest.query, {});
    }
  }, [searchRequest, applySearchState]);

  // eslint-disable-next-line react-hooks/exhaustive-deps
  useEffect(() => {
    if (debounceRef.current) {
      clearTimeout(debounceRef.current);
    }
    if (searchRequest && appliedSearchRequest.current !== searchRequest && searchQuery === searchRequest.query && !hasFilters) {
      appliedSearchRequest.current = searchRequest;
      setSearchLoading(true);
      void runSearch(searchQuery);
      return;
    }
    if (normalized.length >= 2 || naturalLanguage || hasFilters) {
      setSearchLoading(true);
      debounceRef.current = setTimeout(async () => {
        void runSearch(searchQuery);
      }, 300);
    } else {
      setSearchResults(null);
      setSearchLoading(false);
    }
    return () => {
      if (debounceRef.current) {
        clearTimeout(debounceRef.current);
      }
    };
  }, [searchQuery, effectiveSearchScope, normalized, naturalLanguage, hasFilters, searchFilters, accountId, folderId, searchRequest]);

  useEffect(() => {
    const visibleEmailIds = new Set(displayEmails.map((email) => email.id));

    setOptimisticPinnedIds((prev) => {
      let changed = false;
      const next = new Set<string>();

      for (const id of prev) {
        if (visibleEmailIds.has(id)) {
          next.add(id);
        } else {
          changed = true;
        }
      }

      return changed ? next : prev;
    });
  }, [displayEmails]);

  useEffect(() => {
    if (!isMobile) {
      setIsSelectionMode(false);
      return;
    }

    if (selectedIds.size === 0) {
      setIsSelectionMode(false);
    }
  }, [isMobile, selectedIds]);

  useEffect(() => {
    if (!isSearchFocused) {
      return;
    }

    function handlePointerDown(event: MouseEvent) {
      if (!searchContainerRef.current?.contains(event.target as Node)) {
        setIsSearchFocused(false);
      }
    }

    document.addEventListener("mousedown", handlePointerDown);

    return () => {
      document.removeEventListener("mousedown", handlePointerDown);
    };
  }, [isSearchFocused]);

  useEffect(() => {
    if (!hasMore || !onLoadMore || loading || searchResults) return;
    const sentinel = sentinelRef.current;
    if (!sentinel) return;

    const observer = new IntersectionObserver(
      (entries) => {
        if (entries[0]?.isIntersecting) {
          onLoadMore();
        }
      },
      { rootMargin: '200px' }
    );

    observer.observe(sentinel);
    return () => observer.disconnect();
  }, [hasMore, onLoadMore, loading, searchResults]);

  const virtualizer = useVirtualizer({
    count: displayEmails.length,
    getScrollElement: () => parentRef.current,
    estimateSize: () => {
      if (isMobile) {
        return 104;
      }

      return density === 'compact' ? 62 : 82;
    },
    initialRect: { width: 0, height: 600 },
    overscan: 3,
  });
  const virtualRows = virtualizer.getVirtualItems();

  return (
    <div className="flex h-full w-full flex-col bg-card/55">
      <div className="relative border-b border-border/60 bg-card/75 px-4 pb-3 pt-4 backdrop-blur-xl">
        <div ref={searchContainerRef} className="flex flex-col gap-3">
          <div className="flex items-center justify-between gap-3">
            <div className="min-w-0">
              <h1 className="truncate text-lg font-semibold tracking-tight text-foreground">{mailboxTitle}</h1>
              <p className="text-xs text-muted-foreground">
                {isSearchActive && searchResults
                  ? formatSearchCount(searchResults.total_count)
                  : `${displayEmails.length.toLocaleString()} message${displayEmails.length === 1 ? "" : "s"}`}
              </p>
            </div>
            <div className="flex shrink-0 items-center gap-1.5" role="toolbar" aria-label={t("a11y.globalActions")}>
              {onSync && (
                <button
                  type="button"
                  onClick={onSync}
                  disabled={syncBusy}
                  className="mail-pressable flex h-9 w-9 items-center justify-center rounded-lg text-muted-foreground hover:bg-surface-hover hover:text-foreground focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-primary disabled:opacity-50"
                  aria-label={t("app.syncFolders")}
                  title={t("app.syncFolders")}
                >
                  <RefreshCw size={15} className={syncBusy ? "animate-spin" : undefined} />
                </button>
              )}
              {onCompose && (
                <button
                  type="button"
                  onClick={onCompose}
                  disabled={composeDisabled}
                  className="flex h-9 items-center gap-2 mail-pressable rounded-lg bg-primary px-3.5 text-sm font-semibold text-primary-foreground shadow-sm hover:bg-primary/90 focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-primary focus-visible:ring-offset-2 disabled:opacity-50"
                  aria-label={t("app.compose")}
                  title={t("app.compose")}
                >
                  <PenLine size={15} />
                  <span>{t("app.compose")}</span>
                </button>
              )}
            </div>
          </div>
          <div className="flex items-center gap-2">
            <div className="relative flex h-10 min-w-0 flex-1 items-center gap-2 rounded-xl border border-border/60 bg-background/70 px-3 shadow-sm transition-colors hover:border-foreground/20 focus-within:border-primary/50 focus-within:bg-background">
              <Search size={16} className="text-muted-foreground" />
              <input
                type="text"
                placeholder="Search emails..."
                value={searchQuery}
                onChange={(e) => { setSearchQuery(e.target.value); setShowAutocomplete(true); }}
                onFocus={() => { setIsSearchFocused(true); setShowAutocomplete(true); }}
                data-mail-search-input
                aria-label={t("a11y.searchEmails")}
                className="min-w-0 flex-1 bg-transparent text-sm text-foreground placeholder:text-muted-foreground focus-visible:outline-none"
              />
              <AdvancedSearchPanel filters={searchFilters} onChange={setSearchFilters} />
              <button
                type="button"
                onClick={() => {
                  setSearchLoading(true);
                  void runSearch(searchQuery);
                }}
                className="mail-pressable flex h-7 w-7 items-center justify-center rounded-md text-muted-foreground hover:bg-surface-hover hover:text-foreground focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-primary"
                title="Natural language search"
                aria-label="Ask about mail"
              >
                <Sparkles size={14} />
              </button>
              {searchLoading && <Loader2 size={14} className="animate-spin text-muted-foreground" />}
               <SearchAutocomplete
                 query={searchQuery}
                 onSelect={(newQuery) => { setSearchQuery(newQuery); setShowAutocomplete(false); }}
                 visible={showAutocomplete && isSearchFocused && !shouldShowIdlePanel}
                 onDismiss={() => setShowAutocomplete(false)}
                 recentSearches={autocompleteRecentSearches}
               />
            </div>
          </div>
          {activeOperators.length > 0 && (
            <div className="flex flex-wrap gap-1 px-1">
              {activeOperators.map((op) => (
                <span
                  key={op}
                  className="rounded-md bg-primary/10 px-1.5 py-0.5 text-[10px] font-semibold text-primary"
                >
                  {op}
                </span>
              ))}
            </div>
          )}
          <div className="flex items-center justify-between gap-3 px-1">
            <div className="flex min-w-0 items-center gap-2">
              <span className="sr-only">Scope</span>
              <Select
                value={effectiveSearchScope}
                onChange={(value) => setSearchScope(value as SearchScope)}
                options={scopeOptions}
                className="min-w-[8.5rem]"
              />
            </div>
            {isSearchActive && searchResults && (
              <div className="text-right text-xs text-muted-foreground">
                <span className="font-medium text-foreground">{formatSearchCount(searchResults.total_count)}</span>
                <span className="ml-1.5">in {searchScopeLabel}</span>
              </div>
            )}
          </div>

          {shouldShowIdlePanel && (
            <div className="rounded-2xl border border-border bg-card/40 p-3 shadow-sm">
              <div className="flex flex-col gap-4">
                <div className="flex items-start justify-between gap-3">
                  <div>
                    <p className="text-sm font-medium text-foreground">Search your mail</p>
                    <p className="mt-1 text-xs text-muted-foreground">
                      Start with a keyword, use <span className="font-medium text-foreground">?</span> for natural language,
                      or tap a shortcut below to fill the current search controls.
                    </p>
                  </div>
                  <span className="rounded-full border border-border bg-background px-2 py-1 text-[11px] font-medium uppercase tracking-[0.16em] text-muted-foreground">
                    {searchScopeLabel}
                  </span>
                </div>

                {recentSearches.length > 0 && (
                  <section className="flex flex-col gap-2">
                    <div className="flex items-center justify-between gap-2">
                      <p className="text-xs font-medium uppercase tracking-[0.16em] text-muted-foreground">
                        Recent searches
                      </p>
                      <span className="text-[11px] text-muted-foreground">Saved on successful runs</span>
                    </div>
                    <div className="flex flex-wrap gap-2">
                      {recentSearches.map((entry) => (
                        <button
                          key={`${entry.query}-${JSON.stringify(entry.filters)}`}
                          type="button"
                          onClick={() => handleRecentSearchSelect(entry)}
                          className="rounded-full focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-primary"
                          title={formatRecentSearchLabel(entry)}
                        >
                          <Badge
                            variant="outline"
                            className="cursor-pointer border-border bg-background/80 px-3 py-1 text-left text-foreground transition-colors hover:bg-surface-selected"
                          >
                            {formatRecentSearchLabel(entry)}
                          </Badge>
                        </button>
                      ))}
                    </div>
                  </section>
                )}

                <section className="flex flex-col gap-2">
                  <p className="text-xs font-medium uppercase tracking-[0.16em] text-muted-foreground">
                    {t("search.operatorsTitle")}
                  </p>
                  <div className="grid grid-cols-2 gap-x-4 gap-y-1">
                    {SEARCH_OPERATORS.map((op) => (
                      <button
                        key={op.prefix}
                        type="button"
                        onClick={() => { setSearchQuery(op.prefix); }}
                        className="flex items-center gap-2 rounded-lg px-2 py-1.5 text-left transition-colors hover:bg-surface-hover focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-primary"
                      >
                        <code className="rounded bg-primary/10 px-1.5 py-0.5 text-[11px] font-semibold text-primary">
                          {op.prefix}
                        </code>
                        <span className="truncate text-[11px] text-muted-foreground">{op.example}</span>
                      </button>
                    ))}
                  </div>
                </section>

                <section className="flex flex-col gap-2">
                  <p className="text-xs font-medium uppercase tracking-[0.16em] text-muted-foreground">
                    Tips
                  </p>
                  <div className="grid gap-2 sm:grid-cols-2 xl:grid-cols-3">
                    {SEARCH_TIPS.map((tip) => (
                      <button
                        key={tip.id}
                        type="button"
                        onClick={() => handleSearchTipSelect(tip)}
                        className="rounded-xl border border-border bg-background/80 px-3 py-2 text-left transition-colors hover:border-foreground/20 hover:bg-surface-hover focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-primary"
                      >
                        <div className="flex items-center gap-2">
                          <span className="rounded-lg bg-primary/10 px-2 py-1 text-[11px] font-medium text-primary">
                            {tip.label}
                          </span>
                          {tip.query ? (
                            <span className="truncate text-[11px] text-muted-foreground">{tip.query}</span>
                          ) : null}
                        </div>
                        <p className="mt-2 text-xs leading-5 text-muted-foreground">{tip.description}</p>
                      </button>
                    ))}
                  </div>
                </section>
              </div>
            </div>
          )}
        </div>
      </div>

      {isSearchActive && searchResults && (
        <div className="border-b border-border bg-card/40 px-3 py-2">
          <div className="flex items-center justify-between gap-3">
            <div className="min-w-0">
              <p className="text-sm font-medium text-foreground">
                Search results
              </p>
              <p className="text-xs text-muted-foreground">
                {formatSearchCount(searchResults.total_count)} in {searchScopeLabel}
                {naturalLanguage ? (
                  <>
                    {" for "}
                    <span className="font-medium text-foreground">natural language search</span>
                  </>
                ) : normalized ? (
                  <>
                    {" for "}
                    <span className="font-medium text-foreground">"{normalized}"</span>
                  </>
                ) : null}
              </p>
            </div>
            <div className="flex shrink-0 items-center gap-2">
              {searchLoading && <Loader2 size={14} className="animate-spin text-muted-foreground" />}
              {!showSaveSearch ? (
                <button
                  type="button"
                  onClick={() => { setSaveSearchName(""); setShowSaveSearch(true); }}
                  className="mail-pressable flex h-7 w-7 items-center justify-center rounded-md text-muted-foreground hover:bg-surface-hover hover:text-foreground focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-primary"
                  title={t("search.saveSearch")}
                  aria-label={t("search.saveSearch")}
                >
                  <Bookmark size={14} />
                </button>
              ) : (
                <div className="flex items-center gap-1">
                  <input
                    ref={saveSearchInputRef}
                    type="text"
                    value={saveSearchName}
                    onChange={(e) => setSaveSearchName(e.target.value)}
                    onKeyDown={(e) => {
                      if (e.key === "Enter") void handleSaveSearch();
                      if (e.key === "Escape") setShowSaveSearch(false);
                    }}
                    placeholder={t("search.searchName")}
                    className="w-32 rounded-md bg-background px-2 py-1 text-xs text-foreground outline-none ring-1 ring-border focus:ring-primary"
                  />
                  <button
                    type="button"
                    onClick={() => void handleSaveSearch()}
                    disabled={!saveSearchName.trim()}
                    className="rounded-md bg-primary px-2 py-1 text-xs font-medium text-primary-foreground transition-colors hover:bg-primary/90 disabled:opacity-50 focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-primary"
                  >
                    {t("common.save")}
                  </button>
                  <button
                    type="button"
                    onClick={() => setShowSaveSearch(false)}
                    className="mail-pressable flex h-6 w-6 items-center justify-center rounded-md text-muted-foreground hover:bg-surface-hover hover:text-foreground"
                  >
                    <X size={12} />
                  </button>
                </div>
              )}
            </div>
          </div>
        </div>
      )}

      {smartCategory != null && onSmartCategoryChange && (
        <div className="flex items-center gap-1 border-b border-border px-3 py-2">
          {(["personal", "notification", "newsletter", "promotion"] as const).map((cat) => (
            <button
              key={cat}
              type="button"
              onClick={() => onSmartCategoryChange(cat)}
              className={`rounded-lg px-3 py-1.5 text-xs font-medium capitalize transition-colors ${
                smartCategory === cat
                  ? "bg-primary/15 text-primary ring-1 ring-inset ring-primary/20"
                  : "text-muted-foreground hover:bg-surface-hover hover:text-foreground"
              }`}
            >
              {cat}
            </button>
          ))}
        </div>
      )}

      {batchMode && (
        <div className={cn(
          "flex items-center gap-2 border-b border-primary/20 bg-primary/5 px-4 py-2.5 shadow-sm",
          isMobile && "sticky top-0 z-10 min-h-14 flex-wrap gap-1.5 bg-background/95 px-4 py-3 backdrop-blur",
        )}>
          <button
            type="button"
            onClick={clearSelection}
            className={cn(
              "mail-pressable rounded-md text-muted-foreground hover:bg-surface-hover hover:text-foreground",
              isMobile ? "flex h-11 items-center px-3" : "p-1",
            )}
            aria-label="Clear selection"
          >
            <X size={14} />
            {isMobile && <span className="ml-2 text-sm">Cancel</span>}
          </button>
          <span className="text-sm font-semibold text-foreground">{selectedIds.size} selected</span>
          {!isMobile && (
            <button
              type="button"
              onClick={selectAll}
              className="rounded-md px-2 py-1 text-xs font-medium text-primary hover:bg-primary/10"
              aria-label="Select all"
            >
              Select all
            </button>
          )}
          <div className="flex-1" />
          <button
            type="button"
            onClick={() => void handleBatchMarkRead()}
            className={cn(
              "text-muted-foreground hover:bg-surface-hover hover:text-foreground",
              isMobile ? "flex h-11 min-w-11 items-center justify-center rounded-lg px-3" : "mail-pressable flex h-8 w-8 items-center justify-center rounded-md",
            )}
            title="Mark read"
            aria-label="Mark read"
          >
            <MailOpen size={14} />
            {isMobile && <span className="ml-2 text-xs font-medium">Read</span>}
          </button>
          {isMobile ? (
            <button
              type="button"
              onClick={() => void handleBatchMarkUnread()}
              className="mail-pressable flex h-11 min-w-11 items-center justify-center rounded-lg px-3 text-muted-foreground hover:bg-surface-hover hover:text-foreground"
              title="Mark unread"
              aria-label="Mark unread"
            >
              <Mail size={14} />
              <span className="ml-2 text-xs font-medium">Unread</span>
            </button>
          ) : (
            <button
              type="button"
              onClick={() => void handleBatchStar()}
              className="mail-pressable flex h-8 w-8 items-center justify-center rounded-md text-muted-foreground hover:bg-surface-hover hover:text-foreground"
              title="Toggle star"
              aria-label="Toggle star"
            >
              <Star size={14} />
            </button>
          )}
          {archiveFolderId && (
            <button
              type="button"
              onClick={() => void handleBatchMove()}
              className={cn(
                "text-muted-foreground hover:bg-surface-hover hover:text-foreground",
                isMobile ? "flex h-11 min-w-11 items-center justify-center rounded-lg px-3" : "mail-pressable flex h-8 w-8 items-center justify-center rounded-md",
              )}
              title={isMobile ? "Move" : "Archive"}
              aria-label={isMobile ? "Move" : "Archive"}
            >
              <Archive size={14} />
              {isMobile && <span className="ml-2 text-xs font-medium">Move</span>}
            </button>
          )}
          <button
            type="button"
            onClick={() => void handleBatchDelete()}
            className={cn(
              "text-muted-foreground hover:bg-destructive/10 hover:text-destructive",
              isMobile ? "flex h-11 min-w-11 items-center justify-center rounded-lg px-3" : "mail-pressable flex h-8 w-8 items-center justify-center rounded-md",
            )}
            title="Delete"
            aria-label="Delete"
          >
            <Trash2 size={14} />
            {isMobile && <span className="ml-2 text-xs font-medium">Delete</span>}
          </button>
        </div>
      )}

      <div
        ref={parentRef}
        className="flex-1 overflow-y-auto overscroll-y-contain bg-card/35"
        style={{
          contain: "strict",
          paddingBottom: isMobile && keyboardVisible ? keyboardHeight : undefined,
        }}
        {...(isMobile ? pullToRefreshBindings : {})}
      >
        {loading && emails.length === 0 ? (
          <EmailListSkeleton />
        ) : shouldShowIdlePanel ? (
          <div className="px-3 pb-6 pt-4 text-center text-sm text-muted-foreground">
            Pick a recent search or tip above to get started.
          </div>
        ) : syncError && displayEmails.length === 0 ? (
          <div role="alert" className="flex items-center justify-center px-6 py-12">
            <div className="max-w-sm rounded-2xl border border-destructive/30 bg-destructive/10 px-6 py-5 text-center">
              <h3 className="font-semibold text-foreground">Mail could not sync</h3>
              <p className="mt-2 text-sm leading-6 text-muted-foreground">{syncError}</p>
              {onSync && (
                <button
                  type="button"
                  onClick={onSync}
                  className="mt-4 rounded-xl bg-primary px-4 py-2 text-sm font-semibold text-primary-foreground hover:bg-primary/90 focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring"
                >
                  Try again
                </button>
              )}
            </div>
          </div>
        ) : displayEmails.length === 0 ? (
          <div className="flex items-center justify-center py-12">
            <EmptyState
              title={normalized || hasFilters ? `No results for "${searchQuery || "current filters"}"` : "All caught up"}
              description={
                normalized || hasFilters
                  ? "Try a different search term or clear the search box."
                  : "There are no emails here right now."
              }
              icon={normalized || hasFilters ? <Search size={28} className="text-muted-foreground" /> : <CheckCircle2 size={28} className="text-muted-foreground" />}
            />
          </div>
        ) : (
          <>
             {(isMobile || isRefreshing || isPulling) && (
               <div
                 className="flex items-center justify-center overflow-hidden transition-[height,opacity] duration-200"
                 style={{
                   height: isRefreshing ? 56 : Math.min(56, pullDistance),
                   opacity: isRefreshing || isPulling ? 1 : 0,
                 }}
               >
                 <div
                   className="flex h-9 w-9 items-center justify-center rounded-full border border-border bg-background shadow-sm"
                   style={{ transform: `scale(${0.8 + pullProgress * 0.2})` }}
                   aria-hidden={!isRefreshing}
                 >
                   <Loader2
                     size={16}
                     className={cn("text-muted-foreground", (isRefreshing || isPulling) && "animate-spin")}
                     style={{ animationDuration: isRefreshing ? "0.8s" : "1.6s" }}
                   />
                 </div>
               </div>
             )}
             <div role="grid" aria-label={t("a11y.emailList")} style={{ height: virtualizer.getTotalSize(), width: "100%", position: "relative", willChange: "transform" }}>
               {virtualRows.map((virtualRow) => {
                 const email = displayEmails[virtualRow.index];

                 return (
                    <div
                      key={email.id}
                      style={{
                       position: "absolute",
                       top: 0,
                      left: 0,
                      width: "100%",
                      transform: `translateY(${virtualRow.start}px)`,
                    }}
                  >
                      <EmailRow
                        email={email}
                        isPinned={optimisticPinnedIds.has(email.id)}
                        selectedEmailId={selectedEmailId}
                        highlightQuery={highlightQuery}
                        selectedIds={selectedIds}
                        batchMode={batchMode}
                        isMobile={isMobile}
                        density={density}
                        onSelectEmail={onSelectEmail}
                       onToggleStar={onToggleStar}
                      archiveFolderId={archiveFolders?.find(folder => folder.account_id === email.account_id)?.id ?? archiveFolderId}
                      folderId={folderId}
                       onPin={handlePin}
                      onSnooze={onSnooze}
                      onReply={onReply}
                      onForward={onForward}
                      onArchive={handleArchive}
                       onDelete={handleDelete}
                       onToggleRead={handleToggleRead}
                       toggleSelection={toggleSelection}
                        enterSelectionMode={enterSelectionMode}
                     />
                   </div>
                 );
              })}
            </div>
            {searchResults && !naturalLanguage && searchResults.emails.length < searchResults.total_count && (
              <button type="button" disabled={searchLoading} onClick={() => void runSearch(searchQuery, searchResults.emails.length)} className="w-full p-3 text-sm text-primary disabled:opacity-50">
                Load more search results
              </button>
            )}
            {!searchResults && hasMore && onLoadMore && (
              <div ref={sentinelRef} className="flex w-full items-center justify-center py-3">
                {loading && <Loader2 size={16} className="animate-spin text-muted-foreground" />}
              </div>
            )}
          </>
        )}
      </div>
      <LiveRegion message={t("a11y.showingEmails", { count: displayEmails.length })} />
    </div>
  );
}
