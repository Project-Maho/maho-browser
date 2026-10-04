import { useState, useEffect, useCallback, useMemo, type MutableRefObject } from "react";
import { isValidEmail } from "../utils/email";
import type { ReplyContext } from "../types";

export interface UseComposeRecipientsParams {
  initialTo?: string;
  initialCc?: string;
  initialBcc?: string;
  replyTo?: ReplyContext;
  forwardFrom?: ReplyContext;
  effectiveAccountEmail?: string;
  isOpen: boolean;
  prevIsOpenRef: MutableRefObject<boolean>;
}

export interface UseComposeRecipientsReturn {
  to: string;
  setTo: (value: string) => void;
  cc: string;
  setCc: (value: string) => void;
  bcc: string;
  setBcc: (value: string) => void;
  showCcBcc: boolean;
  setShowCcBcc: (value: boolean) => void;
  parseRecipients: (value: string) => string[];
  validateRecipients: () => { invalid: string[] };
  hasInvalidEmails: boolean;
}

function buildInitialTo(
  replyTo?: ReplyContext,
  accountEmail?: string,
  initialTo?: string,
): string {
  if (initialTo) return initialTo;
  if (!replyTo) return "";
  if (!replyTo.reply_all) return replyTo.original_from;

  const recipients = new Set<string>();
  recipients.add(replyTo.original_from);
  if (replyTo.original_to_addresses) {
    replyTo.original_to_addresses
      .split(",")
      .map((s) => s.trim())
      .filter(Boolean)
      .forEach((recipient) => recipients.add(recipient));
  }

  if (accountEmail) {
    const selfLower = accountEmail.toLowerCase();
    for (const r of recipients) {
      if (r.toLowerCase() === selfLower) recipients.delete(r);
    }
  }
  return Array.from(recipients).join(", ");
}

function buildInitialCc(replyTo?: ReplyContext, accountEmail?: string): string {
  if (!replyTo?.reply_all || !replyTo.original_cc_addresses) return "";

  return replyTo.original_cc_addresses
    .split(",")
    .map((s) => s.trim())
    .filter(Boolean)
    .filter((recipient) => recipient.toLowerCase() !== accountEmail?.toLowerCase())
    .join(", ");
}

/**
 * Parse a recipient string into individual addresses.
 * Splits on comma or semicolon, trims whitespace, filters empty entries.
 */
export function parseRecipientsFn(value: string): string[] {
  const recipients: string[] = [];
  let start = 0;
  let quoted = false;
  let escaped = false;
  let angle = false;
  for (let i = 0; i < value.length; i++) {
    const char = value[i];
    if (escaped) { escaped = false; continue; }
    if (quoted && char === "\\") { escaped = true; continue; }
    if (char === '"') quoted = !quoted;
    if (!quoted) {
      if (char === "<") angle = true;
      if (char === ">") angle = false;
      if (!angle && (char === "," || char === ";")) {
        const token = value.slice(start, i).trim();
        if (token) recipients.push(token);
        start = i + 1;
      }
    }
  }
  const token = value.slice(start).trim();
  if (token) recipients.push(token);
  return recipients;
}

export function useComposeRecipients({
  initialTo,
  initialCc,
  initialBcc,
  replyTo,
  forwardFrom,
  effectiveAccountEmail,
  isOpen,
  prevIsOpenRef,
}: UseComposeRecipientsParams): UseComposeRecipientsReturn {
  const [to, setTo] = useState("");
  const [cc, setCc] = useState("");
  const [bcc, setBcc] = useState("");
  const [showCcBcc, setShowCcBcc] = useState(false);

  // Seed recipient fields on first-open edge transition (false→true)
  useEffect(() => {
    if (!isOpen) return;

    const isFirstOpen = !prevIsOpenRef.current;
    if (!isFirstOpen) return;

    // Forward: leave recipients empty (user fills them)
    if (forwardFrom && !replyTo && !initialTo) {
      setTo("");
      setCc("");
      setBcc("");
      setShowCcBcc(false);
      return;
    }

    const computedTo = buildInitialTo(replyTo, effectiveAccountEmail, initialTo);
    const computedCc = initialCc ?? buildInitialCc(replyTo, effectiveAccountEmail);
    const computedBcc = initialBcc ?? "";

    setTo(computedTo);
    setCc(computedCc);
    setBcc(computedBcc);
    setShowCcBcc(
      !!initialCc || !!initialBcc || !!buildInitialCc(replyTo, effectiveAccountEmail),
    );
  }, [isOpen, replyTo, forwardFrom, initialTo, initialCc, initialBcc, effectiveAccountEmail]);

  const parseRecipients = useCallback((value: string): string[] => {
    return parseRecipientsFn(value);
  }, []);

  const validateRecipients = useCallback((): { invalid: string[] } => {
    const allAddresses = [
      ...parseRecipientsFn(to),
      ...parseRecipientsFn(cc),
      ...parseRecipientsFn(bcc),
    ];
    const invalid = allAddresses.filter((addr) => !isValidEmail(addr));
    return { invalid };
  }, [to, cc, bcc]);

  const hasInvalidEmails = useMemo(() => {
    const allAddresses = [
      ...parseRecipientsFn(to),
      ...parseRecipientsFn(cc),
      ...parseRecipientsFn(bcc),
    ];
    if (allAddresses.length === 0) return false;
    return allAddresses.some((addr) => !isValidEmail(addr));
  }, [to, cc, bcc]);

  return {
    to,
    setTo,
    cc,
    setCc,
    bcc,
    setBcc,
    showCcBcc,
    setShowCcBcc,
    parseRecipients,
    validateRecipients,
    hasInvalidEmails,
  };
}
