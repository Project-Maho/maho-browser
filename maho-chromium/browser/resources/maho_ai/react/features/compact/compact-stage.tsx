import React from 'react';

import {DismissibleMcpGuidanceCard} from '../dismissible-mcp-guidance-card.js';

const SUGGESTION_CHIPS = ['Analyze', 'Explain', 'Summarize'] as const;

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
      disabled,
      onSelect,
    }: {
      disabled: boolean;
      onSelect: (text: string) => void;
    }) {
  return (
    <div className="flex flex-wrap items-center justify-center gap-2 pb-3">
      {SUGGESTION_CHIPS.map(label => (
        <button
          key={label}
          className="h-10 rounded-full bg-secondary px-5 text-sm font-semibold text-secondary-foreground transition-colors hover:bg-surface-hover focus-visible:outline-none focus-visible:ring-1 focus-visible:ring-ring disabled:cursor-default disabled:opacity-50"
          disabled={disabled}
          type="button"
          onClick={() => onSelect(label)}>
          {label}
        </button>
      ))}
    </div>
  );
}
