import { describe, it, expect } from 'vitest';
import { agentReducer, initialAgentState } from '../agent-reducer';
import type { AgentAction, AgentState } from '../agent-types';

const now = new Date();
const run = (state: AgentState, ...actions: AgentAction[]) => actions.reduce(agentReducer, state);
const live = () => run(
  initialAgentState(),
  { type: 'SUBMIT_PROMPT', prompt: 'Go', messageId: 'u1', turnId: 't1', createdAt: now },
  { type: 'SESSION_READY', handle: 'h1' },
);
const approval = (requestId = 'req-1'): AgentAction => ({
  type: 'INTERACTION_REQUEST', requestId, interactionKind: 'confirmation', question: 'Send?',
  options: [], artifactRef: null, initialState: null, handle: 'h1', messageId: 'a1', createdAt: now,
});
const blocksOf = (state: AgentState) => state.messages.flatMap((m) => (m.role === 'assistant' ? [...m.blocks] : []));

describe('agentReducer block-based model', () => {
  it('correctly appends tokens and accumulates them in a single message block', () => {
    let state = initialAgentState();

    state = agentReducer(state, {
      type: 'AGENT_TOKEN',
      token: 'Hello',
      messageId: 'msg-1',
      createdAt: new Date(),
    });

    state = agentReducer(state, {
      type: 'AGENT_TOKEN',
      token: ' World!',
      messageId: 'msg-1',
      createdAt: new Date(),
    });

    expect(state.messages).toHaveLength(1);
    const msg = state.messages[0];
    if (msg.role !== 'assistant') {
      throw new Error('Expected assistant message');
    }
    expect(msg.blocks).toHaveLength(1);
    expect(msg.blocks[0]).toEqual({
      kind: 'message',
      id: expect.any(String),
      text: 'Hello World!',
      markdown: true,
    });
  });

  it('suppresses empty or whitespace-only thinking blocks at the reducer level', () => {
    let state = initialAgentState();

    state = agentReducer(state, {
      type: 'AGENT_THINKING',
      thinking: '   ',
      messageId: 'msg-1',
      createdAt: new Date(),
    });

    state = agentReducer(state, {
      type: 'AGENT_THINKING',
      thinking: '',
      messageId: 'msg-2',
      createdAt: new Date(),
    });

    expect(state.messages).toHaveLength(0);
  });

  it('correctly appends thinking block and accumulates thinking text', () => {
    let state = initialAgentState();

    state = agentReducer(state, {
      type: 'AGENT_THINKING',
      thinking: 'Thinking hard',
      messageId: 'msg-1',
      createdAt: new Date(),
    });

    state = agentReducer(state, {
      type: 'AGENT_THINKING',
      thinking: '...',
      messageId: 'msg-1',
      createdAt: new Date(),
    });

    expect(state.messages).toHaveLength(1);
    const msg = state.messages[0];
    if (msg.role !== 'assistant') {
      throw new Error('Expected assistant message');
    }
    expect(msg.blocks).toHaveLength(1);
    expect(msg.blocks[0]).toEqual({
      kind: 'thinking',
      id: expect.any(String),
      text: 'Thinking hard...',
      startedAt: expect.any(Number),
      durationMs: null,
    });
  });

  it('correctly handles interleaved streams of text and tool calls in chronological order', () => {
    let state = initialAgentState();

    state = agentReducer(state, {
      type: 'AGENT_TOKEN',
      token: 'Executing tool first.',
      messageId: 'msg-1',
      createdAt: new Date(),
    });

    state = agentReducer(state, {
      type: 'AGENT_TOOL_CALL',
      id: 'call-1',
      name: 'calc',
      args: '{"x": 1}',
      messageId: 'msg-1',
      createdAt: new Date(),
    });

    state = agentReducer(state, {
      type: 'AGENT_TOKEN',
      token: 'Tool finished.',
      messageId: 'msg-1',
      createdAt: new Date(),
    });

    expect(state.messages).toHaveLength(1);
    const msg = state.messages[0];
    if (msg.role !== 'assistant') {
      throw new Error('Expected assistant message');
    }
    expect(msg.blocks).toHaveLength(3);

    const block0 = msg.blocks[0];
    if (block0.kind !== 'message') throw new Error('Expected message block');
    expect(block0.text).toBe('Executing tool first.');

    const block1 = msg.blocks[1];
    if (block1.kind !== 'tool_call') throw new Error('Expected tool call block');
    expect(block1.id).toBe('call-1');
    expect(block1.status).toBe('running');
    expect(block1.startedAt).toEqual(expect.any(Number));
    expect(block1.durationMs).toBeNull();

    const block2 = msg.blocks[2];
    if (block2.kind !== 'message') throw new Error('Expected message block');
    expect(block2.text).toBe('Tool finished.');
  });

  it('correctly updates tool result matching the tool call ID', () => {
    let state = initialAgentState();

    state = agentReducer(state, {
      type: 'AGENT_TOOL_CALL',
      id: 'call-1',
      name: 'calc',
      args: '{"x": 1}',
      messageId: 'msg-1',
      createdAt: new Date(),
    });

    state = agentReducer(state, {
      type: 'AGENT_TOOL_RESULT',
      id: 'call-1',
      name: 'calc',
      result: '2',
      messageId: 'msg-1',
      createdAt: new Date(),
    });

    expect(state.messages).toHaveLength(1);
    const msg = state.messages[0];
    if (msg.role !== 'assistant') {
      throw new Error('Expected assistant message');
    }
    expect(msg.blocks).toHaveLength(1);
    expect(msg.blocks[0]).toEqual({
      kind: 'tool_call',
      id: 'call-1',
      name: 'calc',
      args: '{"x": 1}',
      result: '2',
      status: 'done',
      startedAt: expect.any(Number),
      durationMs: expect.any(Number),
    });
  });
});

