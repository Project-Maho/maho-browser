import { useEffect, useState, useRef, useCallback } from "react";
import {
  Reply,
  Forward,
  Trash2,
  Star,
  Loader2,
  Send,
} from "lucide-react";
import * as api from "../../api";
import type { EmailDetail, ComposeEmailRequest } from "../../types";
import { useToast } from "../ui/Toast";
import { Avatar } from "../ui/Avatar";
import { EmailDetailSkeleton } from "../ui/Skeleton";
import { formatRelativeDate } from "../ui/utils";
import {
  useMailUpdatedListener,
  usePopoutWindow,
  emitMailUpdated,
  closeSelf,
} from "../../hooks/usePopoutWindow";
import type { ComposeInitPayload } from "../../events";
import { useSettings } from "../../hooks/useSettings";
import { EmailBodyView } from "../layout/EmailBodyView";
import { UndoSendToast } from "../common/UndoSendToast";

interface ReaderWindowProps {
  emailId: string;
}

export function ReaderWindow({ emailId }: ReaderWindowProps) {
  const { toast } = useToast();
  const { openCompose } = usePopoutWindow();
  const { undoSendDelay } = useSettings();
  const [emailDetail, setEmailDetail] = useState<EmailDetail | null>(null);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);

  const [quickReplyText, setQuickReplyText] = useState("");
  const [quickReplySending, setQuickReplySending] = useState(false);
  const [pendingSend, setPendingSend] = useState<ComposeEmailRequest | null>(null);
  const quickReplyRef = useRef<HTMLTextAreaElement>(null);
  const pendingSendRef = useRef<ComposeEmailRequest | null>(null);
  const emailDetailRef = useRef<EmailDetail | null>(null);

  useEffect(() => {
    pendingSendRef.current = pendingSend;
  }, [pendingSend]);

  useEffect(() => {
    emailDetailRef.current = emailDetail;
  }, [emailDetail]);

  useEffect(() => {
    const flushPendingSend = () => {
      const toSend = pendingSendRef.current;
      if (!toSend) return;
      pendingSendRef.current = null;
      setPendingSend(null);
      void api.sendEmail(toSend).catch((err) => {
        console.error("Failed to flush pending email on close:", err);
      });
      if (emailDetailRef.current) {
        void emitMailUpdated(emailDetailRef.current.email.id, "send");
      }
    };

    window.addEventListener("beforeunload", flushPendingSend);
    return () => {
      window.removeEventListener("beforeunload", flushPendingSend);
      flushPendingSend();
    };
  }, []);

  const autoResizeTextarea = useCallback(() => {
    const el = quickReplyRef.current;
    if (!el) return;
    el.style.height = "auto";
    el.style.height = `${Math.min(el.scrollHeight, 200)}px`;
  }, []);

  useEffect(() => {
    autoResizeTextarea();
  }, [quickReplyText, autoResizeTextarea]);

  const fetchEmail = useCallback(async () => {
    try {
      setLoading(true);
      setError(null);
      const detail = await api.getEmail(emailId);
      setEmailDetail(detail);
      if (!detail.email.is_read) {
        await api.markRead(emailId);
        void emitMailUpdated(emailId, "markRead");
      }
    } catch (err) {
      setError(err instanceof Error ? err.message : "Failed to load email");
    } finally {
      setLoading(false);
    }
  }, [emailId]);

  useEffect(() => {
    void fetchEmail();
  }, [fetchEmail]);

  useMailUpdatedListener((payload) => {
    if (payload.emailId === emailId) {
      void fetchEmail();
    }
  });

  const handleReply = async () => {
    if (!emailDetail) return;
    const { email } = emailDetail;
    try {
      const ctx = await api.getReplyContext(email.id);
      const payload: ComposeInitPayload = {
        to: ctx.original_from,
        cc: "",
        bcc: "",
        subject: ctx.original_subject.startsWith("Re:")
          ? ctx.original_subject
          : `Re: ${ctx.original_subject}`,
        body: "",
        bodyHtml: "",
        accountId: email.account_id,
        replyTo: ctx,
      };
      void openCompose(payload);
    } catch (err) {
      toast("error", err instanceof Error ? err.message : "Failed to open reply");
    }
  };

  const handleForward = async () => {
    if (!emailDetail) return;
    const { email } = emailDetail;
    try {
      const ctx = await api.getReplyContext(email.id);
      const payload: ComposeInitPayload = {
        to: "",
        cc: "",
        bcc: "",
        subject: ctx.original_subject.startsWith("Fwd:")
          ? ctx.original_subject
          : `Fwd: ${ctx.original_subject}`,
        body: ctx.original_body_text ?? "",
        bodyHtml: ctx.original_body_html ?? "",
        accountId: email.account_id,
        forwardFrom: ctx,
        forwardEmailUid: email.uid,
        forwardFolderId: email.folder_id,
      };
      void openCompose(payload as any);
    } catch (err) {
      toast("error", err instanceof Error ? err.message : "Failed to forward");
    }
  };

  const handleDelete = async () => {
    if (!emailDetail) return;
    try {
      await api.deleteEmail(emailDetail.email.id);
      void emitMailUpdated(emailDetail.email.id, "delete");
      closeSelf();
    } catch (err) {
      toast("error", err instanceof Error ? err.message : "Failed to delete email");
    }
  };

  const handleToggleStar = async () => {
    if (!emailDetail) return;
    try {
      await api.toggleStar(emailDetail.email.id);
      setEmailDetail((prev) =>
        prev
          ? { ...prev, email: { ...prev.email, is_starred: !prev.email.is_starred } }
          : null,
      );
      void emitMailUpdated(emailDetail.email.id, "toggleStar");
    } catch (err) {
      toast("error", err instanceof Error ? err.message : "Failed to update star");
    }
  };

  const handleQuickReply = async () => {
    if (!quickReplyText.trim() || !emailDetail) return;
    const { email } = emailDetail;
    const request: ComposeEmailRequest = {
      account_id: email.account_id,
      to: [email.from_address],
      subject: email.subject.startsWith("Re:") ? email.subject : `Re: ${email.subject}`,
      body_text: quickReplyText,
    };
    pendingSendRef.current = request;
    setPendingSend(request);
  };

  async function handleUndoSendComplete() {
    const toSend = pendingSendRef.current;
    if (!toSend || !emailDetail) return;
    pendingSendRef.current = null;
    try {
      setQuickReplySending(true);
      await api.sendEmail(toSend);
      setQuickReplyText("");
      toast("success", "Reply sent");
      void emitMailUpdated(emailDetail.email.id, "send");
    } catch (err) {
      toast("error", err instanceof Error ? err.message : "Failed to send reply");
    } finally {
      setQuickReplySending(false);
      setPendingSend(null);
    }
  }



  if (loading) {
    return (
      <div className="flex h-screen flex-col bg-background text-foreground">
        <div className="h-8 flex-shrink-0" data-tauri-drag-region />
        <EmailDetailSkeleton />
      </div>
    );
  }

  if (error || !emailDetail) {
    return (
      <div className="flex h-screen flex-col items-center justify-center bg-background text-foreground">
        <div className="absolute inset-x-0 top-0 h-8" data-tauri-drag-region />
        <p className="text-sm text-destructive">{error ?? "Email not found"}</p>
      </div>
    );
  }

  const { email } = emailDetail;

  return (
    <div className="flex h-screen flex-col bg-background text-foreground">
      <div className="h-8 flex-shrink-0" data-tauri-drag-region />

      <div className="border-b border-border px-6 py-5">
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
                <h2 className="truncate text-lg font-semibold text-foreground">
                  {email.subject}
                </h2>
                <div className="mt-1 flex flex-wrap items-center gap-x-2 gap-y-1">
                  <span className="font-medium text-foreground">
                    {email.from_name ?? email.from_address}
                  </span>
                  <span className="text-sm text-muted-foreground">
                    &lt;{email.from_address}&gt;
                  </span>
                  <span className="text-xs text-muted-foreground">
                    {formatRelativeDate(email.date)}
                  </span>
                </div>
                <p className="mt-2 text-sm text-muted-foreground">
                  To: {email.to_addresses}
                  {email.cc_addresses ? ` · Cc: ${email.cc_addresses}` : ""}
                </p>
              </div>
              <button
                type="button"
                onClick={() => void handleToggleStar()}
                className="mail-pressable flex h-9 w-9 items-center justify-center rounded-lg border border-border bg-background/70 text-muted-foreground hover:border-foreground/20 hover:bg-surface-hover hover:text-foreground focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring"
                aria-label={email.is_starred ? "Unstar" : "Star"}
                title={email.is_starred ? "Unstar" : "Star"}
              >
                <Star
                  size={18}
                  className={
                    email.is_starred
                      ? "fill-amber-400 text-amber-400"
                      : "text-muted-foreground"
                  }
                />
              </button>
            </div>
          </div>
        </div>
      </div>
      <EmailBodyView emailDetail={emailDetail} />

      <div className="border-t border-border px-4 py-3">
        <div className="flex items-end gap-2">
          <textarea
            ref={quickReplyRef}
            value={quickReplyText}
            onChange={(e) => setQuickReplyText(e.target.value)}
            placeholder="Quick reply..."
            rows={2}
            className="flex-1 resize-none rounded-lg border border-border bg-background px-3 py-2 text-sm text-foreground placeholder:text-muted-foreground focus:border-ring focus:outline-none"
            onKeyDown={(e) => {
              if (e.key === "Enter" && (e.metaKey || e.ctrlKey)) {
                e.preventDefault();
                void handleQuickReply();
              }
            }}
          />
           <button
            type="button"
            onClick={() => void handleQuickReply()}
            disabled={quickReplySending || !quickReplyText.trim()}
            className="flex h-9 w-9 shrink-0 items-center justify-center mail-pressable rounded-lg bg-primary text-primary-foreground hover:bg-primary/90 disabled:opacity-50"
            aria-label="Send reply"
          >
            {quickReplySending ? (
              <Loader2 size={16} className="animate-spin" />
            ) : (
              <Send size={16} />
            )}
          </button>
        </div>
      </div>

      <div className="sticky bottom-0 z-10 flex items-center gap-2 border-t border-border bg-background p-4">
        <button
          type="button"
          onClick={() => void handleReply()}
          className="mail-pressable flex h-9 items-center gap-2 rounded-lg px-4 text-sm text-foreground hover:bg-surface-hover"
        >
          <Reply size={16} />
          Reply
        </button>
        <button
          type="button"
          onClick={() => void handleForward()}
          className="mail-pressable flex h-9 items-center gap-2 rounded-lg px-4 text-sm text-foreground hover:bg-surface-hover"
        >
          <Forward size={16} />
          Forward
        </button>
        <div className="flex-1" />
        <button
          type="button"
          onClick={() => void handleDelete()}
          className="mail-pressable flex h-9 items-center gap-2 rounded-lg px-4 text-sm text-muted-foreground hover:bg-destructive/10 hover:text-destructive"
        >
          <Trash2 size={16} />
          Delete
        </button>
      </div>

      {pendingSend && (
        <UndoSendToast
          delaySeconds={undoSendDelay ?? 5}
          onUndo={() => {
            pendingSendRef.current = null;
            setPendingSend(null);
            toast("success", "Send cancelled");
          }}
          onComplete={handleUndoSendComplete}
        />
      )}
    </div>
  );
}
