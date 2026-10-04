import { act, cleanup, fireEvent, render, renderHook, screen } from '@testing-library/preact';
import { afterEach, describe, expect, it, vi } from 'vitest';
import { AgentScreen } from '../src/screens/agent/agent-screen';
import { ChatScreen } from '../src/screens/chat/chat-screen';
import { useHashRoute } from '../src/hooks/use-hash-route';
import { deferred, domSignal, reviewBridge } from './review-fixture';

afterEach(cleanup);

async function type(text: string) {
  await act(async () => { fireEvent.input(screen.getByRole('textbox'), { target: { value: text } }); });
}

describe('P0 draft acknowledgement', () => {
  it.each(['agent', 'chat'] as const)('retains a newer %s draft in storage after successful send', async (kind) => {
    const send = deferred<boolean>();
    const started = deferred<void>();
    const sendChat = vi.fn(async () => { started.resolve(); await send.promise; });
    const ctx = reviewBridge({
      agentSendMessage: () => { started.resolve(); return send.promise; },
      chatSendMessage: sendChat,
    });
    const ready = domSignal(() => {
      const input = screen.queryByRole('textbox') as HTMLTextAreaElement | null;
      return input !== null && !input.disabled;
    });
    const view = render(kind === 'agent' ? <AgentScreen bridge={ctx.bridge} /> : <ChatScreen bridge={ctx.bridge} sessionId="conv-1" />);
    await ready;
    await type('A');
    await act(async () => { fireEvent.click(screen.getByRole('button', { name: 'Send message' })); await started.promise; });
    expect(screen.getByRole('textbox')).toHaveValue('');
    const persisted = deferred<void>();
    const set = ctx.bridge.composerDraftSet!;
    ctx.bridge.composerDraftSet = async (scope, text) => {
      const result = await set(scope, text);
      if (text === 'B') persisted.resolve();
      return result;
    };
    await type('B');
    // Persist B before A's acknowledgement, so this isolates the send-clear bug
    // from the separate unmount/debounce defect.
    await act(async () => { fireEvent.blur(screen.getByRole('textbox')); await persisted.promise; });
    const expectedKey = kind === 'agent' ? 'new_task' : 'conversation:conv-1';
    expect(ctx.saved.get(expectedKey)?.text).toBe('B');
    await act(async () => {
      send.resolve(true);
      await (kind === 'chat' ? sendChat.mock.results[0]!.value : send.promise);
    });
    view.unmount();
    const scope = kind === 'agent' ? { kind: 'new_task' as const } : { kind: 'conversation' as const, conversationId: 'conv-1' };
    expect((await ctx.bridge.composerDraftGet!(scope))?.text).toBe('B');
  });
});

it('P0 flushes the last edit on SPA unmount without blur or hidden event', async () => {
  const ctx = reviewBridge();
  let view!: ReturnType<typeof render>;
  await act(async () => { view = render(<AgentScreen bridge={ctx.bridge} />); });
  await type('last unsent edit');
  await act(async () => { view.unmount(); });
  expect((await ctx.bridge.composerDraftGet!({ kind: 'new_task' }))?.text).toBe('last unsent edit');
});

it('P0 renders a malformed percent-encoded hash without throwing', () => {
  window.history.replaceState(null, '', '#agent?goal=50%&valid=hello%20world');
  const { result } = renderHook(() => useHashRoute());
  expect(result.current).toEqual({ name: 'agent', params: { goal: '50%', valid: 'hello world' } });
});
