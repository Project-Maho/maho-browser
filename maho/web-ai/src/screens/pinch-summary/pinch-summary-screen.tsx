import { useCallback, useEffect, useMemo, useReducer } from 'preact/hooks';
import type { CostEstimate, PinchSummaryEvent } from '../../bridge';
import { subscribeStream } from '../../bridge/streaming';
import { useBridge } from '../../hooks/use-bridge';
import { useHashRoute } from '../../hooks/use-hash-route';

type Stage = 'extractive' | 'cost_confirm' | 'llm_pending' | 'llm_streaming' | 'llm_success';

interface State {
  stage: Stage;
  extractiveSentences: string[];
  llmSentences: string[];
  costEstimate: CostEstimate | null;
  errorMessage: string | null;
}

type Action =
  | { type: 'RESET' }
  | { type: 'ESTIMATE_SUCCESS'; costEstimate: CostEstimate }
  | { type: 'ESTIMATE_ERROR'; message: string }
  | { type: 'DECLINE_AI' }
  | { type: 'ACCEPT_AI' }
  | { type: 'PINCH_EVENT'; event: PinchSummaryEvent };

interface LegacyCostEstimate {
  inputTokens?: number;
  outputTokens?: number;
  estimatedCostUsd?: number;
  model?: string;
  estimatedInputTokens?: number;
  estimatedOutputTokens?: number;
  tokenCount?: number;
  estimatedCost?: number;
}

interface PinchSummaryWindow extends Window {
  __mahoCurrentPageText?: string;
}

function initialState(): State {
  return {
    stage: 'extractive',
    extractiveSentences: [],
    llmSentences: [],
    costEstimate: null,
    errorMessage: null,
  };
}

/*
 * Lifecycle: native lands on an offline extractive summary first, the user can
 * optionally request a cost estimate, confirmation moves the screen into an
 * LLM-pending state, streaming progressively replaces the bullets, and any LLM
 * failure snaps back to the cached extractive bullets so the offline summary
 * never disappears.
 */
function reducer(state: State, action: Action): State {
  switch (action.type) {
    case 'RESET':
      return initialState();
    case 'ESTIMATE_SUCCESS':
      return {
        ...state,
        stage: 'cost_confirm',
        costEstimate: action.costEstimate,
        errorMessage: null,
      };
    case 'ESTIMATE_ERROR':
      return {
        ...state,
        stage: 'extractive',
        errorMessage: action.message,
      };
    case 'DECLINE_AI':
      return {
        ...state,
        stage: 'extractive',
      };
    case 'ACCEPT_AI':
      return {
        ...state,
        stage: 'llm_pending',
        errorMessage: null,
      };
    case 'PINCH_EVENT':
      return applyPinchEvent(state, action.event);
    default:
      return state;
  }
}

function applyPinchEvent(state: State, event: PinchSummaryEvent): State {
  switch (event.kind) {
    case 'extractive_default':
      return {
        ...state,
        stage: 'extractive',
        extractiveSentences: event.sentences,
        llmSentences: [],
        errorMessage: null,
      };
    case 'llm_pending':
      return {
        ...state,
        stage: 'llm_pending',
        errorMessage: null,
      };
    case 'llm_streaming':
      return {
        ...state,
        stage: 'llm_streaming',
        llmSentences: event.sentences.length > 0 ? event.sentences : state.llmSentences,
        errorMessage: null,
      };
    case 'llm_success':
      return {
        ...state,
        stage: 'llm_success',
        llmSentences: event.sentences.length > 0 ? event.sentences : state.llmSentences,
        errorMessage: null,
      };
    case 'llm_failed':
      return {
        ...state,
        stage: 'extractive',
        extractiveSentences:
          state.extractiveSentences.length > 0 ? state.extractiveSentences : event.sentences,
        llmSentences: [],
        errorMessage: event.reason ?? null,
      };
    default:
      return state;
  }
}

