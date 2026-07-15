/**
 * useAgentSession — drives the native agent kernel poll loop.
 *
 * The native agent event wire format is:
 *   { "type": "token" | "complete" | "error", "data": <string | object> }
 *
 * This hook normalizes those into AgentEvent and dispatches to the caller.
 * The poll loop runs every POLL_INTERVAL_MS while a turn is in progress,
 * and stops when a "complete" or "error" event is received, or on unmount.
 *
 * Platform notes:
 *   Android: agentPollEvent drains a Mutex<Vec<String>> queue (push from Rust worker)
 *   iOS:     agentPollEvent drains an AgentSessionContext event queue (push from FFI callbacks)
 *   Both use identical JSON wire format; normalization is platform-agnostic.
 */
import { useCallback, useEffect, useRef } from 'preact/hooks';
import type { AgentEvent, AgentHandle, MahoBridge } from '../bridge/types';

const POLL_INTERVAL_MS = 50;

export interface UseAgentSessionOptions {
  bridge: MahoBridge;
  /** Opaque handle returned by agentCreateSession. Poll loop is inactive when null. */
  handle: AgentHandle | null;
  /** Called for every normalized AgentEvent (token, complete, error). */
  onEvent: (event: AgentEvent) => void;
  /** Called once when the turn is finished (complete or error), before onEvent fires. */
  onDone?: () => void;
}

/**
 * Normalize a raw event JSON string from agentPollEvent into an AgentEvent.
 * Returns null for unrecognised or malformed payloads (caller skips silently).
 */
export function normalizeAgentEvent(raw: string): AgentEvent | null {
  let parsed: unknown;
  try {
    parsed = JSON.parse(raw);
  } catch {
    return null;
  }

  if (
    typeof parsed !== 'object' ||
    parsed === null ||
    typeof (parsed as Record<string, unknown>).type !== 'string'
  ) {
    return null;
  }

  const envelope = parsed as { type: string; data: unknown };

  switch (envelope.type) {
    case 'token': {
      // data is a plain string — the raw token delta
      const token = typeof envelope.data === 'string' ? envelope.data : '';
      return { kind: 'token', token };
    }
    case 'complete': {
      // data is `{ full_text: string, tool_calls_json: string }`
      // tool_calls_json is always "[]" — ignored here
      let fullText = '';
      if (typeof envelope.data === 'string') {
        fullText = envelope.data;
      } else if (
        typeof envelope.data === 'object' &&
        envelope.data !== null &&
        typeof (envelope.data as Record<string, unknown>).full_text === 'string'
      ) {
        fullText = (envelope.data as Record<string, string>).full_text;
      }
      return { kind: 'complete', fullText };
    }
    case 'error': {
      // data is a plain string — the AgentError display string
      const message = typeof envelope.data === 'string' ? envelope.data : 'Unknown agent error';
      return { kind: 'error', message };
    }
    default:
      return null;
  }
}

export function useAgentSession({
  bridge,
  handle,
  onEvent,
  onDone,
}: UseAgentSessionOptions): void {
  const onEventRef = useRef(onEvent);
  const onDoneRef = useRef(onDone);
  onEventRef.current = onEvent;
  onDoneRef.current = onDone;

  const poll = useCallback(async (): Promise<boolean> => {
    if (handle === null) return false;

    try {
      const raw = await bridge.agentPollEvent(handle);

      // null means queue was empty — keep polling
      if (raw === null) return true;

      const event = normalizeAgentEvent(raw);
      if (event === null) return true; // unrecognised event type — keep polling

      onEventRef.current(event);

      if (event.kind === 'complete' || event.kind === 'error') {
        onDoneRef.current?.();
        return false;
      }
      return true;
    } catch {
      onDoneRef.current?.();
      return false;
    }
  }, [bridge, handle]);

  useEffect(() => {
    if (handle === null) return;

    let active = true;
    let timeoutId: ReturnType<typeof setTimeout>;

    const tick = async () => {
      if (!active) return;
      const shouldContinue = await poll();
      if (active && shouldContinue) {
        timeoutId = setTimeout(tick, POLL_INTERVAL_MS);
      }
    };

    timeoutId = setTimeout(tick, POLL_INTERVAL_MS);

    return () => {
      active = false;
      clearTimeout(timeoutId);
    };
  }, [handle, poll]);
}
