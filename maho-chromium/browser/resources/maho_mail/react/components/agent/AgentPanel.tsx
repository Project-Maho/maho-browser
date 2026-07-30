import { Bot, PanelRightClose } from "lucide-react";
import { useTranslation } from "react-i18next";

import { useAgentChat } from "../../hooks/useAgentChat";
import { cn } from "../../lib/utils";
import { Button } from "../ui/Button";
import { Skeleton } from "../ui/Skeleton";
import { ChatInput } from "./ChatInput";
import { ChatMessageList } from "./ChatMessageList";
import { SessionSelector } from "./SessionSelector";

interface AgentPanelProps {
  accountId: string;
  onClose: () => void;
  onOpenSettings: () => void;
}

export function AgentPanel({ accountId, onClose, onOpenSettings }: AgentPanelProps) {
  const { t } = useTranslation();
  const {
    sessions,
    currentSessionId,
    currentSession,
    messages,
    isHistoryLoading,
    isSending,
    aiConfigured,
    setCurrentSessionId,
    handleCreateSession,
    handleDeleteSession,
    handleSend,
    handleCancel,
    streamingMessage,
    phaseText,
  } = useAgentChat(accountId);

  return (
    <div className="pointer-events-auto fixed inset-y-0 right-0 z-[45] flex w-full justify-end sm:w-auto">
      <button
        type="button"
        className="absolute inset-0 bg-black/30 backdrop-blur-[1px] sm:hidden"
        onClick={onClose}
        aria-label={t("a11y.closeDialog")}
      />
      <aside
        className={cn(
          "relative flex h-full w-full max-w-[420px] translate-x-0 flex-col overflow-hidden border-l border-border bg-background shadow-[-18px_0_42px_rgba(15,23,42,0.16)] transition-transform duration-300 ease-out dark:shadow-[-18px_0_42px_rgba(0,0,0,0.45)]",
          "sm:w-[400px]",
        )}
        aria-label="AI Agent chat panel"
      >
        <div className="flex items-center justify-between border-b border-border px-4 py-3">
          <div className="min-w-0">
            <div className="flex items-center gap-2">
              <div className="flex h-9 w-9 items-center justify-center rounded-2xl bg-primary/10 text-primary">
                <Bot className="h-4 w-4" />
              </div>
              <div className="min-w-0">
                <p className="truncate text-sm font-semibold text-foreground">AI Agent</p>
                <p className="truncate text-xs text-muted-foreground">
                  {currentSession?.title || "Chat across your local mail context"}
                </p>
              </div>
            </div>
          </div>
          <Button type="button" variant="ghost" size="icon" onClick={onClose} aria-label="Close AI agent panel">
            <PanelRightClose className="h-4 w-4" />
          </Button>
        </div>

        <div className="grid min-h-0 flex-1 grid-cols-[168px_minmax(0,1fr)]">
          <SessionSelector
            sessions={sessions}
            currentSessionId={currentSessionId}
            onSelect={setCurrentSessionId}
            onDelete={(id) => void handleDeleteSession(id)}
            onNew={() => void handleCreateSession()}
            disabled={aiConfigured === false}
          />

          <div className="flex min-h-0 flex-col bg-background">
            {aiConfigured === null ? (
              <div className="flex-1 space-y-4 p-4">
                <Skeleton className="h-4 w-3/4" />
                <Skeleton className="h-20 w-full" />
                <Skeleton className="h-4 w-1/2" />
                <Skeleton className="h-20 w-full" />
              </div>
            ) : aiConfigured === false ? (
              <div className="flex flex-1 flex-col items-center justify-center bg-background/90 p-6 text-center backdrop-blur-md">
                <div className="mb-4 flex h-14 w-14 items-center justify-center rounded-full bg-muted text-muted-foreground">
                  <Bot className="h-6 w-6" />
                </div>
                <h4 className="mb-2 text-base font-semibold text-foreground">AI Not Configured</h4>
                <p className="mb-3 max-w-[280px] text-sm text-muted-foreground">
                  The AI Agent is disabled because the AI provider configuration could not be loaded or is not set up.
                </p>
                <p className="max-w-[280px] text-xs text-muted-foreground/80">
                  Please go to Settings &gt; AI Configuration to set up your API key and provider.
                </p>
                <Button type="button" size="sm" className="mt-4" onClick={onOpenSettings}>
                  {t("settings.title")}
                </Button>
              </div>
            ) : (
              <div className="min-h-0 flex-1 bg-gradient-to-b from-background via-background to-muted/20">
                <ChatMessageList
                  messages={messages}
                  isHistoryLoading={isHistoryLoading}
                  isSending={isSending}
                  streamingMessage={streamingMessage}
                  phaseText={phaseText}
                />
              </div>
            )}

            <ChatInput
              onSend={(content) => void handleSend(content)}
              disabled={isSending || !accountId || aiConfigured !== true}
              loading={isSending}
              onCancel={handleCancel}
            />
          </div>
        </div>
      </aside>
    </div>
  );
}