export function PinchSummaryScreen() {
  const bridge = useBridge();
  const route = useHashRoute();
  const [state, dispatch] = useReducer(reducer, undefined, initialState);

  const tabId = route.params.tabId?.trim() || undefined;
  const requestedModel = route.params.model?.trim() || 'default';
  const initialSentences = useMemo(() => parseInitialSentences(route.params), [route.params]);
  const pageContent = useMemo(() => resolvePageContent(route.params), [route.params]);

  useEffect(() => {
    dispatch({ type: 'RESET' });
  }, [tabId]);

  useEffect(() => {
    if (!tabId) {
      return;
    }

    return subscribeStream(tabId, {
      onPinchState: (event) => {
        dispatch({ type: 'PINCH_EVENT', event });
      },
    });
  }, [tabId]);

  const extractiveSentences =
    state.extractiveSentences.length > 0 ? state.extractiveSentences : initialSentences;

  const renderedSentences = useMemo(() => {
    if (state.stage === 'llm_streaming' || state.stage === 'llm_success') {
      return state.llmSentences.length > 0 ? state.llmSentences : extractiveSentences;
    }

    return extractiveSentences;
  }, [extractiveSentences, state.llmSentences, state.stage]);

  const showEnhanceCallToAction = state.stage === 'extractive' && extractiveSentences.length > 0;
  const showPendingNotice = state.stage === 'llm_pending' || state.stage === 'llm_streaming';
  const isStreaming = state.stage === 'llm_streaming';

  const requestCostEstimate = useCallback(async () => {
    try {
      const estimate = await bridge.pinchEstimateCost(pageContent, requestedModel);
      dispatch({
        type: 'ESTIMATE_SUCCESS',
        costEstimate: normalizeCostEstimate(estimate, requestedModel),
      });
    } catch (error) {
      dispatch({
        type: 'ESTIMATE_ERROR',
        message: toErrorMessage(error, 'Unable to estimate AI cost right now.'),
      });
    }
  }, [bridge, pageContent, requestedModel]);

  const declineEnhancement = useCallback(() => {
    dispatch({ type: 'DECLINE_AI' });
  }, []);

  const acceptEnhancement = useCallback(() => {
    dispatch({ type: 'ACCEPT_AI' });
  }, []);

  if (!tabId) {
    return (
      <div class="pinch-summary-shell">
        <div class="pinch-summary-screen ps-screen-state" role="alert">
          <h1 class="ps-title">Missing tabId</h1>
          <p class="ps-copy">No tabId was provided for this pinch summary route.</p>
        </div>
      </div>
    );
  }

  return (
    <div class="pinch-summary-shell">
      <section class="pinch-summary-screen" data-testid="pinch-summary-screen">
        <header class="ps-header">
          <div class="ps-header-copy">
            <p class="ps-eyebrow">Pinch summary</p>
            <h1 class="ps-title">Page summary</h1>
            <p class="ps-copy">
              Offline highlights arrive first. AI enhancement is optional, uses your own key,
              and only runs after cost confirmation.
            </p>
          </div>

          <div class="ps-badge-stack" aria-live="polite">
            <span class="ps-badge">Offline first</span>
            {state.stage === 'llm_pending' && <span class="ps-badge">Preparing AI…</span>}
            {state.stage === 'llm_streaming' && <span class="ps-badge">AI streaming…</span>}
            {state.stage === 'llm_success' && (
              <span class="ps-badge ps-badge-success">AI enhanced</span>
            )}
          </div>
        </header>

        {showPendingNotice && (
          <div class="ps-stream-status" role="status">
            <span class="ps-stream-indicator" aria-hidden="true" />
            <span>
              {isStreaming
                ? 'AI bullets are streaming in and will replace the offline summary when complete.'
                : 'Waiting for native AI enhancement to begin…'}
            </span>
          </div>
        )}

        {renderedSentences.length > 0 ? (
          <ul class="ps-bullets" aria-label="Summary bullets">
            {renderedSentences.map((sentence, index) => (
              <li
                key={`${index}:${sentence}`}
                class={isStreaming ? 'ps-bullet ps-bullet-streaming' : 'ps-bullet'}
              >
                <span class="ps-bullet-copy">{sentence}</span>
              </li>
            ))}
          </ul>
        ) : (
          <div class="ps-empty-state" role="status">
            Preparing the offline page summary…
          </div>
        )}

        {showEnhanceCallToAction && (
          <div class="ps-enhance-card">
            <div>
              <h2 class="ps-section-title">Need a deeper pass?</h2>
              <p class="ps-section-copy">
                Enhance these bullets with your configured LLM. This requires network access and
                may incur provider cost.
              </p>
            </div>

            <div class="ps-enhance-actions">
              <button
                type="button"
                class="ps-button ps-button-primary"
                onClick={() => void requestCostEstimate()}
              >
                Enhance with AI
              </button>
            </div>
          </div>
        )}

        {state.errorMessage && (
          <div class="ps-error-banner" role="alert">
            {state.errorMessage}
          </div>
        )}
      </section>

      {state.stage === 'cost_confirm' && state.costEstimate && (
        <div class="ps-overlay" aria-hidden="false">
          <div
            class="ps-cost-confirm"
            role="dialog"
            aria-modal="true"
            aria-labelledby="pinch-summary-cost-title"
          >
            <p class="ps-eyebrow">AI enhancement</p>
            <h2 id="pinch-summary-cost-title" class="ps-title ps-title-dialog">
              Enhance with AI?
            </h2>
            <p class="ps-copy ps-copy-dialog">
              Estimated cost: <strong>${state.costEstimate.estimatedCostUsd.toFixed(4)}</strong>
            </p>
            <p class="ps-cost-detail">
              {state.costEstimate.inputTokens.toLocaleString()} input +{' '}
              {state.costEstimate.outputTokens.toLocaleString()} output tokens ·{' '}
              {state.costEstimate.model}
            </p>
            <div class="ps-cost-actions">
              <button
                type="button"
                class="ps-button ps-button-secondary"
                onClick={declineEnhancement}
              >
                Use offline summary
              </button>
              <button
                type="button"
                class="ps-button ps-button-primary"
                onClick={acceptEnhancement}
                aria-label={`Use AI enhancement for about $${state.costEstimate.estimatedCostUsd.toFixed(3)}`}
              >
                Use AI (~${state.costEstimate.estimatedCostUsd.toFixed(3)})
              </button>
            </div>
          </div>
        </div>
      )}
    </div>
  );
}

