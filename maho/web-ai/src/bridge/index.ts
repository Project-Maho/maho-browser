// Barrel export for src/bridge/

export type {
  BridgeError,
  ChatContent,
  ChatEvent,
  ChatMessage,
  ChatSessionOpts,
  ConversationMeta,
  CostEstimate,
  HapticStyle,
  KeyValidationResult,
  MahoBridge,
  PhotoCapture,
  PinchSummaryEvent,
  Platform,
  Provider,
  RpcRequest,
  RpcResponse,
  SessionHandle,
  SpaceAIConfig,
  ToolCallRecord,
  ToolDefinition,
  ToolResult,
} from './types';

export { callBridge, call, isBridgeRpcError, detectPlatform } from './rpc';
export { subscribeStream } from './streaming';
export type { ImageDelta, StreamSubscriber, ToolCallDelta } from './streaming';
