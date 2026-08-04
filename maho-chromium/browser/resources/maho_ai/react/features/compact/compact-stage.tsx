import type {ChatIntent} from '../../../maho_ai.mojom-webui.js';
import type {QuickAction} from '../../../types.js';
import {DismissibleMcpGuidanceCard} from '../dismissible-mcp-guidance-card.js';

type QuickActionIntentKey = 'kSummarizeCurrentPage'|'kQuizCurrentPage';

const QUICK_ACTION_INTENTS = {
  kSummarizeCurrentPage: 1,
  kQuizCurrentPage: 2,
} as const satisfies Record<QuickActionIntentKey, ChatIntent>;

const SUGGESTION_CHIPS = [
  {
    id: 'summarize',
    label: 'Summarize',
    userPrompt: 'Summarize this page.',
    intent: QUICK_ACTION_INTENTS.kSummarizeCurrentPage,
  },
  {
    id: 'quiz-me',
    label: 'Quiz me',
    userPrompt: 'Quiz me on this page.',
    intent: QUICK_ACTION_INTENTS.kQuizCurrentPage,
  },
] as const satisfies readonly QuickAction[];

export function CompactStage({onConnect}: {readonly onConnect?: () => void}) {
  return (
    <main
        aria-label="Empty conversation"
        className="flex min-h-0 flex-1 items-center justify-center overflow-y-auto px-4 py-6">
      <DismissibleMcpGuidanceCard onConnect={onConnect} />
    </main>
  );
}

export function SuggestionChips(
    {
      attachmentCount,
      developerMode,
      disabled,
      hasMessages,
      onSelect,
      prompt,
    }: {
      attachmentCount: number;
      developerMode: boolean;
      disabled: boolean;
      hasMessages: boolean;
      onSelect: (action: QuickAction) => void;
      prompt: string;
    }) {
  const visible = !developerMode && !hasMessages &&
      prompt.trim().length === 0 && attachmentCount === 0;
  if (!visible) {
    return null;
  }

  return (
    <div className="flex flex-wrap items-center justify-center gap-2 pb-3">
      {SUGGESTION_CHIPS.map(action => (
        <button
          key={action.id}
          className="h-10 rounded-full border border-border/80 bg-secondary/80 px-5 text-sm font-semibold text-secondary-foreground shadow-sm transition-colors hover:border-border hover:bg-surface-hover focus-visible:outline-none focus-visible:ring-1 focus-visible:ring-ring disabled:cursor-default disabled:opacity-50"
          disabled={disabled}
          type="button"
          onClick={() => onSelect(action)}>
          {action.label}
        </button>
      ))}
    </div>
  );
}
