import { detectPlatform } from './platform-detect';
import type { BridgeError } from './types';

export { detectPlatform };

interface JsonRpcRequest {
  jsonrpc: '2.0';
  id: string;
  method: string;
  params: Record<string, unknown> | unknown[];
}

interface JsonRpcResponse {
  jsonrpc: '2.0';
  id: string;
  result?: unknown;
  error?: BridgeError;
}

interface PendingCall {
  resolve: (value: unknown) => void;
  reject: (reason: Error) => void;
}

const _pending = new Map<string, PendingCall>();

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

  const callObj = _pending.get(parsed.id);
  if (!callObj) return;
  _pending.delete(parsed.id);

  if (parsed.error) {
    const err = new Error(`[maho-bridge] ${parsed.error.kind}`);
    (err as unknown as { bridgeError: BridgeError }).bridgeError = parsed.error;
    callObj.reject(err);
  } else {
    callObj.resolve(parsed.result);
  }
};

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

export function call<T>(method: string, params?: Record<string, unknown> | unknown[]): Promise<T> {
  return new Promise<T>((resolve, reject) => {
    const id = uuidv4();
    _pending.set(id, {
      resolve: (v) => resolve(v as T),
      reject,
    });

    const request: JsonRpcRequest = {
      jsonrpc: '2.0',
      id,
      method,
      params: params ?? [],
    };
    const json = JSON.stringify(request);

    try {
      sendNative(json);
    } catch (err) {
      _pending.delete(id);
      reject(err);
    }
  });
}

// Keep callBridge for backwards compatibility if needed, using positional parameters.
export function callBridge<T>(method: string, params: unknown[] = []): Promise<T> {
  return call<T>(method, params);
}

// Internal reset for tests
export function _resetBridgeStateForTest(): void {
  _pending.clear();
}
