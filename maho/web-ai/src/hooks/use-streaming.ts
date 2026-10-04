/**
 * useStreaming — maintains one sequential chat event poll loop per active handle.
 *
 * Native chat events use `{ type, data }`; desktop/test bridges may already return
 * normalized `{ kind, ... }` events. Malformed and unknown entries are ignored.
 */
import { useEffect, useRef } from 'preact/hooks';
import type { ChatEvent, MahoBridge, SessionHandle } from '../bridge/types';
import { parseCredentialErrorCode } from '../screens/credential-error';
import { coalesceChatEventBatch } from '../store/chatStore';

const POLL_INTERVAL_MS = 50;

export interface UseStreamingOptions {
  bridge: MahoBridge;
  sessionId: SessionHandle | null;
  onEvents: (events: ChatEvent[]) => void;
  onPollError?: (message: string) => void;
  onReady?: (handle: SessionHandle) => void;
}

export function normalizeChatEvent(raw: unknown): ChatEvent | null {
  if (!isRecord(raw)) {
    return null;
  }

  if (typeof raw.type === 'string') {
    switch (raw.type) {
      case 'token':
        return typeof raw.data === 'string' ? { kind: 'token', token: raw.data } : null;
      case 'thinking':
        return typeof raw.data === 'string' ? { kind: 'thinking', thinking: raw.data } : null;
      case 'complete': {
        if (typeof raw.data === 'string') {
          return { kind: 'complete', content: raw.data };
        }
        if (isRecord(raw.data) && typeof raw.data.full_text === 'string') {
          const toolCalls = parseCompleteToolCalls(raw.data.tool_calls_json);
          return {
            kind: 'complete',
            content: raw.data.full_text,
            ...(toolCalls.length > 0 ? { toolCalls } : {}),
            ...(typeof raw.data.tool_calls_json === 'string'
              ? { toolCallsJson: raw.data.tool_calls_json }
              : {}),
          };
        }
        return null;
      }
      case 'error': {
        if (typeof raw.data !== 'string') return null;
        const message = raw.data;
        const code = parseCredentialErrorCode(message);
        return code
          ? { kind: 'error', message, credentialErrorCode: code }
          : { kind: 'error', message };
      }
      default:
        return null;
    }
  }

  switch (raw.kind) {
    case 'token':
      return typeof raw.token === 'string' ? { kind: 'token', token: raw.token } : null;
    case 'thinking':
      return typeof raw.thinking === 'string' ? { kind: 'thinking', thinking: raw.thinking } : null;
    case 'complete': {
      const content = typeof raw.content === 'string'
        ? raw.content
        : typeof raw.message === 'string'
          ? raw.message
          : null;
      return content === null ? null : { kind: 'complete', content };
    }
    case 'error': {
      const message = typeof raw.message === 'string'
        ? raw.message
        : typeof raw.error === 'string'
          ? raw.error
          : null;
      if (message === null) return null;
      const code = parseCredentialErrorCode(message);
      return code
        ? { kind: 'error', message, credentialErrorCode: code }
        : { kind: 'error', message };
    }
    case 'tool_call':
      if (
        typeof raw.toolCallId === 'string' &&
        typeof raw.name === 'string' &&
        isRecord(raw.args)
      ) {
        return {
          kind: 'tool_call',
          toolCallId: raw.toolCallId,
          name: raw.name,
          args: raw.args,
        };
      }
      return null;
    default:
      return null;
  }
}

export function useStreaming({
  bridge,
  sessionId,
  onEvents,
  onPollError,
  onReady,
}: UseStreamingOptions): void {
  const onEventsRef = useRef(onEvents);
  const onPollErrorRef = useRef(onPollError);
  const onReadyRef = useRef(onReady);
  onEventsRef.current = onEvents;
  onPollErrorRef.current = onPollError;
  onReadyRef.current = onReady;

  useEffect(() => {
    if (sessionId === null) return;

    let active = true;
    let timeoutId: ReturnType<typeof setTimeout> | undefined;

    const tick = async (): Promise<void> => {
      if (!active) return;

      try {
        const rawEvents = await bridge.chatPollEvents(sessionId);
        if (!active) return;

        const events = (Array.isArray(rawEvents) ? rawEvents : [])
          .map(normalizeChatEvent)
          .filter((event): event is ChatEvent => event !== null);
        const coalesced = coalesceChatEventBatch(events);
        if (coalesced.length > 0) {
          onEventsRef.current(coalesced);
        }
      } catch (error) {
        if (active) {
          onPollErrorRef.current?.(toErrorMessage(error));
        }
        return;
      }

      if (active) {
        timeoutId = setTimeout(() => void tick(), POLL_INTERVAL_MS);
      }
    };

    onReadyRef.current?.(sessionId);
    void tick();

    return () => {
      active = false;
      if (timeoutId !== undefined) {
        clearTimeout(timeoutId);
      }
    };
  }, [bridge, sessionId]);
}

function parseCompleteToolCalls(value: unknown): Array<{ id: string; name: string; args: unknown }> {
  if (typeof value !== 'string') {
    return [];
  }

  try {
    const decoded: unknown = JSON.parse(value);
    if (!Array.isArray(decoded)) {
      return [];
    }

    return decoded.flatMap((call) => {
      if (!isRecord(call) || typeof call.id !== 'string' || typeof call.name !== 'string') {
        return [];
      }

      const rawArgs = typeof call.arguments_json === 'string'
        ? call.arguments_json
        : typeof call.arguments === 'string'
          ? call.arguments
          : '{}';
      return [{
        id: call.id,
        name: call.name,
        args: parseToolArguments(rawArgs),
      }];
    });
  } catch {
    return [];
  }
}

function parseToolArguments(rawArgs: string): unknown {
  try {
    return JSON.parse(rawArgs);
  } catch {
    return rawArgs;
  }
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === 'object' && value !== null;
}

function toErrorMessage(error: unknown): string {
  if (error instanceof Error && error.message.trim()) {
    return error.message;
  }
  if (typeof error === 'string' && error.trim()) {
    return error;
  }
  return 'Chat event polling failed.';
}
