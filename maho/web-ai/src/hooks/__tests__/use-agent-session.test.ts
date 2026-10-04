import { describe, expect, it } from 'vitest';
import { normalizeAgentEvent } from '../use-agent-session';

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
