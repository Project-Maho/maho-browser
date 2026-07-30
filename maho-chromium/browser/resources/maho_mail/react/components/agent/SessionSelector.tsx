import { MessageSquarePlus, Trash2 } from "lucide-react";

import type { AgentChatSession } from "../../types";
import { cn } from "../../lib/utils";
import { Button } from "../ui/Button";
import { formatRelativeDate } from "../ui/utils";

interface SessionSelectorProps {
  sessions: AgentChatSession[];
  currentSessionId: string | null;
  onSelect: (id: string) => void;
  onDelete: (id: string) => void;
  onNew: () => void;
  disabled?: boolean;
}

export function SessionSelector({
  sessions,
  currentSessionId,
  onSelect,
  onDelete,
  onNew,
  disabled = false,
}: SessionSelectorProps) {
  return (
    <div className="flex h-full flex-col border-r border-border bg-muted/20">
      <div className="border-b border-border p-3">
        <Button
          type="button"
          variant="primary"
          className="w-full justify-center"
          onClick={onNew}
          disabled={disabled}
        >
          <MessageSquarePlus className="h-4 w-4" />
          New Chat
        </Button>
      </div>

      <div className="flex-1 overflow-y-auto p-2">
        {sessions.length === 0 ? (
          <div className="flex h-full min-h-32 items-center justify-center rounded-xl border border-dashed border-border px-4 text-center text-sm text-muted-foreground">
            No sessions yet
          </div>
        ) : (
          <div className="space-y-1.5">
            {sessions.map((session) => {
              const active = session.id === currentSessionId;

              return (
                <div
                  key={session.id}
                  className={cn(
                    "group flex items-start gap-2 rounded-xl border px-3 py-2 transition-colors",
                    active
                      ? "border-primary/30 bg-primary/10 shadow-sm"
                      : "border-transparent bg-transparent hover:border-border hover:bg-card/80",
                  )}
                >
                  <button
                    type="button"
                    onClick={() => onSelect(session.id)}
                    disabled={disabled}
                    className={cn(
                      "min-w-0 flex-1 text-left focus-visible:outline-none",
                      disabled && "cursor-not-allowed opacity-60"
                    )}
                  >
                    <p className="truncate text-sm font-medium text-foreground">
                      {session.title || "Untitled chat"}
                    </p>
                    <p className="mt-1 text-xs text-muted-foreground">
                      {formatRelativeDate(session.updated_at)}
                    </p>
                  </button>
                  {!disabled && (
                    <button
                      type="button"
                      onClick={() => onDelete(session.id)}
                      className="mt-0.5 flex h-7 w-7 shrink-0 items-center justify-center rounded-full text-muted-foreground opacity-0 transition hover:bg-destructive/10 hover:text-destructive focus-visible:opacity-100 focus-visible:outline-none group-hover:opacity-100"
                      aria-label={`Delete ${session.title || "chat"}`}
                    >
                      <Trash2 className="h-3.5 w-3.5" />
                    </button>
                  )}
                </div>
              );
            })}
          </div>
        )}
      </div>
    </div>
  );
}
