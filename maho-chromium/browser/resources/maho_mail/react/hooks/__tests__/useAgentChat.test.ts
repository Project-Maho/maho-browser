import { renderHook, act, waitFor } from "@testing-library/react";
import { describe, it, expect, vi, beforeEach } from "vitest";
import { useAgentChat } from "../useAgentChat";
import * as api from "../../api";
import { listen } from "@tauri-apps/api/event";
import type { AgentChatSession, AgentChatMessage } from "../../types";

vi.mock("@tauri-apps/api/event", () => ({ listen: vi.fn() }));

// Mock the API module
vi.mock("../../api", () => ({
  getAiConfig: vi.fn(),
  listAgentSessions: vi.fn(),
  getAgentMessages: vi.fn(),
  createAgentSession: vi.fn(),
  deleteAgentSession: vi.fn(),
  sendAgentMessage: vi.fn(),
}));

// Mock useToast from components/ui/Toast
const mockToast = vi.fn();
vi.mock("../../components/ui/Toast", () => ({
  useToast: () => ({
    toast: mockToast,
  }),
  Toaster: () => null,
}));

describe("useAgentChat hook", () => {
  const accountId = "acc-123";
  const mockSessions: AgentChatSession[] = [
    {
      id: "session-1",
      account_id: accountId,
      title: "Chat 1",
      created_at: new Date().toISOString(),
      updated_at: new Date().toISOString(),
    },
  ];

  const mockMessages: AgentChatMessage[] = [
    {
      id: "msg-1",
      session_id: "session-1",
      role: "user",
      content: "Hello",
      created_at: new Date().toISOString(),
    },
  ];

  beforeEach(() => {
    vi.clearAllMocks();

    vi.mocked(listen).mockImplementation((_event: string, _callback: any) => {
      return Promise.resolve(() => {});
    });
  });

  it("loads sessions on mount and messages on session change", async () => {
    vi.mocked(api.getAiConfig).mockResolvedValue({
      provider: "OpenAi",
      model: "gpt-4o",
      has_key: true,
    });
    vi.mocked(api.listAgentSessions).mockResolvedValue(mockSessions);
    vi.mocked(api.getAgentMessages).mockResolvedValue(mockMessages);

    const { result } = renderHook(() => useAgentChat(accountId));

    // Initially loading should check config and load sessions
    await waitFor(() => {
      expect(result.current.aiConfigured).toBe(true);
      expect(result.current.sessions).toEqual(mockSessions);
    });

    // Session-1 should be auto-selected, loading its messages
    await waitFor(() => {
      expect(result.current.currentSessionId).toBe("session-1");
      expect(result.current.messages).toEqual(mockMessages);
    });
  });

  it("re-checks config on 'ai-config-changed' event", async () => {
    vi.mocked(api.getAiConfig).mockResolvedValue(null); // start unconfigured
    vi.mocked(api.listAgentSessions).mockResolvedValue([]);

    const { result } = renderHook(() => useAgentChat(accountId));

    await waitFor(() => {
      expect(result.current.aiConfigured).toBe(false);
    });

    // Configure the AI
    vi.mocked(api.getAiConfig).mockResolvedValue({
      provider: "OpenAi",
      model: "gpt-4o",
      has_key: true,
    });

    // Producer side (AiConfigSection / TranslationSection) dispatches a DOM
    // CustomEvent, not a Tauri event; the hook subscribes via window.addEventListener.
    act(() => {
      window.dispatchEvent(new CustomEvent("ai-config-changed"));
    });

    await waitFor(() => {
      expect(result.current.aiConfigured).toBe(true);
    });
  });

  it("cancels in-flight session load when accountId changes", async () => {
    vi.mocked(api.getAiConfig).mockResolvedValue({
      provider: "OpenAi",
      model: "gpt-4o",
      has_key: true,
    });

    let resolveSessions: (val: any) => void = () => {};
    const delayPromise = new Promise((resolve) => {
      resolveSessions = resolve;
    });

    vi.mocked(api.listAgentSessions).mockImplementation(() => delayPromise as Promise<AgentChatSession[]>);

    const { result, rerender } = renderHook(({ accId }) => useAgentChat(accId), {
      initialProps: { accId: "acc-1" },
    });

    // Rerender immediately with different accountId before first call resolves
    rerender({ accId: "acc-2" });

    // Resolve the first call with session-1
    act(() => {
      resolveSessions([
        {
          id: "session-1",
          account_id: "acc-1",
          title: "Session 1",
          created_at: new Date().toISOString(),
          updated_at: new Date().toISOString(),
        },
      ]);
    });

    // Sessions should not contain session-1 because the call was cancelled/ignored
    await waitFor(() => {
      expect(result.current.sessions).not.toEqual([
        expect.objectContaining({ id: "session-1" }),
      ]);
    });
  });

  it("accumulates streamingMessage chunks when agent-message-chunk event is received", async () => {
    let chunkListener: (event: { payload: { session_id: string; chunk: string } }) => void = () => {};
    vi.mocked(listen).mockImplementation((event, cb) => {
      if (event === "agent-message-chunk") {
        chunkListener = cb as (event: { payload: { session_id: string; chunk: string } }) => void;
      }
      return Promise.resolve(() => {});
    });

    vi.mocked(api.getAiConfig).mockResolvedValue({
      provider: "OpenAi",
      model: "gpt-4o",
      has_key: true,
    });
    vi.mocked(api.listAgentSessions).mockResolvedValue(mockSessions);
    vi.mocked(api.getAgentMessages).mockResolvedValue(mockMessages);

    const { result } = renderHook(() => useAgentChat(accountId));

    await waitFor(() => {
      expect(result.current.currentSessionId).toBe("session-1");
    });

    // Send a chunk event matching the current session
    act(() => {
      chunkListener({
        payload: { session_id: "session-1", chunk: "Hello" },
      });
    });
    expect(result.current.streamingMessage).toBe("Hello");

    // Send another chunk
    act(() => {
      chunkListener({
        payload: { session_id: "session-1", chunk: " world" },
      });
    });
    expect(result.current.streamingMessage).toBe("Hello world");
  });

  it("optimistic echo and single assistant msg append on send", async () => {
    vi.mocked(api.getAiConfig).mockResolvedValue({
      provider: "OpenAi",
      model: "gpt-4o",
      has_key: true,
    });
    vi.mocked(api.listAgentSessions).mockResolvedValue(mockSessions);
    vi.mocked(api.getAgentMessages).mockResolvedValue(mockMessages);
    
    const mockAssistantResponse: AgentChatMessage = {
      id: "msg-assistant-1",
      session_id: "session-1",
      role: "assistant",
      content: "Here is your response.",
      created_at: new Date().toISOString(),
    };
    
    let sendResolve: (msg: AgentChatMessage) => void = () => {};
    const sendPromise = new Promise<AgentChatMessage>((resolve) => {
      sendResolve = resolve;
    });
    
    vi.mocked(api.sendAgentMessage).mockImplementation(() => sendPromise);

    const { result } = renderHook(() => useAgentChat(accountId));

    await waitFor(() => {
      expect(result.current.currentSessionId).toBe("session-1");
    });

    // Start sending
    let handleSendPromise: Promise<void>;
    act(() => {
      handleSendPromise = result.current.handleSend("Hello assistant");
    });

    // 1. Verify optimistic echo exists in messages before API resolves
    expect(result.current.messages).toHaveLength(2); // mockMessages (1) + optimistic (1)
    expect(result.current.messages[1]).toMatchObject({
      role: "user",
      content: "Hello assistant",
    });
    expect(result.current.isSending).toBe(true);

    // Resolve API call
    await act(async () => {
      sendResolve(mockAssistantResponse);
      await handleSendPromise;
    });

    // 2. Verify messages contains exactly 3 messages now (mockMessages + optimistic user + assistant)
    expect(result.current.messages).toHaveLength(3);
    expect(result.current.messages[2]).toEqual(mockAssistantResponse);
    expect(result.current.isSending).toBe(false);
  });

  it("race regression (new-session path)", async () => {
    vi.mocked(api.getAiConfig).mockResolvedValue({
      provider: "OpenAi",
      model: "gpt-4o",
      has_key: true,
    });
    vi.mocked(api.listAgentSessions).mockResolvedValue([]); // no sessions initially

    const staleRow: AgentChatMessage = {
      id: "stale-row-from-db",
      session_id: "new-session-id",
      role: "user",
      content: "STALE - should never replace optimistic + assistant",
      created_at: new Date().toISOString(),
    };
    vi.mocked(api.getAgentMessages).mockResolvedValue([staleRow]);

    const newSession: AgentChatSession = {
      id: "new-session-id",
      account_id: accountId,
      title: "New chat",
      created_at: new Date().toISOString(),
      updated_at: new Date().toISOString(),
    };
    vi.mocked(api.createAgentSession).mockResolvedValue(newSession);

    const mockAssistantResponse: AgentChatMessage = {
      id: "msg-asst-new",
      session_id: "new-session-id",
      role: "assistant",
      content: "New session response.",
      created_at: new Date().toISOString(),
    };
    vi.mocked(api.sendAgentMessage).mockResolvedValue(mockAssistantResponse);

    const { result } = renderHook(() => useAgentChat(accountId));

    await waitFor(() => {
      expect(result.current.aiConfigured).toBe(true);
      expect(result.current.sessions).toHaveLength(0);
    });

    await act(async () => {
      await result.current.handleSend("First message in new session");
    });

    expect(result.current.currentSessionId).toBe("new-session-id");

    expect(api.getAgentMessages).not.toHaveBeenCalledWith(accountId, "new-session-id");

    expect(result.current.messages).toHaveLength(2);
    expect(result.current.messages[0]).toMatchObject({
      role: "user",
      content: "First message in new session",
    });
    expect(result.current.messages[1]).toEqual(mockAssistantResponse);

    expect(result.current.messages.some((m) => m.id === "stale-row-from-db")).toBe(false);
  });
});
