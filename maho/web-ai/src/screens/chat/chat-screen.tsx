import { type JSX } from 'preact';
import { useCallback, useEffect, useMemo, useReducer, useRef, useState } from 'preact/hooks';
import type { ChatContent, ChatMessage as BridgeChatMessage, ChatSessionOpts, ChatToolCall, ComposerDraftScope, ImageDelta, MahoBridge, ToolCallDelta, ToolDefinition, ToolResult } from '../../bridge';
import { useBridge } from '../../hooks/use-bridge';
import { useStreaming } from '../../hooks/use-streaming';
import { useComposerDraft } from '../../storage/use-composer-draft';
import { Icon } from '../../ui/icon';
import { parseCredentialErrorCode, type CredentialErrorCode } from '../credential-error';
import { CredentialErrorCard } from '../credential-error-card';
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
  apiKey?: string;
  credentialProvider?: string;
  endpoint?: string;
  id: string;
  label: string;
  model?: string;
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
  autoRun: boolean;
  status: 'pending' | 'approved' | 'denied' | 'unsupported';
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

interface ThinkingMessageItem {
  kind: 'thinking';
  id: string;
  thinking: string;
  isStreaming: boolean;
}

interface CompleteToolBatch {
  calls: ChatToolCall[];
  nextIndex: number;
}

type MessageItem =
  | UserMessageItem
  | AssistantMessageItem
  | ToolRequestMessageItem
  | ToolResultMessageItem
  | ThinkingMessageItem
  | SystemMessageItem;

interface ChatScreenState {
  booting: boolean;
  conversationId: string | null;
  /**
   * True once the conversation row exists in core. A brand-new chat holds an
   * ephemeral session handle long before any row is written, and core reaps a
   * draft whose conversation does not exist, so drafts stay in the `new_task`
   * scope until this flips after the first successful send.
   */
  conversationPersisted: boolean;
  draft: string;
  errorMessage: string | null;
  credentialErrorCode: CredentialErrorCode | null;
  isStreaming: boolean;
  messages: MessageItem[];
  missingApiKey: boolean;
  pollReadyHandle: string | null;
  provider: ProviderConfig | null;
  sessionHandle: string | null;
  toolActionId: string | null;
  toolBatchPending: boolean;
}

type Action =
  | { type: 'BOOT_START' }
  | { type: 'BOOT_SUCCESS'; conversationId: string; conversationPersisted: boolean; messages: MessageItem[]; provider: ProviderConfig; sessionHandle: string }
  | { type: 'CONVERSATION_PERSISTED'; conversationId: string }
  | { type: 'BOOT_ERROR'; message: string }
  | { type: 'BYOK_MISSING' }
  | { type: 'POLL_READY'; handle: string }
  | { type: 'SET_DRAFT'; value: string }
  | { type: 'SEND_TURN_START'; message: ChatContent }
  | { type: 'SEND_TURN_FAILURE'; message: string }
  | { type: 'STREAM_APPEND'; text: string }
  | { type: 'STREAM_THINKING'; text: string }
  | { type: 'STREAM_COMPLETE'; finalMessage: string }
  | { type: 'STREAM_ERROR'; message: string }
  | { type: 'STREAM_IMAGE'; delta: ImageDelta }
  | { type: 'TOOL_DELTA'; autoRun?: boolean; delta: ToolCallDelta }
  | { type: 'TOOL_RESPONSE_START'; toolCallId: string }
  | { type: 'TOOL_RESPONSE_DONE'; result: unknown; status: 'approved' | 'denied' | 'unsupported'; toolCallId: string; toolName: string }
  | { type: 'TOOL_RESPONSE_ERROR'; message: string; toolCallId: string }
  | { type: 'TOOL_BATCH_PENDING' }
  | { type: 'TOOL_BATCH_RESOLVED' }
  | { type: 'TURN_CANCELLED' };

function initialState(): ChatScreenState {
  return {
    booting: true,
    conversationId: null,
    conversationPersisted: false,
    draft: '',
    errorMessage: null,
    credentialErrorCode: null,
    isStreaming: false,
    messages: [],
    missingApiKey: false,
    pollReadyHandle: null,
    provider: null,
    sessionHandle: null,
    toolActionId: null,
    toolBatchPending: false,
  };
}

