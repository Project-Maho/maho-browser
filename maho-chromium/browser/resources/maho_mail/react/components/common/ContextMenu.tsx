import { useCallback, useEffect, useRef, useState } from "react";
import type { EmailSummary } from "../../types";

interface ContextMenuProps {
  children: React.ReactNode;
  email: EmailSummary;
  onMarkRead?: (id: string) => void;
  onMarkUnread?: (id: string) => void;
  onToggleStar?: (id: string) => void;
  onPin?: (id: string, nextPinned: boolean) => void | Promise<void>;
  isPinned?: boolean;
  onSnooze?: (id: string) => void;
  onDelete?: (id: string) => void;
  onArchive?: (id: string) => void;
  onReply?: (id: string) => void;
  onForward?: (id: string) => void;
}

interface MenuPosition {
  x: number;
  y: number;
}

export function ContextMenu({
  children,
  email,
  onMarkRead,
  onMarkUnread,
  onToggleStar,
  onPin,
  isPinned = false,
  onSnooze,
  onDelete,
  onArchive,
  onReply,
  onForward,
}: ContextMenuProps) {
  const [position, setPosition] = useState<MenuPosition | null>(null);
  const menuRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    if (!position || !menuRef.current) return;
    const rect = menuRef.current.getBoundingClientRect();
    const pad = 8;
    let x = position.x;
    let y = position.y;
    if (x + rect.width > window.innerWidth - pad) x = window.innerWidth - rect.width - pad;
    if (y + rect.height > window.innerHeight - pad) y = window.innerHeight - rect.height - pad;
    if (x < pad) x = pad;
    if (y < pad) y = pad;
    if (x !== position.x || y !== position.y) {
      setPosition({ x, y });
    }
  }, [position]);

  useEffect(() => {
    if (position && menuRef.current) {
      const first = menuRef.current.querySelector('[role="menuitem"]') as HTMLElement | null;
      first?.focus();
    }
  }, [position]);

  const close = useCallback(() => setPosition(null), []);

  const handleKeyDown = useCallback((e: React.KeyboardEvent) => {
    const items = menuRef.current?.querySelectorAll('[role="menuitem"]');
    if (!items?.length) return;
    const currentIndex = Array.from(items).indexOf(document.activeElement as Element);

    if (e.key === "ArrowDown") {
      e.preventDefault();
      const next = currentIndex < items.length - 1 ? currentIndex + 1 : 0;
      (items[next] as HTMLElement).focus();
    } else if (e.key === "ArrowUp") {
      e.preventDefault();
      const prev = currentIndex > 0 ? currentIndex - 1 : items.length - 1;
      (items[prev] as HTMLElement).focus();
    } else if (e.key === "Escape") {
      e.preventDefault();
      close();
    }
  }, [close]);

  const handleContextMenu = useCallback((e: React.MouseEvent) => {
    e.preventDefault();
    setPosition({ x: e.clientX, y: e.clientY });
  }, []);

  const action = (fn?: (id: string) => void | Promise<void>) => {
    void fn?.(email.id);
    close();
  };

  const pinAction = (fn?: (id: string, nextPinned: boolean) => void | Promise<void>) => {
    void fn?.(email.id, !isPinned);
    close();
  };

  return (
    <>
      <div onContextMenu={handleContextMenu}>{children}</div>
      {position && (
        <>
          <div className="fixed inset-0 z-40" onClick={close} />
          <div
            ref={menuRef}
            role="menu"
            onKeyDown={handleKeyDown}
            className="fixed z-50 min-w-[160px] rounded-xl border border-border bg-card py-1 shadow-xl"
            style={{ left: position.x, top: position.y }}
          >
            {onReply && (
              <button type="button" onClick={() => action(onReply)} className="context-menu-item" role="menuitem" tabIndex={-1}>
                Reply
              </button>
            )}
            {onForward && (
              <button type="button" onClick={() => action(onForward)} className="context-menu-item" role="menuitem" tabIndex={-1}>
                Forward
              </button>
            )}
            <div className="my-1 border-t border-border" role="separator" />
            {email.is_read && onMarkUnread && (
              <button type="button" onClick={() => action(onMarkUnread)} className="context-menu-item" role="menuitem" tabIndex={-1}>
                Mark as unread
              </button>
            )}
            {!email.is_read && onMarkRead && (
              <button type="button" onClick={() => action(onMarkRead)} className="context-menu-item" role="menuitem" tabIndex={-1}>
                Mark as read
              </button>
            )}
            {onToggleStar && (
              <button type="button" onClick={() => action(onToggleStar)} className="context-menu-item" role="menuitem" tabIndex={-1}>
                {email.is_starred ? "Unstar" : "Star"}
              </button>
            )}
            {onPin && (
              <button type="button" onClick={() => pinAction(onPin)} className="context-menu-item" role="menuitem" tabIndex={-1}>
                {isPinned ? "Unpin" : "Pin"}
              </button>
            )}
            {onSnooze && (
              <button type="button" onClick={() => action(onSnooze)} className="context-menu-item" role="menuitem" tabIndex={-1}>
                Snooze
              </button>
            )}
            <div className="my-1 border-t border-border" role="separator" />
            {onArchive && (
              <button type="button" onClick={() => action(onArchive)} className="context-menu-item" role="menuitem" tabIndex={-1}>
                Archive
              </button>
            )}
            {onDelete && (
              <button type="button" onClick={() => action(onDelete)} className="context-menu-item text-destructive hover:text-destructive" role="menuitem" tabIndex={-1}>
                Delete
              </button>
            )}
          </div>
        </>
      )}
    </>
  );
}
