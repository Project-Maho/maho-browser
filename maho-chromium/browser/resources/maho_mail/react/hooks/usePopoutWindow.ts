// Copyright 2026 Maho Browser. All rights reserved.

import { useCallback, useEffect, useRef, useState } from "react";
import { useToast } from "../components/ui/Toast";
import type { ReplyContext } from "../types";

export interface PopoutOptions {
  url: string;
  title: string;
  width?: number;
  height?: number;
}

export interface ComposeInitPayload {
  to?: string;
  cc?: string;
  bcc?: string;
  subject?: string;
  body?: string;
  bodyHtml?: string;
  accountId?: string;
  draftId?: string;
  replyTo?: ReplyContext;
  forwardFrom?: ReplyContext;
}

export type MailUpdateAction =
  | "markRead"
  | "markUnread"
  | "toggleStar"
  | "delete"
  | "move"
  | "send";

export interface MailUpdatedPayload {
  emailId: string;
  action: MailUpdateAction;
  sourceLabel: string;
}

export interface MailRollbackPayload {
  emailId: string;
  action: MailUpdateAction;
  sourceLabel: string;
}

const POPOUT_CHANNEL_NAME = "maho-mail-popout";

/** Identity of this window. Popouts are opened via window.open(url, label), which
 *  assigns the child window's `window.name`, so each popout can recover its label. */
export function getWindowLabel(): string {
  if (typeof window === "undefined") return "main";
  return window.name || "main";
}

export function closeSelf(): void {
  if (typeof window !== "undefined") window.close();
}

interface PopoutMessage {
  event: string;
  payload: unknown;
}

function popoutEmit(event: string, payload: unknown): void {
  if (typeof BroadcastChannel === "undefined") return;
  const channel = new BroadcastChannel(POPOUT_CHANNEL_NAME);
  channel.postMessage({ event, payload } satisfies PopoutMessage);
  channel.close();
}

function popoutListen(
  event: string,
  handler: (payload: unknown) => void,
): () => void {
  if (typeof BroadcastChannel === "undefined") return () => {};
  const channel = new BroadcastChannel(POPOUT_CHANNEL_NAME);
  const onMessage = (e: MessageEvent<PopoutMessage>) => {
    if (e.data && e.data.event === event) handler(e.data.payload);
  };
  channel.addEventListener("message", onMessage);
  return () => {
    channel.removeEventListener("message", onMessage);
    channel.close();
  };
}

const openWindows = new Map<string, Window>();

function popoutUrl(query: string): string {
  const origin = typeof window !== "undefined" ? window.location.origin : "";
  return `${origin}/?${query}`;
}

export async function focusOrCreate(
  label: string,
  options: PopoutOptions,
): Promise<boolean> {
  const existing = openWindows.get(label);
  if (existing && !existing.closed) {
    existing.focus();
    return false;
  }

  const width = options.width ?? 900;
  const height = options.height ?? 700;
  const features = `popup=yes,width=${width},height=${height}`;
  const child = window.open(options.url, label, features);
  if (!child) {
    throw new Error("Popup blocked by the browser.");
  }
  openWindows.set(label, child);
  const checkClosedInterval = setInterval(() => {
    if (child.closed) {
      clearInterval(checkClosedInterval);
      if (openWindows.get(label) === child) {
        openWindows.delete(label);
      }
    }
  }, 1000);
  try {
    child.document.title = options.title;
  } catch {
    // Cross-document title set may fail before load; the popout sets its own title.
  }
  return true;
}

export async function closePopout(label: string): Promise<void> {
  const existing = openWindows.get(label);
  if (existing && !existing.closed) {
    existing.close();
  }
  openWindows.delete(label);
}

export function isPopout(): boolean {
  return typeof window !== "undefined" &&
    new URLSearchParams(window.location.search).has("popout");
}

export function getPopoutType(): "reader" | "compose" | null {
  if (typeof window === "undefined") return null;
  const params = new URLSearchParams(window.location.search);
  if (params.has("emailId")) return "reader";
  if (params.get("mode") === "compose") return "compose";
  return null;
}

export function getPopoutEmailId(): string | null {
  if (typeof window === "undefined") return null;
  return new URLSearchParams(window.location.search).get("emailId");
}

export async function emitMailUpdated(
  emailId: string,
  action: MailUpdateAction,
): Promise<void> {
  const payload: MailUpdatedPayload = {
    emailId,
    action,
    sourceLabel: getWindowLabel(),
  };
  popoutEmit("mail-updated", payload);
}

