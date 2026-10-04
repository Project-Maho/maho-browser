import { cleanup, fireEvent, render, screen, waitFor, act } from '@testing-library/preact';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import type { StreamSubscriber } from '../src/bridge/streaming';

const mockSubscribeStream = vi.fn<(id: string, sub: StreamSubscriber) => () => void>();

vi.mock('../src/bridge/streaming', () => ({
  subscribeStream: (id: string, sub: StreamSubscriber) => mockSubscribeStream(id, sub),
}));

const mockBridge = {
  chatSessionStart: vi.fn<() => Promise<string>>(),
  chatSessionFree: vi.fn<() => Promise<void>>(),
  chatSendMessage: vi.fn<() => Promise<void>>(),
  conversationCreate: vi.fn<() => Promise<string>>(),
  byokGetProviders: vi.fn<() => Promise<string[]>>(),
  byokGetKey: vi.fn<() => Promise<string | null>>(),
  byokSetKey: vi.fn<() => Promise<void>>(),
  byokDeleteKey: vi.fn<() => Promise<void>>(),
  byokValidateKey: vi.fn<() => Promise<{ valid: boolean }>>(),
  chatSessionResume: vi.fn<() => Promise<void>>(),
  chatCancelTurn: vi.fn<() => Promise<void>>(),
  chatPollEvents: vi.fn<() => Promise<[]>>(),
  chatRegisterTool: vi.fn<() => Promise<void>>(),
  chatSendToolResult: vi.fn<() => Promise<void>>(),
  chatAppendAssistantMessage: vi.fn<() => Promise<void>>(),
  chatGetHistory: vi.fn<() => Promise<[]>>(),
  conversationList: vi.fn<() => Promise<[]>>(),
  conversationGet: vi.fn<() => Promise<null>>(),
  conversationDelete: vi.fn<() => Promise<void>>(),
  conversationRename: vi.fn<() => Promise<void>>(),
  conversationGetMessages: vi.fn<() => Promise<[]>>(),
  getSpaceAIConfig: vi.fn<() => Promise<null>>(),
  setSpaceAIConfig: vi.fn<() => Promise<void>>(),
  pinchEstimateCost: vi.fn<() => Promise<{ inputTokens: 0; outputTokens: 0; estimatedCostUsd: 0; model: '' }>>(),
};

vi.mock('../src/hooks/use-bridge', () => ({
  useBridge: () => mockBridge,
}));

import { VisualSearchScreen } from '../src/screens/visual-search/visual-search-screen';

const TEST_IMAGE_PAYLOAD = {
  dataUrl: 'data:image/jpeg;base64,/9j/test',
  mime: 'image/jpeg',
  width: 800,
  height: 600,
};

type TestWindow = typeof window & {
  __mahoVisualSearchImage?: typeof TEST_IMAGE_PAYLOAD;
  __mahoVisualSearchReceive?: (payload: typeof TEST_IMAGE_PAYLOAD) => void;
  MahoBridgeAndroid?: { requestCameraRecapture?: () => void };
  webkit?: { messageHandlers?: { mahoBridge?: { postMessage: (msg: string) => void } } };
};

function getTestWindow(): TestWindow {
  return window as TestWindow;
}

afterEach(() => {
  cleanup();
  vi.clearAllMocks();
  delete getTestWindow().__mahoVisualSearchImage;
  delete getTestWindow().__mahoVisualSearchReceive;
  delete getTestWindow().MahoBridgeAndroid;
  delete getTestWindow().webkit;
});

