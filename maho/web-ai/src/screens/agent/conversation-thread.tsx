import type { RefObject } from 'preact';
import { useState } from 'preact/hooks';
import { Icon } from '../../ui/icon';
import { clsx } from '../../utils/clsx';
import type { AgentMessage, AgentPhase } from './agent-types';
import type { ArtifactInfo } from '../../bridge/types';
import { isRunActive } from './agent-types';
import { MarkdownRenderer } from './markdown-renderer';
import { ApprovalCard, type AgentInteractionAnswer } from './approval-card';
import type { CredentialErrorCode } from '../credential-error';
import { CredentialErrorCard } from '../credential-error-card';

interface ConversationThreadProps {
  messages: readonly AgentMessage[];
  phase: AgentPhase;
  errorMessage: string | null;
  errorCredentialCode: CredentialErrorCode | null;
  scrollRef: RefObject<HTMLDivElement>;
  onScroll: () => void;
  onShareArtifact: (artifactId: string) => Promise<boolean>;
  onAnswerInteraction: (requestId: string, answer: AgentInteractionAnswer) => Promise<void>;
}

export function ConversationThread({
  messages,
  phase,
  errorMessage,
  errorCredentialCode,
  scrollRef,
  onScroll,
  onShareArtifact,
  onAnswerInteraction,
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
        {messages.map((message) => (
          <ConversationMessage
            key={message.id}
            message={message}
            onShareArtifact={onShareArtifact}
            onAnswerInteraction={onAnswerInteraction}
          />
        ))}
        {errorCredentialCode
          ? <CredentialErrorCard code={errorCredentialCode} />
          : errorMessage && <ErrorBanner message={errorMessage} />}
        {isRunActive(phase) && <PendingThought />}
      </div>
    </main>
  );
}

interface ConversationMessageProps {
  readonly message: AgentMessage;
  readonly onShareArtifact: (artifactId: string) => Promise<boolean>;
  readonly onAnswerInteraction: (requestId: string, answer: AgentInteractionAnswer) => Promise<void>;
}

function ConversationMessage({ message, onShareArtifact, onAnswerInteraction }: ConversationMessageProps) {
  const isUser = message.role === 'user';
  const label = isUser ? 'You' : 'Maho';

  if (isUser) {
    return (
      <article
        class="agent-message agent-message-user"
        data-testid="agent-message-user"
      >
        <div class="agent-message-meta">
          <span>{label}</span>
          <time dateTime={message.createdAt.toISOString()}>{formatTimestamp(message.createdAt)}</time>
        </div>
        <div class="agent-message-bubble">
          {message.text}
        </div>
      </article>
    );
  }

  return (
    <article
      class="agent-message agent-message-assistant"
      data-testid="agent-message-assistant"
    >
      <div class="agent-assistant-blocks">
        {message.blocks.map((block) => {
          switch (block.kind) {
            case 'thinking':
              if (!block.text.trim()) return null;
              return (
                <ThinkingBubble
                  key={block.id}
                  text={block.text}
                  durationMs={block.durationMs}
                />
              );
            case 'tool_call':
              return (
                <ToolExecution
                  key={block.id}
                  name={block.name}
                  args={block.args}
                  result={block.result ?? undefined}
                  durationMs={block.durationMs}
                />
              );
            case 'message':
              if (!block.text) return null;
              return <MarkdownRenderer key={block.id} text={block.text} />;
            case 'artifact':
              return <ArtifactBlock key={block.id} artifact={block.artifact} onShare={onShareArtifact} />;
            case 'interaction_request':
              return (
                <ApprovalCard
                  key={block.id}
                  request={{
                    id: block.id,
                    interactionKind: block.interactionKind,
                    question: block.question,
                    options: block.options,
                    artifactRef: block.artifactRef,
                    initialState: block.initialState,
                    status: block.status,
                    answerLabel: block.answerLabel,
                  }}
                  onAnswer={onAnswerInteraction}
                />
              );
            default:
              return null;
          }
        })}
      </div>
    </article>
  );
}

function formatArtifactSize(bytes: number): string {
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  return `${(bytes / (1024 * 1024)).toFixed(1)} MB`;
}

interface ArtifactBlockProps {
  readonly artifact: ArtifactInfo;
  readonly onShare: (artifactId: string) => Promise<boolean>;
}

type ArtifactShareState = 'idle' | 'sharing' | 'shared';

