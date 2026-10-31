import { useCallback, useEffect, useReducer, useRef } from 'preact/hooks';
import type { AgentEvent, AgentHandle, AgentRuntimeConfig, ComposerDraftScope, MahoBridge } from '../../bridge/types';
import { useComposerDraft } from '../../storage/use-composer-draft';
import { useAgentSession } from '../../hooks/use-agent-session';
import { useBridge } from '../../hooks/use-bridge';
import { agentReducer, initialAgentState } from './agent-reducer';
import {
  agentInteractionAnswerJson,
  agentInteractionAnswerLabel,
  type AgentInteractionAnswer,
} from './approval-card';
import './agent.css';
import { Composer } from './composer';
import { CompactStage } from './compact-stage';
import { CompactTopbar } from './compact-topbar';
import { ConversationThread } from './conversation-thread';
import { freeSessionAfterTeardown, freeSessionForReset } from './session-cleanup';
import { isRunActive } from './agent-types';
import { useAgentAutoScroll } from './use-agent-auto-scroll';
import { getSuggestedTaskCards, SUGGESTED_TASK_RUNTIME_CONFIG, type SuggestedTask } from './suggested-tasks';
import { SuggestedTaskCards } from './suggested-task-cards';

const AGENT_DRAFT_SCOPE: ComposerDraftScope = { kind: 'new_task' };

// Mirrors the native maho-agent browser-tool descriptor policy. Read/viewport
// tools can run automatically; DOM mutations remain approval-gated.
export const AGENT_BROWSER_AUTORUN_TOOLS = new Set([
  'get_page_elements',
  'get_page_snapshot',
  'scroll_page',
]);
export const AGENT_BROWSER_APPROVAL_TOOLS = new Set(['click_element', 'fill_input']);

export interface AgentScreenProps {
  onBack?: () => void;
  goal?: string;
  bridge?: MahoBridge;
  /** SuggestionSettings gate for the idle suggested-task cards (default on). */
  suggestionsEnabled?: boolean;
}

