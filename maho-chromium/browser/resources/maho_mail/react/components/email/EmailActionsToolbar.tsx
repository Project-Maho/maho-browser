import { useEffect, useMemo, useRef, useState } from "react";
import {
  Loader2,
  MoreHorizontal,
  Pencil,
  Check,
  GripVertical,
  X,
} from "lucide-react";
import { useTranslation } from "react-i18next";
import {
  DndContext,
  KeyboardSensor,
  PointerSensor,
  closestCenter,
  useSensor,
  useSensors,
  type DragEndEvent,
} from "@dnd-kit/core";
import {
  SortableContext,
  arrayMove,
  horizontalListSortingStrategy,
  sortableKeyboardCoordinates,
  useSortable,
} from "@dnd-kit/sortable";
import { CSS } from "@dnd-kit/utilities";
import { Popover, PopoverContent, PopoverTrigger } from "../ui/popover";
import { useToolbarConfig } from "../../hooks/useToolbarConfig";
import { ACTION_BY_ID, type ActionId, type ActionSpec } from "../../types/toolbar";

export const CONTEXT_MENU_WIDTH = 200;
export const CONTEXT_MENU_HEIGHT = 120;

export interface ClampedPosition {
  x: number;
  y: number;
  left: number;
  top: number;
}

export function clampContextMenuPosition(
  x: number,
  y: number,
  menuWidth: number = CONTEXT_MENU_WIDTH,
  menuHeight: number = CONTEXT_MENU_HEIGHT,
  viewportWidth: number = typeof window !== "undefined" ? window.innerWidth : 1024,
  viewportHeight: number = typeof window !== "undefined" ? window.innerHeight : 768,
): ClampedPosition {
  const maxX = Math.max(0, viewportWidth - menuWidth);
  const maxY = Math.max(0, viewportHeight - menuHeight);
  const clampedX = Math.max(0, Math.min(x, maxX));
  const clampedY = Math.max(0, Math.min(y, maxY));
  return {
    x: clampedX,
    y: clampedY,
    left: clampedX,
    top: clampedY,
  };
}

export interface EmailActionsToolbarProps {
  onReply: () => void;
  onReplyWithAI: () => void;
  onForward: () => void;
  onPrint: () => void;
  onDelete: () => void;
  onPin?: () => void;
  onSnooze?: () => void;
  onReminder?: () => void;
  onDelegate?: () => void;
  onToggleMute?: () => void;
  onPopOut?: () => void;
  onToggleStar?: () => void;
  draftLoading?: boolean;
  isThreadMuted?: boolean;
  isStarred?: boolean;
}

interface ContextMenuState {
  x: number;
  y: number;
  actionId: ActionId;
  zone: "primary" | "overflow";
}

function shouldShowLabelInPrimary(spec: ActionSpec): boolean {
  if (spec.variant === "primary" || spec.variant === "destructive") return true;
  return spec.id === "reply" || spec.id === "forward";
}

function primaryButtonClassName(variant: ActionSpec["variant"]): string {
  switch (variant) {
    case "primary":
      return "mail-pressable flex h-9 items-center gap-2 rounded-lg bg-primary/10 px-3 text-sm font-medium text-primary transition-colors hover:bg-primary/15 disabled:opacity-50";
    case "destructive":
      return "mail-pressable flex h-9 items-center gap-2 rounded-lg px-3 text-sm text-muted-foreground transition-colors hover:bg-destructive/10 hover:text-destructive";
    default:
      return "mail-pressable flex h-9 items-center gap-2 rounded-lg px-3 text-sm text-foreground transition-colors hover:bg-surface-hover";
  }
}

interface SortablePrimaryProps {
  id: ActionId;
  children: React.ReactNode;
}

