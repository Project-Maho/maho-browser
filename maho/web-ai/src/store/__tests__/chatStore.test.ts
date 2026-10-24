import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import type { ChatEvent } from '../../bridge/types';
import {
  StreamCadenceBuffer,
  coalesceChatEventBatch,
  chatStoreReducer,
  createChatStore,
  initialChatStoreState,
  type ChatStoreState,
} from '../chatStore';

describe('StreamCadenceBuffer', () => {
  beforeEach(() => {
    vi.useFakeTimers();
  });

  afterEach(() => {
    vi.restoreAllMocks();
  });

  it('buffers high-frequency streaming tokens and flushes on cadence', () => {
    const onTokenFlush = vi.fn();
    const cadence = new StreamCadenceBuffer({
      frameWindowMs: 16,
      onTokenFlush,
    });

    cadence.pushToken('Hello');
    cadence.pushToken(' ');
    cadence.pushToken('world');
    cadence.pushToken('!');

    expect(onTokenFlush).not.toHaveBeenCalled();
    expect(cadence.hasPending).toBe(true);

    vi.advanceTimersByTime(16);

    expect(onTokenFlush).toHaveBeenCalledTimes(1);
    expect(onTokenFlush).toHaveBeenCalledWith('Hello world!');
    expect(cadence.hasPending).toBe(false);
  });

  it('buffers thinking chunks and flushes on cadence', () => {
    const onThinkingFlush = vi.fn();
    const cadence = new StreamCadenceBuffer({
      frameWindowMs: 16,
      onThinkingFlush,
    });

    cadence.pushThinking('Thinking part 1. ');
    cadence.pushThinking('Thinking part 2.');

    expect(onThinkingFlush).not.toHaveBeenCalled();
    expect(cadence.hasPending).toBe(true);

    vi.advanceTimersByTime(16);

    expect(onThinkingFlush).toHaveBeenCalledTimes(1);
    expect(onThinkingFlush).toHaveBeenCalledWith('Thinking part 1. Thinking part 2.');
    expect(cadence.hasPending).toBe(false);
  });

  it('synchronously flushes immediately when flush() is called', () => {
    const onTokenFlush = vi.fn();
    const cadence = new StreamCadenceBuffer({
      frameWindowMs: 16,
      onTokenFlush,
    });

    cadence.pushToken('Instant token');
    expect(onTokenFlush).not.toHaveBeenCalled();

    cadence.flush();
    expect(onTokenFlush).toHaveBeenCalledTimes(1);
    expect(onTokenFlush).toHaveBeenCalledWith('Instant token');
    expect(cadence.hasPending).toBe(false);

    // Subsequent timer tick should not double-flush
    vi.advanceTimersByTime(32);
    expect(onTokenFlush).toHaveBeenCalledTimes(1);
  });

  it('cancels pending buffers without emitting when cancel() is called', () => {
    const onTokenFlush = vi.fn();
    const cadence = new StreamCadenceBuffer({
      frameWindowMs: 16,
      onTokenFlush,
    });

    cadence.pushToken('Dropped token');
    cadence.cancel();

    expect(cadence.hasPending).toBe(false);
    vi.advanceTimersByTime(32);
    expect(onTokenFlush).not.toHaveBeenCalled();
  });
});

describe('coalesceChatEventBatch', () => {
  it('merges contiguous token events in a batch into a single token event', () => {
    const input: ChatEvent[] = [
      { kind: 'token', token: 'H' },
      { kind: 'token', token: 'e' },
      { kind: 'token', token: 'llo ' },
      { kind: 'token', token: 'world' },
    ];

    const result = coalesceChatEventBatch(input);
    expect(result).toHaveLength(1);
    expect(result[0]).toEqual({ kind: 'token', token: 'Hello world' });
  });

  it('merges contiguous thinking events', () => {
    const input: ChatEvent[] = [
      { kind: 'thinking', thinking: 'Step 1. ' },
      { kind: 'thinking', thinking: 'Step 2.' },
    ];

    const result = coalesceChatEventBatch(input);
    expect(result).toHaveLength(1);
    expect(result[0]).toEqual({ kind: 'thinking', thinking: 'Step 1. Step 2.' });
  });

  it('preserves ordering when interrupted by non-token/thinking events', () => {
    const input: ChatEvent[] = [
      { kind: 'token', token: 'Before tool ' },
      { kind: 'token', token: 'call' },
      { kind: 'tool_call', toolCallId: 't1', name: 'search', args: { query: 'test' } },
      { kind: 'token', token: 'After ' },
      { kind: 'token', token: 'tool' },
      { kind: 'complete', content: 'Final' },
    ];

    const result = coalesceChatEventBatch(input);
    expect(result).toEqual([
      { kind: 'token', token: 'Before tool call' },
      { kind: 'tool_call', toolCallId: 't1', name: 'search', args: { query: 'test' } },
      { kind: 'token', token: 'After tool' },
      { kind: 'complete', content: 'Final' },
    ]);
  });

  it('handles empty or single item arrays cleanly', () => {
    expect(coalesceChatEventBatch([])).toEqual([]);
    expect(coalesceChatEventBatch([{ kind: 'token', token: 'solo' }])).toEqual([
      { kind: 'token', token: 'solo' },
    ]);
  });
});

