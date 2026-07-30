import { render, screen, fireEvent, waitFor } from "@testing-library/react";
import { describe, it, expect, vi, beforeEach } from "vitest";
import { AgentPanel } from "../AgentPanel";
import * as api from "../../../api";
import { TestProviders } from "../../../test/mocks";
import type { AgentChatSession, AgentChatMessage } from "../../../types";

// Mock the API module
vi.mock("../../../api", () => ({
  getAiConfig: vi.fn(),
  listAgentSessions: vi.fn(),
  getAgentMessages: vi.fn(),
  createAgentSession: vi.fn(),
  deleteAgentSession: vi.fn(),
  sendAgentMessage: vi.fn(),
}));

// Mock useToast from ui/Toast
const mockToast = vi.fn();
vi.mock("../../ui/Toast", () => ({
  useToast: () => ({
    toast: mockToast,
  }),
  Toaster: () => null,
}));

describe("AgentPanel", () => {
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
    {
      id: "msg-2",
      session_id: "session-1",
      role: "assistant",
      content: "Hi there",
      created_at: new Date().toISOString(),
    },
  ];

  beforeEach(() => {
    vi.clearAllMocks();
    // Mock scrollTo which does not exist in jsdom environment
    Element.prototype.scrollTo = vi.fn();
  });

  it("renders 'AI Not Configured' when getAiConfig returns null/throws", async () => {
    vi.mocked(api.getAiConfig).mockResolvedValue(null);
    vi.mocked(api.listAgentSessions).mockResolvedValue([]);

    render(
      <TestProviders>
        <AgentPanel accountId={accountId} onClose={vi.fn()} onOpenSettings={vi.fn()} />
      </TestProviders>
    );

    await waitFor(() => {
      expect(screen.getByText("AI Not Configured")).toBeInTheDocument();
    });
  });

  it("renders session list when configured", async () => {
    vi.mocked(api.getAiConfig).mockResolvedValue({
      provider: "OpenAi",
      model: "gpt-4o",
      has_key: true,
    });
    vi.mocked(api.listAgentSessions).mockResolvedValue(mockSessions);
    vi.mocked(api.getAgentMessages).mockResolvedValue([]);

    render(
      <TestProviders>
        <AgentPanel accountId={accountId} onClose={vi.fn()} onOpenSettings={vi.fn()} />
      </TestProviders>
    );

    await waitFor(() => {
      expect(screen.getAllByText("Chat 1").length).toBeGreaterThan(0);
    });
  });

  it("calls api.sendAgentMessage on submit", async () => {
    vi.mocked(api.getAiConfig).mockResolvedValue({
      provider: "OpenAi",
      model: "gpt-4o",
      has_key: true,
    });
    vi.mocked(api.listAgentSessions).mockResolvedValue(mockSessions);
    vi.mocked(api.getAgentMessages).mockResolvedValue(mockMessages);
    vi.mocked(api.sendAgentMessage).mockResolvedValue({
      id: "msg-4",
      session_id: "session-1",
      role: "assistant",
      content: "Your name is User.",
      created_at: new Date().toISOString(),
    });

    render(
      <TestProviders>
        <AgentPanel accountId={accountId} onClose={vi.fn()} onOpenSettings={vi.fn()} />
      </TestProviders>
    );

    // Wait for the active session to load and show messages
    await waitFor(() => {
      expect(screen.getByText("Hello")).toBeInTheDocument();
    });

    const textarea = screen.getByPlaceholderText(/ask maho agent/i);
    const sendButton = screen.getByLabelText(/send message/i);

    fireEvent.change(textarea, { target: { value: "What is my name?" } });
    fireEvent.click(sendButton);

    await waitFor(() => {
      expect(api.sendAgentMessage).toHaveBeenCalledWith("acc-123", "session-1", "What is my name?");
    });
  });

  it("shows error toast on API failure", async () => {
    vi.mocked(api.getAiConfig).mockResolvedValue({
      provider: "OpenAi",
      model: "gpt-4o",
      has_key: true,
    });
    vi.mocked(api.listAgentSessions).mockResolvedValue(mockSessions);
    vi.mocked(api.getAgentMessages).mockResolvedValue(mockMessages);
    vi.mocked(api.sendAgentMessage).mockRejectedValue(new Error("API Error"));

    render(
      <TestProviders>
        <AgentPanel accountId={accountId} onClose={vi.fn()} onOpenSettings={vi.fn()} />
      </TestProviders>
    );

    await waitFor(() => {
      expect(screen.getByText("Hello")).toBeInTheDocument();
    });

    const textarea = screen.getByPlaceholderText(/ask maho agent/i);
    const sendButton = screen.getByLabelText(/send message/i);

    fireEvent.change(textarea, { target: { value: "Hello" } });
    fireEvent.click(sendButton);

    await waitFor(() => {
      expect(mockToast).toHaveBeenCalledWith("error", expect.stringContaining("API Error"));
    });
  });

  it("switches sessions when accountId prop changes", async () => {
    vi.mocked(api.getAiConfig).mockResolvedValue({
      provider: "OpenAi",
      model: "gpt-4o",
      has_key: true,
    });
    vi.mocked(api.listAgentSessions).mockResolvedValue(mockSessions);
    vi.mocked(api.getAgentMessages).mockResolvedValue([]);

    const { rerender } = render(
      <TestProviders>
        <AgentPanel accountId={accountId} onClose={vi.fn()} onOpenSettings={vi.fn()} />
      </TestProviders>
    );

    await waitFor(() => {
      expect(api.listAgentSessions).toHaveBeenCalledWith(accountId);
    });

    // Rerender with a different accountId
    const newAccountId = "acc-456";
    vi.mocked(api.listAgentSessions).mockResolvedValue([
      {
        id: "session-2",
        account_id: newAccountId,
        title: "Chat 2",
        created_at: new Date().toISOString(),
        updated_at: new Date().toISOString(),
      },
    ]);

    rerender(
      <TestProviders>
        <AgentPanel accountId={newAccountId} onClose={vi.fn()} onOpenSettings={vi.fn()} />
      </TestProviders>
    );

    await waitFor(() => {
      expect(api.listAgentSessions).toHaveBeenCalledWith(newAccountId);
    });
  });
});
