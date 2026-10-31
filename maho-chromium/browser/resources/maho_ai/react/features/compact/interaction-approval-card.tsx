import React, {useState} from 'react';

import {MahoAiStore} from '../../../store.js';
import type {
  InteractionAnswerPayload,
  InteractionRequestRecord,
} from '../../../types.js';
import {
  getInteractionStatusLabel,
  getInteractionTitle,
  isInteractionActionable,
} from '../../../views/interaction.js';
import {
  parseJsonRecord,
  summarizeInlineText,
  summarizePath,
} from '../../../views/conversation_thread.js';
import {useAppState} from '../../hooks/use-app-state.js';

function humanizeFieldKey(key: string): string {
  return key.replace(/[_.-]+/g, ' ').trim().replace(/^./, c => c.toUpperCase());
}

interface ArtifactRefField {
  readonly label: string;
  readonly value: string;
}

// Projects a raw artifact reference onto the handful of fields a user needs
// to judge blast radius: path tails, action verbs, and sizes. JSON refs
// contribute their primitive fields; anything else falls back to a truncated
// inline summary. The untouched raw text stays available in the disclosure.
function summarizeArtifactRef(artifactRef: string): ArtifactRefField[] {
  const record = parseJsonRecord(artifactRef);
  if (!record) {
    return [];
  }
  const fields: ArtifactRefField[] = [];
  for (const [key, value] of Object.entries(record)) {
    if (fields.length >= 6) {
      break;
    }
    if (typeof value === 'string' && value.trim()) {
      const trimmed = value.trim();
      const valueText = trimmed.includes('/') ? summarizePath(trimmed) ?? trimmed : trimmed;
      fields.push({label: humanizeFieldKey(key), value: summarizeInlineText(valueText)});
    } else if (typeof value === 'number' || typeof value === 'boolean') {
      fields.push({label: humanizeFieldKey(key), value: String(value)});
    }
  }
  return fields;
}

function ArtifactRefSummary({artifactRef}: {readonly artifactRef: string}) {
  const fields = summarizeArtifactRef(artifactRef);
  return (
    <div className="grid min-w-0 max-w-full gap-1.5 rounded-lg bg-surface-hover/60 p-2 text-[11px] leading-4 text-muted-foreground">
      {fields.length > 0 ? (
        <dl aria-label="Artifact summary" className="grid min-w-0 max-w-full gap-0.5">
          {fields.map(field => (
            <div className="flex min-w-0 max-w-full gap-2" key={field.label}>
              <dt className="w-20 shrink-0 font-medium text-foreground/80">{field.label}</dt>
              <dd className="min-w-0 max-w-full break-words [overflow-wrap:anywhere]">{field.value}</dd>
            </div>
          ))}
        </dl>
      ) : (
        <p className="min-w-0 max-w-full break-words [overflow-wrap:anywhere]">{summarizeInlineText(artifactRef, 120)}</p>
      )}
      <details className="min-w-0 max-w-full">
        <summary className="cursor-pointer select-none font-medium transition-colors hover:text-foreground">
          Technical details
        </summary>
        <pre
          aria-label="Review artifact"
          className="m-0 mt-1 box-border block w-full min-w-0 max-w-full max-h-32 overflow-auto whitespace-pre-wrap break-words font-mono text-[11px] leading-4 text-muted-foreground [overflow-wrap:anywhere]">
          {artifactRef}
        </pre>
      </details>
    </div>
  );
}

