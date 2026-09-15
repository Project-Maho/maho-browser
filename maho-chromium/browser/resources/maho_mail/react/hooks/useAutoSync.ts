import { useEffect, useRef } from "react";
import { listen } from "../events.js";
import type { SyncErrorEvent } from "../types";

interface NewEmailsPayload {
  account_id: string;
  message_ids: string[];
}

interface UseAutoSyncOptions {
  onNewEmails?: (accountId: string, messageIds: string[]) => void;
  onSyncStarted?: () => void;
  onSyncCompleted?: () => void;
  onSyncError?: (event: SyncErrorEvent) => void;
  onConnectionError?: (accountId: string, error: string, errorType: SyncErrorEvent["error_type"] | string) => void;
  onSnoozeTriggered?: (emailIds: string[]) => void;
  onReminderTriggered?: (items: Array<[string, string]>) => void;
  onSendLaterSent?: (id: string) => void;
  onSendLaterFailed?: (data: { id: string; error: string }) => void;
}

type SyncErrorPayload = SyncErrorEvent | string;

function normalizeSyncErrorPayload(payload: SyncErrorPayload): SyncErrorEvent {
  if (typeof payload === "string") {
    return { account_id: "", error: payload, error_type: "other" };
  }

  return payload;
}

export function useAutoSync(options: UseAutoSyncOptions) {
  const optionsRef = useRef(options);

  useEffect(() => {
    optionsRef.current = options;
  }, [options]);

  useEffect(() => {
    let active = true;
    const unlisteners: Array<() => void> = [];

    const setup = async () => {
      if (optionsRef.current.onNewEmails) {
        const unlisten = await listen<NewEmailsPayload>("new-emails", (event) => {
          if (active) optionsRef.current.onNewEmails?.(event.payload.account_id, event.payload.message_ids);
        });
        if (!active) {
          unlisten();
          return;
        }
        unlisteners.push(unlisten);
      }

      if (optionsRef.current.onSyncStarted) {
        const unlisten = await listen("sync-started", () => {
          if (active) optionsRef.current.onSyncStarted?.();
        });
        if (!active) {
          unlisten();
          return;
        }
        unlisteners.push(unlisten);
      }

      if (optionsRef.current.onSyncCompleted) {
        const unlisten = await listen("sync-completed", () => {
          if (active) optionsRef.current.onSyncCompleted?.();
        });
        if (!active) {
          unlisten();
          return;
        }
        unlisteners.push(unlisten);
      }

      if (optionsRef.current.onSyncError) {
        const unlisten = await listen<SyncErrorPayload>("sync-error", (event) => {
          if (active) optionsRef.current.onSyncError?.(normalizeSyncErrorPayload(event.payload));
        });
        if (!active) {
          unlisten();
          return;
        }
        unlisteners.push(unlisten);
      }

      if (optionsRef.current.onConnectionError) {
        const unlisten = await listen<{ account_id: string; error: string; error_type: string }>("connection-error", (event) => {
          if (active) optionsRef.current.onConnectionError?.(event.payload.account_id, event.payload.error, event.payload.error_type);
        });
        if (!active) {
          unlisten();
          return;
        }
        unlisteners.push(unlisten);
      }

      {
        const unlisten = await listen<string[]>("snooze-triggered", (event) => {
          if (active) optionsRef.current.onSnoozeTriggered?.(event.payload);
        });
        if (!active) {
          unlisten();
          return;
        }
        unlisteners.push(unlisten);
      }

      {
        const unlisten = await listen<Array<[string, string]>>("reminder-triggered", (event) => {
          if (active) optionsRef.current.onReminderTriggered?.(event.payload);
        });
        if (!active) {
          unlisten();
          return;
        }
        unlisteners.push(unlisten);
      }

      {
        const unlisten = await listen<string>("send-later-sent", (event) => {
          if (active) optionsRef.current.onSendLaterSent?.(event.payload);
        });
        if (!active) {
          unlisten();
          return;
        }
        unlisteners.push(unlisten);
      }

      {
        const unlisten = await listen<{ id: string; error: string }>("send-later-failed", (event) => {
          if (active) optionsRef.current.onSendLaterFailed?.(event.payload);
        });
        if (!active) {
          unlisten();
          return;
        }
        unlisteners.push(unlisten);
      }

    };

    void setup();

    return () => {
      active = false;
      for (const unlisten of unlisteners) {
        unlisten();
      }
    };
  }, []);
}
