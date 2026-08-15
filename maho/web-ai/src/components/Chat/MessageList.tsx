import { type JSX, type RefObject } from 'preact';
import { memo } from 'preact/compat';
import { useCallback, useEffect, useMemo, useRef, useState } from 'preact/hooks';
import type { ChatContent } from '../../bridge/types';
import type {
  AssistantMessageItem,
  MessageItem,
  SystemMessageItem,
  ThinkingMessageItem,
  ToolRequestMessageItem,
  ToolResultMessageItem,
  UserMessageItem,
} from '../../store/chatStore';
import { formatStructuredValue } from '../../store/chatStore';
import { Icon } from '../../ui/icon';
import type { CredentialErrorCode } from '../../screens/credential-error';
import { CredentialErrorCard } from '../../screens/credential-error-card';

export interface MessageListProps {
  messages: readonly MessageItem[];
  toolActionId?: string | null;
  credentialErrorCode?: CredentialErrorCode | null;
  onApproveTool?: (toolCallId: string, toolName: string, rawArguments: string, approved: boolean) => void;
  logRef?: RefObject<HTMLDivElement | null>;
  onScroll?: (event: Event) => void;
  virtualizeThreshold?: number;
  estimatedItemHeight?: number;
  overscan?: number;
  className?: string;
}

const DEFAULT_VIRTUALIZE_THRESHOLD = 50;
const DEFAULT_ESTIMATED_ITEM_HEIGHT = 80;
const DEFAULT_OVERSCAN = 12;

export function MessageList({
  messages,
  toolActionId = null,
  credentialErrorCode = null,
  onApproveTool,
  logRef,
  onScroll,
  virtualizeThreshold = DEFAULT_VIRTUALIZE_THRESHOLD,
  estimatedItemHeight = DEFAULT_ESTIMATED_ITEM_HEIGHT,
  overscan = DEFAULT_OVERSCAN,
  className = '',
}: MessageListProps) {
  const internalContainerRef = useRef<HTMLDivElement | null>(null);
  const containerRef = logRef ?? internalContainerRef;

  const totalCount = messages.length;
  const shouldVirtualize = virtualizeThreshold > 0 && totalCount > virtualizeThreshold;

  const [scrollTop, setScrollTop] = useState(0);
  const [viewportHeight, setViewportHeight] = useState(800);

  const updateViewportMetrics = useCallback(() => {
    const el = containerRef.current;
    if (!el) return;
    setScrollTop(el.scrollTop);
    setViewportHeight(el.clientHeight || 800);
  }, [containerRef]);

  const handleScroll = useCallback(
    (event: Event) => {
      updateViewportMetrics();
      onScroll?.(event);
    },
    [onScroll, updateViewportMetrics],
  );

  useEffect(() => {
    updateViewportMetrics();
  }, [updateViewportMetrics, totalCount]);

  // Compute virtual window indices
  const { visibleMessages, topSpacerHeight, bottomSpacerHeight, startIndex } = useMemo(() => {
    if (!shouldVirtualize) {
      return {
        visibleMessages: messages,
        topSpacerHeight: 0,
        bottomSpacerHeight: 0,
        startIndex: 0,
      };
    }

    const start = Math.max(0, Math.floor(scrollTop / estimatedItemHeight) - overscan);
    const visibleCount = Math.ceil(viewportHeight / estimatedItemHeight) + 2 * overscan;
    const end = Math.min(totalCount, start + visibleCount);

    const topSpacer = start * estimatedItemHeight;
    const bottomSpacer = Math.max(0, (totalCount - end) * estimatedItemHeight);

    return {
      visibleMessages: messages.slice(start, end),
      topSpacerHeight: topSpacer,
      bottomSpacerHeight: bottomSpacer,
      startIndex: start,
    };
  }, [estimatedItemHeight, messages, overscan, scrollTop, shouldVirtualize, totalCount, viewportHeight]);

  return (
    <div
      ref={containerRef as RefObject<HTMLDivElement>}
      class={`chat-log ${className}`.trim()}
      role="log"
      aria-live="polite"
      aria-relevant="additions text"
      onScroll={handleScroll}
    >
      {topSpacerHeight > 0 && (
        <div
          class="chat-virtual-spacer"
          style={{ height: `${topSpacerHeight}px` }}
          aria-hidden="true"
        />
      )}

      {visibleMessages.map((message, idx) => {
        const itemIndex = startIndex + idx;
        const key = message.id || `msg-${itemIndex}`;

        switch (message.kind) {
          case 'tool-request':
            return (
              <MemoizedToolRequestCard
                key={key}
                activeToolCallId={toolActionId}
                message={message}
                onApprove={onApproveTool}
              />
            );
          case 'tool-result':
            return <MemoizedToolResultCard key={key} message={message} />;
          case 'system':
            return <MemoizedSystemRow key={key} message={message} />;
          case 'thinking':
            return <MemoizedThinkingBubble key={key} message={message} />;
          case 'chat':
            return <MemoizedChatBubble key={key} message={message} />;
          default:
            return null;
        }
      })}

      {bottomSpacerHeight > 0 && (
        <div
          class="chat-virtual-spacer"
          style={{ height: `${bottomSpacerHeight}px` }}
          aria-hidden="true"
        />
      )}

      {credentialErrorCode && (
        <CredentialErrorCard code={credentialErrorCode} />
      )}
    </div>
  );
}