describe('VisualSearchScreen', () => {
  describe('loading_image stage', () => {
    it('shows waiting message when no image injected', () => {
      render(<VisualSearchScreen onBack={vi.fn()} />);
      expect(screen.getByText('Waiting for camera capture…')).toBeTruthy();
    });

    it('renders header with Back and Retake buttons', () => {
      render(<VisualSearchScreen onBack={vi.fn()} />);
      expect(screen.getByRole('button', { name: 'Back' })).toBeTruthy();
      expect(screen.getByRole('button', { name: 'Retake photo' })).toBeTruthy();
      expect(screen.getByText('Visual Search')).toBeTruthy();
    });
  });

  describe('image received → analyzing', () => {
    beforeEach(() => {
      mockBridge.chatSessionStart.mockResolvedValue('handle-123');
      mockBridge.chatSendMessage.mockResolvedValue(undefined);
      mockBridge.chatSessionFree.mockResolvedValue(undefined);
      mockSubscribeStream.mockImplementation(() => () => {});
    });

    it('shows preview and starts analysis when image pre-injected', async () => {
      getTestWindow().__mahoVisualSearchImage = TEST_IMAGE_PAYLOAD;

      render(<VisualSearchScreen onBack={vi.fn()} />);

      await waitFor(() => {
        expect(screen.getByAltText('Captured frame for visual search')).toBeTruthy();
      });

      expect(mockBridge.chatSessionStart).toHaveBeenCalledTimes(1);
    });

    it('shows preview when image injected via receive callback', async () => {
      mockSubscribeStream.mockImplementation(() => () => {});
      render(<VisualSearchScreen onBack={vi.fn()} />);

      await act(async () => {
        getTestWindow().__mahoVisualSearchReceive?.(TEST_IMAGE_PAYLOAD);
      });

      await waitFor(() => {
        expect(screen.getByAltText('Captured frame for visual search')).toBeTruthy();
      });

      expect(mockBridge.chatSessionStart).toHaveBeenCalledTimes(1);
    });

    it('sends image + text messages to bridge', async () => {
      getTestWindow().__mahoVisualSearchImage = TEST_IMAGE_PAYLOAD;

      render(<VisualSearchScreen onBack={vi.fn()} />);

      await waitFor(() => {
        expect(mockBridge.chatSendMessage).toHaveBeenCalledTimes(2);
      });

      expect(mockBridge.chatSendMessage).toHaveBeenCalledWith(
        'handle-123',
        expect.objectContaining({ kind: 'image', mime: 'image/jpeg' })
      );
      expect(mockBridge.chatSendMessage).toHaveBeenCalledWith(
        'handle-123',
        { kind: 'text', text: 'Analyze this image' }
      );
    });
  });

  describe('streaming tokens', () => {
    it('appends tokens to analysis text', async () => {
      getTestWindow().__mahoVisualSearchImage = TEST_IMAGE_PAYLOAD;
      mockBridge.chatSessionStart.mockResolvedValue('handle-abc');
      mockBridge.chatSendMessage.mockResolvedValue(undefined);

      let capturedSub: StreamSubscriber | null = null;
      mockSubscribeStream.mockImplementation((_id, sub) => {
        capturedSub = sub;
        return () => {};
      });

      render(<VisualSearchScreen onBack={vi.fn()} />);

      await waitFor(() => expect(capturedSub).not.toBeNull());

      await act(async () => {
        capturedSub!.onToken?.('Hello ');
        capturedSub!.onToken?.('world');
      });

      await waitFor(() => {
        expect(screen.getByText(/Hello world/)).toBeTruthy();
      });
    });
  });

  describe('complete stage', () => {
    it('shows action buttons after complete', async () => {
      getTestWindow().__mahoVisualSearchImage = TEST_IMAGE_PAYLOAD;
      mockBridge.chatSessionStart.mockResolvedValue('handle-done');
      mockBridge.chatSendMessage.mockResolvedValue(undefined);

      let capturedSub: StreamSubscriber | null = null;
      mockSubscribeStream.mockImplementation((_id, sub) => {
        capturedSub = sub;
        return () => {};
      });

      render(<VisualSearchScreen onBack={vi.fn()} />);
      await waitFor(() => expect(capturedSub).not.toBeNull());

      await act(async () => {
        capturedSub!.onComplete?.('Final analysis text here');
      });

      await waitFor(() => {
        expect(screen.getByRole('button', { name: 'Ask a follow-up question' })).toBeTruthy();
        expect(screen.getByRole('button', { name: 'Save to conversation' })).toBeTruthy();
        expect(screen.getAllByRole('button', { name: 'Retake photo' })).toHaveLength(2);
      });
    });

    it('follow-up navigates to chat with sessionHandle', async () => {
      getTestWindow().__mahoVisualSearchImage = TEST_IMAGE_PAYLOAD;
      mockBridge.chatSessionStart.mockResolvedValue('handle-fu');
      mockBridge.chatSendMessage.mockResolvedValue(undefined);

      let capturedSub: StreamSubscriber | null = null;
      mockSubscribeStream.mockImplementation((_id, sub) => {
        capturedSub = sub;
        return () => {};
      });

      const onNavigateToChat = vi.fn<(handle: string) => void>();
      render(<VisualSearchScreen onBack={vi.fn()} onNavigateToChat={onNavigateToChat} />);
      await waitFor(() => expect(capturedSub).not.toBeNull());

      await act(async () => {
        capturedSub!.onComplete?.('Done');
      });

      await waitFor(() => {
        expect(screen.getByRole('button', { name: 'Ask a follow-up question' })).toBeTruthy();
      });

      fireEvent.click(screen.getByRole('button', { name: 'Ask a follow-up question' }));
      expect(onNavigateToChat).toHaveBeenCalledWith('handle-fu');
    });

    it('save creates conversation and navigates', async () => {
      getTestWindow().__mahoVisualSearchImage = TEST_IMAGE_PAYLOAD;
      mockBridge.chatSessionStart.mockResolvedValue('handle-save');
      mockBridge.chatSendMessage.mockResolvedValue(undefined);
      mockBridge.conversationCreate.mockResolvedValue('conv-999');

      let capturedSub: StreamSubscriber | null = null;
      mockSubscribeStream.mockImplementation((_id, sub) => {
        capturedSub = sub;
        return () => {};
      });

      const onNavigateToConversation = vi.fn<(id: string) => void>();
      render(
        <VisualSearchScreen
          onBack={vi.fn()}
          onNavigateToConversation={onNavigateToConversation}
        />
      );
      await waitFor(() => expect(capturedSub).not.toBeNull());

      await act(async () => {
        capturedSub!.onComplete?.('Done');
      });

      await waitFor(() => {
        expect(screen.getByRole('button', { name: 'Save to conversation' })).toBeTruthy();
      });

      await act(async () => {
        fireEvent.click(screen.getByRole('button', { name: 'Save to conversation' }));
      });

      await waitFor(() => {
        expect(mockBridge.conversationCreate).toHaveBeenCalledTimes(1);
        expect(onNavigateToConversation).toHaveBeenCalledWith('conv-999');
      });
    });
  });

  describe('retake', () => {
    it('calls Android requestCameraRecapture', async () => {
      const recaptureMock = vi.fn<() => void>();
      getTestWindow().MahoBridgeAndroid = { requestCameraRecapture: recaptureMock };

      render(<VisualSearchScreen onBack={vi.fn()} />);
      fireEvent.click(screen.getByRole('button', { name: 'Retake photo' }));

      expect(recaptureMock).toHaveBeenCalledTimes(1);
    });

    it('calls iOS mahoBridge postMessage for recapture', () => {
      const postMessageMock = vi.fn<(msg: string) => void>();
      getTestWindow().webkit = {
        messageHandlers: { mahoBridge: { postMessage: postMessageMock } },
      };

      render(<VisualSearchScreen onBack={vi.fn()} />);
      fireEvent.click(screen.getByRole('button', { name: 'Retake photo' }));

      expect(postMessageMock).toHaveBeenCalledTimes(1);
      const callArg = postMessageMock.mock.calls[0][0];
      expect(JSON.parse(callArg)).toMatchObject({ method: 'requestCameraRecapture' });
    });
  });

  describe('error stage', () => {
    it('shows error UI when bridge throws', async () => {
      getTestWindow().__mahoVisualSearchImage = TEST_IMAGE_PAYLOAD;
      mockBridge.chatSessionStart.mockRejectedValue(new Error('Network error'));
      mockSubscribeStream.mockImplementation(() => () => {});

      render(<VisualSearchScreen onBack={vi.fn()} />);

      await waitFor(() => {
        expect(screen.getByRole('alert')).toBeTruthy();
        expect(screen.getByText('Network error')).toBeTruthy();
      });
    });

    it('shows error UI when stream emits error', async () => {
      getTestWindow().__mahoVisualSearchImage = TEST_IMAGE_PAYLOAD;
      mockBridge.chatSessionStart.mockResolvedValue('handle-err');
      mockBridge.chatSendMessage.mockResolvedValue(undefined);

      let capturedSub: StreamSubscriber | null = null;
      mockSubscribeStream.mockImplementation((_id, sub) => {
        capturedSub = sub;
        return () => {};
      });

      render(<VisualSearchScreen onBack={vi.fn()} />);
      await waitFor(() => expect(capturedSub).not.toBeNull());

      await act(async () => {
        capturedSub!.onError?.('LLM quota exceeded');
      });

      await waitFor(() => {
        expect(screen.getByRole('alert')).toBeTruthy();
        expect(screen.getByText('LLM quota exceeded')).toBeTruthy();
      });
    });
  });
});
