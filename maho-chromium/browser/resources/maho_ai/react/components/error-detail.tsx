import {useEffect, useState} from 'react';
import {toast} from 'sonner';

import {Check, Copy} from '@icons/lucide';
import {Button} from '@ui/button';
import {cn} from '@lib/utils';

type CopyStatus = 'copied'|'failed'|'idle';

const COPY_STATUS_RESET_MS = 1800;

const COPY_STATUS_LABEL: Record<Exclude<CopyStatus, 'idle'>, string> = {
  copied: 'Copied',
  failed: 'Copy failed',
};

export function ErrorDetail(
    {className, label = 'Error details', text}: {
      readonly className?: string;
      readonly label?: string;
      readonly text: string;
    }) {
  const [copyStatus, setCopyStatus] = useState<CopyStatus>('idle');

  useEffect(() => {
    if (copyStatus === 'idle') {
      return undefined;
    }

    const timer = window.setTimeout(() => setCopyStatus('idle'), COPY_STATUS_RESET_MS);
    return () => window.clearTimeout(timer);
  }, [copyStatus]);

  const copyDetail = async () => {
    try {
      await navigator.clipboard.writeText(text);
      setCopyStatus('copied');
    } catch (error: unknown) {
      console.error('[maho-ai] Failed to copy error details', error);
      toast.error('Could not copy the error details to the clipboard.');
      setCopyStatus('failed');
    }
  };

  return (
    <div className={cn('grid min-w-0 gap-1.5', className)}>
      <div className="flex items-center gap-2">
        <span className="min-w-0 flex-1 text-[11px] font-semibold uppercase tracking-[0.12em] text-muted-foreground">
          {label}
        </span>
        <Button
          aria-label={copyStatus === 'idle' ? 'Copy error details' : COPY_STATUS_LABEL[copyStatus]}
          className="size-6 rounded-md text-muted-foreground hover:text-foreground"
          data-action="copy-error"
          size="icon"
          type="button"
          variant="ghost"
          onClick={() => void copyDetail()}>
          {copyStatus === 'copied' ?
            <Check aria-hidden="true" className="size-3" /> :
            <Copy aria-hidden="true" className="size-3" />}
        </Button>
      </div>
      <pre
        className="m-0 max-h-64 min-w-0 select-text overflow-auto whitespace-pre-wrap rounded-xl border border-border/70 bg-background/70 px-3 py-2 font-mono text-[11px] leading-5 text-muted-foreground [overflow-wrap:anywhere]"
        data-testid="error-detail">{text}</pre>
      <span aria-live="polite" className="sr-only" role="status">
        {copyStatus === 'idle' ? '' : COPY_STATUS_LABEL[copyStatus]}
      </span>
    </div>
  );
}
