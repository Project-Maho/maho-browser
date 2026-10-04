import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import {
  AlertCircle,
  Archive,
  Bell,
  Calendar,
  ChevronLeft,
  ChevronRight,
  Clock3,
  FileText,
  Folder as FolderIcon,
  Inbox,
  Loader2,
  MoreHorizontal,
  Pin,
  Plus,
  RefreshCw,
  Search,
  Send,
  Settings,
  Star,
  Trash2,
  Wifi,
  WifiOff,
} from "lucide-react";
import { useTranslation } from "react-i18next";
import type { AccountSummary, AuthError, Folder, FolderType, SavedSearch } from "../../types";
import * as api from "../../api";
import { Avatar } from "../ui/Avatar";
import { DropTargetFolder } from "../common/DragDrop";
import { useConfirm } from "../ui/ConfirmDialog";
import { useToast } from "../ui/Toast";
import { cn, getErrorMessage } from "../../lib/utils";

const FOLDER_ENTRIES: Array<{
  id: string;
  label: string;
  icon: typeof Inbox;
  target: { type: "aggregate"; folderType: FolderType } | { type: "synthetic"; syntheticId: string } | null;
}> = [
  { id: "archive", label: "Archive", icon: Archive, target: { type: "aggregate", folderType: "Archive" } },
  { id: "trash", label: "Trash", icon: Trash2, target: { type: "aggregate", folderType: "Trash" } },
  { id: "spam", label: "Spam", icon: AlertCircle, target: { type: "aggregate", folderType: "Spam" } },
];

const SYNTHETIC_PINS = { id: "pins", label: "Pins", icon: Pin };
const SYNTHETIC_STARRED = { id: "starred", label: "Starred", icon: Star };
const SYNTHETIC_REMINDERS = { id: "reminders", label: "Reminders", icon: Bell };

const FOLDER_TYPE_ICONS: Record<FolderType, typeof Inbox> = {
  Inbox,
  Sent: Send,
  Drafts: FileText,
  Trash: Trash2,
  Spam: AlertCircle,
  Archive,
  Custom: FolderIcon,
};

interface SidebarChildNode {
  type: "child";
  id: string;
  accountId: string;
  accountEmail: string;
  folderId?: string;
  unreadCount?: number;
  syntheticId?: string;
}

interface FolderChildNode extends SidebarChildNode {
  folderId: string;
  unreadCount: number;
}

interface SidebarParentNode {
  type: "parent";
  id: string;
  folderType: FolderType;
  name: string;
  isExpanded: boolean;
  totalUnread: number;
  variant: "top" | "recents";
  children: SidebarChildNode[];
}

interface SidebarFolderEntryNode {
  type: "folder-entry";
  id: string;
  label: string;
  icon: typeof Inbox;
  target: { type: "aggregate"; folderType: FolderType } | { type: "synthetic"; syntheticId: string } | null;
}

interface SidebarSyntheticNode {
  type: "synthetic";
  id: string;
  syntheticId: string;
  label: string;
  icon: typeof Pin;
  isExpanded: boolean;
  variant: "top" | "recents";
  children: SidebarChildNode[];
}

interface SidebarFooterStatusNode {
  type: "footer-status";
  id: string;
  title: string;
  icon: typeof RefreshCw;
}

interface SidebarFooterActionNode {
  type: "footer-action";
  id: string;
  title: string;
  icon: typeof Settings;
}

interface SidebarFooterNetworkNode {
  type: "footer-network";
  id: string;
}

interface SidebarCustomFolderNode {
  type: "custom-folder";
  id: string;
  folder: Folder;
  name: string;
  depth: number;
  hasChildren: boolean;
  isExpanded: boolean;
  children: SidebarCustomFolderNode[];
}

interface SidebarCustomFoldersSectionNode {
  type: "custom-folders-section";
  id: string;
  label: string;
  isExpanded: boolean;
  customFolderTree: SidebarCustomFolderNode[];
}

const LEADING_SLOT_CLASSNAME = "flex h-6 w-5 shrink-0 items-center justify-center";
const SHELL_TOGGLE_CLASSNAME = "absolute left-full top-24 z-30 flex h-10 w-8 -translate-x-1/2 -translate-y-1/2 items-center justify-center rounded-full border border-border bg-background/95 text-foreground shadow-lg ring-1 ring-background/90 backdrop-blur-sm transition-colors hover:bg-card hover:text-foreground focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring";

type SidebarRowNode =
  | SidebarParentNode
  | SidebarChildNode
  | SidebarFolderEntryNode
  | SidebarSyntheticNode
  | SidebarFooterStatusNode
  | SidebarFooterActionNode
  | SidebarFooterNetworkNode
  | SidebarCustomFolderNode
  | SidebarCustomFoldersSectionNode;

interface SidebarSection {
  id: string;
  kind: "top" | "folders" | "recents" | "footer";
  title?: string;
  action?: { label: string; onClick?: () => void };
  rows: SidebarRowNode[];
}

function buildCustomFolderTree(
  folders: Folder[],
  expandedParents: Set<string>,
): SidebarCustomFolderNode[] {
  if (folders.length === 0) return [];

  const delimiter = folders.some((f) => f.path.includes("/")) ? "/" : ".";
  const nodeMap = new Map<string, SidebarCustomFolderNode>();
  const roots: SidebarCustomFolderNode[] = [];

  const sortedFolders = [...folders].sort((a, b) => a.path.localeCompare(b.path));

  for (const folder of sortedFolders) {
    const parts = folder.path.split(delimiter);
    const name = parts[parts.length - 1] || folder.name;
    const parentPath = parts.length > 1 ? parts.slice(0, -1).join(delimiter) : null;

    const node: SidebarCustomFolderNode = {
      type: "custom-folder",
      id: `custom-${folder.id}`,
      folder,
      name,
      depth: parts.length - 1,
      hasChildren: false,
      isExpanded: expandedParents.has(`custom-${folder.id}`),
      children: [],
    };

    nodeMap.set(folder.path, node);

    if (parentPath && nodeMap.has(parentPath)) {
      const parent = nodeMap.get(parentPath)!;
      parent.children.push(node);
      parent.hasChildren = true;
    } else {
      roots.push(node);
    }
  }

  return roots;
}

