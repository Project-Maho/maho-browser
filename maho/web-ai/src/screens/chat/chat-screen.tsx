import { type JSX } from 'preact';
import { useCallback, useEffect, useMemo, useReducer, useRef } from 'preact/hooks';
import type { ChatContent, ChatMessage as BridgeChatMessage, MahoBridge, ToolDefinition, ToolResult } from '../../bridge';
import { subscribeStream, type ImageDelta, type ToolCallDelta } from '../../bridge';
import { useBridge } from '../../hooks/use-bridge';
import { Icon } from '../../ui/icon';

const DEFAULT_OPENAI_MODEL = 'gpt-4o';
const DEFAULT_OPENAI_ENDPOINT = 'https://api.openai.com/v1';
const DEFAULT_ANTHROPIC_MODEL = 'claude-sonnet-4-20250514';
const DEFAULT_ANTHROPIC_ENDPOINT = 'https://api.anthropic.com/v1';
const DEFAULT_SYSTEM_INSTRUCTION =
  'You are the in-browser AI assistant for Maho Browser. Keep answers practical, concise, and grounded in the user’s question.';
const MAX_PAGE_CONTEXT_CHARS = 12_000;
const AUTO_SCROLL_THRESHOLD_PX = 96;

const DEFAULT_TOOLS: ToolDefinition[] = [
  {
    name: 'search_bookmarks',
    description: "Search the user's bookmarks by keyword",
    parameters: {
      type: 'object',
      properties: {
        query: { type: 'string', description: 'Search query' },
      },
      required: ['query'],
    },
  },
  {
    name: 'list_tabs',
    description: 'List all open tabs with their titles and URLs',
    parameters: {
      type: 'object',
      properties: {},
    },
  },
  {
    name: 'get_page_info',
    description: 'Get the title and URL of the currently active tab',
    parameters: {
      type: 'object',
      properties: {},
    },
  },
  {
    name: 'search_history',
    description: 'Search browsing history by keyword',
    parameters: {
      type: 'object',
      properties: {
        query: { type: 'string', description: 'Search query' },
      },
      required: ['query'],
    },
  },
  {
    name: 'open_tab',
    description: 'Open a new tab with the given URL',
    parameters: {
      type: 'object',
      properties: {
        url: { type: 'string', description: 'URL to open' },
      },
      required: ['url'],
    },
  },
  {
    name: 'navigate',
    description: 'Navigate the current tab to a URL',
    parameters: {
      type: 'object',
      properties: {
        url: { type: 'string', description: 'URL to navigate to' },
      },
      required: ['url'],
    },
  },
  {
    name: 'close_tab',
    description: 'Close a tab by its ID',
    parameters: {
      type: 'object',
      properties: {
        tab_id: { type: 'string', description: 'Tab ID to close' },
      },
      required: ['tab_id'],
    },
  },
  {
    name: 'create_bookmark',
    description: 'Create a bookmark for a URL',
    parameters: {
      type: 'object',
      properties: {
        url: { type: 'string', description: 'URL to bookmark' },
        title: { type: 'string', description: 'Bookmark title' },
      },
      required: ['url', 'title'],
    },
  },
];

interface ChatScreenProps {
  onBack?: () => void;
  bridge?: MahoBridge;
  sessionId?: string;
  pageUrl?: string;
  pageTitle?: string;
  pageText?: string;
  systemInstruction?: string;
  model?: string;
  providerId?: string;
}

interface ProviderConfig {
  apiKey: string;
  endpoint: string;
  id: string;
  label: string;
  model: string;
}

interface RenderedImage {
  id: string;
  mime: string;
  src: string;
  rawBase64: string;
}

interface UserMessageItem {
  kind: 'chat';
  id: string;
  role: 'user';
  content: ChatContent;
}

interface AssistantMessageItem {
  kind: 'chat';
  id: string;
  role: 'assistant';
  content: string;
  images: RenderedImage[];
  isStreaming: boolean;
}

interface ToolRequestMessageItem {
  kind: 'tool-request';
  id: string;
  toolCallId: string;
  toolName: string;
  rawArguments: string;
  argsText: string;
  status: 'pending' | 'approved' | 'denied';
}

interface ToolResultMessageItem {
  kind: 'tool-result';
  id: string;
  toolCallId: string;
  toolName: string;
  result: unknown;
}

interface SystemMessageItem {
  kind: 'system';
  id: string;
  content: string;
}

type MessageItem =
  | UserMessageItem
  | AssistantMessageItem
  | ToolRequestMessageItem
  | ToolResultMessageItem
  | SystemMessageItem;

interface ChatScreenState {
  booting: boolean;
  draft: string;
  errorMessage: string | null;
  isStreaming: boolean;
  messages: MessageItem[];
  missingApiKey: boolean;
  provider: ProviderConfig | null;
  sessionId: string | null;
  toolActionId: string | null;
}

