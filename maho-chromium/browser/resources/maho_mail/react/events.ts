// Copyright 2026 Maho Browser. All rights reserved.

import { callbackRouter } from './mojo_client.js';
import type { ReplyContext, ComposeAttachment } from './types';

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
  forwardEmailUid?: number;
  forwardFolderId?: string;
  initialAttachments?: ComposeAttachment[];
  initialReadReceipt?: boolean;
}

export interface Event<T> {
  event: string;
  payload: T;
}

export type EventCallback<T> = (event: Event<T>) => void;

function safeParseJson(payload: string): unknown {
  try {
    return JSON.parse(payload);
  } catch {
    return payload;
  }
}

function safeParseJsonObject(payload: string): Record<string, unknown> {
  try {
    const parsed = JSON.parse(payload);
    if (typeof parsed === 'object' && parsed !== null && !Array.isArray(parsed)) {
      return parsed as Record<string, unknown>;
    }
  } catch {}
  return {};
}

function dispatchEvent<T>(handler: EventCallback<T>, event: string, payload: unknown): void {
  handler({
    event,
    payload: payload as T,
  });
}

/**
 * Tauri event listen mock for the WebUI migration.
 *
 * Listens to Mojo callbacks where available, and maps them to Tauri payload format
 * so that existing frontend hooks don't break.
 */
