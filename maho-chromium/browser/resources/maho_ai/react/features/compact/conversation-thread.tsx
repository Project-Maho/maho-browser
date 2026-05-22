import {useMemo} from 'react';

import {useAutoScroll} from '../../hooks/use-auto-scroll.js';
import type {TimelineEntry} from '../../../types.js';
import {collectConversationItems} from '../../../views/conversation_thread.js';
import {ConversationMessage} from './conversation-message.js';
import {ConversationSystemItem} from './conversation-system-item.js';
import {PendingThought} from './pending-thought.js';

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
        {items.map(item => item.role === 'system' ? (
          <ConversationSystemItem
            key={item.key}
            item={item}
            readOnly={readOnly}
            onRespondToApproval={onRespondToApproval}
          />
        ) : (
          <ConversationMessage
            key={item.key}
            item={item}
            label={item.role === 'user' ? 'You' : 'Maho'}
          />
        ))}
        {thinkingLabel ? <PendingThought label={thinkingLabel} /> : null}
      </div>
    </main>
  );
}