describe('agentReducer poll errors (W1)', () => {
  it('shows a visible recoverable error but keeps the active assistant and turn', () => {
    const streaming = run(live(), { type: 'AGENT_TOKEN', token: 'Hi', messageId: 'a1', createdAt: now });
    const next = agentReducer(streaming, { type: 'POLL_ERROR', message: 'bridge down' });
    expect(next.phase).toBe('error');
    expect(next.errorMessage).toBe('bridge down');
    expect(next.pollFailed).toBe(true);
    expect(next.activeAssistantId).toBe('a1');
    expect(next.activeTurnId).toBe('t1');
  });

  it('the next token continues the SAME assistant message and clears the error', () => {
    const failed = run(live(),
      { type: 'AGENT_TOKEN', token: 'Hi', messageId: 'a1', createdAt: now },
      { type: 'POLL_ERROR', message: 'bridge down' });
    const after = agentReducer(failed, { type: 'AGENT_TOKEN', token: '!', messageId: 'a2', createdAt: now });
    expect(after.messages.filter((m) => m.role === 'assistant')).toHaveLength(1);
    expect(after.phase).toBe('streaming');
    expect(after.errorMessage).toBeNull();
    expect(after.pollFailed).toBe(false);
    const block = blocksOf(after)[0];
    if (block?.kind !== 'message') throw new Error('Expected message block');
    expect(block.text).toBe('Hi!');
  });

  it('a recovered complete event does not duplicate already streamed text', () => {
    const failed = run(live(),
      { type: 'AGENT_TOKEN', token: 'Hello', messageId: 'a1', createdAt: now },
      { type: 'POLL_ERROR', message: 'bridge down' });
    const done = agentReducer(failed, { type: 'AGENT_COMPLETE', fullText: 'Hello', messageId: 'a2', createdAt: now });
    expect(done.phase).toBe('complete');
    expect(done.errorMessage).toBeNull();
    const text = blocksOf(done).flatMap((b) => (b.kind === 'message' ? [b.text] : []));
    expect(text.join('')).toBe('Hello');
  });

  it('identical repeated poll errors are a no-op (no scroll bump, same state)', () => {
    const once = agentReducer(live(), { type: 'POLL_ERROR', message: 'bridge down' });
    expect(agentReducer(once, { type: 'POLL_ERROR', message: 'bridge down' })).toBe(once);
  });

  it('a poll failure on a cancelled or finished run is ignored', () => {
    const cancelled = run(live(), { type: 'CANCEL' });
    expect(agentReducer(cancelled, { type: 'POLL_ERROR', message: 'late' })).toBe(cancelled);
  });

  it('a genuine kernel error after a poll failure stays locked against later tokens', () => {
    const state = run(live(),
      { type: 'POLL_ERROR', message: 'bridge down' },
      { type: 'AGENT_ERROR', message: 'Execution error: boom' });
    expect(state.phase).toBe('error');
    expect(state.pollFailed).toBe(false);
    const late = agentReducer(state, { type: 'AGENT_TOKEN', token: 'x', messageId: 'z', createdAt: now });
    expect(late).toBe(state);
  });
});

