import { detectPlatform } from './platform-detect';
import type { BridgeError, CallOptions } from './types';
import { parseCredentialErrorCode, type CredentialErrorCode } from '../screens/credential-error';

export { detectPlatform };

interface JsonRpcResponse {
  jsonrpc: '2.0';
  id: string;
  result?: unknown;
  error?: BridgeError;
}

interface PendingCall {
  resolve: (value: unknown) => void;
  reject: (reason: Error) => void;
  timer: ReturnType<typeof setTimeout> | null;
  signal: AbortSignal | null;
  onAbort: (() => void) | null;
  resourceCreating: boolean;
  settled: boolean;
}

const _pending = new Map<string, PendingCall>();

// U15 bounded pending-call lifetime: at most 256 in-flight calls; overloaded
// callers get a typed rejection instead of unbounded queue growth.
const MAX_PENDING_CALLS = 256;
const DEFAULT_CALL_TIMEOUT_MS = 30_000;
const LONG_CALL_TIMEOUT_MS = 300_000;
const FAST_CALL_TIMEOUT_MS = 10_000;
const LONG_TIMEOUT_METHODS: ReadonlySet<string> = new Set([
  'agentCreateSession',
  'chatSessionStart',
  'chatSessionResume',
  'browserToolInvoke',
  'capturePhoto',
  'artifactShare',
  'relaySignInWithGoogle',
  'openDefaultBrowserSettings',
]);
const FAST_TIMEOUT_METHODS: ReadonlySet<string> = new Set([
  'agentPollEvent',
  'chatPollEvents',
]);
const RESOURCE_CREATING_METHODS: ReadonlySet<string> = new Set([
  'agentCreateSession',
  'chatSessionStart',
  'chatSessionResume',
  'capturePhoto',
  'artifactShare',
]);

let _documentDisposed = false;

function defaultTimeoutFor(method: string): number {
  if (LONG_TIMEOUT_METHODS.has(method)) return LONG_CALL_TIMEOUT_MS;
  if (FAST_TIMEOUT_METHODS.has(method)) return FAST_CALL_TIMEOUT_MS;
  return DEFAULT_CALL_TIMEOUT_MS;
}

function rpcControlError(code: string): Error {
  const err = new Error(code);
  (err as unknown as { code: string }).code = code;
  return err;
}

/// Exactly-once settlement: removes the pending entry and every deadline/
/// abort resource before rejecting. A late duplicate response finds no entry.
function settleCall(id: string, error: Error): void {
  const entry = _pending.get(id);
  if (!entry || entry.settled) return;
  entry.settled = true;
  if (entry.timer !== null) {
    clearTimeout(entry.timer);
    entry.timer = null;
  }
  if (entry.signal !== null && entry.onAbort !== null) {
    entry.signal.removeEventListener('abort', entry.onAbort);
  }
  _pending.delete(id);
  entry.reject(error);
}

/// Best-effort internal control notification to the native host. Losing this
/// message is a transport failure, not a promise of a usable session.
function notifyNativeControl(method: 'bridgeResourceClaim' | 'bridgeRequestAbandon', requestId: string): void {
  try {
    sendNative(
      JSON.stringify({ jsonrpc: '2.0', id: uuidv4(), method, params: { requestId } }),
    );
  } catch {
    // Transport unavailable: the caller's own settlement still happened.
  }
}

export class BridgeRpcError extends Error {
  bridgeError: BridgeError;
  constructor(bridgeError: BridgeError) {
    super(`Bridge RPC error: ${JSON.stringify(bridgeError)}`);
    this.bridgeError = bridgeError;
  }
}

export function isBridgeRpcError(err: unknown): err is BridgeRpcError {
  return (
    err instanceof BridgeRpcError ||
    (typeof err === 'object' && err !== null && 'bridgeError' in err)
  );
}

// Injected by native as a global so the bridge can resolve pending promises.
(
  window as unknown as { __mahoBridgeResponse: (responseJson: string) => void }
).__mahoBridgeResponse = (responseJson: string): void => {
  let parsed: JsonRpcResponse;
  try {
    parsed = JSON.parse(responseJson) as JsonRpcResponse;
  } catch {
    console.error('[maho-bridge] malformed response', responseJson);
    return;
  }

  const entry = _pending.get(parsed.id);
  if (!entry || entry.settled) return;
  entry.settled = true;
  if (entry.timer !== null) {
    clearTimeout(entry.timer);
    entry.timer = null;
  }
  if (entry.signal !== null && entry.onAbort !== null) {
    entry.signal.removeEventListener('abort', entry.onAbort);
  }
  _pending.delete(parsed.id);

  if (parsed.error) {
    const reason = 'reason' in parsed.error ? parsed.error.reason : null;
    const credentialCode = reason === null ? null : parseCredentialErrorCode(reason);
    const genericMessage = `[maho-bridge] ${parsed.error.kind}`;
    const safeReason = credentialCode === null ? genericMessage : credentialErrorEnvelope(credentialCode);
    const err = new Error(safeReason);
    const bridgeError = reason !== null && isCredentialEnvelopeCandidate(reason) && credentialCode === null
      ? { ...parsed.error, reason: genericMessage }
      : parsed.error;
    (err as unknown as { bridgeError: BridgeError }).bridgeError = bridgeError;
    entry.reject(err);
  } else {
    // Claim resource-creating results with the native owner ledger BEFORE the
    // caller's promise resolves, so a later abandonment can free the handle.
    if (entry.resourceCreating) {
      notifyNativeControl('bridgeResourceClaim', parsed.id);
    }
    entry.resolve(parsed.result);
  }
};

