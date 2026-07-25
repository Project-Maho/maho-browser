import React from 'react';
import {formatTimestamp} from '../../lib/format-timestamp.js';
import {cn} from '@lib/utils';
import {Button} from '@ui/button';
import type {ConversationItem} from '../../../views/conversation_thread.js';

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
      onRespondToApproval,
      readOnly,
    }: {
      item: ConversationItem;
      onRespondToApproval: (approvalId: string, approved: boolean) => void;
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
              <span className="text-sm leading-6 text-foreground">{item.text}</span>
            </div>
          ) : (
            <div className="grid min-w-0 flex-1 gap-0.5">
              {item.toolName ? (
                <span className="truncate font-mono text-[11px] font-semibold text-muted-foreground">{item.toolName}</span>
              ) : (
                <span className="text-[11px] font-semibold uppercase tracking-[0.12em] text-muted-foreground">
                  {item.tone === 'danger' ? 'Issue' : 'Activity'}
                </span>
              )}
              <span className="text-sm leading-6 text-foreground/90">{item.text}</span>
            </div>
          )}
          <span className="ml-auto text-[11px] text-muted-foreground">{formatTimestamp(item.timestamp)}</span>
        </div>
        {!isApproval && item.note ? (
          <div className="text-xs leading-5 text-muted-foreground">{item.note}</div>
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
                    className="bg-success text-foreground hover:bg-success/90"
                    disabled={readOnly}
                    size="sm"
                    variant="default"
                    onClick={() => onRespondToApproval(approval.approvalId, true)}>
                    Accept
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
      </div>
    </article>
  );
}
