import type { RefObject } from 'preact';
import { useState } from 'preact/hooks';
import { Icon } from '../../ui/icon';
import { clsx } from '../../utils/clsx';
import type { AgentMessage, AgentPhase } from './agent-types';
import { isRunActive } from './agent-types';
import { MarkdownRenderer } from './markdown-renderer';

interface ConversationThreadProps {
  messages: readonly AgentMessage[];
  phase: AgentPhase;
  errorMessage: string | null;
  scrollRef: RefObject<HTMLDivElement>;
  onScroll: () => void;
}

export function ConversationThread({
  messages,
  phase,
  errorMessage,
  scrollRef,
  onScroll,
}: ConversationThreadProps) {
  return (
    <main class="agent-thread-shell">
      <div
        ref={scrollRef}
        class="agent-thread"
        data-testid="agent-thread"
        role="log"
        aria-live="polite"
        onScroll={onScroll}
      >
        {errorMessage && <ErrorBanner message={errorMessage} />}
        {messages.map((message) => (
          <ConversationMessage key={message.id} message={message} />
        ))}
        {isRunActive(phase) && <PendingThought />}
      </div>
    </main>
  );
}

function ConversationMessage({ message }: { message: AgentMessage }) {
  const isUser = message.role === 'user';
  const label = isUser ? 'You' : 'Maho';

  return (
    <article
      class={clsx('agent-message', isUser ? 'agent-message-user' : 'agent-message-assistant')}
      data-testid={isUser ? 'agent-message-user' : 'agent-message-assistant'}
    >
      <div class="agent-message-meta">
        <span>{label}</span>
        <time dateTime={message.createdAt.toISOString()}>{formatTimestamp(message.createdAt)}</time>
      </div>
      <div class="agent-message-bubble">
        {message.role === 'assistant' ? <MarkdownRenderer text={message.text} /> : message.text}
      </div>
    </article>
  );
}

export function ThinkingBubble({ text }: { text: string }) {
  const [expanded, setExpanded] = useState(false);
  const isLong = text.length > 80;
  const preview = isLong && !expanded ? `${text.slice(0, 80).trimEnd()}…` : text;

  return (
    <article class="agent-thinking" data-testid="agent-thinking-bubble">
      <button
        type="button"
        class="agent-thinking-button"
        disabled={!isLong}
        aria-expanded={isLong ? expanded : undefined}
        onClick={() => setExpanded((value) => !value)}
      >
        <span class="agent-thinking-copy">
          <span class="agent-thinking-eyebrow">Thinking</span>
          <span class="agent-thinking-text">{preview}</span>
        </span>
        {isLong && (
          <Icon
            name="chevron-down"
            size={14}
            class={clsx('agent-thinking-chevron', expanded && 'agent-thinking-chevron-expanded')}
            aria-hidden
          />
        )}
      </button>
    </article>
  );
}

function PendingThought() {
  return (
    <article class="agent-pending" data-testid="agent-pending-thought">
      Thinking…
    </article>
  );
}

function ErrorBanner({ message }: { message: string }) {
  return (
    <div class="agent-error-banner" data-testid="agent-error-banner" role="alert">
      <Icon name="circle-alert" size={16} aria-hidden />
      <span>{message}</span>
    </div>
  );
}

function formatTimestamp(date: Date): string {
  return date.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });
}
