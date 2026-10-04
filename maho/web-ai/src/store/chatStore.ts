/**
 * chatStore.ts — High-cadence streaming store & micro-buffering for Web AI chat.
 *
 * Coalesces high-frequency streaming token/delta events using requestAnimationFrame
 * (or ~16ms fallback window) so that store updates and component re-renders happen
 * at screen refresh rate rather than per-token.
 */
import type { ChatContent, ChatToolCall, ImageDelta, ToolCallDelta } from '../bridge';
import type { CredentialErrorCode } from '../screens/credential-error';

export interface RenderedImage {
  id: string;
  mime: string;
  src: string;
  rawBase64: string;
}

export interface UserMessageItem {
  kind: 'chat';
  id: string;
  role: 'user';
  content: ChatContent;
}

export interface AssistantMessageItem {
  kind: 'chat';
  id: string;
  role: 'assistant';
  content: string;
  images: RenderedImage[];
  isStreaming: boolean;
}

export interface ToolRequestMessageItem {
  kind: 'tool-request';
  id: string;
  toolCallId: string;
  toolName: string;
  rawArguments: string;
  argsText: string;
  autoRun: boolean;
  status: 'pending' | 'approved' | 'denied' | 'unsupported';
}

export interface ToolResultMessageItem {
  kind: 'tool-result';
  id: string;
  toolCallId: string;
  toolName: string;
  result: unknown;
}

export interface SystemMessageItem {
  kind: 'system';
  id: string;
  content: string;
}

export interface ThinkingMessageItem {
  kind: 'thinking';
  id: string;
  thinking: string;
  isStreaming: boolean;
}

export type MessageItem =
  | UserMessageItem
  | AssistantMessageItem
  | ToolRequestMessageItem
  | ToolResultMessageItem
  | ThinkingMessageItem
  | SystemMessageItem;

export interface ProviderConfig {
  apiKey?: string;
  credentialProvider?: string;
  endpoint?: string;
  id: string;
  label: string;
  model?: string;
}

export interface CompleteToolBatch {
  calls: ChatToolCall[];
  nextIndex: number;
}