// -----------------------------------------------------------------------------
// Memoized Sub-components
// -----------------------------------------------------------------------------

export const MemoizedChatBubble = memo(
  ChatBubble,
  (prev, next) => {
    if (prev.message === next.message) return true;
    const p = prev.message;
    const n = next.message;
    if (p.id !== n.id || p.role !== n.role) return false;
    if (p.role === 'assistant' && n.role === 'assistant') {
      if (p.isStreaming !== n.isStreaming) return false;
      if (p.content !== n.content) return false;
      if (p.images.length !== n.images.length) return false;
      for (let i = 0; i < p.images.length; i++) {
        if (p.images[i]?.src !== n.images[i]?.src) return false;
      }
      return true;
    }
    if (p.role === 'user' && n.role === 'user') {
      return shallowEqualUserContent(p.content, n.content);
    }
    return false;
  },
);

export function ChatBubble({ message }: { message: UserMessageItem | AssistantMessageItem }) {
  const isUser = message.role === 'user';

  return (
    <article class={`chat-row chat-row--${message.role}`}>
      <div
        class={[
          'chat-bubble',
          isUser ? 'chat-bubble-user' : 'chat-bubble-assistant',
          !isUser && message.isStreaming ? 'chat-bubble-streaming' : '',
        ]
          .filter(Boolean)
          .join(' ')}
      >
        <div class="chat-bubble-label">{isUser ? 'You' : 'Maho'}</div>
        {isUser ? <UserMessageContent message={message.content} /> : <AssistantMessageContent message={message} />}
      </div>
    </article>
  );
}

function shallowEqualUserContent(a: ChatContent, b: ChatContent): boolean {
  if (a === b) return true;
  if (a.kind !== b.kind) return false;
  if (a.kind === 'text' && b.kind === 'text') {
    return a.text === b.text;
  }
  if (a.kind === 'image' && b.kind === 'image') {
    return a.base64 === b.base64 && a.mime === b.mime && a.text === b.text;
  }
  return false;
}

export const MemoizedUserMessageContent = memo(UserMessageContent);

export function UserMessageContent({ message }: { message: ChatContent }) {
  if (message.kind === 'image') {
    const src = `data:${message.mime};base64,${message.base64}`;
    return (
      <div class="chat-message-stack">
        {message.text ? <p class="chat-message-copy">{message.text}</p> : null}
        <img class="chat-message-image" src={src} alt="Uploaded attachment" />
      </div>
    );
  }

  return <p class="chat-message-copy">{message.text}</p>;
}

export const MemoizedAssistantMessageContent = memo(AssistantMessageContent);

export function AssistantMessageContent({ message }: { message: AssistantMessageItem }) {
  return (
    <div class="chat-message-stack">
      {message.content ? (
        message.isStreaming ? (
          <p class="chat-message-copy">{message.content}</p>
        ) : (
          <MarkdownRenderer text={message.content} />
        )
      ) : message.isStreaming ? (
        <p class="chat-message-copy chat-message-copy--placeholder">Streaming…</p>
      ) : null}
      {message.images.map((image) => (
        <img key={image.id} class="chat-message-image" src={image.src} alt="Assistant generated" />
      ))}
    </div>
  );
}

export const MemoizedToolRequestCard = memo(
  ToolRequestCard,
  (prev, next) => {
    return (
      prev.message.id === next.message.id &&
      prev.message.status === next.message.status &&
      prev.message.argsText === next.message.argsText &&
      prev.message.toolName === next.message.toolName &&
      prev.message.autoRun === next.message.autoRun &&
      (prev.activeToolCallId === prev.message.toolCallId) ===
        (next.activeToolCallId === next.message.toolCallId) &&
      prev.onApprove === next.onApprove
    );
  },
);

