/**
 * useStreaming — drives the chat event poll loop.
 *
 * Calls chatPollEvents every POLL_INTERVAL_MS.
 * Each raw event from Rust is dispatched to the caller.
 * Stops automatically when the session ends or the component unmounts.
 */
import { useCallback, useEffect, useRef } from 'preact/hooks';
import type { MahoBridge, SessionHandle, ChatEvent } from '../bridge/types';

const POLL_INTERVAL_MS = 50;

export interface UseStreamingOptions {
  bridge: MahoBridge;
  sessionId: SessionHandle | null;
  onEvents: (events: ChatEvent[]) => void;
  onDone?: () => void;
}

export function useStreaming({ bridge, sessionId, onEvents, onDone }: UseStreamingOptions): void {
  const onEventsRef = useRef(onEvents);
  const onDoneRef = useRef(onDone);
  onEventsRef.current = onEvents;
  onDoneRef.current = onDone;

  const poll = useCallback(async (): Promise<boolean> => {
    if (sessionId === null) return false;

    try {
      const events = await bridge.chatPollEvents(sessionId);
      if (events.length === 0) {
        return true;
      }
      onEventsRef.current(events);
      const finished = events.some((e) => e.kind === 'complete' || e.kind === 'error');
      if (finished) {
        onDoneRef.current?.();
        return false;
      }
      return true;
    } catch {
      onDoneRef.current?.();
      return false;
    }
  }, [bridge, sessionId]);

  useEffect(() => {
    if (sessionId === null) return;

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
  }, [sessionId, poll]);
}
