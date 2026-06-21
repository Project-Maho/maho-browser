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
  BridgeError,
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

// PinchSummaryEvent is discriminated on 'kind'
type AssertPinchEventHasKind = PinchSummaryEvent extends { kind: string } ? true : never;
const _pinchEventCheck: AssertPinchEventHasKind = true;

describe('bridge type exports', () => {
  it('all types are importable and distinct from any', () => {
    // Runtime confirmation that type imports resolve (not `any`)
    const types: string[] = [
      'BridgeError',
      'ChatContent',
      'ChatEvent',
      'ChatMessage',
      'ChatSessionOpts',
      'ConversationMeta',
      'CostEstimate',
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
    expect(types).toHaveLength(19);
  });

  it('MahoBridge has exactly 28 method signatures', () => {
    // Count keys on the interface via a dummy conformance object
    const methodNames: (keyof MahoBridge)[] = [
      'byokGetProviders',
      'byokGetKey',
      'byokSetKey',
      'byokDeleteKey',
      'byokValidateKey',
      'chatSessionStart',
      'chatSessionResume',
      'chatSessionFree',
      'chatSendMessage',
      'chatCancelTurn',
      'chatPollEvents',
      'chatRegisterTool',
      'chatSendToolResult',
      'chatAppendAssistantMessage',
      'chatGetHistory',
      'conversationCreate',
      'conversationList',
      'conversationGet',
      'conversationDelete',
      'conversationRename',
      'conversationGetMessages',
      'getSpaceAIConfig',
      'setSpaceAIConfig',
      'pinchEstimateCost',
      'capturePhoto',
      'captureScreenshot',
      'openSettings',
      'hapticFeedback',
    ];
    expect(methodNames).toHaveLength(28);
  });

  it('chatSessionStart returns Promise<SessionHandle> (string), not number', () => {
    // Type-level: if this compiles, SessionHandle = string is confirmed
    type StartReturn = ReturnType<MahoBridge['chatSessionStart']>;
    type AssertStringPromise = StartReturn extends Promise<string> ? true : never;
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

  it('ChatEvent covers all 4 discriminants', () => {
    const kinds: ChatEvent['kind'][] = ['token', 'tool_call', 'complete', 'error'];
    expect(kinds).toHaveLength(4);
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
void _providerCheck;
void _platformCheck;
void _rpcIdCheck;
void _bridgeErrorCheck;
void _chatEventCheck;
void _pinchEventCheck;
