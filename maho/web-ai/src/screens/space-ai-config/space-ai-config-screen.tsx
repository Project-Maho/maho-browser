import { useCallback, useEffect, useMemo, useReducer } from 'preact/hooks';
import { useBridge } from '../../hooks/use-bridge';
import { useHashRoute } from '../../hooks/use-hash-route';
import type { SpaceAIConfig } from '../../bridge';

interface SpaceAIConfigScreenProps {
  onBack?: () => void;
}

interface State {
  loading: boolean;
  saving: boolean;
  config: SpaceAIConfig | null;
  errorMessage: string | null;
  savedAt: number | null;
}

type Action =
  | { type: 'LOAD_START' }
  | { type: 'LOAD_SUCCESS'; config: SpaceAIConfig }
  | { type: 'LOAD_ERROR'; message: string }
  | { type: 'UPDATE_MODEL'; value: string }
  | { type: 'UPDATE_SYSTEM_INSTRUCTION'; value: string }
  | { type: 'UPDATE_ENABLED'; value: boolean }
  | { type: 'SAVE_START' }
  | { type: 'SAVE_SUCCESS'; timestamp: number }
  | { type: 'SAVE_ERROR'; message: string };

interface ModelOption {
  value: string;
  label: string;
}

const BASE_MODEL_OPTIONS: ModelOption[] = [
  { value: '', label: 'Default (per profile)' },
  { value: 'gpt-4o', label: 'GPT-4o' },
  { value: 'claude-sonnet-4-7', label: 'Claude Sonnet 4.7' },
];

function initialState(): State {
  return {
    loading: true,
    saving: false,
    config: null,
    errorMessage: null,
    savedAt: null,
  };
}

function reducer(state: State, action: Action): State {
  switch (action.type) {
    case 'LOAD_START':
      return {
        ...state,
        loading: true,
        errorMessage: null,
        savedAt: null,
      };
    case 'LOAD_SUCCESS':
      return {
        ...state,
        loading: false,
        config: action.config,
        errorMessage: null,
      };
    case 'LOAD_ERROR':
      return {
        ...state,
        loading: false,
        config: null,
        errorMessage: action.message,
      };
    case 'UPDATE_MODEL':
      return updateConfig(state, { model: action.value || null });
    case 'UPDATE_SYSTEM_INSTRUCTION':
      return updateConfig(state, { systemInstruction: action.value || null });
    case 'UPDATE_ENABLED':
      return updateConfig(state, { enabled: action.value });
    case 'SAVE_START':
      return {
        ...state,
        saving: true,
        errorMessage: null,
      };
    case 'SAVE_SUCCESS':
      return {
        ...state,
        saving: false,
        errorMessage: null,
        savedAt: action.timestamp,
      };
    case 'SAVE_ERROR':
      return {
        ...state,
        saving: false,
        errorMessage: action.message,
      };
    default:
      return state;
  }
}

function updateConfig(state: State, patch: Partial<SpaceAIConfig>): State {
  if (!state.config) {
    return state;
  }

  return {
    ...state,
    config: {
      ...state.config,
      ...patch,
    },
    errorMessage: null,
    savedAt: null,
  };
}

function defaultConfig(): SpaceAIConfig {
  return {
    model: null,
    systemInstruction: null,
    enabled: true,
  };
}

function getModelOptions(currentModel: string | null): ModelOption[] {
  if (!currentModel || BASE_MODEL_OPTIONS.some((option) => option.value === currentModel)) {
    return BASE_MODEL_OPTIONS;
  }

  return [
    ...BASE_MODEL_OPTIONS,
    {
      value: currentModel,
      label: currentModel,
    },
  ];
}

function toErrorMessage(error: unknown, fallback: string): string {
  return error instanceof Error ? error.message : fallback;
}

function formatSavedTime(savedAt: number): string {
  return new Date(savedAt).toLocaleTimeString([], {
    hour: 'numeric',
    minute: '2-digit',
  });
}

