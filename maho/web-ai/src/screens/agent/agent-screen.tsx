import { type JSX } from 'preact';
import { useCallback, useEffect, useReducer, useRef } from 'preact/hooks';
import { useBridge } from '../../hooks/use-bridge';
import { Icon } from '../../ui/icon';

interface ToolDefinition {
  name: string;
  description: string;
  parameters: Record<string, unknown>;
}

interface ToolResult {
  success: boolean;
  output: string;
}

interface ToolCallDelta {
  index?: number;
  id?: string;
  toolCallId?: string;
  name?: string;
  tool?: string;
  args?: unknown;
  arguments?: unknown;
  argumentsDelta?: string;
}

interface StreamSubscriber {
  onToken?: (token: string) => void;
  onComplete?: (finalMessage: string) => void;
  onError?: (error: string) => void;
  onToolDelta?: (delta: ToolCallDelta) => void;
}

interface AgentBridgeMethods {
  byokGetKey(provider: string): Promise<string | null>;
  chatSessionStart(opts: { model: string; systemInstruction: string; apiKey: string; endpoint: string }): Promise<string>;
  chatSessionFree(handle: string): Promise<void>;
  chatSendMessage(handle: string, content: { kind: string; text: string }): Promise<void>;
  chatCancelTurn(handle: string): Promise<void>;
  chatRegisterTool(handle: string, tool: ToolDefinition): Promise<void>;
  chatSendToolResult(handle: string, toolCallId: string, result: ToolResult): Promise<void>;
}

type AgentBridge = AgentBridgeMethods;

function subscribeStream(sessionId: string, sub: StreamSubscriber): () => void {
  const globalWindow = window as Window & {
    __mahoAgentStreamSubscribers?: Map<string, StreamSubscriber>;
  };
  if (!globalWindow.__mahoAgentStreamSubscribers) {
    globalWindow.__mahoAgentStreamSubscribers = new Map();
  }
  globalWindow.__mahoAgentStreamSubscribers.set(sessionId, sub);
  return () => {
    globalWindow.__mahoAgentStreamSubscribers?.delete(sessionId);
  };
}

const AGENT_SYSTEM_PROMPT =
  'You are a web agent for Maho Browser. You can navigate pages, query the DOM, take screenshots, and summarize page content. Execute user tasks step by step using the available tools.';

const AGENT_TOOLS: ToolDefinition[] = [
  {
    name: 'browser_navigate',
    description: 'Navigate to a URL',
    parameters: { type: 'object', properties: { url: { type: 'string' } }, required: ['url'] },
  },
  {
    name: 'dom_query',
    description: 'Query DOM with CSS selector',
    parameters: { type: 'object', properties: { selector: { type: 'string' } }, required: ['selector'] },
  },
  {
    name: 'screenshot',
    description: 'Take screenshot of current page',
    parameters: { type: 'object', properties: {} },
  },
  {
    name: 'page_summary',
    description: 'Summarize current page',
    parameters: { type: 'object', properties: {} },
  },
];

// ─── Types ──────────────────────────────────────────────────────────────────

type AgentPhase = 'idle' | 'submitted' | 'streaming' | 'awaiting_approval' | 'complete' | 'cancelled' | 'error';

interface ThinkingEntry {
  kind: 'thinking';
  id: string;
  text: string;
}

interface ToolCallEntry {
  kind: 'tool-call';
  id: string;
  toolCallId: string;
  toolName: string;
  argsText: string;
  status: 'pending' | 'approved' | 'denied' | 'result';
}

interface ResultEntry {
  kind: 'result';
  id: string;
  text: string;
}

type LogEntry = ThinkingEntry | ToolCallEntry | ResultEntry;

interface AgentState {
  phase: AgentPhase;
  task: string;
  log: LogEntry[];
  sessionId: string | null;
  missingApiKey: boolean;
  errorMessage: string | null;
}

type AgentAction =
  | { type: 'SET_TASK'; value: string }
  | { type: 'BYOK_MISSING' }
  | { type: 'BYOK_OK' }
  | { type: 'SUBMIT'; sessionId: string }
  | { type: 'STREAM_TOKEN'; text: string }
  | { type: 'TOOL_DELTA'; delta: ToolCallDelta }
  | { type: 'TOOL_APPROVED'; toolCallId: string }
  | { type: 'TOOL_DENIED'; toolCallId: string }
  | { type: 'STREAM_COMPLETE'; finalMessage: string }
  | { type: 'STREAM_ERROR'; message: string }
  | { type: 'CANCEL' }
  | { type: 'RESET' };

export interface AgentScreenProps {
  onBack?: () => void;
  bridge?: AgentBridge;
}

// ─── Reducer ────────────────────────────────────────────────────────────────

