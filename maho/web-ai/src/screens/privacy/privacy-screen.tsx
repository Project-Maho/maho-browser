/**
 * Privacy Dashboard screen — Phase C implementation.
 *
 * Bridge contract adaptation notes:
 *   - No getPrivacyConfig()/setPrivacyConfig() on A2 bridge surface.
 *   - No bulk clear method; we iterate conversationList() + conversationDelete().
 *   - No cache-clear method; stubbed with TODO until bridge exposes one.
 *   - Toggles shown are UI-layer preferences stored in localStorage
 *     (sendHistory, allowTelemetry) because the bridge has no global settings
 *     get/set. spaceMemory toggle is also a local preference for future bridging.
 *     Each toggle auto-saves on change; no explicit Save button needed.
 *
 * Bridge methods used:
 *   conversationList()        → count and list for bulk delete
 *   conversationDelete(id)    → per-item delete in clear-all loop
 *
 * Deviations from spec skeleton:
 *   - loadPrivacyConfig / savePrivacyConfig / clearAiCache use localStorage
 *     since no bridge method exists. Documented with TODO comments.
 *   - No PrivacyConfig type imported from bridge (doesn't exist); defined locally.
 */

import { useCallback, useEffect, useReducer } from 'preact/hooks';
import { useBridge } from '../../hooks/use-bridge';

// ─── Local config type ───────────────────────────────────────────────────────

/**
 * Privacy preferences stored locally.
 * TODO: Move to bridge once a getPrivacyConfig/setPrivacyConfig method is added.
 */
export interface PrivacyConfig {
  /** Include past messages when building the prompt context window. */
  sendHistory: boolean;
  /** Allow anonymous usage telemetry to be sent to Maho. */
  allowTelemetry: boolean;
  /** Save personal context (space memory) across sessions. */
  spaceMemory: boolean;
}

const STORAGE_KEY = 'maho_privacy_config';

const DEFAULT_CONFIG: PrivacyConfig = {
  sendHistory: true,
  allowTelemetry: false,
  spaceMemory: true,
};

// ─── Local persistence helpers ───────────────────────────────────────────────

function readLocalConfig(): PrivacyConfig {
  // TODO: Replace with bridge.getPrivacyConfig() once the method is added.
  try {
    const raw = localStorage.getItem(STORAGE_KEY);
    if (!raw) return { ...DEFAULT_CONFIG };
    const parsed = JSON.parse(raw) as Partial<PrivacyConfig>;
    return {
      sendHistory: parsed.sendHistory ?? DEFAULT_CONFIG.sendHistory,
      allowTelemetry: parsed.allowTelemetry ?? DEFAULT_CONFIG.allowTelemetry,
      spaceMemory: parsed.spaceMemory ?? DEFAULT_CONFIG.spaceMemory,
    };
  } catch {
    return { ...DEFAULT_CONFIG };
  }
}

function writeLocalConfig(config: PrivacyConfig): void {
  // TODO: Replace with bridge.setPrivacyConfig(config) once the method is added.
  localStorage.setItem(STORAGE_KEY, JSON.stringify(config));
}

async function clearAiResponseCache(): Promise<void> {
  // TODO: Replace with bridge.clearAiCache() once the method is exposed.
  // For now, clear sessionStorage only (response cache not bridged yet).
  sessionStorage.clear();
}

// ─── State ───────────────────────────────────────────────────────────────────

interface State {
  loading: boolean;
  saving: boolean;
  config: PrivacyConfig | null;
  conversationCount: number | null;
  errorMessage: string | null;
  clearedAt: number | null;
}

type Action =
  | { type: 'LOAD_START' }
  | { type: 'LOAD_SUCCESS'; config: PrivacyConfig; conversationCount: number }
  | { type: 'LOAD_ERROR'; message: string }
  | { type: 'TOGGLE'; key: keyof PrivacyConfig; value: boolean }
  | { type: 'TOGGLE_REVERT'; config: PrivacyConfig }
  | { type: 'SAVE_START' }
  | { type: 'SAVE_DONE' }
  | { type: 'CLEAR_START' }
  | { type: 'CLEAR_CONVERSATIONS_DONE' }
  | { type: 'CLEAR_CACHE_DONE' }
  | { type: 'CLEAR_ERROR'; message: string }
  | { type: 'DISMISS_ERROR' };

function initialState(): State {
  return {
    loading: true,
    saving: false,
    config: null,
    conversationCount: null,
    errorMessage: null,
    clearedAt: null,
  };
}

