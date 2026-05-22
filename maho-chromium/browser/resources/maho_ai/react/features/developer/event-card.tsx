import type {RuntimeEventRecord} from '../../../types.js';
import {getEventText, getRuntimeStateLabel, RuntimeEventKind} from '../../../types.js';
import {formatTimestamp} from '../../lib/format-timestamp.js';
import {Markdown} from '../../ui/markdown.js';
import {Pill} from '../../ui/pill.js';

export function EventCard({event}: {event: RuntimeEventRecord}) {
  const meta = formatTimestamp(event.timestamp);

  if (event.kind === RuntimeEventKind.kUserPrompt) {
    return (
      <article className="timeline-card" data-kind="user">
        <div className="timeline-card__header">
          <h3 className="timeline-card__title">User prompt</h3>
          <div className="timeline-card__meta">{meta}</div>
        </div>
        <Markdown className="timeline-card__body" text={getEventText(event)} />
      </article>
    );
  }

  if (event.kind === RuntimeEventKind.kSessionStatus) {
    return (
      <article className="timeline-card" data-kind="session">
        <div className="timeline-card__header">
          <h3 className="timeline-card__title">Session status</h3>
          <div className="timeline-card__meta">{meta}</div>
        </div>
        <div className="timeline-card__body">{getRuntimeStateLabel(getEventText(event))}</div>
      </article>
    );
  }

  if (event.kind === RuntimeEventKind.kAssistantToken) {
    return (
      <article className="timeline-card" data-kind="assistant">
        <div className="timeline-card__header">
          <h3 className="timeline-card__title">Assistant streaming</h3>
          <div className="timeline-card__meta">{meta}</div>
        </div>
        <Markdown className="timeline-card__body" text={getEventText(event)} />
      </article>
    );
  }

  if (event.kind === RuntimeEventKind.kTurnComplete) {
    return (
      <article className="timeline-card" data-kind="assistant-final">
        <div className="timeline-card__header">
          <h3 className="timeline-card__title">Assistant response</h3>
          <div className="timeline-card__meta">{meta}</div>
        </div>
        <Markdown className="timeline-card__body" text={getEventText(event)} />
      </article>
    );
  }

  if (event.kind === RuntimeEventKind.kToolRequest) {
    return (
      <article className="timeline-card" data-kind="tool">
        <div className="timeline-card__header">
          <h3 className="timeline-card__title">{event.toolCall?.toolName || 'Tool request'}</h3>
          <Pill text={event.toolCall?.status !== undefined ?
            ['Pending', 'Running', 'Completed', 'Failed', 'Cancelled'][event.toolCall.status] || 'Pending' :
            'Pending'} tone="accent" />
          <div className="timeline-card__meta">{meta}</div>
        </div>
        <div className="timeline-card__body">
          {event.toolCall?.argumentsJson || 'Waiting for tool arguments'}
        </div>
      </article>
    );
  }

  if (event.kind === RuntimeEventKind.kToolResult) {
    const success = !!event.toolResult?.success;
    return (
      <article className="timeline-card" data-kind="tool-result">
        <div className="timeline-card__header">
          <h3 className="timeline-card__title">Tool result</h3>
          <Pill text={success ? 'Success' : 'Error'} tone={success ? 'success' : 'danger'} />
          <div className="timeline-card__meta">{meta}</div>
        </div>
        <div className="timeline-card__body">
          {event.toolResult?.errorMessage || event.toolResult?.output || 'No tool output'}
        </div>
      </article>
    );
  }

  if (event.kind === RuntimeEventKind.kApprovalRequest) {
    return (
      <article className="timeline-card" data-kind="approval">
        <div className="timeline-card__header">
          <h3 className="timeline-card__title">Approval required</h3>
          <Pill text="Pending" tone="warning" />
          <div className="timeline-card__meta">{meta}</div>
        </div>
        <div className="timeline-card__body">
          {event.approvalRequest?.description || 'The runtime is waiting for approval.'}
        </div>
      </article>
    );
  }

  if (event.kind === RuntimeEventKind.kApprovalResult) {
    const approved = !!event.approvalResult?.approved;
    return (
      <article className="timeline-card" data-kind="approval-result">
        <div className="timeline-card__header">
          <h3 className="timeline-card__title">Approval decision</h3>
          <Pill text={approved ? 'Approved' : 'Denied'} tone={approved ? 'success' : 'danger'} />
          <div className="timeline-card__meta">{meta}</div>
        </div>
        <div className="timeline-card__body">
          {event.approvalResult?.reason || 'Approval response recorded'}
        </div>
      </article>
    );
  }

  if (event.kind === RuntimeEventKind.kError) {
    return (
      <article className="timeline-card" data-kind="error">
        <div className="timeline-card__header">
          <h3 className="timeline-card__title">Runtime error</h3>
          <Pill text="Error" tone="danger" />
          <div className="timeline-card__meta">{meta}</div>
        </div>
        <div className="timeline-card__body">
          {getEventText(event) || 'The runtime reported an error.'}
        </div>
      </article>
    );
  }

  return (
    <article className="timeline-card" data-kind="connection">
      <div className="timeline-card__header">
        <h3 className="timeline-card__title">Connection state</h3>
        <div className="timeline-card__meta">{meta}</div>
      </div>
      <div className="timeline-card__body">{getRuntimeStateLabel(event.text)}</div>
    </article>
  );
}
