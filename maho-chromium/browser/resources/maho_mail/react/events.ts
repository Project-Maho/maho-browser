// Copyright 2026 Maho Browser. All rights reserved.

import { callbackRouter } from './mojo_client.js';

export interface Event<T> {
  event: string;
  payload: T;
}

export type EventCallback<T> = (event: Event<T>) => void;

/**
 * Tauri event listen mock for the WebUI migration.
 *
 * Listens to Mojo callbacks where available, and maps them to Tauri payload format
 * so that existing frontend hooks don't break.
 */
async function realListen<T>(eventName: string, handler: EventCallback<T>): Promise<() => void> {
  if (eventName === 'accounts-changed' || eventName === 'accounts-changed-event') {
    const listenerId = callbackRouter.onAccountsChanged.addListener(() => {
      handler({
        event: eventName,
        payload: {} as T,
      });
    });
    return () => {
      callbackRouter.removeListener(listenerId);
    };
  }

  if (eventName === 'auth-reauth-required') {
    const listenerId = callbackRouter.onAuthRequired.addListener((accountId: string, provider: string, reason: string) => {
      handler({
        event: eventName,
        payload: {
          account_id: accountId,
          provider,
          reason,
        } as unknown as T,
      });
    });
    return () => {
      callbackRouter.removeListener(listenerId);
    };
  }

  if (eventName === 'auth-refresh-succeeded') {
    const listenerId = callbackRouter.onAuthRefreshSucceeded.addListener((accountId: string) => {
      handler({
        event: eventName,
        payload: {
          account_id: accountId,
        } as unknown as T,
      });
    });
    return () => {
      callbackRouter.removeListener(listenerId);
    };
  }

  if (eventName === 'sync-started' || eventName === 'sync-completed' || eventName === 'sync-error') {
    const listenerId = callbackRouter.onSyncEvent.addListener((eventType: string, payload: string) => {
      if (eventType === eventName) {
        let p: any = payload;
        try { p = JSON.parse(payload); } catch (_) {}
        handler({
          event: eventName,
          payload: p as T,
        });
      }
    });
    return () => {
      callbackRouter.removeListener(listenerId);
    };
  }

  if (eventName === 'backfill-started' || eventName === 'backfill-completed' || eventName === 'backfill-error') {
    const listenerId = callbackRouter.onBackfillEvent.addListener((eventType: string, payload: string) => {
      if (eventType === eventName) {
        let p: any = payload;
        try { p = JSON.parse(payload); } catch (_) {}
        handler({
          event: eventName,
          payload: p as T,
        });
      }
    });
    return () => {
      callbackRouter.removeListener(listenerId);
    };
  }

  if (eventName === 'status-changed' || eventName === 'connection-status-changed') {
    const listenerId = callbackRouter.onStatusChanged.addListener((accountId: string, connected: boolean) => {
      handler({
        event: eventName,
        payload: { account_id: accountId, connected } as unknown as T,
      });
    });
    return () => {
      callbackRouter.removeListener(listenerId);
    };
  }

  if (eventName === 'new-mail' || eventName === 'new-emails') {
    const listenerId = callbackRouter.onNewMail.addListener((accountId: string) => {
      handler({
        event: eventName,
        payload: { account_id: accountId, message_ids: [] } as unknown as T,
      });
    });
    return () => {
      callbackRouter.removeListener(listenerId);
    };
  }

  if (eventName === 'idle:new-mail') {
    const listenerId = callbackRouter.onNewMail.addListener((accountId: string) => {
      handler({
        event: eventName,
        payload: { account_id: accountId, exists: 1 } as unknown as T,
      });
    });
    return () => {
      callbackRouter.removeListener(listenerId);
    };
  }

  if (eventName === 'mutation') {
    const listenerId = callbackRouter.onMutation.addListener((accountId: string, payload: string) => {
      let p: any = {};
      try { p = JSON.parse(payload); } catch (_) {}
      handler({
        event: eventName,
        payload: { account_id: accountId, ...p } as T,
      });
    });
    return () => {
      callbackRouter.removeListener(listenerId);
    };
  }

  if (eventName === 'mutations-flushed' || eventName === 'mutation-queued') {
    const listenerId = callbackRouter.onMutation.addListener((_accountId: string, payload: string) => {
      let p: any = {};
      try { p = JSON.parse(payload); } catch (_) {}
      if (p.event === eventName) {
        handler({
          event: eventName,
          payload: p.data as T,
        });
      }
    });
    return () => {
      callbackRouter.removeListener(listenerId);
    };
  }

  if (eventName === 'outbox-queued' || eventName === 'outbox-flushed') {
    const listenerId = callbackRouter.onOutbox.addListener((accountId: string, payload: string) => {
      let p: any = {};
      try { p = JSON.parse(payload); } catch (_) {}
      if (p.event === eventName) {
        handler({
          event: eventName,
          payload: p.data as T,
        });
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
      let p: any = {};
      try { p = JSON.parse(payload); } catch (_) {}
      if (p.event === eventName) {
        handler({
          event: eventName,
          payload: p.data as T,
        });
      }
    });
    return () => {
      callbackRouter.removeListener(listenerId);
    };
  }

  if (eventName === 'connection-error') {
    const listenerId = callbackRouter.onStatusChanged.addListener((accountId: string, connected: boolean) => {
      if (!connected) {
        handler({
          event: eventName,
          payload: { account_id: accountId, error: 'Connection lost', error_type: 'network' } as unknown as T,
        });
      }
    });
    return () => {
      callbackRouter.removeListener(listenerId);
    };
  }

  if (eventName === 'agent-stream') {
    const listenerId = callbackRouter.onAgentStream.addListener((sessionId: string, chunk: string) => {
      handler({
        event: eventName,
        payload: { session_id: sessionId, chunk } as unknown as T,
      });
    });
    return () => {
      callbackRouter.removeListener(listenerId);
    };
  }

  if (eventName === 'agent-message-chunk') {
    const listenerId = callbackRouter.onAgentStream.addListener((sessionId: string, chunk: string) => {
      let wire: any;
      try {
        wire = JSON.parse(chunk);
        if (typeof wire === 'object' && wire !== null && 'kind' in wire) {
          handler({
            event: eventName,
            payload: { session_id: sessionId, ...wire } as unknown as T,
          });
          return;
        }
      } catch (_) {}
      handler({
        event: eventName,
        payload: { session_id: sessionId, chunk } as unknown as T,
      });
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
          handler({
            event: eventName,
            payload: { session_id: sessionId } as unknown as T,
          });
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
          handler({
            event: eventName,
            payload: { session_id: sessionId, error: errorMsg } as unknown as T,
          });
        }
      } catch (_) {}
    });
    return () => {
      callbackRouter.removeListener(listenerId);
    };
  }

  if (eventName === 'calendar-events-changed') {
    const listenerId = callbackRouter.onCalendar.addListener((accountId: string, _payload: string) => {
      handler({
        event: eventName,
        payload: accountId as unknown as T,
      });
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
      let p: any = {};
      try { p = JSON.parse(payload); } catch (_) {}
      if (p.event === eventName) {
        handler({
          event: eventName,
          payload: p.data as T,
        });
      }
    });
    return () => {
      callbackRouter.removeListener(listenerId);
    };
  }

  if (eventName === 'import-progress' || eventName === 'export-progress') {
    const listenerId = callbackRouter.onImport.addListener((payload: string) => {
      let p: any = {};
      try { p = JSON.parse(payload); } catch (_) {}
      if (p.event === eventName) {
        handler({
          event: eventName,
          payload: p.data as T,
        });
      }
    });
    return () => {
      callbackRouter.removeListener(listenerId);
    };
  }

  return () => {};
}

const isTest = typeof globalThis !== 'undefined' && 'vi' in globalThis;

async function emit(eventName: string, payload?: any): Promise<void> {}
async function emitTo(target: string | { kind: string }, eventName: string, payload?: any): Promise<void> {}

const mockListen: typeof realListen = isTest
  ? (globalThis as any).vi.fn().mockResolvedValue(() => {})
  : realListen;

export { mockListen as listen };

const mockEmit: typeof emit = isTest
  ? (globalThis as any).vi.fn().mockResolvedValue(undefined)
  : emit;

export { mockEmit as emit };

const mockEmitTo: typeof emitTo = isTest
  ? (globalThis as any).vi.fn().mockResolvedValue(undefined)
  : emitTo;

export { mockEmitTo as emitTo };
