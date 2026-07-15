// Bridge type contracts — shared across all platforms.
// Implementations: rpc.ts (mobile postMessage) and the existing Mojo page handler (desktop).

export interface ConversationSession {
  id: string;
  title: string | null;
  spaceId: string | null;
  model: string | null;
  createdAt: string;
  updatedAt: string;
}

export interface ConversationMeta {
  id: string;
  title: string | null;
  spaceId?: string | null;
  model?: string | null;
  createdAt: string;
  updatedAt: string;
}

export interface ConversationTurn {
  id: string;
  sessionId: string;
  role: 'user' | 'assistant' | 'system' | 'tool';
  content: string;
  urlContext: string | null;
  createdAt: string;
}

export interface SpaceAIConfig {
  spaceId?: string;
  model: string | null;
  systemInstruction: string | null;
  enabled: boolean;
}

export interface OfflineModelInfo {
  name: string;
  sizeBytes: number;
  quantization: string;
  downloadedAt: string | null;
}

export interface PhotoCapture {
  mime: string;
  data: Uint8Array;
}

export type HapticStyle = 'light' | 'medium' | 'heavy';

export type SessionHandle = string;
export type AgentHandle = string;
export type Provider = string;
export type Platform = 'ios' | 'android' | 'unknown';

export type BridgeError =
  | { kind: 'session_invalid'; id: string }
  | { kind: 'method_unknown'; method: string }
  | { kind: 'params_invalid'; method: string; reason: string }
  | { kind: 'native_error'; method: string; reason: string };

export type ChatEvent =
  | { kind: 'token'; token: string }
  | { kind: 'tool_call'; toolCallId: string; name: string; args: Record<string, unknown> }
  | { kind: 'complete'; content: string }
  | { kind: 'error'; message: string };

/**
 * Normalized event from the native agent kernel.
 *
 * The native wire format is `{ "type": "token"|"complete"|"error", "data": <string|object> }`.
 * This union maps those directly after normalization in use-agent-session.ts.
 *
 * - token:    streaming LLM delta; accumulate to build full text
 * - complete: turn finished; fullText is the entire response
 * - error:    turn failed; message is the AgentError display string
 */
export type AgentEvent =
  | { kind: 'token'; token: string }
  | { kind: 'complete'; fullText: string }
  | { kind: 'error'; message: string };

/**
 * ToolDescriptor returned by agentListTools().
 * Mirrors maho-types/src/tool.rs ToolDescriptor (camelCase serde).
 */
export interface AgentToolDescriptor {
  name: string;
  description: string;
  /** JSON Schema object for the tool's parameters (key: "inputSchema" on wire) */
  inputSchema: Record<string, unknown>;
  provenance: AgentToolProvenance;
  sensitive: boolean;
  permission: AgentToolPermission;
}

export type AgentToolProvenance =
  | 'builtin_browser'
  | 'agent_local'
  | { external: string };

export type AgentToolPermission =
  | 'auto_approve'
  | 'session_approve'
  | 'always_ask';

export type ChatContent =
  | { kind: 'text'; text: string }
  | { kind: 'image'; mime: string; base64: string; text?: string };

export interface ChatMessage {
  id: string;
  role: 'user' | 'assistant' | 'system' | 'tool';
  content: string;
  toolCalls?: ToolCallRecord[];
}

export interface ToolCallRecord {
  id: string;
  tool: string;
  args: unknown;
  result?: string;
}

export interface ChatSessionOpts {
  apiKey?: string;
  endpoint?: string;
  model?: string;
  systemInstruction?: string;
}

export interface CostEstimate {
  inputTokens: number;
  outputTokens: number;
  estimatedCostUsd: number;
  model: string;
}

export interface KeyValidationResult {
  valid: boolean;
  error?: string;
}

export interface PinchSummaryEvent {
  kind: 'extractive_default' | 'llm_pending' | 'llm_streaming' | 'llm_success' | 'llm_failed';
  tabId: string;
  sentences: string[];
  reason?: string;
}

