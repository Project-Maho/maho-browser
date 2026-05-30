import type {AppState, SessionRecord} from '../../../types.js';
import {
  getBrowserContextStatusLabel,
  getBrowserContextStatusTone,
  redactUrlForDisplay,
  getRuntimeStateLabel,
  getSessionAccessLabel,
  getSessionTitle,
  getStatusLabel,
  hasCurrentPageAttachment,
  isSessionReadOnly,
} from '../../../types.js';
import {cn} from '../../lib/cn.js';
import {Button} from '../../ui/button.js';
import {EmptyState} from '../../ui/empty-state.js';
import {Pill} from '../../ui/pill.js';
import type {PillTone} from '../../ui/pill.js';

function SectionLabel({text}: {text: string}) {
  return <div className="console-section-label">{text}</div>;
}

function KeyValueRow({label, value}: {label: string; value: string}) {
  return (
    <div className="inspector-row">
      <span className="inspector-row__label">{label}</span>
      <span className="inspector-row__value">{value}</span>
    </div>
  );
}

function SessionSummaryCard({session}: {session: SessionRecord}) {
  return (
    <section className="inspector-card">
      <SectionLabel text="Session" />
      <KeyValueRow label="Status" value={getStatusLabel(session.status)} />
      <KeyValueRow label="Access" value={getSessionAccessLabel(session)} />
      <KeyValueRow label="Adapter" value={session.adapterName} />
      <KeyValueRow label="Events" value={String(session.eventCount)} />
      <KeyValueRow label="Tools" value={String(session.toolCallCount)} />
      {isSessionReadOnly(session) ? <Pill text="Historical session" tone="warning" /> : null}
    </section>
  );
}