type Action =
  | { type: 'BOOT_START' }
  | { type: 'BOOT_SUCCESS'; messages: MessageItem[]; provider: ProviderConfig; sessionId: string }
  | { type: 'BOOT_ERROR'; message: string }
  | { type: 'BYOK_MISSING' }
  | { type: 'SET_DRAFT'; value: string }
  | { type: 'SEND_TURN_START'; message: ChatContent }
  | { type: 'SEND_TURN_FAILURE'; message: string }
  | { type: 'STREAM_APPEND'; text: string }
  | { type: 'STREAM_COMPLETE'; finalMessage: string }
  | { type: 'STREAM_ERROR'; message: string }
  | { type: 'STREAM_IMAGE'; delta: ImageDelta }
  | { type: 'TOOL_DELTA'; delta: ToolCallDelta }
  | { type: 'TOOL_RESPONSE_START'; toolCallId: string }
  | { type: 'TOOL_RESPONSE_DONE'; result: unknown; status: 'approved' | 'denied'; toolCallId: string; toolName: string }
  | { type: 'TOOL_RESPONSE_ERROR'; message: string; toolCallId: string }
  | { type: 'TURN_CANCELLED' };

function initialState(): ChatScreenState {
  return {
    booting: true,
    draft: '',
    errorMessage: null,
    isStreaming: false,
    messages: [],
    missingApiKey: false,
    provider: null,
    sessionId: null,
    toolActionId: null,
  };
}

function reducer(state: ChatScreenState, action: Action): ChatScreenState {
  switch (action.type) {
    case 'BOOT_START':
      return {
        ...state,
        booting: true,
        errorMessage: null,
        isStreaming: false,
        messages: [],
        missingApiKey: false,
        provider: null,
        sessionId: null,
        toolActionId: null,
      };
    case 'BOOT_SUCCESS':
      return {
        ...state,
        booting: false,
        errorMessage: null,
        isStreaming: false,
        messages: action.messages,
        missingApiKey: false,
        provider: action.provider,
        sessionId: action.sessionId,
        toolActionId: null,
      };
    case 'BOOT_ERROR':
      return {
        ...state,
        booting: false,
        errorMessage: action.message,
        isStreaming: false,
        provider: null,
        sessionId: null,
        toolActionId: null,
      };
    case 'BYOK_MISSING':
      return {
        ...state,
        booting: false,
        errorMessage: null,
        isStreaming: false,
        messages: [],
        missingApiKey: true,
        provider: null,
        sessionId: null,
        toolActionId: null,
      };
    case 'SET_DRAFT':
      return {
        ...state,
        draft: action.value,
      };
    case 'SEND_TURN_START':
      return {
        ...state,
        draft: '',
        errorMessage: null,
        isStreaming: true,
        messages: [
          ...state.messages,
          createUserMessage(action.message),
          createAssistantMessage('', true),
        ],
      };
    case 'SEND_TURN_FAILURE':
      return {
        ...state,
        errorMessage: action.message,
        isStreaming: false,
        messages: removeEmptyStreamingAssistant(state.messages),
      };
    case 'STREAM_APPEND':
      return {
        ...state,
        errorMessage: null,
        isStreaming: true,
        messages: appendAssistantTokens(state.messages, action.text),
      };
    case 'STREAM_COMPLETE':
      return {
        ...state,
        isStreaming: false,
        messages: completeAssistantMessage(state.messages, action.finalMessage),
      };
    case 'STREAM_ERROR':
      return {
        ...state,
        errorMessage: action.message,
        isStreaming: false,
        messages: removeEmptyStreamingAssistant(markAssistantStreamClosed(state.messages)),
      };
    case 'STREAM_IMAGE':
      return {
        ...state,
        messages: appendImageDelta(state.messages, action.delta),
      };
    case 'TOOL_DELTA':
      return {
        ...state,
        messages: upsertToolRequest(state.messages, action.delta),
      };
    case 'TOOL_RESPONSE_START':
      return {
        ...state,
        errorMessage: null,
        toolActionId: action.toolCallId,
      };
    case 'TOOL_RESPONSE_DONE':
      return {
        ...state,
        messages: appendToolResult(updateToolStatus(state.messages, action.toolCallId, action.status), action.toolCallId, action.toolName, action.result),
        toolActionId: null,
      };
    case 'TOOL_RESPONSE_ERROR':
      return {
        ...state,
        errorMessage: action.message,
        toolActionId: null,
        messages: updateToolStatus(state.messages, action.toolCallId, 'pending'),
      };
    case 'TURN_CANCELLED':
      return {
        ...state,
        isStreaming: false,
        messages: removeEmptyStreamingAssistant(markAssistantStreamClosed(state.messages)),
      };
    default:
      return state;
  }
}

