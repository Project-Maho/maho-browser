import { type ComponentChildren } from 'preact';
import { clsx } from '../utils/clsx';

interface ListItemProps {
  leading?: ComponentChildren;
  trailing?: ComponentChildren;
  headline: ComponentChildren;
  supporting?: ComponentChildren;
  onClick?: () => void;
  class?: string;
}

export function ListItem({ leading, trailing, headline, supporting, onClick, class: className }: ListItemProps) {
  const Tag = onClick ? 'button' : 'div';
  return (
    <Tag
      class={clsx('ui-list-item', onClick && 'ui-list-item--interactive', className)}
      onClick={onClick}
    >
      {leading && <span class="ui-list-item__leading">{leading}</span>}
      <span class="ui-list-item__body">
        <span class="ui-list-item__headline">{headline}</span>
        {supporting && <span class="ui-list-item__supporting">{supporting}</span>}
      </span>
      {trailing && <span class="ui-list-item__trailing">{trailing}</span>}
    </Tag>
  );
}
