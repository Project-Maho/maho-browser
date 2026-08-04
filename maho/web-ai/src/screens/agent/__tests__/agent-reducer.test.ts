import { describe, it, expect } from 'vitest';
import { agentReducer, initialAgentState } from '../agent-reducer';

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
