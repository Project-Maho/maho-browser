import { act, cleanup, fireEvent, render, renderHook, screen } from '@testing-library/preact';
import { afterEach, expect, it, vi } from 'vitest';
import { AgentScreen } from '../src/screens/agent/agent-screen';
import { ChatScreen } from '../src/screens/chat/chat-screen';
import { ConversationListScreen } from '../src/screens/conversations/conversation-list-screen';
import { useAgentSession, wakeAgentSession } from '../src/hooks/use-agent-session';
import { chatStoreReducer, initialChatStoreState } from '../src/store/chatStore';
import { deferred, domSignal, reviewBridge } from './review-fixture';

afterEach(() => { cleanup(); vi.useRealTimers(); });

async function submit(text = 'task') {
  fireEvent.input(screen.getByRole('textbox'), { target: { value: text } });
  await act(async () => { fireEvent.click(screen.getByRole('button', { name: 'Send message' })); });
}

it('P1 retries session creation after rejection', async () => {
  const create = vi.fn().mockRejectedValueOnce(new Error('create failed')).mockResolvedValue('retry-handle');
  const ctx = reviewBridge({ agentCreateSession: create });
  render(<AgentScreen bridge={ctx.bridge} />);
  const failed = domSignal(() => screen.queryByTestId('agent-error-banner') !== null);
  await submit();
  await failed;
  await submit();
  expect(create).toHaveBeenCalledTimes(2);
  await act(async () => { await ctx.sent; });
  expect(ctx.methods.agentSendMessage).toHaveBeenCalledWith('retry-handle', 'task');
});

it('P1 does not share a cancelled in-flight creation with a retry', async () => {
  const first = deferred<string>();
  const create = vi.fn().mockReturnValueOnce(first.promise).mockResolvedValue('retry-handle');
  const ctx = reviewBridge({ agentCreateSession: create });
  render(<AgentScreen bridge={ctx.bridge} />);
  await submit();
  await act(async () => { fireEvent.click(screen.getByRole('button', { name: 'Stop current run' })); });
  await submit('retry');
  await act(async () => { first.resolve('cancelled-handle'); });
  expect(create).toHaveBeenCalledTimes(2);
  await act(async () => { await Promise.all([ctx.sent, ctx.freed]); });
  expect(ctx.methods.agentSendMessage).toHaveBeenCalledWith('retry-handle', 'retry');
  expect(ctx.methods.agentSendMessage).not.toHaveBeenCalledWith('cancelled-handle', expect.anything());
  expect(ctx.methods.agentFreeSession).toHaveBeenCalledExactlyOnceWith('cancelled-handle');
});

it('P1 Stop during runtime configuration prevents sending the cancelled task', async () => {
  const config = deferred<boolean>();
  const started = deferred<void>();
  const ctx = reviewBridge({ agentSetRuntimeConfig: () => { started.resolve(); return config.promise; } });
  render(<AgentScreen bridge={ctx.bridge} />);
  await act(async () => {
    fireEvent.click(screen.getAllByRole('button', { name: /Start suggested task/ })[0]!);
    await started.promise;
  });
  await act(async () => { fireEvent.click(screen.getByRole('button', { name: 'Stop current run' })); });
  await act(async () => { config.resolve(true); });
  expect(ctx.methods.agentSendMessage).not.toHaveBeenCalled();
  await submit('uncancelled');
  expect(ctx.methods.agentSendMessage).toHaveBeenCalledWith('agent-1', 'uncancelled');
});

it.each(['resolve', 'reject'] as const)('P1 ignores a late poll %s after replacing the handle', async (outcome) => {
  const poll = deferred<string | null>();
  const ctx = reviewBridge({ agentPollEvent: () => poll.promise });
  const onEvent = vi.fn();
  const onDone = vi.fn();
  const view = renderHook(({ handle }) => useAgentSession({ bridge: ctx.bridge, handle, onEvent, onDone }), { initialProps: { handle: 'old' } });
  act(() => wakeAgentSession('old'));
  view.rerender({ handle: 'new' });
  await act(async () => {
    if (outcome === 'resolve') poll.resolve(JSON.stringify({ type: 'complete', data: 'old completion' }));
    else poll.reject(new Error('old failure'));
  });
  expect(onEvent).not.toHaveBeenCalled();
  expect(onDone).not.toHaveBeenCalled();
});

