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
import type { AgentEvent, AgentHandle, ArtifactInfo, MahoBridge } from '../bridge/types';
import { parseCredentialErrorCode } from '../screens/credential-error';

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
    case 'thinking': {
      const thinking = typeof envelope.data === 'string' ? envelope.data : '';
      return { kind: 'thinking', thinking };
    }
    case 'tool_call': {
      let id = '';
      let name = 'unknown_tool';
      let args = '{}';
      if (typeof envelope.data === 'object' && envelope.data !== null) {
        const d = envelope.data as Record<string, unknown>;
        if (typeof d.id === 'string') id = d.id;
        if (typeof d.name === 'string') name = d.name;
        if (typeof d.args === 'string') args = d.args;
      }
      return { kind: 'tool_call', id, name, args };
    }
    case 'tool_result': {
      let id = '';
      let name = 'unknown_tool';
      let result = '';
      if (typeof envelope.data === 'object' && envelope.data !== null) {
        const d = envelope.data as Record<string, unknown>;
        if (typeof d.id === 'string') id = d.id;
        if (typeof d.name === 'string') name = d.name;
        if (typeof d.result === 'string') result = d.result;
      }
      return { kind: 'tool_result', id, name, result };
    }
    case 'artifact_created': {
      if (typeof envelope.data !== 'object' || envelope.data === null) {
        return null;
      }
      const d = envelope.data as Record<string, unknown>;
      const artifactId = typeof d.artifact_id === 'string' ? d.artifact_id : '';
      const displayName = typeof d.display_name === 'string' ? d.display_name : '';
      const mimeType = typeof d.mime_type === 'string' ? d.mime_type : '';
      if (!artifactId || !displayName || !mimeType) {
        return null;
      }
      const artifact: ArtifactInfo = {
        artifactId,
        sessionId: typeof d.session_id === 'string' ? d.session_id : '',
        displayName,
        mimeType,
        sizeBytes: typeof d.size_bytes === 'number' ? d.size_bytes : 0,
        createdAt: typeof d.created_at === 'number' ? d.created_at : 0,
      };
      return { kind: 'artifact_created', artifact };
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
      // data is a plain string — the AgentError display string, or the typed
      // credential envelope for credential failures.
      const message = typeof envelope.data === 'string' ? envelope.data : 'Unknown agent error';
      const code = parseCredentialErrorCode(message);
      return code
        ? { kind: 'error', message, credentialErrorCode: code }
        : { kind: 'error', message };
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