export function ChatScreen({
  onBack,
  bridge: bridgeProp,
  sessionId,
  pageUrl,
  pageTitle,
  pageText,
  systemInstruction,
  model,
  providerId,
}: ChatScreenProps) {
  const bridgeFromHook = useBridge();
  const bridge = bridgeProp ?? bridgeFromHook;
  const [state, dispatch] = useReducer(reducer, undefined, initialState);
  const logRef = useRef<HTMLDivElement | null>(null);
  const fileInputRef = useRef<HTMLInputElement | null>(null);
  const stickToBottomRef = useRef(true);
  const activeHandleRef = useRef<string | null>(null);
  const bootstrapGenerationRef = useRef(0);

  const routeSessionId = sessionId?.trim() || null;

  const bootstrapSession = useCallback(
    async (requestedSessionId: string | null) => {
      const generation = ++bootstrapGenerationRef.current;
      const previousHandle = activeHandleRef.current;

      dispatch({ type: 'BOOT_START' });

      const provider = await resolveProviderConfig(bridge, providerId, model);
      if (generation !== bootstrapGenerationRef.current) {
        return;
      }

      if (!provider) {
        activeHandleRef.current = null;
        if (previousHandle) {
          void bridge.chatSessionFree(previousHandle).catch(() => undefined);
        }
        dispatch({ type: 'BYOK_MISSING' });
        return;
      }

      let nextHandle: string | null = null;

      try {
        if (requestedSessionId) {
          await bridge.chatSessionResume(requestedSessionId);
          nextHandle = requestedSessionId;
        } else {
          nextHandle = await bridge.chatSessionStart({
            apiKey: provider.apiKey,
            endpoint: provider.endpoint,
            model: provider.model,
            systemInstruction: buildSystemInstruction({
              pageText,
              pageTitle,
              pageUrl,
              systemInstruction,
            }),
          });
        }

        await registerDefaultTools(bridge, nextHandle);
        const history = requestedSessionId ? await bridge.chatGetHistory(nextHandle) : [];

        if (generation !== bootstrapGenerationRef.current) {
          if (nextHandle && nextHandle !== previousHandle) {
            void bridge.chatSessionFree(nextHandle).catch(() => undefined);
          }
          return;
        }

        activeHandleRef.current = nextHandle;
        dispatch({
          type: 'BOOT_SUCCESS',
          messages: historyToMessages(history),
          provider,
          sessionId: nextHandle,
        });

        if (previousHandle && previousHandle !== nextHandle) {
          void bridge.chatSessionFree(previousHandle).catch(() => undefined);
        }
      } catch (error) {
        if (nextHandle && nextHandle !== requestedSessionId) {
          void bridge.chatSessionFree(nextHandle).catch(() => undefined);
        }
        activeHandleRef.current = null;
        if (previousHandle) {
          void bridge.chatSessionFree(previousHandle).catch(() => undefined);
        }
        if (generation === bootstrapGenerationRef.current) {
          dispatch({
            type: 'BOOT_ERROR',
            message: toErrorMessage(error, 'Unable to open this chat right now.'),
          });
        }
      }
    },
    [bridge, model, pageText, pageTitle, pageUrl, providerId, systemInstruction],
  );

  useEffect(() => {
    void bootstrapSession(routeSessionId);
  }, [bootstrapSession, routeSessionId]);

  useEffect(() => {
    return () => {
      bootstrapGenerationRef.current += 1;
      const handle = activeHandleRef.current;
      activeHandleRef.current = null;
      if (handle) {
        void bridge.chatSessionFree(handle).catch(() => undefined);
      }
    };
  }, [bridge]);

  useEffect(() => {
    if (!state.sessionId) {
      return undefined;
    }

    return subscribeStream(state.sessionId, {
      onToken: (token) => {
        dispatch({ type: 'STREAM_APPEND', text: token });
      },
      onComplete: (finalMessage) => {
        dispatch({ type: 'STREAM_COMPLETE', finalMessage });
      },
      onError: (error) => {
        dispatch({ type: 'STREAM_ERROR', message: error });
      },
      onToolDelta: (delta) => {
        dispatch({ type: 'TOOL_DELTA', delta });
      },
      onImageDelta: (delta) => {
        dispatch({ type: 'STREAM_IMAGE', delta });
      },
    });
  }, [state.sessionId]);

  const handleScroll = useCallback(() => {
    const node = logRef.current;
    if (!node) {
      return;
    }

    const distanceFromBottom = node.scrollHeight - node.scrollTop - node.clientHeight;
    stickToBottomRef.current = distanceFromBottom <= AUTO_SCROLL_THRESHOLD_PX;
  }, []);

  useEffect(() => {
    const node = logRef.current;
    if (!node || !stickToBottomRef.current) {
      return;
    }

    node.scrollTop = node.scrollHeight;
  }, [state.isStreaming, state.messages]);

  const conversationTitle = useMemo(
    () => deriveConversationTitle(state.messages, !!routeSessionId),
    [routeSessionId, state.messages],
  );

  const sendTextTurn = useCallback(async () => {
    const handle = state.sessionId;
    const text = state.draft.trim();
    if (!handle || !text || state.isStreaming || state.booting || state.missingApiKey) {
      return;
    }

    const content: ChatContent = { kind: 'text', text };
    dispatch({ type: 'SEND_TURN_START', message: content });

    try {
      await bridge.chatSendMessage(handle, content);
    } catch (error) {
      dispatch({
        type: 'SEND_TURN_FAILURE',
        message: toErrorMessage(error, 'Unable to send this message.'),
      });
    }
  }, [bridge, state.booting, state.draft, state.isStreaming, state.missingApiKey, state.sessionId]);

  const handleCancelTurn = useCallback(async () => {
    const handle = state.sessionId;
    if (!handle || !state.isStreaming) {
      return;
    }

    try {
      await bridge.chatCancelTurn(handle);
    } finally {
      dispatch({ type: 'TURN_CANCELLED' });
    }
  }, [bridge, state.isStreaming, state.sessionId]);

  const handleApproveTool = useCallback(
    async (toolCallId: string, toolName: string, approved: boolean) => {
      const handle = state.sessionId;
      if (!handle) {
        return;
      }

      dispatch({ type: 'TOOL_RESPONSE_START', toolCallId });

      const result = approved
        ? toBridgeToolResult({ ok: true, data: '<not implemented>' })
        : toBridgeToolResult({ error: 'user_denied' });

      try {
        await bridge.chatSendToolResult(handle, toolCallId, result);
        dispatch({
          type: 'TOOL_RESPONSE_DONE',
          result: approved ? { ok: true, data: '<not implemented>' } : { error: 'user_denied' },
          status: approved ? 'approved' : 'denied',
          toolCallId,
          toolName,
        });
      } catch (error) {
        dispatch({
          type: 'TOOL_RESPONSE_ERROR',
          message: toErrorMessage(error, 'Unable to respond to this tool call.'),
          toolCallId,
        });
      }
    },
    [bridge, state.sessionId],
  );

  const handleNewSession = useCallback(async () => {
    if (routeSessionId) {
      window.location.hash = '#chat';
      return;
    }

    await bootstrapSession(null);
  }, [bootstrapSession, routeSessionId]);

  const handleImageUpload = useCallback(
    async (event: Event) => {
      const handle = state.sessionId;
      if (!handle || state.booting || state.missingApiKey || state.isStreaming) {
        return;
      }

      const target = event.target;
      if (!(target instanceof HTMLInputElement)) {
        return;
      }

      const file = target.files?.[0];
      target.value = '';
      if (!file) {
        return;
      }

      try {
        const dataUrl = await readFileAsDataUrl(file);
        const parsed = parseDataUrl(dataUrl);
        if (!parsed) {
          throw new Error('Unsupported image attachment.');
        }

        const text = state.draft.trim();
        const content: ChatContent = text
          ? { kind: 'image', mime: parsed.mime, base64: parsed.base64, text }
          : { kind: 'image', mime: parsed.mime, base64: parsed.base64 };

        dispatch({ type: 'SEND_TURN_START', message: content });
        await bridge.chatSendMessage(handle, content);
      } catch (error) {
        dispatch({
          type: 'SEND_TURN_FAILURE',
          message: toErrorMessage(error, 'Unable to attach this image.'),
        });
      }
    },
    [bridge, state.booting, state.draft, state.isStreaming, state.missingApiKey, state.sessionId],
  );

  const showEmptyState =
    !state.booting &&
    !state.errorMessage &&
    !state.missingApiKey &&
    state.messages.length === 0;

  return (
    <div class="chat-screen" data-testid="chat-screen">
      <header class="chat-header">
        <div class="chat-header-main">
          {onBack && (
            <button type="button" class="chat-back-button" onClick={onBack} aria-label="Back">
              <Icon name="chevron-left" size={18} aria-hidden />
            </button>
          )}
          <div class="chat-header-copy">
            <p class="chat-eyebrow">Chat</p>
            <h1 class="chat-title">{conversationTitle}</h1>
            <p class="chat-subtitle">
              {state.provider
                ? `${state.provider.label} · ${state.provider.model}`
                : 'Bring your own API key to continue.'}
            </p>
          </div>
        </div>
        <div class="chat-header-actions">
          <button
            type="button"
            class="chat-new-button"
            onClick={() => void handleNewSession()}
            disabled={state.booting}
          >
            + New
          </button>
        </div>
      </header>

      <main class="chat-messages">
        {state.errorMessage && (
          <div class="chat-error-banner" role="alert">
            {state.errorMessage}
          </div>
        )}

        {state.booting ? (
          <div class="chat-loading-panel" aria-live="polite">
            <span class="chat-spinner" aria-hidden="true" />
            <span>Loading conversation…</span>
          </div>
        ) : state.missingApiKey ? (
          <section class="chat-empty-state chat-empty-state--cta">
            <div class="chat-empty-icon" aria-hidden="true">
              <span>🔑</span>
            </div>
            <h2 class="chat-empty-heading">Configure your API key</h2>
            <p class="chat-empty-copy">
              Add an OpenAI or Anthropic key before starting a conversation.
            </p>
            <button
              type="button"
              class="chat-cta-button"
              onClick={() => {
                window.location.hash = '#byok';
              }}
            >
              Configure API key
            </button>
          </section>
        ) : showEmptyState ? (
          <section class="chat-empty-state">
            <div class="chat-empty-icon" aria-hidden="true">
              <span>✦</span>
            </div>
            <h2 class="chat-empty-heading">Start a conversation</h2>
            <p class="chat-empty-copy">AI responses use your BYOK key.</p>
          </section>
        ) : (
          <div
            ref={logRef}
            class="chat-log"
            role="log"
            aria-live="polite"
            aria-relevant="additions text"
            onScroll={handleScroll}
          >
            {state.messages.map((message) => {
              if (message.kind === 'tool-request') {
                return (
                  <ToolRequestCard
                    key={message.id}
                    activeToolCallId={state.toolActionId}
                    message={message}
                    onApprove={(approved) =>
                      void handleApproveTool(message.toolCallId, message.toolName, approved)
                    }
                  />
                );
              }

              if (message.kind === 'tool-result') {
                return <ToolResultCard key={message.id} message={message} />;
              }

              if (message.kind === 'system') {
                return (
                  <div key={message.id} class="chat-system-row">
                    <div class="chat-system-card">{message.content}</div>
                  </div>
                );
              }

              return <ChatBubble key={message.id} message={message} />;
            })}
          </div>
        )}
      </main>

      <form
        class="chat-input-bar"
        onSubmit={(event) => {
          event.preventDefault();
          void sendTextTurn();
        }}
      >
        <input
          ref={fileInputRef}
          type="file"
          accept="image/*"
          class="chat-file-input"
          onChange={(event) => {
            void handleImageUpload(event);
          }}
        />
        <button
          type="button"
          class="chat-attach-button"
          aria-label="Attach image"
          disabled={state.booting || state.missingApiKey || state.isStreaming || !state.sessionId}
          onClick={() => fileInputRef.current?.click()}
        >
          📎
        </button>
        <div class="chat-input-shell">
          <textarea
            class="chat-input"
            aria-label="Message"
            placeholder="Type a message…"
            rows={1}
            value={state.draft}
            disabled={state.booting || state.missingApiKey || !state.sessionId}
            onInput={(event) => {
              dispatch({
                type: 'SET_DRAFT',
                value: (event.target as HTMLTextAreaElement).value,
              });
            }}
            onKeyDown={(event) => {
              if (event.key === 'Enter' && !event.shiftKey) {
                event.preventDefault();
                void sendTextTurn();
              }
            }}
          />
        </div>
        {state.isStreaming && (
          <button
            type="button"
            class="chat-cancel"
            aria-label="Cancel response"
            onClick={() => void handleCancelTurn()}
          >
            Cancel
          </button>
        )}
        <button
          type="submit"
          class="chat-send-button"
          aria-label="Send message"
          disabled={
            state.booting ||
            state.missingApiKey ||
            state.isStreaming ||
            !state.sessionId ||
            !state.draft.trim()
          }
        >
          Send
        </button>
      </form>
    </div>
  );
}

