import { useEffect, useRef } from "react";

interface AppLifecycleOptions {
  onForeground?: () => void;
  onBackground?: () => void;
}

const DEBOUNCE_MS = 30_000;

export function useAppLifecycle({ onForeground, onBackground }: AppLifecycleOptions) {
  const lastSyncRef = useRef(0);
  const onFgRef = useRef(onForeground);
  const onBgRef = useRef(onBackground);
  onFgRef.current = onForeground;
  onBgRef.current = onBackground;

  useEffect(() => {
    if (typeof document === "undefined") return undefined;

    const handler = () => {
      if (document.visibilityState === "visible") {
        const now = Date.now();
        if (now - lastSyncRef.current < DEBOUNCE_MS) return;
        lastSyncRef.current = now;
        onFgRef.current?.();
      } else if (document.visibilityState === "hidden") {
        onBgRef.current?.();
      }
    };

    document.addEventListener("visibilitychange", handler);
    return () => document.removeEventListener("visibilitychange", handler);
  }, []);
}