async function realListen<T>(eventName: string, handler: EventCallback<T>): Promise<() => void> {
  if (eventName === 'accounts-changed' || eventName === 'accounts-changed-event') {
    const listenerId = callbackRouter.onAccountsChanged.addListener(() => {
      dispatchEvent(handler, eventName, {});
    });
    return () => {
      callbackRouter.removeListener(listenerId);
    };
  }

  if (eventName === 'auth-reauth-required') {
    const listenerId = callbackRouter.onAuthRequired.addListener((accountId: string, provider: string, reason: string) => {
      dispatchEvent(handler, eventName, {
        account_id: accountId,
        provider,
        reason,
      });
    });
    return () => {
      callbackRouter.removeListener(listenerId);
    };
  }

  if (eventName === 'auth-refresh-succeeded') {
    const listenerId = callbackRouter.onAuthRefreshSucceeded.addListener((accountId: string) => {
      dispatchEvent(handler, eventName, {
        account_id: accountId,
      });
    });
    return () => {
      callbackRouter.removeListener(listenerId);
    };
  }

  if (eventName === 'sync-started' || eventName === 'sync-completed' || eventName === 'sync-error') {
    const listenerId = callbackRouter.onSyncEvent.addListener((eventType: string, payload: string) => {
      if (eventType === eventName) {
        dispatchEvent(handler, eventName, safeParseJson(payload));
      }
    });
    return () => {
      callbackRouter.removeListener(listenerId);
    };
  }

  if (eventName === 'backfill-started' || eventName === 'backfill-completed' || eventName === 'backfill-error') {
    const listenerId = callbackRouter.onBackfillEvent.addListener((eventType: string, payload: string) => {
      if (eventType === eventName) {
        dispatchEvent(handler, eventName, safeParseJson(payload));
      }
    });
    return () => {
      callbackRouter.removeListener(listenerId);
    };
  }

  if (eventName === 'status-changed' || eventName === 'connection-status-changed') {
    const listenerId = callbackRouter.onStatusChanged.addListener((accountId: string, connected: boolean) => {
      dispatchEvent(handler, eventName, { account_id: accountId, connected });
    });
    return () => {
      callbackRouter.removeListener(listenerId);
    };
  }

  if (eventName === 'new-mail' || eventName === 'new-emails') {
    const listenerId = callbackRouter.onNewMail.addListener((accountId: string) => {
      dispatchEvent(handler, eventName, { account_id: accountId, message_ids: [] });
    });
    return () => {
      callbackRouter.removeListener(listenerId);
    };
  }

  if (eventName === 'idle:new-mail') {
    const listenerId = callbackRouter.onNewMail.addListener((accountId: string) => {
      dispatchEvent(handler, eventName, { account_id: accountId, exists: 1 });
    });
    return () => {
      callbackRouter.removeListener(listenerId);
    };
  }

  if (eventName === 'mutation') {
    const listenerId = callbackRouter.onMutation.addListener((accountId: string, payload: string) => {
      const p = safeParseJsonObject(payload);
      dispatchEvent(handler, eventName, { account_id: accountId, ...p });
    });
    return () => {
      callbackRouter.removeListener(listenerId);
    };
  }

  if (eventName === 'mutations-flushed' || eventName === 'mutation-queued') {
    const listenerId = callbackRouter.onMutation.addListener((_accountId: string, payload: string) => {
      const p = safeParseJsonObject(payload);
      if (p.event === eventName) {
        dispatchEvent(handler, eventName, p.data);
      }
    });
    return () => {
      callbackRouter.removeListener(listenerId);
    };
  }

  if (eventName === 'outbox-queued' || eventName === 'outbox-flushed') {
    const listenerId = callbackRouter.onOutbox.addListener((accountId: string, payload: string) => {
      const p = safeParseJsonObject(payload);
      if (p.event === eventName) {
        dispatchEvent(handler, eventName, p.data);
      }
    });
    return () => {
      callbackRouter.removeListener(listenerId);
    };
  }

  if (
    eventName === 'snooze-triggered' ||
    eventName === 'reminder-triggered' ||
    eventName === 'send-later-sent' ||
    eventName === 'send-later-failed'
  ) {
    const listenerId = callbackRouter.onScheduler.addListener((accountId: string, payload: string) => {
      const p = safeParseJsonObject(payload);
      if (p.event === eventName) {
        dispatchEvent(handler, eventName, p.data);
      }
    });
    return () => {
      callbackRouter.removeListener(listenerId);
    };
  }

  if (eventName === 'connection-error') {
    const listenerId = callbackRouter.onStatusChanged.addListener((accountId: string, connected: boolean) => {
      if (!connected) {
        dispatchEvent(handler, eventName, { account_id: accountId, error: 'Connection lost', error_type: 'network' });
      }
    });
    return () => {
      callbackRouter.removeListener(listenerId);
    };
  }

  if (eventName === 'agent-stream') {
    const listenerId = callbackRouter.onAgentStream.addListener((sessionId: string, chunk: string) => {
      dispatchEvent(handler, eventName, { session_id: sessionId, chunk });
    });
    return () => {
      callbackRouter.removeListener(listenerId);
    };
  }

  if (eventName === 'agent-message-chunk') {
    const listenerId = callbackRouter.onAgentStream.addListener((sessionId: string, chunk: string) => {
      try {
        const wire = JSON.parse(chunk);
        if (typeof wire === 'object' && wire !== null && 'kind' in wire) {
          dispatchEvent(handler, eventName, { session_id: sessionId, ...wire });
          return;
        }
      } catch (_) {}
      dispatchEvent(handler, eventName, { session_id: sessionId, chunk });
    });
    return () => {
      callbackRouter.removeListener(listenerId);
    };
  }

  if (eventName === 'agent-message-complete') {
    const listenerId = callbackRouter.onAgentStream.addListener((sessionId: string, chunk: string) => {
      try {
        const wire = JSON.parse(chunk);
        if (typeof wire === 'object' && wire !== null && wire.kind === 'complete') {
          dispatchEvent(handler, eventName, { session_id: sessionId });
        }
      } catch (_) {}
    });
    return () => {
      callbackRouter.removeListener(listenerId);
    };
  }

  if (eventName === 'agent-message-error') {
    const listenerId = callbackRouter.onAgentStream.addListener((sessionId: string, chunk: string) => {
      try {
        const wire = JSON.parse(chunk);
        if (typeof wire === 'object' && wire !== null && wire.kind === 'error') {
          const errorMsg: string =
            (wire.payload && typeof wire.payload.error === 'string')
              ? wire.payload.error
              : 'Agent stream error';
          dispatchEvent(handler, eventName, { session_id: sessionId, error: errorMsg });
        }
      } catch (_) {}
    });
    return () => {
      callbackRouter.removeListener(listenerId);
    };
  }

  if (eventName === 'calendar-events-changed') {
    const listenerId = callbackRouter.onCalendar.addListener((accountId: string, _payload: string) => {
      dispatchEvent(handler, eventName, accountId);
    });
    return () => {
      callbackRouter.removeListener(listenerId);
    };
  }

  if (
    eventName === 'calendar-auto-declined' ||
    eventName === 'calendar-rsvp-reply-failed' ||
    eventName === 'calendar-rsvp-reply-queued' ||
    eventName === 'calendar-rsvp-reply-drained'
  ) {
    const listenerId = callbackRouter.onCalendar.addListener((_accountId: string, payload: string) => {
      const p = safeParseJsonObject(payload);
      if (p.event === eventName) {
        dispatchEvent(handler, eventName, p.data);
      }
    });
    return () => {
      callbackRouter.removeListener(listenerId);
    };
  }

  if (eventName === 'import-progress' || eventName === 'export-progress') {
    const listenerId = callbackRouter.onImport.addListener((payload: string) => {
      const p = safeParseJsonObject(payload);
      if (p.event === eventName) {
        dispatchEvent(handler, eventName, p.data);
      }
    });
    return () => {
      callbackRouter.removeListener(listenerId);
    };
  }

  return () => {};
}

const isTest = typeof globalThis !== 'undefined' && 'vi' in globalThis;

async function emit(_eventName: string, _payload?: unknown): Promise<void> {}
async function emitTo(_target: string | { kind: string }, _eventName: string, _payload?: unknown): Promise<void> {}

type MockFn<T> = {
  mockResolvedValue: (val: T) => (...args: unknown[]) => Promise<T>;
};

interface ViGlobal {
  vi: {
    fn: <T>() => MockFn<T>;
  };
}

const mockListen: typeof realListen = isTest
  ? ((globalThis as unknown as ViGlobal).vi.fn<() => void>().mockResolvedValue(() => {}) as unknown as typeof realListen)
  : realListen;

export { realListen, mockListen as listen };

const mockEmit: typeof emit = isTest
  ? ((globalThis as unknown as ViGlobal).vi.fn<void>().mockResolvedValue(undefined) as unknown as typeof emit)
  : emit;

export { mockEmit as emit };

const mockEmitTo: typeof emitTo = isTest
  ? ((globalThis as unknown as ViGlobal).vi.fn<void>().mockResolvedValue(undefined) as unknown as typeof emitTo)
  : emitTo;

export { mockEmitTo as emitTo };
