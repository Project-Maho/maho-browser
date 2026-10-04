import { useCallback, useEffect, useRef } from "react";

interface UseLongPressOptions {
  onLongPress: () => void;
  onPress?: () => void;
  delay?: number;
  disabled?: boolean;
  moveThreshold?: number;
}

interface TouchLikeEvent {
  clientX: number;
  clientY: number;
}

function getTouchLikeEvent(
  event:
    | React.PointerEvent<HTMLElement>
    | React.MouseEvent<HTMLElement>
    | React.TouchEvent<HTMLElement>,
): TouchLikeEvent | null {
  if ("touches" in event) {
    const touch = event.touches[0] ?? event.changedTouches[0];
    return touch ? { clientX: touch.clientX, clientY: touch.clientY } : null;
  }

  return { clientX: event.clientX, clientY: event.clientY };
}

export function useLongPress({
  onLongPress,
  onPress,
  delay = 500,
  disabled = false,
  moveThreshold = 10,
}: UseLongPressOptions) {
  const timeoutRef = useRef<ReturnType<typeof setTimeout> | null>(null);
  const startPointRef = useRef<TouchLikeEvent | null>(null);
  const longPressTriggeredRef = useRef(false);

  const clearTimer = useCallback(() => {
    if (timeoutRef.current) {
      clearTimeout(timeoutRef.current);
      timeoutRef.current = null;
    }
  }, []);

  const cancel = useCallback(() => {
    clearTimer();
    startPointRef.current = null;
  }, [clearTimer]);

  const start = useCallback(
    (
      event:
        | React.PointerEvent<HTMLElement>
        | React.MouseEvent<HTMLElement>
        | React.TouchEvent<HTMLElement>,
    ) => {
      if (disabled) {
        return;
      }

      const point = getTouchLikeEvent(event);
      longPressTriggeredRef.current = false;
      startPointRef.current = point;
      clearTimer();

      timeoutRef.current = setTimeout(() => {
        longPressTriggeredRef.current = true;
        onLongPress();
      }, delay);
    },
    [clearTimer, delay, disabled, onLongPress],
  );

  const move = useCallback(
    (
      event:
        | React.PointerEvent<HTMLElement>
        | React.MouseEvent<HTMLElement>
        | React.TouchEvent<HTMLElement>,
    ) => {
      if (!startPointRef.current || disabled) {
        return;
      }

      const point = getTouchLikeEvent(event);
      if (!point) {
        return;
      }

      const deltaX = Math.abs(point.clientX - startPointRef.current.clientX);
      const deltaY = Math.abs(point.clientY - startPointRef.current.clientY);

      if (deltaX > moveThreshold || deltaY > moveThreshold) {
        cancel();
      }
    },
    [cancel, disabled, moveThreshold],
  );

  const end = useCallback(() => {
    const triggered = longPressTriggeredRef.current;
    clearTimer();
    startPointRef.current = null;

    if (!triggered) {
      onPress?.();
    }

    longPressTriggeredRef.current = false;
  }, [clearTimer, onPress]);

  useEffect(() => cancel, [cancel]);

  return {
    onPointerDown: start,
    onPointerMove: move,
    onPointerUp: end,
    onPointerLeave: cancel,
    onPointerCancel: cancel,
  };
}
