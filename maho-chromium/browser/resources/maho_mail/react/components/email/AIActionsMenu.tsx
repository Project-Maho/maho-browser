import { useState } from "react";
import {
  Sparkles,
  ChevronDown,
  Reply as ReplyIcon,
  Tag,
  Languages,
  Loader2,
} from "lucide-react";
import { Popover, PopoverContent, PopoverTrigger } from "../ui/popover";

export interface AIActionsMenuProps {
  onSummarize: () => void | Promise<void>;
  onDraftReply: () => void | Promise<void>;
  onClassify: () => void | Promise<void>;
  onTranslate: () => void | Promise<void>;
  summaryLoading?: boolean;
  draftLoading?: boolean;
  classifyLoading?: boolean;
  isTranslating?: boolean;
  isModelLoading?: boolean;
  aiError?: string | null;
}

interface MenuItemSpec {
  key: string;
  label: string;
  loadingLabel?: string;
  icon: React.ReactNode;
  loading?: boolean;
  onSelect: () => void | Promise<void>;
}

export function AIActionsMenu(props: AIActionsMenuProps) {
  const {
    onSummarize,
    onDraftReply,
    onClassify,
    onTranslate,
    summaryLoading,
    draftLoading,
    classifyLoading,
    isTranslating,
    isModelLoading,
    aiError,
  } = props;

  const [open, setOpen] = useState(false);

  const translateLoading = Boolean(isTranslating || isModelLoading);
  const translateLabel = isModelLoading ? "Loading Model…" : "Translate";

  const items: MenuItemSpec[] = [
    {
      key: "summarize",
      label: "Summarize",
      icon: <Sparkles size={14} />,
      loading: summaryLoading,
      onSelect: onSummarize,
    },
    {
      key: "draft-reply",
      label: "Draft Reply",
      icon: <ReplyIcon size={14} />,
      loading: draftLoading,
      onSelect: onDraftReply,
    },
    {
      key: "classify",
      label: "Classify",
      icon: <Tag size={14} />,
      loading: classifyLoading,
      onSelect: onClassify,
    },
    {
      key: "translate",
      label: translateLabel,
      icon: <Languages size={14} />,
      loading: translateLoading,
      onSelect: onTranslate,
    },
  ];

  const handleSelect = (item: MenuItemSpec) => {
    if (item.loading) return;
    setOpen(false);
    void item.onSelect();
  };

  return (
    <Popover open={open} onOpenChange={setOpen}>
      <PopoverTrigger asChild>
        <button
          type="button"
          aria-label="AI actions"
          aria-haspopup="menu"
          aria-expanded={open}
          className="flex items-center gap-1.5 mail-pressable flex h-8 rounded-lg border border-border bg-background/70 px-2.5 text-xs font-medium text-muted-foreground hover:border-foreground/20 hover:bg-surface-hover hover:text-foreground"
        >
          <Sparkles size={14} className="text-primary" />
          AI
          <ChevronDown size={12} className="opacity-60" />
        </button>
      </PopoverTrigger>
      <PopoverContent align="end" sideOffset={4} className="w-56 p-1">
        <div role="menu" aria-label="AI actions">
          {items.map((item) => (
            <button
              key={item.key}
              type="button"
              role="menuitem"
              aria-disabled={item.loading ? "true" : "false"}
              onClick={() => handleSelect(item)}
              disabled={item.loading}
              className="flex w-full items-center gap-2 rounded-md px-2 py-1.5 text-left text-sm text-foreground transition-colors hover:bg-surface-hover disabled:opacity-50 disabled:cursor-not-allowed"
            >
              {item.loading ? (
                <Loader2 size={14} className="animate-spin" />
              ) : (
                item.icon
              )}
              <span className="flex-1">{item.label}</span>
            </button>
          ))}
          {aiError ? (
            <div className="mt-1 border-t border-border px-2 pt-1 text-xs text-destructive">
              {aiError}
            </div>
          ) : null}
        </div>
      </PopoverContent>
    </Popover>
  );
}
