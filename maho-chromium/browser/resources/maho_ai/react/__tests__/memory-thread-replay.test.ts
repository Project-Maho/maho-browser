import {afterEach, expect, it, vi} from 'vitest';

import {MahoAiStore} from '../../store.js';
import {
  ApprovalPolicy, ApprovalSensitivity, ApprovalState, PageCallbackRouter,
  PageHandlerRemote, RuntimeEventKind, SessionStatus, ToolCallStatus,
} from '../../maho_ai.mojom-webui.js';
import type {RuntimeEvent, SessionInfo} from '../../maho_ai.mojom-webui.js';

const session: SessionInfo = {
  sessionId: 'mt-session', createdAt: 1, adapterName: 'fixture',
  isActive: true, isReadOnly: false, status: SessionStatus.kPausedForApproval,
  eventCount: 5, toolCallCount: 1,
};

function event(sequence: number, payload: Partial<RuntimeEvent>): RuntimeEvent {
  return {
    sessionId: session.sessionId, requestId: 'mt-turn', sequence,
    timestamp: sequence, kind: RuntimeEventKind.kAssistantToken, ...payload,
  };
}

function fixture() {
  const remote = new PageHandlerRemote();
  const router = new PageCallbackRouter();
  const deliver = vi.fn<(event: RuntimeEvent) => void>();
  vi.spyOn(router.onRuntimeEvent, 'addListener').mockImplementation(listener => {
    deliver.mockImplementation(listener);
  });
  const store = new MahoAiStore(remote, router);
  return {remote, store, deliver};
}

afterEach(() => vi.restoreAllMocks());

it('memory-thread: resume preserves partial text approval tool and artifact', async () => {
  // Given: reduced replay from the native resume boundary, not a fake reducer.
  const {remote, store} = fixture();
  const toolCall = {
    callId: 'mt-tool', toolName: 'test.echo', argumentsJson: '{}',
    status: ToolCallStatus.kCompleted,
  };
  const toolResult = {callId: 'mt-tool', success: true, output: 'ok'};
  const approvalRequest = {
    approvalId: 'mt-approval', description: '',
    approvalPolicy: ApprovalPolicy.kPrompt,
    sensitivity: ApprovalSensitivity.kSensitive, state: ApprovalState.kPending,
    pageDerivedJustification: false,
  };
  const artifact = {
    artifactId: 'mt-artifact', sessionId: session.sessionId,
    displayName: 'mt.txt', mimeType: 'text/plain', sizeBytes: 2n, createdAt: 1,
  };
  vi.spyOn(remote, 'resumeSession').mockResolvedValue({session, replayEvents: [
    event(1, {text: 'x'.repeat(65536)}),
    event(2, {text: 'x'.repeat(34464)}),
    event(3, {kind: RuntimeEventKind.kToolRequest, toolCall}),
    event(4, {kind: RuntimeEventKind.kToolResult, toolResult}),
    event(5, {kind: RuntimeEventKind.kApprovalRequest, approvalRequest}),
    event(6, {kind: RuntimeEventKind.kArtifactCreated, artifact}),
  ]});
  try {
    // When: a fresh store resumes a partial turn without a history fallback.
    await store.resumeSession(session.sessionId, false, false);

    // Then: replay restores the actionable semantic indexes, not only text.
    const state = store.getSnapshot();
    const entries = state.eventsBySessionId[session.sessionId] ?? [];
    expect(entries.filter(entry => entry.event.kind === RuntimeEventKind.kAssistantToken)
        .map(entry => entry.event.text ?? '').join('')).toBe('x'.repeat(100000));
    expect(state.toolCallsById['mt-tool']).toEqual(toolCall);
    expect(state.toolResultsByCallId['mt-tool']).toEqual(toolResult);
    expect(state.approvalsById['mt-approval']).toEqual(approvalRequest);
    expect(state.artifactsBySessionId[session.sessionId]).toEqual([artifact]);
    expect(entries.filter(entry => entry.event.kind !== RuntimeEventKind.kAssistantToken)
        .map(entry => entry.event.kind)).toEqual([
      RuntimeEventKind.kToolRequest, RuntimeEventKind.kToolResult,
      RuntimeEventKind.kApprovalRequest, RuntimeEventKind.kArtifactCreated,
    ]);
  } finally {
    store.dispose();
  }
});

it('memory-thread: history load cannot replace newer partial turn', async () => {
  // Given: subscribe to history entry before resume triggers the request.
  const {remote, store, deliver} = fixture();
  type HistoryReply = Awaited<ReturnType<PageHandlerRemote['getSessionHistory']>>;
  const historyEntered = new Promise<(reply: HistoryReply) => void>(entered => {
    vi.spyOn(remote, 'getSessionHistory').mockImplementation(
        () => new Promise<HistoryReply>(reply => entered(reply)));
  });
  const older = event(1, {kind: RuntimeEventKind.kUserPrompt, text: 'old-turn'});
  vi.spyOn(remote, 'resumeSession').mockResolvedValue({
    session, replayEvents: [event(10, {text: 'partial-'})],
  });

  vi.spyOn(store, 'loadArtifacts').mockResolvedValue();
  const resumed = store.resumeSession(session.sessionId, true, false);
  const reply = await historyEntered;
  try {
    deliver(event(11, {text: 'newer'}));

    // When: an older history response arrives after the live partial delta.
    reply({events: [older], totalCount: 1});
    await resumed;

    // Then: the live accepted text survives exactly once alongside history.
    const entries = store.getSnapshot().eventsBySessionId[session.sessionId] ?? [];
    expect(entries.filter(entry => entry.event.kind === RuntimeEventKind.kAssistantToken)
        .map(entry => entry.event.text ?? '').join('')).toBe('partial-newer');
    expect(entries.filter(entry => entry.event.kind === RuntimeEventKind.kUserPrompt)
        .map(entry => entry.event.text)).toEqual(['old-turn']);
  } finally {
    reply({events: [], totalCount: 0});
    await resumed;
    store.dispose();
  }
}, 10000);

