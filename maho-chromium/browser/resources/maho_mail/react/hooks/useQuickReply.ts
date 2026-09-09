import { useState, useRef, useCallback, useEffect, useMemo } from "react";
import * as api from "../api";
import type { Email, EmailDetail, ComposeEmailRequest, SendRequestedOptions } from "../types";
import { parseRecipientsFn } from "./useComposeRecipients";

type ToastFn = (type: "success" | "error", message: string) => void;

interface UseQuickReplyOptions {
  email: Email | null;
  emailDetail: EmailDetail | null;
  accountEmail?: string;
  onSendRequested?: (request: ComposeEmailRequest, options?: SendRequestedOptions) => void;
  toast: ToastFn;
  t: (key: string) => string;
}

interface UseQuickReplyResult {
  quickReplyText: string;
  setQuickReplyText: (text: string) => void;
  quickReplyExpanded: boolean;
  setQuickReplyExpanded: (expanded: boolean) => void;
  quickReplyAll: boolean;
  setQuickReplyAll: (all: boolean) => void;
  quickReplySending: boolean;
  quickReplyRef: React.RefObject<HTMLTextAreaElement | null>;
  handleQuickReply: (replyAll: boolean) => Promise<void>;
  resetQuickReply: () => void;
}

export function useQuickReply({
  email,
  accountEmail,
  onSendRequested,
  toast,
  t,
}: UseQuickReplyOptions): UseQuickReplyResult {
  const [quickReplyText, setQuickReplyText] = useState("");
  const [quickReplySending, setQuickReplySending] = useState(false);
  const [quickReplyExpanded, setQuickReplyExpanded] = useState(false);
  const [quickReplyAll, setQuickReplyAll] = useState(false);
  const quickReplyRef = useRef<HTMLTextAreaElement | null>(null);

  const autoResizeTextarea = useCallback(() => {
    const el = quickReplyRef.current;
    if (!el) return;
    el.style.height = "auto";
    el.style.height = `${Math.min(el.scrollHeight, 200)}px`;
  }, []);

  useEffect(() => {
    autoResizeTextarea();
  }, [quickReplyText, autoResizeTextarea]);

  const handleQuickReply = useCallback(
    async (replyAll: boolean) => {
      if (!email || !quickReplyText.trim()) return;

      const toAddresses = [email.from_address];
      const ccAddresses: string[] = [];

      if (replyAll) {
        const allTo = parseRecipientsFn(email.to_addresses);
        for (const addr of allTo) {
          if (!toAddresses.includes(addr)) toAddresses.push(addr);
        }
        if (email.cc_addresses) {
          const allCc = parseRecipientsFn(email.cc_addresses);
          for (const addr of allCc) {
            if (!toAddresses.includes(addr)) ccAddresses.push(addr);
          }
        }
      }

      const isSelf = (address: string) => (address.match(/<([^>]+)>/)?.[1] ?? address).trim().toLowerCase() === accountEmail?.toLowerCase();
      let context;
      try {
        context = await api.getReplyContext(email.id);
      } catch (error) {
        toast("error", error instanceof Error ? error.message : t("email.quickReplyFailed"));
        return;
      }
      const messageId = context.original_message_id ?? email.message_id;
      const references = [...new Set([...(context.original_references?.split(/\s+/).filter(Boolean) ?? []), ...(messageId ? [messageId] : [])])].join(" ");
      const request: ComposeEmailRequest = {
        account_id: email.account_id,
        to: (() => { const filtered = toAddresses.filter(address => !isSelf(address)); return filtered.length > 0 ? filtered : toAddresses; })(),
        cc: ccAddresses.length > 0 ? ccAddresses.filter(address => !isSelf(address)) : undefined,
        subject: email.subject.startsWith("Re:") ? email.subject : `Re: ${email.subject}`,
        body_text: quickReplyText,
        in_reply_to: messageId || undefined,
        references: references || undefined,
      };

      if (onSendRequested) {
        onSendRequested(request, {
          composition: {
            accountId: email.account_id,
            to: request.to.join(", "),
            cc: request.cc?.join(", "),
            subject: request.subject,
            body: quickReplyText,
            inReplyTo: request.in_reply_to,
            references: request.references,
          },
        });
        setQuickReplyText("");
        setQuickReplyExpanded(false);
        return;
      }

      try {
        setQuickReplySending(true);
        await api.sendEmail(request);
        setQuickReplyText("");
        setQuickReplyExpanded(false);
        toast("success", t("email.quickReplySent"));
      } catch (err) {
        toast("error", err instanceof Error ? err.message : t("email.quickReplyFailed"));
      } finally {
        setQuickReplySending(false);
      }
    },
    [email, accountEmail, quickReplyText, onSendRequested, toast, t],
  );

  const resetQuickReply = useCallback(() => {
    setQuickReplyText("");
    setQuickReplyExpanded(false);
    setQuickReplyAll(false);
  }, []);

  return useMemo(
    () => ({
      quickReplyText,
      setQuickReplyText,
      quickReplyExpanded,
      setQuickReplyExpanded,
      quickReplyAll,
      setQuickReplyAll,
      quickReplySending,
      quickReplyRef,
      handleQuickReply,
      resetQuickReply,
    }),
    [
      quickReplyText,
      quickReplyExpanded,
      quickReplyAll,
      quickReplySending,
      handleQuickReply,
      resetQuickReply,
    ],
  );
}
