import { useCallback, useEffect, useMemo, useReducer, useRef } from 'preact/hooks';
import type { ChatContent, ChatMessage as BridgeChatMessage, ChatSessionOpts, ComposerDraftScope, MahoBridge, ToolDefinition, ToolResult } from '../../bridge';
import { useBridge } from '../../hooks/use-bridge';
import { useStreaming } from '../../hooks/use-streaming';
import { useComposerDraft } from '../../storage/use-composer-draft';
import { Icon } from '../../ui/icon';
import { parseCredentialErrorCode, type CredentialErrorCode } from '../credential-error';
import { MessageList } from '../../components/Chat/MessageList';
import type { CompleteToolBatch, MessageItem, ProviderConfig, UserMessageItem } from '../../store/chatStore';
import {
  chatStoreReducer,
  formatStructuredValue,
  initialChatStoreState,
  parseDataUrl,
  safeParseJson,
} from '../../store/chatStore';
import './chat.css';

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
  {
    name: 'get_page_elements',
    description: 'List visible interactive page elements with numeric refs, labels, and bounding boxes',
    parameters: {
      type: 'object',
      additionalProperties: false,
      properties: {},
    },
  },
  {
    name: 'get_page_snapshot',
    description: 'Get the current viewport and visible interactive elements with numeric refs',
    parameters: {
      type: 'object',
      additionalProperties: false,
      properties: {},
    },
  },
  {
    name: 'click_element',
    description: 'Click a page element by numeric ref from a page snapshot or by CSS selector',
    parameters: {
      type: 'object',
      additionalProperties: false,
      properties: {
        id: { type: 'integer', minimum: 1, description: 'Numeric element ref from get_page_elements/get_page_snapshot' },
        selector: { type: 'string', minLength: 1, description: 'CSS selector fallback' },
      },
    },
  },
  {
    name: 'fill_input',
    description: 'Fill an input, textarea, or contenteditable element and fire input/change events',
    parameters: {
      type: 'object',
      additionalProperties: false,
      properties: {
        id: { type: 'integer', minimum: 1, description: 'Numeric element ref from get_page_elements/get_page_snapshot' },
        selector: { type: 'string', minLength: 1, description: 'CSS selector fallback' },
        text: { type: 'string', description: 'Text to enter' },
      },
      required: ['text'],
    },
  },
  {
    name: 'scroll_page',
    description: 'Smoothly scroll the current page up, down, left, or right',
    parameters: {
      type: 'object',
      additionalProperties: false,
      properties: {
        direction: { type: 'string', enum: ['up', 'down', 'left', 'right'] },
        amount: { type: 'number', minimum: 1, maximum: 10000, description: 'Optional scroll distance in CSS pixels' },
      },
      required: ['direction'],
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
  const [state, dispatch] = useReducer(chatStoreReducer, undefined, initialChatStoreState);
  const logRef = useRef<HTMLDivElement | null>(null);
  const fileInputRef = useRef<HTMLInputElement | null>(null);
  const draftRef = useRef('');
  const stickToBottomRef = useRef(true);
  const activeHandleRef = useRef<string | null>(null);
  const bootstrapGenerationRef = useRef(0);
  const completeToolBatchesRef = useRef(new Map<string, CompleteToolBatch>());

  const routeSessionId = sessionId?.trim() || null;

  const bootstrapSession = useCallback(
    async (requestedSessionId: string | null) => {
      const generation = ++bootstrapGenerationRef.current;
      const previousHandle = activeHandleRef.current;

      dispatch({ type: 'BOOT_START' });

      let nextHandle: string | null = null;
      try {
        const provider = await resolveProviderConfig(bridge, providerId, model);
        if (generation !== bootstrapGenerationRef.current) return;

        if (!provider) {
          activeHandleRef.current = null;
          if (previousHandle) {
            void bridge.chatSessionFree(previousHandle).catch(() => undefined);
          }
          dispatch({ type: 'BYOK_MISSING' });
          return;
        }

        const sessionOptions: ChatSessionOpts = {
          ...(provider.apiKey === undefined ? {} : { apiKey: provider.apiKey }),
          ...(provider.credentialProvider === undefined
            ? {}
            : { credentialProvider: provider.credentialProvider }),
          ...(provider.endpoint === undefined ? {} : { endpoint: provider.endpoint }),
          ...(provider.model === undefined ? {} : { model: provider.model }),
          systemInstruction: buildSystemInstruction({
            pageText,
            pageTitle,
            pageUrl,
            systemInstruction,
          }),
        };

        if (requestedSessionId) {
          nextHandle = await bridge.chatSessionResume(requestedSessionId, sessionOptions);
        } else {
          nextHandle = await bridge.chatSessionStart(sessionOptions);
        }

        await registerDefaultTools(bridge, nextHandle);
        const conversationId = requestedSessionId ?? nextHandle;
        const history = requestedSessionId ? await bridge.chatGetHistory(conversationId) : [];

        if (generation !== bootstrapGenerationRef.current) {
          if (nextHandle && nextHandle !== previousHandle) {
            void bridge.chatSessionFree(nextHandle).catch(() => undefined);
          }
          return;
        }

        activeHandleRef.current = nextHandle;
        dispatch({
          type: 'BOOT_SUCCESS',
          conversationId,
          // A resumed conversation already has a row; a fresh session does not.
          conversationPersisted: requestedSessionId !== null,
          messages: historyToMessages(history),
          provider,
          sessionHandle: nextHandle,
        });

        if (previousHandle && previousHandle !== nextHandle) {
          void bridge.chatSessionFree(previousHandle).catch(() => undefined);
        }
      } catch (error) {
        if (nextHandle && nextHandle !== previousHandle) {
          void bridge.chatSessionFree(nextHandle).catch(() => undefined);
        }
        if (generation !== bootstrapGenerationRef.current) return;
        activeHandleRef.current = null;
        if (previousHandle) {
          void bridge.chatSessionFree(previousHandle).catch(() => undefined);
        }
        if (generation === bootstrapGenerationRef.current) {
          const message = toErrorMessage(error, 'Unable to open this chat right now.');
          dispatch({
            type: 'BOOT_ERROR',
            message,
            credentialErrorCode: parseCredentialErrorCode(message),
          });
        }
      }
    },
    [bridge, model, pageText, pageTitle, pageUrl, providerId, systemInstruction],
  );

  useEffect(() => {
    void bootstrapSession(routeSessionId);
  }, [bootstrapSession, routeSessionId]);

  draftRef.current = state.draft;

  const draftScope = useMemo<ComposerDraftScope | null>(() => {
    if (state.booting || state.missingApiKey) return null;
    if (state.conversationPersisted && state.conversationId !== null) {
      return { kind: 'conversation', conversationId: state.conversationId };
    }
    return { kind: 'new_task' };
  }, [state.booting, state.conversationId, state.conversationPersisted, state.missingApiKey]);

  const restoreDraft = useCallback((text: string) => {
    dispatch({ type: 'SET_DRAFT', value: text });
  }, []);

  const drafts = useComposerDraft({
    bridge,
    onRestore: restoreDraft,
    scope: draftScope,
  });

  const setDraft = useCallback(
    (value: string) => {
      dispatch({ type: 'SET_DRAFT', value });
      drafts.handleDraftChange(value);
    },
    [drafts],
  );

  // A failed send returns the text to the composer unless the user already typed
  // a replacement while the send was in flight.
  const restoreUnsentDraft = useCallback((text: string) => {
    if (draftRef.current.trim().length > 0) return;
    dispatch({ type: 'SET_DRAFT', value: text });
  }, []);

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

  const executeTool = useCallback(
    async (toolCallId: string, toolName: string, rawArguments: string, trigger = true) => {
      const handle = state.sessionHandle;
      if (!handle) {
        return false;
      }

      dispatch({ type: 'TOOL_RESPONSE_START', toolCallId });

      try {
        const args = parseToolArgumentsForExecution(rawArguments);
        const result: ToolResult = args === null
          ? { error: 'invalid_arguments' }
          : bridge.browserToolInvoke
            ? { output: JSON.stringify(await bridge.browserToolInvoke(toolName, args)) }
            : { error: 'tool_execution_unsupported' };
        await bridge.chatSendToolResult(handle, toolCallId, result, { toolName, trigger });
        dispatch({
          type: 'TOOL_RESPONSE_DONE',
          result,
          status: result.error === 'tool_execution_unsupported' ? 'unsupported' : 'approved',
          toolCallId,
          toolName,
        });
        return true;
      } catch (error) {
        const message = toErrorMessage(error, 'Unable to respond to this tool call.');
        dispatch({
          type: 'TOOL_RESPONSE_ERROR',
          message,
          credentialErrorCode: parseCredentialErrorCode(message),
          toolCallId,
        });
        return false;
      }
    },
    [bridge, state.sessionHandle],
  );

  const executeCompletedReadTools = useCallback(
    async (batch: CompleteToolBatch) => {
      while (batch.nextIndex < batch.calls.length) {
        const toolCall = batch.calls[batch.nextIndex];
        if (!isReadTool(toolCall.name)) {
          return;
        }
        const didExecute = await executeTool(
          toolCall.id,
          toolCall.name,
          formatStructuredValue(toolCall.args),
          batch.nextIndex === batch.calls.length - 1,
        );
        if (!didExecute) {
          return;
        }
        batch.nextIndex += 1;
      }
      for (const toolCall of batch.calls) {
        completeToolBatchesRef.current.delete(toolCall.id);
      }
      dispatch({ type: 'TOOL_BATCH_RESOLVED' });
    },
    [executeTool],
  );

  useStreaming({
    bridge,
    sessionId: state.sessionHandle,
    onReady: (handle) => {
      dispatch({ type: 'POLL_READY', handle });
    },
    onEvents: (events) => {
      for (const event of events) {
        switch (event.kind) {
          case 'token':
            dispatch({ type: 'STREAM_APPEND', text: event.token });
            break;
          case 'thinking':
            dispatch({ type: 'STREAM_THINKING', text: event.thinking });
            break;
          case 'complete':
            dispatch({ type: 'STREAM_COMPLETE', finalMessage: event.content });
            const toolCalls = event.toolCalls ?? [];
            const completeToolBatch: CompleteToolBatch = { calls: toolCalls, nextIndex: 0 };
            if (toolCalls.length > 0) {
              dispatch({ type: 'TOOL_BATCH_PENDING' });
            }
            for (const toolCall of toolCalls) {
              const autoRun = isReadTool(toolCall.name);
              completeToolBatchesRef.current.set(toolCall.id, completeToolBatch);
              dispatch({
                type: 'TOOL_DELTA',
                autoRun,
                delta: {
                  args: toolCall.args,
                  id: toolCall.id,
                  name: toolCall.name,
                },
              });
            }
            if (toolCalls.length > 0 && state.sessionHandle) {
              const toolCallsJson = event.toolCallsJson ?? JSON.stringify(toolCalls.map((toolCall) => ({
                arguments_json: formatStructuredValue(toolCall.args),
                id: toolCall.id,
                name: toolCall.name,
              })));
              void (async () => {
                try {
                  await bridge.chatAppendAssistantMessage(state.sessionHandle!, event.content, toolCallsJson);
                  await executeCompletedReadTools(completeToolBatch);
                } catch (error) {
                  const message = toErrorMessage(error, 'Unable to record this tool call.');
                  dispatch({
                    type: 'TOOL_RESPONSE_ERROR',
                    message,
                    credentialErrorCode: parseCredentialErrorCode(message),
                    toolCallId: toolCalls[completeToolBatch.nextIndex]?.id ?? toolCalls[0].id,
                  });
                }
              })();
            }
            if (state.conversationId) {
              void bridge.saveConversationMessage?.(state.conversationId, 'assistant', event.content).catch(() => undefined);
            }
            break;
          case 'error':
            dispatch({
              type: 'STREAM_ERROR',
              message: event.message,
              credentialErrorCode: (event.credentialErrorCode as CredentialErrorCode | undefined) ?? parseCredentialErrorCode(event.message),
            });
            break;
          case 'tool_call':
            dispatch({
              type: 'TOOL_DELTA',
              delta: {
                args: event.args,
                id: event.toolCallId,
                name: event.name,
              },
            });
            break;
        }
      }
    },
    onPollError: (message) => {
      dispatch({
        type: 'STREAM_ERROR',
        message,
        credentialErrorCode: parseCredentialErrorCode(message),
      });
    },
  });

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
    const handle = state.sessionHandle;
    const text = state.draft.trim();
    if (
      !handle ||
      state.pollReadyHandle !== handle ||
      !text ||
      state.isStreaming ||
      state.booting ||
      state.missingApiKey ||
      state.toolBatchPending
    ) {
      return;
    }

    const content: ChatContent = { kind: 'text', text };
    const acknowledgeDraft = drafts.captureSend();
    dispatch({ type: 'SEND_TURN_START', message: content });

    try {
      await bridge.chatSendMessage(handle, content);
      if (state.conversationId) {
        void bridge.saveConversationMessage?.(state.conversationId, 'user', text).catch(() => undefined);
        dispatch({ type: 'CONVERSATION_PERSISTED', conversationId: state.conversationId });
      }
      void acknowledgeDraft(state.conversationId ? { kind: 'conversation', conversationId: state.conversationId } : undefined);
    } catch (error) {
      restoreUnsentDraft(text);
      const message = toErrorMessage(error, 'Unable to send this message.');
      dispatch({
        type: 'SEND_TURN_FAILURE',
        message,
        credentialErrorCode: parseCredentialErrorCode(message),
      });
    }
  }, [bridge, draftScope, drafts, restoreUnsentDraft, state.booting, state.conversationId, state.draft, state.isStreaming, state.missingApiKey, state.pollReadyHandle, state.sessionHandle, state.toolBatchPending]);

  const handleCancelTurn = useCallback(async () => {
    const handle = state.sessionHandle;
    if (!handle || !state.isStreaming) {
      return;
    }

    try {
      await bridge.chatCancelTurn(handle);
    } finally {
      dispatch({ type: 'TURN_CANCELLED' });
    }
  }, [bridge, state.isStreaming, state.sessionHandle]);

  const handleApproveTool = useCallback(
    async (toolCallId: string, toolName: string, rawArguments: string, approved: boolean) => {
      if (approved) {
        const batch = completeToolBatchesRef.current.get(toolCallId);
        if (batch !== undefined && batch.calls[batch.nextIndex]?.id !== toolCallId) {
          return;
        }
        const trigger = batch !== undefined && batch.calls[batch.nextIndex]?.id === toolCallId
          ? batch.nextIndex === batch.calls.length - 1
          : true;
        const didExecute = await executeTool(toolCallId, toolName, rawArguments, trigger);
        if (didExecute && batch !== undefined && batch.calls[batch.nextIndex]?.id === toolCallId) {
          batch.nextIndex += 1;
          await executeCompletedReadTools(batch);
        }
        return;
      }

      const handle = state.sessionHandle;
      if (!handle) return;

      dispatch({ type: 'TOOL_RESPONSE_START', toolCallId });
      const result: ToolResult = { error: 'user_denied' };

      try {
        const batch = completeToolBatchesRef.current.get(toolCallId);
        if (batch !== undefined && batch.calls[batch.nextIndex]?.id !== toolCallId) {
          return;
        }
        const trigger = batch !== undefined && batch.calls[batch.nextIndex]?.id === toolCallId
          ? batch.nextIndex === batch.calls.length - 1
          : true;
        await bridge.chatSendToolResult(handle, toolCallId, result, { toolName, trigger });
        dispatch({
          type: 'TOOL_RESPONSE_DONE',
          result,
          status: 'denied',
          toolCallId,
          toolName,
        });
        if (batch !== undefined && batch.calls[batch.nextIndex]?.id === toolCallId) {
          batch.nextIndex += 1;
          await executeCompletedReadTools(batch);
        }
      } catch (error) {
        const message = toErrorMessage(error, 'Unable to respond to this tool call.');
        dispatch({
          type: 'TOOL_RESPONSE_ERROR',
          message,
          credentialErrorCode: parseCredentialErrorCode(message),
          toolCallId,
        });
      }
    },
    [bridge, executeCompletedReadTools, executeTool, state.sessionHandle],
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
      const handle = state.sessionHandle;
      if (
        !handle ||
        state.pollReadyHandle !== handle ||
        state.booting ||
        state.missingApiKey ||
        state.isStreaming ||
        state.toolBatchPending
      ) {
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

        const acknowledgeDraft = drafts.captureSend();
        dispatch({ type: 'SEND_TURN_START', message: content });
        await bridge.chatSendMessage(handle, content);
        if (state.conversationId) {
          dispatch({ type: 'CONVERSATION_PERSISTED', conversationId: state.conversationId });
        }
        void acknowledgeDraft(state.conversationId ? { kind: 'conversation', conversationId: state.conversationId } : undefined);
      } catch (error) {
        restoreUnsentDraft(state.draft.trim());
        const message = toErrorMessage(error, 'Unable to attach this image.');
        dispatch({
          type: 'SEND_TURN_FAILURE',
          message,
          credentialErrorCode: parseCredentialErrorCode(message),
        });
      }
    },
    [bridge, draftScope, drafts, restoreUnsentDraft, state.booting, state.conversationId, state.draft, state.isStreaming, state.missingApiKey, state.pollReadyHandle, state.sessionHandle, state.toolBatchPending],
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
                ? state.provider.model
                  ? `${state.provider.label} · ${state.provider.model}`
                  : state.provider.label
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
        {state.errorMessage && !state.credentialErrorCode && (
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
          <section class="chat-empty-state">
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
          <MessageList
            logRef={logRef}
            messages={state.messages}
            toolActionId={state.toolActionId}
            credentialErrorCode={state.credentialErrorCode}
            onScroll={handleScroll}
            onApproveTool={(toolCallId, toolName, rawArguments, approved) =>
              void handleApproveTool(toolCallId, toolName, rawArguments, approved)
            }
          />
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
          onInput={(event) => {
            void handleImageUpload(event);
          }}
        />
        <button
          type="button"
          class="chat-attach-button"
          aria-label="Attach image"
          disabled={
            state.booting ||
            state.missingApiKey ||
            state.isStreaming ||
            state.toolBatchPending ||
            !state.sessionHandle ||
            state.pollReadyHandle !== state.sessionHandle
          }
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
            disabled={
              state.booting ||
              state.missingApiKey ||
              state.toolBatchPending ||
              !state.sessionHandle ||
              state.pollReadyHandle !== state.sessionHandle
            }
            onBlur={() => drafts.flush()}
            onInput={(event) => {
              setDraft((event.target as HTMLTextAreaElement).value);
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
            state.toolBatchPending ||
            !state.sessionHandle ||
            state.pollReadyHandle !== state.sessionHandle ||
            !state.draft.trim()
          }
        >
          Send
        </button>
      </form>
    </div>
  );
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
      const rawArgsStr = typeof toolCall.args === 'string'
        ? toolCall.args
        : JSON.stringify(toolCall.args ?? {});
      messages.push({
        argsText: formatToolArguments(rawArgsStr, typeof toolCall.args !== 'string' ? toolCall.args : undefined),
        autoRun: false,
        id: `${message.id}:${toolCall.id}`,
        kind: 'tool-request',
        rawArguments: rawArgsStr,
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

function parseToolArgumentsForExecution(rawArguments: string): Record<string, unknown> | null {
  const parsed = safeParseJson(rawArguments);
  return typeof parsed === 'object' && parsed !== null && !Array.isArray(parsed)
    ? (parsed as Record<string, unknown>)
    : null;
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

function isReadTool(toolName: string): boolean {
  return (
    toolName === 'search_bookmarks' ||
    toolName === 'list_tabs' ||
    toolName === 'get_page_info' ||
    toolName === 'search_history' ||
    toolName === 'get_page_elements' ||
    toolName === 'get_page_snapshot' ||
    toolName === 'scroll_page'
  );
}

function buildSystemInstruction({
  pageText,
  pageTitle,
  pageUrl,
  systemInstruction,
}: {
  pageText?: string;
  pageTitle?: string;
  pageUrl?: string;
  systemInstruction?: string;
}): string {
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
  _preferredProviderId?: string,
  preferredModel?: string,
): Promise<ProviderConfig | null> {
  const settings = await bridge.getAiSettings();
  const providerId = settings.provider.trim();
  const configuredModel = settings.model.trim() || preferredModel?.trim();

  switch (providerId) {
    case 'openai': {
      const apiKey = (await bridge.byokGetKey('openai'))?.trim() ?? '';
      if (!apiKey) return null;
      return {
        apiKey,
        endpoint: normalizeChatCompletionsEndpoint(DEFAULT_OPENAI_ENDPOINT),
        id: providerId,
        label: 'OpenAI',
        model: configuredModel || DEFAULT_OPENAI_MODEL,
      };
    }
    case 'anthropic': {
      const apiKey = (await bridge.byokGetKey('anthropic'))?.trim() ?? '';
      if (!apiKey) return null;
      return {
        apiKey,
        endpoint: DEFAULT_ANTHROPIC_ENDPOINT,
        id: providerId,
        label: 'Anthropic',
        model: configuredModel || DEFAULT_ANTHROPIC_MODEL,
      };
    }
    case 'openai-compatible': {
      const baseUrl = settings.baseUrl.trim();
      if (!baseUrl || !configuredModel) return null;
      return {
        credentialProvider: providerId,
        endpoint: normalizeChatCompletionsEndpoint(baseUrl),
        id: providerId,
        label: 'OpenAI-compatible',
        model: configuredModel,
      };
    }
    case 'maho-managed':
      return {
        credentialProvider: providerId,
        id: providerId,
        label: 'Maho AI',
      };
    default:
      return null;
  }
}

function normalizeChatCompletionsEndpoint(baseUrl: string): string {
  const trimmed = baseUrl.trim().replace(/\/+$/u, '');
  return trimmed.endsWith('/chat/completions')
    ? trimmed
    : `${trimmed}/chat/completions`;
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

function toErrorMessage(error: unknown, fallback: string): string {
  if (error instanceof Error) {
    return error.message;
  }
  if (typeof error === 'string' && error.trim()) {
    return error;
  }
  return fallback;
}
