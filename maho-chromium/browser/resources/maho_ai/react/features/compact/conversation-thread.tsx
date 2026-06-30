import {useMemo, useState} from 'react';

import {cn} from '@lib/utils';
import {ChevronDown} from '@icons/lucide';
import {useAutoScroll} from '../../hooks/use-auto-scroll.js';
import type {TimelineEntry} from '../../../types.js';
import {collectConversationItems} from '../../../views/conversation_thread.js';
import type {ConversationItem} from '../../../views/conversation_thread.js';
import {ConversationMessage} from './conversation-message.js';
import {ConversationSystemItem} from './conversation-system-item.js';
import {PendingThought} from './pending-thought.js';

function ThinkingBubble({item}: {item: ConversationItem}) {
  const [expanded, setExpanded] = useState(false);
  const preview = item.text.length > 80 && !expanded
      ? `${item.text.slice(0, 80).trimEnd()}\u2026`
      : item.text;

  return (
    <article className="w-full motion-safe:animate-in motion-safe:fade-in motion-safe:slide-in-from-bottom-1 motion-reduce:animate-none">
      <button
        type="button"
        className="flex w-full items-start gap-3 rounded-xl border border-border/80 bg-secondary/60 px-3 py-2 text-left shadow-sm transition-colors hover:bg-accent/30 focus-visible:outline-none focus-visible:ring-1 focus-visible:ring-ring"
        onClick={() => setExpanded(!expanded)}>
        <div className="min-w-0 flex-1">
          <span className="mb-0.5 block text-[10px] font-semibold uppercase tracking-[0.12em] text-muted-foreground">Thinking</span>
          <span className="block whitespace-pre-wrap text-xs italic leading-5 text-muted-foreground">{preview}</span>
        </div>
        {item.text.length > 80 ? (
          <ChevronDown className={cn(
              'mt-0.5 size-3.5 shrink-0 text-muted-foreground transition-transform',
              expanded && 'rotate-180')} />
        ) : null}
      </button>
    </article>
  );
}

export function ConversationThread(
    {
      entries,
      onRespondToApproval,
      readOnly,
      thinkingLabel,
    }: {
      entries: TimelineEntry[];
      onRespondToApproval: (approvalId: string, approved: boolean) => void;
      readOnly: boolean;
      thinkingLabel: string | null;
    }) {
  const items = useMemo(() => collectConversationItems(entries), [entries]);
  const listRef = useAutoScroll<HTMLDivElement>(items);

  return (
    <main className="flex min-h-0 flex-1 flex-col px-0 pb-0 pt-3">
      <div ref={listRef} className="flex min-h-0 flex-1 flex-col gap-4 overflow-y-auto px-1 pb-6 pt-2 max-[520px]:gap-3 max-[520px]:pb-5">
        {items.map(item => {
          if (item.kind === 'thinking') {
            return <ThinkingBubble key={item.key} item={item} />;
          }
          if (item.role === 'system') {
            return (
              <ConversationSystemItem
                key={item.key}
                item={item}
                readOnly={readOnly}
                onRespondToApproval={onRespondToApproval}
              />
            );
          }
          return (
            <ConversationMessage
              key={item.key}
              item={item}
              label={item.role === 'user' ? 'You' : 'Maho'}
            />
          );
        })}
        {thinkingLabel ? <PendingThought label={thinkingLabel} /> : null}
      </div>
    </main>
  );
}
