import { useState, useEffect, useRef, useId } from "react";
import { X, ChevronDown, ChevronUp, Loader2, Clock, FileText, Code, Paperclip, Lock, Shield, Maximize2, Minimize2 } from "lucide-react";
import { useTranslation } from "react-i18next";
import { ToneAdjustButton } from "./ToneAdjustButton";
import * as api from "../../api";
import type { ReplyContext, AccountSummary, ComposeEmailRequest, Signature, ComposeAttachment, EmailTemplate, SendLaterRequest } from "../../types";
import { useToast } from "../ui/Toast";
import { useConfirm } from "../ui/ConfirmDialog";
import { Tooltip, TooltipContent, TooltipTrigger } from "../ui/Tooltip";
import { RichTextEditor } from "./RichTextEditor";
import { SchedulePicker } from "../common/SchedulePicker";
import { ContactAutocomplete } from "./ContactAutocomplete";
import { useSettings } from "../../hooks/useSettings";
import { useComposeDraft } from "../../hooks/useComposeDraft";
import { useComposeAttachments } from "../../hooks/useComposeAttachments";
import { useComposeRecipients } from "../../hooks/useComposeRecipients";
import { useComposeSecurity } from "../../hooks/useComposeSecurity";
import { useVirtualKeyboard } from "../../hooks/useVirtualKeyboard";
import { cn } from "../../lib/utils";
import { isMobile } from "../../utils/platform";
import { isValidEmail } from "../../utils/email";
import { base64ToUint8Array, uint8ArrayToBase64 } from "../../utils/base64";
import { sanitizeEmailHtml } from "../../utils/sanitizeHtml";
import { UndoSendToast } from "../common/UndoSendToast";
import { emitMailUpdated } from "../../hooks/usePopoutWindow";

export interface BodySplit {
  userText: string;
  userHtml: string;
  signatureText: string;
  signatureHtml: string;
  quoteText: string;
  quoteHtml: string;
}

export function splitBodyForTone(body: string, bodyHtml: string): BodySplit {
  const sigRegexText = /\n\n--\n[\s\S]*?(?=\n\nOn |\n\n---------- Forwarded|$)/;
  const sigRegexHtml = /<br><br><div data-maho-signature="true"[\s\S]*?<\/div>/;
  const quoteRegexText = /\n\nOn [\s\S]*$|\n\n---------- Forwarded[\s\S]*$/;
  const quoteRegexHtml = /<br><br><p>On [\s\S]*$|<br><br><p>---------- Forwarded[\s\S]*$/;

  let userText = body;
  let userHtml = bodyHtml;

  const sigTextMatch = userText.match(sigRegexText);
  const signatureText = sigTextMatch ? sigTextMatch[0] : "";
  if (sigTextMatch) {
    userText = userText.slice(0, sigTextMatch.index ?? 0);
  }

  const sigHtmlMatch = userHtml.match(sigRegexHtml);
  const signatureHtml = sigHtmlMatch ? sigHtmlMatch[0] : "";
  if (sigHtmlMatch) {
    userHtml = userHtml.slice(0, sigHtmlMatch.index ?? 0);
  }

  const quoteTextMatch = userText.match(quoteRegexText);
  let quoteText = "";
  if (quoteTextMatch) {
    quoteText = quoteTextMatch[0];
    userText = userText.slice(0, quoteTextMatch.index ?? 0);
  } else if (signatureText) {
    const afterSig = body.slice((sigTextMatch?.index ?? 0) + signatureText.length);
    const tailQuoteMatch = afterSig.match(quoteRegexText);
    if (tailQuoteMatch) {
      quoteText = tailQuoteMatch[0];
    }
  }

  const quoteHtmlMatch = userHtml.match(quoteRegexHtml);
  let quoteHtml = "";
  if (quoteHtmlMatch) {
    quoteHtml = quoteHtmlMatch[0];
    userHtml = userHtml.slice(0, quoteHtmlMatch.index ?? 0);
  } else if (signatureHtml) {
    const afterSig = bodyHtml.slice((sigHtmlMatch?.index ?? 0) + signatureHtml.length);
    const tailQuoteMatch = afterSig.match(quoteRegexHtml);
    if (tailQuoteMatch) {
      quoteHtml = tailQuoteMatch[0];
    }
  }

  return {
    userText: userText.trimEnd(),
    userHtml: userHtml.trimEnd(),
    signatureText,
    signatureHtml,
    quoteText,
    quoteHtml,
  };
}

function buildInitialSubject(
  replyTo?: ReplyContext,
  forwardFrom?: ReplyContext,
): string {
  if (replyTo) {
    const subj = replyTo.original_subject;
    return subj.startsWith("Re:") ? subj : `Re: ${subj}`;
  }
  if (forwardFrom) {
    const subj = forwardFrom.original_subject;
    return subj.startsWith("Fwd:") ? subj : `Fwd: ${subj}`;
  }
  return "";
}

