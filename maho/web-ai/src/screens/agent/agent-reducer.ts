import type { AgentAction, AgentBlock, AgentMessage, AgentState } from './agent-types';
import type { ArtifactInfo } from '../../bridge/types';
import { parseCredentialErrorCode } from '../credential-error';

export function initialAgentState(): AgentState {
  return {
    activeAssistantId: null,
    activeTurnId: null,
    draft: '',
    errorMessage: null,
    errorCredentialCode: null,
    handle: null,
    messages: [],
    phase: 'idle',
    scrollRevision: 0,
  };
}

export function agentReducer(state: AgentState, action: AgentAction): AgentState {
  switch (action.type) {
    case 'SET_DRAFT':
      return { ...state, draft: action.value };
    case 'SUBMIT_PROMPT':
      return {
        ...state,
        activeAssistantId: null,
        activeTurnId: action.turnId,
        draft: '',
        errorMessage: null,
        errorCredentialCode: null,
        messages: [
          ...state.messages,
          { createdAt: action.createdAt, id: action.messageId, role: 'user', text: action.prompt },
        ],
        phase: 'creating',
        scrollRevision: state.scrollRevision + 1,
      };
    case 'SESSION_READY':
      return { ...state, handle: action.handle, phase: 'streaming' };
    case 'AGENT_TOKEN':
      return appendAssistantToken(state, action.token, action.messageId, action.createdAt);
    case 'AGENT_THINKING':
      return appendAssistantThinking(state, action.thinking, action.messageId, action.createdAt);
    case 'AGENT_TOOL_CALL':
      return appendAssistantToolCall(state, action.id, action.name, action.args, action.messageId, action.createdAt);
    case 'AGENT_TOOL_RESULT':
      return updateAssistantToolResult(state, action.id, action.name, action.result, action.messageId, action.createdAt);
    case 'AGENT_ARTIFACT':
      return appendAssistantArtifact(state, action.artifact, action.messageId, action.createdAt);
    case 'INTERACTION_REQUEST':
      return upsertInteractionRequestBlock(state, {
        kind: 'interaction_request',
        id: action.requestId,
        interactionKind: action.interactionKind,
        question: action.question,
        options: action.options,
        artifactRef: action.artifactRef,
        initialState: action.initialState,
        status: 'pending',
        answerLabel: null,
      }, action.messageId, action.createdAt);
    case 'INTERACTION_RESOLVE':
      return resolveInteractionRequest(state, action.requestId, action.answerLabel);
    case 'AGENT_COMPLETE':
      return completeAssistantTurn(state, action.fullText, action.messageId, action.createdAt);
    case 'AGENT_ERROR':
      return {
        ...state,
        activeAssistantId: null,
        activeTurnId: null,
        errorMessage: action.message,
        errorCredentialCode: parseCredentialErrorCode(action.message),
        phase: 'error',
        scrollRevision: state.scrollRevision + 1,
      };
    case 'CANCEL':
      return {
        ...state,
        activeAssistantId: null,
        activeTurnId: null,
        phase: 'cancelled',
        scrollRevision: state.scrollRevision + 1,
      };
    case 'RESET':
      return initialAgentState();
    default:
      return assertNever(action);
  }
}

function finalizeLastThinkingBlock(
  blocks: readonly AgentBlock[],
  endedAt: Date,
): readonly AgentBlock[] {
  const last = blocks.length > 0 ? blocks[blocks.length - 1] : null;
  if (last && last.kind === 'thinking' && last.durationMs === null) {
    const updatedBlocks = [...blocks];
    updatedBlocks[blocks.length - 1] = {
      ...last,
      durationMs: endedAt.getTime() - last.startedAt,
    };
    return updatedBlocks;
  }
  return blocks;
}

function appendAssistantThinking(
  state: AgentState,
  thinking: string,
  messageId: string,
  createdAt: Date,
): AgentState {
  if (!thinking.trim()) {
    return state;
  }

  if (state.activeAssistantId !== null) {
    return {
      ...state,
      messages: state.messages.map((message) => {
        if (message.id === state.activeAssistantId && message.role === 'assistant') {
          const blocks = [...message.blocks];
          const last = blocks.length > 0 ? blocks[blocks.length - 1] : null;
          if (last && last.kind === 'thinking') {
            blocks[blocks.length - 1] = { ...last, text: last.text + thinking };
          } else {
            blocks.push({
              kind: 'thinking',
              id: Math.random().toString(36).substring(7),
              text: thinking,
              startedAt: createdAt.getTime(),
              durationMs: null,
            });
          }
          return { ...message, blocks };
        }
        return message;
      }),
      phase: 'streaming',
      scrollRevision: state.scrollRevision + 1,
    };
  }

  return {
    ...state,
    activeAssistantId: messageId,
    messages: [
      ...state.messages,
      {
        createdAt,
        id: messageId,
        role: 'assistant',
        blocks: [{
          kind: 'thinking',
          id: Math.random().toString(36).substring(7),
          text: thinking,
          startedAt: createdAt.getTime(),
          durationMs: null,
        }],
      },
    ],
    phase: 'streaming',
    scrollRevision: state.scrollRevision + 1,
  };
}

