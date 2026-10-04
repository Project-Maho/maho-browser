import {describe, expect, it} from 'vitest';
import {
  ApprovalPolicy,
  ApprovalSensitivity,
  ApprovalState,
  RuntimeEventKind,
  ToolCallStatus,
  type RuntimeEvent,
} from '../../maho_ai.mojom-webui.js';
import {collectConversationItems} from '../../views/conversation_thread.js';

function approvalEvent(): RuntimeEvent {
  return {
    approvalRequest: {
      approvalId: 'approval-typed',
      approvalPolicy: ApprovalPolicy.kPrompt,
      description: 'Approve browser_click',
      pageDerivedJustification: true,
      relatedToolCall: {
        argumentsJson: '',
        callId: '',
        status: ToolCallStatus.kPending,
        toolName: 'browser_click',
      },
      sensitivity: ApprovalSensitivity.kSensitive,
      state: ApprovalState.kPending,
    },
    kind: RuntimeEventKind.kApprovalRequest,
    sequence: 1,
    sessionId: 'session-typed',
    timestamp: 1,
    text: 'browser_click',
  };
}

describe('approval metadata projection', () => {
  it('keeps typed approval metadata and no raw arguments in conversation items', () => {
    // Given
    const event = approvalEvent();

    // When
    const items = collectConversationItems([{event, sessionId: event.sessionId}]);

    // Then
    expect(items).toHaveLength(1);
    expect(items[0]?.approval).toMatchObject({
      approvalId: 'approval-typed',
      approvalPolicy: 'Prompt',
      pageDerivedJustification: true,
      sensitivity: 'Sensitive',
      status: 'pending',
      toolName: 'browser_click',
    });
    expect(items[0]?.approval?.toolAction).toBe('Calling browser_click');
    expect(JSON.stringify(items)).not.toContain('selector');
    expect(JSON.stringify(items)).not.toContain('password');
  });
});
