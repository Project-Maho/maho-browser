import { render, screen, fireEvent } from "@testing-library/react";
import { describe, it, expect, vi } from "vitest";
import { SessionSelector } from "../SessionSelector";
import type { AgentChatSession } from "../../../types";

describe("SessionSelector", () => {
  const mockSessions: AgentChatSession[] = [
    {
      id: "session-1",
      account_id: "acc-1",
      title: "Chat 1",
      created_at: new Date().toISOString(),
      updated_at: new Date().toISOString(),
    },
    {
      id: "session-2",
      account_id: "acc-1",
      title: "Chat 2",
      created_at: new Date().toISOString(),
      updated_at: new Date().toISOString(),
    },
  ];

  it("renders the sessions list and highlights active session", () => {
    const handleSelect = vi.fn();
    const handleDelete = vi.fn();
    const handleNew = vi.fn();

    render(
      <SessionSelector
        sessions={mockSessions}
        currentSessionId="session-1"
        onSelect={handleSelect}
        onDelete={handleDelete}
        onNew={handleNew}
        disabled={false}
      />
    );

    expect(screen.getByText("Chat 1")).toBeInTheDocument();
    expect(screen.getByText("Chat 2")).toBeInTheDocument();
    expect(screen.getByText("New Chat")).toBeInTheDocument();
  });

  it("calls onSelect when a session is clicked", () => {
    const handleSelect = vi.fn();
    const handleDelete = vi.fn();
    const handleNew = vi.fn();

    render(
      <SessionSelector
        sessions={mockSessions}
        currentSessionId="session-1"
        onSelect={handleSelect}
        onDelete={handleDelete}
        onNew={handleNew}
        disabled={false}
      />
    );

    fireEvent.click(screen.getByText("Chat 2"));
    expect(handleSelect).toHaveBeenCalledWith("session-2");
  });

  it("calls onDelete when delete button is clicked", () => {
    const handleSelect = vi.fn();
    const handleDelete = vi.fn();
    const handleNew = vi.fn();

    render(
      <SessionSelector
        sessions={mockSessions}
        currentSessionId="session-1"
        onSelect={handleSelect}
        onDelete={handleDelete}
        onNew={handleNew}
        disabled={false}
      />
    );

    const deleteButtons = screen.getAllByRole("button", { name: /delete/i });
    expect(deleteButtons.length).toBe(2);

    fireEvent.click(deleteButtons[0]);
    expect(handleDelete).toHaveBeenCalledWith("session-1");
  });

  it("calls onNew when new chat button is clicked", () => {
    const handleSelect = vi.fn();
    const handleDelete = vi.fn();
    const handleNew = vi.fn();

    render(
      <SessionSelector
        sessions={mockSessions}
        currentSessionId="session-1"
        onSelect={handleSelect}
        onDelete={handleDelete}
        onNew={handleNew}
        disabled={false}
      />
    );

    fireEvent.click(screen.getByText("New Chat"));
    expect(handleNew).toHaveBeenCalled();
  });

  it("disables actions when disabled is true", () => {
    const handleSelect = vi.fn();
    const handleDelete = vi.fn();
    const handleNew = vi.fn();

    render(
      <SessionSelector
        sessions={mockSessions}
        currentSessionId="session-1"
        onSelect={handleSelect}
        onDelete={handleDelete}
        onNew={handleNew}
        disabled={true}
      />
    );

    // New Chat button should be disabled
    expect(screen.getByText("New Chat").closest("button")).toBeDisabled();

    // Session buttons should be disabled
    const sessionButton = screen.getByText("Chat 1").closest("button");
    expect(sessionButton).toBeDisabled();

    // Delete buttons should not be visible when disabled
    const deleteButton = screen.queryByRole("button", { name: /delete/i });
    expect(deleteButton).not.toBeInTheDocument();
  });
});
