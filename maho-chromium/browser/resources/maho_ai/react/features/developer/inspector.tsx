import type {AppState, SessionRecord} from '../../../types.js';
import {
  getApprovalDecisionLabel,
  getApprovalPolicyLabel,
  getApprovalSensitivityLabel,
  getApprovalStateLabel,
  getBrowserContextStatusLabel,
  getBrowserContextStatusTone,
  redactUrlForDisplay,
  getRuntimeStateLabel,
  getSessionAccessLabel,
  getSessionTitle,
  getStatusLabel,
  isSessionReadOnly,
} from '../../../types.js';
import {Button} from '@ui/button';
import {EmptyState} from '../../components/empty-state.js';
import {Badge} from '@ui/badge';
import {Tabs, TabsContent, TabsList, TabsTrigger} from '@ui/tabs';
import * as React from 'react';

const mapToneToVariant = (tone: string): any => {
  if (tone === 'danger') return 'destructive';
  if (tone === 'neutral') return 'secondary';
  return tone;
};

function SectionLabel({text}: {text: string}) {
  return (
    <div className="text-[11px] font-semibold uppercase tracking-[0.14em] text-muted-foreground">
      {text}
    </div>
  );
}

function KeyValueRow({label, value}: {label: string; value: string}) {
  return (
    <div className="flex flex-col gap-1 border-t border-border/70 pt-3 first:border-t-0 first:pt-0 max-[1160px]:items-start min-[1161px]:flex-row min-[1161px]:items-start min-[1161px]:justify-between min-[1161px]:gap-4">
      <span className="text-[11px] font-semibold uppercase tracking-[0.14em] text-muted-foreground">
        {label}
      </span>
      <span className="text-sm leading-6 text-muted-foreground min-[1161px]:text-right whitespace-pre-wrap break-words">
        {value}
      </span>
    </div>
  );
}

function InspectorCard(
    {children}: {children: React.ReactNode}) {
  return (
    <section className="grid gap-3 rounded-2xl border border-border/80 bg-secondary/35 p-4 shadow-[var(--shadow-raised)]">
      {children}
    </section>
  );
}

