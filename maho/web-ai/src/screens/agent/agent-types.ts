import type { AgentHandle } from '../../bridge/types';

export type AgentPhase = 'idle' | 'creating' | 'streaming' | 'complete' | 'cancelled' | 'error';

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
      readonly text: string;
      readonly createdAt: Date;
      readonly markdown: true;
    };

export interface AgentState {
  readonly phase: AgentPhase;
  readonly draft: string;
  readonly messages: readonly AgentMessage[];
  readonly handle: AgentHandle | null;
  readonly activeAssistantId: string | null;
  readonly activeTurnId: string | null;
  readonly errorMessage: string | null;
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
  | { readonly type: 'AGENT_COMPLETE'; readonly fullText: string; readonly messageId: string; readonly createdAt: Date }
  | { readonly type: 'AGENT_ERROR'; readonly message: string }
  | { readonly type: 'CANCEL' }
  | { readonly type: 'RESET' };

export function isRunActive(phase: AgentPhase): boolean {
  return phase === 'creating' || phase === 'streaming';
}