function escapeHtml(text: string): string {
  return text
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/\"/g, "&quot;")
    .replace(/'/g, "&#39;");
}

function plainTextToHtml(text: string): string {
  return escapeHtml(text).replace(/\n/g, "<br>");
}

function buildQuoteText(
  replyTo?: ReplyContext,
  forwardFrom?: ReplyContext,
): string {
  const ctx = replyTo ?? forwardFrom;
  if (!ctx) return "";

  const dateStr = new Date(ctx.original_date).toLocaleString();
  const header = `\n\nOn ${dateStr}, ${ctx.original_from} wrote:\n> `;
  const quoted = (ctx.original_body_text ?? "").replace(/\n/g, "\n> ");
  return header + quoted;
}

function buildQuoteHtml(
  replyTo?: ReplyContext,
  forwardFrom?: ReplyContext,
): string {
  const ctx = replyTo ?? forwardFrom;
  if (!ctx) return "";

  const dateStr = new Date(ctx.original_date).toLocaleString();
  const header = `<br><br><p>On ${escapeHtml(dateStr)}, ${escapeHtml(ctx.original_from)} wrote:</p>`;
  const rawBody = ctx.original_body_html ?? plainTextToHtml(ctx.original_body_text ?? "");
  const body = sanitizeEmailHtml(rawBody);
  return header + `<blockquote>${body}</blockquote>`;
}

function buildSignatureHtml(sig: Signature): string {
  const safeBody = sanitizeEmailHtml(sig.body_html);
  return `<br><br><div data-maho-signature="true" class="email-signature">--<br>${safeBody}</div>`;
}

function buildSignatureText(sig: Signature): string {
  return `\n\n--\n${sig.body_text}`;
}

export interface ComposeFormProps {
  onClose: () => void;
  replyTo?: ReplyContext;
  forwardFrom?: ReplyContext;
  accountId: string;
  accountEmail?: string;
  initialBody?: string;
  initialBodyHtml?: string;
  initialTo?: string;
  initialDraftId?: string;
  initialSubject?: string;
  initialCc?: string;
  initialBcc?: string;
  accounts: AccountSummary[];
  sourceAccountId?: string;
  onSendRequested?: (request: ComposeEmailRequest) => void;
  fullscreen?: boolean;
  onToggleFullscreen?: () => void;
  isPopout?: boolean;
}

export function ComposeForm({
  onClose,
  replyTo,
  forwardFrom,
  accountId,
  accountEmail,
  initialBody,
  initialBodyHtml,
  initialTo,
  initialDraftId,
  initialSubject,
  initialCc,
  initialBcc,
  accounts,
  sourceAccountId,
  onSendRequested,
  fullscreen = false,
  onToggleFullscreen,
  isPopout = false,
}: ComposeFormProps) {
  const { toast } = useToast();
  const confirm = useConfirm();
  const { t } = useTranslation();
  const { autoSaveDrafts, undoSendDelay } = useSettings();
  const toInputId = useId();
  const ccInputId = useId();
  const bccInputId = useId();
  const subjectInputId = useId();
  const fromInputId = useId();
  const messageInputId = useId();
  const sendErrorId = useId();
  const attachmentsHeadingId = useId();
  
  const [subject, setSubject] = useState("");
  const [body, setBody] = useState("");
  const [bodyHtml, setBodyHtml] = useState("");
  const bodyRef = useRef(body);
  bodyRef.current = body;
  const bodyHtmlRef = useRef(bodyHtml);
  bodyHtmlRef.current = bodyHtml;
  const [sending, setSending] = useState(false);
  const [sendError, setSendError] = useState<string | null>(null);
  const [selectedAccountId, setSelectedAccountId] = useState<string>("");
  const [showSchedule, setShowSchedule] = useState(false);

  const [signatures, setSignatures] = useState<Signature[]>([]);
  const [selectedSignatureId, setSelectedSignatureId] = useState<string>("");
  const userHasTypedRef = useRef(false);
  const prevIsOpenRef = useRef(false);
  const security = useComposeSecurity({ isOpen: true, prevIsOpenRef });
  const attach = useComposeAttachments({ isOpen: true, prevIsOpenRef, t, toast: toast as (type: string, message: string) => void });
  const [editorMode, setEditorMode] = useState<"rich" | "plain">("rich");
  const fileInputRef = useRef<HTMLInputElement>(null);
  const [showTemplatePicker, setShowTemplatePicker] = useState(false);
  const [templates, setTemplates] = useState<EmailTemplate[]>([]);
  const templatePickerRef = useRef<HTMLDivElement>(null);
  const { keyboardVisible, keyboardHeight } = useVirtualKeyboard();
  const mobile = isMobile();

  const isInitializedRef = useRef(false);
  const [pendingSend, setPendingSend] = useState<ComposeEmailRequest | null>(null);

  // Resolve the effective account ID for sending
  const effectiveAccountId = sourceAccountId || accountId || selectedAccountId;

  // Resolve the effective account email for reply-all filtering
  const effectiveAccountEmail =
    accounts.find((a) => a.id === effectiveAccountId)?.email ?? accountEmail;

  const recipients = useComposeRecipients({
    initialTo,
    initialCc,
    initialBcc,
    replyTo,
    forwardFrom,
    effectiveAccountEmail,
    isOpen: true,
    prevIsOpenRef,
  });

  const { to, setTo, cc, setCc, bcc, setBcc, showCcBcc, setShowCcBcc } = recipients;

  const draft = useComposeDraft({
    isOpen: true,
    autoSaveDrafts,
    effectiveAccountId: effectiveAccountId || null,
    subject,
    body,
    bodyHtml,
    to,
    cc,
    bcc,
    attachments: attach.attachments,
    requestReadReceipt: security.requestReadReceipt,
    initialDraftId,
  });
  const handleDiscardRef = useRef(handleDiscard);
  handleDiscardRef.current = handleDiscard;

  // Listen for Escape key to close/save compose
  useEffect(() => {
    const handleEscape = (e: KeyboardEvent) => {
      if (e.key === "Escape") {
        void handleDiscardRef.current();
      }
    };
    document.addEventListener("keydown", handleEscape);
    return () => document.removeEventListener("keydown", handleEscape);
  }, []);

  // Reset form fields and resolve account when component mounts
  useEffect(() => {
    if (isInitializedRef.current) return;
    isInitializedRef.current = true;

    setSubject(initialSubject ?? buildInitialSubject(replyTo, forwardFrom));

    const quoteText = buildQuoteText(replyTo, forwardFrom);
    const quoteHtml = buildQuoteHtml(replyTo, forwardFrom);

    const baseText = initialBody ? initialBody : "";
    const baseHtml = initialBodyHtml ? initialBodyHtml : (initialBody ? plainTextToHtml(initialBody) : "");

    setBody(baseText + quoteText);
    setBodyHtml(baseHtml + quoteHtml);

    setSending(false);
    setSendError(null);
    setShowSchedule(false);

    if (sourceAccountId) {
      setSelectedAccountId(sourceAccountId);
    } else if (accountId) {
      setSelectedAccountId(accountId);
    } else if (accounts.length > 0) {
      setSelectedAccountId(accounts.length === 1 ? accounts[0].id : "");
    }
  }, [
    initialSubject,
    replyTo,
    forwardFrom,
    initialBody,
    initialBodyHtml,
    accountId,
    sourceAccountId,
    accounts,
  ]);

  // Derived dirty tracking: compare actual user text (excluding signatures/quotes) with initial body
  const currentUserText = splitBodyForTone(body, bodyHtml).userText;
  const isFormDirty =
    to.trim() !== (initialTo ?? "") ||
    cc.trim() !== (initialCc ?? "") ||
    bcc.trim() !== (initialBcc ?? "") ||
    subject.trim() !== (initialSubject ?? "") ||
    currentUserText !== (initialBody ?? "");

  draft.isDirtyRef.current = isFormDirty;
  userHasTypedRef.current = isFormDirty;

  useEffect(() => {
    if (!effectiveAccountId) {
      setSignatures([]);
      return;
    }
    let cancelled = false;
    void api
      .listSignatures(effectiveAccountId)
      .then((sigs) => {
        if (cancelled) return;
        setSignatures(sigs);
        const active = sigs.find((s) => s.is_default);
        if (active) {
          setSelectedSignatureId(active.id);
          if (userHasTypedRef.current) return;
          const sigText = buildSignatureText(active);
          const sigHtml = buildSignatureHtml(active);
          const quoteText = buildQuoteText(replyTo, forwardFrom);
          const quoteHtml = buildQuoteHtml(replyTo, forwardFrom);
          const userText = bodyRef.current.replace(quoteText, "");
          const userHtml = bodyHtmlRef.current.replace(quoteHtml, "");

          setBody(userText + sigText + quoteText);
          setBodyHtml(userHtml + sigHtml + quoteHtml);
        } else {
          setSelectedSignatureId("");
        }
      })
      .catch(() => {});
    return () => {
      cancelled = true;
    };
  }, [effectiveAccountId]);

  useEffect(() => {
    if (!effectiveAccountId) {
      setTemplates([]);
      return;
    }
    let cancelled = false;
    void api
      .listTemplates()
      .then((res) => {
        if (!cancelled) setTemplates(res);
      })
      .catch(() => {});
    return () => {
      cancelled = true;
    };
  }, [effectiveAccountId]);

  async function handleEditorModeToggle() {
    if (editorMode === "rich") {
      const hasFormatting = /<(?!p>|\/p>|br>)[a-z]/i.test(bodyHtml);
      if (hasFormatting) {
        const confirmed = await confirm({
          title: t("compose.switchToPlainTitle"),
          message: t("compose.switchToPlainWarning"),
          confirmLabel: t("compose.switch"),
          danger: true,
        });
        if (!confirmed) return;
      }
      setEditorMode("plain");
    } else {
      setEditorMode("rich");
      setBodyHtml(plainTextToHtml(body));
    }
  }

  // Handle template picker click outside
  useEffect(() => {
    function handleClickOutside(event: MouseEvent) {
      if (
        templatePickerRef.current &&
        !templatePickerRef.current.contains(event.target as Node)
      ) {
        setShowTemplatePicker(false);
      }
    }
    document.addEventListener("mousedown", handleClickOutside);
    return () => document.removeEventListener("mousedown", handleClickOutside);
  }, []);

  const handleSignatureChange = (sigId: string) => {
    const prevSig = signatures.find((s) => s.id === selectedSignatureId);
    const newSig = signatures.find((s) => s.id === sigId);
    setSelectedSignatureId(sigId);

    const quoteText = buildQuoteText(replyTo, forwardFrom);
    const quoteHtml = buildQuoteHtml(replyTo, forwardFrom);

    let cleanText = body.replace(quoteText, "");
    let cleanHtml = bodyHtml.replace(quoteHtml, "");

    if (prevSig) {
      cleanText = cleanText.replace(buildSignatureText(prevSig), "");
      cleanHtml = cleanHtml.replace(buildSignatureHtml(prevSig), "");
    }

    const addedText = newSig ? buildSignatureText(newSig) : "";
    const addedHtml = newSig ? buildSignatureHtml(newSig) : "";

    setBody(cleanText + addedText + quoteText);
    setBodyHtml(cleanHtml + addedHtml + quoteHtml);
  };

  const handleApplyTemplate = async (tpl: EmailTemplate) => {
    const isDirty = subject || body || attach.attachments.length > 0;
    if (isDirty) {
      const confirmed = await confirm({
        title: t("compose.templateReplaceTitle"),
        message: t("compose.templateReplaceMessage"),
        confirmLabel: t("compose.templateReplaceConfirm"),
        danger: true,
      });
      if (!confirmed) return;
    }

    const quoteText = buildQuoteText(replyTo, forwardFrom);
    const quoteHtml = buildQuoteHtml(replyTo, forwardFrom);

    const prevSig = signatures.find((s) => s.id === selectedSignatureId);
    const sigText = prevSig ? buildSignatureText(prevSig) : "";
    const sigHtml = prevSig ? buildSignatureHtml(prevSig) : "";

    setBody((tpl.body_text ?? "") + sigText + quoteText);
    setBodyHtml((tpl.body_html ?? "") + sigHtml + quoteHtml);

    const newSubject =
      replyTo || forwardFrom
        ? subject.startsWith("Re: ")
          ? "Re: " + (tpl.subject ?? "")
          : subject.startsWith("Fwd: ")
          ? "Fwd: " + (tpl.subject ?? "")
          : (tpl.subject ?? "")
        : (tpl.subject ?? "");
    setSubject(newSubject);

    setShowTemplatePicker(false);
    toast("success", t("compose.templateApplied"));
  };

  async function handleSend() {
    const autocompleteInputs = document.querySelectorAll('input[role="combobox"]');
    let blurredAny = false;
    autocompleteInputs.forEach((el) => {
      if (el instanceof HTMLInputElement && el.value.trim()) {
        el.blur();
        blurredAny = true;
      }
    });

    if (blurredAny) {
      setTimeout(() => void handleSend(), 0);
      return;
    }
    const toAddresses = recipients.parseRecipients(to);
    if (toAddresses.length === 0) {
      setSendError(t("compose.recipientRequired"));
      return;
    }
    const allAddresses = [
      ...toAddresses,
      ...recipients.parseRecipients(cc),
      ...recipients.parseRecipients(bcc),
    ];
    const invalid = allAddresses.filter((addr) => !isValidEmail(addr));
    if (invalid.length > 0) {
      setSendError(t("compose.invalidRecipients", { addresses: invalid.join(", ") }));
      return;
    }
    if (!effectiveAccountId) {
      setSendError(t("compose.selectAccountRequired"));
      return;
    }

    const request: ComposeEmailRequest = {
      account_id: effectiveAccountId,
      to: toAddresses,
      cc: cc ? recipients.parseRecipients(cc) : undefined,
      bcc: bcc ? recipients.parseRecipients(bcc) : undefined,
      subject,
      body_text: body,
      body_html: bodyHtml,
      attachments: attach.attachments.length > 0 ? attach.attachments : undefined,
      read_receipt: security.requestReadReceipt ? true : undefined,
      in_reply_to: replyTo?.original_message_id ?? undefined,
      references: replyTo?.original_references ?? replyTo?.original_message_id ?? undefined,
    };

    let currentBody = body;

    if (security.pgpEncrypt) {
      try {
        const allRecipients = [
          ...toAddresses,
          ...(cc ? recipients.parseRecipients(cc) : []),
          ...(bcc ? recipients.parseRecipients(bcc) : []),
        ];
        const encrypted = await api.encryptEmailPgp(effectiveAccountId, allRecipients, currentBody);
        currentBody = encrypted;
        request.body_text = currentBody;
        request.body_html = `<pre>${currentBody}</pre>`;

        if (attach.attachments.length > 0) {
          const encryptedAttachments: ComposeAttachment[] = [];
          for (const att of attach.attachments) {
            const plaintextBytes = base64ToUint8Array(att.data);
            const ciphertextBytes = await api.encryptAttachmentPgp(
              effectiveAccountId,
              allRecipients,
              Array.from(plaintextBytes),
            );
            encryptedAttachments.push({
              filename: `${att.filename}.pgp`,
              mime_type: "application/octet-stream",
              data: uint8ArrayToBase64(new Uint8Array(ciphertextBytes)),
            });
          }
          request.attachments = encryptedAttachments;
        }
      } catch (err) {
        const message = err instanceof Error ? err.message : t("compose.pgpEncryptionFailed");
        setSendError(message);
        toast("error", message);
        return;
      }
    }

    if (security.smimeSign) {
      try {
        const signed = await api.signEmailSmime(effectiveAccountId, currentBody);
        currentBody = signed;
        request.body_text = currentBody;
        request.body_html = `<pre>${currentBody}</pre>`;
      } catch (err) {
        const message = err instanceof Error ? err.message : t("compose.smimeSigningFailed");
        setSendError(message);
        toast("error", message);
        return;
      }
    }

    if (security.smimeEncrypt) {
      try {
        const encrypted = await api.encryptEmailSmime(effectiveAccountId, [], currentBody);
        currentBody = encrypted;
        request.body_text = currentBody;
        request.body_html = `<pre>${currentBody}</pre>`;
      } catch (err) {
        const message = err instanceof Error ? err.message : t("compose.smimeEncryptionFailed");
        setSendError(message);
        toast("error", message);
        return;
      }
    }

    if (onSendRequested) {
      onSendRequested(request);
      if (draft.draftId) {
        void api.deleteEmail(draft.draftId).catch(() => {});
      }
      onClose();
      return;
    }

    // Support undo send delay for popout
    if (isPopout && undoSendDelay && undoSendDelay > 0) {
      setPendingSend(request);
      return;
    }

    await performSend(request);
  }

  async function performSend(request: ComposeEmailRequest) {
    try {
      setSending(true);
      setSendError(null);
      await api.sendEmail(request);
      if (draft.draftId) {
        void api.deleteEmail(draft.draftId).catch(() => {});
      }
      toast("success", t("compose.emailSent"));
      void emitMailUpdated("", "send");
      onClose();
    } catch (err) {
      if (isMobile() && !navigator.onLine) {
        try {
          await api.queueEmail({
            account_id: request.account_id,
            to: request.to,
            cc: request.cc,
            bcc: request.bcc,
            subject: request.subject,
            body_text: request.body_text,
            body_html: request.body_html,
          });
          toast("info", t("compose.queuedOffline"));
          if (draft.draftId) {
            void api.deleteEmail(draft.draftId).catch(() => {});
          }
          onClose();
          return;
        } catch {
          // Fall through to normal error
        }
      }
      const message = err instanceof Error ? err.message : t("compose.failedToSend");
      setSendError(message);
      toast("error", message);
    } finally {
      setSending(false);
    }
  }

  async function handleUndoSendComplete() {
    if (!pendingSend) return;
    await performSend(pendingSend);
    setPendingSend(null);
  }

  async function handleSaveDraft() {
    if (!effectiveAccountId) {
      setSendError(t("compose.selectAccountDraft"));
      return;
    }
    try {
      setSending(true);
      setSendError(null);
      const request: ComposeEmailRequest = {
        account_id: effectiveAccountId,
        to: recipients.parseRecipients(to),
        cc: cc ? recipients.parseRecipients(cc) : undefined,
        bcc: bcc ? recipients.parseRecipients(bcc) : undefined,
        subject,
        body_text: body,
        body_html: bodyHtml,
        attachments: attach.attachments.length > 0 ? attach.attachments : undefined,
      };
      if (draft.draftId) {
        await api.updateDraft(draft.draftId, request);
      } else {
        await api.saveDraft(request);
      }
      onClose();
    } catch (err) {
      const message = err instanceof Error ? err.message : t("compose.failedToSaveDraft");
      setSendError(message);
      toast("error", message);
    } finally {
      setSending(false);
    }
  }

  async function handleDiscard() {
    if (!draft.isDirtyRef.current) {
      onClose();
      return;
    }
    const confirmed = await confirm({
      title: t("compose.discardDraft"),
      message: t("compose.discardWarning"),
      confirmLabel: t("compose.discard"),
      danger: true,
    });
    if (confirmed) {
      if (draft.draftId) {
        void api.deleteEmail(draft.draftId).catch(() => {});
      }
      onClose();
    }
  }

  async function handleScheduleSend(scheduledAt: string) {
    const toAddresses = recipients.parseRecipients(to);
    if (toAddresses.length === 0) {
      setSendError(t("compose.recipientRequired"));
      return;
    }
    if (!effectiveAccountId) {
      setSendError(t("compose.selectAccountRequired"));
      return;
    }
    try {
      setSending(true);
      setSendError(null);
      const request: SendLaterRequest = {
        account_id: effectiveAccountId,
        to: toAddresses,
        cc: cc ? recipients.parseRecipients(cc) : undefined,
        bcc: bcc ? recipients.parseRecipients(bcc) : undefined,
        subject,
        body_text: body,
        body_html: bodyHtml,
        attachments: attach.attachments.length > 0 ? attach.attachments : undefined,
        scheduled_at: scheduledAt,
        read_receipt: security.requestReadReceipt ? true : undefined,
        in_reply_to: replyTo?.original_message_id ?? undefined,
        references: replyTo?.original_references ?? replyTo?.original_message_id ?? undefined,
      };
      await api.scheduleSend(request);
      if (draft.draftId) {
        void api.deleteEmail(draft.draftId).catch(() => {});
      }
      toast("success", t("compose.emailScheduled"));
      onClose();
    } catch (err) {
      const message = err instanceof Error ? err.message : t("compose.failedToSchedule");
      setSendError(message);
      toast("error", message);
    } finally {
      setSending(false);
    }
  }

  const CcBccIcon = showCcBcc ? ChevronUp : ChevronDown;

  function handleFileSelected(e: React.ChangeEvent<HTMLInputElement>) {
    const files = e.target.files;
    if (!files || files.length === 0) return;
    void attach.handleFileSelected(files);
  }

  return (
    <div
      className={cn(
        "flex flex-1 flex-col overflow-hidden bg-background text-foreground",
        fullscreen && !isPopout && "h-screen w-screen",
        isPopout && "h-full w-full"
      )}
    >
      {/* Header */}
      <div className="flex shrink-0 items-center justify-between border-b border-border px-4 py-3">
        <h2 className="text-base font-semibold text-foreground">
          {replyTo
            ? replyTo.reply_all
              ? t("compose.replyAll")
              : t("compose.reply")
            : forwardFrom
            ? t("compose.forward")
            : t("compose.newMessage")}
        </h2>
        <div className="flex items-center gap-2">
          {!isPopout && onToggleFullscreen && (
            <button
              onClick={onToggleFullscreen}
              className="rounded-lg p-1.5 text-muted-foreground transition-colors hover:bg-card hover:text-foreground"
              title={fullscreen ? t("compose.exitFullscreen") : t("compose.fullscreen")}
              aria-label={fullscreen ? t("compose.exitFullscreen") : t("compose.fullscreen")}
              aria-pressed={fullscreen}
              type="button"
            >
              {fullscreen ? <Minimize2 size={16} /> : <Maximize2 size={16} />}
            </button>
          )}
          <button
            onClick={() => void handleDiscard()}
            disabled={sending}
            className="rounded-lg p-1.5 text-muted-foreground transition-colors hover:bg-card hover:text-foreground"
            aria-label={t("common.close")}
            title={t("common.close")}
          >
            <X size={18} />
          </button>
        </div>
      </div>

      {/* Main Compose Area */}
      <div
        className="flex-1 overflow-y-auto px-4 py-3 space-y-4"
        style={{
          paddingBottom: mobile && keyboardVisible ? `${keyboardHeight + 16}px` : "16px",
        }}
      >
        {sendError && (
          <div
            id={sendErrorId}
            role="alert"
            className="rounded-lg border border-red-500/30 bg-red-500/10 p-3 text-xs text-red-500"
          >
            {sendError}
          </div>
        )}



        {/* Sender Account Selector */}
        {(!accountId && !sourceAccountId && accounts.length > 1) ? (
          <div className="flex items-center gap-2 border-b border-border/60 pb-2.5">
            <label htmlFor={fromInputId} className="w-12 text-xs font-medium text-muted-foreground">
              {t("compose.from")}
            </label>
            <select
              id={fromInputId}
              value={selectedAccountId}
              onChange={(e) => setSelectedAccountId(e.target.value)}
              className="flex-1 bg-transparent text-xs text-foreground focus:outline-none"
            >
              <option value="" disabled>
                {t("compose.selectAccount")}
              </option>
              {accounts.map((acc) => (
                <option key={acc.id} value={acc.id} className="bg-background text-foreground">
                  {acc.display_name ? `${acc.display_name} (${acc.email})` : acc.email}
                </option>
              ))}
            </select>
          </div>
        ) : (
          accounts.length > 0 && (
            <div className="flex items-center gap-2 border-b border-border/60 pb-2.5 text-xs text-muted-foreground">
              <span className="w-12 font-medium">{t("compose.from")}</span>
              <span className="flex-1 truncate">
                {(() => {
                  const acc = accounts.find((a) => a.id === effectiveAccountId);
                  return acc
                    ? acc.display_name
                      ? `${acc.display_name} (${acc.email})`
                      : acc.email
                    : accountEmail || "";
                })()}
              </span>
            </div>
          )
        )}

        {/* Recipients (To) */}
        <div className="flex items-start gap-2 border-b border-border/60 pb-2">
          <label htmlFor={toInputId} className="w-12 pt-2 text-xs font-medium text-muted-foreground">
            {t("compose.to")}
          </label>
          <div className="flex-1 min-w-0">
            <ContactAutocomplete
              id={toInputId}
              value={to}
              onChange={setTo}
              placeholder={t("compose.toPlaceholder")}
              accountId={effectiveAccountId}
              autoFocus={!replyTo}
            />
          </div>
          <button
            onClick={() => setShowCcBcc(!showCcBcc)}
            className="rounded-lg p-1.5 text-muted-foreground transition-colors hover:bg-card hover:text-foreground shrink-0 mt-0.5"
            aria-label={t("compose.toggleCcBcc")}
            aria-expanded={showCcBcc}
          >
            <CcBccIcon size={16} />
          </button>
        </div>

        {/* CC / BCC fields */}
        {showCcBcc && (
          <div className="space-y-3 border-b border-border/60 pb-3 animate-slide-down">
            <div className="flex items-start gap-2">
              <label htmlFor={ccInputId} className="w-12 pt-2 text-xs font-medium text-muted-foreground">
                {t("compose.cc")}
              </label>
              <div className="flex-1 min-w-0">
                <ContactAutocomplete
                  id={ccInputId}
                  value={cc}
                  onChange={setCc}
                  placeholder={t("compose.ccPlaceholder")}
                  accountId={effectiveAccountId}
                />
              </div>
            </div>
            <div className="flex items-start gap-2">
              <label htmlFor={bccInputId} className="w-12 pt-2 text-xs font-medium text-muted-foreground">
                {t("compose.bcc")}
              </label>
              <div className="flex-1 min-w-0">
                <ContactAutocomplete
                  id={bccInputId}
                  value={bcc}
                  onChange={setBcc}
                  placeholder={t("compose.bccPlaceholder")}
                  accountId={effectiveAccountId}
                />
              </div>
            </div>
          </div>
        )}

        {/* Subject */}
        <div className="flex items-center gap-2 border-b border-border/60 pb-2.5">
          <label htmlFor={subjectInputId} className="w-12 text-xs font-medium text-muted-foreground">
            {t("compose.subjectLabel")}
          </label>
          <input
            id={subjectInputId}
            type="text"
            value={subject}
            onChange={(e) => {
              userHasTypedRef.current = true;
              setSubject(e.target.value);
            }}
            placeholder={t("compose.subject")}
            className="flex-1 bg-transparent text-sm text-foreground focus:outline-none"
          />
        </div>

        {/* Rich/Plain Text Editor */}
        <div className="flex-1 min-h-[250px] flex flex-col">
          {editorMode === "rich" ? (
            <RichTextEditor
              content={bodyHtml}
              onChange={(html, text) => {
                userHasTypedRef.current = true;
                setBody(text);
                setBodyHtml(html);
              }}
              placeholder={t("compose.writeMessage")}
              ariaLabel={t("a11y.emailBodyEditor")}
              autoFocus={!!replyTo}
            />
          ) : (
            <textarea
              id={messageInputId}
              value={body}
              onChange={(e) => {
                userHasTypedRef.current = true;
                setBody(e.target.value);
                setBodyHtml(plainTextToHtml(e.target.value));
              }}
              placeholder={t("compose.writeMessage")}
              autoFocus={!!replyTo}
              className="w-full flex-1 resize-none bg-transparent px-6 py-4 text-sm text-foreground focus:outline-none placeholder:text-muted-foreground"
            />
          )}
        </div>

        {/* Attachments Section */}
        {attach.attachments.length > 0 && (
          <div className="border-t border-border/60 pt-3">
            <h3 id={attachmentsHeadingId} className="text-xs font-semibold text-muted-foreground mb-2">
              {t("compose.attachments")} ({attach.attachments.length})
            </h3>
            <div className="flex flex-wrap gap-2" role="list" aria-labelledby={attachmentsHeadingId}>
              {attach.attachments.map((file, idx) => (
                <div
                  key={idx}
                  role="listitem"
                  className="flex items-center gap-2 rounded-lg border border-border bg-card/50 px-3 py-1.5 text-xs text-foreground"
                >
                  <Paperclip size={12} className="text-muted-foreground" />
                  <span className="max-w-[150px] truncate">{file.filename}</span>
                  <button
                    onClick={() => attach.removeAttachment(file.filename)}
                    className="ml-1 rounded text-muted-foreground hover:bg-muted hover:text-foreground"
                    title={t("compose.removeAttachment")}
                  >
                    <X size={12} />
                  </button>
                </div>
              ))}
            </div>
          </div>
        )}
      </div>

      {/* Footer / Controls */}
      <div className="flex shrink-0 flex-wrap items-center justify-between gap-2 border-t border-border/70 bg-card/65 px-4 py-3 backdrop-blur-xl">
        <div className="flex flex-wrap items-center gap-1">
          {/* Send Buttons */}
          <button
            onClick={() => void handleSend()}
            disabled={sending || !effectiveAccountId}
            className="flex h-10 items-center gap-1.5 rounded-lg bg-primary px-5 text-sm font-semibold text-primary-foreground shadow-sm transition-all hover:bg-primary/90 focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-primary focus-visible:ring-offset-2 disabled:opacity-50"
          >
            {sending ? <Loader2 size={14} className="animate-spin" /> : null}
            {t("compose.send")}
          </button>

          <div className="ml-1 min-w-[7rem] text-xs text-muted-foreground" aria-live="polite">
            {draft.draftSavedAt ? `Saved ${draft.draftSavedAt}` : autoSaveDrafts ? "Draft autosaves" : "Not saved"}
          </div>

          <button
            onClick={() => void handleSaveDraft()}
            disabled={sending || !effectiveAccountId}
            className="rounded-lg px-3 py-2 text-sm font-medium text-muted-foreground transition-colors hover:bg-background hover:text-foreground focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-primary disabled:opacity-50"
          >
            {t("compose.saveDraft")}
          </button>

          <button
            onClick={() => setShowSchedule(!showSchedule)}
            disabled={sending || !effectiveAccountId}
            className="flex h-10 items-center gap-1.5 rounded-lg border border-border/60 bg-background/60 px-3 text-muted-foreground transition-colors hover:bg-background hover:text-foreground disabled:opacity-50"
            aria-label={t("compose.schedule")}
            title={t("compose.schedule")}
          >
            <Clock size={16} />
            <span className="hidden text-xs sm:inline">Schedule</span>
          </button>

          {/* Attach Button */}
          <button
            onClick={() => fileInputRef.current?.click()}
            disabled={sending}
            className="rounded-lg p-2 text-muted-foreground transition-colors hover:bg-card hover:text-foreground"
            title={t("compose.attachFiles")}
          >
            <Paperclip size={16} />
          </button>
          <input
            ref={fileInputRef}
            type="file"
            multiple
            onChange={handleFileSelected}
            className="hidden"
          />

          {/* Template Picker */}
          {templates.length > 0 && (
            <div className="relative" ref={templatePickerRef}>
              <button
                onClick={() => setShowTemplatePicker(!showTemplatePicker)}
                className="rounded-lg p-2 text-muted-foreground transition-colors hover:bg-card hover:text-foreground"
                title={t("compose.insertTemplate")}
                aria-label={t("compose.insertTemplate")}
              >
                <FileText size={16} />
              </button>
              {showTemplatePicker && (
                <div className="absolute bottom-full left-0 z-50 mb-2 w-48 rounded-lg border border-border bg-background py-1 shadow-lg max-h-48 overflow-y-auto">
                  {templates.map((tpl) => (
                    <button
                      key={tpl.id}
                      onClick={() => handleApplyTemplate(tpl)}
                      className="w-full px-3 py-1.5 text-left text-xs text-foreground transition-colors hover:bg-card"
                    >
                      {tpl.name}
                    </button>
                  ))}
                </div>
              )}
            </div>
          )}

          {/* Tone Adjuster */}
          <ToneAdjustButton
            body={body}
            setBody={setBody}
            setBodyHtml={setBodyHtml}
            plainTextToHtml={plainTextToHtml}
          />

          {/* Signatures Selector */}
          {signatures.length > 0 && (
            <select
              value={selectedSignatureId}
              onChange={(e) => handleSignatureChange(e.target.value)}
              className="bg-transparent text-xs text-muted-foreground focus:outline-none py-1 px-1.5 rounded border border-border/40 hover:border-border transition-colors cursor-pointer"
              title={t("compose.selectSignature")}
            >
              <option value="" className="bg-background text-foreground">
                {t("compose.noSignature")}
              </option>
              {signatures.map((sig) => (
                <option key={sig.id} value={sig.id} className="bg-background text-foreground">
                  {sig.name}
                </option>
              ))}
            </select>
          )}

          {/* HTML Toggle */}
          <Tooltip>
            <TooltipTrigger asChild>
              <button
                type="button"
                onClick={() => void handleEditorModeToggle()}
                className="flex items-center gap-1.5 rounded-lg border border-border bg-card/50 px-3 py-2 text-sm text-muted-foreground transition-colors hover:bg-muted hover:text-foreground"
              >
                {editorMode === "rich" ? <FileText size={14} /> : <Code size={14} />}
                {editorMode === "rich" ? t("compose.plainText") : t("compose.richText")}
              </button>
            </TooltipTrigger>
            <TooltipContent>
              {editorMode === "rich" ? t("compose.switchToPlainText") : t("compose.switchToRichText")}
            </TooltipContent>
          </Tooltip>
        </div>

        {/* Security Controls */}
        <div className="flex items-center gap-3 shrink-0">
          <Tooltip>
            <TooltipTrigger asChild>
              <label className="flex items-center gap-1.5 text-xs text-muted-foreground cursor-pointer select-none">
                <input
                  type="checkbox"
                  checked={security.requestReadReceipt}
                  onChange={(e) => security.setRequestReadReceipt(e.target.checked)}
                  className="rounded border-border bg-card text-primary focus:ring-primary h-3.5 w-3.5"
                />
                {t("compose.readReceipt")}
              </label>
            </TooltipTrigger>
            <TooltipContent>{t("compose.readReceiptTooltip")}</TooltipContent>
          </Tooltip>

          <Tooltip>
            <TooltipTrigger asChild>
              <button
                type="button"
                onClick={() => security.togglePgpEncrypt(!security.pgpEncrypt)}
                aria-pressed={security.pgpEncrypt}
                className={cn(
                  "flex items-center gap-1.5 rounded-lg px-3 py-1.5 text-xs transition-colors focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-primary",
                  security.pgpEncrypt
                    ? "bg-primary/10 text-primary"
                    : "text-muted-foreground hover:bg-card hover:text-foreground"
                )}
              >
                <Lock size={14} />
                {t("compose.pgp")}
              </button>
            </TooltipTrigger>
            <TooltipContent>{t("compose.pgpTooltip")}</TooltipContent>
          </Tooltip>

          <Tooltip>
            <TooltipTrigger asChild>
              <button
                type="button"
                onClick={() => security.toggleSmimeSign(!security.smimeSign)}
                aria-pressed={security.smimeSign}
                className={cn(
                  "flex items-center gap-1.5 rounded-lg px-3 py-1.5 text-xs transition-colors focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-primary",
                  security.smimeSign
                    ? "bg-primary/10 text-primary"
                    : "text-muted-foreground hover:bg-card hover:text-foreground"
                )}
              >
                <Shield size={14} />
                {t("compose.smimeSign")}
              </button>
            </TooltipTrigger>
            <TooltipContent>{t("compose.smimeSignTooltip")}</TooltipContent>
          </Tooltip>

          <Tooltip>
            <TooltipTrigger asChild>
              <button
                type="button"
                onClick={() => security.toggleSmimeEncrypt(!security.smimeEncrypt)}
                aria-pressed={security.smimeEncrypt}
                className={cn(
                  "flex items-center gap-1.5 rounded-lg px-3 py-1.5 text-xs transition-colors focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-primary",
                  security.smimeEncrypt
                    ? "bg-primary/10 text-primary"
                    : "text-muted-foreground hover:bg-card hover:text-foreground"
                )}
              >
                <Lock size={14} />
                {t("compose.smimeEncrypt")}
              </button>
            </TooltipTrigger>
            <TooltipContent>{t("compose.smimeEncryptTooltip")}</TooltipContent>
          </Tooltip>

          <button
            onClick={() => void handleDiscard()}
            className="rounded-lg px-4 py-2 text-sm text-muted-foreground transition-colors hover:bg-card hover:text-muted-foreground focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-primary"
          >
            {t("compose.discard")}
          </button>
        </div>
      </div>

      {showSchedule && (
        <SchedulePicker
          isOpen={showSchedule}
          onSchedule={handleScheduleSend}
          onClose={() => setShowSchedule(false)}
        />
      )}

      {pendingSend && (
        <UndoSendToast
          delaySeconds={undoSendDelay ?? 5}
          onUndo={() => { setPendingSend(null); toast("success", t("compose.sendCancelled")); }}
          onComplete={handleUndoSendComplete}
        />
      )}
    </div>
  );
}
