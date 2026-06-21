/**
 * Unit tests for the Privacy Dashboard screen.
 *
 * Bridge methods mocked:
 *   conversationList()      → ConversationMeta[]
 *   conversationDelete(id)  → void
 *
 * Privacy config is read/written via localStorage (no bridge method exists yet).
 * confirm() is spied on per test to control dialog outcome.
 */
import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/preact';
import { afterEach, describe, expect, it, vi } from 'vitest';
import type { ConversationMeta } from '../../../bridge/types';

// ── Mock bridge ──────────────────────────────────────────────────────────────
const mockBridge = {
  conversationList: vi.fn<() => Promise<ConversationMeta[]>>(),
  conversationDelete: vi.fn<(id: string) => Promise<void>>(),
};

vi.mock('../../../hooks/use-bridge', () => ({
  useBridge: () => mockBridge,
}));

import { PrivacyScreen } from '../privacy-screen';

// ── Helpers ──────────────────────────────────────────────────────────────────

function makeConversation(id: string): ConversationMeta {
  return {
    id,
    title: `Conversation ${id}`,
    createdAt: new Date().toISOString(),
    updatedAt: new Date().toISOString(),
  };
}

function setupDefaultBridge(conversations: ConversationMeta[] = []) {
  mockBridge.conversationList.mockResolvedValue(conversations);
  mockBridge.conversationDelete.mockResolvedValue(undefined);
}

afterEach(() => {
  cleanup();
  vi.clearAllMocks();
  vi.restoreAllMocks();
  localStorage.clear();
});

// ─── Tests ───────────────────────────────────────────────────────────────────