// The optional free-text field doubles as the answer for whichever action the
// user takes: Confirm with text answers with the instruction itself, letting
// the agent act on it, instead of a blind confirmation.
function InteractionCard(
    {store, record}: {store: MahoAiStore; record: InteractionRequestRecord}) {
  const [instruction, setInstruction] = useState('');
  const [dispatchFailed, setDispatchFailed] = useState(false);
  const actionable = isInteractionActionable(record);
  const trimmed = instruction.trim();

  const respond = async (answer: InteractionAnswerPayload) => {
    setDispatchFailed(false);
    const accepted = await store.respondToInteraction(record.requestId, answer);
    if (accepted) {
      setInstruction(current => current === instruction ? '' : current);
    } else {
      setDispatchFailed(true);
    }
  };

  const confirm = () => {
    respond(trimmed ? {answerKind: 'text', text: trimmed} :
                      {answerKind: 'confirmed'});
  };

  return (
    <section
      aria-label="Agent approval request"
      className="min-w-0 max-w-full rounded-xl border border-border/80 bg-card/90 p-3 text-xs shadow-[var(--shadow-raised)]"
      data-request-id={record.requestId}
      data-state={record.state}>
      <div className="flex min-w-0 max-w-full flex-col gap-2">
        <p className="text-[11px] font-medium uppercase tracking-wide text-muted-foreground">
          {getInteractionTitle(record)}
        </p>
        <p className="min-w-0 max-w-full break-words text-sm leading-5 text-foreground [overflow-wrap:anywhere]">
          {record.prompt}
        </p>
        {record.artifactRef ? <ArtifactRefSummary artifactRef={record.artifactRef} /> : null}
        {record.kind === 'question' && record.options.length > 0 ? (
          <div aria-label="Answer options" className="flex flex-col gap-1.5" role="group">
            {record.options.map(option => (
              <button
                aria-label={`Option: ${option.label}`}
                className={
                  'rounded-lg border border-border/80 px-2 py-1.5 text-left font-medium transition-colors hover:bg-surface-hover ' +
                  (actionable ? 'text-foreground' : 'cursor-not-allowed text-muted-foreground opacity-60')
                }
                disabled={!actionable}
                key={option.id}
                onClick={() =>
                  respond({answerKind: 'selected_option', optionId: option.id})}
                type="button">
                {option.label}
                {option.description ? (
                  <span
                    className="mt-0.5 block text-[11px] font-normal leading-4 text-muted-foreground"
                    data-option-description={option.id}>
                    {option.description}
                  </span>
                ) : null}
              </button>
            ))}
          </div>
        ) : null}
        <input
          aria-label="Instruction for the agent"
          className="rounded-lg border border-border/80 bg-background px-2 py-1.5 text-xs text-foreground placeholder:text-muted-foreground focus:outline-none focus:ring-1 focus:ring-primary disabled:opacity-60"
          disabled={!actionable}
          onChange={event => setInstruction(event.target.value)}
          placeholder="Add an instruction for the agent"
          type="text"
          value={instruction} />
        {record.kind === 'confirmation' ? (
          <p className="text-[11px] leading-4 text-muted-foreground" data-consequence>
            Confirming lets the agent perform this action. Deny to stop it.
          </p>
        ) : null}
        {record.kind === 'confirmation' ? (
          <div className="flex gap-2">
            <button
              aria-label="Confirm action"
              className={
                'flex-1 rounded-lg bg-primary px-2 py-1.5 font-medium text-primary-foreground transition-colors hover:bg-primary/90 ' +
                (actionable ? '' : 'cursor-not-allowed opacity-60')
              }
              disabled={!actionable}
              onClick={confirm}
              type="button">
              Confirm
            </button>
            <button
              aria-label="Deny action"
              className={
                'flex-1 rounded-lg border border-border/80 px-2 py-1.5 font-medium text-foreground transition-colors hover:bg-surface-hover ' +
                (actionable ? '' : 'cursor-not-allowed opacity-60')
              }
              disabled={!actionable}
              onClick={() => respond({answerKind: 'denied'})}
              type="button">
              Deny
            </button>
          </div>
        ) : null}
        {record.kind === 'question' ? (
          <button
            aria-label="Send reply"
            className={
              'self-start rounded-lg border border-border/80 px-2 py-1.5 font-medium text-foreground transition-colors hover:bg-surface-hover ' +
              (actionable && trimmed ? '' : 'cursor-not-allowed opacity-60')
            }
            disabled={!actionable || !trimmed}
            onClick={() => respond({answerKind: 'text', text: trimmed})}
            type="button">
            Send reply
          </button>
        ) : null}
        {dispatchFailed ? (
          <p role="alert" className="text-xs text-destructive">
            Your reply could not be sent. Check your connection and try again.
          </p>
        ) : null}
        {!actionable ? (
          <p className="text-[11px] text-muted-foreground" role="status">
            {getInteractionStatusLabel(record)}
          </p>
        ) : null}
      </div>
    </section>
  );
}

// Agent interaction approval cards (plan row 8): one card per interaction
// request for the active session, consuming the row-6 kInteractionRequest
// mojom event path. Pure display + dispatch; the kernel InteractionBroker
// remains the sole enforcement point.
export function InteractionApprovalCard({store}: {store: MahoAiStore}) {
  const state = useAppState(store);
  const sessionId = state.currentSessionId;
  const records = sessionId ? state.interactionsBySessionId[sessionId] : undefined;
  if (!records?.length) {
    return null;
  }

  return (
    <div className="flex min-w-0 max-w-full flex-col gap-2">
      {records.map(record => (
        <InteractionCard key={record.requestId} record={record} store={store} />
      ))}
    </div>
  );
}
