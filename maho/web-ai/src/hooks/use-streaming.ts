/**
 * useStreaming — drives the chat event poll loop.
 *
 * Calls chatSessionPollEvent every POLL_INTERVAL_MS.
 * Each raw event string from Rust is dispatched to the caller via onEvent.
 * Stops automatically when the session ends or the component unmounts.
 */
import { useCallback, useEffect, useRef } from 'preact/hooks';
import type { MahoBridge } from '../bridge/types';

const POLL_INTERVAL_MS = 50;

export interface UseStreamingOptions {
  bridge: MahoBridge;
  sessionPtr: number | null;
  onEvent: (rawEvent: string) => void;
  onDone?: () => void;
}

export function useStreaming({ bridge, sessionPtr, onEvent, onDone }: UseStreamingOptions): void {
  const onEventRef = useRef(onEvent);
  const onDoneRef = useRef(onDone);
  onEventRef.current = onEvent;
  onDoneRef.current = onDone;

  const poll = useCallback(async (): Promise<boolean> => {
    if (sessionPtr === null) return false;

    try {
      const event = await bridge.chatSessionPollEvent(sessionPtr);
      if (event === null) {
        // null means the session is finished
        onDoneRef.current?.();
        return false;
      }
      onEventRef.current(event);
      return true;
    } catch {
      onDoneRef.current?.();
      return false;
    }
  }, [bridge, sessionPtr]);

  useEffect(() => {
    if (sessionPtr === null) return;

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
  }, [sessionPtr, poll]);
}
