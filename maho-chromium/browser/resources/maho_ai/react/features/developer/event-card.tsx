import React from 'react';
import type {RuntimeEventRecord} from '../../../types.js';
import {
  getApprovalDecisionLabel,
  getApprovalPolicyLabel,
  getApprovalSensitivityLabel,
  getEventText,
  getRuntimeStateLabel,
  RuntimeEventKind,
} from '../../../types.js';
import {formatTimestamp} from '../../lib/format-timestamp.js';
import {Markdown} from '../../components/markdown.js';
import {Badge} from '@ui/badge';
import {CredentialErrorNotice} from '../../components/credential-error-notice.js';
import {ErrorDetail} from '../../components/error-detail.js';
import {getCredentialErrorPresentation} from '../../../views/credential_error.js';

function EventShell(
    {
      badge,
      body,
      meta,
      title,
    }: {
      badge?: React.ReactNode;
      body: React.ReactNode;
      meta: string;
      title: string;
    }) {
  return (
    <article className="grid gap-3 rounded-2xl border border-border/80 bg-secondary/35 p-4 shadow-sm">
      <div className="flex flex-wrap items-start justify-between gap-2">
        <div className="text-[11px] font-semibold uppercase tracking-[0.14em] text-muted-foreground">
          {title}
        </div>
        <div className="flex flex-wrap items-center gap-2">
          {badge}
          <div className="text-[11px] text-muted-foreground">{meta}</div>
        </div>
      </div>
      {body}
    </article>
  );
}

const markdownBodyClassName =
    'text-sm leading-6 text-muted-foreground [&_p]:m-0 [&_p:not(:last-child)]:mb-3 [&_h2]:m-0 [&_h2]:text-base [&_h2]:font-semibold [&_h3]:m-0 [&_h3]:text-sm [&_h3]:font-semibold [&_ul]:m-0 [&_ul]:list-disc [&_ul]:pl-5 [&_ol]:m-0 [&_ol]:list-decimal [&_ol]:pl-5 [&_li+li]:mt-2 [&_a]:text-primary [&_a]:underline [&_a]:underline-offset-4 [&_blockquote]:m-0 [&_blockquote]:border-l-2 [&_blockquote]:border-border [&_blockquote]:pl-3 [&_blockquote]:text-muted-foreground [&_pre]:m-0 [&_pre]:overflow-x-auto [&_pre]:rounded-xl [&_pre]:bg-background/70 [&_pre]:p-3 [&_pre]:text-xs';

