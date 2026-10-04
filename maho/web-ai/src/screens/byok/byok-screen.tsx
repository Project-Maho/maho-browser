import type { JSX } from 'preact';
import { useState } from 'preact/hooks';
import { Icon } from '../../ui/icon';
import { ListItem } from '../../ui/list-item';
import { StatusBanner } from '../../ui/status-banner';
import { ProviderSettings } from './ai-settings-fields';
import { parseAiProvider, type AiProvider } from './ai-settings-state';
import './byok.css';
import { useAiSettings } from './use-ai-settings';

const PROVIDER_OPTIONS = [
  { label: 'Maho Managed', value: 'maho-managed' },
  { label: 'OpenAI', value: 'openai' },
  { label: 'Anthropic', value: 'anthropic' },
  { label: 'OpenAI-compatible', value: 'openai-compatible' },
] as const satisfies readonly { label: string; value: Exclude<AiProvider, ''> }[];

interface AiSettingsScreenProps {
  onBack?: () => void;
}

function readThinkingExpandedDefault(): boolean {
  try {
    return localStorage.getItem('maho.agent.thinkingExpandedByDefault') === 'true';
  } catch {
    return false;
  }
}

export function AiSettingsScreen({ onBack }: AiSettingsScreenProps) {
  const settings = useAiSettings();
  const { state } = settings;
  const [thinkingExpanded, setThinkingExpanded] = useState(readThinkingExpandedDefault);

  const handleToggleThinking = (event: JSX.TargetedEvent<HTMLInputElement, Event>) => {
    const nextVal = event.currentTarget.checked;
    try {
      localStorage.setItem('maho.agent.thinkingExpandedByDefault', nextVal ? 'true' : 'false');
    } catch {
      // localStorage unavailable (private mode / disabled) — keep in-memory only.
    }
    setThinkingExpanded(nextVal);
  };

  return (
    <div class="ai-settings-screen" data-testid="aiScreenBYOK">
      <header class="ai-settings-header">
        <div class="ai-settings-header-inner">
          {onBack ? (
            <button type="button" class="ai-settings-back" onClick={onBack} aria-label="Back">
              <Icon name="chevron-left" size={20} aria-hidden />
            </button>
          ) : null}
          <div>
            <h1>AI Settings</h1>
            <p>Choose how Maho connects to an AI provider.</p>
          </div>
        </div>
      </header>

      <main class="ai-settings-content">
        {state.loading ? (
          <ListItem
            class="ai-settings-card ai-settings-loading"
            leading={<Icon name="loader" size={18} class="animate-spin" aria-hidden />}
            headline="Loading AI settings"
            supporting="Reading your provider and model configuration."
          />
        ) : (
          <>
            {state.errorMessage ? (
              <StatusBanner kind="error" class="ai-settings-banner">
                {state.errorMessage}
              </StatusBanner>
            ) : null}
            <section class="ai-settings-section" aria-labelledby="provider-heading">
              <div class="ai-settings-section-heading">
                <h2 id="provider-heading">Provider</h2>
                <p>Select the service Maho should use for AI requests.</p>
              </div>
              <div class="ai-settings-card ai-settings-fields">
                <label class="ai-settings-select-label" for="ai-provider">AI provider</label>
                <select
                  id="ai-provider"
                  class="ai-settings-select"
                  value={state.provider === '' ? 'maho-managed' : state.provider}
                  disabled={state.pending !== null}
                  onChange={(event: JSX.TargetedEvent<HTMLSelectElement, Event>) => {
                    void settings.saveProvider(parseAiProvider(event.currentTarget.value));
                  }}
                >
                  {PROVIDER_OPTIONS.map((option) => (
                    <option key={option.value} value={option.value}>{option.label}</option>
                  ))}
                </select>
              </div>
            </section>
            <ProviderSettings
              state={state}
              onBaseUrlChange={settings.changeBaseUrl}
              onBaseUrlBlur={() => void settings.saveBaseUrl()}
              onModelChange={settings.changeModel}
              onModelBlur={() => void settings.saveModel()}
              onApiKeyChange={settings.changeApiKey}
              onSaveApiKey={() => void settings.saveApiKey()}
              onClearApiKey={() => void settings.clearApiKey()}
            />
            <section class="ai-settings-section" aria-labelledby="agent-settings-heading">
              <div class="ai-settings-section-heading">
                <h2 id="agent-settings-heading">Agent Settings</h2>
                <p>Configure agent interaction preferences.</p>
              </div>
              <div class="ai-settings-card ai-settings-toggle-row">
                <label class="ai-settings-toggle-label" for="agent-thinking-default">
                  <span class="ai-settings-toggle-title">Expand thinking by default</span>
                  <span class="ai-settings-toggle-desc">Show full agent reasoning without needing to click.</span>
                </label>
                <input
                  id="agent-thinking-default"
                  type="checkbox"
                  class="ai-settings-checkbox"
                  checked={thinkingExpanded}
                  onChange={handleToggleThinking}
                />
              </div>
            </section>
          </>
        )}
      </main>
    </div>
  );
}