export function ToolRequestCard({
  activeToolCallId,
  message,
  onApprove,
}: {
  activeToolCallId: string | null;
  message: ToolRequestMessageItem;
  onApprove?: (toolCallId: string, toolName: string, rawArguments: string, approved: boolean) => void;
}) {
  const pending = message.status === 'pending';
  const busy = activeToolCallId === message.toolCallId;
  const autoRunState = message.autoRun && (busy ? 'Running…' : message.status === 'approved' ? 'Ran automatically' : null);

  return (
    <article class="chat-tool-row">
      <div class="chat-tool-card">
        <div class="chat-tool-header">
          <span class="chat-tool-label">Calling</span>
          <strong class="chat-tool-name">{message.toolName}</strong>
        </div>
        <pre class="chat-tool-code">{message.argsText}</pre>
        <div class="chat-tool-actions">
          {autoRunState ? (
            <span class="chat-tool-state chat-tool-state--approved">{autoRunState}</span>
          ) : pending ? (
            <>
              <button
                type="button"
                class="chat-tool-button chat-tool-button--approve"
                aria-label={`Approve ${message.toolName}`}
                disabled={busy}
                onClick={() => onApprove?.(message.toolCallId, message.toolName, message.rawArguments, true)}
              >
                {busy ? 'Working…' : 'Approve'}
              </button>
              <button
                type="button"
                class="chat-tool-button chat-tool-button--deny"
                aria-label={`Deny ${message.toolName}`}
                disabled={busy}
                onClick={() => onApprove?.(message.toolCallId, message.toolName, message.rawArguments, false)}
              >
                Deny
              </button>
            </>
          ) : (
            <span class={`chat-tool-state chat-tool-state--${message.status}`}>
              {message.status === 'approved'
                ? 'Approved'
                : message.status === 'unsupported'
                  ? 'Tool execution unsupported'
                  : 'Denied'}
            </span>
          )}
        </div>
      </div>
    </article>
  );
}

export const MemoizedToolResultCard = memo(
  ToolResultCard,
  (prev, next) =>
    prev.message.id === next.message.id &&
    prev.message.toolName === next.message.toolName &&
    prev.message.result === next.message.result,
);

export function ToolResultCard({ message }: { message: ToolResultMessageItem }) {
  return (
    <article class="chat-tool-row">
      <div class="chat-tool-card chat-tool-card--result">
        <div class="chat-tool-header">
          <span class="chat-tool-label">Result</span>
          <strong class="chat-tool-name">{message.toolName}</strong>
        </div>
        <pre class="chat-tool-code">{formatStructuredValue(toolResultDisplayValue(message.result))}</pre>
      </div>
    </article>
  );
}

function toolResultDisplayValue(result: unknown): unknown {
  if (
    typeof result === 'object' &&
    result !== null &&
    !Array.isArray(result) &&
    typeof (result as Record<string, unknown>).output === 'string'
  ) {
    try {
      return JSON.parse((result as Record<string, string>).output);
    } catch {
      return (result as Record<string, string>).output;
    }
  }
  return result;
}

export const MemoizedThinkingBubble = memo(
  ThinkingBubble,
  (prev, next) =>
    prev.message.id === next.message.id &&
    prev.message.thinking === next.message.thinking &&
    prev.message.isStreaming === next.message.isStreaming,
);

export function ThinkingBubble({ message }: { message: ThinkingMessageItem }) {
  const [expanded, setExpanded] = useState(false);

  return (
    <article class="chat-thinking" data-testid="chat-thinking-bubble">
      <button
        type="button"
        class="chat-thinking-header-btn"
        onClick={() => setExpanded((prev) => !prev)}
        aria-expanded={expanded}
      >
        <Icon
          name="chevron-down"
          size={14}
          class={expanded ? 'chat-thinking-chevron chat-thinking-chevron-expanded' : 'chat-thinking-chevron'}
          aria-hidden
        />
        <span class="chat-thinking-header-title">
          {message.isStreaming ? 'Thinking…' : 'Thought'}
        </span>
      </button>
      {expanded && (
        <div class="chat-thinking-content">
          <div class="chat-thinking-text">{message.thinking}</div>
        </div>
      )}
    </article>
  );
}

