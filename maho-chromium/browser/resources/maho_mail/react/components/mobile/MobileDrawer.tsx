import { useEffect, useRef, useState, type ReactNode, type TouchEvent } from "react";

import { useFocusTrap } from "../../hooks/useFocusTrap";
import { cn } from "../../lib/utils";

interface MobileDrawerProps {
  isOpen: boolean;
  onClose: () => void;
  children: ReactNode;
}

const SWIPE_CLOSE_THRESHOLD = 50;

export function MobileDrawer({ isOpen, onClose, children }: MobileDrawerProps) {
  const drawerRef = useRef<HTMLDivElement>(null);
  const [touchStartX, setTouchStartX] = useState<number | null>(null);
  const [dragOffset, setDragOffset] = useState(0);

  useFocusTrap(drawerRef, isOpen);

  useEffect(() => {
    if (!isOpen) {
      setTouchStartX(null);
      setDragOffset(0);
      return undefined;
    }

    const previousOverflow = document.body.style.overflow;
    document.body.style.overflow = "hidden";

    return () => {
      document.body.style.overflow = previousOverflow;
    };
  }, [isOpen]);

  useEffect(() => {
    if (!isOpen) {
      return undefined;
    }

    const handleKeyDown = (event: KeyboardEvent) => {
      if (event.key === "Escape") {
        onClose();
      }
    };

    document.addEventListener("keydown", handleKeyDown);

    return () => {
      document.removeEventListener("keydown", handleKeyDown);
    };
  }, [isOpen, onClose]);

  const handleTouchStart = (event: TouchEvent<HTMLDivElement>) => {
    if (!isOpen) {
      return;
    }

    setTouchStartX(event.touches[0]?.clientX ?? null);
    setDragOffset(0);
  };

  const handleTouchMove = (event: TouchEvent<HTMLDivElement>) => {
    if (!isOpen || touchStartX == null) {
      return;
    }

    const currentX = event.touches[0]?.clientX ?? touchStartX;
    const nextOffset = Math.min(0, currentX - touchStartX);
    setDragOffset(nextOffset);
  };

  const handleTouchEnd = () => {
    if (!isOpen) {
      return;
    }

    if (dragOffset <= -SWIPE_CLOSE_THRESHOLD) {
      onClose();
    }

    setTouchStartX(null);
    setDragOffset(0);
  };

  const drawerTransform = isOpen ? `translateX(${dragOffset}px)` : "translateX(-100%)";

  return (
    <div
      className={cn(
        "fixed inset-0 z-50 md:hidden",
        isOpen ? "pointer-events-auto" : "pointer-events-none",
      )}
      aria-hidden={!isOpen}
    >
      <button
        type="button"
        aria-label="Close navigation drawer"
        className={cn(
          "absolute inset-0 bg-black/50 transition-opacity duration-300",
          isOpen ? "opacity-100" : "opacity-0",
        )}
        onClick={onClose}
      />

      <div
        ref={drawerRef}
        role="dialog"
        aria-modal="true"
        aria-label="Mailbox folders"
        tabIndex={-1}
        className="absolute inset-y-0 left-0 flex h-full w-[80%] max-w-[320px] flex-col overflow-hidden border-r border-border bg-background shadow-2xl transition-transform duration-300"
        style={{ transform: drawerTransform }}
        onTouchStart={handleTouchStart}
        onTouchMove={handleTouchMove}
        onTouchEnd={handleTouchEnd}
        onTouchCancel={handleTouchEnd}
      >
        <div
          className="h-full overflow-y-auto overscroll-contain"
          style={{
            paddingTop: "env(safe-area-inset-top)",
            paddingLeft: "env(safe-area-inset-left)",
            paddingRight: "env(safe-area-inset-right)",
            paddingBottom: "env(safe-area-inset-bottom)",
          }}
        >
          {children}
        </div>
      </div>
    </div>
  );
}
