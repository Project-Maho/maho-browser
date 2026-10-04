import { useCallback, useEffect, useRef, useState } from "react";

interface PullToRefreshOptions {
  onRefresh: () => void | Promise<void>;
  disabled?: boolean;
  threshold?: number;
  maxPull?: number;
}

interface PullToRefreshBindings {
  onTouchStart: (event: React.TouchEvent<HTMLElement>) => void;
  onTouchMove: (event: React.TouchEvent<HTMLElement>) => void;
  onTouchEnd: () => void;
  onTouchCancel: () => void;
}

export function usePullToRefresh({
  onRefresh,
  disabled = false,
  threshold = 72,
  maxPull = 120,
}: PullToRefreshOptions): {
  bindings: PullToRefreshBindings;
  isPulling: boolean;
  isRefreshing: boolean;
  pullDistance: number;
  progress: number;
} {
  const startYRef = useRef<number | null>(null);
  const shouldTrackRef = useRef(false);
  const disabledRef = useRef(disabled);
  disabledRef.current = disabled;
  const maxPullRef = useRef(maxPull);
  maxPullRef.current = maxPull;

  const [pullDistance, setPullDistance] = useState(0);
  const [isPulling, setIsPulling] = useState(false);
  const [isRefreshing, setIsRefreshing] = useState(false);
  const isRefreshingRef = useRef(isRefreshing);
  isRefreshingRef.current = isRefreshing;

  const activeTargetRef = useRef<HTMLElement | null>(null);
  const nativeMoveListenerRef = useRef<((event: TouchEvent) => void) | null>(null);

  const cleanupNativeListener = useCallback(() => {
    if (activeTargetRef.current && nativeMoveListenerRef.current) {
      activeTargetRef.current.removeEventListener("touchmove", nativeMoveListenerRef.current);
    }
    activeTargetRef.current = null;
    nativeMoveListenerRef.current = null;
  }, []);

  useEffect(() => {
    return cleanupNativeListener;
  }, [cleanupNativeListener]);

  const reset = useCallback(() => {
    cleanupNativeListener();
    startYRef.current = null;
    shouldTrackRef.current = false;
    setIsPulling(false);
    setPullDistance(0);
  }, [cleanupNativeListener]);

  const handleTouchStart = useCallback(
    (event: React.TouchEvent<HTMLElement>) => {
      cleanupNativeListener();

      if (disabled || isRefreshing) {
        return;
      }

      const currentTarget = event.currentTarget;
      shouldTrackRef.current = currentTarget.scrollTop <= 0;
      startYRef.current = event.touches[0]?.clientY ?? null;
      setIsPulling(false);
      setPullDistance(0);

      if (
        shouldTrackRef.current &&
        currentTarget &&
        typeof (currentTarget as HTMLElement).addEventListener === "function"
      ) {
        const target = currentTarget as HTMLElement;
        const onNativeTouchMove = (e: TouchEvent) => {
          if (
            disabledRef.current ||
            isRefreshingRef.current ||
            !shouldTrackRef.current ||
            startYRef.current == null
          ) {
            return;
          }

          const currentY = e.touches[0]?.clientY ?? startYRef.current;
          const delta = currentY - startYRef.current;

          if (delta <= 0) {
            setIsPulling(false);
            setPullDistance(0);
            return;
          }

          const easedDistance = Math.min(maxPullRef.current, delta * 0.55);
          setIsPulling(true);
          setPullDistance(easedDistance);

          if (e.cancelable && !e.defaultPrevented) {
            try {
              e.preventDefault();
            } catch {
              // ignore passive / restricted listener invocation errors
            }
          }
        };

        target.addEventListener("touchmove", onNativeTouchMove, { passive: false });
        activeTargetRef.current = target;
        nativeMoveListenerRef.current = onNativeTouchMove;
      }
    },
    [cleanupNativeListener, disabled, isRefreshing],
  );

  const handleTouchMove = useCallback(
    (event: React.TouchEvent<HTMLElement>) => {
      if (disabled || isRefreshing || !shouldTrackRef.current || startYRef.current == null) {
        return;
      }

      const currentY = event.touches[0]?.clientY ?? startYRef.current;
      const delta = currentY - startYRef.current;

      if (delta <= 0) {
        setIsPulling(false);
        setPullDistance(0);
        return;
      }

      const easedDistance = Math.min(maxPull, delta * 0.55);
      setIsPulling(true);
      setPullDistance(easedDistance);

      if (event.cancelable && !event.defaultPrevented) {
        try {
          event.preventDefault();
        } catch {
          // ignore passive / restricted listener invocation errors
        }
      }
    },
    [disabled, isRefreshing, maxPull],
  );

  const handleTouchEnd = useCallback(() => {
    if (disabled || isRefreshing) {
      reset();
      return;
    }

    const shouldRefresh = pullDistance >= threshold;
    reset();

    if (!shouldRefresh) {
      return;
    }

    setIsRefreshing(true);
    Promise.resolve(onRefresh()).finally(() => {
      setIsRefreshing(false);
    });
  }, [disabled, isRefreshing, onRefresh, pullDistance, reset, threshold]);

  return {
    bindings: {
      onTouchStart: handleTouchStart,
      onTouchMove: handleTouchMove,
      onTouchEnd: handleTouchEnd,
      onTouchCancel: reset,
    },
    isPulling,
    isRefreshing,
    pullDistance,
    progress: Math.min(1, pullDistance / threshold),
  };
}
