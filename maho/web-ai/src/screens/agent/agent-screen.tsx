import { useCallback, useEffect, useReducer, useRef } from 'preact/hooks';
import type { AgentEvent, AgentHandle, MahoBridge } from '../../bridge/types';
import { useAgentSession } from '../../hooks/use-agent-session';
import { useBridge } from '../../hooks/use-bridge';
import { agentReducer, initialAgentState } from './agent-reducer';
import './agent.css';
import { Composer } from './composer';
import { CompactStage } from './compact-stage';
import { CompactTopbar } from './compact-topbar';
import { ConversationThread } from './conversation-thread';
import { freeSessionAfterTeardown, freeSessionForReset } from './session-cleanup';
import { isRunActive } from './agent-types';
import { useAgentAutoScroll } from './use-agent-auto-scroll';

export interface AgentScreenProps {
  onBack?: () => void;
  goal?: string;
  bridge?: MahoBridge;
}

export function AgentScreen({ onBack, goal, bridge: bridgeProp }: AgentScreenProps) {
  const bridgeFromHook = useBridge();
  const bridge = bridgeProp ?? bridgeFromHook;
  const [state, dispatch] = useReducer(agentReducer, undefined, initialAgentState);
  const handleRef = useRef<AgentHandle | null>(null);
  const submittedGoalRef = useRef(false);
  const cancelledTurnIdsRef = useRef<Set<string>>(new Set());
  const { forceBottom, onScroll, scrollRef } = useAgentAutoScroll(state.scrollRevision);

  handleRef.current = state.handle;

  const submitPrompt = useCallback(
    async (rawPrompt: string) => {
      const prompt = rawPrompt.trim();
      if (!prompt) return;

      const turnId = createId('turn');
      dispatch({ type: 'SUBMIT_PROMPT', createdAt: new Date(), messageId: createId('user'), prompt, turnId });
      forceBottom();

      try {
        const handle = await ensureSessionHandle(bridge, handleRef.current, turnId, cancelledTurnIdsRef.current);
        if (handle === null) return;
        handleRef.current = handle;
        dispatch({ type: 'SESSION_READY', handle });

        if (cancelledTurnIdsRef.current.has(turnId)) return;
        const sent = await bridge.agentSendMessage(handle, prompt);
        if (!sent) {
          dispatch({ type: 'AGENT_ERROR', message: 'Agent did not accept the message.' });
        }
      } catch (error) {
        dispatch({ type: 'AGENT_ERROR', message: messageFromError(error, 'Failed to start agent session') });
      }
    },
    [bridge, forceBottom],
  );

  const handleAgentEvent = useCallback((event: AgentEvent) => {
    switch (event.kind) {
      case 'token':
        dispatch({ type: 'AGENT_TOKEN', createdAt: new Date(), messageId: createId('assistant'), token: event.token });
        break;
      case 'thinking':
        dispatch({ type: 'AGENT_THINKING', createdAt: new Date(), messageId: createId('assistant'), thinking: event.thinking });
        break;
      case 'tool_call':
        dispatch({
          type: 'AGENT_TOOL_CALL',
          createdAt: new Date(),
          id: event.id,
          name: event.name,
          args: event.args,
          messageId: createId('assistant'),
        });
        break;
      case 'tool_result':
        dispatch({
          type: 'AGENT_TOOL_RESULT',
          createdAt: new Date(),
          id: event.id,
          name: event.name,
          result: event.result,
          messageId: createId('assistant'),
        });
        break;
      case 'complete':
        dispatch({
          type: 'AGENT_COMPLETE',
          createdAt: new Date(),
          fullText: event.fullText,
          messageId: createId('assistant'),
        });
        break;
      case 'error':
        dispatch({ type: 'AGENT_ERROR', message: event.message });
        break;
      default:
        assertNever(event);
    }
  }, []);

  useAgentSession({
    bridge,
    handle: state.phase === 'streaming' ? state.handle : null,
    onEvent: handleAgentEvent,
  });

  useEffect(() => {
    const trimmedGoal = goal?.trim() ?? '';
    if (!trimmedGoal || submittedGoalRef.current) return;
    submittedGoalRef.current = true;
    dispatch({ type: 'SET_DRAFT', value: trimmedGoal });
    void submitPrompt(trimmedGoal);
  }, [goal, submitPrompt]);

  useEffect(() => {
    return () => {
      const handle = handleRef.current;
      if (handle !== null) {
        freeSessionAfterTeardown(bridge, handle);
      }
    };
  }, [bridge]);

  const handleSubmit = useCallback(() => {
    if (isRunActive(state.phase)) return;
    void submitPrompt(state.draft);
  }, [state.draft, state.phase, submitPrompt]);

  const handleStop = useCallback(async () => {
    if (state.activeTurnId !== null) {
      cancelledTurnIdsRef.current.add(state.activeTurnId);
    }

    const handle = handleRef.current;
    if (handle !== null) {
      try {
        await bridge.agentCancel(handle);
      } catch (error) {
        dispatch({ type: 'AGENT_ERROR', message: messageFromError(error, 'Failed to stop current run') });
        return;
      }
    }
    dispatch({ type: 'CANCEL' });
  }, [bridge, state.activeTurnId]);

  const handleNewSession = useCallback(async () => {
    if (isRunActive(state.phase)) return;
    const handle = handleRef.current;
    if (handle !== null) {
      await freeSessionForReset(bridge, handle);
      handleRef.current = null;
    }
    cancelledTurnIdsRef.current.clear();
    dispatch({ type: 'RESET' });
  }, [bridge, state.phase]);

  const handleOpenSettings = useCallback(() => {
    window.location.hash = 'byok';
  }, []);

  const showThread = state.messages.length > 0 || state.errorMessage !== null;

  return (
    <div class="agent-screen" data-testid="agent-screen">
      <CompactTopbar
        isRunning={isRunActive(state.phase)}
        onBack={onBack}
        onNewSession={() => void handleNewSession()}
        onOpenSettings={() => void handleOpenSettings()}
      />
      {showThread ? (
        <ConversationThread
          errorMessage={state.errorMessage}
          messages={state.messages}
          onScroll={onScroll}
          phase={state.phase}
          scrollRef={scrollRef}
        />
      ) : (
        <CompactStage hasSession={state.handle !== null || Boolean(goal?.trim())} />
      )}
      <Composer
        draft={state.draft}
        hasMessages={showThread}
        phase={state.phase}
        onDraftChange={(value) => dispatch({ type: 'SET_DRAFT', value })}
        onStop={() => void handleStop()}
        onSubmit={handleSubmit}
      />
    </div>
  );
}

async function ensureSessionHandle(
  bridge: MahoBridge,
  existingHandle: AgentHandle | null,
  turnId: string,
  cancelledTurnIds: ReadonlySet<string>,
): Promise<AgentHandle | null> {
  if (existingHandle !== null) {
    return existingHandle;
  }

  const handle = await bridge.agentCreateSession(createId('agent-session'));
  if (cancelledTurnIds.has(turnId)) {
    freeSessionAfterTeardown(bridge, handle);
    return null;
  }
  return handle;
}

function messageFromError(error: unknown, fallback: string): string {
  return error instanceof Error ? error.message : fallback;
}

function createId(prefix: string): string {
  if (typeof crypto !== 'undefined' && typeof crypto.randomUUID === 'function') {
    return `${prefix}-${crypto.randomUUID()}`;
  }
  return `${prefix}-${Math.random().toString(36).slice(2, 10)}`;
}

function assertNever(value: never): never {
  throw new Error(`Unhandled agent event: ${JSON.stringify(value)}`);
}
