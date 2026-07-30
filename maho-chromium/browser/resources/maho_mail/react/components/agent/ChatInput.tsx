import { useEffect, useRef, useState } from "react";
import { SendHorizontal, Square } from "lucide-react";

import { Button } from "../ui/Button";
import { Textarea } from "../ui/Textarea";

interface ChatInputProps {
  onSend: (content: string) => void;
  disabled: boolean;
  loading?: boolean;
  onCancel?: () => void;
}

export function ChatInput({ onSend, disabled, loading = false, onCancel }: ChatInputProps) {
  const [content, setContent] = useState("");
  const textareaRef = useRef<HTMLTextAreaElement | null>(null);

  useEffect(() => {
    const textarea = textareaRef.current;
    if (!textarea) {
      return;
    }

    textarea.style.height = "auto";
    const nextHeight = Math.min(textarea.scrollHeight, 120);
    textarea.style.height = `${nextHeight}px`;
    textarea.style.overflowY = textarea.scrollHeight > 120 ? "auto" : "hidden";
  }, [content]);

  const handleSubmit = () => {
    const trimmed = content.trim();
    if (!trimmed || disabled) {
      return;
    }

    onSend(trimmed);
    setContent("");
  };

  return (
    <div className="border-t border-border bg-background/95 p-3 backdrop-blur">
      <div className="flex items-end gap-2 rounded-2xl border border-border bg-card/70 p-2 shadow-sm">
        <Textarea
          ref={textareaRef}
          value={content}
          onChange={setContent}
          disabled={disabled && !loading}
          placeholder="Ask Maho Agent anything about this account…"
          rows={1}
          className="max-h-[120px] min-h-[40px] resize-none border-0 bg-transparent px-2 py-2 text-sm shadow-none focus-visible:ring-0"
          onKeyDown={(event) => {
            if (event.key === "Enter" && !event.shiftKey) {
              event.preventDefault();
              handleSubmit();
            }
          }}
        />
        {loading && onCancel ? (
          <Button
            type="button"
            size="icon"
            onClick={onCancel}
            className="h-10 w-10 shrink-0 rounded-full bg-destructive/10 hover:bg-destructive/20 text-destructive border border-destructive/20"
            aria-label="Cancel generating message"
          >
            <Square className="h-4 w-4 fill-current" />
          </Button>
        ) : (
          <Button
            type="button"
            size="icon"
            onClick={handleSubmit}
            disabled={disabled || content.trim().length === 0}
            className="h-10 w-10 shrink-0 rounded-full"
            aria-label="Send message"
          >
            <SendHorizontal className="h-4 w-4" />
          </Button>
        )}
      </div>
      <p className="mt-2 px-1 text-[11px] text-muted-foreground">
        Enter to send • Shift+Enter for a new line
      </p>
    </div>
  );
}
