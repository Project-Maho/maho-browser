import type { AiSettings } from '../../bridge/types';

export type AiProvider = '' | 'maho-managed' | 'openai' | 'anthropic' | 'openai-compatible';
export type SaveTarget = 'provider' | 'base-url' | 'api-key' | 'model';

export interface AiSettingsState {
  loading: boolean;
  provider: AiProvider;
  savedProvider: AiProvider;
  baseUrl: string;
  savedBaseUrl: string;
  model: string;
  savedModel: string;
  apiKeyDraft: string;
  hasApiKey: boolean;
  hasByokOpenai: boolean;
  hasByokAnthropic: boolean;
  pending: SaveTarget | null;
  errorMessage: string | null;
  savedTarget: SaveTarget | null;
}

export type AiSettingsAction =
  | { type: 'LOAD_SUCCESS'; settings: AiSettings }
  | { type: 'LOAD_ERROR'; message: string }
  | { type: 'CHANGE_PROVIDER'; provider: AiProvider }
  | { type: 'CHANGE_BASE_URL'; value: string }
  | { type: 'CHANGE_MODEL'; value: string }
  | { type: 'CHANGE_API_KEY'; value: string }
  | { type: 'SAVE_START'; target: SaveTarget }
  | { type: 'PROVIDER_SAVED' }
  | { type: 'BASE_URL_SAVED' }
  | { type: 'MODEL_SAVED' }
  | { type: 'API_KEY_SAVED'; provider: AiProvider }
  | { type: 'API_KEY_CLEARED'; provider: AiProvider }
  | { type: 'SAVE_ERROR'; message: string; restoreProvider?: boolean };

export function initialAiSettingsState(): AiSettingsState {
  return {
    loading: true,
    provider: '',
    savedProvider: '',
    baseUrl: '',
    savedBaseUrl: '',
    model: '',
    savedModel: '',
    apiKeyDraft: '',
    hasApiKey: false,
    hasByokOpenai: false,
    hasByokAnthropic: false,
    pending: null,
    errorMessage: null,
    savedTarget: null,
  };
}

export function parseAiProvider(value: string): AiProvider {
  switch (value) {
    case '':
    case 'maho-managed':
    case 'openai':
    case 'anthropic':
    case 'openai-compatible':
      return value;
    default:
      return '';
  }
}

export function aiSettingsReducer(
  state: AiSettingsState,
  action: AiSettingsAction,
): AiSettingsState {
  switch (action.type) {
    case 'LOAD_SUCCESS': {
      const provider = parseAiProvider(action.settings.provider);
      return {
        ...state,
        loading: false,
        provider,
        savedProvider: provider,
        baseUrl: action.settings.baseUrl,
        savedBaseUrl: action.settings.baseUrl,
        model: action.settings.model,
        savedModel: action.settings.model,
        hasApiKey: action.settings.hasApiKey,
        hasByokOpenai: action.settings.hasByokOpenai,
        hasByokAnthropic: action.settings.hasByokAnthropic,
        errorMessage: null,
      };
    }
    case 'LOAD_ERROR':
      return { ...state, loading: false, errorMessage: action.message };
    case 'CHANGE_PROVIDER':
      return {
        ...state,
        provider: action.provider,
        apiKeyDraft: '',
        errorMessage: null,
        savedTarget: null,
      };
    case 'CHANGE_BASE_URL':
      return { ...state, baseUrl: action.value, savedTarget: null };
    case 'CHANGE_MODEL':
      return { ...state, model: action.value, savedTarget: null };
    case 'CHANGE_API_KEY':
      return { ...state, apiKeyDraft: action.value, savedTarget: null };
    case 'SAVE_START':
      return { ...state, pending: action.target, errorMessage: null, savedTarget: null };
    case 'PROVIDER_SAVED':
      return { ...state, savedProvider: state.provider, pending: null, savedTarget: 'provider' };
    case 'BASE_URL_SAVED':
      return { ...state, savedBaseUrl: state.baseUrl, pending: null, savedTarget: 'base-url' };
    case 'MODEL_SAVED':
      return { ...state, savedModel: state.model, pending: null, savedTarget: 'model' };
    case 'API_KEY_SAVED':
      return updateApiKeyState(state, action.provider, true);
    case 'API_KEY_CLEARED':
      return updateApiKeyState(state, action.provider, false);
    case 'SAVE_ERROR':
      return {
        ...state,
        provider: action.restoreProvider ? state.savedProvider : state.provider,
        pending: null,
        errorMessage: action.message,
      };
    default:
      return assertNever(action);
  }
}

function updateApiKeyState(
  state: AiSettingsState,
  provider: AiProvider,
  isSaved: boolean,
): AiSettingsState {
  return {
    ...state,
    apiKeyDraft: '',
    hasApiKey: provider === 'openai-compatible' ? isSaved : state.hasApiKey,
    hasByokOpenai: provider === 'openai' ? isSaved : state.hasByokOpenai,
    hasByokAnthropic: provider === 'anthropic' ? isSaved : state.hasByokAnthropic,
    pending: null,
    errorMessage: null,
    savedTarget: 'api-key',
  };
}

function assertNever(value: never): never {
  throw new Error(`Unhandled AI settings action: ${JSON.stringify(value)}`);
}
