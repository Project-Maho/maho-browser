import { useEffect, useRef, useState } from 'preact/hooks';
import { Icon } from '../../ui/icon';
import { clsx } from '../../utils/clsx';
import type { AgentHandle, AgentInteractionRequestOption } from '../../bridge/types';

/** A user resolution for an agent interaction request. */
export type AgentInteractionAnswer =
  | { readonly kind: 'confirmed' }
  | { readonly kind: 'denied' }
  | { readonly kind: 'text'; readonly text: string }
  | { readonly kind: 'option'; readonly optionId: string; readonly label: string };

/**
 * Canonical kernel answer JSON — the exact serde tagged shapes that
 * maho-agent `parse_interaction_answer` (maho-ffi events.rs) maps back to
 * InteractionAnswer::Confirmed / Denied / SelectedOption / Text.
 */
export function agentInteractionAnswerJson(answer: AgentInteractionAnswer): string {
  switch (answer.kind) {
    case 'confirmed':
      return '{"answer_kind":"confirmed"}';
    case 'denied':
      return '{"answer_kind":"denied"}';
    case 'text':
      return '{"answer_kind":"text","0":' + JSON.stringify(answer.text) + '}';
    case 'option':
      // Built manually: JS JSON.stringify hoists integer-like keys ("0") to
      // the front, and the kernel's documented shape lists answer_kind first.
      return '{"answer_kind":"selected_option","0":' + JSON.stringify(answer.optionId) + '}';
  }
}

/** Short past-tense label shown on the card after the answer is accepted. */
export function agentInteractionAnswerLabel(answer: AgentInteractionAnswer): string {
  switch (answer.kind) {
    case 'confirmed':
      return 'Confirmed';
    case 'denied':
      return 'Denied';
    case 'option':
      return answer.label;
    case 'text':
      return answer.text;
  }
}

/**
 * Any kernel lifecycle state other than "pending" is terminal — the card
 * renders disabled (fail-closed: expired/cancelled requests must never be
 * answerable, and nothing here ever auto-approves).
 */
export function isTerminalInteractionState(state: string | null | undefined): boolean {
  return state !== null && state !== undefined && state !== 'pending';
}

function formatStateLabel(state: string): string {
  return state.charAt(0).toUpperCase() + state.slice(1);
}

export interface ApprovalCardRequest {
  readonly id: string;
  readonly interactionKind: 'question' | 'confirmation';
  readonly question: string;
  readonly options: readonly AgentInteractionRequestOption[];
  readonly artifactRef: string | null;
  readonly initialState: string | null;
  readonly status: 'pending' | 'resolved';
  readonly answerLabel: string | null;
  /** Session handle that issued the request; the answer is routed back to it. */
  readonly handle: AgentHandle | null;
}

interface ApprovalCardProps {
  readonly request: ApprovalCardRequest;
  /** Throws on failure so the card can surface the error and stay retryable. */
  readonly onAnswer: (
    requestId: string,
    answer: AgentInteractionAnswer,
    handle: AgentHandle | null,
  ) => Promise<void>;
}