describe('agentReducer late events after the run ended (W2)', () => {
  it.each(['CANCEL', 'AGENT_COMPLETE', 'AGENT_ERROR'] as const)('ignores late stream actions after %s', (end) => {
    const ended = run(live(), end === 'CANCEL'
      ? { type: 'CANCEL' }
      : end === 'AGENT_COMPLETE'
        ? { type: 'AGENT_COMPLETE', fullText: 'Done', messageId: 'a1', createdAt: now }
        : { type: 'AGENT_ERROR', message: 'boom' });
    const phase = ended.phase;
    for (const late of [
      { type: 'AGENT_TOKEN', token: 'late', messageId: 'x', createdAt: now },
      { type: 'AGENT_THINKING', thinking: 'late', messageId: 'x', createdAt: now },
      { type: 'AGENT_TOOL_CALL', id: 'c', name: 'n', args: '{}', messageId: 'x', createdAt: now },
    ] as const) {
      const next = agentReducer(ended, late);
      expect(next.phase).toBe(phase);
      expect(next.messages).toBe(ended.messages);
    }
  });

  it('a new SUBMIT_PROMPT starts a fresh turn that accepts tokens again', () => {
    const cancelled = run(live(), { type: 'CANCEL' });
    const next = run(cancelled,
      { type: 'SUBMIT_PROMPT', prompt: 'again', messageId: 'u2', turnId: 't2', createdAt: now },
      { type: 'AGENT_TOKEN', token: 'ok', messageId: 'a9', createdAt: now });
    expect(next.phase).toBe('streaming');
  });

  it('swallows the kernel cancel-echo error after a user cancel instead of showing a banner', () => {
    const next = run(live(), { type: 'CANCEL' }, { type: 'AGENT_ERROR', message: 'Cancelled' });
    expect(next.phase).toBe('cancelled');
    expect(next.errorMessage).toBeNull();
  });

  it('still surfaces a genuine error while the run is active', () => {
    const next = agentReducer(live(), { type: 'AGENT_ERROR', message: 'network timeout' });
    expect(next.phase).toBe('error');
    expect(next.errorMessage).toBe('network timeout');
  });
});

describe('agentReducer pending approvals when the run ends (W3)', () => {
  it.each(['CANCEL', 'AGENT_COMPLETE', 'AGENT_ERROR'] as const)('marks pending cards terminal on %s', (end) => {
    const ended = run(live(), approval(), end === 'CANCEL'
      ? { type: 'CANCEL' }
      : end === 'AGENT_COMPLETE'
        ? { type: 'AGENT_COMPLETE', fullText: 'Done', messageId: 'a1', createdAt: now }
        : { type: 'AGENT_ERROR', message: 'boom' });
    const card = blocksOf(ended).find((b) => b.kind === 'interaction_request');
    expect(card).toMatchObject({ status: 'pending', initialState: 'cancelled' });
  });

  it('does not touch an already resolved card', () => {
    const ended = run(live(), approval(), { type: 'INTERACTION_RESOLVE', requestId: 'req-1', answerLabel: 'Confirmed' }, { type: 'CANCEL' });
    expect(blocksOf(ended).find((b) => b.kind === 'interaction_request')).toMatchObject({ status: 'resolved' });
  });
});

describe('agentReducer tool_call / artifact identity (W9)', () => {
  it('upserts a redelivered tool_call by id instead of duplicating it', () => {
    const state = run(live(),
      { type: 'AGENT_TOOL_CALL', id: 'c1', name: 'calc', args: '{"a":1}', messageId: 'a1', createdAt: now },
      { type: 'AGENT_TOOL_RESULT', id: 'c1', name: 'calc', result: '2', messageId: 'a1', createdAt: now },
      { type: 'AGENT_TOOL_CALL', id: 'c1', name: 'calc', args: '{"a":1}', messageId: 'a1', createdAt: now });
    const calls = blocksOf(state).filter((b) => b.kind === 'tool_call');
    expect(calls).toHaveLength(1);
    expect(calls[0]).toMatchObject({ result: '2', status: 'done' });
  });

  it('drops tool events with an empty id rather than colliding them', () => {
    const state = run(live(),
      { type: 'AGENT_TOOL_CALL', id: '', name: 'a', args: '{}', messageId: 'a1', createdAt: now },
      { type: 'AGENT_TOOL_CALL', id: '', name: 'b', args: '{}', messageId: 'a1', createdAt: now });
    expect(blocksOf(state)).toHaveLength(0);
  });

  it('upserts a redelivered artifact by artifactId', () => {
    const artifact = { artifactId: 'art-1', sessionId: 's', displayName: 'p.html', mimeType: 'text/html', sizeBytes: 1, createdAt: 1 };
    const state = run(live(),
      { type: 'AGENT_ARTIFACT', artifact, messageId: 'a1', createdAt: now },
      { type: 'AGENT_ARTIFACT', artifact: { ...artifact, sizeBytes: 2 }, messageId: 'a1', createdAt: now });
    const arts = blocksOf(state).filter((b) => b.kind === 'artifact');
    expect(arts).toHaveLength(1);
  });
});
