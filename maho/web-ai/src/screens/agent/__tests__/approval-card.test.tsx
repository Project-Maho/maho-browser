import { act, cleanup, fireEvent, render, screen, waitFor } from '@testing-library/preact';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import type { MahoBridge } from '../../../bridge/types';
import { agentReducer, initialAgentState } from '../agent-reducer';
import { AgentScreen } from '../agent-screen';

/**
 * Focused harness for the interaction_request approval-card flow. The kernel
 * live wire shape is `{ request_id, kind, args: <json string> }` (FFI kind 8);
 * the hook normalizes it before the reducer sees an INTERACTION_REQUEST action.
 */
function makeApprovalHarness(overrides: Partial<MahoBridge> = {}) {
  let eventQueue: string[] = [];
  const agentResolveInteraction = vi.fn<(handle: string, requestId: string, answerJson: string) => Promise<boolean>>()
    .mockResolvedValue(true);
  const bridge = {
    agentCreateSession: vi.fn<MahoBridge['agentCreateSession']>().mockResolvedValue('handle-1'),
    agentSendMessage: vi.fn<MahoBridge['agentSendMessage']>().mockResolvedValue(true),
    agentCancel: vi.fn<MahoBridge['agentCancel']>().mockResolvedValue(true),
    agentFreeSession: vi.fn<MahoBridge['agentFreeSession']>().mockResolvedValue(undefined),
    agentPollEvent: vi.fn<MahoBridge['agentPollEvent']>().mockImplementation(async () => eventQueue.shift() ?? null),
    agentResolveInteraction,
    artifactShare: vi.fn<MahoBridge['artifactShare']>().mockResolvedValue(true),
    ...overrides,
  } as unknown as MahoBridge;

  return {
    agentResolveInteraction,
    bridge,
    enqueueEvent: (type: string, data: unknown) => {
      eventQueue.push(JSON.stringify({ type, data }));
    },
    clearQueue: () => {
      eventQueue = [];
    },
  };
}

async function startRun(harness: ReturnType<typeof makeApprovalHarness>) {
  render(<AgentScreen bridge={harness.bridge} />);
  fireEvent.input(screen.getByRole('textbox', { name: 'Task description' }), { target: { value: 'Go' } });
  fireEvent.click(screen.getByRole('button', { name: 'Send message' }));
  await waitFor(() => expect(harness.bridge.agentSendMessage).toHaveBeenCalledWith('handle-1', 'Go'));
}

describe('agentReducer interaction_request blocks', () => {
  it('appends an approval-card block and keeps the waiting phase untouched', () => {
    let state = initialAgentState();
    state = agentReducer(state, {
      type: 'SUBMIT_PROMPT',
      prompt: 'Book the table',
      messageId: 'user-1',
      turnId: 'turn-1',
      createdAt: new Date(),
    });
    state = agentReducer(state, { type: 'SESSION_READY', handle: 'handle-1' });
    expect(state.phase).toBe('streaming');

    state = agentReducer(state, {
      type: 'INTERACTION_REQUEST',
      requestId: 'interaction-req-7',
      interactionKind: 'confirmation',
      question: 'Submit the booking form on example.com',
      options: [],
      artifactRef: 'draft-preview-1',
      initialState: null,
      messageId: 'assistant-1',
      createdAt: new Date(),
    });

    expect(state.phase).toBe('streaming');
    const message = state.messages[state.messages.length - 1];
    if (message.role !== 'assistant') throw new Error('Expected assistant message');
    const block = message.blocks.find((entry) => entry.kind === 'interaction_request');
    expect(block).toMatchObject({
      kind: 'interaction_request',
      id: 'interaction-req-7',
      interactionKind: 'confirmation',
      question: 'Submit the booking form on example.com',
      artifactRef: 'draft-preview-1',
      status: 'pending',
    });
  });

  it('marks the card resolved with the chosen answer label', () => {
    let state = initialAgentState();
    state = agentReducer(state, {
      type: 'INTERACTION_REQUEST',
      requestId: 'interaction-req-7',
      interactionKind: 'question',
      question: 'Which draft?',
      options: [{ id: 'opt-a', label: 'Version A' }],
      artifactRef: null,
      initialState: null,
      messageId: 'assistant-1',
      createdAt: new Date(),
    });

    state = agentReducer(state, { type: 'INTERACTION_RESOLVE', requestId: 'interaction-req-7', answerLabel: 'Version A' });

    const message = state.messages[state.messages.length - 1];
    if (message.role !== 'assistant') throw new Error('Expected assistant message');
    const block = message.blocks.find((entry) => entry.kind === 'interaction_request');
    expect(block).toMatchObject({ id: 'interaction-req-7', status: 'resolved', answerLabel: 'Version A' });
  });
});