function buildSections(
  folders: Folder[],
  accounts: AccountSummary[],
  expandedParents: Set<string>,
  updateAvailable: boolean,
): SidebarSection[] {
  const accountMap = new Map(accounts.map((account) => [account.id, account]));
  const accountOrder = new Map(accounts.map((account, index) => [account.id, index]));

  const foldersByType = new Map<FolderType, Folder[]>();
  for (const folder of folders) {
    if (folder.folder_type === "Custom") continue;

    const list = foldersByType.get(folder.folder_type) ?? [];
    list.push(folder);
    foldersByType.set(folder.folder_type, list);
  }

  for (const list of foldersByType.values()) {
    list.sort((left, right) => {
      const leftIndex = accountOrder.get(left.account_id) ?? Number.MAX_SAFE_INTEGER;
      const rightIndex = accountOrder.get(right.account_id) ?? Number.MAX_SAFE_INTEGER;

      if (leftIndex !== rightIndex) {
        return leftIndex - rightIndex;
      }

      return left.name.localeCompare(right.name);
    });
  }

  const buildFolderChildren = (folderType: FolderType): SidebarChildNode[] => {
    const typeFolders = foldersByType.get(folderType) ?? [];
    const children: SidebarChildNode[] = [];

    for (const folder of typeFolders) {
      const account = accountMap.get(folder.account_id);
      if (!account) continue;

      children.push({
        type: "child",
        id: `child-${folder.id}`,
        accountId: account.id,
        accountEmail: account.email,
        folderId: folder.id,
        unreadCount: folder.unread_count,
      });
    }

    return children;
  };

  const buildSyntheticChildren = (syntheticId: string): SidebarChildNode[] =>
    accounts.map((account) => ({
      type: "child" as const,
      id: `child-${syntheticId}-${account.id}`,
      accountId: account.id,
      accountEmail: account.email,
      syntheticId,
    }));

  const topRows: SidebarRowNode[] = [
    {
      type: "parent",
      id: "parent-Inbox",
      folderType: "Inbox",
      name: "Inbox",
      isExpanded: expandedParents.has("parent-Inbox"),
      totalUnread: (foldersByType.get("Inbox") ?? []).reduce((sum, folder) => sum + folder.unread_count, 0),
      variant: "top",
      children: buildFolderChildren("Inbox"),
    },
    {
      type: "synthetic",
      id: "synthetic-starred",
      syntheticId: "starred",
      label: SYNTHETIC_STARRED.label,
      icon: SYNTHETIC_STARRED.icon,
      isExpanded: expandedParents.has("synthetic-starred"),
      variant: "top",
      children: buildSyntheticChildren("starred"),
    },
    {
      type: "folder-entry",
      id: "snoozed",
      label: "Snoozed",
      icon: Clock3,
      target: { type: "synthetic", syntheticId: "snoozed" },
    },
    {
      type: "parent",
      id: "parent-Sent",
      folderType: "Sent",
      name: "Sent",
      isExpanded: expandedParents.has("parent-Sent"),
      totalUnread: (foldersByType.get("Sent") ?? []).reduce((sum, folder) => sum + folder.unread_count, 0),
      variant: "top",
      children: buildFolderChildren("Sent"),
    },
    {
      type: "parent",
      id: "parent-Drafts",
      folderType: "Drafts",
      name: "Drafts",
      isExpanded: expandedParents.has("parent-Drafts"),
      totalUnread: (foldersByType.get("Drafts") ?? []).reduce((sum, folder) => sum + folder.unread_count, 0),
      variant: "top",
      children: buildFolderChildren("Drafts"),
    },
  ];

  const viewsRows: SidebarRowNode[] = [
    {
      type: "synthetic",
      id: "synthetic-pins",
      syntheticId: "pins",
      label: SYNTHETIC_PINS.label,
      icon: SYNTHETIC_PINS.icon,
      isExpanded: expandedParents.has("synthetic-pins"),
      variant: "recents",
      children: buildSyntheticChildren("pins"),
    },
    {
      type: "synthetic",
      id: "synthetic-reminders",
      syntheticId: "reminders",
      label: SYNTHETIC_REMINDERS.label,
      icon: SYNTHETIC_REMINDERS.icon,
      isExpanded: expandedParents.has("synthetic-reminders"),
      variant: "recents",
      children: buildSyntheticChildren("reminders"),
    },
    {
      type: "folder-entry",
      id: "calendar",
      label: "Calendar",
      icon: Calendar,
      target: { type: "synthetic", syntheticId: "calendar" },
    },
  ];

  const folderRows: SidebarRowNode[] = FOLDER_ENTRIES.map((entry) => ({
    type: "folder-entry",
    id: entry.id,
    label: entry.label,
    icon: entry.icon,
    target: entry.target,
  }));

  const footerRows: SidebarRowNode[] = [];

  footerRows.push({
    type: "footer-action",
    id: "footer-settings",
    title: "Settings",
    icon: Settings,
  });

  footerRows.push({
    type: "footer-network",
    id: "footer-network",
  });

  const customFolders = folders.filter((folder) => folder.folder_type === "Custom");
  const customFolderTree = buildCustomFolderTree(customFolders, expandedParents);

  const sections: SidebarSection[] = [
    { id: "top-mailboxes", kind: "top", rows: topRows },
    { id: "views", kind: "recents", title: "Views", rows: viewsRows },
  ];

  if (customFolderTree.length > 0) {
    sections.push({
      id: "custom-folders",
      kind: "folders",
      title: "Folders",
      rows: [
        {
          type: "custom-folders-section",
          id: "custom-folders-section",
          label: "Custom Folders",
          isExpanded: expandedParents.has("custom-folders-section"),
          customFolderTree,
        },
      ],
    });
  }

  sections.push(
    { id: "more", kind: "folders", title: "More", rows: folderRows },
    { id: "footer", kind: "footer", rows: footerRows },
  );

  return sections;
}

interface SidebarProps {
  accounts: AccountSummary[];
  folders: Folder[];
  selectedFolderId: string | null;
  selectedFolderType?: FolderType | null;
  selectedSyntheticId?: string | null;
  onSelectFolder: (folderId: string) => void;
  onSelectAggregate?: (folderType: FolderType) => void;
  onSelectSynthetic?: (id: string) => void;
  onOpenSettings: () => void;
  loading: boolean;
  updateAvailable?: boolean;
  updateVersion?: string | null;
  onUpdateClick?: () => void;
  onMoveEmail?: (emailId: string, targetFolderId: string) => void;
  collapsed?: boolean;
  onToggleCollapsed?: () => void;
  onCreateFolder?: (accountId: string, folderName: string) => void;
  onRenameFolder?: (accountId: string, folderId: string, newName: string) => void;
  onDeleteFolder?: (accountId: string, folderId: string) => void;
  isOnline?: boolean;
  onSearchSelect?: (query: string) => void;
  authErrors?: AuthError[];
}

