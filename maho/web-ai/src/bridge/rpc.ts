/**
 * JSON-RPC 2.0 dispatcher over postMessage.
 *
 * Supports three platform transports:
 *   iOS     → window.webkit.messageHandlers.mahoBridge.postMessage(json)
 *   Android → window.MahoBridgeAndroid.rpc(json)
 *   Desktop → window.__mahoChromePageHandler(json)   (existing Mojo bridge)
 *
 * Native side resolves calls by invoking window.__mahoBridgeResponse(responseJson).
 */

interface JsonRpcRequest {
  jsonrpc: '2.0';
  id: number;
  method: string;
  params: unknown[];
}

interface JsonRpcResponse {
  id: number;
  result?: unknown;
  error?: { message: string; code?: number };
}

interface PendingCall {
  resolve: (value: unknown) => void;
  reject: (reason: Error) => void;
}

let _callId = 0;
const _pending = new Map<number, PendingCall>();

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

  const call = _pending.get(parsed.id);
  if (!call) return;
  _pending.delete(parsed.id);

  if (parsed.error) {
    call.reject(new Error(parsed.error.message));
  } else {
    call.resolve(parsed.result);
  }
};

/**
 * Send a typed RPC call to the native side.
 * @param method  Corresponds to a MahoBridge interface method name.
 * @param params  Positional parameters (must be JSON-serialisable).
 * @returns       A promise that resolves with the JSON-decoded result.
 */
export function callBridge<T>(method: string, params: unknown[] = []): Promise<T> {
  return new Promise<T>((resolve, reject) => {
    const id = ++_callId;
    _pending.set(id, {
      resolve: (v) => resolve(v as T),
      reject,
    });

    const request: JsonRpcRequest = { jsonrpc: '2.0', id, method, params };
    const json = JSON.stringify(request);

    const w = window as unknown as {
      webkit?: { messageHandlers?: { mahoBridge?: { postMessage: (msg: string) => void } } };
      MahoBridgeAndroid?: { rpc: (msg: string) => void };
      __mahoChromePageHandler?: (msg: string) => void;
    };

    if (w.webkit?.messageHandlers?.mahoBridge) {
      // iOS — WKScriptMessageHandler
      w.webkit.messageHandlers.mahoBridge.postMessage(json);
    } else if (w.MahoBridgeAndroid) {
      // Android — @JavascriptInterface
      w.MahoBridgeAndroid.rpc(json);
    } else if (w.__mahoChromePageHandler) {
      // Desktop — Mojo page handler
      w.__mahoChromePageHandler(json);
    } else {
      // No bridge available (dev/test environment) — reject immediately.
      _pending.delete(id);
      reject(new Error(`[maho-bridge] no transport available for method "${method}"`));
    }
  });
}
