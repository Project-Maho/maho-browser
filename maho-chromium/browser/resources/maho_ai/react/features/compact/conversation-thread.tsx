import {useMemo, useState} from 'react';

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
      ? `${item.text.slice(0, 80).trimEnd()}…`
      : item.text;

  return (
    <article className="compact-thread__row compact-thread__row--system compact-thread__row--thinking-content">
      <div
        className="compact-thread__bubble compact-thread__bubble--thinking"
        onClick={() => setExpanded(!expanded)}
        role="button"
        tabIndex={0}
        onKeyDown={e => { if (e.key === 'Enter' || e.key === ' ') setExpanded(!expanded); }}>
        <span className="compact-thread__thinking-label">Thinking</span>
        <span className="compact-thread__thinking-text">{preview}</span>
        {item.text.length > 80 ? (
          <span className="compact-thread__thinking-chevron" data-expanded={expanded}>
            {expanded ? '▾' : '▸'}
          </span>
        ) : null}
      </div>
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
    <main className="compact-thread">
      <div ref={listRef} className="compact-thread__list">
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
