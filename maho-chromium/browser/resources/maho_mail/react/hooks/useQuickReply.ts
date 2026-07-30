import { useState, useRef, useCallback, useEffect, useMemo } from "react";
import * as api from "../api";
import type { Email, EmailDetail, ComposeEmailRequest } from "../types";

type ToastFn = (type: "success" | "error", message: string) => void;

interface UseQuickReplyOptions {
  email: Email | null;
  emailDetail: EmailDetail | null;
  onSendRequested?: (request: ComposeEmailRequest) => void;
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
        const allTo = email.to_addresses
          .split(",")
          .map((a) => a.trim())
          .filter(Boolean);
        for (const addr of allTo) {
          if (!toAddresses.includes(addr)) toAddresses.push(addr);
        }
        if (email.cc_addresses) {
          const allCc = email.cc_addresses
            .split(",")
            .map((a) => a.trim())
            .filter(Boolean);
          for (const addr of allCc) {
            if (!toAddresses.includes(addr)) ccAddresses.push(addr);
          }
        }
      }

      const request: ComposeEmailRequest = {
        account_id: email.account_id,
        to: toAddresses,
        cc: ccAddresses.length > 0 ? ccAddresses : undefined,
        subject: email.subject.startsWith("Re:") ? email.subject : `Re: ${email.subject}`,
        body_text: quickReplyText,
      };

      if (onSendRequested) {
        onSendRequested(request);
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
    [email, quickReplyText, onSendRequested, toast, t],
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
