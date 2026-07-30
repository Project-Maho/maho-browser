import type { ComponentType } from "react";
import {
  Reply,
  Forward,
  Sparkles,
  Trash2,
  Printer,
  Pin,
  Clock,
  BellRing,
  BellOff,
  Maximize2,
  Star,
  Send,
} from "lucide-react";

export type ActionId =
  | "reply"
  | "reply-ai"
  | "forward"
  | "star"
  | "delete"
  | "print"
  | "pin"
  | "snooze"
  | "remind"
  | "delegate"
  | "mute"
  | "pop-out";

export type ActionVariant = "default" | "primary" | "destructive";

export interface ActionSpec {
  id: ActionId;
  labelKey: string;
  icon: ComponentType<{ size?: number; className?: string }>;
  variant: ActionVariant;
  defaultShortcut: string | null;
  hasLoading: boolean;
  hasToggle: boolean;
}

export const ACTION_REGISTRY: readonly ActionSpec[] = [
  { id: "reply",     labelKey: "email.actions.reply",     icon: Reply,     variant: "default",     defaultShortcut: "r",       hasLoading: false, hasToggle: false },
  { id: "reply-ai",  labelKey: "email.actions.replyAi",   icon: Sparkles,  variant: "primary",     defaultShortcut: null,      hasLoading: true,  hasToggle: false },
  { id: "forward",   labelKey: "email.actions.forward",   icon: Forward,   variant: "default",     defaultShortcut: "f",       hasLoading: false, hasToggle: false },
  { id: "star",      labelKey: "email.actions.star",      icon: Star,      variant: "default",     defaultShortcut: "s",       hasLoading: false, hasToggle: true  },
  { id: "delete",    labelKey: "email.actions.delete",    icon: Trash2,    variant: "destructive", defaultShortcut: "Delete",  hasLoading: false, hasToggle: false },
  { id: "print",     labelKey: "email.actions.print",     icon: Printer,   variant: "default",     defaultShortcut: null,      hasLoading: false, hasToggle: false },
  { id: "pin",       labelKey: "email.actions.pin",       icon: Pin,       variant: "default",     defaultShortcut: null,      hasLoading: false, hasToggle: false },
  { id: "snooze",    labelKey: "email.actions.snooze",    icon: Clock,     variant: "default",     defaultShortcut: null,      hasLoading: false, hasToggle: false },
  { id: "remind",    labelKey: "email.actions.remind",    icon: BellRing,  variant: "default",     defaultShortcut: null,      hasLoading: false, hasToggle: false },
  { id: "delegate",  labelKey: "email.actions.delegate",  icon: Send,      variant: "default",     defaultShortcut: null,      hasLoading: false, hasToggle: false },
  { id: "mute",      labelKey: "email.actions.mute",      icon: BellOff,   variant: "default",     defaultShortcut: null,      hasLoading: false, hasToggle: true  },
  { id: "pop-out",   labelKey: "email.actions.popOut",    icon: Maximize2, variant: "default",     defaultShortcut: null,      hasLoading: false, hasToggle: false },
];

export const ACTION_BY_ID: Readonly<Record<ActionId, ActionSpec>> = Object.fromEntries(
  ACTION_REGISTRY.map((s) => [s.id, s]),
) as Record<ActionId, ActionSpec>;

export const ALL_ACTION_IDS: readonly ActionId[] = ACTION_REGISTRY.map((s) => s.id);

export interface ToolbarConfig {
  primary: ActionId[];
  overflow: ActionId[];
  shortcuts: Record<ActionId, string | null>;
}

export const DEFAULT_TOOLBAR_CONFIG: ToolbarConfig = {
  primary: ["reply", "reply-ai", "forward", "star", "delete"],
  overflow: ["print", "pin", "snooze", "remind", "delegate", "mute", "pop-out"],
  shortcuts: Object.fromEntries(
    ACTION_REGISTRY.map((s) => [s.id, s.defaultShortcut]),
  ) as Record<ActionId, string | null>,
};

export function isActionId(value: unknown): value is ActionId {
  return typeof value === "string" && (ALL_ACTION_IDS as readonly string[]).includes(value);
}

export function sanitizeConfig(input: unknown): ToolbarConfig {
  if (!input || typeof input !== "object") return DEFAULT_TOOLBAR_CONFIG;
  const obj = input as Record<string, unknown>;
  const primary = Array.isArray(obj.primary) ? obj.primary.filter(isActionId) : DEFAULT_TOOLBAR_CONFIG.primary;
  const overflow = Array.isArray(obj.overflow) ? obj.overflow.filter(isActionId) : DEFAULT_TOOLBAR_CONFIG.overflow;

  const seen = new Set<ActionId>();
  const dedupedPrimary = primary.filter((id) => (seen.has(id) ? false : (seen.add(id), true)));
  const dedupedOverflow = overflow.filter((id) => (seen.has(id) ? false : (seen.add(id), true)));

  const shortcuts: Record<ActionId, string | null> = { ...DEFAULT_TOOLBAR_CONFIG.shortcuts };
  if (obj.shortcuts && typeof obj.shortcuts === "object") {
    for (const [key, value] of Object.entries(obj.shortcuts as Record<string, unknown>)) {
      if (isActionId(key)) {
        shortcuts[key] = typeof value === "string" && value.length > 0 ? value : null;
      }
    }
  }

  return { primary: dedupedPrimary, overflow: dedupedOverflow, shortcuts };
}
