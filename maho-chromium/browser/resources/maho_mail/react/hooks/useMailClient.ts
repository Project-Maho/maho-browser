import { useState, useEffect, useCallback, useRef, useMemo } from "react";
import * as api from "../api";
import { useOptimisticMutation } from "./useOptimisticMutation";
import { emitMailUpdated, emitMailRollback } from "./usePopoutWindow";
import type {
  AccountSummary,
  EmailDetail,
  EmailSummary,
  FolderType,
  MailboxSelection,
  AccountFolders,
} from "../types";

// Per-account offset tracking for aggregate mode pagination
export type AggregateOffsets = Record<string, number>;

// Result from a single account's email fetch in aggregate mode
export interface AggregateAccountResult {
  accountId: string;
  emails: EmailSummary[];
  folderId: string;
}

export interface MailAuthRecoveryError {
  emailId: string;
  message: string;
}

function isAuthenticationError(message: string): boolean {
  return /authenticationfailed|invalid credentials|auth error|invalid_grant|sign-in expired/i.test(message);
}

// Last landing view (aggregate Inbox), painted on the first render of a fresh
// Mail tab while the helper round-trips (accounts -> folders -> emails)
// refresh it. Without it every open showed the boot screen, then a skeleton.
export const MAIL_LIST_SNAPSHOT_KEY = "maho-mail-list-snapshot-v1";
const SNAPSHOT_EMAIL_LIMIT = 50;

interface MailListSnapshot {
  accounts: AccountSummary[];
  accountFolders: AccountFolders[];
  emails: EmailSummary[];
}

const LANDING_SELECTION: MailboxSelection = { type: "aggregate", folderType: "Inbox" };

function isLandingSelection(selection: MailboxSelection): boolean {
  return selection?.type === "aggregate" && selection.folderType === "Inbox";
}

function readMailListSnapshot(): MailListSnapshot | null {
  try {
    const raw = localStorage.getItem(MAIL_LIST_SNAPSHOT_KEY);
    if (!raw) return null;
    const parsed = JSON.parse(raw) as Partial<MailListSnapshot>;
    if (
      !Array.isArray(parsed.accounts) || parsed.accounts.length === 0 ||
      !Array.isArray(parsed.accountFolders) || parsed.accountFolders.length === 0 ||
      !Array.isArray(parsed.emails)
    ) {
      return null;
    }
    return parsed as MailListSnapshot;
  } catch {
    return null;
  }
}

function writeMailListSnapshot(snapshot: MailListSnapshot | null): void {
  try {
    if (snapshot) {
      localStorage.setItem(MAIL_LIST_SNAPSHOT_KEY, JSON.stringify(snapshot));
    } else {
      localStorage.removeItem(MAIL_LIST_SNAPSHOT_KEY);
    }
  } catch {
    // Storage full or unavailable: the next open cold-boots, nothing breaks.
  }
}



