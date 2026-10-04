import React from 'react';
import { createRoot } from 'react-dom/client';
import './i18n';
import { MailOnboardingContent } from '../../maho_common/react/mail/mail_onboarding_content.js';
import { MailOnboardingSidebar } from '../../maho_common/react/mail/mail_onboarding_sidebar.js';
import type {
  MailAccount,
  MailOnboardingApi,
  MailOnboardingStringLookup,
} from '../../maho_common/react/mail/mail_onboarding_api.js';
import {
  MAIL_APP_ONBOARDING_CONFIG,
  formatMailOnboardingString,
  parseMailAccounts,
} from '../../maho_common/react/mail/mail_onboarding_api.js';
import { handler, callbackRouter } from './mojo_client.js';
import type { Theme } from "./hooks/useTheme";
import type { UseSettingsReturn } from "./hooks/useSettings";

type Density = "comfortable" | "compact";

interface BrowserUiPrefs {
  readonly theme: Theme;
  readonly density: Density;
}

const DEFAULT_BROWSER_UI_PREFS: BrowserUiPrefs = {
  theme: "system",
  density: "comfortable",
};

function parseBrowserUiPrefs(prefsJson: string): BrowserUiPrefs {
  let parsed: unknown;
  try {
    parsed = JSON.parse(prefsJson);
  } catch (error) {
    if (error instanceof SyntaxError) return DEFAULT_BROWSER_UI_PREFS;
    throw error;
  }
  if (typeof parsed !== "object" || parsed === null) return DEFAULT_BROWSER_UI_PREFS;

  const theme = "theme" in parsed && (parsed.theme === "light" || parsed.theme === "dark")
    ? parsed.theme
    : "system";
  const density = "density" in parsed && parsed.density === "compact"
    ? "compact"
    : "comfortable";
  return { theme, density };
}

import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import { useTranslation } from "react-i18next";
import "./theme/app.css";
import { useMailClient } from "./hooks/useMailClient";
import { useAuthStatus } from "./hooks/useAuthStatus";
import { useReauthRetryQueue } from "./hooks/useReauthRetryQueue";
import * as api from "./api";
import { Sidebar } from "./components/layout/Sidebar";
import { MobileDrawer } from "./components/mobile/MobileDrawer";
import { EmailList } from "./components/layout/EmailList";
import { EmailContent } from "./components/layout/EmailContent";
import { AuthExpiredBanner } from "./components/common/AuthExpiredBanner";
import { ComposeModal } from "./components/compose/ComposeModal";

import { AddAccountModal } from "./components/account/AddAccountModal";
import type { ReplyContext, EmailSummary, ComposeEmailRequest, SmartCategory, CalendarEvent, ComposeAttachment, ComposeStateSnapshot, SendRequestedOptions } from "./types";
import { useToast, Toaster } from "./components/ui/Toast";
import { useConfirm, ConfirmProvider } from "./components/ui/ConfirmDialog";
import { TooltipProvider } from "./components/ui/Tooltip";
import { ErrorBoundary } from "./components/ui/ErrorBoundary";
import { useAutoSync } from "./hooks/useAutoSync";
import { useHotkeys } from "./hooks/useHotkeys";
import { useToolbarConfig } from "./hooks/useToolbarConfig";
import type { ActionId } from "./types/toolbar";
import { ShortcutCheatsheet } from "./components/ui/ShortcutCheatsheet";
import { useTheme } from "./hooks/useTheme";
import { useSettings } from "./hooks/useSettings";
import { useTrayBadge } from "./hooks/useTrayBadge";
import { useNetworkStatus } from "./hooks/useNetworkStatus";
import {
  emitMailUpdated,
  useMailUpdatedListener,
  isPopout,
  getPopoutType,
  getPopoutEmailId,
} from "./hooks/usePopoutWindow";
import { ReaderWindow } from "./components/reader/ReaderWindow";
import { ComposeWindow } from "./components/compose/ComposeWindow";
import { useAppLifecycle } from "./hooks/useAppLifecycle";
import { useNotificationSound } from "./hooks/useNotificationSound";
import { useNotifications } from "./hooks/useNotifications";
import { OnboardingScreen } from "./components/account/OnboardingScreen";
import { SnoozePicker } from "./components/common/SnoozePicker";
import { UndoSendToast } from "./components/common/UndoSendToast";
import { OnboardingTour } from "./components/common/OnboardingTour";

import { ArrowLeft, Menu } from "lucide-react";
import { LiveRegion } from "./components/common/LiveRegion";
import { CalendarEventDetail } from "./components/calendar/CalendarEventDetail";
import { RecurringScopeDialog } from "./components/calendar/RecurringScopeDialog";
import { CreateEventDialog } from "./components/calendar/CreateEventDialog";
import { CalendarGrid } from "./components/calendar/CalendarGrid";
import { CommandPalette } from "./components/ui/CommandPalette";
import type { PaletteCommand } from "./hooks/useCommandRegistry";
import { cn } from "./lib/utils";
import { useIsMobile, isMobile } from "./utils/platform";
import { parseAddressList } from "./utils/addresses";
import {
  acceptMailLifecycle,
  lifecycleClearsSensitiveState,
  type MailLifecycleSnapshot,
} from "./mail_lifecycle";
import {parseMailRoute} from "./mail_route";

const COLLAPSED_SIDEBAR_WIDTH = 72;

function setReactInputValue(input: HTMLInputElement, value: string): void {
  const setter = Object.getOwnPropertyDescriptor(
    window.HTMLInputElement.prototype,
    "value",
  )?.set;
  if (setter) {
    setter.call(input, value);
  } else {
    input.value = value;
  }
}

function BootingState() {
  const { t } = useTranslation();

  return (
    <main className="relative flex h-screen items-center justify-center bg-background text-foreground">
      <div className="absolute inset-x-0 top-0 h-8" data-tauri-drag-region />
      <div className="text-sm text-muted-foreground">{t("app.loadingAccounts")}</div>
    </main>
  );
}

