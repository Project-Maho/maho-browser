import {describe, expect, it} from 'vitest';

import {
  ApprovalDecision,
  ApprovalPolicy,
  ApprovalSensitivity,
  ApprovalState,
  RuntimeEventKind,
  ToolCallStatus,
  type RuntimeEvent,
} from '../../maho_ai.mojom-webui.js';
import {collectConversationItems} from '../../views/conversation_thread.js';

function createApprovalRequestEvent(): RuntimeEvent {
  return {
    kind: RuntimeEventKind.kApprovalRequest,
    sequence: 1,
    timestamp: 1,
    sessionId: 'session-approval',
    text: 'browser_click',
    approvalRequest: {
      approvalId: 'approval-1',
      approvalPolicy: ApprovalPolicy.kPrompt,
      description: 'The agent is requesting browser control.',
      pageDerivedJustification: true,
      relatedToolCall: {
        callId: '',
        toolName: 'browser_click',
        argumentsJson: '{"selector":"raw-secret-selector"}',
        status: ToolCallStatus.kPending,
      },
      sensitivity: ApprovalSensitivity.kSensitive,
      state: ApprovalState.kPending,
    },
  };
}

describe('approval payload rendering', () => {
  it('uses typed approval metadata without surfacing raw tool arguments', () => {
    // Given
    const request = createApprovalRequestEvent();

    // When
    const items = collectConversationItems([{sessionId: request.sessionId, event: request}]);

    // Then
    expect(items).toHaveLength(1);
    expect(items[0]?.approval).toEqual(expect.objectContaining({
      approvalId: 'approval-1',
      pageDerivedJustification: true,
      approvalPolicy: 'Prompt',
      sensitivity: 'Sensitive',
      state: 'Pending',
      status: 'pending',
      toolName: 'browser_click',
    }));
    expect(JSON.stringify(items)).not.toContain('raw-secret-selector');
  });

  it('merges typed approval decision metadata onto the pending item', () => {
    // Given
    const request = createApprovalRequestEvent();
    const result: RuntimeEvent = {
      kind: RuntimeEventKind.kApprovalResult,
      sequence: 2,
      timestamp: 2,
      sessionId: request.sessionId,
      approvalResult: {
        approvalId: 'approval-1',
        approved: false,
        approvalPolicy: ApprovalPolicy.kPrompt,
        sensitivity: ApprovalSensitivity.kSensitive,
        state: ApprovalState.kDenied,
        decision: ApprovalDecision.kDeny,
        pageDerivedJustification: true,
      },
    };

    // When
    const items = collectConversationItems([
      {sessionId: request.sessionId, event: request},
      {sessionId: request.sessionId, event: result},
    ]);

    // Then
    expect(items).toHaveLength(1);
    expect(items[0]?.approval).toEqual(expect.objectContaining({
      approvalId: 'approval-1',
      decision: 'Deny',
      state: 'Denied',
      status: 'denied',
    }));
  });
});