function ArtifactBlock({ artifact, onShare }: ArtifactBlockProps) {
  const [shareState, setShareState] = useState<ArtifactShareState>('idle');
  const [shareError, setShareError] = useState<string | null>(null);

  const share = async () => {
    if (shareState === 'sharing') return;
    setShareState('sharing');
    setShareError(null);
    try {
      const shared = await onShare(artifact.artifactId);
      if (!shared) {
        throw new Error('Artifact could not be shared.');
      }
      setShareState('shared');
    } catch (error) {
      setShareState('idle');
      setShareError(messageFromError(error, 'Unable to share artifact.'));
    }
  };

  return (
    <div class="agent-artifact" data-testid="agent-artifact">
      <Icon name="download" aria-hidden />
      <div class="agent-artifact-details">
        <span class="agent-artifact-name">{artifact.displayName}</span>
        <span class="agent-artifact-meta">{formatArtifactSize(artifact.sizeBytes)}</span>
        {shareError && <span class="agent-artifact-error" role="alert">{shareError}</span>}
      </div>
      <button
        type="button"
        class="agent-artifact-share"
        aria-label={`Share ${artifact.displayName}`}
        disabled={shareState === 'sharing'}
        onClick={() => void share()}
      >
        {shareState === 'sharing' ? 'Sharing…' : shareState === 'shared' ? 'Shared' : 'Share'}
      </button>
    </div>
  );
}

function messageFromError(error: unknown, fallback: string): string {
  return error instanceof Error && error.message.trim() ? error.message : fallback;
}

interface ToolExecutionProps {
  readonly name: string;
  readonly args: string;
  readonly result?: string;
  readonly durationMs: number | null;
}

function ToolExecution({ name, args, result, durationMs }: ToolExecutionProps) {
  const [expanded, setExpanded] = useState(false);
  const status = result !== undefined ? 'completed' : 'running';
  const durationStr = formatDuration(durationMs);

  const title = status === 'completed'
    ? `Used ${name}${durationStr ? ` · ${durationStr}` : ''}`
    : `Using ${name}`;

  return (
    <div
      class={clsx('agent-tool-call', status === 'completed' && 'agent-tool-call-completed')}
      data-testid="agent-tool-call-block"
    >
      <button
        type="button"
        class="agent-tool-call-header-btn"
        onClick={() => setExpanded((prev) => !prev)}
        aria-expanded={expanded}
        aria-label={expanded ? 'Collapse tool details' : 'Expand tool details'}
      >
        <Icon
          name={status === 'completed' ? 'arrow-right' : 'loader'}
          size={14}
          class={clsx('agent-tool-call-icon', status === 'running' && 'agent-tool-call-icon-spin')}
          aria-hidden
        />
        <span class="agent-tool-call-title">{title}</span>
        <Icon
          name="chevron-down"
          size={14}
          class={clsx('agent-tool-call-chevron', expanded && 'agent-tool-call-chevron-expanded')}
          aria-hidden
        />
      </button>
      {expanded && (
        <div class="agent-tool-call-details">
          <div class="agent-tool-call-args">
            <span class="agent-tool-call-label">Arguments</span>
            <pre class="agent-tool-call-code"><code>{args}</code></pre>
          </div>
          {result !== undefined && (
            <div class="agent-tool-call-result">
              <span class="agent-tool-call-label">Output</span>
              <MarkdownRenderer text={result} />
            </div>
          )}
        </div>
      )}
    </div>
  );
}

export function ThinkingBubble({ text, durationMs }: { text: string; durationMs: number | null }) {
  const [expanded, setExpanded] = useState(readThinkingExpandedDefault);

  const durationStr = formatDuration(durationMs);
  const headerLabel = durationStr ? `Thought · ${durationStr}` : 'Thought';

  return (
    <article class="agent-thinking" data-testid="agent-thinking-bubble">
      <button
        type="button"
        class="agent-thinking-header-btn"
        onClick={() => setExpanded((prev) => !prev)}
        aria-expanded={expanded}
      >
        <Icon
          name="chevron-down"
          size={14}
          class={clsx('agent-thinking-chevron', expanded && 'agent-thinking-chevron-expanded')}
          aria-hidden
        />
        <span class="agent-thinking-header-title">{headerLabel}</span>
      </button>
      {expanded && (
        <div class="agent-thinking-content">
          <div class="agent-thinking-text">{text}</div>
        </div>
      )}
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

function formatDuration(ms: number | null | undefined): string {
  if (ms === null || ms === undefined) return '';
  if (ms < 1000) {
    return `${ms}ms`;
  }
  return `${(ms / 1000).toFixed(1)}s`;
}

function readThinkingExpandedDefault(): boolean {
  try {
    return localStorage.getItem('maho.agent.thinkingExpandedByDefault') === 'true';
  } catch {
    return false;
  }
}
