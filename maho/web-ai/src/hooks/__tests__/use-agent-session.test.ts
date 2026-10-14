import { act, renderHook } from '@testing-library/preact';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import type { MahoBridge } from '../../bridge/types';
import { normalizeAgentEvent, useAgentSession } from '../use-agent-session';

describe('normalizeAgentEvent', () => {
  it('parses token event data as the token delta', () => {
    expect(normalizeAgentEvent('{"type":"token","data":"hello"}')).toEqual({
      kind: 'token',
      token: 'hello',
    });
  });

  it('parses complete event object data as full text', () => {
    expect(
      normalizeAgentEvent(
        '{"type":"complete","data":{"full_text":"All done.","tool_calls_json":"[]"}}',
      ),
    ).toEqual({ kind: 'complete', fullText: 'All done.' });
  });

  it('parses error event data as a display message', () => {
    expect(normalizeAgentEvent('{"type":"error","data":"Cancelled"}')).toEqual({
      kind: 'error',
      message: 'Cancelled',
    });
  });

  it('extracts a typed credentialErrorCode from the credential envelope', () => {
    const envelope = JSON.stringify({
      version: 1,
      kind: 'credential_error',
      code: 'secure_store_unavailable',
    });
    expect(normalizeAgentEvent(JSON.stringify({ type: 'error', data: envelope }))).toEqual({
      kind: 'error',
      message: envelope,
      credentialErrorCode: 'secure_store_unavailable',
    });

    expect(normalizeAgentEvent('{"type":"error","data":"network timeout"}')).toEqual({
      kind: 'error',
      message: 'network timeout',
    });
  });

  it('returns null for unknown, malformed, or incomplete events', () => {
    expect(normalizeAgentEvent('{"type":"unknown_type","data":"..."}')).toBeNull();
    expect(normalizeAgentEvent('not-json')).toBeNull();
    expect(normalizeAgentEvent('{"data":"hello"}')).toBeNull();
  });

  it('parses thinking event data', () => {
    expect(normalizeAgentEvent('{"type":"thinking","data":"..."}')).toEqual({
      kind: 'thinking',
      thinking: '...',
    });
  });

  it('parses tool_call event data', () => {
    expect(
      normalizeAgentEvent(
        '{"type":"tool_call","data":{"id":"c1","name":"t1","args":"{}"}}',
      ),
    ).toEqual({
      kind: 'tool_call',
      id: 'c1',
      name: 't1',
      args: '{}',
    });
  });

  it('parses tool_result event data', () => {
    expect(
      normalizeAgentEvent(
        '{"type":"tool_result","data":{"id":"c1","name":"t1","result":"ok"}}',
      ),
    ).toEqual({
      kind: 'tool_result',
      id: 'c1',
      name: 't1',
      result: 'ok',
    });
  });

  it('keeps empty token strings and string complete payloads', () => {
    expect(normalizeAgentEvent('{"type":"token","data":""}')).toEqual({
      kind: 'token',
      token: '',
    });
    expect(normalizeAgentEvent('{"type":"complete","data":"plain response"}')).toEqual({
      kind: 'complete',
      fullText: 'plain response',
    });
  });
});

describe('normalizeAgentEvent interaction_request parsing (W4, W10)', () => {
  it('never produces a blind confirmation: falls back to tool name and args', () => {
    const event = normalizeAgentEvent(JSON.stringify({
      type: 'interaction_request',
      data: { request_id: 'r1', kind: 'confirmation', tool_name: 'browser.click', args: JSON.stringify({ selector: '#buy' }) },
    }));
    expect(event).toMatchObject({ kind: 'interaction_request', id: 'r1', interactionKind: 'confirmation' });
    const question = (event as { question: string }).question;
    expect(question).toContain('browser.click');
    expect(question).toContain('#buy');
  });

  it('prefers request_id over id as the kernel reply key', () => {
    const event = normalizeAgentEvent(JSON.stringify({
      type: 'interaction_request',
      data: { id: 'wrong', request_id: 'right', kind: 'question', question: 'Which?' },
    }));
    expect(event).toMatchObject({ id: 'right' });
  });
});

describe('useAgentSession transport failures (W1)', () => {
  beforeEach(() => vi.useFakeTimers());
  afterEach(() => vi.useRealTimers());

  it('reports a failed poll via onPollStatus only (no onEvent, no onDone) and keeps retrying', async () => {
    let calls = 0;
    const bridge = {
      agentPollEvent: vi.fn().mockImplementation(async () => {
        calls += 1;
        if (calls === 1) throw new Error('bridge down');
        return null;
      }),
    } as unknown as MahoBridge;
    const onEvent = vi.fn();
    const onDone = vi.fn();
    const onPollStatus = vi.fn();
    renderHook(() => useAgentSession({ bridge, handle: 'h1', onEvent, onDone, onPollStatus }));

    for (let pump = 0; pump < 8 && calls < 3; pump++) {
      await act(async () => {
        await vi.advanceTimersByTimeAsync(500);
      });
    }

    expect(onPollStatus.mock.calls.map(([s]) => s)).toEqual([{ kind: 'poll_error', message: 'bridge down' }]);
    expect(calls).toBeGreaterThanOrEqual(3);
    expect(onEvent).not.toHaveBeenCalled();
    expect(onDone).not.toHaveBeenCalled();
  });
});
