import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/preact';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import type { AiSettings } from '../src/bridge/types';

const mockBridge = {
  getAiSettings: vi.fn<() => Promise<AiSettings>>(),
  setAiProvider: vi.fn<(provider: string) => Promise<void>>(),
  setAiBaseUrl: vi.fn<(url: string) => Promise<void>>(),
  setAiApiKey: vi.fn<(key: string) => Promise<void>>(),
  setAiModel: vi.fn<(model: string) => Promise<void>>(),
  byokSetKey: vi.fn<(provider: string, key: string) => Promise<boolean>>(),
  byokDeleteKey: vi.fn<(provider: string) => Promise<boolean>>(),
};

vi.mock('../src/hooks/use-bridge', () => ({
  useBridge: () => mockBridge,
}));

import { AiSettingsScreen } from '../src/screens/byok/byok-screen';

const OPENAI_SETTINGS: AiSettings = {
  provider: 'openai',
  baseUrl: '',
  model: 'gpt-4.1-mini',
  hasApiKey: false,
  hasByokOpenai: true,
  hasByokAnthropic: false,
};

beforeEach(() => {
  mockBridge.getAiSettings.mockResolvedValue(OPENAI_SETTINGS);
  mockBridge.setAiProvider.mockResolvedValue();
  mockBridge.setAiBaseUrl.mockResolvedValue();
  mockBridge.setAiApiKey.mockResolvedValue();
  mockBridge.setAiModel.mockResolvedValue();
  mockBridge.byokSetKey.mockResolvedValue(true);
  mockBridge.byokDeleteKey.mockResolvedValue(true);
});

afterEach(() => {
  cleanup();
  vi.clearAllMocks();
});

describe('AiSettingsScreen', () => {
  it('loads the current provider settings into accessible fields', async () => {
    render(<AiSettingsScreen />);

    expect(screen.getByText('Loading AI settings')).toBeTruthy();
    expect(await screen.findByRole('heading', { name: 'AI Settings' })).toBeTruthy();
    expect(screen.getByRole('combobox', { name: 'AI provider' })).toHaveValue('openai');
    expect(screen.getByLabelText('Model')).toHaveValue('gpt-4.1-mini');
    expect(screen.getByText('OpenAI API key is saved.')).toBeTruthy();
    expect(screen.queryByLabelText('Base URL')).toBeNull();
  });

  it('changes provider immediately and reveals custom endpoint fields', async () => {
    render(<AiSettingsScreen />);
    const provider = await screen.findByRole('combobox', { name: 'AI provider' });

    fireEvent.change(provider, { target: { value: 'openai-compatible' } });

    await waitFor(() => {
      expect(mockBridge.setAiProvider).toHaveBeenCalledWith('openai-compatible');
    });
    expect(screen.getByLabelText('Base URL')).toBeTruthy();
    expect(screen.getByLabelText('API Key')).toBeTruthy();
    expect(screen.getByText(/custom OpenAI-compatible endpoints/i)).toBeTruthy();
  });

  it('persists a custom base URL exactly as typed on blur', async () => {
    mockBridge.getAiSettings.mockResolvedValue({
      ...OPENAI_SETTINGS,
      provider: 'openai-compatible',
      baseUrl: '',
    });
    render(<AiSettingsScreen />);
    const baseUrl = await screen.findByLabelText('Base URL');
    const value = 'http://127.0.0.1:8317/v1';

    fireEvent.input(baseUrl, { target: { value } });
    fireEvent.blur(baseUrl);

    await waitFor(() => {
      expect(mockBridge.setAiBaseUrl).toHaveBeenCalledWith(value);
    });
  });

  it('saves the OpenAI-compatible API key without exposing a stored secret', async () => {
    mockBridge.getAiSettings.mockResolvedValue({
      ...OPENAI_SETTINGS,
      provider: 'openai-compatible',
      hasApiKey: true,
    });
    render(<AiSettingsScreen />);
    const apiKey = await screen.findByLabelText('API Key');

    expect(apiKey).toHaveValue('');
    expect(apiKey).toHaveAttribute('placeholder', 'Saved key — enter a new key to replace it');
    fireEvent.input(apiKey, { target: { value: 'custom-secret' } });
    fireEvent.click(screen.getByRole('button', { name: 'Save API key' }));

    await waitFor(() => {
      expect(mockBridge.setAiApiKey).toHaveBeenCalledWith('custom-secret');
      expect(screen.getByText('OpenAI-compatible API key is saved.')).toBeTruthy();
    });
  });

  it('saves and clears a provider BYOK key', async () => {
    mockBridge.getAiSettings.mockResolvedValue({
      ...OPENAI_SETTINGS,
      provider: 'anthropic',
      hasByokOpenai: false,
      hasByokAnthropic: false,
    });
    render(<AiSettingsScreen />);
    const apiKey = await screen.findByLabelText('API Key');

    fireEvent.input(apiKey, { target: { value: 'sk-ant-secret' } });
    fireEvent.click(screen.getByRole('button', { name: 'Save API key' }));

    await waitFor(() => {
      expect(mockBridge.byokSetKey).toHaveBeenCalledWith('anthropic', 'sk-ant-secret');
    });
    fireEvent.click(screen.getByRole('button', { name: 'Clear API key' }));
    await waitFor(() => {
      expect(mockBridge.byokDeleteKey).toHaveBeenCalledWith('anthropic');
      expect(screen.getByText('Anthropic API key is not saved.')).toBeTruthy();
    });
  });

  it('persists the optional model on blur', async () => {
    render(<AiSettingsScreen />);
    const model = await screen.findByLabelText('Model');

    fireEvent.input(model, { target: { value: 'gpt-4.1' } });
    fireEvent.blur(model);

    await waitFor(() => {
      expect(mockBridge.setAiModel).toHaveBeenCalledWith('gpt-4.1');
    });
  });

  it('shows no credential fields for the managed provider', async () => {
    mockBridge.getAiSettings.mockResolvedValue({
      ...OPENAI_SETTINGS,
      provider: 'maho-managed',
      model: '',
    });
    render(<AiSettingsScreen />);

    expect(await screen.findByText('Maho manages the secure relay connection.')).toBeTruthy();
    expect(screen.queryByLabelText('API Key')).toBeNull();
    expect(screen.queryByLabelText('Model')).toBeNull();
  });

  it('calls the passed back handler', async () => {
    const onBack = vi.fn<() => void>();
    render(<AiSettingsScreen onBack={onBack} />);
    await screen.findByRole('heading', { name: 'AI Settings' });

    fireEvent.click(screen.getByRole('button', { name: 'Back' }));

    expect(onBack).toHaveBeenCalledOnce();
  });
});
