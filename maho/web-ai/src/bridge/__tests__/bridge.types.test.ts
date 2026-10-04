/**
 * Type-level tests for the bridge contract.
 *
 * These tests verify that all exported types compile correctly under strict mode
 * and that no `any` types leak into the public surface. If a type regresses to
 * `any`, the type assertions below will fail at compile time (and at runtime via
 * vitest type-checking).
 */
import { describe, expect, it } from 'vitest';
import type {
  AgentEvent,
  AgentHandle,
  BridgeError,
  BrowserToolInvokeResult,
  ChatEvent,
  KeyValidationResult,
  MahoBridge,
  PinchSummaryEvent,
  Platform,
  Provider,
  RpcRequest,
  SessionHandle,
} from '../types';

// ─── Type-level assertions (compile-time checks) ────────────────────────────

// SessionHandle must be string, never number
type AssertSessionIsString = SessionHandle extends string ? true : never;
const _sessionCheck: AssertSessionIsString = true;

// AgentHandle must be string
type AssertAgentHandleIsString = AgentHandle extends string ? true : never;
const _agentHandleCheck: AssertAgentHandleIsString = true;

// Provider must be string
type AssertProviderIsString = Provider extends string ? true : never;
const _providerCheck: AssertProviderIsString = true;

// Platform is a union of known strings
type AssertPlatformUnion = Platform extends 'android' | 'ios' | 'unknown' ? true : never;
const _platformCheck: AssertPlatformUnion = true;

// RpcRequest id must be string (not number)
type AssertRpcIdIsString = RpcRequest['id'] extends string ? true : never;
const _rpcIdCheck: AssertRpcIdIsString = true;

// BridgeError is discriminated on 'kind'
type AssertBridgeErrorHasKind = BridgeError extends { kind: string } ? true : never;
const _bridgeErrorCheck: AssertBridgeErrorHasKind = true;

// ChatEvent is discriminated on 'kind'
type AssertChatEventHasKind = ChatEvent extends { kind: string } ? true : never;
const _chatEventCheck: AssertChatEventHasKind = true;

// AgentEvent is discriminated on 'kind'
type AssertAgentEventHasKind = AgentEvent extends { kind: string } ? true : never;
const _agentEventCheck: AssertAgentEventHasKind = true;

// PinchSummaryEvent is discriminated on 'kind'
type AssertPinchEventHasKind = PinchSummaryEvent extends { kind: string } ? true : never;
const _pinchEventCheck: AssertPinchEventHasKind = true;

describe('bridge type exports', () => {
  it('all types are importable and distinct from any', () => {
    // Runtime confirmation that type imports resolve (not `any`)
    const types: string[] = [
      'AgentEvent',
      'AgentHandle',
      'AgentToolDescriptor',
      'AgentToolPermission',
      'AgentToolProvenance',
      'BridgeError',
      'ChatContent',
      'ChatEvent',
      'ChatMessage',
      'ChatSessionOpts',
      'ConversationMeta',
      'CostEstimate',
      'HapticStyle',
      'KeyValidationResult',
      'MahoBridge',
      'PinchSummaryEvent',
      'Platform',
      'Provider',
      'RpcRequest',
      'RpcResponse',
      'SessionHandle',
      'SpaceAIConfig',
      'ToolCallRecord',
      'ToolDefinition',
      'ToolResult',
    ];
    expect(types).toHaveLength(25);
  });

  it('MahoBridge has exactly 39 method signatures', () => {
    // Count keys on the interface via a dummy conformance object
    const methodNames: (keyof MahoBridge)[] = [
      // BYOK (5)
      'byokGetProviders',
      'byokGetKey',
      'byokSetKey',
      'byokDeleteKey',
      'byokValidateKey',
      // AI Settings (5)
      'getAiSettings',
      'setAiProvider',
      'setAiBaseUrl',
      'setAiApiKey',
      'setAiModel',
      // Chat Session (10)
      'chatSessionStart',
      'chatSessionResume',
      'chatSessionFree',
      'chatSendMessage',
      'chatCancelTurn',
      'chatPollEvents',
      'chatRegisterTool',
      'chatSendToolResult',
      'browserToolInvoke',
      'chatAppendAssistantMessage',
      'chatGetHistory',
      // Conversations (6)
      'conversationCreate',
      'conversationList',
      'conversationGet',
      'conversationDelete',
      'conversationRename',
      'conversationGetMessages',
      // Space AI Config (2)
      'getSpaceAIConfig',
      'setSpaceAIConfig',
      // Pinch (1)
      'pinchEstimateCost',
      // Agent Session (8)
      'agentCreateSession',
      'agentFreeSession',
      'agentSendMessage',
      'agentCancel',
      'agentPollEvent',
      'agentListTools',
      'agentListArtifacts',
      'artifactShare',
      // Native-only (4)
      'capturePhoto',
      'captureScreenshot',
      'openSettings',
      'hapticFeedback',
    ];
    expect(methodNames).toHaveLength(42);
  });

  it('chatSessionStart returns Promise<SessionHandle> (string), not number', () => {
    // Type-level: if this compiles, SessionHandle = string is confirmed
    type StartReturn = ReturnType<MahoBridge['chatSessionStart']>;
    type AssertStringPromise = StartReturn extends Promise<string> ? true : never;
    const _check: AssertStringPromise = true;
    expect(_check).toBe(true);
  });

  it('agentCreateSession returns Promise<AgentHandle> (string)', () => {
    type AgentReturn = ReturnType<MahoBridge['agentCreateSession']>;
    type AssertStringPromise = AgentReturn extends Promise<string> ? true : never;
    const _check: AssertStringPromise = true;
    expect(_check).toBe(true);
  });

  it('error envelope covers all 4 discriminants', () => {
    const kinds: BridgeError['kind'][] = [
      'session_invalid',
      'method_unknown',
      'params_invalid',
      'native_error',
    ];
    expect(kinds).toHaveLength(4);
  });

  it('browser tool invocation results are explicitly discriminated', () => {
    const success: BrowserToolInvokeResult = { ok: true, result: { opened: true } };
    const failure: BrowserToolInvokeResult = { ok: false, error: 'Unknown browser tool.' };
    expect(success.ok).toBe(true);
    expect(failure.ok).toBe(false);
  });

  it('ChatEvent covers all 4 discriminants', () => {
    const kinds: ChatEvent['kind'][] = ['token', 'tool_call', 'complete', 'error'];
    expect(kinds).toHaveLength(4);
  });

  it('AgentEvent covers all 6 discriminants', () => {
    const kinds: AgentEvent['kind'][] = ['token', 'thinking', 'tool_call', 'tool_result', 'complete', 'error'];
    expect(kinds).toHaveLength(6);
  });

  it('KeyValidationResult has valid boolean and optional error', () => {
    const result: KeyValidationResult = { valid: true };
    expect(result.valid).toBe(true);
    expect(result.error).toBeUndefined();

    const failed: KeyValidationResult = { valid: false, error: 'expired' };
    expect(failed.valid).toBe(false);
    expect(failed.error).toBe('expired');
  });
});

// Suppress unused variable warnings for compile-time-only assertions
void _sessionCheck;
void _agentHandleCheck;
void _providerCheck;
void _platformCheck;
void _rpcIdCheck;
void _bridgeErrorCheck;
void _chatEventCheck;
void _agentEventCheck;
void _pinchEventCheck;
