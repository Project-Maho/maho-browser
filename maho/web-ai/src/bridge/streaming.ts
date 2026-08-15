import type { PinchSummaryEvent } from './types';
import { StreamCadenceBuffer } from '../store/chatStore';

export interface ToolCallDelta {
  index?: number;
  id?: string;
  toolCallId?: string;
  name?: string;
  tool?: string;
  args?: unknown;
  arguments?: unknown;
  argumentsDelta?: string;
}

export interface ImageDelta {
  index?: number;
  mime?: string;
  b64Chunk?: string;
  url?: string;
}

export interface StreamSubscriber {
  onToken?: (token: string, droppedCount?: number) => void;
  onThinking?: (thinking: string) => void;
  onComplete?: (finalMessage: string) => void;
  onError?: (error: string) => void;
  onToolDelta?: (delta: ToolCallDelta) => void;
  onImageDelta?: (delta: ImageDelta) => void;
  onPinchState?: (event: PinchSummaryEvent) => void;
}

const subscribers = new Map<string, StreamSubscriber>();

function coerceToolDelta(delta: ToolCallDelta | string): ToolCallDelta {
  if (typeof delta !== 'string') {
    return delta;
  }

  try {
    return JSON.parse(delta) as ToolCallDelta;
  } catch {
    return { argumentsDelta: delta };
  }
}

function coerceImageDelta(delta: ImageDelta | string): ImageDelta {
  if (typeof delta !== 'string') {
    return delta;
  }

  try {
    return JSON.parse(delta) as ImageDelta;
  } catch {
    return { url: delta };
  }
}

export function subscribeStream(sessionId: string, sub: StreamSubscriber): () => void {
  subscribers.set(sessionId, sub);
  return () => {
    const active = subscribers.get(sessionId);
    if (active === sub) {
      subscribers.delete(sessionId);
    }
  };
}

export function subscribeStreamWithCadence(
  sessionId: string,
  sub: StreamSubscriber,
  options?: { frameWindowMs?: number },
): () => void {
  const cadence = new StreamCadenceBuffer({
    frameWindowMs: options?.frameWindowMs ?? 16,
    onTokenFlush: (token) => sub.onToken?.(token),
    onThinkingFlush: (thinking) => sub.onThinking?.(thinking),
  });

  return subscribeStream(sessionId, {
    ...sub,
    onToken: (token) => {
      cadence.pushToken(token);
    },
    onThinking: (thinking) => {
      cadence.pushThinking(thinking);
    },
    onComplete: (finalMessage) => {
      cadence.flush();
      sub.onComplete?.(finalMessage);
    },
    onError: (error) => {
      cadence.flush();
      sub.onError?.(error);
    },
    onToolDelta: (delta) => {
      cadence.flush();
      sub.onToolDelta?.(delta);
    },
    onImageDelta: (delta) => {
      cadence.flush();
      sub.onImageDelta?.(delta);
    },
    onPinchState: (event) => {
      cadence.flush();
      sub.onPinchState?.(event);
    },
  });
}

if (typeof window !== 'undefined') {
  const globalWindow = window as Window & {
    __mahoStreamBatch?: (sessionId: string, tokens: string[], droppedCount?: number) => void;
    __mahoStreamComplete?: (sessionId: string, finalMessage: string) => void;
    __mahoStreamError?: (sessionId: string, error: string) => void;
    __mahoStreamToolDelta?: (sessionId: string, delta: ToolCallDelta | string) => void;
    __mahoStreamThinking?: (sessionId: string, thinking: string) => void;
    __mahoStreamImageDelta?: (sessionId: string, delta: ImageDelta | string) => void;
    __mahoPinchSummaryEvent?: (tabId: string, event: unknown) => void;
  };

  globalWindow.__mahoStreamBatch = (sessionId, tokens, droppedCount) => {
    const subscriber = subscribers.get(sessionId);
    if (!subscriber || tokens.length === 0) {
      return;
    }

    subscriber.onToken?.(tokens.join(''), droppedCount);
  };

  globalWindow.__mahoStreamComplete = (sessionId, finalMessage) => {
    subscribers.get(sessionId)?.onComplete?.(finalMessage);
  };

  globalWindow.__mahoStreamError = (sessionId, error) => {
    subscribers.get(sessionId)?.onError?.(error);
  };

  globalWindow.__mahoStreamToolDelta = (sessionId, delta) => {
    subscribers.get(sessionId)?.onToolDelta?.(coerceToolDelta(delta));
  };

  globalWindow.__mahoStreamThinking = (sessionId, thinking) => {
    subscribers.get(sessionId)?.onThinking?.(thinking);
  };

  globalWindow.__mahoStreamImageDelta = (sessionId, delta) => {
    subscribers.get(sessionId)?.onImageDelta?.(coerceImageDelta(delta));
  };

  globalWindow.__mahoPinchSummaryEvent = (tabId, event) => {
    const pinchEvent = normalizePinchSummaryEvent(tabId, event);
    if (!pinchEvent) {
      return;
    }

    subscribers.get(tabId)?.onPinchState?.(pinchEvent);
  };
}

function normalizePinchSummaryEvent(tabId: string, event: unknown): PinchSummaryEvent | null {
  if (!isRecord(event) || typeof event.kind !== 'string') {
    return null;
  }

  const sentences = Array.isArray(event.sentences)
    ? event.sentences.filter((sentence): sentence is string => typeof sentence === 'string')
    : [];

  switch (event.kind) {
    case 'extractive_default':
    case 'llm_pending':
    case 'llm_streaming':
    case 'llm_success':
      return {
        kind: event.kind,
        tabId,
        sentences,
      };
    case 'llm_failed':
      return {
        kind: 'llm_failed',
        tabId,
        sentences,
        reason:
          typeof event.reason === 'string' && event.reason.trim().length > 0
            ? event.reason
            : 'AI enhancement failed.',
      };
    default:
      return null;
  }
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === 'object' && value !== null;
}