describe('createChatStore', () => {
  beforeEach(() => {
    vi.useFakeTimers();
  });

  afterEach(() => {
    vi.restoreAllMocks();
  });

  it('initializes with default state and supports subscription updates', () => {
    const store = createChatStore();
    expect(store.getState().booting).toBe(true);
    expect(store.getState().messages).toHaveLength(0);

    const listener = vi.fn();
    const unsub = store.subscribe(listener);

    store.dispatch({
      type: 'BOOT_SUCCESS',
      conversationId: 'c1',
      conversationPersisted: true,
      messages: [],
      provider: { id: 'openai', label: 'OpenAI' },
      sessionHandle: 'h1',
    });

    expect(store.getState().sessionHandle).toBe('h1');
    expect(store.getState().booting).toBe(false);
    expect(listener).toHaveBeenCalledTimes(1);

    unsub();
    store.dispatch({ type: 'SET_DRAFT', value: 'hello' });
    expect(listener).toHaveBeenCalledTimes(1);
    expect(store.getState().draft).toBe('hello');
  });

  it('buffers streaming tokens via pushStreamingToken and flushes on cadence', () => {
    const store = createChatStore({
      booting: false,
      sessionHandle: 'h1',
      messages: [
        { kind: 'chat', id: 'u1', role: 'user', content: { kind: 'text', text: 'Hi' } },
        { kind: 'chat', id: 'a1', role: 'assistant', content: '', images: [], isStreaming: true },
      ],
    });

    store.pushStreamingToken('A');
    store.pushStreamingToken('B');
    store.pushStreamingToken('C');

    // Before timer advances, state has not yet updated
    expect((store.getState().messages[1] as any).content).toBe('');

    vi.advanceTimersByTime(16);

    expect((store.getState().messages[1] as any).content).toBe('ABC');
  });

  it('flushes pending streaming tokens synchronously before processing complete action', () => {
    const store = createChatStore({
      booting: false,
      sessionHandle: 'h1',
      messages: [
        { kind: 'chat', id: 'u1', role: 'user', content: { kind: 'text', text: 'Hi' } },
        { kind: 'chat', id: 'a1', role: 'assistant', content: 'Initial ', images: [], isStreaming: true },
      ],
    });

    store.pushStreamingToken('buffered text');
    // Dispatch complete directly while tokens are buffered
    store.dispatch({ type: 'STREAM_COMPLETE', finalMessage: 'Initial buffered text' });

    expect(store.getState().isStreaming).toBe(false);
    expect((store.getState().messages[1] as any).content).toBe('Initial buffered text');
    expect((store.getState().messages[1] as any).isStreaming).toBe(false);
  });
});