export function Inspector(
    {
      onRespondToApproval,
      onSelectInspector,
      state,
    }: {
      onRespondToApproval: (approvalId: string, approved: boolean) => void;
      onSelectInspector: (kind: AppState['selectedInspector']['kind'], id?: string) => void;
      state: AppState;
    }) {
  const currentSessionId = state.currentSessionId;
  const session = currentSessionId ? state.sessionsById[currentSessionId] : null;
  const readOnly = isSessionReadOnly(session);
  const tabDefs: Array<AppState['selectedInspector']['kind']> = ['session', 'context', 'tool', 'approval'];

  return (
    <aside className="console-inspector">
      <div className="console-panel-header">
        <h2>Inspector</h2>
        <p>Session, context, tool, and approval details.</p>
      </div>
      <div className="inspector-tabs">
        {tabDefs.map(kind => (
          <Button
            key={kind}
            className={cn(state.selectedInspector.kind === kind && 'is-active')}
            variant="inspector-tab"
            onClick={() => onSelectInspector(kind, state.selectedInspector.id)}>
            {kind[0]!.toUpperCase() + kind.slice(1)}
          </Button>
        ))}
      </div>
      <div className="inspector-body">
        {!session || !currentSessionId ? (
          <EmptyState
            description="Start a session to inspect runtime state."
            title="Nothing selected"
          />
        ) : state.selectedInspector.kind === 'session' ? (
          <>
            <SessionSummaryCard session={session} />
            <section className="inspector-card">
              <SectionLabel text="Context" />
              <KeyValueRow label="Session" value={getSessionTitle(session)} />
              <KeyValueRow label="Access" value={getSessionAccessLabel(session)} />
              <KeyValueRow
                label="Runtime session"
                value={session.runtimeSessionId || 'Pending assignment'}
              />
              <KeyValueRow
                label="Last runtime state"
                value={getRuntimeStateLabel(session.lastRuntimeState)}
              />
            </section>
          </>
        ) : state.selectedInspector.kind === 'context' ? (
          (() => {
            const payloads = state.contextsBySessionId[currentSessionId] || [];
            return (
              <section className="inspector-card">
                {payloads.length > 0 ? (
                  <>
                    <SectionLabel text={`Injected context (${payloads.length})`} />
                    {payloads.map((ctx, idx) => (
                      <div key={idx} className="inspector-context-entry">
                        <KeyValueRow
                          label={`#${idx + 1}`}
                          value={ctx.label || `Attachment ${idx + 1}`}
                        />
                        <Pill
                          text={getBrowserContextStatusLabel(ctx.status)}
                          tone={getBrowserContextStatusTone(ctx.status) as PillTone}
                        />
                        {ctx.url ? <KeyValueRow label="URL" value={redactUrlForDisplay(ctx.url)} /> : null}
                        {ctx.title ? <KeyValueRow label="Title" value={ctx.title} /> : null}
                        {ctx.contentLength > 0 ? (
                          <KeyValueRow
                            label="Content size"
                            value={`${ctx.contentLength.toLocaleString()} chars`}
                          />
                        ) : null}
                        {ctx.contentSnippet ? (
                          <pre className="inspector-pre">{ctx.contentSnippet}</pre>
                        ) : null}
                        {ctx.warnings.length ? (
                          <KeyValueRow label="Warnings" value={ctx.warnings.join('\n')} />
                        ) : null}
                      </div>
                    ))}
                  </>
                ) : (
                  <>
                    <SectionLabel text="Attached context" />
                     <KeyValueRow
                       label="Page context"
                       value={readOnly ?
                         'Historical snapshot only' :
                         (hasCurrentPageAttachment(state.composer.attachments) ?
                           'On — injected at send time' :
                           'Off')}
                     />
                    <KeyValueRow label="Composer mode" value={state.composer.modeLabel} />
                    <KeyValueRow
                      label="Pending turn"
                      value={state.turnPendingBySessionId[currentSessionId] ? 'Yes' : 'No'}
                    />
                    {!readOnly ? (
                      <p className="inspector-note">
                        Page context (URL, title, content) is assembled and injected by the browser
                        runtime at send time. The actual payload will appear here after the first
                        submission.
                      </p>
                    ) : null}
                  </>
                )}
              </section>
            );
          })()
        ) : state.selectedInspector.kind === 'tool' ? (
          (() => {
            const toolId = state.selectedInspector.id || Object.keys(state.toolCallsById).at(-1);
            const tool = toolId ? state.toolCallsById[toolId] : null;
            const result = toolId ? state.toolResultsByCallId[toolId] : null;
            if (!tool && !result) {
              return (
                <EmptyState
                  description="Tool requests and results will appear here."
                  title="No tool selected"
                />
              );
            }

            return (
              <section className="inspector-card">
                <SectionLabel text="Tool details" />
                {tool ? (
                  <>
                    <KeyValueRow label="Tool" value={tool.toolName} />
                    <KeyValueRow label="Call ID" value={tool.callId} />
                    <KeyValueRow label="Arguments" value={tool.argumentsJson || '{}'} />
                  </>
                ) : null}
                {result ? (
                  <>
                    <Pill text={result.success ? 'Success' : 'Failure'} tone={result.success ? 'success' : 'danger'} />
                    <pre className="inspector-pre">
                      {result.errorMessage || result.output || 'No output'}
                    </pre>
                  </>
                ) : null}
              </section>
            );
          })()
        ) : (
          (() => {
            const approvalId = state.selectedInspector.id || Object.keys(state.approvalsById).at(-1);
            const approval = approvalId ? state.approvalsById[approvalId] : null;
            const result = approvalId ? state.approvalResultsById[approvalId] : null;
            if (!approval) {
              return (
                <EmptyState
                  description="Approval requests will appear here when the runtime pauses."
                  title="No approval selected"
                />
              );
            }

            return (
              <section className="inspector-card">
                <SectionLabel text="Approval request" />
                <KeyValueRow label="Approval ID" value={approval.approvalId} />
                <KeyValueRow label="Description" value={approval.description} />
                {approval.relatedToolCall ? (
                  <KeyValueRow label="Related tool" value={approval.relatedToolCall.toolName} />
                ) : null}
                {result ? (
                  <>
                    <Pill text={result.approved ? 'Approved' : 'Denied'} tone={result.approved ? 'success' : 'danger'} />
                    {result.reason ? <KeyValueRow label="Reason" value={result.reason} /> : null}
                  </>
                ) : (
                  <>
                    <div className="inspector-actions">
                      <Button
                        disabled={readOnly}
                        variant="console-primary"
                        onClick={() => onRespondToApproval(approval.approvalId, true)}>
                        Approve
                      </Button>
                      <Button
                        disabled={readOnly}
                        variant="console-default"
                        onClick={() => onRespondToApproval(approval.approvalId, false)}>
                        Deny
                      </Button>
                    </div>
                    {readOnly ? (
                      <EmptyState
                        description="This approval is shown from stored session history only and can no longer be answered from this view."
                        title="Historical approval"
                      />
                    ) : null}
                  </>
                )}
              </section>
            );
          })()
        )}
      </div>
    </aside>
  );
}