export function AgentScreen({ onBack, goal, bridge: bridgeProp, suggestionsEnabled = true }: AgentScreenProps) {
  const bridgeFromHook = useBridge();
  const bridge = bridgeProp ?? bridgeFromHook;
  const [state, dispatch] = useReducer(agentReducer, undefined, initialAgentState);
  const handleRef = useRef<AgentHandle | null>(null);
  const submittedGoalRef = useRef(false);
  const cancelledTurnIdsRef = useRef<Set<string>>(new Set());
  const draftRef = useRef(state.draft);
  // U13 SPA lifetime: monotonic generation captured at submit; invalidated on
  // unmount, bridge change, and reset. Every await re-checks it before
  // touching the handle, so a late create/config/send can never act on a dead
  // generation and a late-created handle is freed exactly once.
  const generationRef = useRef(0);
  // Shared in-flight create promise within the live generation: two submits
  // never create duplicate ownership.
  const createInFlightRef = useRef<Promise<AgentHandle> | null>(null);
  const { forceBottom, onScroll, scrollRef } = useAgentAutoScroll(state.scrollRevision);

  handleRef.current = state.handle;
  draftRef.current = state.draft;

  // The agent composer has no persisted conversation of its own, so its draft
  // always lives under the new_task scope.
  const hasGoal = Boolean(goal?.trim());
  const restoreDraft = useCallback((text: string) => {
    dispatch({ type: 'SET_DRAFT', value: text });
  }, []);
  const drafts = useComposerDraft({
    bridge,
    onRestore: restoreDraft,
    scope: AGENT_DRAFT_SCOPE,
    suppressRestore: hasGoal,
  });

  const setDraft = useCallback(
    (value: string) => {
      dispatch({ type: 'SET_DRAFT', value });
      drafts.handleDraftChange(value);
    },
    [drafts],
  );

  // A failed send returns the text to the composer, but only when the user has
  // not already typed a replacement while the send was in flight.
  const restoreUnsentDraft = useCallback((prompt: string) => {
    if (draftRef.current.trim().length > 0) return;
    dispatch({ type: 'SET_DRAFT', value: prompt });
  }, []);

  const submitPrompt = useCallback(
    async (rawPrompt: string, runtimeConfig?: AgentRuntimeConfig) => {
      const prompt = rawPrompt.trim();
      if (!prompt) return;

      const generation = generationRef.current;
      const acknowledgeDraft = drafts.captureSend();
      const turnId = createId('turn');
      dispatch({ type: 'SUBMIT_PROMPT', createdAt: new Date(), messageId: createId('user'), prompt, turnId });
      // The optimistic clear only empties the in-memory composer; the persisted
      // draft survives until the native send is accepted.
      forceBottom();

      const generationDead = (): boolean => generationRef.current !== generation;

      try {
        let handle: AgentHandle;
        if (handleRef.current !== null) {
          handle = handleRef.current;
        } else {
          // Shared in-flight create within the live generation prevents two
          // submits from creating duplicate ownership.
          if (createInFlightRef.current === null) {
            const creating = bridge.agentCreateSession(createId('agent-session')).finally(() => {
              if (createInFlightRef.current === creating) createInFlightRef.current = null;
            });
            createInFlightRef.current = creating;
          }
          const created = await createInFlightRef.current;
          if (generationDead()) {
            // A newly created handle that returns to a dead generation is
            // freed once, never sent to, never stored in a dead ref.
            freeSessionAfterTeardown(bridge, created);
            return;
          }
          if (cancelledTurnIdsRef.current.has(turnId)) {
            freeSessionAfterTeardown(bridge, created);
            return;
          }
          handle = created;
          handleRef.current = handle;
          dispatch({ type: 'SESSION_READY', handle });
        }

        if (generationDead()) return;
        if (cancelledTurnIdsRef.current.has(turnId)) return;
        // Suggested-task flag dispatch (mobile parity of the desktop
        // startSuggestedTask flow): create → apply flags → send. A rejection
        // means the broker did not take the guard flags — do not run the task.
        if (runtimeConfig) {
          if (!bridge.agentSetRuntimeConfig) {
            restoreUnsentDraft(prompt);
            dispatch({ type: 'AGENT_ERROR', message: 'Agent runtime flags are not supported on this platform yet.' });
            return;
          }
          const accepted = await bridge.agentSetRuntimeConfig(handle, runtimeConfig);
          if (generationDead() || cancelledTurnIdsRef.current.has(turnId)) return;
          if (!accepted) {
            restoreUnsentDraft(prompt);
            dispatch({ type: 'AGENT_ERROR', message: 'The suggested task flags were rejected; the task was not started.' });
            return;
          }
        }
        const sent = await bridge.agentSendMessage(handle, prompt);
        if (generationDead()) {
          // Late acknowledgement for a dead generation: never resurrect state
          // and never restore the draft into a new screen.
          return;
        }
        if (!sent) {
          restoreUnsentDraft(prompt);
          dispatch({ type: 'AGENT_ERROR', message: 'Agent did not accept the message.' });
          return;
        }
        void acknowledgeDraft();
      } catch (error) {
        if (generationDead()) return;
        restoreUnsentDraft(prompt);
        dispatch({ type: 'AGENT_ERROR', message: messageFromError(error, 'Failed to start agent session') });
      }
    },
    [bridge, drafts, forceBottom, restoreUnsentDraft],
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
      case 'artifact_created':
        dispatch({
          type: 'AGENT_ARTIFACT',
          createdAt: new Date(),
          artifact: event.artifact,
          messageId: createId('assistant'),
        });
        break;
      case 'interaction_request':
        dispatch({
          type: 'INTERACTION_REQUEST',
          requestId: event.id,
          interactionKind: event.interactionKind,
          question: event.question,
          options: event.options,
          artifactRef: event.artifactRef ?? null,
          initialState: event.state ?? null,
          messageId: createId('assistant'),
          createdAt: new Date(),
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

  // Poll whenever a session handle is attached — not only mid-turn — so the
  // resume/bootstrap replay of historical events (interaction requests that
  // still await an answer, or expired ones) re-materializes approval cards.
  useAgentSession({
    bridge,
    handle: state.handle,
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
      // Unmount invalidates every in-flight submit generation; late creates
      // are freed by their submit's generation check, and the live session —
      // if one was already attached — is freed here.
      generationRef.current += 1;
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
    if (handle === null) {
      // The cancelled submit owns its late result and will free it; a retry
      // must allocate independently rather than adopt that same handle.
      createInFlightRef.current = null;
    }
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
    // U13: reset is valid even while a session create is in flight — the
    // generation bump makes the pending submit free its late handle exactly
    // once instead of adopting it.
    generationRef.current += 1;
    createInFlightRef.current = null;
    if (state.activeTurnId !== null) {
      cancelledTurnIdsRef.current.add(state.activeTurnId);
    }
    const handle = handleRef.current;
    if (handle !== null) {
      await freeSessionForReset(bridge, handle);
      handleRef.current = null;
    }
    cancelledTurnIdsRef.current.clear();
    await drafts.clear();
    dispatch({ type: 'RESET' });
  }, [bridge, drafts, state.activeTurnId]);

  const handleOpenSettings = useCallback(() => {
    window.location.hash = 'byok';
  }, []);

  const suggestedCards = getSuggestedTaskCards({ enabled: suggestionsEnabled });
  const startSuggestedTask = useCallback((task: SuggestedTask) => {
    void submitPrompt(task.prompt, SUGGESTED_TASK_RUNTIME_CONFIG);
  }, [submitPrompt]);

  const handleShareArtifact = useCallback(async (artifactId: string): Promise<boolean> => {
    const handle = handleRef.current;
    if (handle === null) {
      throw new Error('Agent session is no longer available.');
    }
    return bridge.artifactShare(handle, artifactId);
  }, [bridge]);

  // Sends the user's resolution for a pending interaction request back to the
  // kernel through the dedicated bridge method (mirrors the FFI
  // maho_agent_interaction_resolve session-handle call). Failures throw so the
  // approval card can surface them locally and stay retryable.
  const handleInteractionAnswer = useCallback(
    async (requestId: string, answer: AgentInteractionAnswer): Promise<void> => {
      const handle = handleRef.current;
      if (handle === null) {
        throw new Error('Agent session is no longer available.');
      }
      if (!bridge.agentResolveInteraction) {
        throw new Error('Agent interaction answers are not supported on this platform yet.');
      }
      const accepted = await bridge.agentResolveInteraction(handle, requestId, agentInteractionAnswerJson(answer));
      if (!accepted) {
        throw new Error('The agent could not accept this answer. The request may have expired.');
      }
      dispatch({ type: 'INTERACTION_RESOLVE', requestId, answerLabel: agentInteractionAnswerLabel(answer) });
    },
    [bridge],
  );

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
          errorCredentialCode={state.errorCredentialCode}
          messages={state.messages}
          onScroll={onScroll}
          onShareArtifact={handleShareArtifact}
          onAnswerInteraction={handleInteractionAnswer}
          phase={state.phase}
          scrollRef={scrollRef}
        />
      ) : (
        <CompactStage hasSession={state.handle !== null || Boolean(goal?.trim())} />
      )}
      {!showThread && <SuggestedTaskCards cards={suggestedCards} onStart={startSuggestedTask} />}
      <Composer
        draft={state.draft}
        hasMessages={showThread}
        phase={state.phase}
        onBlur={drafts.flush}
        onDraftChange={setDraft}
        onStop={() => void handleStop()}
        onSubmit={handleSubmit}
      />
    </div>
  );
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
