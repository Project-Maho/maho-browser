import { useEffect, useRef, useState } from "react";
import { Loader2, ChevronDown, ChevronRight, CheckCircle2 } from "lucide-react";

import type { AgentChatMessage } from "../../types";
import { cn } from "../../lib/utils";

interface ChatMessageListProps {
  messages: AgentChatMessage[];
  isHistoryLoading: boolean;
  isSending: boolean;
  streamingMessage?: string;
  phaseText?: string;
}

interface ToolResultDisplay {
  toolName: string;
  summary: string;
  rawResult: string;
}

function isObject(value: unknown): value is Record<string, unknown> {
  return value !== null && typeof value === "object";
}

function parseToolResult(content: string): ToolResultDisplay {
  const fallback: ToolResultDisplay = {
    toolName: "Unknown Tool",
    summary: "",
    rawResult: content,
  };

  let parsed: unknown;
  try {
    parsed = JSON.parse(content);
  } catch {
    return fallback;
  }

  if (!isObject(parsed) || typeof parsed.result !== "string") {
    return fallback;
  }

  const result = parsed.result;
  let toolName = "Unknown Tool";
  let summary = "";

  if (result.startsWith("[")) {
    toolName = "search_emails";
    try {
      const arr: unknown = JSON.parse(result);
      if (Array.isArray(arr)) {
        summary = `Found ${arr.length} matching emails`;
      }
    } catch {
      // Malformed inner JSON; keep summary empty and fall through to raw display.
    }
  } else if (result.startsWith("{")) {
    toolName = "get_email";
    try {
      const detail: unknown = JSON.parse(result);
      if (isObject(detail) && typeof detail.subject === "string") {
        summary = `Read email: "${detail.subject}"`;
      }
    } catch {
      // Malformed inner JSON; keep summary empty and fall through to raw display.
    }
  }

  return { toolName, summary, rawResult: result };
}

function parseToolCallNames(toolCalls: string | undefined): string[] {
  if (!toolCalls) return [];
  try {
    const parsed: unknown = JSON.parse(toolCalls);
    if (!Array.isArray(parsed)) return [];
    return parsed
      .filter(
        (entry): entry is { name: string } =>
          isObject(entry) && typeof entry.name === "string",
      )
      .map((entry) => entry.name);
  } catch {
    return [];
  }
}

function TypingIndicator() {
  return (
    <div className="flex justify-start" data-testid="typing-indicator">
      <div className="inline-flex items-center gap-1 rounded-2xl border border-border bg-card px-3 py-2 shadow-sm">
        {[0, 1, 2].map((index) => (
          <span
            key={index}
            className="h-2 w-2 animate-bounce rounded-full bg-muted-foreground/70"
            style={{ animationDelay: `${index * 0.15}s` }}
          />
        ))}
      </div>
    </div>
  );
}

function ToolResultMessage({ message }: { message: AgentChatMessage }) {
  const [expanded, setExpanded] = useState(false);
  const { toolName, summary, rawResult } = parseToolResult(message.content);

  return (
    <div className="flex justify-start my-1 w-full" data-testid="tool-result-msg">
      <div className="max-w-[85%] rounded-lg border border-border bg-muted/30 px-3 py-2 text-xs">
        <button
          type="button"
          onClick={() => setExpanded(!expanded)}
          className="flex items-center gap-1.5 font-medium text-muted-foreground hover:text-foreground"
        >
          {expanded ? <ChevronDown className="h-3.5 w-3.5" /> : <ChevronRight className="h-3.5 w-3.5" />}
          <CheckCircle2 className="h-3.5 w-3.5 text-emerald-500" />
          <span>
            Used tool:{" "}
            <code className="bg-muted px-1 py-0.5 rounded text-[10px]">{toolName}</code>
          </span>
          {summary && <span className="text-[11px] text-muted-foreground font-normal">({summary})</span>}
        </button>
        {expanded && (
          <pre className="mt-2 max-h-40 overflow-auto whitespace-pre-wrap rounded bg-card p-2 font-mono text-[10px] text-card-foreground border border-border">
            {rawResult}
          </pre>
        )}
      </div>
    </div>
  );
}

