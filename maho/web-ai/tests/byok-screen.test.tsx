/**
 * Unit tests for the BYOK Settings screen.
 *
 * The bridge is fully mocked so tests run in jsdom without a WebView.
 */
import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/preact';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';

// ── Mock bridge ──────────────────────────────────────────────────────────────
const mockBridge = {
  byokGetProviders: vi.fn<() => Promise<string[]>>(),
  byokGetKey: vi.fn<(provider: string) => Promise<string | null>>(),
  byokSetKey: vi.fn<(provider: string, key: string) => Promise<boolean>>(),
  byokDeleteKey: vi.fn<(provider: string) => Promise<boolean>>(),
  byokValidateKey: vi.fn<(provider: string, key: string) => Promise<boolean>>(),
};

vi.mock('../src/hooks/use-bridge', () => ({
  useBridge: () => mockBridge,
}));

import { BYOKScreen } from '../src/screens/byok/byok-screen';

afterEach(() => {
  cleanup();
  vi.clearAllMocks();
});

// Helper: click the "Add Key" button (not the section header of the same text)
function clickAddKeyButton() {
  fireEvent.click(screen.getByRole('button', { name: /Add Key/i }));
}

describe('BYOKScreen', () => {
  describe('initial load', () => {
    it('shows the loading state while fetching keys', () => {
      mockBridge.byokGetProviders.mockReturnValue(new Promise(() => {}));
      render(<BYOKScreen />);
      expect(screen.getByText('Loading saved keys')).toBeTruthy();
    });

    it('shows "No API keys saved" when no providers have keys', async () => {
      mockBridge.byokGetProviders.mockResolvedValue(['openai', 'anthropic']);
      mockBridge.byokGetKey.mockResolvedValue(null);

      render(<BYOKScreen />);

      await waitFor(() => {
        expect(screen.getByText('No API keys saved')).toBeTruthy();
      });
    });

    it('renders saved keys after load', async () => {
      mockBridge.byokGetProviders.mockResolvedValue(['openai', 'anthropic']);
      mockBridge.byokGetKey.mockImplementation(async (provider: string) => {
        if (provider === 'openai') return 'sk-test1234';
        return null;
      });

      render(<BYOKScreen />);

      await waitFor(() => {
        expect(screen.getByText('OpenAI')).toBeTruthy();
      });

      // Masked key should end with the last 4 chars
      expect(screen.getByText(/1234$/)).toBeTruthy();
    });
  });

  describe('Add Key sheet', () => {
    beforeEach(() => {
      mockBridge.byokGetProviders.mockResolvedValue(['openai', 'anthropic']);
      mockBridge.byokGetKey.mockResolvedValue(null);
    });

    it('opens the Add Key sheet on button click', async () => {
      render(<BYOKScreen />);

      await waitFor(() => screen.getByRole('button', { name: /Add Key/i }));
      clickAddKeyButton();

      expect(screen.getByText('Add API Key')).toBeTruthy();
    });

    it('dismisses the sheet on Cancel', async () => {
      render(<BYOKScreen />);
      await waitFor(() => screen.getByRole('button', { name: /Add Key/i }));
      clickAddKeyButton();

      fireEvent.click(screen.getByText('Cancel'));

      await waitFor(() => {
        expect(screen.queryByText('Add API Key')).toBeNull();
      });
    });
  });

  describe('validate flow', () => {
    beforeEach(() => {
      mockBridge.byokGetProviders.mockResolvedValue(['openai', 'anthropic']);
      mockBridge.byokGetKey.mockResolvedValue(null);
    });

    it('shows "Validated" message on successful validation', async () => {
      mockBridge.byokValidateKey.mockResolvedValue(true);

      render(<BYOKScreen />);
      await waitFor(() => screen.getByRole('button', { name: /Add Key/i }));
      clickAddKeyButton();

      const input = screen.getByPlaceholderText('sk-...');
      fireEvent.input(input, { target: { value: 'sk-good-key' } });

      fireEvent.click(screen.getByRole('button', { name: /^Validate$/i }));

      await waitFor(() => {
        expect(screen.getByText(/Validated/)).toBeTruthy();
      });
    });

    it('shows failure message on invalid key', async () => {
      mockBridge.byokValidateKey.mockResolvedValue(false);

      render(<BYOKScreen />);
      await waitFor(() => screen.getByRole('button', { name: /Add Key/i }));
      clickAddKeyButton();

      const input = screen.getByPlaceholderText('sk-...');
      fireEvent.input(input, { target: { value: 'sk-bad-key' } });

      fireEvent.click(screen.getByRole('button', { name: /^Validate$/i }));

      await waitFor(() => {
        expect(screen.getByText(/Validation failed/)).toBeTruthy();
      });
    });
  });

  describe('save flow', () => {
    it('saves a key and shows it in the list', async () => {
      mockBridge.byokGetProviders.mockResolvedValue(['openai', 'anthropic']);
      mockBridge.byokGetKey.mockResolvedValue(null);
      mockBridge.byokValidateKey.mockResolvedValue(true);
      mockBridge.byokSetKey.mockResolvedValue(true);

      render(<BYOKScreen />);
      await waitFor(() => screen.getByRole('button', { name: /Add Key/i }));
      clickAddKeyButton();

      const input = screen.getByPlaceholderText('sk-...');
      fireEvent.input(input, { target: { value: 'sk-saved5678' } });

      fireEvent.click(screen.getByRole('button', { name: /^Validate$/i }));
      await waitFor(() => screen.getByText(/Validated/));

      fireEvent.click(screen.getByRole('button', { name: /^Save$/i }));

      await waitFor(() => {
        expect(screen.queryByText('Add API Key')).toBeNull();
        expect(screen.getByText('OpenAI')).toBeTruthy();
      });
    });
  });

  describe('delete flow', () => {
    it('removes a provider after deletion', async () => {
      mockBridge.byokGetProviders.mockResolvedValue(['openai', 'anthropic']);
      mockBridge.byokGetKey.mockImplementation(async (provider: string) => {
        if (provider === 'openai') return 'sk-del1234';
        return null;
      });
      mockBridge.byokDeleteKey.mockResolvedValue(true);

      render(<BYOKScreen />);
      await waitFor(() => screen.getByText('OpenAI'));

      fireEvent.click(screen.getByLabelText('Delete OpenAI key'));

      await waitFor(() => {
        expect(screen.getByText('No API keys saved')).toBeTruthy();
      });
    });
  });
});
