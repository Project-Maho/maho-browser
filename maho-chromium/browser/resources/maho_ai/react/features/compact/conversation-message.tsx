import {formatTimestamp} from '../../lib/format-timestamp.js';
import {Button} from '../../ui/button.js';
import {Icon} from '../../ui/icon.js';
import type {IconSymbol} from '../../ui/icon.js';
import {Markdown} from '../../ui/markdown.js';
import {cn} from '../../lib/cn.js';
import type {ConversationItem} from '../../../views/conversation_thread.js';

function UserControls() {
  const actions: Array<{ariaLabel: string; symbol: IconSymbol}> = [
    {ariaLabel: 'Edit your message', symbol: 'edit'},
    {ariaLabel: 'Delete your message', symbol: 'trash'},
  ];

  return (
    <div className="compact-thread__hover-controls">
      {actions.map(action => (
        <Button
          key={action.ariaLabel}
          aria-label={action.ariaLabel}
          variant="thread-action">
          <Icon symbol={action.symbol} />
        </Button>
      ))}
    </div>
  );
}

export function ConversationMessage(
    {item, label}: {item: ConversationItem; label: string}) {
  const bubbleClassName = cn(
      'compact-thread__bubble',
      item.markdown && 'compact-thread__bubble--markdown');

  return (
    <article className={`compact-thread__row compact-thread__row--${item.role}`}>
      <div className="compact-thread__meta">
        <span className="compact-thread__meta-label">{label}</span>
        <span className="compact-thread__meta-time">{formatTimestamp(item.timestamp)}</span>
      </div>
      {item.markdown ?
        <Markdown className={bubbleClassName} text={item.text} /> :
        <div className={bubbleClassName}>{item.text}</div>}
      {item.role === 'user' ? <UserControls /> : null}
    </article>
  );
}
