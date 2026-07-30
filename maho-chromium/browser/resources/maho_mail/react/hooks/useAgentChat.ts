import { useEffect, useMemo, useState, useCallback, useRef } from "react";
import { listen } from "@tauri-apps/api/event";
import * as api from "../api";
import type {
  AgentChatMessage,
  AgentChatSession,
  AgentStreamEvent,
  AgentStreamEventKind,
} from "../types";
import { useToast } from "../components/ui/Toast";
import { getErrorMessage } from "../lib/utils";

// Discriminated wire format for the `agent-message-chunk` Tauri event.
// Legacy backend emits the flat `{ session_id, chunk }` shape; the new typed
// pipeline (D-A8) emits AgentStreamEvent. The hook accepts either to stay
// forward-compatible while the Rust side migrates.
type AgentChunkWireFormat =
  | { session_id: string; chunk: string; kind?: undefined; payload?: undefined }
  | AgentStreamEvent;

function getString(payload: Record<string, unknown>, key: string): string {
  const value = payload[key];
  return typeof value === "string" ? value : "";
}

function phaseTextForTool(toolName: string): string {
  if (toolName === "search_emails") return "Searching emails...";
  if (toolName === "get_email") return "Reading email...";
  return "Executing tool...";
}

function parseStructuredError(errorMsg: string): { summary: string; detail: string } | null {
  const match = errorMsg.match(/^([^(]+)\s*\((.+)\)$/s);
  if (match) {
    return {
      summary: match[1].trim(),
      detail: match[2].trim(),
    };
  }
  return null;
}

export function useAgentChat(accountId: string) {
  const { toast } = useToast();
  const [sessions, setSessions] = useState<AgentChatSession[]>([]);
  const [currentSessionId, setCurrentSessionId] = useState<string | null>(null);
  const [messages, setMessages] = useState<AgentChatMessage[]>([]);
  const [isHistoryLoading, setIsHistoryLoading] = useState(false);
  const [isSending, setIsSending] = useState(false);
  const [aiConfigured, setAiConfigured] = useState<boolean | null>(null);
  const [streamingMessage, setStreamingMessage] = useState<string>("");
  const [phaseText, setPhaseText] = useState<string>("");

  const isSendingRef = useRef(isSending);
  const currentSessionIdRef = useRef(currentSessionId);
  const accountIdRef = useRef(accountId);
  const loadedSessionIdRef = useRef<string | null>(null);

  useEffect(() => {
    isSendingRef.current = isSending;
  }, [isSending]);

  useEffect(() => {
    currentSessionIdRef.current = currentSessionId;
  }, [currentSessionId]);

  useEffect(() => {
    accountIdRef.current = accountId;
  }, [accountId]);

  const currentSession = useMemo(
    () => sessions.find((session) => session.id === currentSessionId) ?? null,
    [sessions, currentSessionId],
  );

  const handleCancel = useCallback(async () => {
    if (!currentSessionIdRef.current) {
      return;
    }
    try {
      await api.cancelAgentMessage(accountId, currentSessionIdRef.current);
    } catch (error) {
      const errMsg = getErrorMessage(error, "Failed to cancel request");
      const structured = parseStructuredError(errMsg);
      toast("error", structured || errMsg);
    } finally {
      isSendingRef.current = false;
      setIsSending(false);
      setStreamingMessage("");
      setPhaseText("");
    }
  }, [accountId, toast]);

  // Listen for streaming chunks plus the OQ4 triplet completion / error
  // signals emitted by the Rust backend
  useEffect(() => {
    const unlistenChunk = listen<AgentChunkWireFormat>(
      "agent-message-chunk",
      (event) => {
        const wire = event.payload as any;
        if (wire.session_id !== currentSessionIdRef.current) return;

        if (wire.kind === undefined) {
          setStreamingMessage((prev) => prev + (wire.chunk ?? ""));
          return;
        }

        const kind: AgentStreamEventKind = wire.kind;
        const payload = wire.payload;

        if (kind === "token") {
          setStreamingMessage((prev) => prev + getString(payload, "chunk"));
        } else if (kind === "phase") {
          setPhaseText(getString(payload, "text"));
        } else if (kind === "tool_call_start") {
          setPhaseText(phaseTextForTool(getString(payload, "tool_name")));
        } else if (kind === "tool_call_result") {
          setPhaseText("");
        } else if (kind === "error") {
          setPhaseText("");
          const err = getString(payload, "error") || "Tool execution failed";
          toast("error", err);
        }
      }
    );
    const unlistenComplete = listen<{ session_id: string }>(
      "agent-message-complete",
      (event) => {
        if (event.payload.session_id === currentSessionIdRef.current) {
          setStreamingMessage("");
          setPhaseText("");
        }
      }
    );
    const unlistenError = listen<{ session_id: string; error: string }>(
      "agent-message-error",
      (event) => {
        if (event.payload.session_id === currentSessionIdRef.current) {
          setStreamingMessage("");
          setPhaseText("");
          setIsSending(false);
        }
      }
    );

    return () => {
      unlistenChunk.then((u) => u());
      unlistenComplete.then((u) => u());
      unlistenError.then((u) => u());
    };
  }, [toast]);

  // Clear streaming message and phase when session changes
  useEffect(() => {
    setStreamingMessage("");
    setPhaseText("");
  }, [currentSessionId]);

  // Check AI provider configuration
  useEffect(() => {
    let cancelled = false;

    async function checkAiConfig() {
      try {
        const config = await api.getAiConfig();
        if (!cancelled) {
          setAiConfigured(!!config);
        }
      } catch (error) {
        if (!cancelled) {
          setAiConfigured(false);
        }
      }
    }

    void checkAiConfig();

    const handleConfigChanged = () => {
      void checkAiConfig();
      if (isSendingRef.current && currentSessionIdRef.current) {
        void api.cancelAgentMessage(accountIdRef.current, currentSessionIdRef.current);
        setIsSending(false);
        setStreamingMessage("");
        setPhaseText("");
      }
    };
    window.addEventListener("ai-config-changed", handleConfigChanged);

    return () => {
      cancelled = true;
      window.removeEventListener("ai-config-changed", handleConfigChanged);
    };
  }, []);

  // Load sessions when accountId changes
  useEffect(() => {
    let cancelled = false;

    async function loadSessions() {
      if (!accountId) {
        setSessions([]);
        setCurrentSessionId(null);
        setMessages([]);
        return;
      }

      try {
        const nextSessions = await api.listAgentSessions(accountId);
        if (cancelled) {
          return;
        }

        setSessions(nextSessions);
        setCurrentSessionId((current) => {
          if (current && nextSessions.some((session) => session.id === current)) {
            return current;
          }

          return nextSessions[0]?.id ?? null;
        });
      } catch (error) {
        if (!cancelled) {
          const errMsg = getErrorMessage(error, "Failed to load agent sessions");
          const structured = parseStructuredError(errMsg);
          toast("error", structured || errMsg);
        }
      }
    }

    void loadSessions();

    return () => {
      cancelled = true;
    };
  }, [accountId, toast]);

  // Load messages when currentSessionId changes
  useEffect(() => {
    let cancelled = false;

    async function loadMessages() {
      if (!currentSessionId) {
        setMessages([]);
        loadedSessionIdRef.current = null;
        return;
      }

      if (currentSessionId === loadedSessionIdRef.current) {
        return;
      }

      if (isSendingRef.current) return;
      setIsHistoryLoading(true);
      try {
        const nextMessages = await api.getAgentMessages(accountId, currentSessionId);
        if (!cancelled) {
          setMessages(nextMessages);
          loadedSessionIdRef.current = currentSessionId;
        }
      } catch (error) {
        if (!cancelled) {
          const errMsg = getErrorMessage(error, "Failed to load agent messages");
          const structured = parseStructuredError(errMsg);
          toast("error", structured || errMsg);
        }
      } finally {
        if (!cancelled) {
          setIsHistoryLoading(false);
        }
      }
    }

    void loadMessages();

    return () => {
      cancelled = true;
    };
  }, [accountId, currentSessionId, toast]);

  const handleCreateSession = useCallback(async () => {
    if (!accountId) {
      toast("error", "Select an account before opening the AI agent");
      return;
    }

    try {
      const session = await api.createAgentSession(accountId);
      setSessions((current) => [session, ...current]);
      setCurrentSessionId(session.id);
      loadedSessionIdRef.current = session.id;
      setMessages([]);
    } catch (error) {
      const errMsg = getErrorMessage(error, "Failed to create a new chat");
      const structured = parseStructuredError(errMsg);
      toast("error", structured || errMsg);
    }
  }, [accountId, toast]);

  const handleDeleteSession = useCallback(async (sessionId: string) => {
    try {
      await api.deleteAgentSession(accountId, sessionId);
      setSessions((current) => {
        const nextSessions = current.filter((session) => session.id !== sessionId);
        if (sessionId === currentSessionId) {
          setCurrentSessionId(nextSessions[0]?.id ?? null);
          setMessages([]);
        }
        return nextSessions;
      });
    } catch (error) {
      const errMsg = getErrorMessage(error, "Failed to delete chat");
      const structured = parseStructuredError(errMsg);
      toast("error", structured || errMsg);
    }
  }, [accountId, currentSessionId, toast]);

  const handleSend = useCallback(async (content: string) => {
    let sessionId = currentSessionId;
    isSendingRef.current = true;

    try {
      setIsSending(true);
      setStreamingMessage("");

      if (!sessionId) {
        const session = await api.createAgentSession(accountId);
        setSessions((current) => [session, ...current]);
        setCurrentSessionId(session.id);
        loadedSessionIdRef.current = session.id;
        sessionId = session.id;
      }

      const optimisticUserMsg: AgentChatMessage = {
        id: crypto.randomUUID(),
        session_id: sessionId,
        role: "user",
        content,
        created_at: new Date().toISOString(),
      };
      setMessages((prev) => [...prev, optimisticUserMsg]);

      const assistantMsg = await api.sendAgentMessage(accountId, sessionId, content);
      setMessages((prev) => [...prev, assistantMsg]);
      setSessions((current) => current.map((session) => (
        session.id === sessionId
          ? {
              ...session,
              title: session.title || content.slice(0, 48),
              updated_at: new Date().toISOString(),
            }
          : session
      )));
    } catch (error) {
      const errMsg = getErrorMessage(error, "Failed to send message");
      const structured = parseStructuredError(errMsg);
      toast("error", structured || errMsg);
    } finally {
      isSendingRef.current = false;
      setIsSending(false);
      setStreamingMessage("");
      setPhaseText("");
    }
  }, [accountId, currentSessionId, toast]);

  return {
    sessions,
    currentSessionId,
    currentSession,
    messages,
    isHistoryLoading,
    isSending,
    aiConfigured,
    streamingMessage,
    phaseText,
    setCurrentSessionId,
    handleCreateSession,
    handleDeleteSession,
    handleSend,
    handleCancel,
  };
}
