import { useState, useRef, useCallback, useEffect, type DragEvent, type ReactNode } from "react";

interface DraggableEmailProps {
  emailId: string;
  children: ReactNode;
}

const DRAG_HOLD_MS = 300;

export function DraggableEmail({ emailId, children }: DraggableEmailProps) {
  const [dragging, setDragging] = useState(false);
  const [canDrag, setCanDrag] = useState(false);
  const holdTimer = useRef<ReturnType<typeof setTimeout> | null>(null);

  const clearHold = useCallback(() => {
    if (holdTimer.current !== null) {
      clearTimeout(holdTimer.current);
      holdTimer.current = null;
    }
  }, []);

  useEffect(() => clearHold, [clearHold]);

  function handleMouseDown() {
    clearHold();
    holdTimer.current = setTimeout(() => {
      setCanDrag(true);
    }, DRAG_HOLD_MS);
  }

  function handleMouseUp() {
    clearHold();
    if (!dragging) {
      setCanDrag(false);
    }
  }

  function handleDragStart(e: DragEvent) {
    if (!canDrag) {
      e.preventDefault();
      return;
    }
    e.dataTransfer.setData("text/plain", emailId);
    e.dataTransfer.effectAllowed = "move";
    setDragging(true);
  }

  function handleDragEnd() {
    setDragging(false);
    setCanDrag(false);
    clearHold();
  }

  return (
    <div
      draggable={canDrag}
      onMouseDown={handleMouseDown}
      onMouseUp={handleMouseUp}
      onMouseLeave={handleMouseUp}
      onDragStart={handleDragStart}
      onDragEnd={handleDragEnd}
      className={dragging ? "opacity-50 cursor-grabbing" : canDrag ? "cursor-grab" : ""}
    >
      {children}
    </div>
  );
}

interface DropTargetFolderProps {
  folderId: string;
  onDrop: (emailId: string, targetFolderId: string) => void;
  children: ReactNode;
}

export function DropTargetFolder({ folderId, onDrop, children }: DropTargetFolderProps) {
  const [over, setOver] = useState(false);

  function handleDragOver(e: DragEvent) {
    e.preventDefault();
    e.dataTransfer.dropEffect = "move";
    setOver(true);
  }

  function handleDragLeave() {
    setOver(false);
  }

  function handleDrop(e: DragEvent) {
    e.preventDefault();
    setOver(false);
    const emailId = e.dataTransfer.getData("text/plain");
    if (emailId) {
      onDrop(emailId, folderId);
    }
  }

  return (
    <div
      onDragOver={handleDragOver}
      onDragLeave={handleDragLeave}
      onDrop={handleDrop}
      className={over ? "ring-2 ring-inset ring-primary/50 rounded-2xl" : ""}
    >
      {children}
    </div>
  );
}
