import { useState, useEffect, useRef, useCallback, type MutableRefObject } from "react";
import * as api from "../api";
import type { ComposeEmailRequest, ComposeAttachment } from "../types";
import { useToast } from "../components/ui/Toast";

export interface UseComposeDraftParams {
  isOpen: boolean;
  autoSaveDrafts: boolean;
  effectiveAccountId: string | null;
  subject: string;
  body: string;
  bodyHtml: string;
  to: string;
  cc: string;
  bcc: string;
  attachments: ComposeAttachment[];
  requestReadReceipt: boolean;
  initialDraftId?: string;
}

export interface UseComposeDraftReturn {
  draftId: string | null;
  setDraftId: (id: string | null) => void;
  draftSavedAt: string | null;
  isDirtyRef: MutableRefObject<boolean>;
  draftIdRef: MutableRefObject<string | null>;
  saveDraft: () => Promise<void>;
}

function parseRecipients(value: string): string[] {
  return value
    .split(",")
    .map((s) => s.trim())
    .filter(Boolean);
}

export function useComposeDraft({
  isOpen,
  autoSaveDrafts,
  effectiveAccountId,
  subject,
  body,
  bodyHtml,
  to,
  cc,
  bcc,
  attachments,
  requestReadReceipt,
  initialDraftId,
}: UseComposeDraftParams): UseComposeDraftReturn {
  const [draftId, setDraftId] = useState<string | null>(initialDraftId ?? null);
  const [draftSavedAt, setDraftSavedAt] = useState<string | null>(null);
  const { toast } = useToast();

  const isDirtyRef = useRef(false);
  const draftIdRef = useRef<string | null>(initialDraftId ?? null);

  // Refs to read current field values in the interval callback without re-creating it
  const toRef = useRef(to);
  const ccRef = useRef(cc);
  const bccRef = useRef(bcc);
  const subjectRef = useRef(subject);
  const bodyRef = useRef(body);
  const bodyHtmlRef = useRef(bodyHtml);
  const attachmentsRef = useRef(attachments);
  const requestReadReceiptRef = useRef(requestReadReceipt);

  // Keep refs synced with latest prop values
  useEffect(() => {
    toRef.current = to;
    ccRef.current = cc;
    bccRef.current = bcc;
    subjectRef.current = subject;
    bodyRef.current = body;
    bodyHtmlRef.current = bodyHtml;
  }, [to, cc, bcc, subject, body, bodyHtml]);

  useEffect(() => {
    attachmentsRef.current = attachments;
  }, [attachments]);

  useEffect(() => {
    requestReadReceiptRef.current = requestReadReceipt;
  }, [requestReadReceipt]);

  // Keep draftIdRef synced with state
  useEffect(() => {
    draftIdRef.current = draftId;
  }, [draftId]);

  // Build request from current ref values
  const buildRequest = useCallback((): ComposeEmailRequest | null => {
    if (!effectiveAccountId) return null;
    return {
      account_id: effectiveAccountId,
      to: parseRecipients(toRef.current),
      cc: ccRef.current ? parseRecipients(ccRef.current) : undefined,
      bcc: bccRef.current ? parseRecipients(bccRef.current) : undefined,
      subject: subjectRef.current,
      body_text: bodyRef.current,
      body_html: bodyHtmlRef.current,
    };
  }, [effectiveAccountId]);

  // Manual save trigger
  const saveDraft = useCallback(async () => {
    const request = buildRequest();
    if (!request) return;

    try {
      if (draftIdRef.current) {
        await api.updateDraft(draftIdRef.current, request);
      } else {
        const newId = await api.saveDraft(request);
        draftIdRef.current = newId;
        setDraftId(newId);
      }
      isDirtyRef.current = false;
      setDraftSavedAt(new Date().toLocaleTimeString());
    } catch {
      // Silent — don't interrupt composition
    }
  }, [buildRequest]);

  // Auto-save interval (30s)
  useEffect(() => {
    if (!isOpen || !autoSaveDrafts) return;

    const timer = setInterval(async () => {
      if (!isDirtyRef.current || !effectiveAccountId) return;

      const request = buildRequest();
      if (!request) return;

      // P1-3 fix: flip dirty flag BEFORE the await so keystrokes during
      // in-flight save re-set it and are not lost.
      isDirtyRef.current = false;

      try {
        if (draftIdRef.current) {
          await api.updateDraft(draftIdRef.current, request);
        } else {
          const newId = await api.saveDraft(request);
          draftIdRef.current = newId;
          setDraftId(newId);
        }
        setDraftSavedAt(new Date().toLocaleTimeString());
      } catch (err) {
        // On failure, re-mark dirty so next interval retries
        isDirtyRef.current = true;
        const message = err instanceof Error ? err.message : "Failed to save draft";
        toast("error", `Auto-save failed: ${message}`);
      }
    }, 30_000);

    return () => clearInterval(timer);
  }, [isOpen, autoSaveDrafts, effectiveAccountId, buildRequest]);

  return {
    draftId,
    setDraftId,
    draftSavedAt,
    isDirtyRef,
    draftIdRef,
    saveDraft,
  };
}