export function SpaceAIConfigScreen({ onBack }: SpaceAIConfigScreenProps) {
  const bridge = useBridge();
  const route = useHashRoute();
  const [state, dispatch] = useReducer(reducer, undefined, initialState);

  const spaceId = route.params.spaceId?.trim() || undefined;
  const modelOptions = useMemo(
    () => getModelOptions(state.config?.model ?? null),
    [state.config?.model],
  );

  const handleBack = useCallback(() => {
    if (onBack) {
      onBack();
      return;
    }

    window.history.back();
  }, [onBack]);

  const loadConfig = useCallback(async () => {
    if (!spaceId) {
      return;
    }

    dispatch({ type: 'LOAD_START' });

    try {
      const existingConfig = await bridge.getSpaceAIConfig(spaceId);
      dispatch({ type: 'LOAD_SUCCESS', config: existingConfig ?? defaultConfig() });
    } catch (error) {
      dispatch({
        type: 'LOAD_ERROR',
        message: toErrorMessage(error, 'Unable to load this space configuration.'),
      });
    }
  }, [bridge, spaceId]);

  useEffect(() => {
    if (!spaceId) {
      return;
    }

    void loadConfig();
  }, [loadConfig, spaceId]);

  const saveConfig = useCallback(async () => {
    if (!spaceId || !state.config) {
      return;
    }

    dispatch({ type: 'SAVE_START' });

    try {
      await bridge.setSpaceAIConfig(spaceId, state.config);
      dispatch({ type: 'SAVE_SUCCESS', timestamp: Date.now() });
    } catch (error) {
      dispatch({
        type: 'SAVE_ERROR',
        message: toErrorMessage(error, 'Unable to save this space configuration.'),
      });
    }
  }, [bridge, spaceId, state.config]);

  if (!spaceId) {
    return (
      <div class="sai-shell">
        <div class="space-ai-config">
          <header class="sai-topbar">
            <button type="button" class="sai-nav-button" onClick={handleBack} aria-label="Back">
              Back
            </button>
          </header>
          <div class="sai-state-card sai-error-state">
            <h1 class="sai-state-title">Missing space</h1>
            <p class="sai-state-copy">No spaceId provided in URL.</p>
          </div>
        </div>
      </div>
    );
  }

  return (
    <div class="sai-shell">
      <div class="space-ai-config" data-testid="space-ai-config-screen">
        <header class="sai-topbar">
          <button type="button" class="sai-nav-button" onClick={handleBack} aria-label="Back">
            Back
          </button>
        </header>

        <section class="sai-panel">
          <div class="sai-header">
            <div>
              <p class="sai-eyebrow">Space AI</p>
              <h1 class="sai-title">Configure this space assistant</h1>
              <p class="sai-copy">
                Pin a model, set the standing instruction, or disable AI behavior for this
                specific space.
              </p>
            </div>
            <span class="sai-space-id">{spaceId}</span>
          </div>

          {state.loading ? (
            <div class="sai-state-card sai-loading" role="status">
              Loading configuration…
            </div>
          ) : !state.config ? (
            <div class="sai-state-card sai-error-state">
              <h2 class="sai-state-title">Could not load configuration</h2>
              <p class="sai-state-copy">{state.errorMessage ?? 'Failed to load this space.'}</p>
              <div class="sai-actions">
                <button
                  type="button"
                  class="sai-button sai-button-secondary"
                  onClick={() => void loadConfig()}
                >
                  Retry
                </button>
                <button type="button" class="sai-button sai-save" onClick={handleBack}>
                  Back
                </button>
              </div>
            </div>
          ) : (
            <form
              class="sai-form"
              onSubmit={(event) => {
                event.preventDefault();
                void saveConfig();
              }}
            >
              <div class="sai-field">
                <label class="sai-label" for="space-ai-config-model">
                  Model
                </label>
                <select
                  id="space-ai-config-model"
                  class="sai-select"
                  aria-describedby="space-ai-config-model-support"
                  value={state.config.model ?? ''}
                  onChange={(event) =>
                    dispatch({
                      type: 'UPDATE_MODEL',
                      value: (event.target as HTMLSelectElement).value,
                    })
                  }
                >
                  {modelOptions.map((option) => (
                    <option key={option.value || 'default'} value={option.value}>
                      {option.label}
                    </option>
                    ))}
                </select>
                <span id="space-ai-config-model-support" class="sai-support">
                  Leave on default to inherit the profile-wide model.
                </span>
              </div>

              <div class="sai-field">
                <label class="sai-label" for="space-ai-config-system-instruction">
                  Persona / System Prompt
                </label>
                <textarea
                  id="space-ai-config-system-instruction"
                  class="sai-textarea"
                  aria-describedby="space-ai-config-system-instruction-support"
                  value={state.config.systemInstruction ?? ''}
                  rows={7}
                  placeholder="Guide how the assistant should behave inside this space."
                  onInput={(event) =>
                    dispatch({
                      type: 'UPDATE_SYSTEM_INSTRUCTION',
                      value: (event.target as HTMLTextAreaElement).value,
                    })
                  }
                />
                <span id="space-ai-config-system-instruction-support" class="sai-support">
                  Keep it empty to use the default instruction for new conversations.
                </span>
              </div>

              <div class="sai-field sai-field-row">
                <span class="sai-field-copy">
                  <label class="sai-label" for="space-ai-config-enabled">
                    Enable AI for this space
                  </label>
                  <span id="space-ai-config-enabled-support" class="sai-support">
                    Turn this off when the space should never spawn AI-assisted actions.
                  </span>
                </span>
                <input
                  id="space-ai-config-enabled"
                  class="sai-switch"
                  type="checkbox"
                  aria-describedby="space-ai-config-enabled-support"
                  checked={state.config.enabled}
                  onChange={(event) =>
                    dispatch({
                      type: 'UPDATE_ENABLED',
                      value: (event.target as HTMLInputElement).checked,
                    })
                  }
                />
              </div>

              {state.errorMessage && (
                <div class="sai-error" role="alert">
                  {state.errorMessage}
                </div>
              )}

              {state.savedAt && !state.errorMessage && (
                <div class="sai-saved" role="status">
                  Saved at {formatSavedTime(state.savedAt)}
                </div>
              )}

              <div class="sai-actions">
                <button type="button" class="sai-button sai-button-secondary" onClick={handleBack}>
                  Cancel
                </button>
                <button type="submit" class="sai-button sai-save" disabled={state.saving}>
                  {state.saving ? 'Saving…' : 'Save'}
                </button>
              </div>
            </form>
          )}
        </section>
      </div>
    </div>
  );
}