describe('chatStoreReducer turn lifecycle', () => {
  const streaming = (): ChatStoreState => ({
    ...initialChatStoreState(),
    booting: false,
    isStreaming: true,
    messages: [
      { kind: 'chat', id: 'u1', role: 'user', content: { kind: 'text', text: 'hi' } },
      { kind: 'thinking', id: 't1', thinking: 'hmm', isStreaming: true },
      { kind: 'chat', id: 'a1', role: 'assistant', content: 'Part', images: [], isStreaming: true },
    ],
  });

  it('POLL_ERROR surfaces the error without closing the live assistant message', () => {
    const next = chatStoreReducer(streaming(), { type: 'POLL_ERROR', message: 'poll failed' });
    expect(next.errorMessage).toBe('poll failed');
    expect(next.isStreaming).toBe(true);
    const resumed = chatStoreReducer(next, { type: 'STREAM_APPEND', text: ' two' });
    const assistants = resumed.messages.filter((m) => m.kind === 'chat' && m.role === 'assistant');
    expect(assistants).toHaveLength(1);
    expect(assistants[0]).toMatchObject({ content: 'Part two' });
  });

  it('STREAM_COMPLETE after a POLL_ERROR clears the recovered poll banner', () => {
    const failed = chatStoreReducer(streaming(), { type: 'POLL_ERROR', message: 'poll failed' });
    const done = chatStoreReducer(failed, { type: 'STREAM_COMPLETE', finalMessage: 'Part' });
    expect(done.errorMessage).toBeNull();
    expect(done.pollFailed).toBe(false);
  });

  it('POLL_ERROR while idle does not raise a banner', () => {
    const idle = { ...initialChatStoreState(), booting: false };
    expect(chatStoreReducer(idle, { type: 'POLL_ERROR', message: 'poll failed' }).errorMessage).toBeNull();
  });

  it('STREAM_COMPLETE keeps a non-poll error banner', () => {
    const state = { ...streaming(), errorMessage: 'other' };
    expect(chatStoreReducer(state, { type: 'STREAM_COMPLETE', finalMessage: '' }).errorMessage).toBe('other');
  });

  it('TURN_CANCELLED after a failed stop clears the stop error banner', () => {
    const failedStop = chatStoreReducer(streaming(), { type: 'CANCEL_FAILURE', message: "Couldn't stop the response. Try again." });
    expect(failedStop.errorMessage).not.toBeNull();
    const cancelled = chatStoreReducer(failedStop, { type: 'TURN_CANCELLED' });
    expect(cancelled.errorMessage).toBeNull();
    expect(cancelled.isStreaming).toBe(false);
  });

  it.each([
    ['STREAM_COMPLETE', { type: 'STREAM_COMPLETE', finalMessage: '' } as const],
    ['STREAM_ERROR', { type: 'STREAM_ERROR', message: 'x' } as const],
    ['TURN_CANCELLED', { type: 'TURN_CANCELLED' } as const],
  ])('%s closes a streaming thinking item', (_name, action) => {
    const next = chatStoreReducer(streaming(), action);
    expect(next.messages.find((m) => m.kind === 'thinking')).toMatchObject({ isStreaming: false });
  });

  it('TURN_CANCELLED after a POLL_ERROR clears the poll banner', () => {
    const failed = chatStoreReducer(streaming(), { type: 'POLL_ERROR', message: 'poll failed' });
    const cancelled = chatStoreReducer(failed, { type: 'TURN_CANCELLED' });
    expect(cancelled.errorMessage).toBeNull();
    expect(cancelled.pollFailed).toBe(false);
  });

  it.each([
    ['TURN_CANCELLED', { type: 'TURN_CANCELLED' } as const],
    ['STREAM_ERROR', { type: 'STREAM_ERROR', message: 'stream failed' } as const],
    ['SEND_TURN_FAILURE', { type: 'SEND_TURN_FAILURE', message: 'send failed' } as const],
  ])('an idle POLL_ERROR after %s does not raise or replace a banner', (_name, action) => {
    const failed = chatStoreReducer(streaming(), { type: 'POLL_ERROR', message: 'poll failed' });
    const ended = chatStoreReducer(failed, action);
    const idlePoll = chatStoreReducer(ended, { type: 'POLL_ERROR', message: 'poll again' });
    expect(idlePoll.pollFailed).toBe(false);
    expect(idlePoll.errorMessage).toBe(ended.errorMessage);
  });

  it('an idle POLL_ERROR after a re-boot does not raise a banner', () => {
    const failed = chatStoreReducer(streaming(), { type: 'POLL_ERROR', message: 'poll failed' });
    const booting = chatStoreReducer(failed, { type: 'BOOT_START' });
    expect(booting.pollFailed).toBe(false);
    const booted = chatStoreReducer({ ...booting, pollFailed: true }, {
      type: 'BOOT_SUCCESS',
      conversationId: 'c2',
      conversationPersisted: false,
      messages: [],
      provider: null,
      sessionHandle: 'h2',
    } as never);
    expect(booted.pollFailed).toBe(false);
    expect(chatStoreReducer(booted, { type: 'POLL_ERROR', message: 'poll again' }).errorMessage).toBeNull();
  });
});
