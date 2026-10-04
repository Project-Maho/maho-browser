import { useEffect, useState } from "react";
import {
  Reply,
  ReplyAll,
  Sparkles,
  Tag,
  MessageSquare,
  Loader2,
  ChevronDown,
  ChevronUp,
  ArrowLeft,
  Shield,
  ShieldCheck,
  ShieldAlert,
  Lock,
  Unlock,
  FileText,
  LogIn,
} from "lucide-react";
import { EmptyState } from "../common/EmptyState";
import { DelegateEmailDialog } from "../common/DelegateEmailDialog";
import { CalendarInviteCard, hasCalendarInviteContent } from "../calendar/CalendarInviteCard";
import { AutoDraftBanner } from "../email/AutoDraftBanner";
import { EmailBodyView } from "./EmailBodyView";
import { ConversationThread } from "./ConversationThread";
import { AIActionsMenu } from "../email/AIActionsMenu";
import { TranslationStatusBar } from "../email/TranslationStatusBar";
import { EmailActionsToolbar } from "../email/EmailActionsToolbar";
import * as api from "../../api";
import type { EmailDetail, EmailSummary, Label, ComposeEmailRequest, Attachment, Email, SendRequestedOptions } from "../../types";
import { useToast } from "../ui/Toast";
import { Avatar } from "../ui/Avatar";
import { EmailDetailSkeleton } from "../ui/Skeleton";
import { formatRelativeDate } from "../ui/utils";
import {
  useTranslation,
  SUPPORTED_LANGUAGES,
  SUPPORTED_LANGUAGE_CODES,
  getTranslationTargetLang,
  saveTranslationTargetLang,
  type LanguageCode,
} from "../../hooks/useTranslation";
import { useClipboard } from "../../hooks/useClipboard";
import { AddressPopover } from "../email/AddressPopover";
import { parseAddressList } from "../../utils/addresses";
import { useTranslation as useI18nTranslation } from "react-i18next";
import { usePopoutWindow } from "../../hooks/usePopoutWindow";
import { useEmailSecurity } from "../../hooks/useEmailSecurity";
import { useQuickReply } from "../../hooks/useQuickReply";
import { detectLanguage, isPotentialIdnHomograph } from "../../utils/languageDetect";
import { sanitizeEmailHtml } from "../../utils/sanitizeHtml";
import { blockExternalResources } from "../../utils/trackerBlocker";

interface AttachmentPreviewProps {
  attachment: Attachment;
  email: Email;
}

export function AttachmentPreview({ attachment }: AttachmentPreviewProps) {
  const isImage = attachment.mime_type.startsWith("image/");
  const isPdf = attachment.mime_type === "application/pdf";

  if (isPdf) {
    return (
      <div className="mb-2 flex items-center justify-center rounded-lg bg-muted/30 p-4">
        <FileText size={32} className="text-red-400" />
      </div>
    );
  }

  if (!isImage) return null;

  return (
    <div className="mb-2 flex h-[80px] items-center justify-center rounded-lg bg-muted/30 text-xs text-muted-foreground">
      Inline preview unavailable
    </div>
  );
}

const QWEN_HTML_HINT_KEY = "maho-qwen-html-translation-hint-shown";
const QWEN_HTML_HINT_MESSAGE = "Local translation model is plain-text only. Switch to a cloud AI provider in Settings to translate HTML emails while preserving formatting.";

function showQwenHtmlHintOnce(toast: (type: "info", message: string) => void): void {
  if (localStorage.getItem(QWEN_HTML_HINT_KEY) === "true") return;
  toast("info", QWEN_HTML_HINT_MESSAGE);
  localStorage.setItem(QWEN_HTML_HINT_KEY, "true");
}