export function EventCard(
    {
      event,
      onOpenSettings,
    }: {
      event: RuntimeEventRecord;
      onOpenSettings: (paneKey: string) => void;
    }) {
  const meta = formatTimestamp(event.timestamp);

  if (event.kind === RuntimeEventKind.kUserPrompt) {
    return (
      <EventShell
        body={<Markdown className={markdownBodyClassName} text={getEventText(event)} />}
        meta={meta}
        title="User prompt"
      />
    );
  }

  if (event.kind === RuntimeEventKind.kSessionStatus) {
    return (
      <EventShell
        body={<div className="text-sm leading-6 text-muted-foreground">{getRuntimeStateLabel(getEventText(event))}</div>}
        meta={meta}
        title="Session status"
      />
    );
  }

  if (event.kind === RuntimeEventKind.kAssistantToken) {
    return (
      <EventShell
        body={<Markdown className={markdownBodyClassName} text={getEventText(event)} />}
        meta={meta}
        title="Assistant streaming"
      />
    );
  }

  if (event.kind === RuntimeEventKind.kAssistantThinking) {
    return (
      <EventShell
        body={<Markdown className={markdownBodyClassName} text={getEventText(event)} />}
        meta={meta}
        title="Assistant thinking"
      />
    );
  }

  if (event.kind === RuntimeEventKind.kTurnComplete) {
    return (
      <EventShell
        body={<Markdown className={markdownBodyClassName} text={getEventText(event)} />}
        meta={meta}
        title="Assistant response"
      />
    );
  }

  if (event.kind === RuntimeEventKind.kToolRequest) {
    return (
      <EventShell
        badge={(
          <Badge variant="secondary">
            {event.toolCall?.status !== undefined ?
              ['Pending', 'Running', 'Completed', 'Failed', 'Cancelled'][event.toolCall.status] || 'Pending' :
              'Pending'}
          </Badge>
        )}
        body={<div className="text-sm leading-6 text-muted-foreground break-words">{event.toolCall?.argumentsJson || 'Waiting for tool arguments'}</div>}
        meta={meta}
        title={event.toolCall?.toolName || 'Tool request'}
      />
    );
  }

  if (event.kind === RuntimeEventKind.kToolResult) {
    const success = !!event.toolResult?.success;
    return (
      <EventShell
        badge={<Badge variant={success ? 'success' : 'destructive'}>{success ? 'Success' : 'Error'}</Badge>}
        body={<div className="text-sm leading-6 text-muted-foreground break-words">{event.toolResult?.errorMessage || event.toolResult?.output || 'No tool output'}</div>}
        meta={meta}
        title="Tool result"
      />
    );
  }

  if (event.kind === RuntimeEventKind.kApprovalRequest) {
    const approval = event.approvalRequest;
    return (
      <EventShell
        badge={<Badge variant="warning">Pending</Badge>}
        body={(
          <div className="grid gap-2 text-sm leading-6 text-muted-foreground break-words">
            <div>{approval?.description || 'The runtime is waiting for approval.'}</div>
            <div className="flex flex-wrap gap-1.5 text-[11px] leading-4">
              <Badge variant="secondary">{getApprovalSensitivityLabel(approval?.sensitivity)}</Badge>
              <Badge variant="secondary">{getApprovalPolicyLabel(approval?.approvalPolicy)}</Badge>
              {approval?.pageDerivedJustification ? <Badge variant="warning">Page-derived</Badge> : null}
            </div>
          </div>
        )}
        meta={meta}
        title="Approval required"
      />
    );
  }

  if (event.kind === RuntimeEventKind.kApprovalResult) {
    const approved = !!event.approvalResult?.approved;
    const result = event.approvalResult;
    return (
      <EventShell
        badge={<Badge variant={approved ? 'success' : 'destructive'}>{approved ? 'Approved' : 'Denied'}</Badge>}
        body={(
          <div className="grid gap-2 text-sm leading-6 text-muted-foreground break-words">
            <div>{result?.reason || getApprovalDecisionLabel(result?.decision)}</div>
            <div className="flex flex-wrap gap-1.5 text-[11px] leading-4">
              <Badge variant="secondary">{getApprovalSensitivityLabel(result?.sensitivity)}</Badge>
              <Badge variant="secondary">{getApprovalPolicyLabel(result?.approvalPolicy)}</Badge>
              {result?.pageDerivedJustification ? <Badge variant="warning">Page-derived</Badge> : null}
            </div>
          </div>
        )}
        meta={meta}
        title="Approval decision"
      />
    );
  }

  if (event.kind === RuntimeEventKind.kError) {
    const credentialError = getCredentialErrorPresentation(event);
    return (
      <EventShell
        badge={<Badge variant="destructive">Error</Badge>}
        body={credentialError ? (
          <CredentialErrorNotice
            onOpenSettings={onOpenSettings}
            presentation={credentialError}
            surface="developer"
          />
        ) : getEventText(event) ? (
          <ErrorDetail label="Runtime error" text={getEventText(event)} />
        ) : (
          <div className="text-sm leading-6 text-muted-foreground break-words">
            The runtime reported an error.
          </div>
        )}
        meta={meta}
        title="Runtime error"
      />
    );
  }

  return (
    <EventShell
      body={<div className="text-sm leading-6 text-muted-foreground">{getRuntimeStateLabel(event.text)}</div>}
      meta={meta}
      title="Connection state"
    />
  );
}