export function useMailClient(onError?: (message: string) => void) {
  const [snapshot] = useState(readMailListSnapshot);
  const [accounts, setAccounts] = useState<AccountSummary[]>(snapshot?.accounts ?? []);
  const [accountFolders, setAccountFolders] = useState<AccountFolders[]>(snapshot?.accountFolders ?? []);
  const [selection, setSelection] = useState<MailboxSelection>(snapshot ? LANDING_SELECTION : null);
  const [emails, setEmails] = useState<EmailSummary[]>(snapshot?.emails ?? []);
  const [selectedEmail, setSelectedEmail] = useState<EmailDetail | null>(null);
  const [authRecoveryError, setAuthRecoveryError] = useState<MailAuthRecoveryError | null>(null);
  const [emailLoading, setEmailLoading] = useState(false);
  const [loading, setLoading] = useState(false);
  const [isBootstrappingAccounts, setIsBootstrappingAccounts] = useState(snapshot === null);
  // A snapshot paints stale folders; reload them once the live accounts land.
  const revalidateFoldersRef = useRef(snapshot !== null);
  const accountsFetchedRef = useRef(false);
  const [error, setError] = useState<string | null>(null);
  const [hasMore, setHasMore] = useState(true);
  
  // Per-account offsets for aggregate mode pagination
  const [aggregateOffsets, setAggregateOffsets] = useState<AggregateOffsets>({});

  const onErrorRef = useRef(onError);
  onErrorRef.current = onError;

  const selectingRef = useRef<string | null>(null);
  const loadedSelectionRef = useRef<MailboxSelection>(null);
  const listGenerationRef = useRef(0);
  useEffect(() => {
    ++listGenerationRef.current;
    return () => { ++listGenerationRef.current; };
  }, [selection, accounts, accountFolders]);
  const emailsRef = useRef(emails);
  emailsRef.current = emails;
  const markReadTimerRef = useRef<any>(null);
  const deletedEmailsCacheRef = useRef<Map<string, { email: EmailSummary; index: number; selectedDetail: EmailDetail | null }>>(new Map());

  useEffect(() => {
    return () => {
      if (markReadTimerRef.current) {
        clearTimeout(markReadTimerRef.current);
      }
    };
  }, []);

  const reportError = useCallback(
    (fallbackMessage: string, err: unknown) => {
      const message = err instanceof Error ? err.message : fallbackMessage;
      setError(message);
      onErrorRef.current?.(message);
    },
    [],
  );

  // Derived state for backward compatibility
  const selectedAccountId = useMemo(() => {
    if (selection?.type === "child") {
      return selection.accountId;
    }
    return null;
  }, [selection]);

  const selectedFolderId = useMemo(() => {
    if (selection?.type === "child") {
      return selection.folderId;
    }
    return null;
  }, [selection]);

  const selectedFolderType = useMemo(() => {
    if (selection?.type === "aggregate") {
      return selection.folderType;
    }
    if (selection?.type === "child") {
      const af = accountFolders.find((af) => af.account.id === selection.accountId);
      const folder = af?.folders.find((f) => f.id === selection.folderId);
      return folder?.folder_type ?? null;
    }
    return null;
  }, [selection, accountFolders]);

  // Flat folders for backward compatibility with existing UI
  const folders = useMemo(() => {
    return accountFolders.flatMap((af) =>
      Array.isArray(af.folders) ? af.folders : [],
    );
  }, [accountFolders]);

  const loadAccounts = useCallback(async (options?: { bootstrap?: boolean }) => {
    const isBootstrap = options?.bootstrap ?? false;

    try {
      setLoading(true);
      if (isBootstrap && !revalidateFoldersRef.current) {
        setIsBootstrappingAccounts(true);
      }
      setError(null);
      const result = await api.listAccounts();
      accountsFetchedRef.current = true;
      setAccounts(result);
      if (result.length === 0) {
        revalidateFoldersRef.current = false;
        writeMailListSnapshot(null);
        setSelection(null);
        setAccountFolders([]);
        setEmails([]);
        setSelectedEmail(null);
        return;
      }
    } catch (err) {
      reportError("Failed to load accounts", err);
    } finally {
      setLoading(false);
      if (isBootstrap) {
        setIsBootstrappingAccounts(false);
      }
    }
  }, [reportError]);

  const loadAllFolders = useCallback(async () => {
    if (accounts.length === 0) return;
    try {
      setError(null);
      const results: AccountFolders[] = [];

      for (const account of accounts) {
        try {
          const listResult = await api.listFolders(account.id);
          const folders = Array.isArray(listResult) ? listResult : [];
          results.push({ account, folders });
        } catch (err) {
          reportError(`Failed to load folders for ${account.email}`, err);
          results.push({ account, folders: [] });
        }
      }

      setAccountFolders(results);

      // Auto-select aggregate Inbox if nothing selected
      setSelection((prev) => {
        if (prev) return prev;
        return { type: "aggregate", folderType: "Inbox" };
      });
    } catch (err) {
      reportError("Failed to load folders", err);
    }
  }, [accounts, reportError]);

  const syncAllFolders = useCallback(async () => {
    if (accounts.length === 0) return;

    try {
      setLoading(true);
      setError(null);

      const results: AccountFolders[] = [];

      for (const account of accounts) {
        try {
          await api.syncFolders(account.id);
          const listResult = await api.listFolders(account.id);
          const folders = Array.isArray(listResult) ? listResult : [];
          results.push({ account, folders });
        } catch (err) {
          reportError(`Failed to sync folders for ${account.email}`, err);

          try {
            const listResult = await api.listFolders(account.id);
            const folders = Array.isArray(listResult) ? listResult : [];
            results.push({ account, folders });
          } catch (listErr) {
            reportError(`Failed to load folders for ${account.email}`, listErr);
            results.push({ account, folders: [] });
          }
        }
      }

      setAccountFolders(results);

      setSelection((prev) => {
        if (prev) return prev;
        return { type: "aggregate", folderType: "Inbox" };
      });
    } catch (err) {
      reportError("Failed to sync folders", err);
    } finally {
      setLoading(false);
    }
  }, [accounts, reportError]);

  // Load emails for aggregate selection (parallel across accounts)
  // Uses per-account offsets to ensure correct pagination
  const loadAggregateEmails = useCallback(
    async (
      folderType: FolderType,
      limitPerAccount: number | AggregateOffsets,
      offsets: AggregateOffsets,
      isCurrent: () => boolean = () => true,
    ): Promise<{ results: AggregateAccountResult[]; hasMore: boolean }> => {
      const targets: { accountId: string; folderId: string }[] = [];

      for (const af of accountFolders) {
        const folder = af.folders.find((f) => f.folder_type === folderType);
        if (folder) {
          targets.push({ accountId: af.account.id, folderId: folder.id });
        }
      }

      if (targets.length === 0) {
        return { results: [], hasMore: false };
      }

      // Parallel fetch from all accounts using their individual offsets
      const results = await Promise.allSettled(
        targets.map((t) =>
          api
            .listEmails(
              t.accountId,
              t.folderId,
              typeof limitPerAccount === "number"
                ? limitPerAccount
                : Math.max(50, limitPerAccount[t.accountId] ?? 0),
              offsets[t.accountId] ?? 0,
            )
            .then((emails): AggregateAccountResult => ({
              accountId: t.accountId,
              folderId: t.folderId,
              emails,
            })),
        ),
      );

      const accountResults: AggregateAccountResult[] = [];
      let hasMoreResults = false;

      results.forEach((result) => {
        if (result.status === "fulfilled") {
          accountResults.push(result.value);
          const limit = typeof limitPerAccount === "number"
            ? limitPerAccount
            : Math.max(50, limitPerAccount[result.value.accountId] ?? 0);
          if (result.value.emails.length === limit) {
            hasMoreResults = true;
          }
        } else if (isCurrent()) {
          reportError(`Failed to load emails from aggregate`, result.reason);
        }
      });

      return { results: accountResults, hasMore: hasMoreResults };
    },
    [accountFolders, reportError],
  );

  // Reset aggregate offsets when selection changes or accountFolders change materially
  const selectionKey = selection?.type === "aggregate" ? selection.folderType : null;
  useEffect(() => {
    if (selection?.type === "aggregate") {
      // Reset offsets when entering aggregate mode or changing folder type
      setAggregateOffsets({});
    }
  }, [selectionKey]);

  // Main email loading effect based on selection
  useEffect(() => {
    if (!selection) return;
    // Snapshot folders may be stale and the helper may still be starting; the
    // list is refreshed once live folders replace them (loadAggregateEmails
    // changes identity and re-runs this effect).
    if (revalidateFoldersRef.current) return;

    let cancelled = false;

    const init = async () => {
      const selectionChanged = loadedSelectionRef.current !== selection;
      loadedSelectionRef.current = selection;
      try {
        if (selectionChanged) setLoading(true);
        setError(null);
        if (selectionChanged) setSelectedEmail(null);
        const limit = selectionChanged ? 50 : Math.max(50, emailsRef.current.length);

        if (selection.type === "aggregate") {
          const loadedPerAccount: AggregateOffsets = {};
          if (!selectionChanged) {
            for (const email of emailsRef.current) {
              loadedPerAccount[email.account_id] =
                (loadedPerAccount[email.account_id] ?? 0) + 1;
            }
          }
          // Aggregate mode: load from all accounts with matching folder type
          // Use empty offsets (all start at 0) for initial load
          const { results, hasMore: more } = await loadAggregateEmails(
            selection.folderType,
            loadedPerAccount,
            {},
          );
          if (cancelled) return;

          // Merge and sort all emails by date descending
          const allEmails = results.flatMap((r) => r.emails);
          allEmails.sort((a, b) => {
            const ta = new Date(b.date).getTime() || 0;
            const tb = new Date(a.date).getTime() || 0;
            return ta - tb;
          });

          // Initialize offsets based on actual emails returned per account
          const initialOffsets: AggregateOffsets = {};
          results.forEach((r) => {
            initialOffsets[r.accountId] = r.emails.length;
          });

          setEmails(allEmails);
          setAggregateOffsets(initialOffsets);
          setHasMore(more);
        } else {
          // Child mode: specific account/folder
          const cached = await api.listEmails(
            selection.accountId,
            selection.folderId,
            limit,
            0,
          );
          if (cancelled) return;
          setEmails(cached);
          setHasMore(cached.length === limit);
        }
      } catch (err) {
        if (!cancelled) reportError("Failed to load emails", err);
      } finally {
        if (!cancelled) setLoading(false);
      }
    };

    void init();

    return () => {
      cancelled = true;
    };
  }, [selection, loadAggregateEmails, reportError]);

  const loadMoreEmails = useCallback(async () => {
    if (!selection || loading) return;
    const generation = listGenerationRef.current;
    const isCurrent = () => generation === listGenerationRef.current;

    try {
      setLoading(true);
      setError(null);

      if (selection.type === "aggregate") {
        // Aggregate mode: use per-account offsets for correct pagination
        const { results, hasMore: more } = await loadAggregateEmails(
          selection.folderType,
          50,
          aggregateOffsets,
          isCurrent,
        );
        if (!isCurrent()) return;

        // Merge new emails with existing, deduplicate by id, and sort by date
        setEmails((prev) => {
          const existingIds = new Set(prev.map((e) => e.id));
          const newEmails = results.flatMap((r) =>
            r.emails.filter((e) => !existingIds.has(e.id)),
          );
          const merged = [...prev, ...newEmails];
          merged.sort((a, b) => {
            const ta = new Date(b.date).getTime() || 0;
            const tb = new Date(a.date).getTime() || 0;
            return ta - tb;
          });
          return merged;
        });

        // Advance offsets for each account based on actual emails returned
        setAggregateOffsets((prev) => {
          const next = { ...prev };
          results.forEach((r) => {
            next[r.accountId] = (next[r.accountId] ?? 0) + r.emails.length;
          });
          return next;
        });

        setHasMore(more);
      } else {
        // Child mode: use simple offset based on current email count (unchanged behavior)
        const result = await api.listEmails(
          selection.accountId,
          selection.folderId,
          50,
          emails.length,
        );
        if (!isCurrent()) return;
        setEmails((prev) =>
          [...prev, ...result].sort((a, b) => {
            const ta = new Date(b.date).getTime() || 0;
            const tb = new Date(a.date).getTime() || 0;
            return ta - tb;
          }),
        );
        setHasMore(result.length === 50);
      }
    } catch (err) {
      if (isCurrent()) reportError("Failed to load more emails", err);
    } finally {
      if (isCurrent()) setLoading(false);
    }
  }, [
    selection,
    emails.length,
    loading,
    loadAggregateEmails,
    aggregateOffsets,
    reportError,
  ]);

  const selectEmail = useCallback(
    async (emailId: string) => {
      if (markReadTimerRef.current) {
        clearTimeout(markReadTimerRef.current);
        markReadTimerRef.current = null;
      }

      selectingRef.current = emailId;
      try {
        setEmailLoading(true);
        setError(null);
        setAuthRecoveryError(null);
        const detail = await api.getEmail(emailId);
        
        if (selectingRef.current !== emailId) return;
        
        setSelectedEmail(detail);
        
        if (!detail.email.is_read) {
          markReadTimerRef.current = setTimeout(async () => {
            try {
              await api.markRead(emailId);
              if (selectingRef.current === emailId) {
                setEmails((prev) =>
                  prev.map((e) => (e.id === emailId ? { ...e, is_read: true } : e)),
                );
                setSelectedEmail((prev) => {
                  if (prev && prev.email.id === emailId) {
                    return {
                      ...prev,
                      email: { ...prev.email, is_read: true },
                    };
                  }
                  return prev;
                });
              }
            } catch (err) {
              console.error("Failed to mark email as read", err);
            }
          }, 2000);
        }
      } catch (err) {
        if (selectingRef.current === emailId) {
          const message = err instanceof Error ? err.message : "Failed to load email";
          if (isAuthenticationError(message)) {
            setError(null);
            setAuthRecoveryError({ emailId, message });
          } else {
            reportError("Failed to load email", err);
          }
        }
      } finally {
        if (selectingRef.current === emailId) {
          setEmailLoading(false);
        }
      }
    },
    [reportError],
  );

  const syncCurrentFolder = useCallback(async () => {
    if (!selection) return;
    const generation = listGenerationRef.current;
    const isCurrent = () => generation === listGenerationRef.current;

    try {
      setLoading(true);
      setError(null);

      if (selection.type === "aggregate") {
        // For aggregate, sync all matching folders and reload
        const targets: { accountId: string; folderId: string }[] = [];
        for (const af of accountFolders) {
          const folder = af.folders.find((f) => f.folder_type === selection.folderType);
          if (folder) {
            targets.push({ accountId: af.account.id, folderId: folder.id });
          }
        }

        const results = await Promise.allSettled(
          targets.map((t) => api.syncFolder(t.accountId, t.folderId)),
        );
        if (!isCurrent()) return;

        let allEmails: EmailSummary[] = [];
        results.forEach((result) => {
          if (result.status === "fulfilled") {
            allEmails = allEmails.concat(result.value);
          }
        });

        allEmails.sort((a, b) => {
          const ta = new Date(b.date).getTime() || 0;
          const tb = new Date(a.date).getTime() || 0;
          return ta - tb;
        });

        setEmails(allEmails);
        setAggregateOffsets({});
      } else {
        const result = await api.syncFolder(selection.accountId, selection.folderId);
        if (!isCurrent()) return;
        setEmails(result);
      }
    } catch (err) {
      if (isCurrent()) reportError("Failed to sync folder", err);
    } finally {
      if (isCurrent()) setLoading(false);
    }
  }, [selection, accountFolders, reportError]);

  const deleteEmailMutation = useOptimisticMutation<string, void>({
    mutate: async (emailId) => {
      await api.deleteEmail(emailId);
      void emitMailUpdated(emailId, "delete");
    },
    applyOptimistic: (emailId) => {
      const idx = emails.findIndex((e) => e.id === emailId);
      if (idx !== -1) {
        const email = emails[idx];
        const selectedDetail = selectedEmail?.email.id === emailId ? selectedEmail : null;
        deletedEmailsCacheRef.current.set(emailId, { email, index: idx, selectedDetail });
      }

      setEmails((prev) => prev.filter((e) => e.id !== emailId));
      if (selectedEmail?.email.id === emailId) {
        setSelectedEmail(null);
      }
    },
    rollback: (emailId) => {
      const cached = deletedEmailsCacheRef.current.get(emailId);
      if (cached) {
        setEmails((prev) => {
          const next = [...prev];
          next.splice(cached.index, 0, cached.email);
          return next;
        });
        if (cached.selectedDetail) {
          setSelectedEmail(cached.selectedDetail);
        }
        deletedEmailsCacheRef.current.delete(emailId);
      }
      void emitMailRollback(emailId, "delete");
    },
    onSuccess: (_data, emailId) => {
      deletedEmailsCacheRef.current.delete(emailId);
    },
  });

  const handleDeleteEmail = useCallback(
    async (emailId: string) => {
      setError(null);
      await deleteEmailMutation.execute(emailId);
    },
    [deleteEmailMutation],
  );

  const toggleStarMutation = useOptimisticMutation<string, void>({
    mutate: async (emailId) => {
      await api.toggleStar(emailId);
      void emitMailUpdated(emailId, "toggleStar");
    },
    applyOptimistic: (emailId) => {
      setEmails((prev) =>
        prev.map((e) =>
          e.id === emailId ? { ...e, is_starred: !e.is_starred } : e,
        ),
      );
      setSelectedEmail((prev) => {
        if (prev && prev.email.id === emailId) {
          return {
            ...prev,
            email: { ...prev.email, is_starred: !prev.email.is_starred },
          };
        }
        return prev;
      });
    },
    rollback: (emailId) => {
      setEmails((prev) =>
        prev.map((e) =>
          e.id === emailId ? { ...e, is_starred: !e.is_starred } : e,
        ),
      );
      setSelectedEmail((prev) => {
        if (prev && prev.email.id === emailId) {
          return {
            ...prev,
            email: { ...prev.email, is_starred: !prev.email.is_starred },
          };
        }
        return prev;
      });
      void emitMailRollback(emailId, "toggleStar");
    },
  });

  const handleToggleStar = useCallback(
    async (emailId: string) => {
      await toggleStarMutation.execute(emailId);
    },
    [toggleStarMutation],
  );

  const clearSelectedEmail = useCallback(() => {
    selectingRef.current = null;
    if (markReadTimerRef.current) clearTimeout(markReadTimerRef.current);
    setSelectedEmail(null);
    setAuthRecoveryError(null);
    setEmailLoading(false);
  }, []);

  const removeEmails = useCallback((ids: string[]) => {
    setEmails(prev => prev.filter(email => !ids.includes(email.id)));
    setSelectedEmail(prev => prev && ids.includes(prev.email.id) ? null : prev);
  }, []);

  // Selection change handlers
  const selectAggregate = useCallback((folderType: FolderType) => {
    setSelection({ type: "aggregate", folderType });
    setSelectedEmail(null);
  }, []);

  const selectChild = useCallback((accountId: string, folderId: string) => {
    setSelection({ type: "child", accountId, folderId });
    setSelectedEmail(null);
  }, []);

  // Backward compatibility: changeAccount clears to aggregate Inbox for that account's context
  const changeAccount = useCallback(
    (accountId: string) => {
      const af = accountFolders.find((a) => a.account.id === accountId);
      const inbox = af?.folders.find((f) => f.folder_type === "Inbox");
      if (inbox) {
        setSelection({ type: "child", accountId, folderId: inbox.id });
      } else if (af && af.folders.length > 0) {
        setSelection({ type: "child", accountId, folderId: af.folders[0].id });
      }
      setSelectedEmail(null);
    },
    [accountFolders],
  );

  // Backward compatibility: changeFolder tries to find in current context
  const changeFolder = useCallback(
    (folderId: string) => {
      // Find which account has this folder
      for (const af of accountFolders) {
        const folder = af.folders.find((f) => f.id === folderId);
        if (folder) {
          setSelection({ type: "child", accountId: af.account.id, folderId });
          setSelectedEmail(null);
          return;
        }
      }
    },
    [accountFolders],
  );

  // Initial load
  useEffect(() => {
    void loadAccounts({ bootstrap: true });
  }, [loadAccounts]);

  // The app shell only loads folders when none are shown, which a snapshot
  // defeats; refresh them here once the live account list has arrived.
  useEffect(() => {
    if (!revalidateFoldersRef.current || !accountsFetchedRef.current) return;
    revalidateFoldersRef.current = false;
    void loadAllFolders();
  }, [accounts, loadAllFolders]);

  // Persist the landing view for the next fresh open.
  useEffect(() => {
    if (!accountsFetchedRef.current || !isLandingSelection(selection)) return;
    if (accounts.length === 0 || accountFolders.length === 0) return;
    writeMailListSnapshot({
      accounts,
      accountFolders,
      emails: emails.slice(0, SNAPSHOT_EMAIL_LIMIT),
    });
  }, [accounts, accountFolders, selection, emails]);

  return useMemo(() => ({
    // Core state
    accounts,
    accountFolders,
    selection,
    emails,
    selectedEmail,
    authRecoveryError,
    emailLoading,
    loading,
    isBootstrappingAccounts,
    error,
    hasMore,

    // Backward compatible derived state
    selectedAccountId,
    selectedFolderId,
    selectedFolderType,
    folders,

    // Actions
    clearSelectedEmail,
    removeEmails,
    loadAccounts,
    loadAllFolders,
    syncAllFolders,
    loadMoreEmails,
    selectEmail,
    syncCurrentFolder,
    deleteEmail: handleDeleteEmail,
    toggleStar: handleToggleStar,

    // Selection actions
    selectAggregate,
    selectChild,
    changeAccount,
    changeFolder,
    clearError: () => setError(null),
    clearAuthRecoveryError: () => setAuthRecoveryError(null),
  }), [
    accounts,
    accountFolders,
    selection,
    emails,
    selectedEmail,
    authRecoveryError,
    emailLoading,
    loading,
    isBootstrappingAccounts,
    error,
    hasMore,
    selectedAccountId,
    selectedFolderId,
    selectedFolderType,
    folders,
    loadAccounts,
    clearSelectedEmail,
    removeEmails,
    loadAllFolders,
    syncAllFolders,
    loadMoreEmails,
    selectEmail,
    syncCurrentFolder,
    handleDeleteEmail,
    handleToggleStar,
    selectAggregate,
    selectChild,
    changeAccount,
    changeFolder,
  ]);
}