it('memory-thread: overlapping checkpoints preserve UTF-8 chunks exactly once', async () => {
  const {remote, store, deliver} = fixture();
  const first = '\u{1f642}'.repeat(16384);
  const checkpoint = [
    event(1024, {sequenceStart: 1, text: first}),
    event(1025, {sequenceStart: 1025, text: 'x'}),
  ];
  vi.spyOn(remote, 'resumeSession').mockResolvedValue({session, replayEvents: checkpoint});
  vi.spyOn(remote, 'getSessionHistory').mockResolvedValue({
    events: checkpoint, totalCount: checkpoint.length,
  });
  vi.spyOn(store, 'loadArtifacts').mockResolvedValue();
  try {
    await store.resumeSession(session.sessionId, false, false);
    deliver(event(1026, {text: 'y'}));
    await store.loadHistory(session.sessionId);
    const entries = store.getSnapshot().eventsBySessionId[session.sessionId] ?? [];
    expect(entries).toHaveLength(2);
    expect(entries.map(entry => entry.event.text).join('')).toBe(`${first}xy`);
    expect(entries.map(entry => entry.event.sequence)).toEqual([1024, 1026]);
    expect(entries.map(entry => entry.event.sequenceStart)).toEqual([1, 1025]);
    for (const {event: item} of entries) {
      expect(new TextEncoder().encode(item.text ?? '').byteLength).toBeLessThanOrEqual(65536);
    }
  } finally {
    store.dispose();
  }
});

it('memory-thread: live window eviction cannot be undone by stale history', async () => {
  const {remote, store, deliver} = fixture();
  const toolCall = {callId: 'old-tool', toolName: 'test.echo', argumentsJson: '{}',
    status: ToolCallStatus.kPending};
  const old = [
    event(1, {kind: RuntimeEventKind.kUserPrompt, text: 'old'}),
    event(2, {kind: RuntimeEventKind.kToolRequest, text: null, toolCall}),
    event(3, {kind: RuntimeEventKind.kToolResult, text: null,
      toolResult: {callId: 'old-tool', success: true, output: 'ok', errorMessage: null}}),
    event(4, {kind: RuntimeEventKind.kTurnComplete, text: 'done'}),
  ];
  vi.spyOn(remote, 'resumeSession').mockResolvedValue({session, replayEvents: old});
  vi.spyOn(remote, 'getSessionHistory').mockResolvedValue({events: old, totalCount: old.length});
  vi.spyOn(store, 'loadArtifacts').mockResolvedValue();
  try {
    await store.resumeSession(session.sessionId, false, false);
    deliver(event(11, {requestId: 'new-turn', text: 'new',
      replayWindow: {omittedTurns: 1, omittedThroughSequence: 10}}));
    await store.loadHistory(session.sessionId);
    const state = store.getSnapshot();
    expect(state.toolCallsById['old-tool']).toBeUndefined();
    expect(state.toolResultsByCallId['old-tool']).toBeUndefined();
    expect((state.eventsBySessionId[session.sessionId] ?? [])
        .map(entry => entry.event.text)).toEqual(['new']);
  } finally {
    store.dispose();
  }
});

it('memory-thread: native uint64 sequences keep exact replay ordering', async () => {
  const {remote, store, deliver} = fixture();
  const firstSequence = 9007199254740993n;
  const checkpoint = event(0, {
    sequence: firstSequence, sequenceStart: firstSequence, text: 'a',
  });
  vi.spyOn(remote, 'resumeSession').mockResolvedValue({session, replayEvents: [checkpoint]});
  vi.spyOn(remote, 'getSessionHistory').mockResolvedValue({
    events: [checkpoint], totalCount: 1,
  });
  vi.spyOn(store, 'loadArtifacts').mockResolvedValue();
  try {
    await store.resumeSession(session.sessionId, false, false);
    deliver(event(0, {sequence: firstSequence + 1n, text: 'b'}));
    await store.loadHistory(session.sessionId);
    let entries = store.getSnapshot().eventsBySessionId[session.sessionId] ?? [];
    expect(entries).toHaveLength(1);
    expect(entries[0]?.event.text).toBe('ab');
    expect(entries[0]?.event.sequence).toBe(firstSequence + 1n);
    expect(entries[0]?.event.sequenceStart).toBe(firstSequence);
    deliver(event(0, {
      sequence: firstSequence + 2n, requestId: 'next-turn', text: 'c',
      replayWindow: {omittedTurns: 1n, omittedThroughSequence: firstSequence + 1n},
    }));
    await store.loadHistory(session.sessionId);
    entries = store.getSnapshot().eventsBySessionId[session.sessionId] ?? [];
    expect(entries.map(entry => entry.event.text)).toEqual(['c']);
    expect(entries[0]?.event.sequence).toBe(firstSequence + 2n);
  } finally {
    store.dispose();
  }
});
