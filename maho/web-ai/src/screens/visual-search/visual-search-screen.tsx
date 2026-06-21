/**
 * VisualSearchScreen — displays captured image + AI analysis streamed from LLM.
 *
 * Image injection contract:
 *   1. Native sets `window.__mahoVisualSearchImage` before navigating to this hash.
 *   2. Or native calls `window.__mahoVisualSearchReceive(payload)` after load.
 *
 * Camera capture is NATIVE only — this screen only shows results.
 */
import { useCallback, useEffect, useReducer, useRef } from 'preact/hooks';
import { subscribeStream } from '../../bridge/streaming';
import { useBridge } from '../../hooks/use-bridge';
import type { SessionHandle } from '../../bridge/types';

// ─── Window bridge typing ─────────────────────────────────────────────────────

interface VisualSearchImagePayload {
  dataUrl: string;
  mime: string;
  width?: number;
  height?: number;
}

interface VisualSearchWindow {
  __mahoVisualSearchImage?: VisualSearchImagePayload;
  __mahoVisualSearchReceive?: (payload: VisualSearchImagePayload) => void;
  MahoBridgeAndroid?: { requestCameraRecapture?: () => void };
  webkit?: { messageHandlers?: { mahoBridge?: { postMessage: (msg: string) => void } } };
}

function getVisualSearchWindow(): VisualSearchWindow {
  return window as unknown as VisualSearchWindow;
}

// ─── State machine ────────────────────────────────────────────────────────────

type Stage = 'loading_image' | 'analyzing' | 'complete' | 'error';

interface State {
  stage: Stage;
  imagePayload: VisualSearchImagePayload | null;
  analysisText: string;
  errorMessage: string | null;
  sessionHandle: SessionHandle | null;
}

type Action =
  | { type: 'IMAGE_RECEIVED'; payload: VisualSearchImagePayload }
  | { type: 'ANALYSIS_STARTED'; handle: SessionHandle }
  | { type: 'TOKEN'; delta: string }
  | { type: 'COMPLETE'; finalMessage: string }
  | { type: 'ERROR'; message: string }
  | { type: 'RETRY' };

function initialState(): State {
  return {
    stage: 'loading_image',
    imagePayload: null,
    analysisText: '',
    errorMessage: null,
    sessionHandle: null,
  };
}

function reducer(state: State, action: Action): State {
  switch (action.type) {
    case 'IMAGE_RECEIVED':
      return { ...state, stage: 'analyzing', imagePayload: action.payload, analysisText: '', errorMessage: null };
    case 'ANALYSIS_STARTED':
      return { ...state, sessionHandle: action.handle };
    case 'TOKEN':
      return { ...state, analysisText: state.analysisText + action.delta };
    case 'COMPLETE':
      return { ...state, stage: 'complete', analysisText: action.finalMessage };
    case 'ERROR':
      return { ...state, stage: 'error', errorMessage: action.message };
    case 'RETRY':
      return initialState();
    default:
      return state;
  }
}

// ─── Retake helper ────────────────────────────────────────────────────────────

function retake(): void {
  const w = getVisualSearchWindow();
  if (w.MahoBridgeAndroid?.requestCameraRecapture) {
    w.MahoBridgeAndroid.requestCameraRecapture();
  } else if (w.webkit?.messageHandlers?.mahoBridge) {
    w.webkit.messageHandlers.mahoBridge.postMessage(
      JSON.stringify({ method: 'requestCameraRecapture' })
    );
  }
}

// ─── Props ────────────────────────────────────────────────────────────────────

interface VisualSearchScreenProps {
  onBack: () => void;
  onNavigateToChat?: (handle: SessionHandle) => void;
  onNavigateToConversation?: (conversationId: string) => void;
}

// ─── Component ────────────────────────────────────────────────────────────────

