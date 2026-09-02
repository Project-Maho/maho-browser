import type { AgentHandle, AgentInteractionRequestOption, ArtifactInfo } from '../../bridge/types';
import type { CredentialErrorCode } from '../credential-error';

export type AgentPhase = 'idle' | 'creating' | 'streaming' | 'complete' | 'cancelled' | 'error';

export type AgentBlock =
  | {
      readonly kind: 'thinking';
      readonly id: string;
      readonly text: string;
      readonly startedAt: number;
      readonly durationMs: number | null;
    }
  | {
      readonly kind: 'message';
      readonly id: string;
      readonly text: string;
      readonly markdown: true;
    }
  | {
      readonly kind: 'tool_call';
      readonly id: string;
      readonly name: string;
      readonly args: string;
      readonly result: string | null;
      readonly status: 'running' | 'done';
      readonly startedAt: number;
      readonly durationMs: number | null;
    }
  | {
      readonly kind: 'artifact';
      readonly id: string;
      readonly artifact: ArtifactInfo;
    }
  | {
      readonly kind: 'interaction_request';
      readonly id: string;
      readonly interactionKind: 'question' | 'confirmation';
      readonly question: string;
      readonly options: readonly AgentInteractionRequestOption[];
      readonly artifactRef: string | null;
      readonly initialState: string | null;
      readonly status: 'pending' | 'resolved';
      readonly answerLabel: string | null;
    };

export type AgentMessage =
  | {
      readonly id: string;
      readonly role: 'user';
      readonly text: string;
      readonly createdAt: Date;
    }
  | {
      readonly id: string;
      readonly role: 'assistant';
      readonly blocks: readonly AgentBlock[];
      readonly createdAt: Date;
    };

export interface AgentState {
  readonly phase: AgentPhase;
  readonly draft: string;
  readonly messages: readonly AgentMessage[];
  readonly handle: AgentHandle | null;
  readonly activeAssistantId: string | null;
  readonly activeTurnId: string | null;
  readonly errorMessage: string | null;
  readonly errorCredentialCode: CredentialErrorCode | null;
  readonly scrollRevision: number;
}

export type AgentAction =
  | { readonly type: 'SET_DRAFT'; readonly value: string }
  | {
      readonly type: 'SUBMIT_PROMPT';
      readonly prompt: string;
      readonly messageId: string;
      readonly turnId: string;
      readonly createdAt: Date;
    }
  | { readonly type: 'SESSION_READY'; readonly handle: AgentHandle }
  | { readonly type: 'AGENT_TOKEN'; readonly token: string; readonly messageId: string; readonly createdAt: Date }
  | { readonly type: 'AGENT_THINKING'; readonly thinking: string; readonly messageId: string; readonly createdAt: Date }
  | { readonly type: 'AGENT_TOOL_CALL'; readonly id: string; readonly name: string; readonly args: string; readonly messageId: string; readonly createdAt: Date }
  | { readonly type: 'AGENT_TOOL_RESULT'; readonly id: string; readonly name: string; readonly result: string; readonly messageId: string; readonly createdAt: Date }
  | { readonly type: 'AGENT_ARTIFACT'; readonly artifact: ArtifactInfo; readonly messageId: string; readonly createdAt: Date }
  | {
      readonly type: 'INTERACTION_REQUEST';
      readonly requestId: string;
      readonly interactionKind: 'question' | 'confirmation';
      readonly question: string;
      readonly options: readonly AgentInteractionRequestOption[];
      readonly artifactRef: string | null;
      readonly initialState: string | null;
      readonly messageId: string;
      readonly createdAt: Date;
    }
  | { readonly type: 'INTERACTION_RESOLVE'; readonly requestId: string; readonly answerLabel: string }
  | { readonly type: 'AGENT_COMPLETE'; readonly fullText: string; readonly messageId: string; readonly createdAt: Date }
  | { readonly type: 'AGENT_ERROR'; readonly message: string }
  | { readonly type: 'CANCEL' }
  | { readonly type: 'RESET' };

export function isRunActive(phase: AgentPhase): boolean {
  return phase === 'creating' || phase === 'streaming';
}