function parseInitialSentences(params: Record<string, string>): string[] {
  const encodedSentences = params.sentences?.trim() || params.extractive?.trim();
  if (!encodedSentences) {
    return [];
  }

  try {
    const parsed = JSON.parse(encodedSentences) as unknown;
    if (Array.isArray(parsed)) {
      return parsed.filter((sentence): sentence is string => typeof sentence === 'string');
    }
  } catch {
  }

  return encodedSentences
    .split(/\n+/)
    .map((sentence) => sentence.trim())
    .filter((sentence) => sentence.length > 0);
}

function resolvePageContent(params: Record<string, string>): string {
  const routeValue = params.pageContent?.trim() || params.pageText?.trim();
  if (routeValue) {
    return routeValue;
  }

  const injectedValue = (window as PinchSummaryWindow).__mahoCurrentPageText;
  return typeof injectedValue === 'string' ? injectedValue : '';
}

function normalizeCostEstimate(
  estimate: CostEstimate | LegacyCostEstimate,
  fallbackModel: string,
): CostEstimate {
  const legacyEstimate = estimate as LegacyCostEstimate;
  const inputTokens = selectNumber(
    estimate.inputTokens,
    legacyEstimate.estimatedInputTokens,
    legacyEstimate.tokenCount,
  );
  const outputTokens = selectNumber(
    estimate.outputTokens,
    legacyEstimate.estimatedOutputTokens,
    inputTokens > 0 ? Math.min(Math.round(inputTokens / 4), 1024) : 0,
  );

  return {
    inputTokens,
    outputTokens,
    estimatedCostUsd: selectNumber(estimate.estimatedCostUsd, legacyEstimate.estimatedCost),
    model:
      typeof estimate.model === 'string' && estimate.model.trim().length > 0
        ? estimate.model
        : fallbackModel,
  };
}

function selectNumber(...values: Array<number | undefined>): number {
  for (const value of values) {
    if (typeof value === 'number' && Number.isFinite(value)) {
      return value;
    }
  }

  return 0;
}

function toErrorMessage(error: unknown, fallback: string): string {
  if (error instanceof Error && error.message.trim().length > 0) {
    return error.message;
  }

  if (typeof error === 'string' && error.trim().length > 0) {
    return error;
  }

  return fallback;
}