function appendAssistantToolCall(
  state: AgentState,
  id: string,
  name: string,
  args: string,
  messageId: string,
  createdAt: Date,
): AgentState {
  const newBlock = {
    kind: 'tool_call' as const,
    id,
    name,
    args,
    result: null,
    status: 'running' as const,
    startedAt: createdAt.getTime(),
    durationMs: null,
  };

  if (state.activeAssistantId !== null) {
    return {
      ...state,
      messages: state.messages.map((message) => {
        if (message.id === state.activeAssistantId && message.role === 'assistant') {
          const finalizedBlocks = finalizeLastThinkingBlock(message.blocks, createdAt);
          return { ...message, blocks: [...finalizedBlocks, newBlock] };
        }
        return message;
      }),
      phase: 'streaming',
      scrollRevision: state.scrollRevision + 1,
    };
  }

  return {
    ...state,
    activeAssistantId: messageId,
    messages: [
      ...state.messages,
      {
        createdAt,
        id: messageId,
        role: 'assistant',
        blocks: [newBlock],
      },
    ],
    phase: 'streaming',
    scrollRevision: state.scrollRevision + 1,
  };
}

function appendAssistantArtifact(
  state: AgentState,
  artifact: ArtifactInfo,
  messageId: string,
  createdAt: Date,
): AgentState {
  const newBlock = {
    kind: 'artifact' as const,
    id: artifact.artifactId,
    artifact,
  };

  if (state.activeAssistantId !== null) {
    return {
      ...state,
      messages: state.messages.map((message) => {
        if (message.id === state.activeAssistantId && message.role === 'assistant') {
          const finalizedBlocks = finalizeLastThinkingBlock(message.blocks, createdAt);
          return { ...message, blocks: [...finalizedBlocks, newBlock] };
        }
        return message;
      }),
      phase: 'streaming',
      scrollRevision: state.scrollRevision + 1,
    };
  }

  return {
    ...state,
    activeAssistantId: messageId,
    messages: [
      ...state.messages,
      {
        createdAt,
        id: messageId,
        role: 'assistant',
        blocks: [newBlock],
      },
    ],
    phase: 'streaming',
    scrollRevision: state.scrollRevision + 1,
  };
}

/**
 * Appends an interaction-request approval block. The arrival of a request must
 * not disturb the turn phase — the run stays in its waiting/streaming phase
 * while the kernel is suspended. A block with the same request id (replay
 * redelivery) replaces the existing card instead of duplicating it.
 */
function upsertInteractionRequestBlock(
  state: AgentState,
  block: Extract<AgentBlock, { kind: 'interaction_request' }>,
  messageId: string,
  createdAt: Date,
): AgentState {
  const existing = state.messages.some(
    (message) => message.role === 'assistant'
      && message.blocks.some((entry) => entry.kind === 'interaction_request' && entry.id === block.id),
  );

  if (existing) {
    return {
      ...state,
      messages: state.messages.map((message) => {
        if (message.role !== 'assistant') return message;
        return {
          ...message,
          blocks: message.blocks.map((entry) => {
            if (entry.kind !== 'interaction_request' || entry.id !== block.id) return entry;
            // Replay fidelity: a replayed redelivery must never resurrect a
            // card the user already answered — the local resolution wins.
            if (entry.status === 'resolved') {
              return { ...block, status: 'resolved' as const, answerLabel: entry.answerLabel };
            }
            return block;
          }),
        };
      }),
      scrollRevision: state.scrollRevision + 1,
    };
  }

  if (state.activeAssistantId !== null) {
    return {
      ...state,
      messages: state.messages.map((message) => {
        if (message.id === state.activeAssistantId && message.role === 'assistant') {
          const finalizedBlocks = finalizeLastThinkingBlock(message.blocks, createdAt);
          return { ...message, blocks: [...finalizedBlocks, block] };
        }
        return message;
      }),
      scrollRevision: state.scrollRevision + 1,
    };
  }

  return {
    ...state,
    activeAssistantId: messageId,
    messages: [
      ...state.messages,
      {
        createdAt,
        id: messageId,
        role: 'assistant',
        blocks: [block],
      },
    ],
    scrollRevision: state.scrollRevision + 1,
  };
}

function resolveInteractionRequest(
  state: AgentState,
  requestId: string,
  answerLabel: string,
): AgentState {
  let updated = false;
  const messages = state.messages.map((message) => {
    if (message.role !== 'assistant') return message;
    const blocks = message.blocks.map((entry) => {
      if (entry.kind === 'interaction_request' && entry.id === requestId && entry.status === 'pending') {
        updated = true;
        return { ...entry, status: 'resolved' as const, answerLabel };
      }
      return entry;
    });
    return { ...message, blocks };
  });
  return updated ? { ...state, messages } : state;
}