export function Sidebar({
  accounts,
  folders,
  selectedFolderId,
  selectedFolderType,
  selectedSyntheticId,
  onSelectFolder,
  onSelectAggregate,
  onSelectSynthetic,
  onOpenSettings,
  loading,
  updateAvailable,
  updateVersion,
  onUpdateClick,
  onMoveEmail,
  collapsed = false,
  onToggleCollapsed,
  onCreateFolder,
  onRenameFolder,
  onDeleteFolder,
  isOnline = true,
  onSearchSelect,
  authErrors = [],
}: SidebarProps) {
  const [expandedParents, setExpandedParents] = useState<Set<string>>(() => {
    try {
      const stored = localStorage.getItem("maho-folder-tree-expanded");
      if (stored) return new Set(JSON.parse(stored) as string[]);
    } catch { /* ignore */ }
    return new Set(["parent-Inbox"]);
  });

  const [contextMenu, setContextMenu] = useState<{
    x: number;
    y: number;
    folderId: string;
    accountId: string;
    folderPath: string;
    folderName: string;
  } | null>(null);

  const [accountContextMenu, setAccountContextMenu] = useState<{
    x: number;
    y: number;
    accountId: string;
    accountEmail: string;
  } | null>(null);

  const [reconnectingAccountId, setReconnectingAccountId] = useState<string | null>(null);

  const [renamingFolderId, setRenamingFolderId] = useState<string | null>(null);
  const [renameValue, setRenameValue] = useState("");
  const renameInputRef = useRef<HTMLInputElement>(null);
  const contextMenuRef = useRef<HTMLDivElement>(null);
  const accountContextMenuRef = useRef<HTMLDivElement>(null);

  const [newSubfolderParent, setNewSubfolderParent] = useState<{ accountId: string; parentPath: string } | null>(null);
  const [newSubfolderName, setNewSubfolderName] = useState("");
  const newSubfolderInputRef = useRef<HTMLInputElement>(null);

  const confirm = useConfirm();
  const { toast } = useToast();
  const { t } = useTranslation();

  const [savedSearches, setSavedSearches] = useState<SavedSearch[]>([]);

  const loadSavedSearches = useCallback(() => {
    void api.listSavedSearches().then(setSavedSearches).catch(() => { /* ignore */ });
  }, []);

  useEffect(() => {
    loadSavedSearches();
  }, [loadSavedSearches]);

  useEffect(() => {
    const handler = () => loadSavedSearches();
    window.addEventListener("maho-saved-searches-changed", handler);
    return () => window.removeEventListener("maho-saved-searches-changed", handler);
  }, [loadSavedSearches]);

  const handleDeleteSavedSearch = useCallback(async (id: string) => {
    try {
      await api.deleteSavedSearch(id);
      setSavedSearches((prev) => prev.filter((s) => s.id !== id));
    } catch { /* ignore */ }
  }, []);

  const handleSavedSearchClick = useCallback((query: string) => {
    onSearchSelect?.(query);
  }, [onSearchSelect]);

  const labelMap = useMemo(() => ({
    "Inbox": t("sidebar.inbox"),
    "Sent": t("sidebar.sent"),
    "Drafts": t("sidebar.drafts"),
    "Trash": t("sidebar.trash"),
    "Spam": t("sidebar.spam"),
    "Starred": t("sidebar.starred"),
    "Archive": t("sidebar.archive"),
    "Pins": t("sidebar.pins"),
    "Snoozed": t("sidebar.snoozed"),
    "Reminders": t("sidebar.reminders"),
    "More": t("sidebar.more"),
    "Settings": t("sidebar.settings"),
    "Update available": t("sidebar.updateAvailable"),
    "Folders": t("sidebar.folders"),
    "Custom Folders": t("sidebar.customFolders"),
    "Recents": t("sidebar.recents"),
    "Saved Searches": t("sidebar.savedSearches"),
    "Clear": t("sidebar.clear"),

  } as Record<string, string>), [t]);

  const sidebarLabel = useCallback((key: string) => labelMap[key] ?? key, [labelMap]);

  useEffect(() => {
    const arr = Array.from(expandedParents);
    localStorage.setItem("maho-folder-tree-expanded", JSON.stringify(arr));
  }, [expandedParents]);

  useEffect(() => {
    if (renamingFolderId && renameInputRef.current) {
      renameInputRef.current.focus();
      renameInputRef.current.select();
    }
  }, [renamingFolderId]);

  useEffect(() => {
    if (newSubfolderParent && newSubfolderInputRef.current) {
      newSubfolderInputRef.current.focus();
    }
  }, [newSubfolderParent]);

  useEffect(() => {
    if (!contextMenu && !accountContextMenu) return;
    const trigger = document.activeElement;
    const menu = contextMenu ? contextMenuRef.current : accountContextMenuRef.current;
    const items = Array.from(menu?.querySelectorAll<HTMLButtonElement>('[role="menuitem"]') ?? []);
    items[0]?.focus();
    const onKeyDown = (event: KeyboardEvent) => {
      if (event.key === 'Escape' || event.key === 'Tab') {
        if (event.key === 'Escape') event.preventDefault();
        setContextMenu(null);
        setAccountContextMenu(null);
        if (trigger instanceof HTMLElement) trigger.focus();
      } else if (['ArrowDown', 'ArrowUp', 'Home', 'End'].includes(event.key)) {
        event.preventDefault();
        const index = items.findIndex(item => item === document.activeElement);
        const next = event.key === 'Home' ? 0 : event.key === 'End' ? items.length - 1
          : (index + (event.key === 'ArrowDown' ? 1 : -1) + items.length) % items.length;
        items[next]?.focus();
      }
    };
    const handler = (e: MouseEvent) => {
      if (contextMenuRef.current && !contextMenuRef.current.contains(e.target as Node)) {
        setContextMenu(null);
      }
      if (accountContextMenuRef.current && !accountContextMenuRef.current.contains(e.target as Node)) {
        setAccountContextMenu(null);
      }
    };
    document.addEventListener("mousedown", handler);
    menu?.addEventListener('keydown', onKeyDown);
    return () => {
      document.removeEventListener("mousedown", handler);
      menu?.removeEventListener('keydown', onKeyDown);
    };
  }, [contextMenu, accountContextMenu]);

  const authErrorAccountIds = useMemo(
    () => new Set(authErrors.map((error) => error.account_id)),
    [authErrors],
  );

  const sections = useMemo(
    () => buildSections(folders, accounts, expandedParents, updateAvailable ?? false),
    [folders, accounts, expandedParents, updateAvailable],
  );

  const footerSection = sections.find((section) => section.kind === "footer");
  const contentSections = sections.filter((section) => section.kind !== "footer");

  const toggleParent = useCallback((parentId: string) => {
    setExpandedParents((prev) => {
      const next = new Set(prev);
      if (next.has(parentId)) {
        next.delete(parentId);
      } else {
        next.add(parentId);
      }
      return next;
    });
  }, []);

  const handleAggregateSelect = useCallback(
    (folderType: FolderType) => {
      onSelectAggregate?.(folderType);
    },
    [onSelectAggregate],
  );

  const handleFolderSelect = useCallback(
    (folderId: string) => {
      onSelectFolder(folderId);
    },
    [onSelectFolder],
  );

  const handleSyntheticSelect = useCallback(
    (syntheticId: string) => {
      onSelectSynthetic?.(syntheticId);
    },
    [onSelectSynthetic],
  );

  const handleReconnectAccount = useCallback(async (accountId: string) => {
    setReconnectingAccountId(accountId);
    try {
      await api.reconnectAccount(accountId);
    } catch (error) {
      toast("error", getErrorMessage(error, t("auth.reconnectFailed")));
    } finally {
      setReconnectingAccountId((current) => current === accountId ? null : current);
    }
  }, [t, toast]);

  const handleAccountContextMenu = useCallback((e: React.MouseEvent, child: SidebarChildNode) => {
    e.preventDefault();
    setContextMenu(null);
    setAccountContextMenu({
      x: e.clientX,
      y: e.clientY,
      accountId: child.accountId,
      accountEmail: child.accountEmail,
    });
  }, []);

  const handleReconnectFromMenu = useCallback(() => {
    if (!accountContextMenu) return;
    const accountId = accountContextMenu.accountId;
    setAccountContextMenu(null);
    void handleReconnectAccount(accountId);
  }, [accountContextMenu, handleReconnectAccount]);

  const renderChildRow = (child: SidebarChildNode) => {
    const isFolderChild = Boolean(child.folderId);
    const isSelected = isFolderChild ? child.folderId === selectedFolderId : child.syntheticId === selectedSyntheticId;
    const hasAuthError = authErrorAccountIds.has(child.accountId);
    const isReconnecting = reconnectingAccountId === child.accountId;
    const reconnectRequiredLabel = t("auth.reconnectRequiredShort");

    const rowClassName = `flex w-full items-center gap-2 rounded-xl px-2 py-1 text-left transition-colors focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring ${
      isSelected
        ? "mail-nav-current bg-primary/12 text-foreground"
        : "text-muted-foreground hover:bg-surface-hover hover:text-foreground"
    }`;

    const mainButtonClassName = "flex min-w-0 flex-1 items-center gap-2 text-left focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring";

    const content = (
      <>
        <Avatar name={child.accountEmail} size="sm" className={isSelected ? "ring-1 ring-white/10" : "opacity-90"} />
        <span className={`min-w-0 flex-1 truncate text-[12px] ${isSelected ? "font-medium text-foreground" : "text-muted-foreground"}`}>
          {child.accountEmail}
        </span>
        {(child.unreadCount ?? 0) > 0 && (
          <span
            className={`shrink-0 rounded-full px-1.5 py-0.5 text-[10px] font-medium ${
              isSelected ? "bg-primary/20 text-primary-foreground" : "bg-card/80 text-muted-foreground"
            }`}
          >
            {child.unreadCount}
          </span>
        )}
      </>
    );

    const authBadge = hasAuthError ? (
      <button
        type="button"
        onClick={(event) => {
          event.stopPropagation();
          void handleReconnectAccount(child.accountId);
        }}
        disabled={isReconnecting}
        title={reconnectRequiredLabel}
        aria-label={reconnectRequiredLabel}
        className="flex h-5 w-5 shrink-0 items-center justify-center rounded-full text-destructive transition-colors hover:bg-destructive/10 focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring disabled:opacity-60"
      >
        {isReconnecting ? <Loader2 size={13} className="animate-spin" /> : <AlertCircle size={13} />}
      </button>
    ) : null;

    const row = (
      <div key={child.id} className={rowClassName}>
        <button
          type="button"
          onContextMenu={(event) => handleAccountContextMenu(event, child)}
          onClick={() => {
            if (child.folderId) {
              handleFolderSelect(child.folderId);
              return;
            }
            handleSyntheticSelect(child.syntheticId!);
          }}
          className={mainButtonClassName}
        >
          {content}
        </button>
        {authBadge}
      </div>
    );

    if (child.folderId) {
      const folderChild = child as FolderChildNode;

      return (
        <DropTargetFolder
          key={child.id}
          folderId={folderChild.folderId}
          onDrop={(emailId, targetFolderId) => onMoveEmail?.(emailId, targetFolderId)}
        >
          {row}
        </DropTargetFolder>
      );
    }

    return row;
  };

  const renderExpandableRow = ({
    id,
    label,
    icon: Icon,
    isExpanded,
    isSelected,
    onSelect,
    onToggle,
    totalUnread,
    children,
  }: {
    id: string;
    label: string;
    icon: typeof Inbox;
    isExpanded: boolean;
    isSelected: boolean;
    onSelect: () => void;
    onToggle: () => void;
    totalUnread?: number;
    children: SidebarChildNode[];
  }) => {
    const hasChildren = children.length > 0;

    return (
      <div key={id} className="group">
        <div
          className={`flex items-center gap-1 rounded-2xl px-2 py-1.5 transition-colors ${
            isSelected
              ? "mail-nav-current bg-primary/12 text-foreground"
              : "text-muted-foreground hover:bg-surface-hover hover:text-foreground"
          }`}
        >
          <button
            type="button"
            onClick={onToggle}
            className={`${LEADING_SLOT_CLASSNAME} rounded-md text-muted-foreground transition-colors hover:bg-surface-hover hover:text-foreground focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring`}
            aria-label={`${isExpanded ? "Collapse" : "Expand"} ${label}`}
            aria-expanded={isExpanded}
          >
            {hasChildren ? <ChevronRight size={12} className={cn("mail-disclosure", isExpanded && "rotate-90")} /> : null}
          </button>

          <button
            type="button"
            onClick={onSelect}
            aria-current={isSelected ? "page" : undefined}
            className="flex min-w-0 flex-1 items-center gap-2 text-left focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring"
          >
            <div
              className={`flex h-7 w-7 shrink-0 items-center justify-center rounded-xl ${
                isSelected ? "bg-primary/20 text-primary-foreground" : "bg-card/80 text-muted-foreground"
              }`}
            >
              <Icon size={14} />
            </div>

            <span className={`min-w-0 flex-1 truncate text-[13px] font-medium ${isSelected ? "text-foreground" : "text-foreground"}`}>
              {label}
            </span>

            {(totalUnread ?? 0) > 0 && (
              <span
                className={`shrink-0 rounded-full px-1.5 py-0.5 text-[10px] font-semibold ${
                  isSelected ? "bg-primary/20 text-primary-foreground" : "bg-card/90 text-muted-foreground"
                }`}
              >
                {totalUnread}
              </span>
            )}
          </button>
        </div>

        {isExpanded && hasChildren && <div className="ml-1.5 mt-1 space-y-0.5 border-l border-border/80 pl-2">{children.map(renderChildRow)}</div>}
      </div>
    );
  };

  const handleFolderContextMenu = useCallback(
    (e: React.MouseEvent, folder: Folder) => {
      e.preventDefault();
      setAccountContextMenu(null);
      setContextMenu({
        x: e.clientX,
        y: e.clientY,
        folderId: folder.id,
        accountId: folder.account_id,
        folderPath: folder.path,
        folderName: folder.name,
      });
    },
    [],
  );

  const handleCreateSubfolder = useCallback(() => {
    if (!contextMenu) return;
    setNewSubfolderParent({ accountId: contextMenu.accountId, parentPath: contextMenu.folderPath });
    setNewSubfolderName("");
    setContextMenu(null);
  }, [contextMenu]);

  const handleNewSubfolderCommit = useCallback(() => {
    if (newSubfolderParent && newSubfolderName.trim()) {
      const delimiter = newSubfolderParent.parentPath.includes("/") ? "/" : ".";
      const fullPath = `${newSubfolderParent.parentPath}${delimiter}${newSubfolderName.trim()}`;
      onCreateFolder?.(newSubfolderParent.accountId, fullPath);
    }
    setNewSubfolderParent(null);
    setNewSubfolderName("");
  }, [newSubfolderParent, newSubfolderName, onCreateFolder]);

  const handleNewSubfolderCancel = useCallback(() => {
    setNewSubfolderParent(null);
    setNewSubfolderName("");
  }, []);

  const handleRenameStart = useCallback(() => {
    if (!contextMenu) return;
    setRenamingFolderId(contextMenu.folderId);
    setRenameValue(contextMenu.folderName);
    setContextMenu(null);
  }, [contextMenu]);

  const handleRenameCommit = useCallback(
    (folderId: string, accountId: string) => {
      if (renameValue.trim() && onRenameFolder) {
        onRenameFolder(accountId, folderId, renameValue.trim());
      }
      setRenamingFolderId(null);
      setRenameValue("");
    },
    [renameValue, onRenameFolder],
  );

  const handleDeleteFromMenu = useCallback(async () => {
    if (!contextMenu) return;
    const confirmed = await confirm({
      title: t("sidebar.deleteFolderTitle"),
      message: t("sidebar.deleteFolderConfirm", { name: contextMenu.folderName }),
      confirmLabel: t("common.delete"),
      danger: true,
    });
    if (confirmed) {
      onDeleteFolder?.(contextMenu.accountId, contextMenu.folderId);
    }
    setContextMenu(null);
  }, [contextMenu, onDeleteFolder, confirm, t]);

  const renderCustomFolderNode = (node: SidebarCustomFolderNode): React.ReactNode => {
    const isSelected = selectedFolderId === node.folder.id;
    const isRenaming = renamingFolderId === node.folder.id;
    const isCreatingSubfolder = newSubfolderParent?.parentPath === node.folder.path;

    return (
      <div key={node.id} className="group">
        <div
          className={`flex items-center gap-1 rounded-2xl px-2 py-1 transition-colors ${
            isSelected
              ? "mail-nav-current bg-primary/12 text-foreground"
              : "text-muted-foreground hover:bg-surface-hover hover:text-foreground"
          }`}
        >
          <button
            type="button"
            onClick={() => node.hasChildren && toggleParent(node.id)}
            className={`${LEADING_SLOT_CLASSNAME} rounded-md text-muted-foreground transition-colors hover:bg-surface-hover hover:text-foreground focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring`}
            aria-label={node.hasChildren ? `${node.isExpanded ? "Collapse" : "Expand"} ${node.name}` : undefined}
          >
            {node.hasChildren ? <ChevronRight size={12} className={cn("mail-disclosure", node.isExpanded && "rotate-90")} /> : null}
          </button>

          {isRenaming ? (
            <input
              ref={renameInputRef}
              type="text"
              value={renameValue}
              onChange={(e) => setRenameValue(e.target.value)}
              onBlur={() => handleRenameCommit(node.folder.id, node.folder.account_id)}
              onKeyDown={(e) => {
                if (e.key === "Enter") handleRenameCommit(node.folder.id, node.folder.account_id);
                if (e.key === "Escape") { setRenamingFolderId(null); setRenameValue(""); }
              }}
              className="min-w-0 flex-1 rounded bg-card px-1 py-0.5 text-[13px] text-foreground outline-none ring-1 ring-primary/40"
            />
          ) : (
            <button
              type="button"
              onContextMenu={(e) => handleFolderContextMenu(e, node.folder)}
              onClick={() => handleFolderSelect(node.folder.id)}
              className="flex min-w-0 flex-1 items-center gap-2 text-left focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring"
            >
              <div
                className={`flex h-6 w-6 shrink-0 items-center justify-center rounded-lg ${
                  isSelected ? "bg-primary/20 text-primary-foreground" : "bg-card/80 text-muted-foreground"
                }`}
              >
                <FolderIcon size={12} />
              </div>
              <span className={`min-w-0 flex-1 truncate text-[12px] font-medium ${isSelected ? "text-foreground" : "text-foreground"}`}>
                {node.name}
              </span>
            </button>
          )}

          <button
            type="button"
            onClick={(e) => handleFolderContextMenu(e, node.folder)}
            className="flex h-6 w-6 shrink-0 items-center justify-center rounded-md text-muted-foreground opacity-0 transition-opacity hover:bg-surface-hover hover:text-foreground group-hover:opacity-100 group-focus-within:opacity-100 focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring"
            aria-label={`Actions for ${node.name}`}
          >
            <Plus size={12} />
          </button>
        </div>

        {node.isExpanded && node.hasChildren && (
          <div className="ml-1.5 mt-0.5 space-y-0.5 border-l border-border/80 pl-2">
            {node.children.map(renderCustomFolderNode)}
          </div>
        )}

        {isCreatingSubfolder && (
          <div className="ml-1.5 mt-0.5 border-l border-border/80 pl-2">
            <div className="flex items-center gap-1 rounded-2xl px-2 py-1">
              <span className={LEADING_SLOT_CLASSNAME} />
              <input
                ref={newSubfolderInputRef}
                type="text"
                value={newSubfolderName}
                onChange={(e) => setNewSubfolderName(e.target.value)}
                onBlur={handleNewSubfolderCommit}
                onKeyDown={(e) => {
                  if (e.key === "Enter") handleNewSubfolderCommit();
                  if (e.key === "Escape") handleNewSubfolderCancel();
                }}
                placeholder={t("sidebar.newFolderPlaceholder")}
                className="min-w-0 flex-1 rounded bg-card px-1 py-0.5 text-[13px] text-foreground outline-none ring-1 ring-primary/40"
              />
            </div>
          </div>
        )}
      </div>
    );
  };

  const renderRow = (node: SidebarRowNode) => {
    if (node.type === "parent") {
      const Icon = FOLDER_TYPE_ICONS[node.folderType] ?? FolderIcon;

      return renderExpandableRow({
        id: node.id,
        label: sidebarLabel(node.name),
        icon: Icon,
        isExpanded: node.isExpanded,
        isSelected: selectedFolderType === node.folderType,
        onSelect: () => handleAggregateSelect(node.folderType),
        onToggle: () => toggleParent(node.id),
        totalUnread: node.totalUnread,
        children: node.children,
      });
    }

    if (node.type === "child") {
      return null;
    }

    if (node.type === "folder-entry") {
      const Icon = node.icon;
      const isSelected = node.target?.type === "aggregate"
        ? selectedFolderType === node.target.folderType
        : node.target?.type === "synthetic"
          ? selectedSyntheticId === node.target.syntheticId
          : false;

      if (!node.target) {
        return (
          <div key={node.id} className="flex w-full items-center gap-1 rounded-2xl px-2 py-1.5 text-muted-foreground">
            <span aria-hidden="true" className={LEADING_SLOT_CLASSNAME} />
            <div className="flex h-7 w-7 shrink-0 items-center justify-center rounded-xl bg-card/80 text-muted-foreground">
              <Icon size={14} />
            </div>
            <span className="min-w-0 flex-1 truncate text-[13px] font-medium text-foreground">{sidebarLabel(node.label)}</span>
          </div>
        );
      }

      const target = node.target;

      return (
        <button
          key={node.id}
          type="button"
          onClick={() => {
            if (target.type === "aggregate") {
              handleAggregateSelect(target.folderType);
              return;
            }

            handleSyntheticSelect(target.syntheticId);
          }}
          aria-current={isSelected ? "page" : undefined}
          className={`flex w-full items-center gap-1 rounded-2xl px-2 py-1.5 text-left transition-colors focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring ${
            isSelected
              ? "mail-nav-current bg-primary/12 text-foreground"
              : "text-muted-foreground hover:bg-surface-hover hover:text-foreground"
          }`}
        >
          <span aria-hidden="true" className={LEADING_SLOT_CLASSNAME} />
          <div
            className={`flex h-7 w-7 shrink-0 items-center justify-center rounded-xl ${
              isSelected ? "bg-primary/20 text-primary-foreground" : "bg-card/80 text-muted-foreground"
            }`}
          >
            <Icon size={14} />
          </div>
          <span className={`min-w-0 flex-1 truncate text-[13px] font-medium ${isSelected ? "text-foreground" : "text-foreground"}`}>
            {sidebarLabel(node.label)}
          </span>
        </button>
      );
    }

    if (node.type === "synthetic") {
      return renderExpandableRow({
        id: node.id,
        label: sidebarLabel(node.label),
        icon: node.icon,
        isExpanded: node.isExpanded,
        isSelected: selectedSyntheticId === node.syntheticId,
        onSelect: () => handleSyntheticSelect(node.syntheticId),
        onToggle: () => toggleParent(node.id),
        children: node.children,
      });
    }

    if (node.type === "footer-status") {
      const Icon = node.icon;

      return (
        <button
          key={node.id}
          type="button"
          onClick={onUpdateClick}
          className="flex w-full items-center gap-2.5 rounded-2xl px-2 py-2 text-left text-sm text-muted-foreground transition-colors hover:bg-surface-hover hover:text-foreground focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring"
        >
          <div className="flex h-7 w-7 shrink-0 items-center justify-center rounded-xl bg-card/80 text-amber-300">
            <Icon size={14} />
          </div>
          <div className="min-w-0 flex-1 truncate text-[13px] font-medium text-foreground">{sidebarLabel(node.title)}</div>
          {updateVersion && <span className="text-[10px] uppercase tracking-[0.18em] text-muted-foreground">v{updateVersion}</span>}
        </button>
      );
    }

    if (node.type === "footer-action") {
      const Icon = node.icon;

      return (
        <button
          key={node.id}
          type="button"
          onClick={onOpenSettings}
          className="flex w-full items-center gap-2.5 rounded-2xl px-2 py-2 text-left text-sm text-muted-foreground transition-colors hover:bg-surface-hover hover:text-foreground focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring"
        >
          <div className="flex h-7 w-7 shrink-0 items-center justify-center rounded-xl bg-card/80 text-muted-foreground">
            <Icon size={14} />
          </div>
          <div className="min-w-0 flex-1 truncate text-[13px] font-medium text-foreground">{sidebarLabel(node.title)}</div>
        </button>
      );
    }

    if (node.type === "footer-network") {
      if (isOnline) return null;
      return (
        <div
          key={node.id}
          className="flex items-center gap-2 px-3 py-1.5"
          role="status"
          aria-label={isOnline ? t("a11y.networkOnline") : t("a11y.networkOffline")}
        >
          {isOnline ? (
            <Wifi size={14} className="text-green-500" />
          ) : (
            <WifiOff size={14} className="text-destructive" />
          )}
          <span className="text-[11px] text-muted-foreground">
            {isOnline ? t("sidebar.online") : t("sidebar.offline")}
          </span>
        </div>
      );
    }

    if (node.type === "custom-folder") {
      return renderCustomFolderNode(node);
    }

    if (node.type === "custom-folders-section") {
      return (
        <div key={node.id} className="group">
          <div
            className="flex items-center gap-1 rounded-2xl px-2 py-1.5 text-muted-foreground transition-colors hover:bg-surface-hover hover:text-foreground"
          >
            <button
              type="button"
              onClick={() => toggleParent(node.id)}
              className={`${LEADING_SLOT_CLASSNAME} rounded-md text-muted-foreground transition-colors hover:bg-surface-hover hover:text-foreground focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring`}
              aria-label={`${node.isExpanded ? "Collapse" : "Expand"} ${sidebarLabel(node.label)} section`}
              aria-expanded={node.isExpanded}
            >
              <ChevronRight size={12} className={cn("mail-disclosure", node.isExpanded && "rotate-90")} />
            </button>

            <button
              type="button"
              onClick={() => toggleParent(node.id)}
              className="flex min-w-0 flex-1 items-center gap-2 text-left focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring"
            >
              <div className="flex h-7 w-7 shrink-0 items-center justify-center rounded-xl bg-card/80 text-muted-foreground">
                <FolderIcon size={14} />
              </div>
              <span className="min-w-0 flex-1 truncate text-[13px] font-medium text-foreground">
                {sidebarLabel(node.label)}
              </span>
            </button>
          </div>

          {node.isExpanded && (
            <div className="ml-1.5 mt-1 space-y-0.5 border-l border-border/80 pl-2">
              {node.customFolderTree.map(renderCustomFolderNode)}
            </div>
          )}
        </div>
      );
    }

    return null;
  };

  const renderCollapsedRow = (node: SidebarRowNode) => {
    if (node.type === "child") return null;

    if (node.type === "parent") {
      const Icon = FOLDER_TYPE_ICONS[node.folderType] ?? FolderIcon;
      const isSelected = selectedFolderType === node.folderType && selectedSyntheticId == null;

      return (
        <button
          key={node.id}
          type="button"
          onClick={() => handleAggregateSelect(node.folderType)}
          className={`flex h-10 w-10 items-center justify-center rounded-2xl transition-colors focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring ${
            isSelected
              ? "mail-nav-current bg-primary/12 text-foreground"
              : "text-muted-foreground hover:bg-surface-hover hover:text-foreground"
          }`}
          aria-label={sidebarLabel(node.name)}
          aria-current={isSelected ? "page" : undefined}
          title={sidebarLabel(node.name)}
        >
          <Icon size={18} />
        </button>
      );
    }

    if (node.type === "synthetic") {
      const Icon = node.icon;
      const isSelected = selectedSyntheticId === node.syntheticId;

      return (
        <button
          key={node.id}
          type="button"
          onClick={() => handleSyntheticSelect(node.syntheticId)}
          className={`flex h-10 w-10 items-center justify-center rounded-2xl transition-colors focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring ${
            isSelected
              ? "mail-nav-current bg-primary/12 text-foreground"
              : "text-muted-foreground hover:bg-surface-hover hover:text-foreground"
          }`}
          aria-label={sidebarLabel(node.label)}
          aria-current={isSelected ? "page" : undefined}
          title={sidebarLabel(node.label)}
        >
          <Icon size={18} />
        </button>
      );
    }

    if (node.type === "folder-entry") {
      const Icon = node.icon;

      if (!node.target) {
        return (
          <div
            key={node.id}
            className="flex h-10 w-10 items-center justify-center rounded-2xl text-muted-foreground"
            role="img"
            aria-label={sidebarLabel(node.label)}
            title={sidebarLabel(node.label)}
          >
            <Icon size={18} />
          </div>
        );
      }

      const target = node.target;
      const isSelected = target.type === "aggregate"
        ? selectedFolderType === target.folderType && selectedSyntheticId == null
        : selectedSyntheticId === target.syntheticId;

      return (
        <button
          key={node.id}
          type="button"
          onClick={() => {
            if (target.type === "aggregate") {
              handleAggregateSelect(target.folderType);
              return;
            }
            handleSyntheticSelect(target.syntheticId);
          }}
          className={`flex h-10 w-10 items-center justify-center rounded-2xl transition-colors focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring ${
            isSelected
              ? "mail-nav-current bg-primary/12 text-foreground"
              : "text-muted-foreground hover:bg-surface-hover hover:text-foreground"
          }`}
          aria-label={sidebarLabel(node.label)}
          aria-current={isSelected ? "page" : undefined}
          title={sidebarLabel(node.label)}
        >
          <Icon size={18} />
        </button>
      );
    }

    if (node.type === "footer-status") {
      const Icon = node.icon;

      return (
        <button
          key={node.id}
          type="button"
          onClick={onUpdateClick}
          className="flex h-10 w-10 items-center justify-center rounded-2xl text-amber-300 transition-colors hover:bg-card/60 focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring"
          aria-label={sidebarLabel(node.title)}
          title={sidebarLabel(node.title)}
        >
          <Icon size={18} />
        </button>
      );
    }

    if (node.type === "footer-action") {
      const Icon = node.icon;

      return (
        <button
          key={node.id}
          type="button"
          onClick={onOpenSettings}
          className="flex h-10 w-10 items-center justify-center rounded-2xl text-muted-foreground transition-colors hover:bg-surface-hover hover:text-foreground focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring"
          aria-label={sidebarLabel(node.title)}
          title={sidebarLabel(node.title)}
        >
          <Icon size={18} />
        </button>
      );
    }

    if (node.type === "footer-network") {
      if (isOnline) return null;
      return (
        <div
          key={node.id}
          className="flex h-10 w-10 items-center justify-center rounded-2xl"
          title={isOnline ? t("sidebar.online") : t("sidebar.offline")}
          role="status"
          aria-label={isOnline ? t("a11y.networkOnline") : t("a11y.networkOffline")}
        >
          {isOnline ? (
            <Wifi size={16} className="text-green-500" />
          ) : (
            <WifiOff size={16} className="text-destructive" />
          )}
        </div>
      );
    }

    return null;
  };

  if (collapsed) {
    return (
      <aside className="relative flex h-full flex-col overflow-visible border-r border-border bg-background">
        {loading && (
          <div className="h-0.5 w-full overflow-hidden bg-card">
            <div className="h-full w-1/3 animate-[progress_1.5s_ease-in-out_infinite] bg-accent" />
          </div>
        )}

        {onToggleCollapsed && (
          <button
            type="button"
            onClick={onToggleCollapsed}
            className={SHELL_TOGGLE_CLASSNAME}
            aria-label={t("a11y.expandSidebar")}
            title={t("a11y.expandSidebar")}
          >
            <ChevronRight size={12} />
          </button>
        )}

        <nav className="flex min-h-0 flex-1 flex-col px-3 pb-3 pt-12" aria-label={t("a11y.mailFolders")}>
          <div className="space-y-1">
            {contentSections.map((section) =>
              section.rows.map(renderCollapsedRow),
            )}
          </div>

          <div className="mt-auto border-t border-border/80 pt-3">
            <div className="space-y-1">
              {footerSection?.rows.map(renderCollapsedRow)}
            </div>
          </div>
        </nav>
      </aside>
    );
  }

  return (
    <aside className="relative flex h-full flex-col overflow-visible border-r border-border bg-background">
      {loading && (
        <div className="h-0.5 w-full overflow-hidden bg-card">
          <div className="h-full w-1/3 animate-[progress_1.5s_ease-in-out_infinite] bg-accent" />
        </div>
      )}

      {onToggleCollapsed && (
        <button
          type="button"
          onClick={onToggleCollapsed}
          className={SHELL_TOGGLE_CLASSNAME}
          aria-label={t("a11y.collapseSidebar")}
          title={t("a11y.collapseSidebar")}
        >
          <ChevronLeft size={12} />
        </button>
      )}

      <div className="flex-1 overflow-y-auto px-3 pb-3 pt-12">
        <nav className="flex min-h-full flex-col" aria-label={t("a11y.mailFolders")}>
          <div className="space-y-5">
            {contentSections.map((section) => (
              <section key={section.id} aria-labelledby={`${section.id}-heading`}>
                {section.title && (
                  <div className="mb-2 flex items-center justify-between px-1">
                    <h2 id={`${section.id}-heading`} className="text-[11px] font-semibold tracking-wide text-muted-foreground">{sidebarLabel(section.title)}</h2>
                    {section.action &&
                      (section.action.onClick ? (
                        <button
                          type="button"
                          onClick={section.action.onClick}
                          className="text-[11px] font-medium tracking-wide text-muted-foreground transition-colors hover:text-foreground focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring"
                        >
                          {sidebarLabel(section.action.label)}
                        </button>
                      ) : (
                        <span className="text-[11px] font-medium tracking-wide text-muted-foreground">{sidebarLabel(section.action.label)}</span>
                      ))}
                  </div>
                )}

                <div id={`${section.id}-rows`} className="space-y-0.5">{section.rows.map(renderRow)}</div>
              </section>
            ))}

            {savedSearches.length > 0 && (
              <section aria-labelledby="saved-searches-heading">
                <div className="mb-2 flex items-center justify-between px-1">
                  <h2 id="saved-searches-heading" className="text-[11px] font-semibold tracking-wide text-muted-foreground">
                    {sidebarLabel("Saved Searches")}
                  </h2>
                </div>
                <div className="space-y-0.5">
                  {savedSearches.map((ss) => (
                    <div key={ss.id} className="group flex items-center gap-1 rounded-2xl px-2 py-1.5 text-muted-foreground transition-colors hover:bg-surface-hover hover:text-foreground">
                      <span aria-hidden="true" className={LEADING_SLOT_CLASSNAME} />
                      <button
                        type="button"
                        onClick={() => handleSavedSearchClick(ss.query)}
                        className="flex min-w-0 flex-1 items-center gap-2 text-left focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring"
                      >
                        <div className="flex h-7 w-7 shrink-0 items-center justify-center rounded-xl bg-card/80 text-muted-foreground">
                          <Search size={14} />
                        </div>
                        <span className="min-w-0 flex-1 truncate text-[13px] font-medium text-foreground">
                          {ss.name}
                        </span>
                      </button>
                      <button
                        type="button"
                        onClick={() => void handleDeleteSavedSearch(ss.id)}
                        className="flex h-6 w-6 shrink-0 items-center justify-center rounded-md text-muted-foreground opacity-0 transition-opacity hover:bg-surface-hover hover:text-destructive group-hover:opacity-100 group-focus-within:opacity-100 focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring"
                        aria-label={`Delete ${ss.name}`}
                      >
                        <Trash2 size={12} />
                      </button>
                    </div>
                  ))}
                </div>
              </section>
            )}
          </div>

          {footerSection && (
            <section className="mt-auto border-t border-border/80 pt-3" aria-label={t("a11y.globalActions")}>
              <div className="space-y-1">
                {footerSection.rows.map(renderRow)}
              </div>
            </section>
          )}
        </nav>
      </div>

      {contextMenu && (
        <div
          ref={contextMenuRef}
          role="menu"
          className="fixed z-50 min-w-[160px] rounded-xl border border-border bg-background py-1 shadow-lg"
          style={(() => {
            const menuWidth = 192; // w-48 = 12rem = 192px
            const menuHeight = 120; // approximate 3-item menu height
            const clampedX = Math.min(contextMenu.x, window.innerWidth - menuWidth);
            const clampedY = Math.min(contextMenu.y, window.innerHeight - menuHeight);
            return { left: clampedX, top: clampedY };
          })()}
        >
          <button
            type="button"
            role="menuitem"
            onClick={handleCreateSubfolder}
            className="flex w-full items-center gap-2 px-3 py-1.5 text-left text-[13px] text-foreground transition-colors hover:bg-card/80"
          >
            <Plus size={14} className="text-muted-foreground" />
            {t("sidebar.newSubfolder")}
          </button>
          <button
            type="button"
            role="menuitem"
            onClick={handleRenameStart}
            className="flex w-full items-center gap-2 px-3 py-1.5 text-left text-[13px] text-foreground transition-colors hover:bg-card/80"
          >
            <FileText size={14} className="text-muted-foreground" />
            {t("sidebar.rename")}
          </button>
          <hr className="my-1 border-border/60" />
          <button
            type="button"
            role="menuitem"
            onClick={handleDeleteFromMenu}
            className="flex w-full items-center gap-2 px-3 py-1.5 text-left text-[13px] text-destructive transition-colors hover:bg-destructive/10"
          >
            <Trash2 size={14} />
            {t("common.delete")}
          </button>
        </div>
      )}
      {accountContextMenu && (
        <div
          ref={accountContextMenuRef}
          role="menu"
          className="fixed z-50 min-w-[160px] rounded-xl border border-border bg-background py-1 shadow-lg"
          style={(() => {
            const menuWidth = 192;
            const menuHeight = 48;
            const clampedX = Math.min(accountContextMenu.x, window.innerWidth - menuWidth);
            const clampedY = Math.min(accountContextMenu.y, window.innerHeight - menuHeight);
            return { left: clampedX, top: clampedY };
          })()}
        >
          <button
            type="button"
            role="menuitem"
            onClick={handleReconnectFromMenu}
            className="flex w-full items-center gap-2 px-3 py-1.5 text-left text-[13px] text-foreground transition-colors hover:bg-card/80"
            aria-label={t("auth.reconnectAccount", { accountEmail: accountContextMenu.accountEmail })}
          >
            {reconnectingAccountId === accountContextMenu.accountId ? (
              <Loader2 size={14} className="animate-spin text-muted-foreground" />
            ) : (
              <RefreshCw size={14} className="text-muted-foreground" />
            )}
            {t("auth.reconnect")}
          </button>
        </div>
      )}
    </aside>
  );
}