function reducer(state: State, action: Action): State {
  switch (action.type) {
    case 'LOAD_START':
      return { ...state, loading: true, errorMessage: null };
    case 'LOAD_SUCCESS':
      return {
        ...state,
        loading: false,
        config: action.config,
        conversationCount: action.conversationCount,
        errorMessage: null,
      };
    case 'LOAD_ERROR':
      return { ...state, loading: false, config: null, errorMessage: action.message };

    case 'TOGGLE':
      if (!state.config) return state;
      return {
        ...state,
        config: { ...state.config, [action.key]: action.value },
        errorMessage: null,
        clearedAt: null,
      };
    case 'TOGGLE_REVERT':
      // Revert config on save failure — defensive UX: user sees original state,
      // not a lie that the toggle is now off when native never accepted the change.
      return { ...state, config: action.config, saving: false };

    case 'SAVE_START':
      return { ...state, saving: true };
    case 'SAVE_DONE':
      return { ...state, saving: false };

    case 'CLEAR_START':
      return { ...state, saving: true, errorMessage: null, clearedAt: null };
    case 'CLEAR_CONVERSATIONS_DONE':
      return { ...state, saving: false, conversationCount: 0, clearedAt: Date.now() };
    case 'CLEAR_CACHE_DONE':
      return { ...state, saving: false, clearedAt: Date.now() };
    case 'CLEAR_ERROR':
      return { ...state, saving: false, errorMessage: action.message };

    case 'DISMISS_ERROR':
      return { ...state, errorMessage: null };

    default:
      return state;
  }
}

// ─── Component ───────────────────────────────────────────────────────────────

interface PrivacyScreenProps {
  onBack?: () => void;
}

export function PrivacyScreen({ onBack }: PrivacyScreenProps) {
  const bridge = useBridge();
  const [state, dispatch] = useReducer(reducer, undefined, initialState);

  const handleBack = useCallback(() => {
    if (onBack) {
      onBack();
      return;
    }
    window.history.back();
  }, [onBack]);

  // ── Load on mount ──
  const load = useCallback(async () => {
    dispatch({ type: 'LOAD_START' });
    try {
      const config = readLocalConfig();
      const list = await bridge.conversationList();
      dispatch({ type: 'LOAD_SUCCESS', config, conversationCount: list.length });
    } catch (err) {
      dispatch({
        type: 'LOAD_ERROR',
        message: err instanceof Error ? err.message : 'Failed to load privacy settings.',
      });
    }
  }, [bridge]);

  useEffect(() => {
    void load();
  }, [load]);

  // ── Toggle + auto-save ──
  const setToggle = useCallback(
    async (key: keyof PrivacyConfig, value: boolean) => {
      if (!state.config) return;
      const previous = state.config;
      dispatch({ type: 'TOGGLE', key, value });
      dispatch({ type: 'SAVE_START' });
      try {
        const next = { ...previous, [key]: value };
        writeLocalConfig(next);
        dispatch({ type: 'SAVE_DONE' });
      } catch (err) {
        // Revert on failure so UI stays consistent with persisted state.
        dispatch({ type: 'TOGGLE_REVERT', config: previous });
        dispatch({
          type: 'CLEAR_ERROR',
          message: err instanceof Error ? err.message : 'Failed to save setting.',
        });
      }
    },
    [state.config],
  );

  // ── Clear all conversations ──
  const clearAllConversations = useCallback(async () => {
    if (!confirm('Delete all saved conversations? This cannot be undone.')) return;
    dispatch({ type: 'CLEAR_START' });
    try {
      const list = await bridge.conversationList();
      await Promise.all(list.map((c) => bridge.conversationDelete(c.id)));
      dispatch({ type: 'CLEAR_CONVERSATIONS_DONE' });
    } catch (err) {
      dispatch({
        type: 'CLEAR_ERROR',
        message: err instanceof Error ? err.message : 'Failed to clear conversations.',
      });
    }
  }, [bridge]);

  // ── Clear cached AI responses ──
  const clearCachedResponses = useCallback(async () => {
    if (!confirm('Clear cached AI responses?')) return;
    dispatch({ type: 'CLEAR_START' });
    try {
      await clearAiResponseCache();
      dispatch({ type: 'CLEAR_CACHE_DONE' });
    } catch (err) {
      dispatch({
        type: 'CLEAR_ERROR',
        message: err instanceof Error ? err.message : 'Failed to clear cache.',
      });
    }
  }, []);

  // ── Render: loading ──
  if (state.loading) {
    return (
      <div class="prv-shell">
        <div class="prv-screen">
          <header class="prv-header">
            <button type="button" class="prv-back-button" onClick={handleBack} aria-label="Back">
              <BackIcon />
            </button>
            <h1 class="prv-heading">Privacy</h1>
          </header>
          <div class="prv-state-card prv-loading" role="status" data-testid="privacy-loading">
            <SpinnerIcon />
            <span>Loading…</span>
          </div>
        </div>
      </div>
    );
  }

  // ── Render: load failure ──
  if (!state.config) {
    return (
      <div class="prv-shell">
        <div class="prv-screen">
          <header class="prv-header">
            <button type="button" class="prv-back-button" onClick={handleBack} aria-label="Back">
              <BackIcon />
            </button>
            <h1 class="prv-heading">Privacy</h1>
          </header>
          <div class="prv-state-card prv-error-card" role="alert" data-testid="privacy-error">
            <p class="prv-state-title">Could not load settings</p>
            <p class="prv-state-copy">{state.errorMessage ?? 'An unknown error occurred.'}</p>
            <button type="button" class="prv-button prv-button-secondary" onClick={() => void load()}>
              Retry
            </button>
          </div>
        </div>
      </div>
    );
  }

  // ── Render: main ──
  return (
    <div class="prv-shell" data-testid="privacy-screen">
      <div class="prv-screen">
        <header class="prv-header">
          <button type="button" class="prv-back-button" onClick={handleBack} aria-label="Back">
            <BackIcon />
          </button>
          <h1 class="prv-heading">Privacy</h1>
        </header>

        {/* ── AI Privacy toggles ── */}
        <section class="prv-section" aria-labelledby="prv-ai-privacy-heading">
          <p class="prv-section-eyebrow" id="prv-ai-privacy-heading">
            AI Privacy
          </p>
          <div class="prv-toggle-group">
            <ToggleRow
              id="prv-toggle-send-history"
              label="Send conversation history"
              description="Include past messages when sending new prompts."
              checked={state.config.sendHistory}
              disabled={state.saving}
              onChange={(v) => void setToggle('sendHistory', v)}
            />
            <ToggleRow
              id="prv-toggle-telemetry"
              label="Allow anonymous telemetry"
              description="Share usage data to help improve Maho."
              checked={state.config.allowTelemetry}
              disabled={state.saving}
              onChange={(v) => void setToggle('allowTelemetry', v)}
            />
            <ToggleRow
              id="prv-toggle-space-memory"
              label="Space memory"
              description="Save personal context across sessions."
              checked={state.config.spaceMemory}
              disabled={state.saving}
              onChange={(v) => void setToggle('spaceMemory', v)}
            />
          </div>
        </section>

        {/* ── Stored data ── */}
        <section class="prv-section" aria-labelledby="prv-data-heading">
          <p class="prv-section-eyebrow" id="prv-data-heading">
            Stored Data
          </p>
          <div class="prv-data-panel">
            <div class="prv-stat">
              <span class="prv-stat-value" data-testid="conversation-count">
                {state.conversationCount ?? '—'}
              </span>
              <span class="prv-stat-label">
                {state.conversationCount === 1 ? 'saved conversation' : 'saved conversations'}
              </span>
            </div>

            <div class="prv-actions">
              <button
                type="button"
                class="prv-button prv-button-destructive"
                onClick={() => void clearAllConversations()}
                disabled={state.saving}
                data-testid="clear-conversations-btn"
              >
                Clear all conversations
              </button>
              <button
                type="button"
                class="prv-button prv-button-secondary"
                onClick={() => void clearCachedResponses()}
                disabled={state.saving}
                data-testid="clear-cache-btn"
              >
                Clear cached AI responses
              </button>
            </div>
          </div>
        </section>

        {/* ── Status / error banners ── */}
        {state.errorMessage && (
          <div class="prv-error-banner" role="alert" data-testid="privacy-error-banner">
            <span>{state.errorMessage}</span>
            <button
              type="button"
              class="prv-dismiss"
              onClick={() => dispatch({ type: 'DISMISS_ERROR' })}
              aria-label="Dismiss error"
            >
              ×
            </button>
          </div>
        )}

        {state.clearedAt && !state.errorMessage && (
          <div class="prv-status-banner" role="status" data-testid="privacy-status-banner">
            Cleared at {new Date(state.clearedAt).toLocaleTimeString()}
          </div>
        )}
      </div>
    </div>
  );
}