function initialState(): AgentState {
  return {
    phase: 'idle',
    task: '',
    log: [],
    sessionId: null,
    missingApiKey: false,
    errorMessage: null,
  };
}

function reducer(state: AgentState, action: AgentAction): AgentState {
  switch (action.type) {
    case 'SET_TASK':
      return { ...state, task: action.value };

    case 'BYOK_MISSING':
      return { ...state, missingApiKey: true };

    case 'BYOK_OK':
      return { ...state, missingApiKey: false };

    case 'SUBMIT':
      return {
        ...state,
        phase: 'submitted',
        log: [],
        sessionId: action.sessionId,
        errorMessage: null,
      };

    case 'STREAM_TOKEN': {
      const lastEntry = state.log[state.log.length - 1];
      let nextLog: LogEntry[];
      if (lastEntry && lastEntry.kind === 'thinking') {
        nextLog = state.log.map((entry, idx) =>
          idx === state.log.length - 1 && entry.kind === 'thinking'
            ? { ...entry, text: entry.text + action.text }
            : entry,
        );
      } else {
        nextLog = [
          ...state.log,
          { kind: 'thinking', id: createId('think'), text: action.text },
        ];
      }
      return { ...state, phase: 'streaming', log: nextLog };
    }

    case 'TOOL_DELTA': {
      const toolCallId = action.delta.id ?? action.delta.toolCallId ?? `tool-${action.delta.index ?? 0}`;
      const toolName = action.delta.name ?? action.delta.tool ?? 'unknown';
      const argsText = formatArgs(action.delta.args ?? action.delta.arguments ?? action.delta.argumentsDelta ?? '{}');
      const existingIdx = state.log.findIndex(
        (e) => e.kind === 'tool-call' && e.toolCallId === toolCallId,
      );

      let nextLog: LogEntry[];
      if (existingIdx >= 0) {
        nextLog = state.log.map((entry, idx) => {
          if (idx !== existingIdx || entry.kind !== 'tool-call') return entry;
          return {
            ...entry,
            toolName: toolName || entry.toolName,
            argsText: argsText || entry.argsText,
          };
        });
      } else {
        nextLog = [
          ...state.log,
          { kind: 'tool-call', id: createId('toolcall'), toolCallId, toolName, argsText, status: 'pending' },
        ];
      }
      return { ...state, phase: 'awaiting_approval', log: nextLog };
    }

    case 'TOOL_APPROVED':
      return {
        ...state,
        phase: 'streaming',
        log: state.log.map((e) =>
          e.kind === 'tool-call' && e.toolCallId === action.toolCallId
            ? { ...e, status: 'approved' as const }
            : e,
        ),
      };

    case 'TOOL_DENIED':
      return {
        ...state,
        phase: 'streaming',
        log: state.log.map((e) =>
          e.kind === 'tool-call' && e.toolCallId === action.toolCallId
            ? { ...e, status: 'denied' as const }
            : e,
        ),
      };

    case 'STREAM_COMPLETE': {
      const resultLog: LogEntry[] = action.finalMessage
        ? [...state.log, { kind: 'result', id: createId('result'), text: action.finalMessage }]
        : state.log;
      return { ...state, phase: 'complete', log: resultLog };
    }

    case 'STREAM_ERROR':
      return { ...state, phase: 'error', errorMessage: action.message };

    case 'CANCEL':
      return { ...state, phase: 'cancelled' };

    case 'RESET':
      return { ...initialState(), missingApiKey: state.missingApiKey };

    default:
      return state;
  }
}

// ─── Component ──────────────────────────────────────────────────────────────