export interface ToolDefinition {
  name: string;
  description: string;
  parameters?: Record<string, unknown>;
}

export type ToolResult = Record<string, unknown>;

export interface RpcRequest {
  jsonrpc: '2.0';
  id: string;
  method: string;
  params?: Record<string, unknown> | unknown[];
}

export interface RpcResponse {
  jsonrpc: '2.0';
  id: string;
  result?: unknown;
  error?: BridgeError;
}

/**
 * Typed bridge surface. Every method returns a Promise — the RPC
 * dispatcher maps each call to the appropriate platform mechanism.
 */
export interface MahoBridge {
  // --- BYOK ---
  byokGetProviders(): Promise<string[]>;
  byokGetKey(provider: string): Promise<string | null>;
  byokSetKey(provider: string, key: string): Promise<boolean>;
  byokDeleteKey(provider: string): Promise<boolean>;
  byokValidateKey(provider: string, key: string): Promise<boolean>;

  // --- Chat Session ---
  chatSessionStart(opts: ChatSessionOpts): Promise<SessionHandle>;
  chatSessionResume(handle: SessionHandle): Promise<void>;
  chatSessionFree(handle: SessionHandle): Promise<void>;
  chatSendMessage(handle: SessionHandle, content: ChatContent): Promise<void>;
  chatCancelTurn(handle: SessionHandle): Promise<void>;
  chatPollEvents(handle: SessionHandle): Promise<ChatEvent[]>;
  chatRegisterTool(handle: SessionHandle, tool: ToolDefinition): Promise<void>;
  chatSendToolResult(handle: SessionHandle, toolCallId: string, result: ToolResult): Promise<void>;
  chatAppendAssistantMessage(handle: SessionHandle, content: string): Promise<void>;
  chatGetHistory(handle: SessionHandle): Promise<ChatMessage[]>;

  // --- Conversations ---
  conversationCreate(meta: ConversationMeta): Promise<string>;
  conversationList(): Promise<ConversationMeta[]>;
  conversationGet(sessionId: string): Promise<ConversationMeta | null>;
  conversationDelete(sessionId: string): Promise<boolean>;
  conversationRename(sessionId: string, title: string): Promise<boolean>;
  conversationGetMessages(sessionId: string): Promise<ChatMessage[]>;

  // --- Space AI Config ---
  getSpaceAIConfig(spaceId: string): Promise<SpaceAIConfig | null>;
  setSpaceAIConfig(spaceId: string, config: SpaceAIConfig): Promise<void>;

  // --- Pinch ---
  pinchEstimateCost(pageContent: string, requestedModel?: string): Promise<CostEstimate>;

  // --- Agent Session ---
  // agentCreateSession allocates a native MahoAgentSession (opaque pointer).
  // The returned AgentHandle is an opaque string managed by the native bridge registry.
  // Caller MUST call agentFreeSession when done.
  agentCreateSession(sessionId: string): Promise<AgentHandle>;
  agentFreeSession(handle: AgentHandle): Promise<void>;
  agentSendMessage(handle: AgentHandle, message: string): Promise<boolean>;
  agentCancel(handle: AgentHandle): Promise<boolean>;
  // Returns a single queued event JSON string, or null if the queue is empty.
  // Events are `{ "type": "token"|"complete"|"error", "data": string | object }`.
  agentPollEvent(handle: AgentHandle): Promise<string | null>;
  // Returns JSON array of AgentToolDescriptor for the session's registered tools.
  agentListTools(handle: AgentHandle): Promise<AgentToolDescriptor[]>;

  // --- Native-only actions ---
  capturePhoto(): Promise<PhotoCapture | null>;
  captureScreenshot(): Promise<Uint8Array | null>;
  openSettings(): Promise<void>;
  hapticFeedback(style: HapticStyle): Promise<void>;
}