function credentialErrorEnvelope(code: CredentialErrorCode): string {
  return JSON.stringify({ version: 1, kind: 'credential_error', code });
}

function isCredentialEnvelopeCandidate(value: string): boolean {
  try {
    const parsed = JSON.parse(value) as { kind?: unknown };
    return parsed.kind === 'credential_error';
  } catch {
    return false;
  }
}

function uuidv4(): string {
  if (typeof crypto !== 'undefined' && typeof crypto.randomUUID === 'function') {
    return crypto.randomUUID();
  }
  return 'xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx'.replace(/[xy]/g, (c) => {
    const r = (Math.random() * 16) | 0;
    const v = c === 'x' ? r : (r & 0x3) | 0x8;
    return v.toString(16);
  });
}

function sendNative(envelope: string): void {
  const w = window as unknown as {
    webkit?: { messageHandlers?: { mahoBridge?: { postMessage: (msg: string) => void } } };
    MahoBridgeAndroid?: { rpc: (msg: string) => void };
    __mahoChromePageHandler?: (msg: string) => void;
  };

  if (w.webkit?.messageHandlers?.mahoBridge) {
    w.webkit.messageHandlers.mahoBridge.postMessage(envelope);
  } else if (w.MahoBridgeAndroid) {
    w.MahoBridgeAndroid.rpc(envelope);
  } else if (w.__mahoChromePageHandler) {
    w.__mahoChromePageHandler(envelope);
  } else {
    throw new Error('[maho-bridge] no native transport available');
  }
}

export function call<T>(
  method: string,
  params?: Record<string, unknown> | unknown[],
  options?: CallOptions,
): Promise<T> {
  return new Promise<T>((resolve, reject) => {
    if (_documentDisposed) {
      reject(rpcControlError('bridge_disposed'));
      return;
    }
    if (_pending.size >= MAX_PENDING_CALLS) {
      reject(rpcControlError('rpc_overloaded'));
      return;
    }
    const id = uuidv4();

    // Serialize BEFORE retaining a pending entry: a serialization failure
    // rejects immediately and retains nothing.
    let envelope: string;
    try {
      envelope = JSON.stringify({ jsonrpc: '2.0', id, method, params: params ?? [] });
    } catch (err) {
      reject(
        new Error(
          `rpc serialization failed for ${method}: ${err instanceof Error ? err.message : String(err)}`,
        ),
      );
      return;
    }

    const entry: PendingCall = {
      resolve: (v) => resolve(v as T),
      reject,
      timer: null,
      signal: null,
      onAbort: null,
      resourceCreating: RESOURCE_CREATING_METHODS.has(method),
      settled: false,
    };
    _pending.set(id, entry);

    const timeoutMs = options?.timeoutMs ?? defaultTimeoutFor(method);
    entry.timer = setTimeout(() => {
      // Timeout abandons the caller's wait. For resource-creating requests it
      // also notifies the native request ledger so a late completion frees the
      // created handle immediately. The timeout NEVER claims native work was
      // cancelled.
      if (entry.resourceCreating) {
        notifyNativeControl('bridgeRequestAbandon', id);
      }
      settleCall(id, rpcControlError('rpc_timeout'));
    }, timeoutMs);

    if (options?.signal !== undefined) {
      if (options.signal.aborted) {
        // Aborted before the request was ever sent: nothing to abandon.
        settleCall(id, rpcControlError('rpc_aborted'));
        return;
      }
      entry.signal = options.signal;
      entry.onAbort = () => {
        if (entry.resourceCreating) {
          notifyNativeControl('bridgeRequestAbandon', id);
        }
        settleCall(id, rpcControlError('rpc_aborted'));
      };
      options.signal.addEventListener('abort', entry.onAbort, { once: true });
    }

    try {
      sendNative(envelope);
    } catch (err) {
      const msg = err instanceof Error ? err.message : String(err);
      settleCall(id, new Error(`${msg} (method: ${method})`));
    }
  });
}

// Keep callBridge for backwards compatibility if needed, using positional parameters.
export function callBridge<T>(method: string, params: unknown[] = []): Promise<T> {
  return call<T>(method, params);
}

// U15 document disposal: settles every pending call exactly once with a typed
// bridge_disposed error and rejects further calls until state reset.
export function disposeDocumentBridge(): void {
  _documentDisposed = true;
  for (const id of Array.from(_pending.keys())) {
    settleCall(id, rpcControlError('bridge_disposed'));
  }
}

// Native host invalidation and page lifecycle share the same disposal path.
if (typeof window !== 'undefined') {
  window.addEventListener('pagehide', disposeDocumentBridge);
}

// Internal test observation seams
export function _getPendingCountForTest(): number {
  return _pending.size;
}

export function _getPendingIdsForTest(): string[] {
  return Array.from(_pending.keys());
}

// Internal reset for tests
export function _resetBridgeStateForTest(): void {
  _documentDisposed = false;
  _pending.clear();
}