interface ConversationBubbleProps {
  message: AgentChatMessage;
  toolNames: string[];
}

function ConversationBubble({ message, toolNames }: ConversationBubbleProps) {
  const isUser = message.role === "user";
  const hasToolCalls = toolNames.length > 0;
  const trimmedContent = (message.content || "").trim();

  return (
    <div className="flex flex-col gap-1 w-full">
      {hasToolCalls && (
        <div className="flex justify-start my-1">
          <div className="max-w-[85%] rounded-lg border border-border bg-muted/10 px-3 py-1.5 text-xs text-muted-foreground flex items-center gap-1.5">
            <CheckCircle2 className="h-3.5 w-3.5 text-indigo-500" />
            <span>
              Agent called tool:{" "}
              {toolNames.map((name) => (
                <code key={name} className="bg-muted px-1 py-0.5 rounded text-[10px]">
                  {name}
                </code>
              ))}
            </span>
          </div>
        </div>
      )}
      {trimmedContent && (
        <div className={cn("flex", isUser ? "justify-end" : "justify-start")}>
          <div
            className={cn(
              "max-w-[85%] rounded-2xl px-4 py-3 shadow-sm",
              isUser
                ? "bg-indigo-600 text-white dark:bg-indigo-500"
                : "border border-border bg-card text-card-foreground",
            )}
          >
            <p className="whitespace-pre-wrap break-words text-sm leading-6">{message.content}</p>
          </div>
        </div>
      )}
    </div>
  );
}

export function ChatMessageList({
  messages,
  isHistoryLoading,
  isSending,
  streamingMessage = "",
  phaseText = "",
}: ChatMessageListProps) {
  const scrollRef = useRef<HTMLDivElement | null>(null);

  useEffect(() => {
    const node = scrollRef.current;
    if (!node) {
      return;
    }

    if (typeof node.scrollTo === "function") {
      node.scrollTo({ top: node.scrollHeight, behavior: "smooth" });
    } else {
      node.scrollTop = node.scrollHeight;
    }
  }, [messages.length, isHistoryLoading, isSending, streamingMessage, phaseText]);

  if (isHistoryLoading) {
    return (
      <div className="flex h-full flex-col gap-4 p-4">
        <div className="flex justify-start">
          <div className="h-10 w-2/3 animate-pulse rounded-2xl bg-muted" />
        </div>
        <div className="flex justify-end">
          <div className="h-12 w-1/2 animate-pulse rounded-2xl bg-muted" />
        </div>
        <div className="flex justify-start">
          <div className="h-8 w-3/4 animate-pulse rounded-2xl bg-muted" />
        </div>
      </div>
    );
  }

  if (messages.length === 0 && !isSending && !streamingMessage && !phaseText) {
    return (
      <div className="flex h-full items-center justify-center px-6 text-center text-sm text-muted-foreground">
        Start a new conversation with the AI agent.
      </div>
    );
  }

  return (
    <div ref={scrollRef} className="flex h-full flex-col gap-3 overflow-y-auto px-4 py-4">
      {messages.map((message) => {
        if (message.role === "tool_result") {
          return <ToolResultMessage key={message.id} message={message} />;
        }

        const toolNames =
          message.role === "assistant" ? parseToolCallNames(message.tool_calls) : [];

        return <ConversationBubble key={message.id} message={message} toolNames={toolNames} />;
      })}
      {streamingMessage && (
        <div className="flex justify-start" data-testid="streaming-message">
          <div className="max-w-[85%] rounded-2xl border border-border bg-card text-card-foreground px-4 py-3 shadow-sm">
            <p className="whitespace-pre-wrap break-words text-sm leading-6">{streamingMessage}</p>
          </div>
        </div>
      )}
      {phaseText && (
        <div className="flex items-center gap-2 px-4 py-1 text-xs text-muted-foreground" data-testid="phase-text">
          <Loader2 className="h-3.5 w-3.5 animate-spin text-primary" />
          <span>{phaseText}</span>
        </div>
      )}
      {isSending && !streamingMessage && !phaseText && <TypingIndicator />}
    </div>
  );
}
