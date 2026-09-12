import React, {useEffect, useState} from 'react';
import {toast} from 'sonner';
import {Button} from '@ui/button';
import {Markdown} from '../../components/markdown.js';
import {cn} from '@lib/utils';
import {Check, Copy, RefreshCw} from '@icons/lucide';
import type {ConversationItem} from '../../../views/conversation_thread.js';

type CopyStatus = 'copied'|'failed'|'idle';

/** How long the transient copy result stays on the button, in milliseconds. */
const COPY_STATUS_RESET_MS = 1800;

const COPY_STATUS_LABEL: Record<Exclude<CopyStatus, 'idle'>, string> = {
  copied: 'Copied',
  failed: 'Copy failed',
};

function AssistantActions(
    {onRegenerate, text}: {onRegenerate?: () => void; text: string}) {
  const [copyStatus, setCopyStatus] = useState<CopyStatus>('idle');

  useEffect(() => {
    if (copyStatus === 'idle') {
      return undefined;
    }

    const timer = window.setTimeout(() => setCopyStatus('idle'), COPY_STATUS_RESET_MS);
    return () => window.clearTimeout(timer);
  }, [copyStatus]);

  const copyResponse = async () => {
    try {
      await navigator.clipboard.writeText(text);
      setCopyStatus('copied');
    } catch (error: unknown) {
      // Clipboard access can be denied or unavailable; surface it instead of
      // leaving the button silently inert.
      console.error('[maho-ai] Failed to copy assistant response', error);
      toast.error('Could not copy the response to the clipboard.');
      setCopyStatus('failed');
    }
  };

  return (
    <div className="flex gap-0.5 px-0.5 text-muted-foreground opacity-0 transition-opacity duration-200 group-hover/message:opacity-100 focus-within:opacity-100">
      <Button
        aria-label={copyStatus === 'idle' ? 'Copy response' : COPY_STATUS_LABEL[copyStatus]}
        className="size-6 rounded-md panel-chip"
        variant="ghost"
        size="icon"
        type="button"
        onClick={() => void copyResponse()}>
        {copyStatus === 'copied' ?
          <Check aria-hidden="true" className="size-3" /> :
          <Copy aria-hidden="true" className="size-3" />}
      </Button>
      {onRegenerate ? (
        <Button
          aria-label="Regenerate response"
          className="size-6 rounded-md panel-chip"
          variant="ghost"
          size="icon"
          type="button"
          onClick={onRegenerate}>
          <RefreshCw aria-hidden="true" className="size-3" />
        </Button>
      ) : null}
      <span aria-live="polite" className="sr-only" role="status">
        {copyStatus === 'idle' ? '' : COPY_STATUS_LABEL[copyStatus]}
      </span>
    </div>
  );
}

const markdownMessageClassName = cn(
    'grid gap-3',
    '[&_p]:m-0',
    '[&_p:not(:last-child)]:mb-3',
    '[&_h2]:m-0 [&_h2]:text-lg [&_h2]:font-semibold [&_h2]:tracking-tight',
    '[&_h3]:m-0 [&_h3]:text-base [&_h3]:font-semibold [&_h3]:tracking-tight',
    '[&_h4]:m-0 [&_h4]:text-sm [&_h4]:font-semibold [&_h4]:uppercase [&_h4]:tracking-[0.08em]',
    '[&_ul]:m-0 [&_ul]:list-disc [&_ul]:pl-5',
    '[&_ol]:m-0 [&_ol]:list-decimal [&_ol]:pl-5',
    '[&_li+li]:mt-2',
    '[&_pre]:m-0 [&_pre]:overflow-x-auto [&_pre]:whitespace-pre-wrap [&_pre]:[overflow-wrap:anywhere] [&_pre]:rounded-xl [&_pre]:bg-background/80 [&_pre]:p-3 [&_pre]:text-xs',
    '[&_pre_code]:border-0 [&_pre_code]:bg-transparent [&_pre_code]:p-0',
    '[&_code]:rounded-md [&_code]:border [&_code]:border-border [&_code]:bg-secondary/70 [&_code]:px-1.5 [&_code]:py-0.5 [&_code]:text-[0.85em] [&_code]:[overflow-wrap:anywhere]',
    '[&_blockquote]:m-0 [&_blockquote]:border-l-2 [&_blockquote]:border-border [&_blockquote]:pl-3 [&_blockquote]:text-muted-foreground',
    '[&_a]:text-primary [&_a]:underline [&_a]:underline-offset-4 hover:[&_a]:text-foreground');

export const ConversationMessage = React.memo(
    function ConversationMessage(
        {item, onRegenerate}:
            {item: ConversationItem; onRegenerate?: () => void}) {
  const isUser = item.role === 'user';
  const messageClassName = cn(
      'relative min-w-0 [overflow-wrap:anywhere] text-[13px] leading-[1.55] tracking-[-0.005em]',
      isUser ?
        'max-w-[85%] rounded-[16px] rounded-br-[6px] panel-card px-3 py-2 text-foreground' :
        'w-full max-w-full px-1 text-foreground',
      item.markdown && markdownMessageClassName);

  return (
    <article className={cn(
        'group/message grid w-full min-w-0 max-w-full grid-cols-[minmax(0,1fr)] gap-1.5',
        isUser ? 'justify-items-end motion-safe:animate-in motion-safe:fade-in motion-safe:slide-in-from-bottom-1 motion-reduce:animate-none' : 'justify-items-start')}>
      {item.markdown ?
        <Markdown className={messageClassName} text={item.text} /> :
        <div className={messageClassName}>{item.text}</div>}
      {isUser ? null :
        <AssistantActions onRegenerate={onRegenerate} text={item.text} />}
    </article>
  );
    },
    (prev, next) =>
        prev.onRegenerate === next.onRegenerate &&
        prev.item.key === next.item.key &&
        prev.item.text === next.item.text &&
        prev.item.role === next.item.role &&
        prev.item.markdown === next.item.markdown &&
        prev.item.timestamp === next.item.timestamp);