function reducer(state: ChatScreenState, action: Action): ChatScreenState {
  switch (action.type) {
    case 'BOOT_START':
      return {
        ...state,
        booting: true,
        conversationId: null,
        conversationPersisted: false,
        errorMessage: null,
        credentialErrorCode: null,
        isStreaming: false,
        messages: [],
        missingApiKey: false,
        pollReadyHandle: null,
        provider: null,
        sessionHandle: null,
        toolActionId: null,
        toolBatchPending: false,
      };
    case 'BOOT_SUCCESS':
      return {
        ...state,
        booting: false,
        conversationId: action.conversationId,
        conversationPersisted: action.conversationPersisted,
        errorMessage: null,
        isStreaming: false,
        messages: action.messages,
        missingApiKey: false,
        pollReadyHandle: null,
        provider: action.provider,
        sessionHandle: action.sessionHandle,
        toolActionId: null,
        toolBatchPending: false,
        credentialErrorCode: null,
      };
    case 'CONVERSATION_PERSISTED':
      if (state.conversationId !== action.conversationId || state.conversationPersisted) {
        return state;
      }
      return {
        ...state,
        conversationPersisted: true,
      };
    case 'BOOT_ERROR':
      return {
        ...state,
        booting: false,
        conversationId: null,
        conversationPersisted: false,
        errorMessage: action.message,
        credentialErrorCode: parseCredentialErrorCode(action.message),
        isStreaming: false,
        pollReadyHandle: null,
        provider: null,
        sessionHandle: null,
        toolActionId: null,
        toolBatchPending: false,
      };
    case 'BYOK_MISSING':
      return {
        ...state,
        booting: false,
        conversationId: null,
        conversationPersisted: false,
        errorMessage: null,
        credentialErrorCode: null,
        isStreaming: false,
        messages: [],
        missingApiKey: true,
        pollReadyHandle: null,
        provider: null,
        sessionHandle: null,
        toolActionId: null,
        toolBatchPending: false,
      };
    case 'POLL_READY':
      if (state.sessionHandle !== action.handle) {
        return state;
      }
      return {
        ...state,
        pollReadyHandle: action.handle,
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
        credentialErrorCode: null,
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
        credentialErrorCode: parseCredentialErrorCode(action.message),
        isStreaming: false,
        messages: removeEmptyStreamingAssistant(state.messages),
      };
    case 'STREAM_APPEND':
      return {
        ...state,
        errorMessage: null,
        credentialErrorCode: null,
        isStreaming: true,
        messages: appendAssistantTokens(closeStreamingThinking(state.messages), action.text),
      };
    case 'STREAM_THINKING':
      return {
        ...state,
        errorMessage: null,
        credentialErrorCode: null,
        isStreaming: true,
        messages: appendThinkingTokens(removeEmptyStreamingAssistant(state.messages), action.text),
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
        credentialErrorCode: parseCredentialErrorCode(action.message),
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
        messages: upsertToolRequest(state.messages, action.delta, action.autoRun ?? false),
      };
    case 'TOOL_RESPONSE_START':
      return {
        ...state,
        errorMessage: null,
        credentialErrorCode: null,
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
        credentialErrorCode: parseCredentialErrorCode(action.message),
        toolActionId: null,
        messages: updateToolStatus(state.messages, action.toolCallId, 'pending'),
      };
    case 'TOOL_BATCH_PENDING':
      return {
        ...state,
        toolBatchPending: true,
      };
    case 'TOOL_BATCH_RESOLVED':
      return {
        ...state,
        toolBatchPending: false,
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

      try {
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
        dispatch({
          type: 'TOOL_RESPONSE_ERROR',
          message: toErrorMessage(error, 'Unable to respond to this tool call.'),
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
                  dispatch({
                    type: 'TOOL_RESPONSE_ERROR',
                    message: toErrorMessage(error, 'Unable to record this tool call.'),
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
            dispatch({ type: 'STREAM_ERROR', message: event.message });
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
      dispatch({ type: 'STREAM_ERROR', message });
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
      state.missingApiKey
      || state.toolBatchPending
    ) {
      return;
    }

    const content: ChatContent = { kind: 'text', text };
    const sendScope = draftScope;
    // SEND_TURN_START only clears the in-memory composer; the persisted draft is
    // deleted below, after the native send is accepted.
    dispatch({ type: 'SEND_TURN_START', message: content });

    try {
      await bridge.chatSendMessage(handle, content);
      if (state.conversationId) {
        void bridge.saveConversationMessage?.(state.conversationId, 'user', text).catch(() => undefined);
        // The message write creates the conversation row, so the draft scope can
        // now be promoted from new_task to this conversation.
        dispatch({ type: 'CONVERSATION_PERSISTED', conversationId: state.conversationId });
      }
      if (sendScope !== null) {
        void drafts.clearScope(sendScope);
      }
    } catch (error) {
      restoreUnsentDraft(text);
      dispatch({
        type: 'SEND_TURN_FAILURE',
        message: toErrorMessage(error, 'Unable to send this message.'),
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
        dispatch({
          type: 'TOOL_RESPONSE_ERROR',
          message: toErrorMessage(error, 'Unable to respond to this tool call.'),
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

        const sendScope = draftScope;
        dispatch({ type: 'SEND_TURN_START', message: content });
        await bridge.chatSendMessage(handle, content);
        if (state.conversationId) {
          dispatch({ type: 'CONVERSATION_PERSISTED', conversationId: state.conversationId });
        }
        if (sendScope !== null) {
          void drafts.clearScope(sendScope);
        }
      } catch (error) {
        restoreUnsentDraft(state.draft.trim());
        dispatch({
          type: 'SEND_TURN_FAILURE',
          message: toErrorMessage(error, 'Unable to attach this image.'),
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
                      void handleApproveTool(message.toolCallId, message.toolName, message.rawArguments, approved)
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

              if (message.kind === 'thinking') {
                return <ThinkingBubble key={message.id} message={message} />;
              }

              return <ChatBubble key={message.id} message={message} />;
            })}
            {state.credentialErrorCode && (
              <CredentialErrorCard code={state.credentialErrorCode} />
            )}
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

function ToolResultCard({ message }: { message: ToolResultMessageItem }) {
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

function ThinkingBubble({ message }: { message: ThinkingMessageItem }) {
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

function upsertToolRequest(messages: MessageItem[], delta: ToolCallDelta, autoRun: boolean): MessageItem[] {
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
    autoRun: existing?.autoRun ?? autoRun,
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

function appendThinkingTokens(messages: MessageItem[], text: string): MessageItem[] {
  const last = messages[messages.length - 1];
  if (last && last.kind === 'thinking' && last.isStreaming) {
    return messages.map((message, index) =>
      index === messages.length - 1 && message.kind === 'thinking'
        ? { ...message, thinking: `${message.thinking}${text}` }
        : message,
    );
  }

  return [
    ...messages,
    { id: createLocalId('thinking'), isStreaming: true, kind: 'thinking', thinking: text },
  ];
}

function closeStreamingThinking(messages: MessageItem[]): MessageItem[] {
  return messages.map((message) =>
    message.kind === 'thinking' && message.isStreaming
      ? { ...message, isStreaming: false }
      : message,
  );
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

function parseToolArgumentsForExecution(rawArguments: string): Record<string, unknown> | null {
  const parsed = safeParseJson(rawArguments);
  return typeof parsed === 'object' && parsed !== null && !Array.isArray(parsed)
    ? parsed as Record<string, unknown>
    : null;
}

function isReadTool(toolName: string): boolean {
  return toolName === 'search_bookmarks' ||
    toolName === 'list_tabs' ||
    toolName === 'get_page_info' ||
    toolName === 'search_history';
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

function toolResultDisplayValue(result: unknown): unknown {
  if (
    typeof result === 'object' &&
    result !== null &&
    !Array.isArray(result) &&
    typeof (result as Record<string, unknown>).output === 'string'
  ) {
    return safeParseJson((result as Record<string, string>).output);
  }
  return result;
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
