import type { JSX } from 'preact';
import { Button } from '../../ui/button';
import { Icon } from '../../ui/icon';
import { Input } from '../../ui/input';
import { ListItem } from '../../ui/list-item';
import { StatusBanner } from '../../ui/status-banner';
import { getProviderName } from './ai-settings-bridge';
import type { AiSettingsState } from './ai-settings-state';

export interface ProviderSettingsProps {
  state: AiSettingsState;
  onBaseUrlChange: (value: string) => void;
  onBaseUrlBlur: () => void;
  onModelChange: (value: string) => void;
  onModelBlur: () => void;
  onApiKeyChange: (value: string) => void;
  onSaveApiKey: () => void;
  onClearApiKey: () => void;
}

export function ProviderSettings(props: ProviderSettingsProps): JSX.Element {
  switch (props.state.provider) {
    case '':
    case 'maho-managed':
      return (
        <ListItem
          class="ai-settings-card ai-settings-managed"
          leading={<Icon name="bot" size={20} aria-hidden />}
          headline="Managed relay"
          supporting="Maho manages the secure relay connection."
        />
      );
    case 'openai':
    case 'anthropic':
    case 'openai-compatible':
      return <ProviderFields {...props} />;
    default:
      return assertNever(props.state.provider);
  }
}

function ProviderFields(props: ProviderSettingsProps) {
  const { state } = props;
  const hasStoredKey = getHasStoredKey(state);
  const providerName = getProviderName(state.provider);
  return (
    <section class="ai-settings-section" aria-labelledby="connection-heading">
      <div class="ai-settings-section-heading">
        <h2 id="connection-heading">Connection</h2>
        <p>Credentials are stored by the native secure-storage layer.</p>
      </div>
      <div class="ai-settings-card ai-settings-fields">
        {state.provider === 'openai-compatible' ? (
          <>
            <StatusBanner kind="info" class="ai-settings-banner">
              Use this mode for custom OpenAI-compatible endpoints.
            </StatusBanner>
            <Input
              id="ai-base-url"
              label="Base URL"
              value={state.baseUrl}
              placeholder="http://127.0.0.1:8317/v1"
              autocomplete="url"
              autocapitalize="none"
              spellcheck={false}
              disabled={state.pending !== null}
              class="ai-settings-input"
              onInput={(event) => props.onBaseUrlChange(event.currentTarget.value)}
              onBlur={props.onBaseUrlBlur}
            />
          </>
        ) : null}
        <Input
          id="ai-api-key"
          label="API Key"
          type="password"
          value={state.apiKeyDraft}
          placeholder={hasStoredKey ? 'Saved key — enter a new key to replace it' : `Enter ${providerName} API key`}
          autocomplete="off"
          autocorrect="off"
          autocapitalize="none"
          spellcheck={false}
          disabled={state.pending !== null}
          class="ai-settings-input"
          hint={`${providerName} API key is ${hasStoredKey ? '' : 'not '}saved.`}
          onInput={(event) => props.onApiKeyChange(event.currentTarget.value)}
        />
        <div class="ai-settings-actions">
          <Button
            variant="primary"
            class="ai-settings-action"
            loading={state.pending === 'api-key' && state.apiKeyDraft.length > 0}
            disabled={!state.apiKeyDraft.trim() || state.pending !== null}
            onClick={props.onSaveApiKey}
            aria-label="Save API key"
          >
            Save key
          </Button>
          {hasStoredKey ? (
            <Button
              variant="ghost"
              class="ai-settings-action"
              disabled={state.pending !== null}
              onClick={props.onClearApiKey}
              aria-label="Clear API key"
            >
              Clear
            </Button>
          ) : null}
        </div>
        <Input
          id="ai-model"
          label="Model"
          value={state.model}
          placeholder="Enter model identifier"
          autocomplete="off"
          autocorrect="off"
          autocapitalize="none"
          spellcheck={false}
          disabled={state.pending !== null}
          class="ai-settings-input"
          hint="Optional. Leave blank to use the provider default."
          onInput={(event) => props.onModelChange(event.currentTarget.value)}
          onBlur={props.onModelBlur}
        />
      </div>
    </section>
  );
}

function getHasStoredKey(state: AiSettingsState): boolean {
  switch (state.provider) {
    case 'openai':
      return state.hasByokOpenai;
    case 'anthropic':
      return state.hasByokAnthropic;
    case 'openai-compatible':
      return state.hasApiKey;
    case '':
    case 'maho-managed':
      return false;
    default:
      return assertNever(state.provider);
  }
}

function assertNever(value: never): never {
  throw new Error(`Unhandled AI provider: ${JSON.stringify(value)}`);
}