// ─── Sub-components ──────────────────────────────────────────────────────────

interface ToggleRowProps {
  id: string;
  label: string;
  description: string;
  checked: boolean;
  disabled: boolean;
  onChange: (value: boolean) => void;
}

function ToggleRow({ id, label, description, checked, disabled, onChange }: ToggleRowProps) {
  const descId = `${id}-desc`;
  return (
    <div class="prv-toggle-row">
      <span class="prv-toggle-text">
        <label class="prv-toggle-label" for={id}>
          {label}
        </label>
        <span class="prv-toggle-desc" id={descId}>
          {description}
        </span>
      </span>
      <input
        id={id}
        type="checkbox"
        role="switch"
        aria-checked={checked}
        class="prv-switch"
        checked={checked}
        disabled={disabled}
        aria-describedby={descId}
        onChange={(e) => onChange((e.target as HTMLInputElement).checked)}
      />
    </div>
  );
}

function BackIcon() {
  return (
    <svg
      width={20}
      height={20}
      viewBox="0 0 24 24"
      fill="none"
      stroke="currentColor"
      stroke-width="2"
      stroke-linecap="round"
      stroke-linejoin="round"
      aria-hidden="true"
      focusable="false"
    >
      <path d="M15 18l-6-6 6-6" />
    </svg>
  );
}

function SpinnerIcon() {
  return (
    <svg
      width={16}
      height={16}
      viewBox="0 0 24 24"
      fill="none"
      stroke="currentColor"
      stroke-width="2"
      stroke-linecap="round"
      stroke-linejoin="round"
      class="animate-spin"
      aria-hidden="true"
      focusable="false"
    >
      <path d="M12 2v4M12 18v4M4.93 4.93l2.83 2.83M16.24 16.24l2.83 2.83M2 12h4M18 12h4M4.93 19.07l2.83-2.83M16.24 7.76l2.83-2.83" />
    </svg>
  );
}
