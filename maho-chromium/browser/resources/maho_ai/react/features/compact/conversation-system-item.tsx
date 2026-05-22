import {formatTimestamp} from '../../lib/format-timestamp.js';
import {cn} from '../../lib/cn.js';
import {Button} from '../../ui/button.js';
import type {ConversationItem} from '../../../views/conversation_thread.js';

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
  const approvalTone = approval?.status === 'approved' ? 'success' :
      approval?.status === 'denied' ? 'danger' : 'warning';

  return (
    <article className={cn(
        'compact-thread__row',
        'compact-thread__row--system',
        isApproval && 'compact-thread__row--approval')}>
      <div className={cn(
          'compact-thread__system',
          isApproval && 'compact-thread__system--approval')}
      data-tone={isApproval ? approvalTone : item.tone}>
        <div className="compact-thread__system-meta">
          {isApproval ? (
            <div className="compact-thread__system-copy">
              <span className="compact-thread__system-label">Approval</span>
              <span className="compact-thread__system-text">{item.text}</span>
            </div>
          ) : (
            <div className="compact-thread__system-copy">
              <span className="compact-thread__system-label">
                {item.tone === 'danger' ? 'Issue' : 'Activity'}
              </span>
              <span className="compact-thread__system-text">{item.text}</span>
            </div>
          )}
          <span className="compact-thread__meta-time">{formatTimestamp(item.timestamp)}</span>
        </div>
        {!isApproval && item.note ? <div className="compact-thread__system-note">{item.note}</div> : null}
        {isApproval ? (
          <>
            {approval.reason ? (
              <div className="compact-thread__system-note compact-thread__system-note--approval">
                {approval.reason}
              </div>
            ) : null}
            <div className="compact-thread__approval-actions">
              {isPendingApproval ? (
                <>
                  <Button
                    disabled={readOnly}
                    variant="approval-approve"
                    onClick={() => onRespondToApproval(approval.approvalId, true)}>
                    Accept
                  </Button>
                  <Button
                    disabled={readOnly}
                    variant="approval-deny"
                    onClick={() => onRespondToApproval(approval.approvalId, false)}>
                    Reject
                  </Button>
                </>
              ) : (
                <span className="compact-thread__approval-state">
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