function SessionSummaryCard({session}: {session: SessionRecord}) {
  return (
    <InspectorCard>
      <SectionLabel text="Session" />
      <KeyValueRow label="Status" value={getStatusLabel(session.status)} />
      <KeyValueRow label="Access" value={getSessionAccessLabel(session)} />
      <KeyValueRow label="Adapter" value={session.adapterName} />
      <KeyValueRow label="Events" value={String(session.eventCount)} />
      <KeyValueRow label="Tools" value={String(session.toolCallCount)} />
      {isSessionReadOnly(session) ? <Badge variant="warning">Historical session</Badge> : null}
    </InspectorCard>
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
    <aside className="grid min-h-0 grid-rows-[auto_minmax(0,1fr)] gap-3 rounded-[1.5rem] border border-border/80 bg-card p-4 shadow-[var(--shadow-raised)]">
      <div className="grid gap-2">
        <h2 className="text-lg font-semibold tracking-tight text-foreground">Inspector</h2>
        <p className="text-sm leading-6 text-muted-foreground">
          Session, context, tool, and approval details.
        </p>
      </div>
      <Tabs
        value={state.selectedInspector.kind}
        onValueChange={value => onSelectInspector(value as AppState['selectedInspector']['kind'], state.selectedInspector.id)}
        className="grid min-h-0 grid-rows-[auto_minmax(0,1fr)] gap-3">
        <TabsList className="grid h-auto w-full grid-cols-4 rounded-xl bg-secondary/60 p-1">
          {tabDefs.map(kind => (
            <TabsTrigger key={kind} value={kind} className="px-2 py-1.5 text-xs font-semibold uppercase tracking-[0.08em]">
              {kind[0]!.toUpperCase() + kind.slice(1)}
            </TabsTrigger>
          ))}
        </TabsList>
        <div className="min-h-0 overflow-y-auto pr-1">
          {!session || !currentSessionId ? (
            <EmptyState
              description="Start a session to inspect runtime state."
              title="Nothing selected"
            />
          ) : (
            <>
              <TabsContent value="session" className="mt-0 grid gap-3">
                <SessionSummaryCard session={session} />
                <InspectorCard>
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
                </InspectorCard>
              </TabsContent>

              <TabsContent value="context" className="mt-0">
                {(() => {
                  const payloads = state.contextsBySessionId[currentSessionId] || [];
                  return (
                    <InspectorCard>
                      {payloads.length > 0 ? (
                        <>
                          <SectionLabel text={`Injected context (${payloads.length})`} />
                          {payloads.map((ctx: any, idx: number) => (
                            <div key={idx} className="grid gap-2 rounded-xl border border-border/70 bg-background/40 p-3">
                              <KeyValueRow
                                label={`#${idx + 1}`}
                                value={ctx.label || `Attachment ${idx + 1}`}
                              />
                              <Badge variant={mapToneToVariant(getBrowserContextStatusTone(ctx.status))}>
                                {getBrowserContextStatusLabel(ctx.status)}
                              </Badge>
                              {ctx.url ? <KeyValueRow label="URL" value={redactUrlForDisplay(ctx.url)} /> : null}
                              {ctx.title ? <KeyValueRow label="Title" value={ctx.title} /> : null}
                              {ctx.contentLength > 0 ? (
                                <KeyValueRow
                                  label="Content size"
                                  value={`${ctx.contentLength.toLocaleString()} chars`}
                                />
                              ) : null}
                            </div>
                          ))}
                        </>
                      ) : (
                        <EmptyState
                          description="No context has been injected into this session."
                          title="No context"
                        />
                      )}
                    </InspectorCard>
                  );
                })()}
              </TabsContent>

              <TabsContent value="tool" className="mt-0">
                {(() => {
                  const toolId = state.selectedInspector.id || Object.keys(state.toolCallsById).at(-1);
                  const tool = toolId ? state.toolCallsById[toolId] : null;
                  const result = toolId ? state.toolResultsByCallId[toolId] : null;
                  if (!tool) {
                    return (
                      <EmptyState
                        description="Tool executions will appear here when they run."
                        title="No tool selected"
                      />
                    );
                  }

                  return (
                    <InspectorCard>
                      <SectionLabel text="Tool details" />
                      <KeyValueRow label="Tool" value={tool.toolName} />
                      <KeyValueRow label="Call ID" value={tool.callId} />
                      <KeyValueRow label="Arguments" value={tool.argumentsJson || '{}'} />
                      {result ? (
                        <>
                          <Badge variant={result.success ? 'success' : 'destructive'}>
                            {result.success ? 'Success' : 'Failure'}
                          </Badge>
                          <pre className="m-0 whitespace-pre-wrap break-words rounded-xl border border-border/70 bg-background/60 p-3 text-xs leading-5 text-muted-foreground">
                            {result.errorMessage || result.output || 'No output'}
                          </pre>
                        </>
                      ) : null}
                    </InspectorCard>
                  );
                })()}
              </TabsContent>

              <TabsContent value="approval" className="mt-0">
                {(() => {
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
                    <InspectorCard>
                      <SectionLabel text="Approval request" />
                      <KeyValueRow label="Approval ID" value={approval.approvalId} />
                      <KeyValueRow label="Description" value={approval.description} />
                      <KeyValueRow label="Policy" value={getApprovalPolicyLabel(approval.approvalPolicy)} />
                      <KeyValueRow label="Sensitivity" value={getApprovalSensitivityLabel(approval.sensitivity)} />
                      <KeyValueRow label="State" value={getApprovalStateLabel(approval.state)} />
                      <KeyValueRow
                        label="Justification source"
                        value={approval.pageDerivedJustification ? 'Page-derived' : 'User/runtime'}
                      />
                      {approval.relatedToolCall ? (
                        <KeyValueRow label="Related tool" value={approval.relatedToolCall.toolName} />
                      ) : null}
                      {result ? (
                        <>
                          <Badge variant={result.approved ? 'success' : 'destructive'}>
                            {result.approved ? 'Approved' : 'Denied'}
                          </Badge>
                          {result.reason ? <KeyValueRow label="Reason" value={result.reason} /> : null}
                          <KeyValueRow label="Decision policy" value={getApprovalPolicyLabel(result.approvalPolicy)} />
                          <KeyValueRow label="Decision sensitivity" value={getApprovalSensitivityLabel(result.sensitivity)} />
                          <KeyValueRow label="Decision" value={getApprovalDecisionLabel(result.decision)} />
                          <KeyValueRow label="State" value={getApprovalStateLabel(result.state)} />
                          <KeyValueRow
                            label="Decision source"
                            value={result.pageDerivedJustification ? 'Page-derived' : 'User/runtime'}
                          />
                        </>
                      ) : (
                        <>
                          <div className="flex flex-wrap items-center gap-2">
                            <Button
                              disabled={readOnly}
                              size="sm"
                              variant="default"
                              onClick={() => onRespondToApproval(approval.approvalId, true)}>
                              Approve
                            </Button>
                            <Button
                              disabled={readOnly}
                              size="sm"
                              variant="outline"
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
                    </InspectorCard>
                  );
                })()}
              </TabsContent>
            </>
          )}
        </div>
      </Tabs>
    </aside>
  );
}