export function AgentScreen({ onBack, bridge: bridgeProp }: AgentScreenProps) {
  const bridgeFromHook = useBridge();
  const bridge = (bridgeProp ?? bridgeFromHook) as unknown as AgentBridge;
  const [state, dispatch] = useReducer(reducer, undefined, initialState);
  const logRef = useRef<HTMLDivElement | null>(null);
  const sessionRef = useRef<string | null>(null);

  // Check BYOK on mount
  useEffect(() => {
    void (async () => {
      const key = await bridge.byokGetKey('openai');
      if (!key) {
        const anthropicKey = await bridge.byokGetKey('anthropic');
        if (!anthropicKey) {
          dispatch({ type: 'BYOK_MISSING' });
          return;
        }
      }
      dispatch({ type: 'BYOK_OK' });
    })();
  }, [bridge]);

  // Subscribe to stream events
  useEffect(() => {
    if (!state.sessionId) return undefined;
    return subscribeStream(state.sessionId, {
      onToken: (token) => dispatch({ type: 'STREAM_TOKEN', text: token }),
      onComplete: (finalMessage) => dispatch({ type: 'STREAM_COMPLETE', finalMessage }),
      onError: (error) => dispatch({ type: 'STREAM_ERROR', message: error }),
      onToolDelta: (delta) => dispatch({ type: 'TOOL_DELTA', delta }),
    });
  }, [state.sessionId]);

  // Auto-scroll log
  useEffect(() => {
    const node = logRef.current;
    if (node) {
      node.scrollTop = node.scrollHeight;
    }
  }, [state.log]);

  // Cleanup session on unmount
  useEffect(() => {
    return () => {
      const handle = sessionRef.current;
      if (handle) {
        void bridge.chatSessionFree(handle).catch(() => undefined);
      }
    };
  }, [bridge]);

  const handleSubmit = useCallback(async () => {
    const taskText = state.task.trim();
    if (!taskText || state.missingApiKey) return;

    try {
      const key = (await bridge.byokGetKey('openai')) ?? (await bridge.byokGetKey('anthropic')) ?? '';
      const sessionId = await bridge.chatSessionStart({
        model: 'gpt-4o',
        systemInstruction: AGENT_SYSTEM_PROMPT,
        apiKey: key,
        endpoint: 'https://api.openai.com/v1',
      });

      sessionRef.current = sessionId;

      await Promise.all(AGENT_TOOLS.map((tool) => bridge.chatRegisterTool(sessionId, tool)));

      dispatch({ type: 'SUBMIT', sessionId });

      await bridge.chatSendMessage(sessionId, { kind: 'text', text: taskText });
    } catch (err) {
      dispatch({ type: 'STREAM_ERROR', message: err instanceof Error ? err.message : 'Failed to start agent session' });
    }
  }, [bridge, state.task, state.missingApiKey]);

  const handleApprove = useCallback(async (toolCallId: string) => {
    if (!state.sessionId) return;
    const result: ToolResult = { success: true, output: JSON.stringify({ ok: true, data: '[stub]' }) };
    dispatch({ type: 'TOOL_APPROVED', toolCallId });
    await bridge.chatSendToolResult(state.sessionId, toolCallId, result);
  }, [bridge, state.sessionId]);

  const handleDeny = useCallback(async (toolCallId: string) => {
    if (!state.sessionId) return;
    const result: ToolResult = { success: false, output: JSON.stringify({ error: 'user_denied' }) };
    dispatch({ type: 'TOOL_DENIED', toolCallId });
    await bridge.chatSendToolResult(state.sessionId, toolCallId, result);
  }, [bridge, state.sessionId]);

  const handleCancel = useCallback(async () => {
    if (!state.sessionId) return;
    await bridge.chatCancelTurn(state.sessionId);
    dispatch({ type: 'CANCEL' });
  }, [bridge, state.sessionId]);

  const handleNewTask = useCallback(async () => {
    if (state.sessionId) {
      await bridge.chatSessionFree(state.sessionId);
      sessionRef.current = null;
    }
    dispatch({ type: 'RESET' });
  }, [bridge, state.sessionId]);

  const isActive = state.phase === 'submitted' || state.phase === 'streaming' || state.phase === 'awaiting_approval';

  return (
    <div class="agent-screen" data-testid="agent-screen">
      <header class="agent-header">
        <div class="agent-header-main">
          {onBack && (
            <button type="button" class="agent-back-button" onClick={onBack} aria-label="Back">
              <Icon name="chevron-left" size={18} aria-hidden />
            </button>
          )}
          <div class="agent-header-copy">
            <p class="agent-eyebrow">Agent</p>
            <h1 class="agent-title">Web Agent</h1>
          </div>
        </div>
        <div class="agent-header-actions">
          {(state.phase === 'complete' || state.phase === 'cancelled' || state.phase === 'error') && (
            <button
              type="button"
              class="agent-new-button"
              onClick={() => void handleNewTask()}
              aria-label="New task"
            >
              + New
            </button>
          )}
        </div>
      </header>

      <main class="agent-content">
        {state.missingApiKey ? (
          <section class="agent-empty-state agent-empty-state--cta">
            <div class="agent-empty-icon" aria-hidden="true"><span>🔑</span></div>
            <h2 class="agent-empty-heading">API key required</h2>
            <p class="agent-empty-copy">Add an OpenAI or Anthropic key to use the Web Agent.</p>
            <button
              type="button"
              class="agent-cta-button"
              onClick={() => { window.location.hash = '#byok'; }}
            >
              Configure API key
            </button>
          </section>
        ) : state.phase === 'idle' ? (
          <section class="agent-task-input">
            <textarea
              class="agent-textarea"
              aria-label="Task description"
              placeholder="Describe a task for the agent…"
              rows={4}
              value={state.task}
              onInput={(e) => dispatch({ type: 'SET_TASK', value: (e.target as HTMLTextAreaElement).value })}
            />
            <button
              type="button"
              class="agent-run-button"
              aria-label="Run Agent"
              disabled={!state.task.trim() || state.missingApiKey}
              onClick={() => void handleSubmit()}
            >
              Run Agent
            </button>
          </section>
        ) : (
          <>
            {state.errorMessage && (
              <div class="agent-error-banner" role="alert">{state.errorMessage}</div>
            )}
            <div ref={logRef} class="agent-log" role="log" aria-live="polite">
              {state.log.map((entry) => renderLogEntry(entry, handleApprove, handleDeny))}
            </div>
            {isActive && (
              <button
                type="button"
                class="agent-cancel-button"
                aria-label="Cancel"
                onClick={() => void handleCancel()}
              >
                Cancel
              </button>
            )}
            {(state.phase === 'complete' || state.phase === 'cancelled' || state.phase === 'error') && (
              <button
                type="button"
                class="agent-new-task-button"
                aria-label="New task"
                onClick={() => void handleNewTask()}
              >
                New Task
              </button>
            )}
          </>
        )}
      </main>
    </div>
  );
}

