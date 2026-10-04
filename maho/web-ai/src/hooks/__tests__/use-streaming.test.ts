import { describe, expect, it } from 'vitest';
import { normalizeChatEvent } from '../use-streaming';

describe('normalizeChatEvent', () => {
  it('normalizes native token, complete, and error wire events', () => {
    expect(normalizeChatEvent({ type: 'token', data: 'hello' })).toEqual({
      kind: 'token',
      token: 'hello',
    });
    expect(normalizeChatEvent({
      type: 'complete',
      data: {
        full_text: 'All done.',
        tool_calls_json: JSON.stringify([
          {
            id: 'call-list-tabs',
            name: 'list_tabs',
            arguments_json: '{}',
          },
        ]),
      },
    })).toEqual({
      kind: 'complete',
      content: 'All done.',
      toolCallsJson: JSON.stringify([
        {
          id: 'call-list-tabs',
          name: 'list_tabs',
          arguments_json: '{}',
        },
      ]),
      toolCalls: [{
        id: 'call-list-tabs',
        name: 'list_tabs',
        args: {},
      }],
    });
    expect(normalizeChatEvent({ type: 'error', data: 'Cancelled' })).toEqual({
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

    const native = normalizeChatEvent({ type: 'error', data: envelope });
    expect(native).toEqual({
      kind: 'error',
      message: envelope,
      credentialErrorCode: 'secure_store_unavailable',
    });

    const normalized = normalizeChatEvent({ kind: 'error', message: envelope });
    expect(normalized).toEqual({
      kind: 'error',
      message: envelope,
      credentialErrorCode: 'secure_store_unavailable',
    });

    // Plain string errors carry no code.
    expect(
      normalizeChatEvent({ type: 'error', data: 'Execution error: network timeout' }),
    ).toEqual({ kind: 'error', message: 'Execution error: network timeout' });
  });

  it('accepts already-normalized desktop events', () => {
    expect(normalizeChatEvent({ kind: 'token', token: 'delta' })).toEqual({
      kind: 'token',
      token: 'delta',
    });
    expect(normalizeChatEvent({ kind: 'complete', content: 'final' })).toEqual({
      kind: 'complete',
      content: 'final',
    });
  });

  it('normalizes native thinking wire events', () => {
    expect(normalizeChatEvent({ type: 'thinking', data: 'Let me think' })).toEqual({
      kind: 'thinking',
      thinking: 'Let me think',
    });
    expect(normalizeChatEvent({ kind: 'thinking', thinking: 'reasoning' })).toEqual({
      kind: 'thinking',
      thinking: 'reasoning',
    });
  });

  it('drops malformed tool-call payloads without losing the completed response', () => {
    expect(normalizeChatEvent({
      type: 'complete',
      data: { full_text: 'All done.', tool_calls_json: '{not-json' },
    })).toEqual({
      kind: 'complete',
      content: 'All done.',
      toolCallsJson: '{not-json',
    });
  });

  it('robustly ignores malformed and unknown input', () => {
    expect(normalizeChatEvent(null)).toBeNull();
    expect(normalizeChatEvent('not-an-object')).toBeNull();
    expect(normalizeChatEvent({})).toBeNull();
    expect(normalizeChatEvent({ type: 'token', data: 42 })).toBeNull();
    expect(normalizeChatEvent({ type: 'complete', data: {} })).toBeNull();
    expect(normalizeChatEvent({ type: 'thinking', data: 42 })).toBeNull();
    expect(normalizeChatEvent({ type: 'unknown', data: 'ignored' })).toBeNull();
  });
});
