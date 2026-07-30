import { useCallback, useRef, useState } from "react";

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
  const [pullDistance, setPullDistance] = useState(0);
  const [isPulling, setIsPulling] = useState(false);
  const [isRefreshing, setIsRefreshing] = useState(false);

  const reset = useCallback(() => {
    startYRef.current = null;
    shouldTrackRef.current = false;
    setIsPulling(false);
    setPullDistance(0);
  }, []);

  const handleTouchStart = useCallback(
    (event: React.TouchEvent<HTMLElement>) => {
      if (disabled || isRefreshing) {
        return;
      }

      const currentTarget = event.currentTarget;
      shouldTrackRef.current = currentTarget.scrollTop <= 0;
      startYRef.current = event.touches[0]?.clientY ?? null;
      setIsPulling(false);
      setPullDistance(0);
    },
    [disabled, isRefreshing],
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
      event.preventDefault();
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
