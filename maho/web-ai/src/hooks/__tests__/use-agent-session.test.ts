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

  it('returns null for unknown, malformed, or incomplete events', () => {
    expect(normalizeAgentEvent('{"type":"thinking","data":"..."}')).toBeNull();
    expect(normalizeAgentEvent('not-json')).toBeNull();
    expect(normalizeAgentEvent('{"data":"hello"}')).toBeNull();
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
