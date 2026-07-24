import React from 'react';
import {formatTimestamp} from '../../lib/format-timestamp.js';
import {Button} from '@ui/button';
import {Markdown} from '../../components/markdown.js';
import {cn} from '@lib/utils';
import {Edit, Trash2} from '@icons/lucide';
import type {ConversationItem} from '../../../views/conversation_thread.js';

function UserControls() {
  return (
    <div className="pointer-events-none flex gap-1 self-end px-1 opacity-0 transition-opacity duration-150 group-hover/message:pointer-events-auto group-hover/message:opacity-100 group-focus-within/message:pointer-events-auto group-focus-within/message:opacity-100 max-[520px]:pointer-events-auto max-[520px]:opacity-100">
      <Button
        aria-label="Edit your message"
        className="h-7 rounded-full border border-transparent px-2 text-muted-foreground hover:-translate-y-px hover:border-border hover:bg-surface-hover hover:text-foreground"
        variant="ghost"
        size="sm">
        <Edit className="size-4" />
      </Button>
      <Button
        aria-label="Delete your message"
        className="h-7 rounded-full border border-transparent px-2 text-muted-foreground hover:-translate-y-px hover:border-border hover:bg-surface-hover hover:text-foreground"
        variant="ghost"
        size="sm">
        <Trash2 className="size-4" />
      </Button>
    </div>
  );
}

const markdownBubbleClassName = cn(
    'grid gap-3',
    '[&_p]:m-0',
    '[&_p:not(:last-child)]:mb-3',
    '[&_h2]:m-0 [&_h2]:text-lg [&_h2]:font-semibold [&_h2]:tracking-tight',
    '[&_h3]:m-0 [&_h3]:text-base [&_h3]:font-semibold [&_h3]:tracking-tight',
    '[&_h4]:m-0 [&_h4]:text-sm [&_h4]:font-semibold [&_h4]:uppercase [&_h4]:tracking-[0.08em]',
    '[&_ul]:m-0 [&_ul]:list-disc [&_ul]:pl-5',
    '[&_ol]:m-0 [&_ol]:list-decimal [&_ol]:pl-5',
    '[&_li+li]:mt-2',
    '[&_pre]:m-0 [&_pre]:overflow-x-auto [&_pre]:rounded-xl [&_pre]:bg-background/80 [&_pre]:p-3 [&_pre]:text-xs',
    '[&_pre_code]:border-0 [&_pre_code]:bg-transparent [&_pre_code]:p-0',
    '[&_code]:rounded-md [&_code]:border [&_code]:border-border [&_code]:bg-secondary/70 [&_code]:px-1.5 [&_code]:py-0.5 [&_code]:text-[0.85em]',
    '[&_blockquote]:m-0 [&_blockquote]:border-l-2 [&_blockquote]:border-border [&_blockquote]:pl-3 [&_blockquote]:text-muted-foreground',
    '[&_a]:text-primary [&_a]:underline [&_a]:underline-offset-4 hover:[&_a]:text-foreground');

export const ConversationMessage = React.memo(
    function ConversationMessage(
        {item, label}: {item: ConversationItem; label: string}) {
  const isUser = item.role === 'user';
  const bubbleClassName = cn(
      'relative min-w-0 max-w-[92%] break-words rounded-2xl px-4 py-3 text-sm leading-6',
      isUser ?
        'rounded-tr-md bg-primary/10 text-foreground' :
        'rounded-tl-md bg-muted text-foreground',
      item.markdown && markdownBubbleClassName);

  return (
    <article className={cn(
        'group/message grid w-full max-w-full gap-2 motion-safe:animate-in motion-safe:fade-in motion-safe:slide-in-from-bottom-1 motion-reduce:animate-none',
        isUser ? 'justify-items-end' : 'justify-items-start')}>
      <div className={cn(
          'flex items-center gap-2 px-1 text-[11px] leading-4',
          isUser && 'justify-end')}>
        <span className="font-semibold uppercase tracking-[0.08em] text-muted-foreground">{label}</span>
        <span className="invisible opacity-0 transition-opacity duration-150 group-hover/message:visible group-hover/message:opacity-100 group-focus-within/message:visible group-focus-within/message:opacity-100 max-[520px]:visible max-[520px]:opacity-100">
          {formatTimestamp(item.timestamp)}
        </span>
      </div>
      {item.markdown ?
        <Markdown className={bubbleClassName} text={item.text} /> :
        <div className={bubbleClassName}>{item.text}</div>}
      {isUser ? <UserControls /> : null}
    </article>
  );
    },
    (prev, next) =>
        prev.label === next.label &&
        prev.item.key === next.item.key &&
        prev.item.text === next.item.text &&
        prev.item.role === next.item.role &&
        prev.item.markdown === next.item.markdown &&
        prev.item.timestamp === next.item.timestamp);
