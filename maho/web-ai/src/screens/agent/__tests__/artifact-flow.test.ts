import { describe, it, expect } from 'vitest';
import { agentReducer, initialAgentState } from '../agent-reducer';
import { normalizeAgentEvent } from '../../../hooks/use-agent-session';
import type { ArtifactInfo } from '../../../bridge/types';

function artifact(): ArtifactInfo {
  return {
    artifactId: 'a1',
    sessionId: 's1',
    displayName: 'plan.html',
    mimeType: 'text/html',
    sizeBytes: 128,
    createdAt: 1,
  };
}

describe('artifact event normalization', () => {
  it('normalizes an artifact_created envelope into an AgentEvent', () => {
    const event = normalizeAgentEvent(JSON.stringify({
      type: 'artifact_created',
      data: {
        artifact_id: 'a1',
        session_id: 's1',
        display_name: 'plan.html',
        mime_type: 'text/html',
        size_bytes: 128,
        created_at: 1,
      },
    }));
    expect(event).toEqual({ kind: 'artifact_created', artifact: artifact() });
  });

  it('skips a malformed artifact payload (missing display_name) without throwing', () => {
    const event = normalizeAgentEvent(JSON.stringify({
      type: 'artifact_created',
      data: { artifact_id: 'a1', mime_type: 'text/html' },
    }));
    expect(event).toBeNull();
  });

  it('skips an artifact_created event whose data is not an object', () => {
    const event = normalizeAgentEvent(JSON.stringify({ type: 'artifact_created', data: 'nope' }));
    expect(event).toBeNull();
  });
});

describe('artifact reducer ordering', () => {
  it('appends an artifact block between a tool_call and its result', () => {
    const createdAt = new Date();
    let state = initialAgentState();
    state = agentReducer(state, {
      type: 'AGENT_TOOL_CALL',
      id: 't1',
      name: 'fs_write',
      args: '{}',
      messageId: 'msg-1',
      createdAt,
    });
    state = agentReducer(state, {
      type: 'AGENT_ARTIFACT',
      artifact: artifact(),
      messageId: 'msg-1',
      createdAt,
    });
    state = agentReducer(state, {
      type: 'AGENT_TOOL_RESULT',
      id: 't1',
      name: 'fs_write',
      result: 'ok',
      messageId: 'msg-1',
      createdAt,
    });

    const message = state.messages.find(m => m.role === 'assistant');
    expect(message && message.role === 'assistant').toBe(true);
    if (!message || message.role !== 'assistant') throw new Error('no assistant message');
    expect(message.blocks.map(b => b.kind)).toEqual(['tool_call', 'artifact']);
    const toolCall = message.blocks[0];
    const artifactBlock = message.blocks[1];
    expect(toolCall.kind === 'tool_call' && toolCall.status).toBe('done');
    expect(artifactBlock.kind === 'artifact' && artifactBlock.artifact.artifactId).toBe('a1');
  });
});