function MailClientShell({ settings, density }: { readonly settings: UseSettingsReturn; readonly density: Density }) {
  const { toast } = useToast();
  const confirm = useConfirm();
  const { t } = useTranslation();
  const mail = useMailClient((message) => toast("error", message));
  const { authErrors, clearAuthError } = useAuthStatus();
  const { enqueueRetry } = useReauthRetryQueue();

  const totalUnread = useMemo(() => mail.folders.reduce((sum, f) => sum + f.unread_count, 0), [mail.folders]);
  const accountIds = useMemo(() => new Set(mail.accounts.map((account) => account.id)), [mail.accounts]);
  useTrayBadge(totalUnread);
  const { playSound } = useNotificationSound(settings.soundEnabled);
  const { notify } = useNotifications({
    enabled: settings.desktopNotifications,
  });
  useAppLifecycle({
    onForeground: () => { void mail.syncAllFolders(); },
  });
  const [showCompose, setShowCompose] = useState(false);
  const [showAddAccount, setShowAddAccount] = useState(false);
  const [composeReplyTo, setComposeReplyTo] = useState<ReplyContext | undefined>();
  const [composeForward, setComposeForward] = useState<ReplyContext | undefined>();
  const [composeForwardUid, setComposeForwardUid] = useState<number | undefined>();
  const [composeForwardFolderId, setComposeForwardFolderId] = useState<string | undefined>();
  const [composeDraftBody, setComposeDraftBody] = useState<string | undefined>();
  const [composeDraftBodyHtml, setComposeDraftBodyHtml] = useState<string | undefined>();
  const [composeBodyIsComplete, setComposeBodyIsComplete] = useState(false);
  const [composeAttachments, setComposeAttachments] = useState<ComposeAttachment[]>([]);
  const [composeAccountId, setComposeAccountId] = useState<string | undefined>();
  const [composeInitialTo, setComposeInitialTo] = useState<string | undefined>();
  const [composeInitialDraftId, setComposeInitialDraftId] = useState<string | null>(null);
  const [composeInitialSubject, setComposeInitialSubject] = useState<string | undefined>();
  const [composeInitialCc, setComposeInitialCc] = useState<string | undefined>();
  const [composeInitialBcc, setComposeInitialBcc] = useState<string | undefined>();
  const [composeReadReceipt, setComposeReadReceipt] = useState<boolean>(false);
  const [composeSecurity, setComposeSecurity] = useState<Pick<ComposeStateSnapshot, 'pgpEncrypt' | 'smimeSign' | 'smimeEncrypt'>>({});
  const [composeInReplyTo, setComposeInReplyTo] = useState<string | undefined>();
  const [composeReferences, setComposeReferences] = useState<string | undefined>();
  const [showCheatsheet, setShowCheatsheet] = useState(false);
  const [showCommandPalette, setShowCommandPalette] = useState(false);
  const [syncing, setSyncing] = useState(false);
  const [drawerOpen, setDrawerOpen] = useState(false);
  const [searchRequest, setSearchRequest] = useState<{query: string} | null>(null);
  const [syntheticView, setSyntheticView] = useState<string | null>(null);
  const [syntheticEmails, setSyntheticEmails] = useState<EmailSummary[]>([]);
  const [showSnoozePicker, setShowSnoozePicker] = useState(false);
  const [snoozeTargetId, setSnoozeTargetId] = useState<string | null>(null);
  const pendingSendsRef = useRef<Map<string, {
    id: string;
    request: ComposeEmailRequest;
    draftId?: string | null;
    composition?: ComposeStateSnapshot;
  }>>(new Map());
  const [pendingSends, setPendingSends] = useState<{
    id: string;
    request: ComposeEmailRequest;
    draftId?: string | null;
    composition?: ComposeStateSnapshot;
  }[]>([]);
  const [recoveredSends, setRecoveredSends] = useState<(typeof pendingSends)[number][]>([]);
  useEffect(() => {
    const guard = (event: BeforeUnloadEvent) => {
      if (pendingSendsRef.current.size > 0) {
        event.preventDefault();
        event.returnValue = '';
      }
    };
    window.addEventListener('beforeunload', guard);
    return () => window.removeEventListener('beforeunload', guard);
  }, []);
  const [smartCategory, setSmartCategory] = useState<SmartCategory>("personal");
  const { isOnline, pendingMutationCount, connectionStatus } = useNetworkStatus();
  const [selectedEvent, setSelectedEvent] = useState<CalendarEvent | null>(null);
  const [recurringPrompt, setRecurringPrompt] = useState<{ event: CalendarEvent; mode: "edit" | "delete" } | null>(null);
  const [showCreateEvent, setShowCreateEvent] = useState(false);
  const [editEvent, setEditEvent] = useState<CalendarEvent | null>(null);
  const [initialEventSummary, setInitialEventSummary] = useState("");

  useEffect(() => {
    const params = new URLSearchParams(window.location.search);
    const accountId = params.get("accountId");
    const emailId = params.get("emailId");
    if (!accountId || !emailId || mail.accountFolders.length === 0) return;
    mail.changeAccount(accountId);
    void mail.selectEmail(emailId);
  }, [mail.accountFolders.length, mail.changeAccount, mail.selectEmail]);

  const handleComposeToRecipient = useCallback((email: string) => {
    setComposeReplyTo(undefined);
    setComposeForward(undefined);
    setComposeDraftBody(undefined);
    setComposeInitialTo(email);
    setComposeInitialDraftId(null);
    setComposeInitialSubject(undefined);
    setComposeInitialCc(undefined);
    setComposeInitialBcc(undefined);
    setShowCompose(true);
  }, []);

  // Cross-window sync: refresh data when popout windows make changes
  useMailUpdatedListener(() => {
    void mail.syncCurrentFolder();
  });

  useEffect(() => {
    if (mail.accounts.length > 0 && mail.accountFolders.length === 0) {
      void mail.loadAllFolders();
    }
  }, [mail.accounts, mail.accountFolders.length, mail.loadAllFolders]);

  useEffect(() => {
    for (const authError of authErrors) {
      if (!accountIds.has(authError.account_id)) {
        clearAuthError(authError.account_id);
      }
    }
  }, [accountIds, authErrors, clearAuthError]);

  useAutoSync({
    onNewEmails: async (accountId: string, messageIds: string[]) => {
      let shouldNotify = true;
      try {
        if (messageIds.length > 0) {
          const unmuted = await api.filterMutedMessageIds(accountId, messageIds);
          shouldNotify = unmuted.length > 0;
        }
      } catch {
        // Fail open — still notify
      }
      if (shouldNotify) {
        playSound();
      }
      void mail.loadAllFolders();
    },
    onSyncStarted: () => setSyncing(true),
    onSyncCompleted: () => setSyncing(false),
    onSyncError: (event) => {
      setSyncing(false);
      if (event.error_type === "auth") {
        console.info("[AutoSync] auth sync error handled by reconnect banner:", event.account_id, event.error);
        return;
      }

      if (event.error_type === "network") {
        toast("error", `${t("notifications.connectionError")}: ${event.error}`);
        return;
      }

      toast("error", `${t("notifications.syncError")}: ${event.error}`);
    },
    onConnectionError: (accountId, error, errorType) => {
      const account = mail.accounts.find((item) => item.id === accountId);
      const accountLabel = account?.email ?? account?.display_name ?? accountId;

      if (errorType === "auth") {
        console.info("[AutoSync] auth connection error handled by reconnect banner:", `${accountLabel} — Sign-in expired, please reconnect.`, error);
        return;
      }

      console.error("[AutoSync] connection error:", accountId, error, errorType);
      toast("error", `${t("notifications.connectionError")}: ${error}`);
    },
    onSnoozeTriggered: (emailIds) => {
      notify(t("app.snoozedEmail"), t("app.snoozedReturned", { count: emailIds.length }));
      void mail.loadAllFolders();
    },
    onReminderTriggered: (items) => {
      for (const [, subject] of items) {
        notify(t("app.reminder"), subject || t("app.defaultReminder"));
      }
    },
    onSendLaterSent: () => {
      toast("success", t("app.scheduledSent"));
    },
    onSendLaterFailed: (data) => {
      toast("error", t("app.scheduledFailed", { error: data.error }));
    },
  });

  useEffect(() => {
    if (settings.syncInterval === 0) {
      void api.stopAutoSync();
      return;
    }

    const accountIdForSync = mail.selectedAccountId ?? null;
    void api.startAutoSync(accountIdForSync, settings.syncInterval);

    return () => {
      void api.stopAutoSync();
    };
  }, [mail.selectedAccountId, settings.syncInterval]);

  useEffect(() => {
    void api.startScheduler();
    return () => { void api.stopScheduler(); };
  }, []);

  const selectedEmailIndex = useMemo(
    () => mail.selectedEmail
      ? mail.emails.findIndex((email) => email.id === mail.selectedEmail?.email.id)
      : -1,
    [mail.selectedEmail, mail.emails]
  );
  const selectedEmail = useMemo(
    () => selectedEmailIndex >= 0 ? mail.emails[selectedEmailIndex] : null,
    [selectedEmailIndex, mail.emails]
  );
  const archiveFolder = useMemo(
    () => mail.folders.find((folder) => folder.folder_type === "Archive" &&
      folder.account_id === (selectedEmail?.account_id ?? mail.selectedAccountId)),
    [mail.folders, selectedEmail, mail.selectedAccountId]
  );
  const composeOpen = showCompose;
  const modalsOpen = useMemo(
    () => composeOpen || showAddAccount || showCheatsheet || showCommandPalette,
    [composeOpen, showAddAccount, showCheatsheet, showCommandPalette]
  );
  const desktopSidebarWidth = useMemo(
    () => settings.sidebarCollapsed ? COLLAPSED_SIDEBAR_WIDTH : settings.sidebarWidth,
    [settings.sidebarCollapsed, settings.sidebarWidth]
  );

  const openSearch = () => {
    const search = document.querySelector<HTMLInputElement>("[data-mail-search-input]");
    search?.focus();
    search?.select();
  };

  const closeCompose = () => {
    setShowCompose(false);
    setComposeReplyTo(undefined);
    setComposeForward(undefined);
    setComposeForwardUid(undefined);
    setComposeForwardFolderId(undefined);
    setComposeAccountId(undefined);
    setComposeDraftBody(undefined);
    setComposeDraftBodyHtml(undefined);
    setComposeAttachments([]);
    setComposeInitialTo(undefined);
    setComposeInitialDraftId(null);
    setComposeInitialSubject(undefined);
    setComposeInitialCc(undefined);
    setComposeInitialBcc(undefined);
    setComposeReadReceipt(false);
    setComposeSecurity({});
    setComposeBodyIsComplete(false);
    setComposeInReplyTo(undefined);
    setComposeReferences(undefined);
  };

  const openComposeScreen = () => {
    setShowCompose(true);
  };

  const openSettingsPanel = (aiTab?: "agent" | "translation") => {
    const pane = aiTab === "agent" || aiTab === "translation"
      ? "mail-ai"
      : "mail-accounts";
    window.location.href = `chrome://maho-settings?pane=${pane}`;
  };

  const reconnectSelectedAccount = () => {
    const failedEmail = mail.emails.find(
      (email) => email.id === mail.authRecoveryError?.emailId,
    );
    const accountId = failedEmail?.account_id ?? mail.selectedAccountId;
    if (!accountId) {
      setShowAddAccount(true);
      return;
    }
    void api.reconnectAccount(accountId).catch(() => {
      setShowAddAccount(true);
    });
  };

  const openDraftForEditing = async (emailId: string) => {
    try {
      const detail = await api.getEmail(emailId);
      const { email, attachments } = detail;
      let draftFiles: ComposeAttachment[] = [];
      if (attachments && attachments.length > 0) {
        draftFiles = await api.getForwardedAttachments(email.account_id, email.uid, email.folder_id);
      }
      setComposeReplyTo(undefined);
      setComposeForward(undefined);
      setComposeForwardUid(undefined);
      setComposeForwardFolderId(undefined);
      setComposeDraftBody(email.body_text ?? undefined);
      setComposeDraftBodyHtml(email.body_html ?? undefined);
      setComposeBodyIsComplete(true);
      setComposeAttachments(draftFiles);

      const toList = parseAddressList(email.to_addresses);
      const ccList = parseAddressList(email.cc_addresses);
      const bccList = parseAddressList(email.bcc_addresses);
      setComposeInitialTo(toList.join(", "));
      setComposeInitialCc(ccList.join(", "));
      setComposeInitialBcc(bccList.join(", "));
      setComposeInitialSubject(email.subject);
      setComposeInitialDraftId(email.id);
      setComposeAccountId(email.account_id);
      setComposeReadReceipt(Boolean(email.mdn_requested));
      setComposeInReplyTo(email.in_reply_to ?? undefined);
      if (email.account_id) {
        mail.changeAccount(email.account_id);
      }
      openComposeScreen();
    } catch (err) {
      toast("error", err instanceof Error ? err.message : t("app.failedToLoadDraft"));
    }
  };

  const handleSelectEmail = (id: string) => {
    const email = mail.emails.find(e => e.id === id);
    if (email?.is_draft) {
      void openDraftForEditing(id);
      return;
    }

    void mail.selectEmail(id);
  };

  const openEmailAt = (index: number) => {
    const target = mail.emails[index];
    if (!target) return;
    handleSelectEmail(target.id);
  };

  const moveSelection = (delta: number) => {
    if (mail.emails.length === 0) return;
    const baseIndex = selectedEmailIndex >= 0 ? selectedEmailIndex : delta > 0 ? -1 : 0;
    const nextIndex = Math.min(Math.max(baseIndex + delta, 0), mail.emails.length - 1);
    openEmailAt(nextIndex);
  };

  const handleArchiveShortcut = async () => {
    if (!selectedEmail || !archiveFolder) return;
    try {
      await api.moveEmail(selectedEmail.id, archiveFolder.id);
      void emitMailUpdated(selectedEmail.id, "move");
      mail.removeEmails([selectedEmail.id]);
    } catch (err) {
      toast("error", err instanceof Error ? err.message : t("app.failedToArchive"));
    }
  };

  const handleToggleReadShortcut = async () => {
    if (!selectedEmail) return;
    try {
      if (selectedEmail.is_read) {
        await api.markUnread(selectedEmail.id);
      } else {
        await api.markRead(selectedEmail.id);
      }
      void emitMailUpdated(selectedEmail.id, selectedEmail.is_read ? "markUnread" : "markRead");
      await mail.syncCurrentFolder();
      await mail.selectEmail(selectedEmail.id);
    } catch (err) {
      toast("error", err instanceof Error ? err.message : t("app.failedToUpdateReadState"));
    }
  };

  const handleDeleteEmail = async (emailId: string) => {
    const ok = await confirm({
      title: t("app.deleteEmailTitle"),
      message: t("app.deleteEmailConfirm"),
      confirmLabel: t("common.delete"),
      danger: true,
    });

    if (!ok) return;

    await mail.deleteEmail(emailId);
  };

  const handleDeleteAccount = async (accountId: string) => {
    const account = mail.accounts.find((a) => a.id === accountId);
    const email = account?.email ?? t("account.thisAccount");

    const ok = await confirm({
      title: t("account.deleteAccount"),
      message: t("account.deleteConfirm", { email }),
      confirmLabel: t("common.delete"),
      danger: true,
    });

    if (!ok) return;

    try {
      await api.cleanupSmimeForAccount(accountId);
      await api.deleteAccount(accountId);
      toast("success", t("account.deleteSuccess", "Account deleted successfully"));
    } catch (err) {
      toast("error", err instanceof Error ? err.message : t("account.deleteFailed"));
    }
  };

  const openCompose = () => {
    setComposeReplyTo(undefined);
    setComposeForward(undefined);
    setComposeForwardUid(undefined);
    setComposeForwardFolderId(undefined);
    setComposeDraftBody(undefined);
    setComposeDraftBodyHtml(undefined);
    setComposeAttachments([]);
    setComposeInitialDraftId(null);
    setComposeInitialSubject(undefined);
    setComposeInitialCc(undefined);
    setComposeInitialBcc(undefined);
    setComposeReadReceipt(false);
    setComposeInReplyTo(undefined);
    setComposeReferences(undefined);
    openComposeScreen();
  };

  const handleManualSync = () => {
    void mail.syncAllFolders();
  };

  const handleSelectSynthetic = async (id: string) => {
    setSyntheticView(id);
    try {
      if (id === "pins") {
        const emails = await api.listPinnedEmails(mail.selectedAccountId ?? undefined);
        setSyntheticEmails(emails);
      } else if (id === "snoozed") {
        const emails = await api.listSnoozedEmails(mail.selectedAccountId ?? undefined);
        setSyntheticEmails(emails);
      } else if (id === "unread") {
        const result = await api.searchEmails({
          query: "",
          is_unread: true,
          account_id: mail.selectedAccountId ?? undefined,
          limit: 100,
        });
        setSyntheticEmails(result.emails);
      } else if (id === "reminders") {
        const emails = await api.listReminders(mail.selectedAccountId ?? undefined);
        setSyntheticEmails(emails);
      } else if (id === "smart_inbox") {
        setSmartCategory("personal");
        const emails = await api.listBySmartCategory("personal", mail.selectedAccountId ?? undefined);
        setSyntheticEmails(emails);
      } else if (id === "starred") {
        const result = await api.searchEmails({
          query: "",
          is_starred: true,
          account_id: mail.selectedAccountId ?? undefined,
          limit: 100,
        });
        setSyntheticEmails(result.emails);
      } else if (id === "vip") {
        const accountId = mail.selectedAccountId;
        if (accountId) {
          const vipContacts = await api.listVipContacts(accountId);
          const vipEmails = mail.emails.filter((e) =>
            vipContacts.some((c) => c.email === e.from_address),
          );
          setSyntheticEmails(vipEmails);
        } else {
          setSyntheticEmails([]);
        }
      } else if (id === "calendar") {
        setSyntheticEmails([]);
        setSelectedEvent(null);
      }
    } catch (err) {
      toast("error", err instanceof Error ? err.message : t("app.failedToLoadEmails"));
    }
  };

  useEffect(() => {
    const route = parseMailRoute(window.location.search);
    if (route.kind === 'none') return;
    if (route.kind === 'compose') {
      openCompose();
    } else if (route.kind === 'search') {
      openSearch();
    } else if (route.kind === 'folder') {
      setSyntheticView(null);
      mail.selectAggregate(route.folder);
    } else {
      void handleSelectSynthetic(route.view);
    }
    window.history.replaceState(null, '', window.location.pathname);
  }, [mail.folders.length]);

  const handlePinEmail = async (emailId: string, nextPinned: boolean) => {
    try {
      if (nextPinned) {
        await api.pinEmail(emailId);
        toast("success", t("app.emailPinned"));
      } else {
        await api.unpinEmail(emailId);
        toast("success", t("app.emailUnpinned"));
      }
    } catch (err) {
      toast("error", err instanceof Error ? err.message : t(nextPinned ? "app.failedToPin" : "app.failedToUnpin"));
    }
  };

  const handleOpenSnooze = (emailId: string) => {
    setSnoozeTargetId(emailId);
    setShowSnoozePicker(true);
  };

  const handleSnooze = async (until: string) => {
    if (!snoozeTargetId) return;
    try {
      await api.snoozeEmail({ email_id: snoozeTargetId, snooze_until: until });
      toast("success", t("app.emailSnoozed"));
      setShowSnoozePicker(false);
      setSnoozeTargetId(null);
    } catch (err) {
      toast("error", err instanceof Error ? err.message : t("app.failedToSnooze"));
    }
  };

  const handleSetReminder = async (emailId: string) => {
    const tomorrow = new Date();
    tomorrow.setDate(tomorrow.getDate() + 1);
    tomorrow.setHours(9, 0, 0, 0);
    try {
      await api.setReminder({ email_id: emailId, reminder_at: tomorrow.toISOString() });
      toast("success", t("app.reminderSet"));
    } catch (err) {
      toast("error", err instanceof Error ? err.message : t("app.failedToSetReminder"));
    }
  };

  const restoreCompositionFromPending = (item: {
    request: ComposeEmailRequest;
    draftId?: string | null;
    composition?: ComposeStateSnapshot;
  }) => {
    setComposeBodyIsComplete(true);
    if (item.composition) {
      setComposeReplyTo(item.composition.replyTo);
      setComposeForward(item.composition.forwardFrom);
      setComposeForwardUid(item.composition.forwardEmailUid);
      setComposeForwardFolderId(item.composition.forwardFolderId);
      setComposeDraftBody(item.composition.body);
      setComposeDraftBodyHtml(item.composition.bodyHtml);
      setComposeAttachments(item.composition.attachments ?? []);
      setComposeInitialTo(item.composition.to);
      setComposeInitialDraftId(item.composition.draftId ?? null);
      setComposeInitialSubject(item.composition.subject);
      setComposeInitialCc(item.composition.cc);
      setComposeInitialBcc(item.composition.bcc);
      setComposeReadReceipt(item.composition.readReceipt ?? false);
      setComposeSecurity(item.composition);
      setComposeInReplyTo(item.composition.inReplyTo);
      setComposeReferences(item.composition.references);
      setComposeAccountId(item.composition.accountId);
      if (item.composition.accountId) {
        mail.changeAccount(item.composition.accountId);
      }
    } else {
      setComposeInitialTo(item.request.to?.join(", "));
      setComposeInitialCc(item.request.cc?.join(", "));
      setComposeInitialBcc(item.request.bcc?.join(", "));
      setComposeInitialSubject(item.request.subject);
      setComposeDraftBody(item.request.body_text);
      setComposeDraftBodyHtml(item.request.body_html);
      setComposeAttachments(item.request.attachments ?? []);
      setComposeInitialDraftId(item.draftId ?? null);
      setComposeReadReceipt(item.request.read_receipt ?? false);
      setComposeInReplyTo(item.request.in_reply_to);
      setComposeReferences(item.request.references);
      setComposeAccountId(item.request.account_id);
      if (item.request.account_id) {
        mail.changeAccount(item.request.account_id);
      }
    }
    openComposeScreen();
  };

  useEffect(() => {
    if (showCompose || recoveredSends.length === 0) return;
    const [item, ...remaining] = recoveredSends;
    restoreCompositionFromPending(item);
    pendingSendsRef.current.delete(item.id);
    setRecoveredSends(remaining);
  }, [showCompose, recoveredSends]);

  const handleSendRequested = async (request: ComposeEmailRequest, options?: SendRequestedOptions) => {
    const composition = options?.composition;
    const draftRequest = composition ? {
      ...request,
      body_text: composition.body,
      body_html: composition.bodyHtml,
      attachments: composition.attachments,
    } : request;
    let draftId = options?.draftId;
    if (draftId) await api.updateDraft(draftId, draftRequest);
    else draftId = await api.saveDraft(draftRequest);
    const entry = {
      id: `pending-${Date.now()}-${Math.random().toString(36).slice(2, 9)}`,
      request,
      draftId,
      composition: composition ? { ...composition, draftId } : undefined,
    };
    pendingSendsRef.current.set(entry.id, entry);
    setPendingSends((prev) => [...prev, entry]);
  };

  const handleUndoSend = (entryId: string) => {
    const item = pendingSendsRef.current.get(entryId) ?? pendingSends.find((p) => p.id === entryId);
    setPendingSends((prev) => prev.filter((p) => p.id !== entryId));
    toast("success", t("app.sendCancelled"));
    if (item) {
      setRecoveredSends((prev) => [...prev, item]);
    }
  };

  const handleUndoSendComplete = async (entryId: string) => {
    const item = pendingSendsRef.current.get(entryId) ?? pendingSends.find((p) => p.id === entryId);
    if (!item) return;
    setPendingSends((prev) => prev.filter((p) => p.id !== entryId));
    try {
      const receipt = await api.sendEmail(item.request);
      pendingSendsRef.current.delete(entryId);
      switch (receipt.status) {
        case "sent":
          toast("success", t("app.emailSent"));
          break;
        case "queued":
          toast("info", t("compose.queuedOffline"));
          break;
        case "uncertain":
          toast("warning", "Delivery could not be confirmed. Verify with the recipient before sending again.");
          break;
      }
      if (item.draftId) {
        void api.deleteEmail(item.draftId).catch(() => {});
      }
    } catch (err) {
      setRecoveredSends((prev) => [...prev, item]);
      toast("error", err instanceof Error ? err.message : t("compose.failedToSend"));
    }
  };

  const handleMoveEmail = async (emailId: string, targetFolderId: string) => {
    try {
      await api.moveEmail(emailId, targetFolderId);
      toast("success", t("app.emailMoved"));
      void emitMailUpdated(emailId, "move");
      void mail.syncCurrentFolder();
    } catch (err) {
      toast("error", err instanceof Error ? err.message : t("app.failedToMoveEmail"));
    }
  };

  const handleToggleStar = async (emailId: string) => {
    await mail.toggleStar(emailId);
  };

  const handleCreateFolder = async (accountId: string, folderName: string) => {
    try {
      await api.createFolder(accountId, folderName);
      toast("success", t("app.folderCreated"));
      void mail.loadAllFolders();
    } catch (err) {
      toast("error", err instanceof Error ? err.message : t("app.failedToCreateFolder"));
    }
  };

  const handleRenameFolder = async (accountId: string, folderId: string, newName: string) => {
    try {
      await api.renameFolder(accountId, folderId, newName);
      toast("success", t("app.folderRenamed"));
      void mail.loadAllFolders();
    } catch (err) {
      toast("error", err instanceof Error ? err.message : t("app.failedToRenameFolder"));
    }
  };

  const handleDeleteFolder = async (accountId: string, folderId: string) => {
    try {
      await api.deleteFolder(accountId, folderId);
      toast("success", t("app.folderDeleted"));
      void mail.loadAllFolders();
    } catch (err) {
      toast("error", err instanceof Error ? err.message : t("app.failedToDeleteFolder"));
    }
  };

  const handleSmartCategoryChange = async (category: SmartCategory) => {
    setSmartCategory(category);
    try {
      const emails = await api.listBySmartCategory(category, mail.selectedAccountId ?? undefined);
      setSyntheticEmails(emails);
    } catch (err) {
      toast("error", err instanceof Error ? err.message : t("app.failedToLoadCategory"));
    }
  };


  const { config: toolbarConfig } = useToolbarConfig();

  const toolbarHotkeys = useMemo(() => {
    const handlers: Partial<Record<ActionId, () => void>> = {
      "reply": () => { if (mail.selectedEmail) void handleReply(mail.selectedEmail.email.id); },
      "forward": () => { if (mail.selectedEmail) void handleForward(mail.selectedEmail.email.id); },
      "star": () => { if (mail.selectedEmail) void handleToggleStar(mail.selectedEmail.email.id); },
      "delete": () => { if (mail.selectedEmail) void handleDeleteEmail(mail.selectedEmail.email.id); },
      "pin": () => { if (mail.selectedEmail) void handlePinEmail(mail.selectedEmail.email.id, true); },
      "snooze": () => { if (mail.selectedEmail) handleOpenSnooze(mail.selectedEmail.email.id); },
      "remind": () => { if (mail.selectedEmail) void handleSetReminder(mail.selectedEmail.email.id); },
    };

    type Combo = { key: string; ctrl?: boolean; shift?: boolean; meta?: boolean; alt?: boolean };
    function parseShortcut(s: string | null): Combo | null {
      if (!s) return null;
      const parts = s.split("+");
      const combo: Combo = { key: "" };
      for (const part of parts) {
        if (part === "Ctrl") combo.ctrl = true;
        else if (part === "Shift") combo.shift = true;
        else if (part === "Meta") combo.meta = true;
        else if (part === "Alt") combo.alt = true;
        else combo.key = part;
      }
      return combo.key ? combo : null;
    }

    const seen = new Set<string>();
    const entries: Array<{ key: string; ctrl?: boolean; shift?: boolean; meta?: boolean; alt?: boolean; handler: () => void; disabled: boolean }> = [];
    for (const id of [...toolbarConfig.primary, ...toolbarConfig.overflow] as ActionId[]) {
      const sc = toolbarConfig.shortcuts[id];
      const combo = parseShortcut(sc);
      if (!combo) continue;
      const handler = handlers[id];
      if (!handler) continue;
      const key = JSON.stringify(combo);
      if (seen.has(key)) continue;
      seen.add(key);
      entries.push({
        ...combo,
        handler,
        disabled: modalsOpen || !mail.selectedEmail || syntheticView === "calendar" || drawerOpen,
      });
    }
    return entries;
  }, [toolbarConfig, modalsOpen, mail.selectedEmail, syntheticView, drawerOpen]);

  const mailHotkeysDisabled = modalsOpen || syntheticView === "calendar" || drawerOpen;
  useHotkeys([
    { key: "c", handler: openCompose, disabled: modalsOpen || syntheticView === "calendar" },
    { key: "j", handler: () => moveSelection(1), disabled: modalsOpen || syntheticView === "calendar" },
    { key: "k", handler: () => moveSelection(-1), disabled: modalsOpen || syntheticView === "calendar" },
    { key: "Enter", handler: () => { if (selectedEmail) { void mail.selectEmail(selectedEmail.id); } }, disabled: mailHotkeysDisabled || selectedEmail === null },
    { key: "/", handler: openSearch, disabled: mailHotkeysDisabled },
    { key: "Escape", handler: () => { setShowAddAccount(false); setShowCheatsheet(false); setShowCommandPalette(false); }, disabled: composeOpen },
    { key: "R", shift: true, handler: () => { if (mail.selectedEmail) void handleReplyAll(mail.selectedEmail.email.id); }, disabled: mailHotkeysDisabled || !mail.selectedEmail },
    { key: "e", handler: () => { void handleArchiveShortcut(); }, disabled: modalsOpen || !selectedEmail || syntheticView === "calendar" },
    { key: "u", handler: () => { void handleToggleReadShortcut(); }, disabled: mailHotkeysDisabled || !selectedEmail },
    { key: ",", ctrl: true, handler: () => openSettingsPanel(), disabled: composeOpen || showAddAccount },
    { key: "?", handler: () => setShowCheatsheet(true) },
    { key: "k", ctrl: true, handler: () => setShowCommandPalette(true), disabled: modalsOpen },
    ...toolbarHotkeys,
  ]);

  const errorBanner = mail.error ? (
      <div className="fixed top-0 left-0 right-0 z-40 flex items-center justify-between border-b border-destructive/30 bg-destructive/10 px-4 py-2 text-sm text-destructive">
        <span>{mail.error}</span>
      <button type="button" onClick={mail.clearError} className="text-destructive hover:text-destructive focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-primary">
        {t("app.dismiss")}
      </button>
    </div>
  ) : null;

  const offlineBanner = connectionStatus !== "online" ? (
    <div className="fixed top-0 left-0 right-0 z-[39] flex items-center justify-center gap-2 border-b border-warning/30 bg-warning/10 px-4 py-1.5 text-xs text-warning-foreground">
      <span>{connectionStatus === "offline" ? t("offline.banner") : t("offline.partial")}</span>
      {pendingMutationCount > 0 && (
        <span className="rounded-full bg-warning/20 px-2 py-0.5">
          {t("offline.pendingChanges", { count: pendingMutationCount })}
        </span>
      )}
    </div>
  ) : null;

  const authBanner = authErrors.length > 0 ? (
    <AuthExpiredBanner authErrors={authErrors} accounts={mail.accounts} />
  ) : null;

  const handleReply = async (emailId: string) => {
    try {
      const ctx = await api.getReplyContext(emailId);
      setComposeReplyTo(ctx);
      setComposeForward(undefined);
      setComposeForwardUid(undefined);
      setComposeForwardFolderId(undefined);
      setComposeDraftBody(undefined);
      openComposeScreen();
    } catch (err) {
      toast("error", err instanceof Error ? err.message : t("app.failedToOpenReply"));
    }
  };

  const handleReplyAll = async (emailId: string) => {
    try {
      const ctx = await api.getReplyContext(emailId);
      setComposeReplyTo({ ...ctx, reply_all: true });
      setComposeForward(undefined);
      setComposeForwardUid(undefined);
      setComposeForwardFolderId(undefined);
      setComposeDraftBody(undefined);
      openComposeScreen();
    } catch (err) {
      toast("error", err instanceof Error ? err.message : t("app.failedToOpenReplyAll"));
    }
  };

  const handleDraftReply = async (emailId: string, draft: string) => {
    try {
      const ctx = await api.getReplyContext(emailId);
      setComposeReplyTo(ctx);
      setComposeForward(undefined);
      setComposeForwardUid(undefined);
      setComposeForwardFolderId(undefined);
      setComposeDraftBody(draft);
      openComposeScreen();
    } catch (err) {
      toast(
        "error",
        err instanceof Error ? err.message : t("app.failedToLoadDraftReply"),
      );
    }
  };

  const handleForward = async (emailId: string) => {
    try {
      const ctx = await api.getReplyContext(emailId);
      const source = mail.emails.find((email) => email.id === emailId)
        ?? (await api.getEmail(emailId)).email;
      setComposeReplyTo(undefined);
      setComposeForward(ctx);
      setComposeDraftBody(undefined);
      setComposeAccountId(source.account_id);
      setComposeForwardUid(source.has_attachments ? source.uid : undefined);
      setComposeForwardFolderId(source.has_attachments ? source.folder_id : undefined);
      openComposeScreen();
    } catch (err) {
      toast("error", err instanceof Error ? err.message : t("app.failedToOpenForward"));
    }
  };

  const paletteCommands: PaletteCommand[] = useMemo(() => {
    const hasEmailDetail = mail.selectedEmail !== null;
    const commands: PaletteCommand[] = [
      {
        id: 'new-email',
        label: 'New Email',
        shortcut: 'C',
        category: 'compose',
        handler: openCompose,
      },
      {
        id: 'go-inbox',
        label: 'Go to Inbox',
        category: 'navigate',
        handler: () => {
          setSyntheticView(null);
          mail.selectAggregate('Inbox');
        },
      },
      {
        id: 'go-sent',
        label: 'Go to Sent',
        category: 'navigate',
        handler: () => {
          setSyntheticView(null);
          mail.selectAggregate('Sent');
        },
      },
      {
        id: 'go-drafts',
        label: 'Go to Drafts',
        category: 'navigate',
        handler: () => {
          setSyntheticView(null);
          mail.selectAggregate('Drafts');
        },
      },
      {
        id: 'go-starred',
        label: 'Go to Starred',
        category: 'navigate',
        handler: () => void handleSelectSynthetic('starred'),
      },
      {
        id: 'go-pinned',
        label: 'Go to Pinned',
        category: 'navigate',
        handler: () => void handleSelectSynthetic('pins'),
      },
      {
        id: 'next-email',
        label: 'Next Email',
        category: 'navigate',
        handler: () => moveSelection(1),
      },
      {
        id: 'previous-email',
        label: 'Previous Email',
        category: 'navigate',
        handler: () => moveSelection(-1),
      },
      {
        id: 'search-emails',
        label: 'Search Emails',
        shortcut: '/',
        category: 'search',
        handler: openSearch,
      },
      {
        id: 'open-settings',
        label: 'Open Settings',
        shortcut: 'Ctrl+,',
        category: 'settings',
        handler: openSettingsPanel,
      },
      {
        id: 'keyboard-shortcuts',
        label: 'Keyboard Shortcuts',
        shortcut: '?',
        category: 'settings',
        handler: () => setShowCheatsheet(true),
      },
      {
        id: 'smart-search',
        label: 'Smart Search (AI)',
        category: 'ai',
        handler: () => {
          const search = document.querySelector<HTMLInputElement>("[data-mail-search-input]");
          if (search) {
            setReactInputValue(search, "? ");
            search.focus();
            search.dispatchEvent(new Event("input", { bubbles: true }));
            search.setSelectionRange(2, 2);
          }
        },
      },
      {
        id: 'adjust-tone',
        label: 'Adjust Tone (AI)',
        category: 'ai',
        handler: () => {
          window.dispatchEvent(new CustomEvent("maho-adjust-tone"));
        },
        enabled: () => composeOpen,
      },
    ];

    if (hasEmailDetail) {
      commands.push(
        {
          id: 'archive',
          label: 'Archive',
          shortcut: 'E',
          category: 'mail',
          handler: handleArchiveShortcut,
          enabled: () => selectedEmail !== null && archiveFolder !== null,
        },
        {
          id: 'delete',
          label: 'Delete',
          shortcut: 'Shift+#',
          category: 'mail',
          handler: () => {
            if (mail.selectedEmail) {
              void handleDeleteEmail(mail.selectedEmail.email.id);
            }
          },
          enabled: () => mail.selectedEmail !== null,
        },
        {
          id: 'toggle-read',
          label: 'Toggle Read',
          shortcut: 'U',
          category: 'mail',
          handler: handleToggleReadShortcut,
          enabled: () => selectedEmail !== null,
        },
        {
          id: 'toggle-star',
          label: 'Toggle Star',
          category: 'mail',
          handler: () => {
            if (mail.selectedEmail) {
              void handleToggleStar(mail.selectedEmail.email.id);
            }
          },
          enabled: () => mail.selectedEmail !== null,
        },
        {
          id: 'pin',
          label: 'Pin',
          category: 'mail',
          handler: () => {
            if (mail.selectedEmail) {
              void handlePinEmail(mail.selectedEmail.email.id, true);
            }
          },
          enabled: () => mail.selectedEmail !== null,
        },
        {
          id: 'snooze',
          label: 'Snooze',
          category: 'mail',
          handler: () => {
            if (mail.selectedEmail) {
              handleOpenSnooze(mail.selectedEmail.email.id);
            }
          },
          enabled: () => mail.selectedEmail !== null,
        },
        {
          id: 'reply',
          label: 'Reply',
          shortcut: 'R',
          category: 'mail',
          handler: () => {
            if (mail.selectedEmail) {
              void handleReply(mail.selectedEmail.email.id);
            }
          },
          enabled: () => mail.selectedEmail !== null,
        },
        {
          id: 'reply-all',
          label: 'Reply All',
          shortcut: 'Shift+R',
          category: 'mail',
          handler: () => {
            if (mail.selectedEmail) {
              void handleReplyAll(mail.selectedEmail.email.id);
            }
          },
          enabled: () => mail.selectedEmail !== null,
        },
        {
          id: 'forward',
          label: 'Forward',
          shortcut: 'F',
          category: 'mail',
          handler: () => {
            if (mail.selectedEmail) {
              void handleForward(mail.selectedEmail.email.id);
            }
          },
          enabled: () => mail.selectedEmail !== null,
        }
      );

      commands.push(
        {
          id: 'summarize',
          label: 'Summarize Email',
          category: 'ai',
          handler: async () => {
            if (mail.selectedEmail) {
              toast('info', t('app.summarizeStarted'));
              try {
                const res = await api.getEmailSummary(mail.selectedEmail.email.account_id, { email_ids: [mail.selectedEmail.email.id] });
                toast('success', res.summary);
              } catch (err) {
                toast('error', err instanceof Error ? err.message : 'Failed to generate summary');
              }
            }
          },
          enabled: () => mail.selectedEmail !== null,
        },
        {
          id: 'draft-reply',
          label: 'Draft Reply with AI',
          category: 'ai',
          handler: async () => {
            if (mail.selectedEmail) {
              toast('info', 'Generating reply draft...');
              try {
                const res = await api.getReplyDraft(mail.selectedEmail.email.account_id, { email_id: mail.selectedEmail.email.id });
                void handleDraftReply(mail.selectedEmail.email.id, res.draft);
              } catch (err) {
                toast('error', err instanceof Error ? err.message : 'Failed to generate reply draft');
              }
            }
          },
          enabled: () => mail.selectedEmail !== null,
        },
        {
          id: 'classify-email',
          label: 'Classify Email with AI',
          category: 'ai',
          handler: async () => {
            if (mail.selectedEmail) {
              toast('info', 'Classifying email...');
              try {
                const res = await api.classifyEmail(mail.selectedEmail.email.account_id, { email_id: mail.selectedEmail.email.id });
                toast('success', `Classified as: ${res.category}`);
              } catch (err) {
                toast('error', err instanceof Error ? err.message : 'Failed to classify email');
              }
            }
          },
          enabled: () => mail.selectedEmail !== null,
        }
      );
    }

    return commands;
  }, [
    mail.selectedEmail,
    selectedEmail,
    archiveFolder,
    mail.selectAggregate,
    mail.selectedAccountId,
    openSettingsPanel,
    composeOpen,
    t,
    toast,
  ]);

  const sidebarContent = (
    <Sidebar
      accounts={mail.accounts}
      folders={mail.folders}
      selectedFolderId={syntheticView ? null : mail.selectedFolderId}
      selectedFolderType={syntheticView ? null : mail.selectedFolderType}
      selectedSyntheticId={syntheticView}
      onSelectFolder={(folderId) => {
        setDrawerOpen(false);
        setSyntheticView(null);
        mail.changeFolder(folderId);
      }}
      onSelectAggregate={(folderType) => {
        setDrawerOpen(false);
        setSyntheticView(null);
        mail.selectAggregate(folderType);
      }}
      onSelectSynthetic={(id) => {
        setDrawerOpen(false);
        void handleSelectSynthetic(id);
      }}
      onOpenSettings={() => {
        openSettingsPanel();
      }}
      loading={mail.loading || syncing}
      onMoveEmail={(emailId, targetFolderId) => void handleMoveEmail(emailId, targetFolderId)}
      collapsed={drawerOpen ? false : settings.sidebarCollapsed}
      onToggleCollapsed={() => settings.setSidebarCollapsed(!settings.sidebarCollapsed)}
      onCreateFolder={(accountId, folderName) => void handleCreateFolder(accountId, folderName)}
      onRenameFolder={(accountId, folderId, newName) => void handleRenameFolder(accountId, folderId, newName)}
      onDeleteFolder={(accountId, folderId) => void handleDeleteFolder(accountId, folderId)}
      isOnline={isOnline}
      authErrors={authErrors}
      onSearchSelect={(query) => {
        setDrawerOpen(false);
        setSyntheticView(null);
        mail.clearSelectedEmail();
        setSearchRequest({query});
      }}
    />
  );

  if (mail.isBootstrappingAccounts) {
    return <BootingState />;
  }

  if (mail.accounts.length === 0) {
    return (
      <>
        {errorBanner}
        {offlineBanner}
        <OnboardingScreen onAccountAdded={mail.loadAccounts} />
      </>
    );
  }

  return (
    <div className="relative flex h-screen overflow-hidden bg-background text-foreground">
      <a href="#main-content" className="sr-only focus:not-sr-only focus:absolute focus:z-[100] focus:rounded focus:bg-primary focus:px-4 focus:py-2 focus:text-primary-foreground">
        {t("a11y.skipToContent")}
      </a>
      <div className="absolute inset-x-0 top-0 z-30 h-12 pointer-events-none" data-tauri-drag-region />
      {errorBanner}
      {offlineBanner}
      <LiveRegion message={syncing ? t("a11y.syncInProgress") : ""} />

      <nav
        style={{ width: desktopSidebarWidth }}
        className="mail-navigation hidden flex-shrink-0 transition-[width] duration-200 ease-out md:block"
        aria-label={t("a11y.mailNavigation")}
      >
        {sidebarContent}
      </nav>

      {!settings.sidebarCollapsed && (
        <button
          type="button"
          aria-label={t("a11y.resizeSidebar")}
          className={cn(
            "mail-resize-handle hidden w-2 -mx-1 cursor-col-resize md:block",
            settings.isResizing && "is-resizing",
          )}
          onMouseDown={settings.handleResizeMouseDown}
          onDoubleClick={() => {
            settings.setSidebarWidth(240);
          }}
        />
      )}

      <div className="flex min-w-0 flex-1 flex-col">
        <button type="button" className="mail-navigation-trigger items-center gap-2 p-3" aria-label="Open mailbox folders" onClick={() => setDrawerOpen(true)}>
          <Menu size={18} /> Folders
        </button>
        {authBanner}

        <div className="mail-workspace flex min-h-0 flex-1">
          {syntheticView === "calendar" ? null : (
            <section
              data-email-list-pane
              aria-label={t("a11y.emailList")}
              className={cn("mail-list-pane w-full flex-shrink-0 md:flex", mail.selectedEmail || mail.emailLoading || mail.authRecoveryError ? "hidden" : "flex flex-1 md:flex-none")}
              style={{ width: `min(100%, ${settings.emailListWidth}px)` }}
            >
              <EmailList
                emails={syntheticView ? syntheticEmails : mail.emails}
                selectedEmailId={mail.selectedEmail?.email.id ?? null}
                onSelectEmail={handleSelectEmail}
                onToggleStar={handleToggleStar}
                loading={mail.loading}
                accountId={mail.selectedAccountId ?? undefined}
                folderId={mail.selectedFolderId ?? undefined}
                archiveFolderId={archiveFolder?.id}
                archiveFolders={mail.folders.filter(folder => folder.folder_type === "Archive")}
                searchRequest={searchRequest}
                onEmailsRemoved={(ids) => {
                  mail.removeEmails(ids);
                  setSyntheticEmails(prev => prev.filter(email => !ids.includes(email.id)));
                }}
                onLoadMore={mail.loadMoreEmails}
                hasMore={syntheticView ? false : mail.hasMore}
                onPin={handlePinEmail}
                onSnooze={handleOpenSnooze}
                onReply={(id) => void handleReply(id)}
                onForward={(id) => void handleForward(id)}
                smartCategory={syntheticView === "smart_inbox" ? smartCategory : null}
                onSmartCategoryChange={syntheticView === "smart_inbox" ? (cat) => void handleSmartCategoryChange(cat) : undefined}
                density={density}
                mailboxTitle={syntheticView ? syntheticView.replaceAll("_", " ") : mail.selectedFolderType ?? "Inbox"}
                onCompose={openCompose}
                onSync={handleManualSync}
                composeDisabled={composeOpen}
                syncBusy={mail.loading || syncing}
                syncError={mail.error}
                enqueueRetry={enqueueRetry}
              />
            </section>
          )}

          {syntheticView === "calendar" ? null : (
            <button
              type="button"
              aria-label={t("a11y.resizeEmailList")}
              className={cn(
                "mail-resize-handle hidden w-2 -mx-1 cursor-col-resize md:block",
                settings.isEmailListResizing && "is-resizing",
              )}
              onMouseDown={settings.handleEmailListResizeMouseDown}
              onDoubleClick={() => {
                settings.setEmailListWidth(350);
              }}
            />
          )}

          <main id="main-content" tabIndex={-1} className={cn("mail-reader-pane min-w-0 flex-1 md:flex", syntheticView === "calendar" || mail.selectedEmail || mail.emailLoading || mail.authRecoveryError ? "flex" : "hidden")}>
            {syntheticView === "calendar" ? (
              <div className="flex w-full">
                <CalendarGrid
                  accountId={mail.selectedAccountId ?? ""}
                  accounts={mail.accounts}
                  onSelectEvent={(event) => setSelectedEvent(event)}
                  onCreateEvent={(summary) => {
                    setEditEvent(null);
                    setInitialEventSummary(summary || "");
                    setShowCreateEvent(true);
                  }}
                  onReauthorize={() => setShowAddAccount(true)}
                />
              </div>
            ) : (
              <EmailContent
                emailDetail={mail.selectedEmail}
                loading={mail.emailLoading}
                authRecoveryError={mail.authRecoveryError ? {
                  ...mail.authRecoveryError,
                  accountEmail: mail.accounts.find((account) => {
                    const failedEmail = mail.emails.find(
                      (email) => email.id === mail.authRecoveryError?.emailId,
                    );
                    return account.id === failedEmail?.account_id;
                  })?.email,
                } : null}
                onReconnectAccount={reconnectSelectedAccount}
                onReply={(id) => void handleReply(id)}
                onForward={(id) => void handleForward(id)}
                onDelete={handleDeleteEmail}
                onToggleStar={handleToggleStar}
                onBack={mail.clearSelectedEmail}
                onDraftReply={(id, draft) => void handleDraftReply(id, draft)}
                onPin={(id) => void handlePinEmail(id, true)}
                onSnooze={handleOpenSnooze}
                onReminder={(id) => void handleSetReminder(id)}
                onSendRequested={handleSendRequested}
                accountEmail={mail.accounts.find((account) => account.id === (mail.selectedEmail?.email.account_id ?? mail.selectedAccountId))?.email}
                onOpenAiSettings={() => openSettingsPanel("translation")}
                onComposeToRecipient={handleComposeToRecipient}
              />
            )}
          </main>
        </div>
      </div>


      {drawerOpen && <div className="mail-navigation-drawer"><MobileDrawer isOpen onClose={() => setDrawerOpen(false)}>{sidebarContent}</MobileDrawer></div>}

      <ComposeModal
        isOpen={composeOpen}
        onClose={closeCompose}
        accountId={composeAccountId || mail.selectedAccountId || ""}
        accountEmail={mail.accounts.find((a) => a.id === (composeAccountId || mail.selectedAccountId))?.email}
        accounts={mail.accounts}
        sourceAccountId={composeAccountId || mail.selectedEmail?.email.account_id}
        replyTo={composeReplyTo}
        forwardFrom={composeForward}
        forwardEmailUid={composeForwardUid}
        forwardFolderId={composeForwardFolderId}
        initialBody={composeDraftBody}
        initialBodyHtml={composeDraftBodyHtml}
        initialBodyIsComplete={composeBodyIsComplete}
        initialAttachments={composeAttachments}
        initialTo={composeInitialTo}
        initialDraftId={composeInitialDraftId ?? undefined}
        initialSubject={composeInitialSubject}
        initialCc={composeInitialCc}
        initialBcc={composeInitialBcc}
        initialReadReceipt={composeReadReceipt}
        initialPgpEncrypt={composeSecurity.pgpEncrypt}
        initialSmimeSign={composeSecurity.smimeSign}
        initialSmimeEncrypt={composeSecurity.smimeEncrypt}
        initialInReplyTo={composeInReplyTo}
        initialReferences={composeReferences}
        onSendRequested={handleSendRequested}
        fullscreen={false}
      />
      <AddAccountModal
        isOpen={showAddAccount}
        onClose={() => setShowAddAccount(false)}
        onAccountAdded={mail.loadAccounts}
        fullscreen={false}
      />
      <ShortcutCheatsheet isOpen={showCheatsheet} onClose={() => setShowCheatsheet(false)} />
      <CommandPalette open={showCommandPalette} onClose={() => setShowCommandPalette(false)} commands={paletteCommands} />
      <SnoozePicker
        isOpen={showSnoozePicker}
        onClose={() => { setShowSnoozePicker(false); setSnoozeTargetId(null); }}
        onSnooze={(until) => void handleSnooze(until)}
      />
      {pendingSends.map((item, index) => (
        <UndoSendToast
          key={item.id}
          offsetIndex={index}
          delaySeconds={settings.undoSendDelay}
          onUndo={() => handleUndoSend(item.id)}
          onComplete={() => void handleUndoSendComplete(item.id)}
        />
      ))}
      <OnboardingTour />
      <CreateEventDialog
        isOpen={showCreateEvent}
        onClose={() => {
          setShowCreateEvent(false);
          setEditEvent(null);
          setInitialEventSummary("");
        }}
        // The unified all-accounts view has no selected account; fall back like
        // useCalendarClient does so creates never send an empty account_id.
        accountId={
          editEvent?.account_id ||
          mail.selectedAccountId ||
          mail.accounts.find((a) => a.auth_type === "oauth2_gmail" || a.provider === "gmail")?.id ||
          ""
        }
        editEvent={editEvent}
        initialSummary={initialEventSummary}
        onEventSaved={() => {
          void handleSelectSynthetic("calendar");
        }}
      />
      {selectedEvent && (
        <CalendarEventDetail
          event={selectedEvent}
          accountEmail={mail.accounts.find((a) => a.id === mail.selectedAccountId)?.email ?? ""}
          onClose={() => setSelectedEvent(null)}
          onRsvp={async (eventId, status) => {
            try {
              const updated = await api.updateRsvp(eventId, status);
              setSelectedEvent(updated);
              void handleSelectSynthetic("calendar");
            } catch (err) {
              toast("error", err instanceof Error ? err.message : "Failed to RSVP");
            }
          }}
          onDelete={async (eventId) => {
            const event = selectedEvent;
            if (event && (event.recurrence_rule || event.recurring_event_id)) {
              setRecurringPrompt({ event, mode: "delete" });
              return;
            }
            try {
              await api.deleteCalendarEvent(eventId);
              setSelectedEvent(null);
              void handleSelectSynthetic("calendar");
            } catch (err) {
              toast("error", err instanceof Error ? err.message : "Failed to delete event");
            }
          }}
          onEdit={(event) => {
            if (event.recurrence_rule || event.recurring_event_id) {
              setRecurringPrompt({ event, mode: "edit" });
              return;
            }
            setEditEvent(event);
            setShowCreateEvent(true);
          }}
        />
      )}

      {recurringPrompt && (
        <RecurringScopeDialog
          open={true}
          mode={recurringPrompt.mode}
          onCancel={() => setRecurringPrompt(null)}
          onSelect={async (scope) => {
            const { event, mode } = recurringPrompt;
            setRecurringPrompt(null);
            if (mode === "delete") {
              try {
                await api.deleteCalendarEvent(event.id, scope);
                setSelectedEvent(null);
                void handleSelectSynthetic("calendar");
              } catch (err) {
                toast("error", err instanceof Error ? err.message : "Failed to delete event");
              }
            } else {
              setEditEvent({ ...event, _edit_scope: scope } as CalendarEvent & { _edit_scope?: "single" | "all" });
              setShowCreateEvent(true);
            }
          }}
        />
      )}
    </div>
  );
}




