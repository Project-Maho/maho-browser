import { useCallback, useEffect, useReducer } from 'preact/hooks';
import { useBridge } from '../../hooks/use-bridge';
import { persistApiKey, removeApiKey } from './ai-settings-bridge';
import {
  aiSettingsReducer,
  initialAiSettingsState,
  type AiProvider,
} from './ai-settings-state';

export function useAiSettings() {
  const bridge = useBridge();
  const [state, dispatch] = useReducer(aiSettingsReducer, undefined, initialAiSettingsState);

  const loadSettings = useCallback(async () => {
    try {
      const settings = await bridge.getAiSettings();
      dispatch({ type: 'LOAD_SUCCESS', settings });
    } catch (error) {
      dispatch({
        type: 'LOAD_ERROR',
        message: error instanceof Error ? error.message : 'Failed to load AI settings.',
      });
    }
  }, [bridge]);

  useEffect(() => {
    void loadSettings();
  }, [loadSettings]);

  const saveProvider = useCallback(
    async (provider: AiProvider) => {
      dispatch({ type: 'CHANGE_PROVIDER', provider });
      dispatch({ type: 'SAVE_START', target: 'provider' });
      try {
        await bridge.setAiProvider(provider);
        dispatch({ type: 'PROVIDER_SAVED' });
      } catch (error) {
        dispatch({
          type: 'SAVE_ERROR',
          message: error instanceof Error ? error.message : 'Failed to save the AI provider.',
          restoreProvider: true,
        });
      }
    },
    [bridge],
  );

  const saveBaseUrl = useCallback(async () => {
    if (state.baseUrl === state.savedBaseUrl) return;
    dispatch({ type: 'SAVE_START', target: 'base-url' });
    try {
      await bridge.setAiBaseUrl(state.baseUrl);
      dispatch({ type: 'BASE_URL_SAVED' });
    } catch (error) {
      dispatch({
        type: 'SAVE_ERROR',
        message: error instanceof Error ? error.message : 'Failed to save the Base URL.',
      });
    }
  }, [bridge, state.baseUrl, state.savedBaseUrl]);

  const saveModel = useCallback(async () => {
    if (state.model === state.savedModel) return;
    dispatch({ type: 'SAVE_START', target: 'model' });
    try {
      await bridge.setAiModel(state.model);
      dispatch({ type: 'MODEL_SAVED' });
    } catch (error) {
      dispatch({
        type: 'SAVE_ERROR',
        message: error instanceof Error ? error.message : 'Failed to save the model.',
      });
    }
  }, [bridge, state.model, state.savedModel]);

  const saveApiKey = useCallback(async () => {
    if (!state.apiKeyDraft.trim()) return;
    dispatch({ type: 'SAVE_START', target: 'api-key' });
    try {
      await persistApiKey(bridge, state.provider, state.apiKeyDraft);
      dispatch({ type: 'API_KEY_SAVED', provider: state.provider });
    } catch (error) {
      dispatch({
        type: 'SAVE_ERROR',
        message: error instanceof Error ? error.message : 'Failed to save the API key.',
      });
    }
  }, [bridge, state.apiKeyDraft, state.provider]);

  const clearApiKey = useCallback(async () => {
    dispatch({ type: 'SAVE_START', target: 'api-key' });
    try {
      await removeApiKey(bridge, state.provider);
      dispatch({ type: 'API_KEY_CLEARED', provider: state.provider });
    } catch (error) {
      dispatch({
        type: 'SAVE_ERROR',
        message: error instanceof Error ? error.message : 'Failed to clear the API key.',
      });
    }
  }, [bridge, state.provider]);

  return {
    state,
    changeBaseUrl: (value: string) => dispatch({ type: 'CHANGE_BASE_URL', value }),
    changeModel: (value: string) => dispatch({ type: 'CHANGE_MODEL', value }),
    changeApiKey: (value: string) => dispatch({ type: 'CHANGE_API_KEY', value }),
    saveProvider,
    saveBaseUrl,
    saveModel,
    saveApiKey,
    clearApiKey,
  };
}
