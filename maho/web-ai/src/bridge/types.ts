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
  archivedAt?: string | null;
  projectId?: string | null;
}

export interface ConversationProject {
  id: string;
  name: string;
  createdAt: string;
  updatedAt: string;
}

export interface ConversationProjectMoveResult {
  requestedCount: number;
  affectedIds: string[];
  missingIds: string[];
}

export type ConversationListState = 'active' | 'archived' | 'all';

export interface ConversationListOptions {
  state?: ConversationListState;
  limit?: number;
}

export interface ConversationBulkResult {
  requestedCount: number;
  affectedIds: string[];
  unchangedIds: string[];
  missingIds: string[];
}

export interface ConversationActiveSessionsError {
  error: 'active_sessions';
  ids: string[];
}

export type ConversationBulkResponse = ConversationBulkResult | ConversationActiveSessionsError;

/**
 * Scope of a persisted composer draft.
 *
 * `new_task` is the draft scope for any composer whose conversation row does not
 * exist in core yet — including a brand-new chat that already holds an ephemeral
 * session handle. Naming a conversation that core does not know about makes core
 * reap the draft as an orphan on read, so promotion to `conversation` scope must
 * wait until the row is persisted (first successful send).
 */
export type ComposerDraftScope =
  | { kind: 'new_task' }
  | { kind: 'conversation'; conversationId: string };

export interface ComposerDraft {
  version: 1;
  text: string;
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

/**
 * AI provider settings, mirroring the desktop `AISettings` mojo struct
 * (maho_settings.mojom). Stored per-shell (Chromium prefs on desktop,
 * UserDefaults/Keychain on iOS, SharedPreferences on Android).
 *
 * provider: "" | "maho-managed" | "openai" | "anthropic" | "openai-compatible"
 */
export interface AiSettings {
  provider: string;
  baseUrl: string;
  model: string;
  hasApiKey: boolean;
  hasByokOpenai: boolean;
  hasByokAnthropic: boolean;
}

export type SessionHandle = string;
export type AgentHandle = string;
export type Provider = string;
export type Platform = 'ios' | 'android' | 'unknown';

export type BridgeError =
  | { kind: 'session_invalid'; id: string }
  | { kind: 'method_unknown'; method: string }
  | { kind: 'params_invalid'; method: string; reason: string }
  | { kind: 'operation_failed'; reason: string }
  | { kind: 'native_error'; method: string; reason: string };

export type ChatEvent =
  | { kind: 'token'; token: string }
  | { kind: 'thinking'; thinking: string }
  | { kind: 'tool_call'; toolCallId: string; name: string; args: Record<string, unknown> }
  | { kind: 'complete'; content: string; toolCalls?: ChatToolCall[]; toolCallsJson?: string }
  | { kind: 'error'; message: string; credentialErrorCode?: string | null };

export interface ChatToolCall {
  id: string;
  name: string;
  args: unknown;
}

export type BrowserToolInvokeResult =
  | { ok: true; result: unknown }
  | { ok: false; error: string };

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
  | { kind: 'thinking'; thinking: string }
  | { kind: 'tool_call'; id: string; name: string; args: string }
  | { kind: 'tool_result'; id: string; name: string; result: string }
  | { kind: 'artifact_created'; artifact: ArtifactInfo }
  | {
      kind: 'interaction_request';
      /** Reply key for interaction resolve/timeout/cancel. */
      id: string;
      /** question = ask_user_question; confirmation = request_action_confirmation */
      interactionKind: 'question' | 'confirmation';
      /** Prompt text: the question, or the confirmation effect description. */
      question: string;
      /** Selectable options; empty for confirmations. */
      options: AgentInteractionRequestOption[];
      /** Kernel-side lifecycle state ("pending" for live requests), when present. */
      state?: string;
      /** Opaque review artifact reference, when the request carries one. */
      artifactRef?: string | null;
    }
  | { kind: 'complete'; fullText: string }
  | { kind: 'error'; message: string; credentialErrorCode?: string | null };

/**
 * Session runtime configuration mirroring the FFI AgentRuntimeConfig
 * (maho_agent_set_runtime_config, plan row 1): the broker-enforced permission
 * tier plus the final-confirm and proactive-mode gates. Tier strings mirror
 * maho-agent RuntimeTier ("read_only" | "guard" | "full_access").
 */
export interface AgentRuntimeConfig {
  permissionTier: 'read_only' | 'guard' | 'full_access';
  finalConfirm: boolean;
  proactiveMode: boolean;
}

/**
 * One selectable option of an agent interaction request question. Mirrors
 * maho-agent interaction::InteractionOption.
 */
export interface AgentInteractionRequestOption {
  id: string;
  label: string;
  description?: string;
}

/**
 * Metadata for an agent output artifact. Paths never cross this boundary;
 * artifactId is an opaque handle resolved natively.
 */
export interface ArtifactInfo {
  artifactId: string;
  sessionId: string;
  displayName: string;
  mimeType: string;
  sizeBytes: number;
  createdAt: number;
}

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
  credentialProvider?: string;
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

export interface ChatToolResultOptions {
  toolName: string;
  trigger: boolean;
}

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

export type BridgeRpcErrorCode =
  | 'rpc_timeout'
  | 'rpc_aborted'
  | 'bridge_disposed'
  | 'rpc_overloaded';

export interface CallOptions {
  readonly signal?: AbortSignal;
  readonly timeoutMs?: number;
}

export interface BridgeResourceClaimPayload {
  readonly requestId: string;
}

export interface BridgeRequestAbandonPayload {
  readonly requestId: string;
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

