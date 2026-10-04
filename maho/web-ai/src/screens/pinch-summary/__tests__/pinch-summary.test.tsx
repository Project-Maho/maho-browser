import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/preact';
import { act } from 'preact/test-utils';
import { afterEach, describe, expect, it, vi } from 'vitest';
import type { CostEstimate, PinchSummaryEvent } from '../../../bridge';

interface TestWindow extends Window {
  __mahoCurrentPageText?: string;
}

interface SubscriptionHandlers {
  onPinchState?: (event: PinchSummaryEvent) => void;
}

const subscriptions = new Map<string, SubscriptionHandlers>();

const mockBridge = {
  pinchEstimateCost: vi.fn<(pageContent: string, model: string) => Promise<CostEstimate>>(),
};

vi.mock('../../../hooks/use-bridge', () => ({
  useBridge: () => mockBridge,
}));

vi.mock('../../../bridge/streaming', () => ({
  subscribeStream: (streamId: string, handlers: SubscriptionHandlers) => {
    subscriptions.set(streamId, handlers);
    return () => {
      subscriptions.delete(streamId);
    };
  },
}));

import { PinchSummaryScreen } from '../pinch-summary-screen';

function renderScreen(hash = '#pinch-summary?tabId=tab-1', pageText = 'Page body for costing.') {
  window.location.hash = hash;
  (window as TestWindow).__mahoCurrentPageText = pageText;
  return render(<PinchSummaryScreen />);
}

function emitPinch(tabId: string, event: PinchSummaryEvent): void {
  act(() => {
    subscriptions.get(tabId)?.onPinchState?.(event);
  });
}

function loadExtractiveSummary(tabId = 'tab-1'): void {
  emitPinch(tabId, {
    kind: 'extractive_default',
    tabId,
    sentences: ['Offline bullet one', 'Offline bullet two'],
  });
}

afterEach(() => {
  cleanup();
  subscriptions.clear();
  vi.clearAllMocks();
  window.location.hash = '';
  delete (window as TestWindow).__mahoCurrentPageText;
});

describe('PinchSummaryScreen', () => {
  it('renders extractive bullets when the native pinch stream reports extractive_default', async () => {
    renderScreen();

    loadExtractiveSummary();

    expect(await screen.findByText('Offline bullet one')).toBeInTheDocument();
    expect(screen.getByText('Offline bullet two')).toBeInTheDocument();
    expect(screen.getByRole('button', { name: 'Enhance with AI' })).toBeInTheDocument();
  });

  it('estimates AI cost and shows the confirmation dialog', async () => {
    mockBridge.pinchEstimateCost.mockResolvedValue({
      inputTokens: 240,
      outputTokens: 64,
      estimatedCostUsd: 0.0034,
      model: 'default',
    });

    renderScreen();
    loadExtractiveSummary();

    fireEvent.click(screen.getByRole('button', { name: 'Enhance with AI' }));

    await waitFor(() => {
      expect(mockBridge.pinchEstimateCost).toHaveBeenCalledWith('Page body for costing.', 'default');
    });

    expect(screen.getByRole('dialog')).toBeInTheDocument();
    expect(screen.getByText('Estimated cost:')).toBeInTheDocument();
    expect(screen.getByText('240 input + 64 output tokens · default')).toBeInTheDocument();
  });

  it('returns to the extractive stage when AI enhancement is declined', async () => {
    mockBridge.pinchEstimateCost.mockResolvedValue({
      inputTokens: 120,
      outputTokens: 32,
      estimatedCostUsd: 0.0018,
      model: 'default',
    });

    renderScreen();
    loadExtractiveSummary();

    fireEvent.click(screen.getByRole('button', { name: 'Enhance with AI' }));

    await screen.findByRole('dialog');
    fireEvent.click(screen.getByRole('button', { name: 'Use offline summary' }));

    await waitFor(() => {
      expect(screen.queryByRole('dialog')).not.toBeInTheDocument();
    });

    expect(screen.getByText('Offline bullet one')).toBeInTheDocument();
    expect(screen.getByRole('button', { name: 'Enhance with AI' })).toBeInTheDocument();
  });

  it('moves from pending to streaming to success after AI enhancement is accepted', async () => {
    mockBridge.pinchEstimateCost.mockResolvedValue({
      inputTokens: 300,
      outputTokens: 90,
      estimatedCostUsd: 0.0032,
      model: 'default',
    });

    const view = renderScreen();
    loadExtractiveSummary();

    fireEvent.click(screen.getByRole('button', { name: 'Enhance with AI' }));
    await screen.findByRole('dialog');

    fireEvent.click(screen.getByRole('button', { name: /Use AI enhancement/ }));

    expect(screen.getByText('Waiting for native AI enhancement to begin…')).toBeInTheDocument();

    emitPinch('tab-1', {
      kind: 'llm_streaming',
      tabId: 'tab-1',
      sentences: ['AI draft bullet', 'Second streaming bullet'],
    });

    expect(screen.getByText('AI draft bullet')).toBeInTheDocument();
    expect(view.container.querySelectorAll('.ps-bullet-streaming')).toHaveLength(2);

    emitPinch('tab-1', {
      kind: 'llm_success',
      tabId: 'tab-1',
      sentences: ['AI final bullet', 'Second final bullet'],
    });

    await waitFor(() => {
      expect(screen.getByText('AI final bullet')).toBeInTheDocument();
    });

    expect(screen.getByText('AI enhanced')).toBeInTheDocument();
  });

  it('falls back to the extractive summary when the AI enhancement fails', async () => {
    mockBridge.pinchEstimateCost.mockResolvedValue({
      inputTokens: 180,
      outputTokens: 48,
      estimatedCostUsd: 0.0021,
      model: 'default',
    });

    renderScreen();
    loadExtractiveSummary();

    fireEvent.click(screen.getByRole('button', { name: 'Enhance with AI' }));
    await screen.findByRole('dialog');
    fireEvent.click(screen.getByRole('button', { name: /Use AI enhancement/ }));

    emitPinch('tab-1', {
      kind: 'llm_streaming',
      tabId: 'tab-1',
      sentences: ['AI partial bullet'],
    });

    emitPinch('tab-1', {
      kind: 'llm_failed',
      tabId: 'tab-1',
      sentences: [],
      reason: 'Provider timeout',
    });

    await waitFor(() => {
      expect(screen.getByText('Offline bullet one')).toBeInTheDocument();
    });

    expect(screen.queryByText('AI partial bullet')).not.toBeInTheDocument();
    expect(screen.getByRole('alert')).toHaveTextContent('Provider timeout');
    expect(screen.queryByText('Waiting for native AI enhancement to begin…')).not.toBeInTheDocument();
  });

  it('renders an error state when tabId is missing', () => {
    renderScreen('#pinch-summary');

    expect(screen.getByText('Missing tabId')).toBeInTheDocument();
    expect(screen.getByText('No tabId was provided for this pinch summary route.')).toBeInTheDocument();
    expect(subscriptions.size).toBe(0);
  });
});
