import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/preact';
import { afterEach, describe, expect, it, vi } from 'vitest';
import type { SpaceAIConfig } from '../../../bridge';

const mockBridge = {
  getSpaceAIConfig: vi.fn<(spaceId: string) => Promise<SpaceAIConfig | null>>(),
  setSpaceAIConfig: vi.fn<(spaceId: string, config: SpaceAIConfig) => Promise<void>>(),
};

vi.mock('../../../hooks/use-bridge', () => ({
  useBridge: () => mockBridge,
}));

import { SpaceAIConfigScreen } from '../space-ai-config-screen';

function renderScreen(hash = '#space-ai-config?spaceId=space-1') {
  window.location.hash = hash;
  return render(<SpaceAIConfigScreen />);
}

afterEach(() => {
  cleanup();
  vi.clearAllMocks();
  window.location.hash = '';
});

describe('SpaceAIConfigScreen', () => {
  it('renders a missing spaceId error when no query param is present', () => {
    renderScreen('#space-ai-config');

    expect(screen.getByText('Missing space')).toBeTruthy();
    expect(screen.getByText('No spaceId provided in URL.')).toBeTruthy();
    expect(mockBridge.getSpaceAIConfig).not.toHaveBeenCalled();
  });

  it('loads existing config on mount and populates the form', async () => {
    mockBridge.getSpaceAIConfig.mockResolvedValue({
      model: 'claude-sonnet-4-7',
      systemInstruction: 'Be precise.',
      enabled: false,
    });

    renderScreen('#space-ai-config?spaceId=abc-123');

    await waitFor(() => {
      expect(mockBridge.getSpaceAIConfig).toHaveBeenCalledWith('abc-123');
    });

    expect(screen.getByLabelText('Model')).toHaveValue('claude-sonnet-4-7');
    expect(screen.getByLabelText('Persona / System Prompt')).toHaveValue('Be precise.');
    expect(screen.getByRole('checkbox')).not.toBeChecked();
  });

  it('uses the default config when the bridge returns null', async () => {
    mockBridge.getSpaceAIConfig.mockResolvedValue(null);

    renderScreen('#space-ai-config?spaceId=fresh-space');

    await waitFor(() => {
      expect(mockBridge.getSpaceAIConfig).toHaveBeenCalledWith('fresh-space');
    });

    expect(screen.getByLabelText('Model')).toHaveValue('');
    expect(screen.getByLabelText('Persona / System Prompt')).toHaveValue('');
    expect(screen.getByRole('checkbox')).toBeChecked();
  });

  it('saves the edited config through the bridge', async () => {
    mockBridge.getSpaceAIConfig.mockResolvedValue({
      model: 'gpt-4o',
      systemInstruction: 'Start here.',
      enabled: true,
    });
    mockBridge.setSpaceAIConfig.mockResolvedValue(undefined);

    renderScreen('#space-ai-config?spaceId=abc-123');

    await waitFor(() => {
      expect(screen.getByRole('button', { name: 'Save' })).toBeTruthy();
    });

    fireEvent.change(screen.getByLabelText('Model'), {
      target: { value: 'claude-sonnet-4-7' },
    });
    fireEvent.input(screen.getByLabelText('Persona / System Prompt'), {
      target: { value: 'Use rigorous citations.' },
    });
    fireEvent.click(screen.getByRole('checkbox'));
    fireEvent.click(screen.getByRole('button', { name: 'Save' }));

    await waitFor(() => {
      expect(mockBridge.setSpaceAIConfig).toHaveBeenCalledWith('abc-123', {
        model: 'claude-sonnet-4-7',
        systemInstruction: 'Use rigorous citations.',
        enabled: false,
      });
    });
  });

  it('shows an error banner when save fails', async () => {
    mockBridge.getSpaceAIConfig.mockResolvedValue({
      model: 'gpt-4o',
      systemInstruction: 'Keep the defaults.',
      enabled: true,
    });
    mockBridge.setSpaceAIConfig.mockRejectedValue(new Error('Save failed hard'));

    renderScreen('#space-ai-config?spaceId=broken-save');

    await waitFor(() => {
      expect(screen.getByRole('button', { name: 'Save' })).toBeTruthy();
    });

    fireEvent.click(screen.getByRole('button', { name: 'Save' }));

    await waitFor(() => {
      expect(screen.getByRole('alert')).toHaveTextContent('Save failed hard');
    });
  });

  it('navigates back on cancel', async () => {
    mockBridge.getSpaceAIConfig.mockResolvedValue({
      model: null,
      systemInstruction: null,
      enabled: true,
    });
    const backSpy = vi.spyOn(window.history, 'back').mockImplementation(() => {});

    renderScreen('#space-ai-config?spaceId=abc-123');

    await waitFor(() => {
      expect(screen.getByRole('button', { name: 'Cancel' })).toBeTruthy();
    });

    fireEvent.click(screen.getByRole('button', { name: 'Cancel' }));

    expect(backSpy).toHaveBeenCalledTimes(1);
  });
});