function SortablePrimary({ id, children }: SortablePrimaryProps) {
  const { attributes, listeners, setNodeRef, transform, transition, isDragging } = useSortable({ id });
  return (
    <div
      ref={setNodeRef}
      style={{
        transform: CSS.Transform.toString(transform),
        transition,
        opacity: isDragging ? 0.4 : 1,
      }}
      className="relative inline-flex items-center"
      {...attributes}
      {...listeners}
    >
      {children}
    </div>
  );
}

export function EmailActionsToolbar(props: EmailActionsToolbarProps) {
  const {
    onReply,
    onReplyWithAI,
    onForward,
    onPrint,
    onDelete,
    onPin,
    onSnooze,
    onReminder,
    onDelegate,
    onToggleMute,
    onPopOut,
    onToggleStar,
    draftLoading,
    isThreadMuted,
    isStarred,
  } = props;

  const { t } = useTranslation();
  const { config, hideAction, moveToPrimary, moveToOverflow, setPrimary } = useToolbarConfig();
  const [overflowOpen, setOverflowOpen] = useState(false);
  const [editMode, setEditMode] = useState(false);
  const [contextMenu, setContextMenu] = useState<ContextMenuState | null>(null);
  const contextMenuRef = useRef<HTMLDivElement | null>(null);

  const sensors = useSensors(
    useSensor(PointerSensor, { activationConstraint: { distance: 4 } }),
    useSensor(KeyboardSensor, { coordinateGetter: sortableKeyboardCoordinates }),
  );

  const handlerMap = useMemo<Partial<Record<ActionId, () => void>>>(() => ({
    "reply": onReply,
    "reply-ai": onReplyWithAI,
    "forward": onForward,
    "star": onToggleStar,
    "delete": onDelete,
    "print": onPrint,
    "pin": onPin,
    "snooze": onSnooze,
    "remind": onReminder,
    "delegate": onDelegate,
    "mute": onToggleMute,
    "pop-out": onPopOut,
  }), [onReply, onReplyWithAI, onForward, onToggleStar, onDelete, onPrint, onPin, onSnooze, onReminder, onDelegate, onToggleMute, onPopOut]);

  const toggleState = useMemo<Partial<Record<ActionId, boolean>>>(() => ({
    "star": !!isStarred,
    "mute": !!isThreadMuted,
  }), [isStarred, isThreadMuted]);

  const loadingState = useMemo<Partial<Record<ActionId, boolean>>>(() => ({
    "reply-ai": !!draftLoading,
  }), [draftLoading]);

  useEffect(() => {
    if (!contextMenu) return;
    const close = (e: MouseEvent) => {
      if (contextMenuRef.current && !contextMenuRef.current.contains(e.target as Node)) {
        setContextMenu(null);
      }
    };
    const closeOnEsc = (e: KeyboardEvent) => {
      if (e.key === "Escape") setContextMenu(null);
    };
    document.addEventListener("mousedown", close);
    document.addEventListener("keydown", closeOnEsc);
    return () => {
      document.removeEventListener("mousedown", close);
      document.removeEventListener("keydown", closeOnEsc);
    };
  }, [contextMenu]);

  function openContextMenu(e: React.MouseEvent, actionId: ActionId, zone: "primary" | "overflow") {
    e.preventDefault();
    e.stopPropagation();
    const clamped = clampContextMenuPosition(e.clientX, e.clientY);
    setContextMenu({ x: clamped.x, y: clamped.y, actionId, zone });
  }

  function resolveLabel(spec: ActionSpec): string {
    if (spec.hasToggle && toggleState[spec.id]) {
      const activeKey = `${spec.labelKey}.active`;
      const activeLabel = t(activeKey);
      if (activeLabel !== activeKey) return activeLabel;
    }
    return t(spec.labelKey);
  }

  function handleHorizontalDragEnd(event: DragEndEvent) {
    const { active, over } = event;
    if (!over || active.id === over.id) return;
    const ids = config.primary;
    const from = ids.indexOf(active.id as ActionId);
    const to = ids.indexOf(over.id as ActionId);
    if (from < 0 || to < 0) return;
    setPrimary(arrayMove(ids, from, to));
  }

  function renderPrimaryButton(id: ActionId): React.ReactNode {
    const spec = ACTION_BY_ID[id];
    const handler = handlerMap[id];
    if (!spec || !handler) return null;

    const label = resolveLabel(spec);
    const loading = loadingState[id] ?? false;
    const toggled = toggleState[id] ?? false;
    const showLabel = shouldShowLabelInPrimary(spec);

    const Icon = spec.icon;
    const iconClassName = spec.id === "star" && toggled ? "fill-amber-400 text-amber-400" : undefined;

    return (
      <button
        type="button"
        onClick={editMode ? undefined : handler}
        onContextMenu={(e) => openContextMenu(e, id, "primary")}
        disabled={loading || editMode}
        aria-label={label}
        title={label}
        className={primaryButtonClassName(spec.variant)}
      >
        {loading ? (
          <Loader2 size={16} className="animate-spin" />
        ) : (
          <Icon size={16} className={iconClassName} />
        )}
        {showLabel && label}
      </button>
    );
  }

  function renderOverflowItem(id: ActionId): React.ReactNode {
    const spec = ACTION_BY_ID[id];
    const handler = handlerMap[id];
    if (!spec || !handler) return null;

    const label = resolveLabel(spec);
    const Icon = spec.icon;

    return (
      <button
        key={id}
        type="button"
        role="menuitem"
        onClick={() => {
          setOverflowOpen(false);
          handler();
        }}
        onContextMenu={(e) => {
          setOverflowOpen(false);
          openContextMenu(e, id, "overflow");
        }}
        className="flex w-full items-center gap-2 rounded-md px-2 py-1.5 text-left text-sm text-foreground transition-colors hover:bg-surface-hover"
      >
        <Icon size={14} />
        <span className="flex-1">{label}</span>
      </button>
    );
  }

  const renderablePrimary = config.primary.filter((id) => handlerMap[id] !== undefined);
  const renderableOverflow = config.overflow.filter((id) => handlerMap[id] !== undefined);

  const a11yLabel = t("a11y.emailActions") !== "a11y.emailActions" ? t("a11y.emailActions") : "Email actions";
  const moreLabel = t("email.actions.more") !== "email.actions.more" ? t("email.actions.more") : "More actions";
  const editLabel = editMode
    ? (t("email.actions.done") !== "email.actions.done" ? t("email.actions.done") : "Done")
    : (t("email.actions.edit") !== "email.actions.edit" ? t("email.actions.edit") : "Edit");
  const customizeLabel = t("email.actions.customize") !== "email.actions.customize" ? t("email.actions.customize") : "Customize toolbar…";

  return (
    <>
      <div
        role="toolbar"
        aria-label={a11yLabel}
        className="sticky bottom-0 z-10 flex items-center gap-2 border-t border-border bg-background p-4"
      >
        {editMode ? (
          <DndContext sensors={sensors} collisionDetection={closestCenter} onDragEnd={handleHorizontalDragEnd}>
            <SortableContext items={renderablePrimary} strategy={horizontalListSortingStrategy}>
              <div className="flex items-center gap-2">
                {renderablePrimary.map((id) => (
                  <div key={id} className="relative">
                    <SortablePrimary id={id}>
                      <span className="flex items-center gap-1 rounded-lg border border-dashed border-primary/40 bg-card/50 px-2 py-1.5 text-xs text-foreground">
                        <GripVertical size={12} className="cursor-grab text-muted-foreground" />
                        {resolveLabel(ACTION_BY_ID[id])}
                      </span>
                    </SortablePrimary>
                    <button
                      type="button"
                      onClick={() => hideAction(id)}
                      aria-label={`Hide ${resolveLabel(ACTION_BY_ID[id])}`}
                      className="absolute -right-1.5 -top-1.5 flex h-5 w-5 items-center justify-center rounded-full border border-destructive/60 bg-background text-destructive transition-colors hover:bg-destructive hover:text-destructive-foreground"
                    >
                      <X size={10} />
                    </button>
                  </div>
                ))}
              </div>
            </SortableContext>
          </DndContext>
        ) : (
          <>
            {renderablePrimary.map((id) => (
              <span key={id}>{renderPrimaryButton(id)}</span>
            ))}

            {renderableOverflow.length > 0 && (
              <Popover open={overflowOpen} onOpenChange={setOverflowOpen}>
                <PopoverTrigger asChild>
                  <button
                    type="button"
                    aria-label={moreLabel}
                    aria-haspopup="menu"
                    aria-expanded={overflowOpen}
                    onContextMenu={(e) => {
                      if (renderableOverflow.length === 0) return;
                      openContextMenu(e, renderableOverflow[0], "overflow");
                    }}
                    className="mail-pressable flex h-9 items-center gap-2 rounded-lg px-3 text-sm text-foreground hover:bg-surface-hover"
                  >
                    <MoreHorizontal size={16} />
                  </button>
                </PopoverTrigger>
                <PopoverContent align="start" sideOffset={4} className="w-48 p-1">
                  <div role="menu">
                    {renderableOverflow.map(renderOverflowItem)}
                  </div>
                </PopoverContent>
              </Popover>
            )}
          </>
        )}

        <div className="flex-1" />

        <button
          type="button"
          onClick={() => setEditMode((v) => !v)}
          aria-label={editLabel}
          title={editLabel}
          className={`flex items-center gap-1.5 rounded-lg px-2.5 py-1.5 text-xs transition-colors ${
            editMode
              ? "bg-primary/10 text-primary hover:bg-primary/20"
              : "text-muted-foreground hover:bg-surface-hover hover:text-foreground"
          }`}
        >
          {editMode ? <Check size={12} /> : <Pencil size={12} />}
          {editLabel}
        </button>
      </div>

      {contextMenu && (
        <div
          ref={contextMenuRef}
          role="menu"
          aria-label="Action options"
          className="fixed z-[60] min-w-[200px] rounded-lg border border-border bg-card py-1 shadow-xl"
          style={(() => {
            const clamped = clampContextMenuPosition(contextMenu.x, contextMenu.y);
            return { left: clamped.left, top: clamped.top };
          })()}
        >
          <button
            type="button"
            role="menuitem"
            onClick={() => {
              hideAction(contextMenu.actionId);
              setContextMenu(null);
            }}
            className="flex w-full items-center gap-2 px-3 py-1.5 text-left text-sm text-foreground hover:bg-surface-hover"
          >
            Hide
          </button>
          {contextMenu.zone === "primary" ? (
            <button
              type="button"
              role="menuitem"
              onClick={() => {
                moveToOverflow(contextMenu.actionId);
                setContextMenu(null);
              }}
              className="flex w-full items-center gap-2 px-3 py-1.5 text-left text-sm text-foreground hover:bg-surface-hover"
            >
              Move to More menu
            </button>
          ) : (
            <button
              type="button"
              role="menuitem"
              onClick={() => {
                moveToPrimary(contextMenu.actionId);
                setContextMenu(null);
              }}
              className="flex w-full items-center gap-2 px-3 py-1.5 text-left text-sm text-foreground hover:bg-surface-hover"
            >
              Move to Visible
            </button>
          )}
          <div className="my-1 h-px bg-border" />
          <button
            type="button"
            role="menuitem"
            onClick={() => {
              setContextMenu(null);
              window.open("chrome://maho-settings?pane=mail-behavior");
            }}
            className="flex w-full items-center gap-2 px-3 py-1.5 text-left text-sm text-foreground hover:bg-surface-hover"
          >
            {customizeLabel}
          </button>
        </div>
      )}
    </>
  );
}
