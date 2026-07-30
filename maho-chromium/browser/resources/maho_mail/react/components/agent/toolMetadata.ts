import {
  BookOpen,
  Search,
  Sparkles,
  PencilLine,
  Wrench,
  type LucideIcon,
} from "lucide-react";

export type ToolCategory = "read" | "search" | "ai" | "write" | "other";

const READ_TOOLS = new Set(["list_emails", "read_email", "get_thread", "list_folders"]);
const SEARCH_TOOLS = new Set(["search_emails"]);
const AI_TOOLS = new Set(["summarize_email", "draft_reply"]);
const WRITE_TOOLS = new Set(["move_email", "star_email"]);

export function getToolCategory(name: string): ToolCategory {
  if (READ_TOOLS.has(name)) {
    return "read";
  }

  if (SEARCH_TOOLS.has(name)) {
    return "search";
  }

  if (AI_TOOLS.has(name)) {
    return "ai";
  }

  if (WRITE_TOOLS.has(name)) {
    return "write";
  }

  return "other";
}

export function getCategoryStyles(category: ToolCategory) {
  switch (category) {
    case "read":
      return {
        card: "border-sky-200/80 bg-sky-50/80 dark:border-sky-400/20 dark:bg-sky-500/10",
        icon: "bg-sky-100 text-sky-700 dark:bg-sky-500/15 dark:text-sky-200",
        badge: "border-sky-200/80 bg-sky-100/80 text-sky-700 dark:border-sky-400/20 dark:bg-sky-500/15 dark:text-sky-200",
        accent: "text-sky-700 dark:text-sky-200",
      };
    case "search":
      return {
        card: "border-violet-200/80 bg-violet-50/80 dark:border-violet-400/20 dark:bg-violet-500/10",
        icon: "bg-violet-100 text-violet-700 dark:bg-violet-500/15 dark:text-violet-200",
        badge: "border-violet-200/80 bg-violet-100/80 text-violet-700 dark:border-violet-400/20 dark:bg-violet-500/15 dark:text-violet-200",
        accent: "text-violet-700 dark:text-violet-200",
      };
    case "ai":
      return {
        card: "border-emerald-200/80 bg-emerald-50/80 dark:border-emerald-400/20 dark:bg-emerald-500/10",
        icon: "bg-emerald-100 text-emerald-700 dark:bg-emerald-500/15 dark:text-emerald-200",
        badge: "border-emerald-200/80 bg-emerald-100/80 text-emerald-700 dark:border-emerald-400/20 dark:bg-emerald-500/15 dark:text-emerald-200",
        accent: "text-emerald-700 dark:text-emerald-200",
      };
    case "write":
      return {
        card: "border-amber-200/80 bg-amber-50/80 dark:border-amber-400/20 dark:bg-amber-500/10",
        icon: "bg-amber-100 text-amber-700 dark:bg-amber-500/15 dark:text-amber-200",
        badge: "border-amber-200/80 bg-amber-100/80 text-amber-700 dark:border-amber-400/20 dark:bg-amber-500/15 dark:text-amber-200",
        accent: "text-amber-700 dark:text-amber-200",
      };
    default:
      return {
        card: "border-border/70 bg-background/80 dark:bg-muted/10",
        icon: "bg-muted text-foreground dark:bg-muted/50 dark:text-foreground",
        badge: "border-border/70 bg-background/80 text-foreground dark:bg-muted/40 dark:text-foreground",
        accent: "text-foreground",
      };
  }
}

export function getToolIcon(category: ToolCategory): LucideIcon {
  switch (category) {
    case "read":
      return BookOpen;
    case "search":
      return Search;
    case "ai":
      return Sparkles;
    case "write":
      return PencilLine;
    default:
      return Wrench;
  }
}

export function getToolLabel(name: string): string {
  return name
    .split("_")
    .filter(Boolean)
    .map((part) => part.charAt(0).toUpperCase() + part.slice(1))
    .join(" ");
}