export function ApprovalCard({ request, onAnswer }: ApprovalCardProps) {
  const [sending, setSending] = useState(false);
  const [text, setText] = useState('');
  const [sendError, setSendError] = useState<string | null>(null);
  // Synchronous in-flight guard: render-time `sending` state lags a second
  // click that lands before the re-render, which would double-submit.
  const inFlightRef = useRef(false);
  const mountedRef = useRef(true);
  useEffect(() => {
    mountedRef.current = true;
    return () => {
      mountedRef.current = false;
    };
  }, []);

  const terminal = isTerminalInteractionState(request.initialState);
  const answered = request.status === 'resolved';
  // Dead cards are natively disabled; an in-flight send only sets
  // aria-disabled so keyboard focus stays on the pressed control.
  const dead = terminal || answered;
  const disabled = sending || dead;

  const answer = (choice: AgentInteractionAnswer) => {
    if (inFlightRef.current || dead) return;
    inFlightRef.current = true;
    setSending(true);
    setSendError(null);
    onAnswer(request.id, choice, request.handle)
      .catch((error) => {
        if (mountedRef.current) setSendError(messageFromError(error, 'Failed to send the answer.'));
      })
      .finally(() => {
        inFlightRef.current = false;
        if (mountedRef.current) setSending(false);
      });
  };

  const header = request.interactionKind === 'confirmation' ? 'Approval required' : 'Agent question';
  const titleId = `agent-approval-title-${request.id}`;

  return (
    <div
      class={clsx('agent-approval-card', disabled && 'agent-approval-card-disabled')}
      data-testid="agent-approval-card"
      role="group"
      aria-labelledby={titleId}
    >
      {!dead && (
        // Assertive announcement of a new pending approval; mounted with the
        // card so the text change is what assistive tech reads out.
        <span class="agent-sr-only" role="alert" data-testid="agent-approval-announcement">
          {header}: {request.question}
        </span>
      )}
      <div class="agent-approval-header">
        <Icon name="triangle-alert" size={14} aria-hidden />
        <span class="agent-approval-title" id={titleId}>{header}</span>
      </div>
      <div class="agent-approval-question">{request.question}</div>
      {request.artifactRef !== null && request.artifactRef !== undefined && (
        <div class="agent-approval-artifact" data-testid="agent-approval-artifact">
          <span class="agent-approval-artifact-label">Review artifact</span>
          <code class="agent-approval-artifact-ref">{request.artifactRef}</code>
        </div>
      )}
      {request.options.length > 0 && (
        <div class="agent-approval-options">
          {request.options.map((option) => (
            <button
              key={option.id}
              type="button"
              class="agent-approval-option"
              data-testid="agent-approval-option"
              disabled={dead}
              aria-disabled={disabled}
              onClick={() => answer({ kind: 'option', optionId: option.id, label: option.label })}
            >
              <span class="agent-approval-option-label">{option.label}</span>
              {option.description && (
                <span class="agent-approval-option-description">{option.description}</span>
              )}
            </button>
          ))}
        </div>
      )}
      {request.interactionKind === 'question' && request.options.length === 0 && (
        <form class="agent-approval-actions" onSubmit={(event) => {
          event.preventDefault();
          if (text.trim()) answer({ kind: 'text', text: text.trim() });
        }}>
          <input
            aria-label="Answer"
            value={text}
            disabled={dead}
            aria-disabled={disabled}
            onInput={(event) => setText(event.currentTarget.value)}
          />
          <button type="submit" class="agent-approval-confirm" disabled={dead || !text.trim()} aria-disabled={disabled || !text.trim()}>
            Submit answer
          </button>
        </form>
      )}
      {request.interactionKind === 'confirmation' && (
        <div class="agent-approval-actions">
          <button
            type="button"
            class="agent-approval-deny"
            data-testid="agent-approval-deny"
            disabled={dead}
            aria-disabled={disabled}
            onClick={() => answer({ kind: 'denied' })}
          >
            Deny
          </button>
          <button
            type="button"
            class="agent-approval-confirm"
            data-testid="agent-approval-confirm"
            disabled={dead}
            aria-disabled={disabled}
            onClick={() => answer({ kind: 'confirmed' })}
          >
            Confirm
          </button>
        </div>
      )}
      {(answered || terminal) && (
        <div class="agent-approval-status" data-testid="agent-approval-status">
          {answered ? `Answered: ${request.answerLabel ?? ''}` : formatStateLabel(request.initialState ?? '')}
        </div>
      )}
      {sendError && <div class="agent-approval-error" role="alert">{sendError}</div>}
    </div>
  );
}

function messageFromError(error: unknown, fallback: string): string {
  return error instanceof Error && error.message.trim() ? error.message : fallback;
}