describe('AgentScreen approval cards (interaction_request events)', () => {
  let harness = makeApprovalHarness();

  beforeEach(() => {
    harness = makeApprovalHarness();
  });

  afterEach(() => {
    cleanup();
    vi.clearAllMocks();
    harness.clearQueue();
  });

  it('renders the approval card with question text and options from the event payload', async () => {
    await startRun(harness);

    act(() => harness.enqueueEvent('interaction_request', {
      request_id: 'interaction-req-7',
      kind: 'question',
      args: JSON.stringify({
        question: 'Which draft should I send?',
        options: [
          { id: 'opt-a', label: 'Version A', description: 'The short summary' },
          { id: 'opt-b', label: 'Version B' },
        ],
      }),
    }));

    const card = await screen.findByTestId('agent-approval-card');
    expect(card).toHaveTextContent('Which draft should I send?');
    const options = screen.getAllByTestId('agent-approval-option');
    expect(options).toHaveLength(2);
    expect(options[0]).toHaveTextContent('Version A');
    expect(options[0]).toHaveTextContent('The short summary');
    expect(options[1]).toHaveTextContent('Version B');
    expect(options[0]).toBeEnabled();
    expect(options[1]).toBeEnabled();
  });

  it('sends the confirm answer through the bridge and resolves the card', async () => {
    await startRun(harness);

    act(() => harness.enqueueEvent('interaction_request', {
      request_id: 'interaction-req-9',
      kind: 'confirmation',
      args: JSON.stringify({ effect_description: 'Submit the payment form on example.com' }),
      artifact_ref: 'draft-preview-42',
    }));

    const card = await screen.findByTestId('agent-approval-card');
    expect(card).toHaveTextContent('Submit the payment form on example.com');
    const artifact = screen.getByTestId('agent-approval-artifact');
    expect(artifact).toHaveTextContent('Review artifact');
    expect(artifact).toHaveTextContent('draft-preview-42');

    fireEvent.click(screen.getByRole('button', { name: 'Confirm' }));

    await waitFor(() =>
      expect(harness.agentResolveInteraction).toHaveBeenCalledWith('handle-1', 'interaction-req-9', '{"answer_kind":"confirmed"}'),
    );
    await waitFor(() => expect(screen.getByTestId('agent-approval-status')).toHaveTextContent('Confirmed'));
  });

  it('sends the deny answer and the selected-option answer through the bridge', async () => {
    await startRun(harness);

    act(() => harness.enqueueEvent('interaction_request', {
      request_id: 'interaction-req-11',
      kind: 'confirmation',
      args: JSON.stringify({ effect_description: 'Submit the payment form on example.com' }),
    }));

    await screen.findByTestId('agent-approval-card');
    fireEvent.click(screen.getByRole('button', { name: 'Deny' }));
    await waitFor(() =>
      expect(harness.agentResolveInteraction).toHaveBeenCalledWith('handle-1', 'interaction-req-11', '{"answer_kind":"denied"}'),
    );

    act(() => harness.enqueueEvent('interaction_request', {
      request_id: 'interaction-req-12',
      kind: 'question',
      args: JSON.stringify({
        question: 'Which account?',
        options: [{ id: 'opt-work', label: 'Work account' }],
      }),
    }));

    await screen.findByText('Which account?');
    fireEvent.click(screen.getByRole('button', { name: 'Work account' }));
    await waitFor(() =>
      expect(harness.agentResolveInteraction).toHaveBeenCalledWith(
        'handle-1',
        'interaction-req-12',
        '{"answer_kind":"selected_option","0":"opt-work"}',
      ),
    );
  });

  it('disables the card when the request arrives with an expired state', async () => {
    await startRun(harness);

    act(() => harness.enqueueEvent('interaction_request', {
      request_id: 'interaction-req-x',
      kind: 'confirmation',
      args: JSON.stringify({ effect_description: 'Delete the old export' }),
      state: 'expired',
    }));

    await screen.findByTestId('agent-approval-card');
    expect(screen.getByTestId('agent-approval-status')).toHaveTextContent('Expired');
    expect(screen.getByRole('button', { name: 'Confirm' })).toBeDisabled();
    expect(screen.getByRole('button', { name: 'Deny' })).toBeDisabled();

    fireEvent.click(screen.getByRole('button', { name: 'Confirm' }));
    expect(harness.agentResolveInteraction).not.toHaveBeenCalled();
  });
});
