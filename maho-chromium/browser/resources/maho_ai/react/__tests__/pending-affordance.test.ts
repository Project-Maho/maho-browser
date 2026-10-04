import {describe, expect, it} from 'vitest';

import {RuntimeEventKind, type RuntimeEvent} from '../../maho_ai.mojom-webui.js';
import {getPendingThreadLabel} from '../../views/conversation_thread.js';
import type {TimelineEntry} from '../../types.js';

function entry(kind: RuntimeEventKind, sequence: number, extras: Partial<RuntimeEvent> = {}):
    TimelineEntry {
  return {
    sessionId: 'session-1',
    event: {kind, sequence, timestamp: sequence, sessionId: 'session-1', ...extras},
  };
}

describe('getPendingThreadLabel — submit dead-air affordance', () => {
  it('labels a freshly submitted turn as Working even with no runtime events yet', () => {
    // After submit, turnPending flips before the first runtime event lands.
    expect(getPendingThreadLabel([], true)).toBe('Working…');
  });

  it('labels a turn whose only event is the user prompt as Working', () => {
    const entries = [entry(RuntimeEventKind.kUserPrompt, 1, {text: 'Summarize this'})];
    expect(getPendingThreadLabel(entries, true)).toBe('Working…');
  });

  it('derives the progress verb from the latest tool request of the turn', () => {
    const entries = [
      entry(RuntimeEventKind.kUserPrompt, 1, {text: 'Read the page'}),
      entry(RuntimeEventKind.kToolRequest, 2,
          {toolCall: {toolCallId: 't1', toolName: 'read_current_page'} as never}),
    ];
    expect(getPendingThreadLabel(entries, true)).toBe('Reading…');
  });

  it('returns no label when the turn is not pending', () => {
    expect(getPendingThreadLabel([], false)).toBeNull();
  });

  it('returns no label once assistant tokens stream (in-place markdown replaces it)', () => {
    const entries = [
      entry(RuntimeEventKind.kUserPrompt, 1, {text: 'Hello'}),
      entry(RuntimeEventKind.kAssistantToken, 2, {text: 'Hi'}),
    ];
    expect(getPendingThreadLabel(entries, true)).toBeNull();
  });
});
