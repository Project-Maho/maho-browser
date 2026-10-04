import { useState, useEffect, useRef, useCallback, type MutableRefObject } from "react";
import * as api from "../api";
import type { ComposeEmailRequest, ComposeAttachment } from "../types";
import { useToast } from "../components/ui/Toast";
import { parseRecipientsFn } from "./useComposeRecipients";

export interface DraftSnapshot {
  draftId: string;
  accountId: string;
  to: string;
  cc: string;
  bcc: string;
  subject: string;
  body: string;
  bodyHtml: string;
  attachments: ComposeAttachment[];
  requestReadReceipt: boolean;
  inReplyTo?: string;
  references?: string;
  version: number;
}

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
  inReplyTo?: string;
  references?: string;
  isReadingAttachments?: boolean;
  onDraftSaved?: (snapshot: DraftSnapshot) => void;
}

export interface UseComposeDraftReturn {
  draftId: string | null;
  setDraftId: (id: string | null) => void;
  draftSavedAt: string | null;
  isDirtyRef: MutableRefObject<boolean>;
  draftIdRef: MutableRefObject<string | null>;
  saveDraft: () => Promise<void>;
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
  inReplyTo,
  references,
  isReadingAttachments = false,
  onDraftSaved,
}: UseComposeDraftParams): UseComposeDraftReturn {
  const [draftId, setDraftId] = useState<string | null>(initialDraftId ?? null);
  const [draftSavedAt, setDraftSavedAt] = useState<string | null>(null);
  const { toast } = useToast();

  const isDirtyRef = useRef(false);
  const draftIdRef = useRef<string | null>(initialDraftId ?? null);
  const isSavingRef = useRef(false);
  const versionRef = useRef(0);
  const onDraftSavedRef = useRef(onDraftSaved);
  onDraftSavedRef.current = onDraftSaved;

  // Refs to read current field values in the interval callback without re-creating it
  const toRef = useRef(to);
  const ccRef = useRef(cc);
  const bccRef = useRef(bcc);
  const subjectRef = useRef(subject);
  const bodyRef = useRef(body);
  const bodyHtmlRef = useRef(bodyHtml);
  const attachmentsRef = useRef(attachments);
  const requestReadReceiptRef = useRef(requestReadReceipt);
  const inReplyToRef = useRef(inReplyTo);
  const referencesRef = useRef(references);
  const isReadingRef = useRef(isReadingAttachments);

  // Keep refs synced with latest prop values and bump version
  useEffect(() => {
    toRef.current = to;
    ccRef.current = cc;
    bccRef.current = bcc;
    subjectRef.current = subject;
    bodyRef.current = body;
    bodyHtmlRef.current = bodyHtml;
    versionRef.current += 1;
  }, [to, cc, bcc, subject, body, bodyHtml]);

  useEffect(() => {
    attachmentsRef.current = attachments;
    versionRef.current += 1;
  }, [attachments]);

  useEffect(() => {
    requestReadReceiptRef.current = requestReadReceipt;
    versionRef.current += 1;
  }, [requestReadReceipt]);

  useEffect(() => {
    inReplyToRef.current = inReplyTo;
    referencesRef.current = references;
  }, [inReplyTo, references]);

  useEffect(() => {
    isReadingRef.current = isReadingAttachments;
  }, [isReadingAttachments]);

  // Keep draftIdRef synced with state
  useEffect(() => {
    draftIdRef.current = draftId;
  }, [draftId]);

  // Build request from current ref values
  const buildRequest = useCallback((): { request: ComposeEmailRequest; snapshot: DraftSnapshot } | null => {
    if (!effectiveAccountId) return null;
    const currentVersion = versionRef.current;
    const request: ComposeEmailRequest = {
      account_id: effectiveAccountId,
      to: parseRecipientsFn(toRef.current),
      cc: ccRef.current ? parseRecipientsFn(ccRef.current) : undefined,
      bcc: bccRef.current ? parseRecipientsFn(bccRef.current) : undefined,
      subject: subjectRef.current,
      body_text: bodyRef.current,
      body_html: bodyHtmlRef.current,
      attachments: attachmentsRef.current.length > 0 ? attachmentsRef.current : undefined,
      read_receipt: requestReadReceiptRef.current ? true : undefined,
      in_reply_to: inReplyToRef.current,
      references: referencesRef.current,
    };
    const snapshot: DraftSnapshot = {
      draftId: draftIdRef.current ?? "",
      accountId: effectiveAccountId,
      to: toRef.current,
      cc: ccRef.current,
      bcc: bccRef.current,
      subject: subjectRef.current,
      body: bodyRef.current,
      bodyHtml: bodyHtmlRef.current,
      attachments: attachmentsRef.current,
      requestReadReceipt: requestReadReceiptRef.current,
      inReplyTo: inReplyToRef.current,
      references: referencesRef.current,
      version: currentVersion,
    };
    return { request, snapshot };
  }, [effectiveAccountId]);

  // Manual save trigger
  const saveDraft = useCallback(async () => {
    if (isSavingRef.current || isReadingRef.current) return;
    const data = buildRequest();
    if (!data) return;

    isSavingRef.current = true;
    const saveVersion = data.snapshot.version;
    isDirtyRef.current = false;

    try {
      let savedId = draftIdRef.current;
      if (savedId) {
        await api.updateDraft(savedId, data.request);
      } else {
        const newId = await api.saveDraft(data.request);
        draftIdRef.current = newId;
        savedId = newId;
        setDraftId(newId);
      }
      setDraftSavedAt(new Date().toLocaleTimeString());

      if (isDirtyRef.current || versionRef.current !== saveVersion) {
        isDirtyRef.current = true;
      } else {
        isDirtyRef.current = false;
      }

      onDraftSavedRef.current?.({
        ...data.snapshot,
        draftId: savedId ?? "",
      });
    } catch {
      // Silent — don't interrupt composition
    } finally {
      isSavingRef.current = false;
    }
  }, [buildRequest]);

  // Auto-save interval (30s)
  useEffect(() => {
    if (!isOpen || !autoSaveDrafts) return;

    const timer = setInterval(async () => {
      if (!isDirtyRef.current || !effectiveAccountId || isSavingRef.current || isReadingRef.current) return;

      const data = buildRequest();
      if (!data) return;

      isSavingRef.current = true;
      const saveVersion = data.snapshot.version;
      isDirtyRef.current = false;

      try {
        let savedId = draftIdRef.current;
        if (savedId) {
          await api.updateDraft(savedId, data.request);
        } else {
          const newId = await api.saveDraft(data.request);
          draftIdRef.current = newId;
          savedId = newId;
          setDraftId(newId);
        }
        setDraftSavedAt(new Date().toLocaleTimeString());

        if (isDirtyRef.current || versionRef.current !== saveVersion) {
          isDirtyRef.current = true;
        } else {
          isDirtyRef.current = false;
        }

        onDraftSavedRef.current?.({
          ...data.snapshot,
          draftId: savedId ?? "",
        });
      } catch (err) {
        // On failure, re-mark dirty so next interval retries
        isDirtyRef.current = true;
        const message = err instanceof Error ? err.message : "Failed to save draft";
        toast("error", `Auto-save failed: ${message}`);
      } finally {
        isSavingRef.current = false;
      }
    }, 30_000);

    return () => clearInterval(timer);
  }, [isOpen, autoSaveDrafts, effectiveAccountId, buildRequest, toast]);

  return {
    draftId,
    setDraftId,
    draftSavedAt,
    isDirtyRef,
    draftIdRef,
    saveDraft,
  };
}
