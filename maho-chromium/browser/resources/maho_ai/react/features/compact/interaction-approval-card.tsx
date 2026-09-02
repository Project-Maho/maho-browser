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
import {useAppState} from '../../hooks/use-app-state.js';

// The optional free-text field doubles as the answer for whichever action the
// user takes: Confirm with text answers with the instruction itself, letting
// the agent act on it, instead of a blind confirmation.
function InteractionCard(
    {store, record}: {store: MahoAiStore; record: InteractionRequestRecord}) {
  const [instruction, setInstruction] = useState('');
  const actionable = isInteractionActionable(record);
  const trimmed = instruction.trim();

  const respond = (answer: InteractionAnswerPayload) => {
    void store.respondToInteraction(record.requestId, answer);
    setInstruction('');
  };

  const confirm = () => {
    respond(trimmed ? {answerKind: 'text', text: trimmed} :
                      {answerKind: 'confirmed'});
  };

  return (
    <section
      aria-label="Agent approval request"
      className="rounded-xl border border-border/80 bg-card/90 p-3 text-xs shadow-[var(--shadow-raised)]"
      data-request-id={record.requestId}
      data-state={record.state}>
      <div className="flex flex-col gap-2">
        <p className="text-[11px] font-medium uppercase tracking-wide text-muted-foreground">
          {getInteractionTitle(record)}
        </p>
        <p className="text-sm leading-5 text-foreground">{record.prompt}</p>
        {record.artifactRef ? (
          <pre
            aria-label="Review artifact"
            className="max-h-32 overflow-auto rounded-lg bg-surface-hover/60 p-2 font-mono text-[11px] leading-4 text-muted-foreground">
            {record.artifactRef}
          </pre>
        ) : null}
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
                title={option.description ?? undefined}
                type="button">
                {option.label}
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
    <div className="flex flex-col gap-2">
      {records.map(record => (
        <InteractionCard key={record.requestId} record={record} store={store} />
      ))}
    </div>
  );
}