it('P1 poll rejection ends visible running state and a subsequent wake recovers delivery', async () => {
  const poll = vi.fn().mockRejectedValueOnce(new Error('poll failed')).mockResolvedValue(null);
  const ctx = reviewBridge({ agentPollEvent: poll });
  render(<AgentScreen bridge={ctx.bridge} />);
  await submit();
  await act(async () => { await ctx.sent; });
  const failed = domSignal(() => screen.queryByTestId('agent-error-banner') !== null);
  await act(async () => { wakeAgentSession('agent-1'); });
  await failed;
  expect(screen.queryByTestId('agent-error-banner')).not.toBeNull();
  expect(screen.queryByRole('button', { name: 'Stop current run' })).toBeNull();
  poll.mockResolvedValueOnce(JSON.stringify({ type: 'complete', data: 'recovered result' }));
  await act(async () => { wakeAgentSession('agent-1'); });
  expect(screen.getByText('recovered result')).toBeInTheDocument();
});

it('P1 provider lookup rejection exits chat loading with a visible recoverable error', async () => {
  const lookup = deferred<never>();
  const ctx = reviewBridge({ getAiSettings: () => lookup.promise });
  render(<ChatScreen bridge={ctx.bridge} />);
  const failed = domSignal(() => screen.queryByRole('alert') !== null);
  await act(async () => { lookup.reject(new Error('settings unavailable')); });
  await failed;
  expect(screen.queryByRole('alert')).not.toBeNull();
  expect(screen.getByRole('button', { name: '+ New' })).not.toBeDisabled();
});

it('P1 changing conversation identity clears the previous in-memory draft', () => {
  const a = { ...initialChatStoreState(), booting: false, conversationId: 'A', draft: 'private draft A' };
  const loadingB = chatStoreReducer(a, { type: 'BOOT_START' });
  const b = chatStoreReducer(loadingB, { type: 'BOOT_SUCCESS', conversationId: 'B', conversationPersisted: true, messages: [], provider: { id: 'maho-managed', label: 'Maho', credentialProvider: 'maho-managed' }, sessionHandle: 'handle-B' });
  expect(b.draft).toBe('');
});

it.each(['false', 'reject'] as const)('P1 failed rename (%s) restores Save and Cancel controls', async (failure) => {
  const rename = deferred<boolean>();
  const ctx = reviewBridge({ conversationRename: () => rename.promise });
  const loaded = domSignal(() => screen.queryByRole('button', { name: 'Conversation actions for Original' }) !== null);
  render(<ConversationListScreen bridge={ctx.bridge} />);
  await loaded;
  fireEvent.click(screen.getByRole('button', { name: 'Conversation actions for Original' }));
  fireEvent.click(screen.getByRole('menuitem', { name: 'Rename' }));
  fireEvent.input(screen.getByRole('textbox'), { target: { value: 'New title' } });
  fireEvent.click(screen.getByRole('button', { name: 'Save title' }));
  await act(async () => {
    if (failure === 'false') rename.resolve(false);
    else rename.reject(new Error('rename failed'));
  });
  expect(screen.getByRole('button', { name: 'Save title' })).not.toBeDisabled();
  expect(screen.getByRole('button', { name: 'Cancel rename' })).not.toBeDisabled();
  expect(screen.getByRole('textbox')).toHaveValue('New title');
});

it('P1 answers an open-ended question using its request ID and text wire envelope', async () => {
  const resolve = vi.fn(async () => true);
  const poll = vi.fn().mockResolvedValueOnce(JSON.stringify({ type: 'interaction_request', data: { id: 'q-1', kind: 'question', question: 'Destination?', options: [] } })).mockResolvedValue(null);
  const ctx = reviewBridge({ agentPollEvent: poll, agentResolveInteraction: resolve });
  render(<AgentScreen bridge={ctx.bridge} />);
  await submit();
  await act(async () => { await ctx.sent; });
  const question = domSignal(() => screen.queryByTestId('agent-approval-card') !== null);
  await act(async () => { wakeAgentSession('agent-1'); });
  await question;
  const answer = screen.getByRole('textbox', { name: 'Answer' });
  fireEvent.input(answer, { target: { value: 'Seoul' } });
  await act(async () => { fireEvent.click(screen.getByRole('button', { name: 'Submit answer' })); });
  expect(resolve).toHaveBeenCalledOnce();
  const [handle, requestId, json] = resolve.mock.calls[0] as unknown as [string, string, string];
  expect([handle, requestId, JSON.parse(json)]).toEqual(['agent-1', 'q-1', { answer_kind: 'text', '0': 'Seoul' }]);
  expect(answer).toBeDisabled();
});