export interface ChatStoreState {
  booting: boolean;
  conversationId: string | null;
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

export type ChatStoreAction =
  | { type: 'BOOT_START' }
  | { type: 'BOOT_SUCCESS'; conversationId: string; conversationPersisted: boolean; messages: MessageItem[]; provider: ProviderConfig; sessionHandle: string }
  | { type: 'CONVERSATION_PERSISTED'; conversationId: string }
  | { type: 'BOOT_ERROR'; message: string; credentialErrorCode?: CredentialErrorCode | null }
  | { type: 'BYOK_MISSING' }
  | { type: 'POLL_READY'; handle: string }
  | { type: 'SET_DRAFT'; value: string }
  | { type: 'SEND_TURN_START'; message: ChatContent }
  | { type: 'SEND_TURN_FAILURE'; message: string; credentialErrorCode?: CredentialErrorCode | null }
  | { type: 'STREAM_APPEND'; text: string }
  | { type: 'STREAM_THINKING'; text: string }
  | { type: 'STREAM_COMPLETE'; finalMessage: string }
  | { type: 'STREAM_ERROR'; message: string; credentialErrorCode?: CredentialErrorCode | null }
  | { type: 'STREAM_IMAGE'; delta: ImageDelta }
  | { type: 'TOOL_DELTA'; autoRun?: boolean; delta: ToolCallDelta }
  | { type: 'TOOL_RESPONSE_START'; toolCallId: string }
  | { type: 'TOOL_RESPONSE_DONE'; result: unknown; status: 'approved' | 'denied' | 'unsupported'; toolCallId: string; toolName: string }
  | { type: 'TOOL_RESPONSE_ERROR'; message: string; toolCallId: string; credentialErrorCode?: CredentialErrorCode | null }
  | { type: 'TOOL_BATCH_PENDING' }
  | { type: 'TOOL_BATCH_RESOLVED' }
  | { type: 'TURN_CANCELLED' };

export function initialChatStoreState(): ChatStoreState {
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

export function chatStoreReducer(state: ChatStoreState, action: ChatStoreAction): ChatStoreState {
  switch (action.type) {
    case 'BOOT_START':
      return {
        ...state,
        draft: '',
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
        credentialErrorCode: action.credentialErrorCode ?? null,
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
        credentialErrorCode: action.credentialErrorCode ?? null,
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
        credentialErrorCode: action.credentialErrorCode ?? null,
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
        messages: appendToolResult(
          updateToolStatus(state.messages, action.toolCallId, action.status),
          action.toolCallId,
          action.toolName,
          action.result,
        ),
        toolActionId: null,
      };
    case 'TOOL_RESPONSE_ERROR':
      return {
        ...state,
        errorMessage: action.message,
        credentialErrorCode: action.credentialErrorCode ?? null,
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

// -----------------------------------------------------------------------------
// Stream Cadence & Micro-Buffering Coalescer
// -----------------------------------------------------------------------------

export interface StreamCadenceOptions {
  frameWindowMs?: number;
  onTokenFlush?: (text: string) => void;
  onThinkingFlush?: (text: string) => void;
}

/**
 * StreamCadenceBuffer batches incoming streaming token and thinking chunks,
 * scheduling a flush via requestAnimationFrame (or ~16ms window) so that store
 * updates and DOM reconciliations occur at screen refresh rate.
 */
export class StreamCadenceBuffer {
  private tokenBuffer = '';
  private thinkingBuffer = '';
  private rafId: number | null = null;
  private timerId: ReturnType<typeof setTimeout> | null = null;
  private options: StreamCadenceOptions;

  constructor(options: StreamCadenceOptions = {}) {
    this.options = options;
  }

  public setOptions(options: StreamCadenceOptions): void {
    this.options = options;
  }

  public pushToken(text: string): void {
    if (!text) return;
    this.tokenBuffer += text;
    this.scheduleFlush();
  }

  public pushThinking(text: string): void {
    if (!text) return;
    this.thinkingBuffer += text;
    this.scheduleFlush();
  }

  public flush(): void {
    this.cancelScheduled();
    const tokens = this.tokenBuffer;
    const thinking = this.thinkingBuffer;
    this.tokenBuffer = '';
    this.thinkingBuffer = '';

    if (thinking && this.options.onThinkingFlush) {
      this.options.onThinkingFlush(thinking);
    }
    if (tokens && this.options.onTokenFlush) {
      this.options.onTokenFlush(tokens);
    }
  }

  public cancel(): void {
    this.cancelScheduled();
    this.tokenBuffer = '';
    this.thinkingBuffer = '';
  }

  public get hasPending(): boolean {
    return this.tokenBuffer.length > 0 || this.thinkingBuffer.length > 0;
  }

  private scheduleFlush(): void {
    if (this.rafId !== null || this.timerId !== null) {
      return;
    }

    if (typeof globalThis !== 'undefined' && typeof globalThis.requestAnimationFrame === 'function') {
      this.rafId = globalThis.requestAnimationFrame(() => {
        this.rafId = null;
        this.flush();
      });
    } else {
      const windowMs = this.options.frameWindowMs ?? 16;
      this.timerId = setTimeout(() => {
        this.timerId = null;
        this.flush();
      }, windowMs);
    }
  }

  private cancelScheduled(): void {
    if (this.rafId !== null) {
      if (typeof globalThis !== 'undefined' && typeof globalThis.cancelAnimationFrame === 'function') {
        globalThis.cancelAnimationFrame(this.rafId);
      }
      this.rafId = null;
    }
    if (this.timerId !== null) {
      clearTimeout(this.timerId);
      this.timerId = null;
    }
  }
}

/**
 * Coalesces contiguous token and thinking events within an event array to minimize dispatches.
 */
export function coalesceChatEventBatch<T extends { kind: string; token?: string; thinking?: string }>(
  events: readonly T[],
): T[] {
  if (events.length <= 1) {
    return [...events];
  }

  const coalesced: T[] = [];

  for (const event of events) {
    const last = coalesced[coalesced.length - 1];
    if (event.kind === 'token' && typeof event.token === 'string' && last && last.kind === 'token' && typeof last.token === 'string') {
      last.token = `${last.token}${event.token}`;
    } else if (event.kind === 'thinking' && typeof event.thinking === 'string' && last && last.kind === 'thinking' && typeof last.thinking === 'string') {
      last.thinking = `${last.thinking}${event.thinking}`;
    } else {
      coalesced.push({ ...event });
    }
  }

  return coalesced;
}

// -----------------------------------------------------------------------------
// Reactive Chat Store Instance Factory
// -----------------------------------------------------------------------------

export interface ChatStore {
  getState: () => ChatStoreState;
  dispatch: (action: ChatStoreAction) => void;
  subscribe: (listener: (state: ChatStoreState) => void) => () => void;
  pushStreamingToken: (text: string) => void;
  pushStreamingThinking: (text: string) => void;
  flushStreaming: () => void;
  cancelStreaming: () => void;
  destroy: () => void;
}

export function createChatStore(initialState?: Partial<ChatStoreState>): ChatStore {
  let state: ChatStoreState = {
    ...initialChatStoreState(),
    ...initialState,
  };

  const listeners = new Set<(state: ChatStoreState) => void>();

  const cadence = new StreamCadenceBuffer({
    onTokenFlush: (text) => {
      dispatch({ type: 'STREAM_APPEND', text });
    },
    onThinkingFlush: (text) => {
      dispatch({ type: 'STREAM_THINKING', text });
    },
  });

  function getState(): ChatStoreState {
    return state;
  }

  function dispatch(action: ChatStoreAction): void {
    // If a non-token event arrives while tokens are buffered, flush them first
    if (
      action.type !== 'STREAM_APPEND' &&
      action.type !== 'STREAM_THINKING' &&
      cadence.hasPending
    ) {
      cadence.flush();
    }

    const nextState = chatStoreReducer(state, action);
    if (nextState !== state) {
      state = nextState;
      listeners.forEach((listener) => listener(state));
    }
  }

  function subscribe(listener: (state: ChatStoreState) => void): () => void {
    listeners.add(listener);
    return () => {
      listeners.delete(listener);
    };
  }

  function pushStreamingToken(text: string): void {
    cadence.pushToken(text);
  }

  function pushStreamingThinking(text: string): void {
    cadence.pushThinking(text);
  }

  function flushStreaming(): void {
    cadence.flush();
  }

  function cancelStreaming(): void {
    cadence.cancel();
  }

  function destroy(): void {
    cadence.cancel();
    listeners.clear();
  }

  return {
    getState,
    dispatch,
    subscribe,
    pushStreamingToken,
    pushStreamingThinking,
    flushStreaming,
    cancelStreaming,
    destroy,
  };
}

// -----------------------------------------------------------------------------
// Pure Helpers
// -----------------------------------------------------------------------------

export function createUserMessage(content: ChatContent): UserMessageItem {
  return {
    content,
    id: createLocalId('user'),
    kind: 'chat',
    role: 'user',
  };
}

export function createAssistantMessage(
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

export function appendAssistantTokens(messages: MessageItem[], text: string): MessageItem[] {
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

export function completeAssistantMessage(messages: MessageItem[], finalMessage: string): MessageItem[] {
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

export function removeEmptyStreamingAssistant(messages: MessageItem[]): MessageItem[] {
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

export function markAssistantStreamClosed(messages: MessageItem[]): MessageItem[] {
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

export function appendThinkingTokens(messages: MessageItem[], text: string): MessageItem[] {
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

export function closeStreamingThinking(messages: MessageItem[]): MessageItem[] {
  return messages.map((message) =>
    message.kind === 'thinking' && message.isStreaming
      ? { ...message, isStreaming: false }
      : message,
  );
}

export function appendImageDelta(messages: MessageItem[], delta: ImageDelta): MessageItem[] {
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

export function upsertAssistantImage(images: RenderedImage[], delta: ImageDelta): RenderedImage[] {
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

export function upsertToolRequest(
  messages: MessageItem[],
  delta: ToolCallDelta,
  autoRun: boolean,
): MessageItem[] {
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

export function updateToolStatus(
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

export function appendToolResult(
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

export function findLastStreamingAssistantIndex(messages: readonly MessageItem[]): number {
  for (let index = messages.length - 1; index >= 0; index -= 1) {
    const message = messages[index];
    if (message?.kind === 'chat' && message.role === 'assistant' && message.isStreaming) {
      return index;
    }
  }

  return -1;
}

export function findLastAssistantIndex(messages: readonly MessageItem[]): number {
  for (let index = messages.length - 1; index >= 0; index -= 1) {
    const message = messages[index];
    if (message?.kind === 'chat' && message.role === 'assistant') {
      return index;
    }
  }

  return -1;
}

export function getToolCallId(delta: ToolCallDelta): string {
  return delta.id?.trim() || delta.toolCallId?.trim() || `tool-${delta.index ?? 0}`;
}

export function buildRawArguments(existing: string, delta: ToolCallDelta): string {
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

export function formatToolArguments(rawArguments: string, argsValue?: unknown): string {
  if (argsValue !== undefined) {
    return formatStructuredValue(argsValue);
  }

  if (!rawArguments.trim()) {
    return '{}';
  }

  return formatStructuredValue(safeParseJson(rawArguments));
}

export function createImageFromDelta(delta: ImageDelta): RenderedImage {
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

export function formatStructuredValue(value: unknown): string {
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

export function safeParseJson(value: string): unknown {
  try {
    return JSON.parse(value) as unknown;
  } catch {
    return value;
  }
}

export function parseDataUrl(value: string): { base64: string; mime: string } | null {
  const match = /^data:(.+?);base64,(.+)$/u.exec(value);
  if (!match) {
    return null;
  }

  return {
    base64: match[2] ?? '',
    mime: match[1] ?? 'application/octet-stream',
  };
}

export function createLocalId(prefix: string): string {
  if (typeof crypto !== 'undefined' && typeof crypto.randomUUID === 'function') {
    return `${prefix}-${crypto.randomUUID()}`;
  }

  return `${prefix}-${Math.random().toString(36).slice(2, 10)}`;
}