  // --- AI provider settings (desktop parity: maho_settings.mojom) ---
  getAiSettings(): Promise<AiSettings>;
  setAiProvider(provider: string): Promise<void>;
  setAiBaseUrl(url: string): Promise<void>;
  setAiApiKey(key: string): Promise<void>;
  setAiModel(model: string): Promise<void>;

  // --- Chat Session ---
  chatSessionStart(opts: ChatSessionOpts): Promise<SessionHandle>;
  chatSessionResume(conversationId: string, opts: ChatSessionOpts): Promise<SessionHandle>;
  chatSessionFree(handle: SessionHandle): Promise<void>;
  chatSendMessage(handle: SessionHandle, content: ChatContent): Promise<void>;
  chatCancelTurn(handle: SessionHandle): Promise<void>;
  chatPollEvents(handle: SessionHandle): Promise<unknown[]>;
  chatRegisterTool(handle: SessionHandle, tool: ToolDefinition): Promise<void>;
  chatSendToolResult(
    handle: SessionHandle,
    toolCallId: string,
    result: ToolResult,
    options: ChatToolResultOptions,
  ): Promise<void>;
  browserToolInvoke?(name: string, args: Record<string, unknown>): Promise<BrowserToolInvokeResult>;
  chatAppendAssistantMessage(
    handle: SessionHandle,
    content: string,
    toolCallsJson?: string,
  ): Promise<void>;
  chatGetHistory(handle: SessionHandle): Promise<ChatMessage[]>;

  // --- Conversations ---
  conversationCreate(meta: ConversationMeta): Promise<string>;
  conversationList(options?: ConversationListOptions): Promise<ConversationMeta[]>;
  conversationGet(sessionId: string): Promise<ConversationMeta | null>;
  conversationDelete(sessionId: string): Promise<boolean>;
  conversationRename(sessionId: string, title: string): Promise<boolean>;
  conversationArchive(sessionId: string): Promise<boolean>;
  conversationUnarchive(sessionId: string): Promise<boolean>;
  conversationBulk(
    op: 'archive' | 'unarchive' | 'delete',
    ids: string[],
  ): Promise<ConversationBulkResponse>;
  conversationGetAutoArchivePolicy(): Promise<number | null>;
  conversationSetAutoArchivePolicy(afterDays: number | null): Promise<boolean>;
  conversationGetMessages(sessionId: string): Promise<ChatMessage[]>;
  conversationProjectList(): Promise<ConversationProject[]>;
  conversationProjectCreate(name: string): Promise<ConversationProject>;
  conversationProjectRename(id: string, name: string): Promise<boolean>;
  conversationProjectDelete(id: string): Promise<boolean>;
  conversationProjectMove(ids: string[], projectId: string | null): Promise<ConversationProjectMoveResult>;
  saveConversationMessage?(sessionId: string, role: string, content: string): Promise<boolean>;

  // --- Composer drafts (device/profile-local; never synced) ---
  // Optional: a host without draft support simply keeps drafts in memory.
  composerDraftGet?(scope: ComposerDraftScope): Promise<ComposerDraft | null>;
  composerDraftSet?(scope: ComposerDraftScope, text: string): Promise<boolean>;
  composerDraftDelete?(scope: ComposerDraftScope): Promise<boolean>;

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
  agentListArtifacts(handle: AgentHandle): Promise<ArtifactInfo[]>;
  artifactShare(handle: AgentHandle, artifactId: string): Promise<boolean>;
  // Returns JSON array of AgentToolDescriptor for the session's registered tools.
  agentListTools(handle: AgentHandle): Promise<AgentToolDescriptor[]>;
  /**
   * Resolves a pending interaction request (FFI `maho_agent_interaction_resolve`).
   * `answerJson` is the canonical kernel answer JSON, parsed by maho-agent
   * `parse_interaction_answer`:
   *   {"answer_kind":"confirmed"} | {"answer_kind":"denied"}
   *   | {"answer_kind":"selected_option","0":"<optionId>"}
   *   | {"answer_kind":"text","0":"<free text>"}
   * Returns false if the request is unknown or already terminal.
   */
  agentResolveInteraction?(handle: AgentHandle, requestId: string, answerJson: string): Promise<boolean>;
  /**
   * Applies session runtime config (FFI `maho_agent_set_runtime_config`).
   * Returns false if the broker rejected the config — fail-closed callers
   * must not proceed with the pending action.
   */
  agentSetRuntimeConfig?(handle: AgentHandle, config: AgentRuntimeConfig): Promise<boolean>;

  // --- Native-only actions ---
  capturePhoto(): Promise<PhotoCapture | null>;
  captureScreenshot(): Promise<Uint8Array | null>;
  openSettings(): Promise<void>;
  hapticFeedback(style: HapticStyle): Promise<void>;

  // --- Relay auth (gated onboarding) ---
  relaySignIn(email: string, password: string): Promise<{ ok: boolean; error?: string }>;
  relaySignUp(email: string, password: string, displayName: string): Promise<{ ok: boolean; error?: string }>;
  relaySignInWithGoogle(): Promise<{ ok: boolean; cancelled?: boolean; error?: string }>;
  relayAccountStatus(): Promise<{ hasValidSession: boolean; isReauth: boolean }>;

  // --- Default browser ---
  openDefaultBrowserSettings(): Promise<void>;

  // --- Finish ---
  completeOnboarding(): Promise<void>;
}
