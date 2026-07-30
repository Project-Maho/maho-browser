import { render, screen, fireEvent } from "@testing-library/react";
import { describe, it, expect } from "vitest";
import { ChatMessageList } from "../ChatMessageList";
import type { AgentChatMessage } from "../../../types";

describe("ChatMessageList", () => {
  const mockMessages: AgentChatMessage[] = [
    {
      id: "msg-1",
      session_id: "session-1",
      role: "user",
      content: "Search recent invoice emails",
      created_at: new Date().toISOString(),
    },
    {
      id: "msg-2",
      session_id: "session-1",
      role: "assistant",
      content: "I will look that up.",
      created_at: new Date().toISOString(),
      tool_calls: JSON.stringify([{ id: "call-1", name: "search_emails", input: { query: "invoice" } }]),
    },
    {
      id: "msg-3",
      session_id: "session-1",
      role: "tool_result",
      content: JSON.stringify({
        tool_use_id: "call-1",
        result: JSON.stringify([{ id: "email-1", subject: "Invoice 123" }]),
      }),
      created_at: new Date().toISOString(),
    },
  ];

  it("renders messages and components correctly", () => {
    render(
      <ChatMessageList
        messages={mockMessages}
        isHistoryLoading={false}
        isSending={false}
      />
    );

    // Verify messages content rendered
    expect(screen.getByText("Search recent invoice emails")).toBeInTheDocument();
    expect(screen.getByText("I will look that up.")).toBeInTheDocument();

    // Verify tool calls rendered
    expect(screen.getByText(/Agent called tool:/)).toBeInTheDocument();
    
    // Verify tool result rendered
    expect(screen.getByText(/Used tool:/)).toBeInTheDocument();
    
    expect(screen.getAllByText("search_emails")).toHaveLength(2);
  });

  it("expand/collapse raw tool result on click", () => {
    render(
      <ChatMessageList
        messages={mockMessages}
        isHistoryLoading={false}
        isSending={false}
      />
    );

    const button = screen.getByRole("button", { name: /Used tool: search_emails/ });
    expect(button).toBeInTheDocument();

    // Pre-condition: Pre block is not present
    expect(screen.queryByText(/Invoice 123/)).not.toBeInTheDocument();

    // Click to expand
    fireEvent.click(button);
    expect(screen.getByText(/Invoice 123/)).toBeInTheDocument();

    // Click to collapse
    fireEvent.click(button);
    expect(screen.queryByText(/Invoice 123/)).not.toBeInTheDocument();
  });

  it("renders phaseText spinner when present", () => {
    render(
      <ChatMessageList
        messages={[]}
        isHistoryLoading={false}
        isSending={true}
        phaseText="Reading email..."
      />
    );

    expect(screen.getByTestId("phase-text")).toBeInTheDocument();
    expect(screen.getByText("Reading email...")).toBeInTheDocument();
  });
});