describe('PrivacyScreen', () => {
  describe('render', () => {
    it('renders the screen with config and conversation count', async () => {
      setupDefaultBridge([makeConversation('c1'), makeConversation('c2')]);

      render(<PrivacyScreen />);

      await waitFor(() => {
        expect(screen.getByTestId('privacy-screen')).toBeTruthy();
      });

      expect(screen.getByText('Privacy')).toBeTruthy();
      expect(screen.getByText('2')).toBeTruthy(); // conversation count stat
      expect(screen.getByTestId('clear-conversations-btn')).toBeTruthy();
      expect(screen.getByTestId('clear-cache-btn')).toBeTruthy();
    });

    it('shows loading state initially', () => {
      mockBridge.conversationList.mockReturnValue(new Promise(() => {}));

      render(<PrivacyScreen />);

      expect(screen.getByTestId('privacy-loading')).toBeTruthy();
    });

    it('shows error state when bridge fails to load', async () => {
      mockBridge.conversationList.mockRejectedValue(new Error('Bridge unavailable'));

      render(<PrivacyScreen />);

      await waitFor(() => {
        expect(screen.getByTestId('privacy-error')).toBeTruthy();
      });

      expect(screen.getByText(/Bridge unavailable/)).toBeTruthy();
    });

    it('renders all three toggle controls', async () => {
      setupDefaultBridge();

      render(<PrivacyScreen />);

      await waitFor(() => screen.getByTestId('privacy-screen'));

      expect(screen.getByRole('switch', { name: /Send conversation history/i })).toBeTruthy();
      expect(screen.getByRole('switch', { name: /Allow anonymous telemetry/i })).toBeTruthy();
      expect(screen.getByRole('switch', { name: /Space memory/i })).toBeTruthy();
    });

    it('shows singular label when there is exactly 1 conversation', async () => {
      setupDefaultBridge([makeConversation('c1')]);

      render(<PrivacyScreen />);

      await waitFor(() => screen.getByTestId('privacy-screen'));

      expect(screen.getByText('saved conversation')).toBeTruthy();
    });
  });

  describe('toggle change', () => {
    it('toggling sendHistory persists to localStorage', async () => {
      setupDefaultBridge();

      render(<PrivacyScreen />);
      await waitFor(() => screen.getByTestId('privacy-screen'));

      const checkbox = screen.getByRole('switch', { name: /Send conversation history/i });
      // Default is true, uncheck it
      fireEvent.change(checkbox, { target: { checked: false } });

      await waitFor(() => {
        const stored = JSON.parse(localStorage.getItem('maho_privacy_config') ?? '{}');
        expect(stored.sendHistory).toBe(false);
      });
    });

    it('toggling allowTelemetry to true persists to localStorage', async () => {
      setupDefaultBridge();

      render(<PrivacyScreen />);
      await waitFor(() => screen.getByTestId('privacy-screen'));

      const checkbox = screen.getByRole('switch', { name: /Allow anonymous telemetry/i });
      // Default is false, check it
      fireEvent.change(checkbox, { target: { checked: true } });

      await waitFor(() => {
        const stored = JSON.parse(localStorage.getItem('maho_privacy_config') ?? '{}');
        expect(stored.allowTelemetry).toBe(true);
      });
    });

    it('reverts toggle UI on save failure and shows error', async () => {
      // localStorage.setItem throws to simulate write failure
      const setItemSpy = vi.spyOn(Storage.prototype, 'setItem').mockImplementationOnce(() => {
        throw new Error('Storage quota exceeded');
      });
      setupDefaultBridge();

      render(<PrivacyScreen />);
      await waitFor(() => screen.getByTestId('privacy-screen'));

      const checkbox = screen.getByRole('switch', { name: /Space memory/i }) as HTMLInputElement;
      const originalChecked = checkbox.checked;

      fireEvent.change(checkbox, { target: { checked: !originalChecked } });

      await waitFor(() => {
        expect(screen.getByTestId('privacy-error-banner')).toBeTruthy();
      });

      // Toggle must have reverted to original state
      expect(checkbox.checked).toBe(originalChecked);

      setItemSpy.mockRestore();
    });
  });

  describe('clear all conversations', () => {
    it('confirms then deletes all conversations via loop', async () => {
      const convos = [makeConversation('a'), makeConversation('b'), makeConversation('c')];
      setupDefaultBridge(convos);

      // First call returns full list (used in clearAllConversations); but wait —
      // conversationList is also called during load, so we need both calls to resolve:
      mockBridge.conversationList
        .mockResolvedValueOnce(convos) // load call
        .mockResolvedValueOnce(convos); // clear call

      vi.spyOn(window, 'confirm').mockReturnValue(true);

      render(<PrivacyScreen />);
      await waitFor(() => screen.getByTestId('privacy-screen'));

      fireEvent.click(screen.getByTestId('clear-conversations-btn'));

      await waitFor(() => {
        expect(mockBridge.conversationDelete).toHaveBeenCalledTimes(3);
        expect(mockBridge.conversationDelete).toHaveBeenCalledWith('a');
        expect(mockBridge.conversationDelete).toHaveBeenCalledWith('b');
        expect(mockBridge.conversationDelete).toHaveBeenCalledWith('c');
      });

      // Count resets to 0
      await waitFor(() => {
        expect(screen.getByTestId('conversation-count').textContent).toBe('0');
      });

      // Status banner appears
      expect(screen.getByTestId('privacy-status-banner')).toBeTruthy();
    });

    it('does NOT delete when confirm dialog is canceled', async () => {
      const convos = [makeConversation('x'), makeConversation('y')];
      setupDefaultBridge(convos);

      vi.spyOn(window, 'confirm').mockReturnValue(false);

      render(<PrivacyScreen />);
      await waitFor(() => screen.getByTestId('privacy-screen'));

      fireEvent.click(screen.getByTestId('clear-conversations-btn'));

      // Yield to allow any async work that should NOT happen
      await new Promise((r) => setTimeout(r, 50));

      expect(mockBridge.conversationDelete).not.toHaveBeenCalled();
      // Count stays at 2
      expect(screen.getByTestId('conversation-count').textContent).toBe('2');
    });

    it('shows error banner when delete fails', async () => {
      const convos = [makeConversation('err1')];
      mockBridge.conversationList
        .mockResolvedValueOnce(convos) // load
        .mockResolvedValueOnce(convos); // clear
      mockBridge.conversationDelete.mockRejectedValue(new Error('Delete failed'));

      vi.spyOn(window, 'confirm').mockReturnValue(true);

      render(<PrivacyScreen />);
      await waitFor(() => screen.getByTestId('privacy-screen'));

      fireEvent.click(screen.getByTestId('clear-conversations-btn'));

      await waitFor(() => {
        expect(screen.getByTestId('privacy-error-banner')).toBeTruthy();
      });

      expect(screen.getByText(/Delete failed/)).toBeTruthy();
    });
  });

  describe('clear cached AI responses', () => {
    it('confirms then clears cache and shows status banner', async () => {
      setupDefaultBridge();
      vi.spyOn(window, 'confirm').mockReturnValue(true);

      render(<PrivacyScreen />);
      await waitFor(() => screen.getByTestId('privacy-screen'));

      fireEvent.click(screen.getByTestId('clear-cache-btn'));

      await waitFor(() => {
        expect(screen.getByTestId('privacy-status-banner')).toBeTruthy();
      });
    });

    it('does NOT clear cache when confirm is canceled', async () => {
      setupDefaultBridge();
      vi.spyOn(window, 'confirm').mockReturnValue(false);

      const sessionClearSpy = vi.spyOn(Storage.prototype, 'clear');

      render(<PrivacyScreen />);
      await waitFor(() => screen.getByTestId('privacy-screen'));

      fireEvent.click(screen.getByTestId('clear-cache-btn'));

      await new Promise((r) => setTimeout(r, 50));

      // sessionStorage.clear should not have been called
      expect(sessionClearSpy).not.toHaveBeenCalled();
      expect(screen.queryByTestId('privacy-status-banner')).toBeNull();
    });
  });

  describe('error display', () => {
    it('dismisses the error banner on ×', async () => {
      // Force a clear error by making delete fail
      const convos = [makeConversation('d1')];
      mockBridge.conversationList
        .mockResolvedValueOnce(convos)
        .mockResolvedValueOnce(convos);
      mockBridge.conversationDelete.mockRejectedValue(new Error('Network error'));

      vi.spyOn(window, 'confirm').mockReturnValue(true);

      render(<PrivacyScreen />);
      await waitFor(() => screen.getByTestId('privacy-screen'));

      fireEvent.click(screen.getByTestId('clear-conversations-btn'));
      await waitFor(() => screen.getByTestId('privacy-error-banner'));

      fireEvent.click(screen.getByRole('button', { name: /Dismiss error/i }));

      await waitFor(() => {
        expect(screen.queryByTestId('privacy-error-banner')).toBeNull();
      });
    });
  });

  describe('back navigation', () => {
    it('calls onBack prop when back button is pressed', async () => {
      setupDefaultBridge();
      const onBack = vi.fn();

      render(<PrivacyScreen onBack={onBack} />);
      await waitFor(() => screen.getByTestId('privacy-screen'));

      fireEvent.click(screen.getByRole('button', { name: /Back/i }));

      expect(onBack).toHaveBeenCalledTimes(1);
    });
  });
});
