import { render, screen, fireEvent } from "@testing-library/react";
import { describe, it, expect, vi } from "vitest";
import { ChatInput } from "../ChatInput";

describe("ChatInput", () => {
  it("calls onSend and clears after send", () => {
    const handleSend = vi.fn();
    render(<ChatInput onSend={handleSend} disabled={false} />);

    const textarea = screen.getByPlaceholderText(/ask maho agent/i);
    const sendButton = screen.getByLabelText(/send message/i);

    // Button should be disabled initially when input is empty
    expect(sendButton).toBeDisabled();

    // Type a message
    fireEvent.change(textarea, { target: { value: "Hello world" } });

    // Send button should be enabled
    expect(sendButton).not.toBeDisabled();

    // Click send
    fireEvent.click(sendButton);

    expect(handleSend).toHaveBeenCalledWith("Hello world");
    expect(textarea).toHaveValue("");
  });

  it("disables input and button when disabled prop is true", () => {
    const handleSend = vi.fn();
    render(<ChatInput onSend={handleSend} disabled={true} />);

    const textarea = screen.getByPlaceholderText(/ask maho agent/i);
    const sendButton = screen.getByLabelText(/send message/i);

    expect(textarea).toBeDisabled();
    expect(sendButton).toBeDisabled();
  });

  it("renders Stop button when loading and onCancel is provided", () => {
    const handleSend = vi.fn();
    const handleCancel = vi.fn();
    render(
      <ChatInput
        onSend={handleSend}
        disabled={true}
        loading={true}
        onCancel={handleCancel}
      />
    );

    const stopButton = screen.getByLabelText("Cancel generating message");
    expect(stopButton).toBeInTheDocument();

    fireEvent.click(stopButton);
    expect(handleCancel).toHaveBeenCalled();
  });
});