function updateAssistantToolResult(
  state: AgentState,
  id: string,
  name: string,
  result: string,
  messageId: string,
  createdAt: Date,
): AgentState {
  if (state.activeAssistantId !== null) {
    return {
      ...state,
      messages: state.messages.map((message) => {
        if (message.id === state.activeAssistantId && message.role === 'assistant') {
          let updated = false;
          const blocks = message.blocks.map((block) => {
            if (block.kind === 'tool_call' && block.id === id) {
              updated = true;
              const durationMs = block.startedAt ? (createdAt.getTime() - block.startedAt) : null;
              return { ...block, result, status: 'done' as const, durationMs };
            }
            return block;
          });
          if (!updated) {
            blocks.push({
              kind: 'tool_call',
              id,
              name,
              args: '',
              result,
              status: 'done' as const,
              startedAt: createdAt.getTime(),
              durationMs: 0,
            });
          }
          return { ...message, blocks };
        }
        return message;
      }),
      phase: 'streaming',
      scrollRevision: state.scrollRevision + 1,
    };
  }

  const newBlock = {
    kind: 'tool_call' as const,
    id,
    name,
    args: '',
    result,
    status: 'done' as const,
    startedAt: createdAt.getTime(),
    durationMs: 0,
  };
  return {
    ...state,
    activeAssistantId: messageId,
    messages: [
      ...state.messages,
      {
        createdAt,
        id: messageId,
        role: 'assistant',
        blocks: [newBlock],
      },
    ],
    phase: 'streaming',
    scrollRevision: state.scrollRevision + 1,
  };
}

function appendAssistantToken(
  state: AgentState,
  token: string,
  messageId: string,
  createdAt: Date,
): AgentState {
  if (!token && state.activeAssistantId === null) {
    return state;
  }

  if (state.activeAssistantId !== null) {
    return {
      ...state,
      messages: state.messages.map((message) => {
        if (message.id === state.activeAssistantId && message.role === 'assistant') {
          const blocks = [...message.blocks];
          const last = blocks.length > 0 ? blocks[blocks.length - 1] : null;
          if (last && last.kind === 'message') {
            blocks[blocks.length - 1] = { ...last, text: last.text + token };
            return { ...message, blocks };
          } else {
            const finalizedBlocks = finalizeLastThinkingBlock(message.blocks, createdAt);
            const newBlock = { kind: 'message' as const, id: Math.random().toString(36).substring(7), text: token, markdown: true as const };
            return { ...message, blocks: [...finalizedBlocks, newBlock] };
          }
        }
        return message;
      }),
      phase: 'streaming',
      scrollRevision: state.scrollRevision + 1,
    };
  }

  return {
    ...state,
    activeAssistantId: messageId,
    messages: [
      ...state.messages,
      {
        createdAt,
        id: messageId,
        role: 'assistant',
        blocks: [{ kind: 'message', id: Math.random().toString(36).substring(7), text: token, markdown: true }],
      },
    ],
    phase: 'streaming',
    scrollRevision: state.scrollRevision + 1,
  };
}

function completeAssistantTurn(
  state: AgentState,
  fullText: string,
  messageId: string,
  createdAt: Date,
): AgentState {
  const stateWithFinalizedThinking = {
    ...state,
    messages: state.messages.map((message) => {
      if (message.id === state.activeAssistantId && message.role === 'assistant') {
        return { ...message, blocks: finalizeLastThinkingBlock(message.blocks, createdAt) };
      }
      return message;
    }),
  };
  const messages = mergeFinalAssistantMessage(stateWithFinalizedThinking, fullText, messageId, createdAt);

  return {
    ...stateWithFinalizedThinking,
    activeAssistantId: null,
    activeTurnId: null,
    messages,
    phase: 'complete',
    scrollRevision: state.scrollRevision + 1,
  };
}

function mergeFinalAssistantMessage(
  state: AgentState,
  fullText: string,
  messageId: string,
  createdAt: Date,
): readonly AgentMessage[] {
  if (!fullText) {
    return state.messages;
  }

  if (state.activeAssistantId !== null) {
    return state.messages.map((message) => {
      if (message.id === state.activeAssistantId && message.role === 'assistant') {
        const msgBlocks = message.blocks.filter(b => b.kind === 'message');
        if (msgBlocks.length <= 1) {
          let updated = false;
          const newBlocks = message.blocks.map(b => {
            if (b.kind === 'message') {
              updated = true;
              return { ...b, text: fullText };
            }
            return b;
          });
          if (!updated && fullText) {
            return {
              ...message,
              blocks: [...message.blocks, { kind: 'message', id: Math.random().toString(36).substring(7), text: fullText, markdown: true }]
            };
          }
          return { ...message, blocks: newBlocks };
        }
        return message;
      }
      return message;
    });
  }

  return [
    ...state.messages,
    {
      createdAt,
      id: messageId,
      role: 'assistant',
      blocks: [{ kind: 'message', id: Math.random().toString(36).substring(7), text: fullText, markdown: true }],
    },
  ];
}

function assertNever(value: never): never {
  throw new Error(`Unhandled agent action: ${JSON.stringify(value)}`);
}