export const MemoizedSystemRow = memo(
  SystemRow,
  (prev, next) =>
    prev.message.id === next.message.id && prev.message.content === next.message.content,
);

export function SystemRow({ message }: { message: SystemMessageItem }) {
  return (
    <div class="chat-system-row">
      <div class="chat-system-card">{message.content}</div>
    </div>
  );
}

export const MarkdownRenderer = memo(function MarkdownRenderer({ text }: { text: string }) {
  return <div class="chat-markdown">{parseMarkdown(text)}</div>;
});

export function parseMarkdown(text: string): JSX.Element[] {
  const normalized = text.replace(/\r\n/g, '\n');
  const lines = normalized.split('\n');
  const blocks: JSX.Element[] = [];

  for (let index = 0; index < lines.length;) {
    const line = lines[index] ?? '';
    if (!line.trim()) {
      index += 1;
      continue;
    }

    if (line.startsWith('```')) {
      const codeLines: string[] = [];
      index += 1;
      while (index < lines.length && !lines[index]?.startsWith('```')) {
        codeLines.push(lines[index] ?? '');
        index += 1;
      }
      index += index < lines.length ? 1 : 0;
      blocks.push(
        <pre key={`code-${blocks.length}`} class="chat-markdown-pre">
          <code>{codeLines.join('\n')}</code>
        </pre>,
      );
      continue;
    }

    if (line.startsWith('- ')) {
      const items: string[] = [];
      while (index < lines.length && (lines[index] ?? '').startsWith('- ')) {
        items.push((lines[index] ?? '').slice(2));
        index += 1;
      }
      blocks.push(
        <ul key={`list-${blocks.length}`} class="chat-markdown-list">
          {items.map((item, itemIndex) => (
            <li key={`item-${itemIndex}`}>{parseInlineMarkdown(item, `list-${blocks.length}-${itemIndex}`)}</li>
          ))}
        </ul>,
      );
      continue;
    }

    const paragraphLines: string[] = [];
    while (index < lines.length) {
      const currentLine = lines[index] ?? '';
      if (!currentLine.trim() || currentLine.startsWith('```') || currentLine.startsWith('- ')) {
        break;
      }
      paragraphLines.push(currentLine);
      index += 1;
    }

    blocks.push(
      <p key={`p-${blocks.length}`} class="chat-markdown-paragraph">
        {parseInlineMarkdown(paragraphLines.join(' '), `p-${blocks.length}`)}
      </p>,
    );
  }

  return blocks;
}

function parseInlineMarkdown(text: string, keyPrefix: string): Array<string | JSX.Element> {
  const pattern = /(\*\*[^*]+\*\*|\*[^*]+\*|`[^`]+`|\[[^\]]+\]\([^)]+\))/g;
  const nodes: Array<string | JSX.Element> = [];
  let cursor = 0;
  let matchIndex = 0;

  for (;;) {
    const match = pattern.exec(text);
    if (!match) {
      break;
    }

    if (match.index > cursor) {
      nodes.push(text.slice(cursor, match.index));
    }

    const token = match[0];
    if (token.startsWith('**')) {
      const inner = token.slice(2, -2);
      nodes.push(
        <strong key={`${keyPrefix}-strong-${matchIndex}`}>
          {parseInlineMarkdown(inner, `${keyPrefix}-strong-${matchIndex}`)}
        </strong>,
      );
    } else if (token.startsWith('*')) {
      const inner = token.slice(1, -1);
      nodes.push(
        <em key={`${keyPrefix}-em-${matchIndex}`}>
          {parseInlineMarkdown(inner, `${keyPrefix}-em-${matchIndex}`)}
        </em>,
      );
    } else if (token.startsWith('`')) {
      nodes.push(
        <code key={`${keyPrefix}-code-${matchIndex}`} class="chat-markdown-inline-code">
          {token.slice(1, -1)}
        </code>,
      );
    } else {
      const linkMatch = /^\[([^\]]+)\]\(([^)]+)\)$/.exec(token);
      if (linkMatch) {
        nodes.push(
          <a
            key={`${keyPrefix}-link-${matchIndex}`}
            href={linkMatch[2]}
            target="_blank"
            rel="noreferrer noopener"
          >
            {linkMatch[1]}
          </a>,
        );
      } else {
        nodes.push(token);
      }
    }

    cursor = match.index + token.length;
    matchIndex += 1;
  }

  if (cursor < text.length) {
    nodes.push(text.slice(cursor));
  }

  return nodes;
}
