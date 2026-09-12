import React from 'react';
import {formatTimestamp} from '../../lib/format-timestamp.js';
import {cn} from '@lib/utils';
import {Button} from '@ui/button';
import {RefreshCw} from '@icons/lucide';
import type {ConversationItem} from '../../../views/conversation_thread.js';
import {CredentialErrorNotice} from '../../components/credential-error-notice.js';
import {ErrorDetail} from '../../components/error-detail.js';

function chipClassName(tone: NonNullable<ConversationItem['chips']>[number]['tone']): string {
  switch (tone) {
    case 'accent':
      return 'border-primary/40 bg-primary/10 text-primary';
    case 'success':
      return 'border-success/40 bg-success/10 text-success';
    case 'warning':
      return 'border-warning/40 bg-warning/10 text-warning';
    case 'danger':
      return 'border-destructive/40 bg-destructive/10 text-destructive';
    case 'neutral':
    default:
      return 'border-border/70 bg-secondary/50 text-muted-foreground';
  }
}

export function ConversationSystemItem(
    {
      item,
      onOpenSettings,
      onRespondToApproval,
      onRetry,
      readOnly,
    }: {
      item: ConversationItem;
      onOpenSettings: (paneKey: string) => void;
      onRespondToApproval: (approvalId: string, approved: boolean) => void;
      onRetry?: () => void;
      readOnly: boolean;
    }) {
  const approval = item.approval;
  const isApproval = item.kind === 'approval' && approval;
  const isPendingApproval = approval?.status === 'pending';
  const toneClassName = isApproval ?
    approval.status === 'approved' ? 'border-success/60' :
    approval.status === 'denied' ? 'border-destructive/60' : 'border-warning/60' :
    item.tone === 'success' ? 'border-success/60' :
    item.tone === 'warning' ? 'border-warning/60' :
    item.tone === 'danger' ? 'border-destructive/60' : 'border-border/70';

  if (item.credentialError) {
    return (
      <article className="w-full max-w-full motion-safe:animate-in motion-safe:fade-in motion-safe:slide-in-from-bottom-1 motion-reduce:animate-none">
        <CredentialErrorNotice
          onOpenSettings={onOpenSettings}
          onRetry={onRetry}
          presentation={item.credentialError}
          surface="compact"
        />
      </article>
    );
  }

  return (
    <article className="w-full max-w-full motion-safe:animate-in motion-safe:fade-in motion-safe:slide-in-from-bottom-1 motion-reduce:animate-none">
      <div className={cn(
          'grid gap-3',
          isApproval ?
            'rounded-2xl border border-border/80 bg-card/90 p-4 shadow-[var(--shadow-raised)]' :
            'border-l pl-4',
          toneClassName)}>
        <div className="flex flex-wrap items-start gap-3">
          {isApproval ? (
            <div className="grid min-w-0 flex-1 gap-0.5">
              <span className="text-[11px] font-semibold uppercase tracking-[0.12em] text-muted-foreground">Approval</span>
              <span className="min-w-0 text-sm leading-6 text-foreground [overflow-wrap:anywhere]">{item.text}</span>
            </div>
          ) : (
            <div className="grid min-w-0 flex-1 gap-0.5">
              {item.toolName ? (
                <span className="min-w-0 font-mono text-[11px] font-semibold text-muted-foreground [overflow-wrap:anywhere]">{item.toolName}</span>
              ) : (
                <span className="text-[11px] font-semibold uppercase tracking-[0.12em] text-muted-foreground">
                  {item.tone === 'danger' ? 'Issue' : 'Activity'}
                </span>
              )}
              <span className="min-w-0 text-sm leading-6 text-foreground/90 [overflow-wrap:anywhere]">{item.text}</span>
            </div>
          )}
          <span className="ml-auto text-[11px] text-muted-foreground">{formatTimestamp(item.timestamp)}</span>
        </div>
        {!isApproval && item.note ? (
          <div className="min-w-0 text-xs leading-5 text-muted-foreground [overflow-wrap:anywhere]">{item.note}</div>
        ) : null}
        {!isApproval && item.errorDetail ? (
          <ErrorDetail text={item.errorDetail} />
        ) : null}
        {item.chips?.length ? (
          <div className="flex flex-wrap gap-1.5">
            {item.chips.map(chip => (
              <span
                key={`${chip.tone}:${chip.text}`}
                className={cn(
                    'rounded-full border px-2 py-0.5 text-[11px] font-medium',
                    chipClassName(chip.tone))}>
                {chip.text}
              </span>
            ))}
          </div>
        ) : null}
        {isApproval ? (
          <>
            {!item.chips?.length ? <div className="flex flex-wrap items-center gap-1.5">
              {approval.sensitivity ? (
                <span className="rounded-full border border-border/70 bg-secondary/45 px-2 py-0.5 text-[11px] font-medium text-muted-foreground">
                  {approval.sensitivity}
                </span>
              ) : null}
              {approval.approvalPolicy ? (
                <span className="rounded-full border border-border/70 bg-secondary/45 px-2 py-0.5 text-[11px] font-medium text-muted-foreground">
                  {approval.approvalPolicy}
                </span>
              ) : null}
              {approval.pageDerivedJustification ? (
                <span className="rounded-full border border-warning/45 bg-warning/10 px-2 py-0.5 text-[11px] font-medium text-warning-foreground">
                  Page-derived justification
                </span>
              ) : null}
            </div> : null}
            {approval.reason ? (
              <div className="rounded-xl border border-border/80 bg-secondary/40 px-3 py-2 text-xs leading-5 text-muted-foreground">
                {approval.reason}
              </div>
            ) : null}
            <div className="flex flex-wrap items-center gap-2">
              {isPendingApproval ? (
                <>
                  <Button
                    className="border-warning/40 bg-warning/10 text-warning hover:bg-warning/20"
                    disabled={readOnly}
                    size="sm"
                    title="Approves this request only. Maho will ask again next time."
                    variant="outline"
                    onClick={() => onRespondToApproval(approval.approvalId, true)}>
                    Allow once
                  </Button>
                  <Button
                    disabled={readOnly}
                    size="sm"
                    variant="destructive"
                    onClick={() => onRespondToApproval(approval.approvalId, false)}>
                    Reject
                  </Button>
                </>
              ) : (
                <span className="text-xs font-semibold uppercase tracking-[0.08em] text-muted-foreground">
                  {approval.status === 'approved' ? 'Approved' : 'Rejected'}
                </span>
              )}
            </div>
          </>
        ) : null}
        {!isApproval && item.tone === 'danger' ? (
          <div className="flex flex-wrap items-center gap-2 pt-1">
            {onRetry ? (
              <Button
                className="gap-1.5 h-7 px-2.5 text-xs"
                data-action="retry"
                disabled={readOnly}
                size="sm"
                variant="outline"
                onClick={onRetry}>
                <RefreshCw className="size-3.5" />
                Retry
              </Button>
            ) : null}
            <Button
              className="h-7 px-2.5 text-xs text-muted-foreground hover:text-foreground"
              disabled={readOnly}
              size="sm"
              variant="ghost"
              onClick={() => onOpenSettings('maho-ai')}>
              Open AI settings
            </Button>
          </div>
        ) : null}
      </div>
    </article>
  );
}