// ─── Log entry rendering ────────────────────────────────────────────────────

function renderLogEntry(
  entry: LogEntry,
  onApprove: (id: string) => Promise<void>,
  onDeny: (id: string) => Promise<void>,
): JSX.Element {
  switch (entry.kind) {
    case 'thinking':
      return (
        <div key={entry.id} class="agent-thinking-entry">
          <span class="agent-thinking-prefix" aria-hidden="true">🤖</span>
          <span class="agent-thinking-text">{entry.text}</span>
        </div>
      );
    case 'tool-call':
      return (
        <div key={entry.id} class="agent-tool-card">
          <div class="agent-tool-header">
            <strong class="agent-tool-name">{entry.toolName}</strong>
            <span class={`agent-tool-badge agent-tool-badge--${entry.status}`}>{entry.status}</span>
          </div>
          <pre class="agent-tool-args">{entry.argsText}</pre>
          {entry.status === 'pending' && (
            <div class="agent-tool-actions">
              <button
                type="button"
                class="agent-tool-approve"
                aria-label={`Approve ${entry.toolName}`}
                onClick={() => void onApprove(entry.toolCallId)}
              >
                Approve
              </button>
              <button
                type="button"
                class="agent-tool-deny"
                aria-label={`Deny ${entry.toolName}`}
                onClick={() => void onDeny(entry.toolCallId)}
              >
                Deny
              </button>
            </div>
          )}
        </div>
      );
    case 'result':
      return (
        <div key={entry.id} class="agent-result-block">
          <MarkdownRenderer text={entry.text} />
        </div>
      );
  }
}

// ─── Minimal markdown renderer (extracted from chat-screen pattern) ─────────

function MarkdownRenderer({ text }: { text: string }): JSX.Element {
  const lines = text.replace(/\r\n/g, '\n').split('\n');
  const blocks: JSX.Element[] = [];

  for (let i = 0; i < lines.length;) {
    const line = lines[i] ?? '';
    if (!line.trim()) { i++; continue; }

    if (line.startsWith('```')) {
      const codeLines: string[] = [];
      i++;
      while (i < lines.length && !lines[i]?.startsWith('```')) {
        codeLines.push(lines[i] ?? '');
        i++;
      }
      if (i < lines.length) i++;
      blocks.push(<pre key={`code-${blocks.length}`} class="agent-md-pre"><code>{codeLines.join('\n')}</code></pre>);
      continue;
    }

    if (line.startsWith('- ')) {
      const items: string[] = [];
      while (i < lines.length && (lines[i] ?? '').startsWith('- ')) {
        items.push((lines[i] ?? '').slice(2));
        i++;
      }
      blocks.push(
        <ul key={`list-${blocks.length}`} class="agent-md-list">
          {items.map((item, idx) => <li key={idx}>{item}</li>)}
        </ul>,
      );
      continue;
    }

    const pLines: string[] = [];
    while (i < lines.length) {
      const cur = lines[i] ?? '';
      if (!cur.trim() || cur.startsWith('```') || cur.startsWith('- ')) break;
      pLines.push(cur);
      i++;
    }
    blocks.push(<p key={`p-${blocks.length}`} class="agent-md-p">{pLines.join(' ')}</p>);
  }

  return <div class="agent-md">{blocks}</div>;
}

// ─── Helpers ────────────────────────────────────────────────────────────────

function createId(prefix: string): string {
  if (typeof crypto !== 'undefined' && typeof crypto.randomUUID === 'function') {
    return `${prefix}-${crypto.randomUUID()}`;
  }
  return `${prefix}-${Math.random().toString(36).slice(2, 10)}`;
}

function formatArgs(value: unknown): string {
  if (typeof value === 'string') {
    try {
      return JSON.stringify(JSON.parse(value), null, 2);
    } catch {
      return value;
    }
  }
  return JSON.stringify(value, null, 2) ?? '{}';
}