export async function emitComposeInit(
  targetLabel: string,
  payload: ComposeInitPayload,
): Promise<void> {
  popoutEmit(`compose:init:${targetLabel}`, payload);
}

export function useMailUpdatedListener(
  onUpdate: (payload: MailUpdatedPayload) => void,
): void {
  const callbackRef = useRef(onUpdate);
  callbackRef.current = onUpdate;

  useEffect(() => {
    const myLabel = getWindowLabel();
    return popoutListen("mail-updated", (raw) => {
      const payload = raw as MailUpdatedPayload;
      if (payload.sourceLabel === myLabel) return;
      callbackRef.current(payload);
    });
  }, []);
}

export async function emitMailRollback(
  emailId: string,
  action: MailUpdateAction,
): Promise<void> {
  const payload: MailRollbackPayload = {
    emailId,
    action,
    sourceLabel: getWindowLabel(),
  };
  popoutEmit("mail-rollback", payload);
}

export function useMailRollbackListener(
  onRollback: (payload: MailRollbackPayload) => void,
): void {
  const callbackRef = useRef(onRollback);
  callbackRef.current = onRollback;

  useEffect(() => {
    const myLabel = getWindowLabel();
    return popoutListen("mail-rollback", (raw) => {
      const payload = raw as MailRollbackPayload;
      if (payload.sourceLabel === myLabel) return;
      callbackRef.current(payload);
    });
  }, []);
}

export function useComposeInitListener(
  onInit: (payload: ComposeInitPayload) => void,
): void {
  const callbackRef = useRef(onInit);
  callbackRef.current = onInit;
  const [received, setReceived] = useState(false);

  useEffect(() => {
    if (received) return;
    const myLabel = getWindowLabel();
    return popoutListen(`compose:init:${myLabel}`, (raw) => {
      setReceived(true);
      callbackRef.current(raw as ComposeInitPayload);
    });
  }, [received]);
}

export function usePopoutWindow() {
  const { toast } = useToast();

  const openReader = useCallback(
    async (emailId: string, subject: string) => {
      const label = `reader-${emailId}`;
      await focusOrCreate(label, {
        url: popoutUrl(`emailId=${encodeURIComponent(emailId)}&popout=1`),
        title: subject || "Email",
        width: 900,
        height: 700,
      });
    },
    [],
  );

  const openCompose = useCallback(
    async (initPayload: ComposeInitPayload): Promise<boolean> => {
      const label = `compose-${Date.now()}`;
      const created = await focusOrCreate(label, {
        url: popoutUrl(`mode=compose&popout=1`),
        title: "Compose",
        width: 800,
        height: 650,
      });

      if (!created) {
        return true;
      }

      let resolved = false;
      let unlistenFn: (() => void) | undefined;
      let pingTimer: ReturnType<typeof setTimeout> | undefined;

      try {
        await new Promise<void>((resolve, reject) => {
          const timeout10s = setTimeout(async () => {
            if (resolved) return;
            resolved = true;
            unlistenFn?.();
            if (pingTimer) clearTimeout(pingTimer);
            await closePopout(label);
            reject(new Error("Compose window failed to initialize."));
          }, 10000);

          pingTimer = setTimeout(() => {
            if (resolved) return;
            popoutEmit(`compose:ping:${label}`, null);
          }, 5000);

          unlistenFn = popoutListen(`compose:ready:${label}`, () => {
            if (resolved) return;
            resolved = true;
            clearTimeout(timeout10s);
            if (pingTimer) clearTimeout(pingTimer);
            unlistenFn?.();
            resolve();
          });
        });

        await emitComposeInit(label, initPayload);
        return true;
      } catch (err) {
        const message =
          err instanceof Error ? err.message : "Failed to open compose popout";
        toast("error", message);
        return false;
      }
    },
    [toast],
  );

  const close = useCallback(async (label: string) => {
    await closePopout(label);
  }, []);

  return {
    openReader,
    openCompose,
    close,
    isPopout: isPopout(),
    popoutType: getPopoutType(),
    popoutEmailId: getPopoutEmailId(),
  };
}

/** Emits the compose-ready signal and answers ping requests from the opener.
 *  Used by the compose popout window to complete the open handshake. */
export function announceComposeReady(): () => void {
  const label = getWindowLabel();
  popoutEmit(`compose:ready:${label}`, null);
  return popoutListen(`compose:ping:${label}`, () => {
    popoutEmit(`compose:ready:${label}`, null);
  });
}