interface AppProps {
  readonly renderMailClient?: (props: {
    readonly accounts: readonly MailAccount[];
    readonly settings: UseSettingsReturn;
    readonly density: Density;
  }) => React.ReactNode;
}

export function App({renderMailClient}: AppProps = {}) {
  const [accounts, setAccounts] = useState<readonly MailAccount[]>([]);
  const [lifecycle, setLifecycle] =
    useState<MailLifecycleSnapshot>({state: 'starting', generation: 0n});
  const lifecycleRef = useRef(lifecycle);
  const refreshSequenceRef = useRef(0);
  const [accountLoadError, setAccountLoadError] = useState(false);
  const [showOnboardingCommandPalette, setShowOnboardingCommandPalette] =
    useState(false);
  const [browserUiPrefs, setBrowserUiPrefs] = useState(DEFAULT_BROWSER_UI_PREFS);
  const settings = useSettings();
  const { t } = useTranslation();

  useTheme(browserUiPrefs.theme);

  useEffect(() => {
    document.documentElement.setAttribute("data-density", browserUiPrefs.density);
  }, [browserUiPrefs.density]);

  useEffect(() => {
    let active = true;
    const applyPrefs = (prefsJson: string) => {
      if (active) setBrowserUiPrefs(parseBrowserUiPrefs(prefsJson));
    };
    void handler.getBrowserUiPrefs().then(({ prefsJson }) => applyPrefs(prefsJson));
    const listenerId = callbackRouter.onBrowserUiPrefsChanged.addListener(applyPrefs);
    return () => {
      active = false;
      callbackRouter.removeListener(listenerId);
    };
  }, []);

  const refreshAccounts = useCallback(async () => {
    const expectedGeneration = lifecycleRef.current.generation;
    const refreshSequence = ++refreshSequenceRef.current;
    const result = await handler.listAccounts();
    const currentLifecycle = lifecycleRef.current;
    if (
      refreshSequence !== refreshSequenceRef.current ||
      currentLifecycle.generation !== expectedGeneration ||
      lifecycleClearsSensitiveState(currentLifecycle.state)
    ) {
      return;
    }
    if (!result.ok) {
      setAccountLoadError(true);
      return;
    }
    const parsed = parseMailAccounts(result.resultJson);
    if (!parsed) {
      setAccountLoadError(true);
      return;
    }
    setAccountLoadError(false);
    setAccounts(parsed);
  }, []);

  useEffect(() => {
    void refreshAccounts();
  }, [refreshAccounts]);

  useEffect(() => {
    const listenerId = callbackRouter.onLifecycleChanged.addListener(
      (state: string, nextGeneration: bigint) => {
        const next = acceptMailLifecycle(
          lifecycleRef.current,
          state,
          nextGeneration,
        );
        if (!next) return;
        lifecycleRef.current = next;
        setLifecycle(next);
        if (lifecycleClearsSensitiveState(next.state)) {
          ++refreshSequenceRef.current;
          setAccounts([]);
          setAccountLoadError(false);
        }
        if (next.state === 'ready') {
          void refreshAccounts();
        }
      },
    );
    return () => callbackRouter.removeListener(listenerId);
  }, [refreshAccounts]);

  useEffect(() => {
    const onKeyDown = (event: KeyboardEvent) => {
      if (event.key.toLowerCase() === 'k' && (event.metaKey || event.ctrlKey)) {
        event.preventDefault();
        setShowOnboardingCommandPalette(true);
      }
    };
    window.addEventListener('keydown', onKeyDown);
    return () => window.removeEventListener('keydown', onKeyDown);
  }, []);

  const api_onboarding: MailOnboardingApi = useMemo(() => ({
    listAccounts: () => handler.listAccounts(),
    addAccount: (requestJson) => handler.addAccount(requestJson),
    testConnection: (paramsJson) => handler.testConnection(paramsJson),
    deleteAccount: (accountId) => handler.deleteAccount(accountId),
    beginOAuth: (provider) => handler.beginOAuth(provider, ''),
    subscribeAccountsChanged: (cb: () => void) => {
      const listenerId = callbackRouter.onAccountsChanged.addListener(cb);
      return () => {
        callbackRouter.removeListener(listenerId);
      };
    },
    cancelOAuth: async (state: string) => {
      const res = await handler.cancelOAuth(state);
      return res.accepted;
    },
    onComplete: () => {
      handler.closeOnboarding();
    }
  }), []);

  const onboardingStrings = useCallback<MailOnboardingStringLookup>((key, fallback, values) => {
    const translated = t(key, {...values, defaultValue: fallback});
    return typeof translated === 'string'
      ? translated
      : formatMailOnboardingString(fallback, values);
  }, [t]);

  const onboardingCommands = useMemo<PaletteCommand[]>(() => [
    {
      id: 'retry-mail-startup',
      label: 'Retry Mail startup',
      category: 'mail',
      keywords: ['helper', 'reload'],
      handler: () => {
        const next = {...lifecycleRef.current, state: 'starting' as const};
        lifecycleRef.current = next;
        setLifecycle(next);
        void refreshAccounts();
      },
    },
    {
      id: 'open-mail-settings',
      label: 'Open Mail settings',
      category: 'settings',
      keywords: ['features', 'accounts'],
      handler: () => {
        window.location.href = 'chrome://maho-settings/mail';
      },
    },
  ], [refreshAccounts]);

  if (lifecycle.state === 'disabled') {
    return (
      <main className="flex min-h-screen items-center justify-center bg-background p-8">
        <section className="max-w-md space-y-4 text-center" role="status">
          <h1 className="text-2xl font-semibold">Mail is turned off</h1>
          <p className="text-muted-foreground">
            Enable Mail from the Features pane to access your accounts.
          </p>
          <a className="text-primary underline" href="chrome://maho-settings/features">
            Open Features settings
          </a>
        </section>
      </main>
    );
  }

  if (lifecycle.state === 'failed' || lifecycle.state === 'stopped') {
    return (
      <main className="flex min-h-screen items-center justify-center bg-background p-8">
        <section className="max-w-md space-y-4 text-center" role="alert">
          <h1 className="text-2xl font-semibold">Mail could not start</h1>
          <p className="text-muted-foreground">
            The Mail helper is unavailable. Retry after checking your profile settings.
          </p>
          <button
            className="rounded-md bg-primary px-4 py-2 text-primary-foreground"
            onClick={() => {
              const next = {...lifecycleRef.current, state: 'starting' as const};
              lifecycleRef.current = next;
              setLifecycle(next);
              void refreshAccounts();
            }}
          >
            Retry
          </button>
        </section>
      </main>
    );
  }

  if (accounts.length > 0) {
    if (renderMailClient) {
      return renderMailClient({accounts, settings, density: browserUiPrefs.density});
    }
    return <MailClientShell settings={settings} density={browserUiPrefs.density} />;
  }

  return (
    <div className="flex min-h-screen w-full flex-col items-center overflow-y-auto bg-background">
      <div className="flex w-full max-w-2xl flex-col gap-6 px-8 py-10">
        <MailOnboardingSidebar accounts={accounts} t={onboardingStrings} />
        {accountLoadError && (
          <div className="rounded-md border border-destructive/30 bg-destructive/10 p-3 text-sm text-destructive" role="alert">
            <p>Mail setup could not load your accounts.</p>
            <button
              className="mt-2 rounded-md bg-primary px-3 py-1.5 text-primary-foreground"
              onClick={() => void refreshAccounts()}
            >
              Retry setup
            </button>
          </div>
        )}
        <MailOnboardingContent
          api={api_onboarding}
          accounts={accounts}
          onAccountsChange={setAccounts}
          config={MAIL_APP_ONBOARDING_CONFIG}
          t={onboardingStrings}
        />
        <button
          className="self-center text-sm text-muted-foreground underline"
          onClick={() => handler.closeOnboarding()}
        >
          Skip for now
        </button>
        <CommandPalette
          open={showOnboardingCommandPalette}
          onClose={() => setShowOnboardingCommandPalette(false)}
          commands={onboardingCommands}
        />
      </div>
    </div>
  );
}

function RootView() {
  if (isPopout()) {
    const type = getPopoutType();
    if (type === "reader") {
      const emailId = getPopoutEmailId();
      if (emailId) return <ReaderWindow emailId={emailId} />;
    }
    if (type === "compose") {
      return <ComposeWindow />;
    }
  }
  return <App />;
}

const rootElement = document.getElementById('app');
if (rootElement) {
  createRoot(rootElement).render(
    <React.StrictMode>
      <TooltipProvider delayDuration={200}>
        <ConfirmProvider>
          <ErrorBoundary>
            <RootView />
          </ErrorBoundary>
        </ConfirmProvider>
      </TooltipProvider>
      <Toaster richColors position="bottom-right" />
    </React.StrictMode>
  );
} else if (!('vi' in globalThis)) {
  throw new Error('Missing #app root for maho_mail.');
}