export function VisualSearchScreen({
  onBack,
  onNavigateToChat,
  onNavigateToConversation,
}: VisualSearchScreenProps) {
  const bridge = useBridge();
  const [state, dispatch] = useReducer(reducer, undefined, initialState);
  const analysisStartedRef = useRef(false);

  // ── Register receive handler + check for pre-injected image ──
  useEffect(() => {
    const w = getVisualSearchWindow();

    const handlePayload = (payload: VisualSearchImagePayload) => {
      dispatch({ type: 'IMAGE_RECEIVED', payload });
    };

    w.__mahoVisualSearchReceive = handlePayload;

    if (w.__mahoVisualSearchImage) {
      handlePayload(w.__mahoVisualSearchImage);
    }

    return () => {
      if (w.__mahoVisualSearchReceive === handlePayload) {
        w.__mahoVisualSearchReceive = undefined;
      }
    };
  }, []);

  // ── Start analysis when image arrives ──
  useEffect(() => {
    if (state.stage !== 'analyzing' || !state.imagePayload || analysisStartedRef.current) {
      return;
    }
    analysisStartedRef.current = true;

    const payload = state.imagePayload;
    const base64 = payload.dataUrl.includes(',')
      ? payload.dataUrl.split(',')[1] ?? payload.dataUrl
      : payload.dataUrl;

    let handle: SessionHandle | null = null;
    let unsub: (() => void) | null = null;

    const run = async () => {
      try {
        handle = await bridge.chatSessionStart({ model: '' });
        dispatch({ type: 'ANALYSIS_STARTED', handle });

        unsub = subscribeStream(handle, {
          onToken: (token) => dispatch({ type: 'TOKEN', delta: token }),
          onComplete: (finalMessage) => dispatch({ type: 'COMPLETE', finalMessage }),
          onError: (error) => dispatch({ type: 'ERROR', message: error }),
        });

        await bridge.chatSendMessage(handle, { kind: 'image', mime: payload.mime, base64 });
        await bridge.chatSendMessage(handle, { kind: 'text', text: 'Analyze this image' });
      } catch (err) {
        const message = err instanceof Error ? err.message : 'Analysis failed';
        dispatch({ type: 'ERROR', message });
      }
    };

    void run();

    return () => {
      unsub?.();
      if (handle) {
        void bridge.chatSessionFree(handle);
      }
    };
  }, [state.stage, state.imagePayload, bridge]);

  // Reset ref when retrying
  useEffect(() => {
    if (state.stage === 'loading_image') {
      analysisStartedRef.current = false;
    }
  }, [state.stage]);

  const handleRetake = useCallback(() => {
    dispatch({ type: 'RETRY' });
    retake();
  }, []);

  const handleFollowUp = useCallback(() => {
    if (state.sessionHandle && onNavigateToChat) {
      onNavigateToChat(state.sessionHandle);
    }
  }, [state.sessionHandle, onNavigateToChat]);

  const handleSave = useCallback(() => {
    if (!onNavigateToConversation) return;
    const meta = {
      id: '',
      title: 'Visual Search',
      createdAt: new Date().toISOString(),
      updatedAt: new Date().toISOString(),
    };
    void bridge.conversationCreate(meta).then((id) => {
      onNavigateToConversation(id);
    }).catch((err: unknown) => {
      const message = err instanceof Error ? err.message : 'Failed to save';
      dispatch({ type: 'ERROR', message });
    });
  }, [bridge, onNavigateToConversation]);

  const handleRetry = useCallback(() => {
    analysisStartedRef.current = false;
    dispatch({ type: 'RETRY' });
    const w = getVisualSearchWindow();
    if (w.__mahoVisualSearchImage) {
      dispatch({ type: 'IMAGE_RECEIVED', payload: w.__mahoVisualSearchImage });
    }
  }, []);

  return (
    <div class="vs-shell">
      <header class="vs-header">
        <button
          type="button"
          class="vs-header-btn"
          onClick={onBack}
          aria-label="Back"
        >
          <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
            <polyline points="15 18 9 12 15 6" />
          </svg>
        </button>
        <h1 class="vs-header-title">Visual Search</h1>
        <button
          type="button"
          class="vs-header-btn vs-retake-btn"
          onClick={handleRetake}
          aria-label="Retake photo"
        >
          <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
            <path d="M23 19a2 2 0 0 1-2 2H3a2 2 0 0 1-2-2V8a2 2 0 0 1 2-2h4l2-3h6l2 3h4a2 2 0 0 1 2 2z" />
            <circle cx="12" cy="13" r="4" />
          </svg>
        </button>
      </header>

      {state.imagePayload && (
        <div class="vs-image-container">
          <img
            src={state.imagePayload.dataUrl}
            alt="Captured frame for visual search"
            class="vs-image"
          />
        </div>
      )}

      <main class="vs-content">
        {state.stage === 'loading_image' && (
          <div class="vs-state-card vs-loading" role="status" aria-live="polite">
            <div class="vs-spinner" aria-hidden="true" />
            <p class="vs-state-copy">Waiting for camera capture…</p>
          </div>
        )}

        {(state.stage === 'analyzing' || state.stage === 'complete') && (
          <section class="vs-analysis-section" aria-label="AI Analysis">
            <p class="vs-section-eyebrow">AI Analysis</p>
            <div class="vs-analysis-body">
              {state.analysisText.length === 0 && state.stage === 'analyzing' ? (
                <div class="vs-state-card vs-loading" role="status" aria-live="polite">
                  <div class="vs-spinner" aria-hidden="true" />
                  <p class="vs-state-copy">Analyzing…</p>
                </div>
              ) : (
                <p class="vs-analysis-text" aria-live="polite">
                  {state.analysisText}
                  {state.stage === 'analyzing' && (
                    <span class="vs-cursor" aria-hidden="true" />
                  )}
                </p>
              )}
            </div>
          </section>
        )}

        {state.stage === 'complete' && (
          <div class="vs-actions">
            <button
              type="button"
              class="vs-action-btn vs-action-btn--primary"
              onClick={handleFollowUp}
              aria-label="Ask a follow-up question"
            >
              Ask follow-up
            </button>
            <button
              type="button"
              class="vs-action-btn"
              onClick={handleSave}
              aria-label="Save to conversation"
            >
              Save to conversation
            </button>
            <button
              type="button"
              class="vs-action-btn vs-action-btn--ghost"
              onClick={handleRetake}
              aria-label="Retake photo"
            >
              Retake
            </button>
          </div>
        )}

        {state.stage === 'error' && (
          <div class="vs-state-card vs-error-card" role="alert">
            <p class="vs-error-title">Analysis failed</p>
            <p class="vs-state-copy vs-error-copy">{state.errorMessage}</p>
            <button
              type="button"
              class="vs-action-btn vs-action-btn--primary"
              onClick={handleRetry}
              aria-label="Retry analysis"
            >
              Retry
            </button>
          </div>
        )}
      </main>
    </div>
  );
}