function ChatBubble({ message }: { message: UserMessageItem | AssistantMessageItem }) {
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

function UserMessageContent({ message }: { message: ChatContent }) {
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

function AssistantMessageContent({ message }: { message: AssistantMessageItem }) {
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

function ToolRequestCard(
  {
    activeToolCallId,
    message,
    onApprove,
  }: {
    activeToolCallId: string | null;
    message: ToolRequestMessageItem;
    onApprove: (approved: boolean) => void;
  },
) {
  const pending = message.status === 'pending';
  const busy = activeToolCallId === message.toolCallId;

  return (
    <article class="chat-tool-row">
      <div class="chat-tool-card">
        <div class="chat-tool-header">
          <span class="chat-tool-label">Calling</span>
          <strong class="chat-tool-name">{message.toolName}</strong>
        </div>
        <pre class="chat-tool-code">{message.argsText}</pre>
        <div class="chat-tool-actions">
          {pending ? (
            <>
              <button
                type="button"
                class="chat-tool-button chat-tool-button--approve"
                aria-label={`Approve ${message.toolName}`}
                disabled={busy}
                onClick={() => onApprove(true)}
              >
                {busy ? 'Working…' : 'Approve'}
              </button>
              <button
                type="button"
                class="chat-tool-button chat-tool-button--deny"
                aria-label={`Deny ${message.toolName}`}
                disabled={busy}
                onClick={() => onApprove(false)}
              >
                Deny
              </button>
            </>
          ) : (
            <span class={`chat-tool-state chat-tool-state--${message.status}`}>
              {message.status === 'approved' ? 'Approved' : 'Denied'}
            </span>
          )}
        </div>
      </div>
    </article>
  );
}

function ToolResultCard({ message }: { message: ToolResultMessageItem }) {
  return (
    <article class="chat-tool-row">
      <div class="chat-tool-card chat-tool-card--result">
        <div class="chat-tool-header">
          <span class="chat-tool-label">Result</span>
          <strong class="chat-tool-name">{message.toolName}</strong>
        </div>
        <pre class="chat-tool-code">{formatStructuredValue(message.result)}</pre>
      </div>
    </article>
  );
}

function MarkdownRenderer({ text }: { text: string }) {
  return <div class="chat-markdown">{parseMarkdown(text)}</div>;
}

function parseMarkdown(text: string): JSX.Element[] {
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
  const pattern = /(\*\*[^*]+\*\*|\*[^*]+\*|`[^`]+`|\[[^\]]+\]\([^\)]+\))/g;
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
      const linkMatch = /^\[([^\]]+)\]\(([^\)]+)\)$/.exec(token);
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

function appendAssistantTokens(messages: MessageItem[], text: string): MessageItem[] {
  const streamingIndex = findLastStreamingAssistantIndex(messages);
  if (streamingIndex === -1) {
    return [...messages, createAssistantMessage(text, true)];
  }

  return messages.map((message, index) => {
    if (index !== streamingIndex || message.kind !== 'chat' || message.role !== 'assistant') {
      return message;
    }

    return {
      ...message,
      content: `${message.content}${text}`,
      isStreaming: true,
    };
  });
}

function completeAssistantMessage(messages: MessageItem[], finalMessage: string): MessageItem[] {
  const streamingIndex = findLastStreamingAssistantIndex(messages);
  if (streamingIndex === -1) {
    return finalMessage
      ? [...messages, createAssistantMessage(finalMessage, false)]
      : messages;
  }

  return messages.map((message, index) => {
    if (index !== streamingIndex || message.kind !== 'chat' || message.role !== 'assistant') {
      return message;
    }

    return {
      ...message,
      content: finalMessage || message.content,
      isStreaming: false,
    };
  });
}

function removeEmptyStreamingAssistant(messages: MessageItem[]): MessageItem[] {
  const streamingIndex = findLastStreamingAssistantIndex(messages);
  if (streamingIndex === -1) {
    return messages;
  }

  const candidate = messages[streamingIndex];
  if (
    candidate &&
    candidate.kind === 'chat' &&
    candidate.role === 'assistant' &&
    candidate.content.trim().length === 0 &&
    candidate.images.length === 0
  ) {
    return messages.filter((_, index) => index !== streamingIndex);
  }

  return messages;
}

function markAssistantStreamClosed(messages: MessageItem[]): MessageItem[] {
  return messages.map((message) => {
    if (message.kind !== 'chat' || message.role !== 'assistant') {
      return message;
    }

    return {
      ...message,
      isStreaming: false,
    };
  });
}

function appendImageDelta(messages: MessageItem[], delta: ImageDelta): MessageItem[] {
  const assistantIndex = findLastAssistantIndex(messages);
  if (assistantIndex === -1) {
    return [
      ...messages,
      createAssistantMessage('', true, [createImageFromDelta(delta)]),
    ];
  }

  return messages.map((message, index) => {
    if (index !== assistantIndex || message.kind !== 'chat' || message.role !== 'assistant') {
      return message;
    }

    const nextImages = upsertAssistantImage(message.images, delta);
    return {
      ...message,
      images: nextImages,
    };
  });
}

function upsertAssistantImage(images: RenderedImage[], delta: ImageDelta): RenderedImage[] {
  const imageId = `image-${delta.index ?? images.length}`;
  const existingIndex = images.findIndex((image) => image.id === imageId);
  const existing = existingIndex === -1 ? null : images[existingIndex];

  const mime = delta.mime ?? existing?.mime ?? 'image/png';
  const nextBase64 = delta.url
    ? parseDataUrl(delta.url)?.base64 ?? existing?.rawBase64 ?? ''
    : `${existing?.rawBase64 ?? ''}${delta.b64Chunk ?? ''}`;
  const src = delta.url ?? `data:${mime};base64,${nextBase64}`;
  const nextImage: RenderedImage = {
    id: imageId,
    mime,
    rawBase64: nextBase64,
    src,
  };

  if (existingIndex === -1) {
    return [...images, nextImage];
  }

  return images.map((image, index) => (index === existingIndex ? nextImage : image));
}

function upsertToolRequest(messages: MessageItem[], delta: ToolCallDelta): MessageItem[] {
  const toolCallId = getToolCallId(delta);
  const toolName = delta.name?.trim() || delta.tool?.trim() || 'Unknown tool';
  const existingIndex = messages.findIndex(
    (message) => message.kind === 'tool-request' && message.toolCallId === toolCallId,
  );
  const existing = existingIndex === -1 ? null : (messages[existingIndex] as ToolRequestMessageItem);
  const rawArguments = buildRawArguments(existing?.rawArguments ?? '', delta);
  const argsText = formatToolArguments(rawArguments, delta.args ?? delta.arguments);
  const nextMessage: ToolRequestMessageItem = {
    argsText,
    id: existing?.id ?? createLocalId('tool-request'),
    kind: 'tool-request',
    rawArguments,
    status: existing?.status ?? 'pending',
    toolCallId,
    toolName: toolName || existing?.toolName || 'Unknown tool',
  };

  if (existingIndex === -1) {
    return [...messages, nextMessage];
  }

  return messages.map((message, index) => (index === existingIndex ? nextMessage : message));
}

function updateToolStatus(
  messages: MessageItem[],
  toolCallId: string,
  status: ToolRequestMessageItem['status'],
): MessageItem[] {
  return messages.map((message) => {
    if (message.kind !== 'tool-request' || message.toolCallId !== toolCallId) {
      return message;
    }

    return {
      ...message,
      status,
    };
  });
}

function appendToolResult(
  messages: MessageItem[],
  toolCallId: string,
  toolName: string,
  result: unknown,
): MessageItem[] {
  const exists = messages.some(
    (message) => message.kind === 'tool-result' && message.toolCallId === toolCallId,
  );
  if (exists) {
    return messages.map((message) => {
      if (message.kind !== 'tool-result' || message.toolCallId !== toolCallId) {
        return message;
      }

      return {
        ...message,
        result,
        toolName,
      };
    });
  }

  return [
    ...messages,
    {
      id: createLocalId('tool-result'),
      kind: 'tool-result',
      result,
      toolCallId,
      toolName,
    },
  ];
}

function historyToMessages(history: BridgeChatMessage[]): MessageItem[] {
  const messages: MessageItem[] = [];

  for (const message of history) {
    if (message.role === 'user') {
      messages.push({
        content: { kind: 'text', text: message.content },
        id: message.id,
        kind: 'chat',
        role: 'user',
      });
      continue;
    }

    if (message.role === 'tool') {
      messages.push({
        id: message.id,
        kind: 'tool-result',
        result: safeParseJson(message.content),
        toolCallId: message.id,
        toolName: 'Tool result',
      });
      continue;
    }

    if (message.role === 'system') {
      messages.push({
        content: message.content,
        id: message.id,
        kind: 'system',
      });
      continue;
    }

    messages.push({
      content: message.content,
      id: message.id,
      images: [],
      isStreaming: false,
      kind: 'chat',
      role: 'assistant',
    });

    for (const toolCall of message.toolCalls ?? []) {
      messages.push({
        argsText: formatToolArguments(toolCall.args),
        id: `${message.id}:${toolCall.id}`,
        kind: 'tool-request',
        rawArguments: toolCall.args,
        status: toolCall.result ? 'approved' : 'pending',
        toolCallId: toolCall.id,
        toolName: toolCall.tool,
      });

      if (toolCall.result) {
        messages.push({
          id: `${message.id}:${toolCall.id}:result`,
          kind: 'tool-result',
          result: safeParseJson(toolCall.result),
          toolCallId: toolCall.id,
          toolName: toolCall.tool,
        });
      }
    }
  }

  return messages;
}

function deriveConversationTitle(messages: MessageItem[], resumed: boolean): string {
  const firstUserMessage = messages.find(
    (message): message is UserMessageItem => message.kind === 'chat' && message.role === 'user',
  );
  const text =
    firstUserMessage?.content.kind === 'text'
      ? firstUserMessage.content.text
      : firstUserMessage?.content.text ?? '';
  const trimmed = text.trim();
  if (!trimmed) {
    return resumed ? 'Saved conversation' : 'New conversation';
  }

  return trimmed.length > 42 ? `${trimmed.slice(0, 42).trimEnd()}…` : trimmed;
}

function createUserMessage(content: ChatContent): UserMessageItem {
  return {
    content,
    id: createLocalId('user'),
    kind: 'chat',
    role: 'user',
  };
}

function createAssistantMessage(
  content: string,
  isStreaming: boolean,
  images: RenderedImage[] = [],
): AssistantMessageItem {
  return {
    content,
    id: createLocalId('assistant'),
    images,
    isStreaming,
    kind: 'chat',
    role: 'assistant',
  };
}

function findLastStreamingAssistantIndex(messages: MessageItem[]): number {
  for (let index = messages.length - 1; index >= 0; index -= 1) {
    const message = messages[index];
    if (message?.kind === 'chat' && message.role === 'assistant' && message.isStreaming) {
      return index;
    }
  }

  return -1;
}

function findLastAssistantIndex(messages: MessageItem[]): number {
  for (let index = messages.length - 1; index >= 0; index -= 1) {
    const message = messages[index];
    if (message?.kind === 'chat' && message.role === 'assistant') {
      return index;
    }
  }

  return -1;
}

function getToolCallId(delta: ToolCallDelta): string {
  return delta.id?.trim() || delta.toolCallId?.trim() || `tool-${delta.index ?? 0}`;
}

function buildRawArguments(existing: string, delta: ToolCallDelta): string {
  if (typeof delta.argumentsDelta === 'string') {
    return `${existing}${delta.argumentsDelta}`;
  }

  if (delta.args !== undefined) {
    return formatStructuredValue(delta.args);
  }

  if (delta.arguments !== undefined) {
    return formatStructuredValue(delta.arguments);
  }

  return existing;
}

function formatToolArguments(rawArguments: string, argsValue?: unknown): string {
  if (argsValue !== undefined) {
    return formatStructuredValue(argsValue);
  }

  if (!rawArguments.trim()) {
    return '{}';
  }

  return formatStructuredValue(safeParseJson(rawArguments));
}

function createImageFromDelta(delta: ImageDelta): RenderedImage {
  const parsed = delta.url ? parseDataUrl(delta.url) : null;
  const mime = delta.mime ?? parsed?.mime ?? 'image/png';
  const rawBase64 = parsed?.base64 ?? delta.b64Chunk ?? '';
  return {
    id: `image-${delta.index ?? 0}`,
    mime,
    rawBase64,
    src: delta.url ?? `data:${mime};base64,${rawBase64}`,
  };
}

function formatStructuredValue(value: unknown): string {
  if (typeof value === 'string') {
    const trimmed = value.trim();
    if (!trimmed) {
      return '{}';
    }

    const parsed = safeParseJson(trimmed);
    if (typeof parsed === 'string') {
      return parsed;
    }
    return JSON.stringify(parsed, null, 2);
  }

  return JSON.stringify(value, null, 2) ?? String(value);
}

function safeParseJson(value: string): unknown {
  try {
    return JSON.parse(value) as unknown;
  } catch {
    return value;
  }
}

function buildSystemInstruction(
  {
    pageText,
    pageTitle,
    pageUrl,
    systemInstruction,
  }: {
    pageText?: string;
    pageTitle?: string;
    pageUrl?: string;
    systemInstruction?: string;
  },
): string {
  const trimmedText = pageText?.trim().slice(0, MAX_PAGE_CONTEXT_CHARS) ?? '';
  const trimmedTitle = pageTitle?.trim() ?? '';
  const trimmedUrl = pageUrl?.trim() ?? '';
  let instruction = systemInstruction?.trim() || DEFAULT_SYSTEM_INSTRUCTION;

  if (!trimmedTitle && !trimmedUrl && !trimmedText) {
    return instruction;
  }

  instruction += '\n\nCurrent page context:';
  if (trimmedTitle) {
    instruction += `\nTitle: ${trimmedTitle}`;
  }
  if (trimmedUrl) {
    instruction += `\nURL: ${trimmedUrl}`;
  }
  if (trimmedText) {
    instruction += `\nContent:\n${trimmedText}`;
  }

  return instruction;
}

async function registerDefaultTools(bridge: MahoBridge, handle: string): Promise<void> {
  await Promise.all(DEFAULT_TOOLS.map(async (tool) => bridge.chatRegisterTool(handle, tool)));
}

async function resolveProviderConfig(
  bridge: MahoBridge,
  preferredProviderId?: string,
  preferredModel?: string,
): Promise<ProviderConfig | null> {
  const requestedProvider = preferredProviderId?.trim();
  const requestedModel = preferredModel?.trim();

  const providerOrder = requestedProvider
    ? [requestedProvider, requestedProvider === 'openai' ? 'anthropic' : 'openai']
    : ['openai', 'anthropic'];

  for (const providerId of providerOrder) {
    const key = (await bridge.byokGetKey(providerId))?.trim() ?? '';
    if (!key) {
      continue;
    }

    if (providerId === 'anthropic') {
      return {
        apiKey: key,
        endpoint: DEFAULT_ANTHROPIC_ENDPOINT,
        id: 'anthropic',
        label: 'Anthropic',
        model: requestedModel || DEFAULT_ANTHROPIC_MODEL,
      };
    }

    return {
      apiKey: key,
      endpoint: DEFAULT_OPENAI_ENDPOINT,
      id: 'openai',
      label: 'OpenAI',
      model: requestedModel || DEFAULT_OPENAI_MODEL,
    };
  }

  return null;
}

function parseDataUrl(value: string): { base64: string; mime: string } | null {
  const match = /^data:(.+?);base64,(.+)$/u.exec(value);
  if (!match) {
    return null;
  }

  return {
    base64: match[2] ?? '',
    mime: match[1] ?? 'application/octet-stream',
  };
}

function readFileAsDataUrl(file: File): Promise<string> {
  return new Promise((resolve, reject) => {
    const reader = new FileReader();
    reader.onerror = () => reject(reader.error ?? new Error('Unable to read attachment.'));
    reader.onload = () => {
      if (typeof reader.result !== 'string') {
        reject(new Error('Unable to read attachment.'));
        return;
      }
      resolve(reader.result);
    };
    reader.readAsDataURL(file);
  });
}

function toBridgeToolResult(result: Record<string, unknown>): ToolResult {
  return result as unknown as ToolResult;
}

function toErrorMessage(error: unknown, fallback: string): string {
  if (error instanceof Error) {
    return error.message;
  }
  if (typeof error === 'string' && error.trim()) {
    return error;
  }
  return fallback;
}

function createLocalId(prefix: string): string {
  if (typeof crypto !== 'undefined' && typeof crypto.randomUUID === 'function') {
    return `${prefix}-${crypto.randomUUID()}`;
  }

  return `${prefix}-${Math.random().toString(36).slice(2, 10)}`;
}