export function generatePrintHtml(email: Email, bodyHtml: string | null): string {
  const safeHtml = (s: string | null) =>
    (s ?? "")
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;")
      .replace(/"/g, "&quot;")
      .replace(/'/g, "&#39;");

  const dateStr = new Date(email.date).toLocaleString();
  const bodyContent = bodyHtml
    ? blockExternalResources(sanitizeEmailHtml(bodyHtml), {
        blockImages: true,
        blockTrackers: true,
      }).html
    : `<pre style="white-space:pre-wrap;font-family:inherit;">${safeHtml(email.body_text ?? email.snippet)}</pre>`;

  return `<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8" />
  <meta http-equiv="Content-Security-Policy" content="default-src 'none'; img-src data: blob: cid:; style-src 'unsafe-inline'">
  <title>${safeHtml(email.subject)}</title>
  <style>
    * { box-sizing: border-box; }
    body {
      margin: 0; padding: 24px 32px;
      font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, 'Helvetica Neue', Arial, sans-serif;
      font-size: 14px; line-height: 1.6; color: #1a1a1a;
      background: #fff;
    }
    .email-header { margin-bottom: 24px; border-bottom: 2px solid #e5e7eb; padding-bottom: 16px; }
    .email-subject { font-size: 20px; font-weight: 600; margin: 0 0 12px 0; color: #111; }
    .header-table { width: 100%; border-collapse: collapse; }
    .header-table td { padding: 3px 0; vertical-align: top; }
    .header-label { font-weight: 600; color: #555; width: 60px; padding-right: 12px; }
    .header-value { color: #333; }
    .email-body { margin-top: 16px; }
    .email-body img { max-width: 100%; height: auto; }
    .email-body a { color: #2563eb; }
    @media print {
      body { padding: 0; }
      @page { margin: 1.5cm; }
      .email-body { page-break-inside: auto; }
      p, li, tr { page-break-inside: avoid; }
    }
  </style>
</head>
<body>
  <div class="email-header">
    <h1 class="email-subject">${safeHtml(email.subject)}</h1>
    <table class="header-table">
      <tr><td class="header-label">From</td><td class="header-value">${safeHtml(email.from_name ? `${email.from_name} <${email.from_address}>` : email.from_address)}</td></tr>
      <tr><td class="header-label">To</td><td class="header-value">${safeHtml(email.to_addresses)}</td></tr>
      ${email.cc_addresses ? `<tr><td class="header-label">Cc</td><td class="header-value">${safeHtml(email.cc_addresses)}</td></tr>` : ""}
      <tr><td class="header-label">Date</td><td class="header-value">${safeHtml(dateStr)}</td></tr>
    </table>
  </div>
  <div class="email-body">${bodyContent}</div>
<!--maho-tt-pipeline-->
</html>`;
}

interface EmailContentProps {
  emailDetail: EmailDetail | null;
  loading?: boolean;
  onReply: (emailId: string) => void;
  onForward: (emailId: string) => void;
  onDelete: (emailId: string) => void;
  onToggleStar: (emailId: string) => void;
  onBack?: () => void;
  onDraftReply?: (emailId: string, draft: string) => void;
  onSelectEmail?: (emailId: string) => void;
  onPin?: (emailId: string) => void;
  onSnooze?: (emailId: string) => void;
  onReminder?: (emailId: string) => void;
  onSendRequested?: (request: ComposeEmailRequest, options?: SendRequestedOptions) => void;
  onOpenAiSettings?: () => void;
  onComposeToRecipient?: (email: string) => void;
  accountEmail?: string;
  authRecoveryError?: {
    emailId: string;
    accountEmail?: string;
    message: string;
  } | null;
  onReconnectAccount?: () => void;
}

export function EmailContent({
  emailDetail,
  loading = false,
  onReply,
  onForward,
  onDelete,
  onToggleStar,
  onBack,
  onDraftReply,
  onSelectEmail,
  onPin,
  onSnooze,
  onReminder,
  onSendRequested,
  onOpenAiSettings,
  onComposeToRecipient,
  accountEmail,
  authRecoveryError,
  onReconnectAccount,
}: EmailContentProps) {
  const { toast } = useToast();
  const { t } = useI18nTranslation();
  const { copy } = useClipboard();
  const { openReader } = usePopoutWindow();
  const [summary, setSummary] = useState<string | null>(null);
  const [summaryOpen, setSummaryOpen] = useState(false);
  const [summaryLoading, setSummaryLoading] = useState(false);
  const [showOriginalBody, setShowOriginalBody] = useState(false);
  const [draftLoading, setDraftLoading] = useState(false);
  const [classifyLoading, setClassifyLoading] = useState(false);
  const [category, setCategory] = useState<string | null>(null);
  const [aiError, setAiError] = useState<string | null>(null);
  const [threadEmails, setThreadEmails] = useState<EmailSummary[]>([]);
  const [threadExpanded, setThreadExpanded] = useState(false);
  const [emailLabels, setEmailLabels] = useState<Label[]>([]);
  const [allLabels, setAllLabels] = useState<Label[]>([]);
  const [showLabelPicker, setShowLabelPicker] = useState(false);
  const [labelSearch, setLabelSearch] = useState("");
  const [isThreadMuted, setIsThreadMuted] = useState(false);
  const [showDelegateDialog, setShowDelegateDialog] = useState(false);
  const [mdnSending, setMdnSending] = useState(false);
  const [mdnDismissed, setMdnDismissed] = useState(false);

  const {
    isPgpEncrypted, isPgpSigned,
    pgpDecryptedText, pgpDecrypting, pgpDecryptError, pgpVerifyResult,
    handlePgpDecrypt, handlePgpVerify,
    isSmimeEncrypted, isSmimeSigned,
    smimeDecryptedText, smimeDecrypting, smimeDecryptError, smimeVerifyResult,
    handleSmimeDecrypt, handleSmimeVerify,
    resetSecurity,
  } = useEmailSecurity({ emailDetail });

  const {
    quickReplyText, setQuickReplyText,
    quickReplyExpanded, setQuickReplyExpanded,
    quickReplyAll,
    quickReplySending,
    quickReplyRef,
    handleQuickReply,
    resetQuickReply,
  } = useQuickReply({ email: emailDetail?.email ?? null, emailDetail, accountEmail, onSendRequested, toast, t });

  const {
    translatedText,
    translatedHtml,
    isTranslating,
    isModelLoading,
    error: translateError,
    fromLang,
    toLang,
    translate: doTranslate,
    translateHtml: doTranslateHtml,
    setFromLang,
    setToLang,
    clearTranslation,
    cleanup: cleanupTranslator,
    partialFailureCount,
  } = useTranslation();

  useEffect(() => {
    if (!translateError) return;
    if (/^(no ai provider|ai provider not configured|translation provider not yet configured)/i.test(translateError)) {
      return;
    }
    toast("error", translateError);
    clearTranslation();
  }, [translateError, toast, clearTranslation]);

  useEffect(() => {
    let cancelled = false;

    if (emailDetail?.email?.message_id) {
      api
        .listThreadEmails(emailDetail.email.account_id, emailDetail.email.message_id)
        .then((emails) => {
          if (!cancelled) {
            setThreadEmails(emails);
            setThreadExpanded(emails.length > 1);
          }
        })
        .catch((err) => {
          if (!cancelled) {
            setThreadEmails([]);
            setThreadExpanded(false);
            toast("error", err instanceof Error ? err.message : "Failed to load thread");
          }
        });
    } else {
      setThreadEmails([]);
      setThreadExpanded(false);
    }

    return () => {
      cancelled = true;
    };
  }, [emailDetail?.email?.id, emailDetail?.email?.account_id, emailDetail?.email?.message_id]);

  useEffect(() => {
    clearTranslation();
    setSummary(null);
    setSummaryOpen(false);
    setCategory(null);
    setAiError(null);
    setEmailLabels([]);
    setShowLabelPicker(false);
    resetQuickReply();
    setMdnDismissed(false);
    resetSecurity();
    setIsThreadMuted(false);
  }, [emailDetail?.email?.id, clearTranslation, resetQuickReply, resetSecurity]);

  useEffect(() => {
    if (!emailDetail) return;
    const text = emailDetail.email.body_text ?? emailDetail.email.snippet ?? "";
    const detected = detectLanguage(text, SUPPORTED_LANGUAGE_CODES);

    if (!detected) return;

    const configRaw = localStorage.getItem("maho-translation-config");
    if (!configRaw) return;
    try {
      const config = JSON.parse(configRaw) as { provider?: string; defaultTargetLang?: string; alwaysTranslateFrom?: string[] };
      if (config.provider === "skip") return;
      const target = getTranslationTargetLang();
      if (detected === target) return;
      if (config.alwaysTranslateFrom?.includes(detected)) {
        const bodyHtml = emailDetail.email.body_html;
        const htmlSize = bodyHtml ? bodyHtml.length : 0;

        if (bodyHtml && config.provider === "byok") {
          if (htmlSize <= 8 * 1024) {
            setFromLang(detected as Parameters<typeof setFromLang>[0]);
            setToLang(target);
            void doTranslateHtml(bodyHtml, detected, target);
          }
        } else {
          if (bodyHtml && config.provider === "local") {
            showQwenHtmlHintOnce(toast);
          }
          setFromLang(detected as Parameters<typeof setFromLang>[0]);
          setToLang(target);
          void doTranslate(text, detected, target);
        }
      }
    } catch { /* invalid config, ignore */ }
  }, [
    emailDetail?.email?.id,
    emailDetail?.email?.body_text,
    emailDetail?.email?.body_html,
    emailDetail?.email?.snippet,
    doTranslate,
    doTranslateHtml,
    setFromLang,
    setToLang,
    toast,
  ]);

  useEffect(() => {
    if (partialFailureCount > 0) {
      toast("info", `${partialFailureCount} paragraph${partialFailureCount > 1 ? 's' : ''} could not be translated cleanly and fell back to original text.`);
    }
  }, [partialFailureCount]);

  useEffect(() => {
    if (!emailDetail?.email?.id || !emailDetail?.email?.message_id) return;
    let cancelled = false;
    api.isThreadMuted(emailDetail.email.account_id, emailDetail.email.message_id).then((muted) => {
      if (!cancelled) setIsThreadMuted(muted);
    }).catch(() => undefined);
    return () => { cancelled = true; };
  }, [emailDetail?.email?.id, emailDetail?.email?.account_id, emailDetail?.email?.message_id]);

  useEffect(() => {
    if (!emailDetail?.email?.id) return;
    let cancelled = false;

    api.listEmailLabels(emailDetail.email.id).then((labels) => {
      if (!cancelled) setEmailLabels(labels);
    }).catch(() => undefined);

    api.listLabels(emailDetail.email.account_id).then((labels) => {
      if (!cancelled) setAllLabels(labels);
    }).catch(() => undefined);

    return () => { cancelled = true; };
  }, [emailDetail?.email?.id, emailDetail?.email?.account_id]);

  useEffect(() => {
    return () => cleanupTranslator();
  }, [cleanupTranslator]);

  if (loading) {
    return (
      <div className="flex flex-1 flex-col bg-background animate-fade-in">
        <EmailDetailSkeleton />
      </div>
    );
  }

  if (!emailDetail) {
    if (authRecoveryError) {
      return (
        <div className="flex flex-1 items-center justify-center bg-background/95 px-8 animate-fade-in">
          <div role="alert" className="max-w-md rounded-3xl border border-amber-500/30 bg-amber-500/10 px-10 py-9 text-center shadow-sm">
            <div className="mx-auto flex h-12 w-12 items-center justify-center rounded-2xl bg-amber-500/15 text-amber-300">
              <LogIn size={22} />
            </div>
            <h2 className="mt-5 text-lg font-semibold text-foreground">Sign in to continue</h2>
            <p className="mt-2 text-sm leading-6 text-muted-foreground">
              {authRecoveryError.accountEmail
                ? `${authRecoveryError.accountEmail} needs to be connected again before Maho can load this message.`
                : "Your email account needs to be connected again before Maho can load this message."}
            </p>
            <button
              type="button"
              onClick={onReconnectAccount}
              className="mt-6 inline-flex items-center justify-center gap-2 rounded-xl bg-primary px-4 py-2 text-sm font-semibold text-primary-foreground shadow-sm transition-colors hover:bg-primary/90 focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring"
            >
              <LogIn size={16} />
              Sign in again
            </button>
          </div>
        </div>
      );
    }

    return (
      <div className="flex flex-1 items-center justify-center bg-background/95 px-8 animate-fade-in">
        <div className="max-w-md rounded-3xl border border-border/50 bg-card/35 px-10 py-9 shadow-sm backdrop-blur-xl">
          <EmptyState
            title="Select an email to read"
            description="Choose a message from the list. Your reading pane keeps the conversation and actions in one place."
          />
          <div className="mt-6 flex flex-wrap justify-center gap-2 text-xs text-muted-foreground">
            <span className="rounded-lg border border-border/60 bg-background/70 px-2.5 py-1.5"><kbd className="font-semibold text-foreground">J/K</kbd> move</span>
            <span className="rounded-lg border border-border/60 bg-background/70 px-2.5 py-1.5"><kbd className="font-semibold text-foreground">Enter</kbd> open</span>
            <span className="rounded-lg border border-border/60 bg-background/70 px-2.5 py-1.5"><kbd className="font-semibold text-foreground">C</kbd> compose</span>
          </div>
        </div>
      </div>
    );
  }

  const { email } = emailDetail;
  const showMdnBanner = !!email.mdn_requested && !mdnDismissed;

  async function handleSendMdnReceipt() {
    try {
      setMdnSending(true);
      await api.sendMdnReceipt({ emailId: email.id, accountId: email.account_id });
      setMdnDismissed(true);
      toast("success", "Read receipt sent");
    } catch (err) {
      toast("error", err instanceof Error ? err.message : "Failed to send read receipt");
    } finally {
      setMdnSending(false);
    }
  }

  async function handleSummarize() {
    if (!emailDetail) return;
    try {
      setSummaryLoading(true);
      setAiError(null);
      const result = await api.getEmailSummary(emailDetail.email.account_id, {
        email_ids: [emailDetail.email.id],
      });
      setSummary(result.summary);
      setSummaryOpen(true);
    } catch (err) {
      const message = err instanceof Error ? err.message : "Failed to summarize";
      setAiError(message);
    } finally {
      setSummaryLoading(false);
    }
  }

  async function handleDraftReply() {
    if (!emailDetail) return;
    try {
      setDraftLoading(true);
      setAiError(null);
      const response = await api.getReplyDraft(emailDetail.email.account_id, { email_id: emailDetail.email.id });
      if (onDraftReply) {
        onDraftReply(emailDetail.email.id, response.draft);
      } else {
        onReply(emailDetail.email.id);
      }
    } catch (err) {
      const message = err instanceof Error ? err.message : "Failed to draft reply";
      setAiError(message);
    } finally {
      setDraftLoading(false);
    }
  }

  async function handleClassify() {
    if (!emailDetail) return;

    try {
      setClassifyLoading(true);
      setAiError(null);
      const result = await api.classifyEmail(emailDetail.email.account_id, { email_id: emailDetail.email.id });
      setCategory(result.category);
    } catch (err) {
      const message = err instanceof Error ? err.message : "Failed to classify email";
      setAiError(message);
    } finally {
      setClassifyLoading(false);
    }
  }

  function handleTranslate(target: LanguageCode = getTranslationTargetLang()) {
    const configRaw = localStorage.getItem("maho-translation-config");
    let provider = "byok";
    try {
      if (configRaw) {
        const config = JSON.parse(configRaw);
        provider = config.provider || "byok";
      }
    } catch { /* ignore */ }

    if (provider === "skip") {
      toast("error", "Translation is disabled in Settings.");
      return;
    }

    if (email.body_html) {
      if (provider === "local") {
        const text = email.body_text ?? email.snippet ?? "";
        if (!text.trim()) {
          toast("error", "No text content to translate");
          return;
        }
        setToLang(target);
        void doTranslate(text, undefined, target);

        showQwenHtmlHintOnce(toast);
      } else {
        setToLang(target);
        void doTranslateHtml(email.body_html, undefined, target);
      }
    } else {
      const text = email.body_text ?? email.snippet ?? "";
      if (!text.trim()) {
        toast("error", "No text content to translate");
        return;
      }
      setToLang(target);
      void doTranslate(text, undefined, target);
    }
  }

  function handleChangeTranslateTarget(code: string) {
    const target = code as LanguageCode;
    saveTranslationTargetLang(target);
    handleTranslate(target);
  }

  async function handleToggleLabel(labelId: string) {
    const isAttached = emailLabels.some((l) => l.id === labelId);
    try {
      if (isAttached) {
        await api.removeLabelFromEmail(email.id, labelId);
        setEmailLabels((prev) => prev.filter((l) => l.id !== labelId));
      } else {
        await api.addLabelToEmail(email.id, labelId);
        const label = allLabels.find((l) => l.id === labelId);
        if (label) setEmailLabels((prev) => [...prev, label]);
      }
    } catch (err) {
      toast("error", err instanceof Error ? err.message : "Failed to update label");
    }
  }

  const categoryStyles: Record<string, string> = {
    important: "border-red-500/40 bg-destructive/10 text-destructive",
    general: "border-primary/40 bg-primary/10 text-primary",
    promotion: "border-amber-500/40 bg-amber-500/10 text-amber-300",
    spam: "border-border/40 bg-muted-foreground/10 text-muted-foreground",
  };

  async function handleDelegate(toEmail: string, note: string) {
    try {
      await api.delegateEmail({
        emailId: email.id,
        accountId: email.account_id,
        delegateTo: toEmail,
        note,
      });
      setShowDelegateDialog(false);
      toast("success", `Delegated to ${toEmail}`);
    } catch (err) {
      toast("error", err instanceof Error ? err.message : "Failed to delegate");
    }
  }

  async function handleToggleMute() {
    if (!email.message_id) return;
    try {
      if (isThreadMuted) {
        await api.unmuteThread(email.account_id, email.message_id);
        setIsThreadMuted(false);
        toast("success", t("mute.unmuted"));
      } else {
        await api.muteThread(email.account_id, email.message_id);
        setIsThreadMuted(true);
        toast("success", t("mute.muted"));
      }
    } catch (err) {
      toast("error", err instanceof Error ? err.message : "Failed to toggle mute");
    }
  }

  return (
    <article aria-label={`${t("a11y.emailContent")}: ${email.subject}`} className="flex flex-1 flex-col bg-background animate-fade-in select-text">
      {onBack && (
        <button type="button" onClick={onBack} aria-label="Back to email list" className="md:hidden flex min-h-11 items-center gap-1 px-3 text-muted-foreground hover:text-foreground focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-primary">
          <ArrowLeft size={18} /> Back
        </button>
      )}
      {/* Header */}
      <div className="border-b border-border/60 px-6 pb-5 pt-6">
        <div className="flex items-start gap-4">
          <Avatar
            name={email.from_name ?? email.from_address}
            email={email.from_address}
            size="lg"
            className="mt-0.5 shrink-0"
          />
          <div className="min-w-0 flex-1">
            <div className="flex items-start gap-3">
              <div className="min-w-0 flex-1">
                <h2 className="text-xl font-semibold leading-snug tracking-tight text-foreground [text-wrap:balance]">
                  <button
                    type="button"
                    onClick={() => {
                      const selection = window.getSelection();
                      if (selection && selection.toString().length > 0) {
                        return;
                      }
                      void copy(email.subject);
                    }}
                    className="block w-full text-left rounded-md -mx-1.5 px-1.5 py-0.5 transition-colors hover:bg-surface-hover focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-primary cursor-pointer"
                    aria-label={t("email.copySubject")}
                    title={t("email.copySubject")}
                  >
                    {email.subject}
                  </button>
                </h2>
                <div className="mt-1 flex flex-wrap items-center gap-x-2 gap-y-1">
                  <AddressPopover
                    address={email.from_address}
                    name={email.from_name}
                    accountId={email.account_id}
                    onCompose={onComposeToRecipient}
                  >
                    <button
                      type="button"
                      className="inline-flex items-center gap-2 rounded-md -mx-1.5 px-1.5 py-0.5 transition-colors hover:bg-surface-hover focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-primary cursor-pointer"
                      aria-label={t("email.senderOptionsFor", { name: email.from_name ?? email.from_address })}
                    >
                      <span className="font-medium text-foreground">
                        {email.from_name ?? email.from_address}
                      </span>
                      <span className="text-sm text-muted-foreground">
                        &lt;{email.from_address}&gt;
                      </span>
                    </button>
                  </AddressPopover>
                  {isPotentialIdnHomograph(email.from_address) && (
                    <span
                      className="inline-flex items-center gap-1 rounded bg-destructive/10 px-1.5 py-0.5 text-[10px] font-medium text-destructive border border-destructive/20"
                      title="Warning: Sender domain uses non-standard characters (IDN Homograph attack risk)"
                    >
                      <ShieldAlert className="h-3 w-3 animate-pulse" />
                      Homograph Risk
                    </span>
                  )}
                  <span className="text-xs text-muted-foreground">
                    {formatRelativeDate(email.date)}
                  </span>
                </div>
                <p className="mt-2 flex flex-wrap items-center gap-x-1 gap-y-1 text-sm text-muted-foreground">
                  <span>To:</span>
                  {parseAddressList(email.to_addresses).map((addr, idx, arr) => (
                    <span key={`to-${addr}`} className="inline-flex items-center">
                      <AddressPopover
                        address={addr}
                        accountId={email.account_id}
                        onCompose={onComposeToRecipient}
                      >
                        <button
                          type="button"
                          className="rounded-md -mx-1 px-1 transition-colors hover:bg-surface-hover focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-primary cursor-pointer"
                        >
                          {addr}
                        </button>
                      </AddressPopover>
                      {idx < arr.length - 1 ? <span>,</span> : null}
                    </span>
                  ))}
                  {parseAddressList(email.cc_addresses).length > 0 && (
                    <>
                      <span>·</span>
                      <span>Cc:</span>
                      {parseAddressList(email.cc_addresses).map((addr, idx, arr) => (
                        <span key={`cc-${addr}`} className="inline-flex items-center">
                          <AddressPopover
                            address={addr}
                            accountId={email.account_id}
                            onCompose={onComposeToRecipient}
                          >
                            <button
                              type="button"
                              className="rounded-md -mx-1 px-1 transition-colors hover:bg-surface-hover focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-primary cursor-pointer"
                            >
                              {addr}
                            </button>
                          </AddressPopover>
                          {idx < arr.length - 1 ? <span>,</span> : null}
                        </span>
                      ))}
                    </>
                  )}
                </p>
              </div>
              <div className="flex shrink-0 items-center gap-2">
                {(translatedText || translatedHtml || translateError) && (
                  <TranslationStatusBar
                    fromLang={fromLang}
                    toLang={toLang}
                    onClose={() => {
                      clearTranslation();
                      setShowOriginalBody(false);
                    }}
                    error={translateError}
                    onOpenAiSettings={onOpenAiSettings}
                    languages={SUPPORTED_LANGUAGES}
                    onChangeToLang={handleChangeTranslateTarget}
                  />
                )}
                <AIActionsMenu
                  onSummarize={() => void handleSummarize()}
                  onDraftReply={() => void handleDraftReply()}
                  onClassify={() => void handleClassify()}
                  onTranslate={() => handleTranslate()}
                  summaryLoading={summaryLoading}
                  draftLoading={draftLoading}
                  classifyLoading={classifyLoading}
                  isTranslating={isTranslating}
                  isModelLoading={isModelLoading}
                  aiError={aiError}
                />
              </div>
            </div>
          </div>
        </div>
      </div>

      {(emailLabels.length > 0 || allLabels.length > 0) && (
        <div className="flex flex-wrap items-center gap-2 border-b border-border px-6 py-2">
          {emailLabels.map((label) => (
            <button
              key={label.id}
              type="button"
              onClick={() => void handleToggleLabel(label.id)}
              className="mail-pressable flex h-7 items-center gap-1.5 rounded-full border border-border px-2.5 text-xs text-muted-foreground hover:border-destructive/50 hover:text-destructive"
              title="Remove label"
              aria-label={`Remove label ${label.name}`}
            >
              <span className="h-2 w-2 rounded-full" style={{ backgroundColor: label.color }} />
              {label.name}
              <span className="text-muted-foreground">&times;</span>
            </button>
          ))}
          <div className="relative">
            <button
              type="button"
              onClick={() => {
                setShowLabelPicker(!showLabelPicker);
                if (!showLabelPicker) setLabelSearch("");
              }}
              className="mail-pressable flex h-7 items-center gap-1 rounded-full border border-dashed border-border px-2.5 text-xs text-muted-foreground hover:border-foreground/30 hover:bg-surface-hover hover:text-foreground"
            >
              <Tag size={10} />
              Add Label
            </button>
            {showLabelPicker && (
              <>
                <button
                  type="button"
                  className="fixed inset-0 z-40 cursor-default"
                  aria-label="Close label picker"
                  onClick={() => { setShowLabelPicker(false); setLabelSearch(""); }}
                />
                <div className="absolute left-0 top-full z-50 mt-1 min-w-[180px] rounded-xl border border-border bg-popover p-1 shadow-xl animate-scale-in">
                  <div className="px-2 py-1.5">
                    <input
                      type="text"
                      value={labelSearch}
                      onChange={(e) => setLabelSearch(e.target.value)}
                      placeholder="Filter labels..."
                      className="w-full rounded border border-border bg-background px-2 py-1 text-xs text-foreground placeholder:text-muted-foreground focus:border-ring focus:outline-none"
                    />
                  </div>
                  {allLabels
                    .filter((l) => !emailLabels.some((el) => el.id === l.id))
                    .filter((l) => !labelSearch || l.name.toLowerCase().includes(labelSearch.toLowerCase()))
                    .map((label) => (
                      <button
                        key={label.id}
                        type="button"
                        onClick={() => {
                          void handleToggleLabel(label.id);
                          setShowLabelPicker(false);
                          setLabelSearch("");
                        }}
                        className="flex h-8 w-full items-center gap-2 rounded-md px-2.5 text-left text-xs text-muted-foreground transition-colors hover:bg-surface-hover hover:text-foreground"
                      >
                        <span className="h-2 w-2 rounded-full" style={{ backgroundColor: label.color }} />
                        {label.name}
                      </button>
                    ))}
                  {allLabels.filter((l) => !emailLabels.some((el) => el.id === l.id))
                    .filter((l) => !labelSearch || l.name.toLowerCase().includes(labelSearch.toLowerCase()))
                    .length === 0 && (
                    <p className="px-3 py-1.5 text-xs text-muted-foreground">
                      {labelSearch ? "No matching labels" : "No more labels"}
                    </p>
                  )}
              </div>
              </>
            )}
          </div>
        </div>
      )}

      {(isPgpEncrypted || isPgpSigned) && (
        <div className="flex flex-wrap items-center gap-2 border-b border-border px-6 py-2">
          {isPgpEncrypted && (
            <>
              <span className="flex items-center gap-1.5 rounded-full border border-primary/30 bg-primary/5 px-2.5 py-1 text-xs font-medium text-primary">
                <Lock size={12} />
                PGP Encrypted
              </span>
              {!pgpDecryptedText && !pgpDecryptError && (
                <button
                  type="button"
                  onClick={() => void handlePgpDecrypt()}
                  disabled={pgpDecrypting}
                  className="mail-pressable flex h-8 items-center gap-1.5 rounded-lg border border-border bg-card/50 px-3 text-xs text-muted-foreground hover:bg-surface-hover hover:text-foreground disabled:opacity-50"
                >
                  {pgpDecrypting ? <Loader2 size={12} className="animate-spin" /> : <Unlock size={12} />}
                  Decrypt
                </button>
              )}
              {pgpDecryptedText && (
                <span className="flex items-center gap-1.5 rounded-full border border-emerald-500/30 bg-emerald-500/5 px-2.5 py-1 text-xs font-medium text-emerald-600 dark:text-emerald-400">
                  <ShieldCheck size={12} />
                  Decrypted
                </span>
              )}
              {pgpDecryptError && (
                <span className="flex items-center gap-1.5 rounded-full border border-destructive/30 bg-destructive/5 px-2.5 py-1 text-xs font-medium text-destructive">
                  <ShieldAlert size={12} />
                  {pgpDecryptError}
                </span>
              )}
            </>
          )}
          {isPgpSigned && (
            <>
              {!pgpVerifyResult ? (
                <button
                  type="button"
                  onClick={() => void handlePgpVerify()}
                  className="flex items-center gap-1.5 rounded-full border border-amber-500/30 bg-amber-500/5 px-2.5 py-1 text-xs font-medium text-amber-600 dark:text-amber-400 transition-colors hover:bg-amber-500/10"
                >
                  <Shield size={12} />
                  Verify Signature
                </button>
              ) : pgpVerifyResult.is_valid ? (
                <span className="flex items-center gap-1.5 rounded-full border border-emerald-500/30 bg-emerald-500/5 px-2.5 py-1 text-xs font-medium text-emerald-600 dark:text-emerald-400">
                  <ShieldCheck size={12} />
                  Signed by {pgpVerifyResult.signer_email}
                </span>
              ) : (
                <span className="flex items-center gap-1.5 rounded-full border border-destructive/30 bg-destructive/5 px-2.5 py-1 text-xs font-medium text-destructive">
                  <ShieldAlert size={12} />
                  {pgpVerifyResult.error ?? "Invalid signature"}
                </span>
              )}
            </>
          )}
        </div>
      )}

      {(isSmimeEncrypted || isSmimeSigned) && (
        <div className="flex flex-wrap items-center gap-2 border-b border-border px-6 py-2">
          {isSmimeEncrypted && (
            <>
              <span className="flex items-center gap-1.5 rounded-full border border-primary/30 bg-primary/5 px-2.5 py-1 text-xs font-medium text-primary">
                <Lock size={12} />
                S/MIME Encrypted
              </span>
              {!smimeDecryptedText && !smimeDecryptError && (
                <button
                  type="button"
                  onClick={() => void handleSmimeDecrypt()}
                  disabled={smimeDecrypting}
                  className="mail-pressable flex h-8 items-center gap-1.5 rounded-lg border border-border bg-card/50 px-3 text-xs text-muted-foreground hover:bg-surface-hover hover:text-foreground disabled:opacity-50"
                >
                  {smimeDecrypting ? <Loader2 size={12} className="animate-spin" /> : <Unlock size={12} />}
                  Decrypt
                </button>
              )}
              {smimeDecryptedText && (
                <span className="flex items-center gap-1.5 rounded-full border border-emerald-500/30 bg-emerald-500/5 px-2.5 py-1 text-xs font-medium text-emerald-600 dark:text-emerald-400">
                  <ShieldCheck size={12} />
                  Decrypted
                </span>
              )}
              {smimeDecryptError && (
                <span className="flex items-center gap-1.5 rounded-full border border-destructive/30 bg-destructive/5 px-2.5 py-1 text-xs font-medium text-destructive">
                  <ShieldAlert size={12} />
                  {smimeDecryptError}
                </span>
              )}
            </>
          )}
          {isSmimeSigned && (
            <>
              {!smimeVerifyResult ? (
                <button
                  type="button"
                  onClick={() => void handleSmimeVerify()}
                  className="flex items-center gap-1.5 rounded-full border border-amber-500/30 bg-amber-500/5 px-2.5 py-1 text-xs font-medium text-amber-600 dark:text-amber-400 transition-colors hover:bg-amber-500/10"
                >
                  <Shield size={12} />
                  Verify S/MIME
                </button>
              ) : smimeVerifyResult.valid && smimeVerifyResult.trusted ? (
                <span className="flex items-center gap-1.5 rounded-full border border-emerald-500/30 bg-emerald-500/5 px-2.5 py-1 text-xs font-medium text-emerald-600 dark:text-emerald-400">
                  <ShieldCheck size={12} />
                  S/MIME Signed by {smimeVerifyResult.signer_email ?? smimeVerifyResult.signer_subject}
                </span>
              ) : smimeVerifyResult.valid && !smimeVerifyResult.trusted ? (
                <span className="flex items-center gap-1.5 rounded-full border border-amber-500/30 bg-amber-500/5 px-2.5 py-1 text-xs font-medium text-amber-600 dark:text-amber-400">
                  <ShieldAlert size={12} />
                  Signature Valid (Untrusted Certificate)
                </span>
              ) : (
                <span className="flex items-center gap-1.5 rounded-full border border-destructive/30 bg-destructive/5 px-2.5 py-1 text-xs font-medium text-destructive">
                  <ShieldAlert size={12} />
                  {smimeVerifyResult.signer_subject ?? "Invalid S/MIME signature"}
                </span>
              )}
            </>
          )}
        </div>
      )}

      {(category || aiError) && (
        <div className="border-b border-border/60 bg-card/35 px-6 py-3">
          <div className="flex flex-wrap items-center gap-2 rounded-xl border border-border/50 bg-background/65 px-3 py-2.5 shadow-sm">
            <Sparkles size={14} className={aiError ? "text-destructive" : "text-primary"} />
            <span className="text-xs font-semibold text-foreground">Mail intelligence</span>
            {category && (
          <span
            className={`inline-flex items-center rounded-full border px-2.5 py-1 text-xs font-medium capitalize ${categoryStyles[category] ?? "border-border bg-card text-muted-foreground"}`}
          >
            <Tag size={12} className="mr-1" />
            {category}
          </span>
            )}
            {aiError && <span className="min-w-0 flex-1 text-xs text-destructive">{aiError}</span>}
            {aiError && onOpenAiSettings && (
              <button type="button" onClick={onOpenAiSettings} className="text-xs font-semibold text-primary hover:underline">
                Open settings
              </button>
            )}
            <button type="button" onClick={() => { setAiError(null); setCategory(null); }} className="ml-auto text-xs text-muted-foreground hover:text-foreground">
              Dismiss
            </button>
          </div>
        </div>
      )}

      {threadEmails.length > 1 && (
        <div className="flex items-center gap-2 border-b border-border px-6 py-3 text-sm text-muted-foreground">
          <MessageSquare className="h-4 w-4" />
          <span>{threadEmails.length} messages in thread</span>
        </div>
      )}

      {/* AI Summary collapsible */}
      {summary && (
        <div className="border-b border-border/60 bg-card/35 px-6 py-3">
          <div className="rounded-2xl border border-primary/20 bg-primary/5 shadow-sm">
          <button
            type="button"
            onClick={() => setSummaryOpen(!summaryOpen)}
            className="flex w-full items-center gap-2 px-4 py-3 text-left text-xs font-semibold text-foreground"
          >
            <Sparkles size={12} />
            AI Summary
            {summaryOpen ? <ChevronUp size={12} /> : <ChevronDown size={12} />}
          </button>
          {summaryOpen && (
            <div className="px-4 pb-4 text-sm leading-relaxed text-muted-foreground">
              {summary}
            </div>
          )}
          </div>
        </div>
      )}

      {showMdnBanner && (
        <div className="flex items-center gap-3 border-b border-border bg-primary/10 px-6 py-3">
          <span className="text-sm text-primary">
            The sender requested a read receipt.
          </span>
          <button
            type="button"
            onClick={() => void handleSendMdnReceipt()}
            disabled={mdnSending}
            className="rounded-lg border border-primary/40 bg-primary/20 px-3 py-1 text-xs font-medium text-primary transition-colors hover:bg-primary/30 disabled:opacity-50"
          >
            {mdnSending ? <Loader2 size={12} className="inline animate-spin" /> : "Send Receipt"}
          </button>
          <button
            type="button"
            onClick={() => setMdnDismissed(true)}
            className="mail-pressable ml-auto flex h-7 w-7 items-center justify-center rounded-md text-xs text-muted-foreground hover:bg-surface-hover hover:text-foreground"
            aria-label="Dismiss"
            title="Dismiss"
          >
            ✕
          </button>
        </div>
      )}

      {/* Email body & Attachments */}
      <div className="flex-1 overflow-y-auto select-text flex flex-col min-h-0">
        {hasCalendarInviteContent(email.body_text, email.body_html) && (
          <div className="px-6 pt-4">
            <CalendarInviteCard
              emailId={email.id}
              accountId={email.account_id}
              bodyText={email.body_text}
              bodyHtml={email.body_html}
            />
          </div>
        )}
        {threadEmails.length > 1 ? (
          <ConversationThread
            threadEmails={threadEmails}
            selectedEmailId={email.id}
            selectedDetail={emailDetail}
            pgpDecryptedText={pgpDecryptedText}
            smimeDecryptedText={smimeDecryptedText}
            translatedText={translatedText}
            translatedHtml={translatedHtml}
            showOriginalBody={showOriginalBody}
            onToggleShowOriginal={() => setShowOriginalBody((v) => !v)}
            onEmailAddressClick={onComposeToRecipient}
          />
        ) : (
          <EmailBodyView
            emailDetail={emailDetail}
            pgpDecryptedText={pgpDecryptedText}
            smimeDecryptedText={smimeDecryptedText}
            translatedText={translatedText}
            translatedHtml={translatedHtml}
            showOriginalBody={showOriginalBody}
            onToggleShowOriginal={() => setShowOriginalBody((v) => !v)}
            onEmailAddressClick={onComposeToRecipient}
          />
        )}
      </div>

      <AutoDraftBanner
        emailId={email.id}
        accountId={email.account_id}
        onAccept={(draftContent) => {
          setQuickReplyText(draftContent);
          setQuickReplyExpanded(true);
          requestAnimationFrame(() => quickReplyRef.current?.focus());
        }}
      />

      <div className="border-t border-border px-4 py-3">
        {!quickReplyExpanded ? (
          <button
            type="button"
            onClick={() => {
              setQuickReplyExpanded(true);
              requestAnimationFrame(() => quickReplyRef.current?.focus());
            }}
            onKeyDown={(e) => {
              if (e.key === "Enter") {
                e.preventDefault();
                setQuickReplyExpanded(true);
                requestAnimationFrame(() => quickReplyRef.current?.focus());
              }
            }}
            className="w-full rounded-xl border border-border bg-card/40 px-4 py-2.5 text-left text-sm text-muted-foreground transition-colors hover:border-foreground/20 hover:bg-surface-hover"
          >
            {t("email.quickReplyPlaceholder")}
          </button>
        ) : (
          <div className="space-y-2">
            <textarea
              ref={quickReplyRef}
              value={quickReplyText}
              onChange={(e) => setQuickReplyText(e.target.value)}
              placeholder={t("email.quickReplyPlaceholder")}
              aria-label={t("a11y.quickReply")}
              rows={3}
              className="w-full resize-none rounded-xl border border-border bg-background px-4 py-3 text-sm leading-relaxed text-foreground placeholder:text-muted-foreground transition-colors focus:border-primary/50 focus:outline-none animate-fade-in"
              onKeyDown={(e) => {
                if (e.key === "Enter" && (e.metaKey || e.ctrlKey)) {
                  e.preventDefault();
                  void handleQuickReply(quickReplyAll);
                }
                if (e.key === "Escape") {
                  if (!quickReplyText.trim()) {
                    setQuickReplyExpanded(false);
                  }
                }
              }}
            />
            <div className="flex items-center gap-2">
              <button
                type="button"
                onClick={() => void handleQuickReply(false)}
                disabled={quickReplySending || !quickReplyText.trim()}
                className="mail-pressable flex h-8 items-center gap-1.5 rounded-lg bg-primary px-3.5 text-xs font-medium text-primary-foreground shadow-sm hover:bg-primary/90 disabled:opacity-50"
              >
                {quickReplySending ? <Loader2 size={14} className="animate-spin" /> : <Reply size={14} />}
                {t("email.reply")}
              </button>
              <button
                type="button"
                onClick={() => void handleQuickReply(true)}
                disabled={quickReplySending || !quickReplyText.trim()}
                className="mail-pressable flex h-8 items-center gap-1.5 rounded-lg px-3 text-xs font-medium text-foreground hover:bg-surface-hover disabled:opacity-50"
              >
                {quickReplySending ? <Loader2 size={14} className="animate-spin" /> : <ReplyAll size={14} />}
                {t("email.replyAll")}
              </button>
              <span className="ml-auto text-xs text-muted-foreground">
                {t("email.quickReplySendHint")}
              </span>
            </div>
          </div>
        )}
      </div>

      <EmailActionsToolbar
        onReply={() => onReply(email.id)}
        onReplyWithAI={() => void handleDraftReply()}
        onForward={() => onForward(email.id)}
        onPrint={() => {
          const html = generatePrintHtml(email, email.body_html);
          const printWindow = window.open("", "_blank");
          if (printWindow) {
            printWindow.document.write(html);
            printWindow.document.close();
            printWindow.focus();
            printWindow.print();
          }
        }}
        onDelete={() => onDelete(email.id)}
        onPin={onPin ? () => onPin(email.id) : undefined}
        onSnooze={onSnooze ? () => onSnooze(email.id) : undefined}
        onReminder={onReminder ? () => onReminder(email.id) : undefined}
        onDelegate={() => setShowDelegateDialog(true)}
        onToggleMute={() => void handleToggleMute()}
        onPopOut={() => void openReader(email.id, email.subject)}
        onToggleStar={() => onToggleStar(email.id)}
        draftLoading={draftLoading}
        isThreadMuted={isThreadMuted}
        isStarred={email.is_starred}
      />
      <DelegateEmailDialog
        isOpen={showDelegateDialog}
        onClose={() => setShowDelegateDialog(false)}
        onDelegate={(toEmail, note) => void handleDelegate(toEmail, note)}
      />
    </article>
  );
}
