import { useRef, useState, type ReactNode } from "react";
import { Archive, Mail, MailOpen, Trash2 } from "lucide-react";

import { cn } from "../../lib/utils";

interface SwipeableRowProps {
  children: ReactNode;
  onSwipeLeft?: () => void;
  onSwipeRight?: () => void;
  leftLabel?: string;
  rightLabel?: string;
  leftIcon?: "archive" | "delete";
  rightIcon?: "read" | "unread";
  disabled?: boolean;
}

const THRESHOLD = 80;
const SWIPE_ACTIVATE_PX = 10;

export function SwipeableRow({
  children,
  onSwipeLeft,
  onSwipeRight,
  leftLabel = "Archive",
  rightLabel = "Mark read",
  leftIcon = "archive",
  rightIcon = "read",
  disabled,
}: SwipeableRowProps) {
  const containerRef = useRef<HTMLDivElement>(null);
  const startX = useRef(0);
  const currentX = useRef(0);
  const pointerId = useRef<number | null>(null);
  const activated = useRef(false);
  const [offset, setOffset] = useState(0);
  const [swiping, setSwiping] = useState(false);

  if (disabled || (!onSwipeLeft && !onSwipeRight)) {
    return <>{children}</>;
  }

  function handlePointerDown(e: React.PointerEvent) {
    startX.current = e.clientX;
    currentX.current = e.clientX;
    pointerId.current = e.pointerId;
    activated.current = false;
    setSwiping(true);
  }

  function handlePointerMove(e: React.PointerEvent) {
    if (!swiping || pointerId.current === null) return;
    currentX.current = e.clientX;
    const dx = currentX.current - startX.current;

    if (!activated.current) {
      if (Math.abs(dx) < SWIPE_ACTIVATE_PX) return;
      activated.current = true;
      (e.currentTarget as HTMLElement).setPointerCapture(pointerId.current);
    }

    const clamped = Math.max(-160, Math.min(160, dx));
    if (!onSwipeRight && clamped > 0) return;
    if (!onSwipeLeft && clamped < 0) return;
    setOffset(clamped);
  }

  function handlePointerUp() {
    if (!swiping) return;
    pointerId.current = null;
    activated.current = false;
    setSwiping(false);
    if (offset < -THRESHOLD && onSwipeLeft) {
      onSwipeLeft();
    } else if (offset > THRESHOLD && onSwipeRight) {
      onSwipeRight();
    }
    setOffset(0);
  }

  const leftActive = offset < -THRESHOLD;
  const rightActive = offset > THRESHOLD;
  const leftProgress = Math.min(1, Math.abs(offset) / THRESHOLD);
  const rightProgress = Math.min(1, Math.abs(offset) / THRESHOLD);
  const LeftIcon = leftIcon === "delete" ? Trash2 : Archive;
  const RightIcon = rightIcon === "unread" ? Mail : MailOpen;

  return (
    <div ref={containerRef} className="relative overflow-hidden">
      {offset < 0 && (
        <div
          className={cn(
            "absolute inset-y-0 right-0 flex items-center justify-end px-4 transition-colors",
            leftActive
              ? leftIcon === "delete"
                ? "bg-red-600"
                : "bg-primary"
              : leftIcon === "delete"
                ? "bg-red-600/40"
                : "bg-primary/40",
          )}
          style={{ width: Math.abs(offset) }}
        >
          <div
            className="flex items-center gap-2 text-primary-foreground transition-transform duration-150"
            style={{
              opacity: 0.45 + leftProgress * 0.55,
              transform: `scale(${0.9 + leftProgress * 0.15})`,
            }}
          >
            <LeftIcon size={18} aria-hidden="true" />
            <span className="text-xs font-medium">{leftLabel}</span>
          </div>
        </div>
      )}

      {offset > 0 && (
        <div
          className={cn(
            "absolute inset-y-0 left-0 flex items-center px-4 transition-colors",
            rightActive ? "bg-emerald-600" : "bg-emerald-600/40",
          )}
          style={{ width: Math.abs(offset) }}
        >
          <div
            className="flex items-center gap-2 text-primary-foreground transition-transform duration-150"
            style={{
              opacity: 0.45 + rightProgress * 0.55,
              transform: `scale(${0.9 + rightProgress * 0.15})`,
            }}
          >
            <RightIcon size={18} aria-hidden="true" />
            <span className="text-xs font-medium">{rightLabel}</span>
          </div>
        </div>
      )}

      <div
        style={{
          transform: `translateX(${offset}px)`,
          transition: swiping ? "none" : "transform 200ms ease-out",
        }}
        onPointerDown={handlePointerDown}
        onPointerMove={handlePointerMove}
        onPointerUp={handlePointerUp}
        onPointerCancel={handlePointerUp}
        className="relative z-10 touch-pan-y"
      >
        {children}
      </div>
    </div>
  );
}
