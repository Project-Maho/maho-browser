import {act} from 'react';
import {createRoot, type Root} from 'react-dom/client';
import {afterEach, beforeEach, describe, expect, it, vi} from 'vitest';

import {
  RuntimeEventKind,
  ToolCallStatus,
  type RuntimeEvent,
} from '../../maho_ai.mojom-webui.js';
import {collectConversationItems} from '../../views/conversation_thread.js';
import {EventCard} from '../features/developer/event-card.js';

Object.defineProperty(globalThis, 'IS_REACT_ACT_ENVIRONMENT', {
  configurable: true,
  value: true,
});

function createEvent(
    kind: RuntimeEventKind,
    text: string,
    sequence: number): RuntimeEvent {
  return {
    kind,
    sequence,
    sessionId: 'session-1',
    text,
    timestamp: sequence,
  };
}

function toEntry(event: RuntimeEvent) {
  return {event, sessionId: event.sessionId};
}

describe('assistant thinking presentation', () => {
  let container: HTMLDivElement;
  let root: Root;

  beforeEach(() => {
    container = document.createElement('div');
    document.body.append(container);
    root = createRoot(container);
  });

  afterEach(() => {
    act(() => root.unmount());
    container.remove();
  });

  it('renders thinking as a distinct developer event', () => {
    // Given: an assistant thinking event with Markdown content.
    const event = createEvent(
        RuntimeEventKind.kAssistantThinking,
        'Comparing **two options**.',
        1);

    // When: the developer timeline card renders the event.
    act(() => root.render(
        <EventCard event={event} onOpenSettings={vi.fn()} />));

    // Then: thinking has its own title and Markdown body, not connection state fallback.
    expect(container.textContent).toContain('Assistant thinking');
    expect(container.textContent).not.toContain('Connection state');
    expect(container.querySelector('strong')?.textContent).toBe('two options');
  });

  it('splits thinking around tool events while preserving event order', () => {
    // Given: thinking before and after a tool request.
    const firstThinking = createEvent(
        RuntimeEventKind.kAssistantThinking, 'First thought.', 1);
    const toolRequest: RuntimeEvent = {
      ...createEvent(RuntimeEventKind.kToolRequest, '', 2),
      toolCall: {
        argumentsJson: '{}',
        callId: 'call-1',
        status: ToolCallStatus.kRunning,
        toolName: 'read_file',
      },
    };
    const toolResult: RuntimeEvent = {
      ...createEvent(RuntimeEventKind.kToolResult, '', 3),
      toolResult: {
        callId: 'call-1',
        output: 'file contents',
        success: true,
      },
    };
    const secondThinking = createEvent(
        RuntimeEventKind.kAssistantThinking, 'Second thought.', 4);

    // When: compact conversation items are collected.
    const items = collectConversationItems([
      toEntry(firstThinking),
      toEntry(toolRequest),
      toEntry(toolResult),
      toEntry(secondThinking),
    ]);

    // Then: each thinking segment stays on its side of the tool boundary.
    expect(items.map(item => item.kind)).toEqual([
      'thinking',
      'activity',
      'thinking',
    ]);
    expect(items.map(item => item.text)).toEqual([
      'First thought.',
      'Reading a file',
      'Second thought.',
    ]);
  });

  it('preserves thinking whitespace and Markdown-ish content', () => {
    // Given: thinking text whose leading, internal, and trailing whitespace matters.
    const text = '  **Plan**\n\n- keep spacing\n  ';
    const event = createEvent(RuntimeEventKind.kAssistantThinking, text, 1);

    // When: compact conversation items are collected.
    const items = collectConversationItems([toEntry(event)]);

    // Then: the original chunk survives without trimming or flattening.
    expect(items).toHaveLength(1);
    expect(items[0]?.text).toBe(text);
  });

});
