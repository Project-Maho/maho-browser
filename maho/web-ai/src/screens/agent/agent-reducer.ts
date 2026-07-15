import type { AgentAction, AgentMessage, AgentState } from './agent-types';

export function initialAgentState(): AgentState {
  return {
    activeAssistantId: null,
    activeTurnId: null,
    draft: '',
    errorMessage: null,
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
    case 'AGENT_COMPLETE':
      return completeAssistantTurn(state, action.fullText, action.messageId, action.createdAt);
    case 'AGENT_ERROR':
      return {
        ...state,
        activeAssistantId: null,
        activeTurnId: null,
        errorMessage: action.message,
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
      messages: state.messages.map((message) =>
        message.id === state.activeAssistantId && message.role === 'assistant'
          ? { ...message, text: message.text + token }
          : message,
      ),
      phase: 'streaming',
      scrollRevision: state.scrollRevision + 1,
    };
  }

  return {
    ...state,
    activeAssistantId: messageId,
    messages: [
      ...state.messages,
      { createdAt, id: messageId, markdown: true, role: 'assistant', text: token },
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
  const messages = mergeFinalAssistantMessage(state, fullText, messageId, createdAt);

  return {
    ...state,
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
    return state.messages.map((message) =>
      message.id === state.activeAssistantId && message.role === 'assistant'
        ? { ...message, text: fullText }
        : message,
    );
  }

  return [
    ...state.messages,
    { createdAt, id: messageId, markdown: true, role: 'assistant', text: fullText },
  ];
}

function assertNever(value: never): never {
  throw new Error(`Unhandled agent action: ${JSON.stringify(value)}`);
}
