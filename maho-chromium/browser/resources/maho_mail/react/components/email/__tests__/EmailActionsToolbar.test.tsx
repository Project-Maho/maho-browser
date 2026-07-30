import { describe, it, expect, vi, beforeEach } from "vitest";
import { render, screen, fireEvent, waitFor } from "@testing-library/react";
import { EmailActionsToolbar } from "../EmailActionsToolbar";

describe("EmailActionsToolbar", () => {
  const baseProps = {
    onReply: vi.fn(),
    onReplyWithAI: vi.fn(),
    onForward: vi.fn(),
    onPrint: vi.fn(),
    onDelete: vi.fn(),
  };

  beforeEach(() => {
    vi.clearAllMocks();
  });

  it("exposes only the primary actions (Reply / Reply with AI / Forward / Delete) plus a 'More actions' overflow", () => {
    render(<EmailActionsToolbar {...baseProps} />);

    expect(screen.getByRole("button", { name: /^Reply$/i })).toBeInTheDocument();
    expect(screen.getByRole("button", { name: /Reply with AI/i })).toBeInTheDocument();
    expect(screen.getByRole("button", { name: /^Forward$/i })).toBeInTheDocument();
    expect(screen.getByRole("button", { name: /More actions/i })).toBeInTheDocument();
    expect(screen.getByRole("button", { name: /^Delete$/i })).toBeInTheDocument();

    expect(screen.queryByRole("button", { name: /^Print$/i })).toBeNull();
    expect(screen.queryByRole("button", { name: /^Pin$/i })).toBeNull();
    expect(screen.queryByRole("button", { name: /^Snooze$/i })).toBeNull();
    expect(screen.queryByRole("button", { name: /^Remind$/i })).toBeNull();
    expect(screen.queryByRole("button", { name: /^Delegate$/i })).toBeNull();
    expect(screen.queryByRole("button", { name: /Pop Out/i })).toBeNull();
  });

  it("reveals secondary actions when overflow is opened", async () => {
    render(
      <EmailActionsToolbar
        {...baseProps}
        onPin={vi.fn()}
        onSnooze={vi.fn()}
        onReminder={vi.fn()}
        onDelegate={vi.fn()}
        onToggleMute={vi.fn()}
        onPopOut={vi.fn()}
      />,
    );
    fireEvent.click(screen.getByRole("button", { name: /More actions/i }));

    await waitFor(() => {
      expect(screen.getByRole("menuitem", { name: /Print/i })).toBeInTheDocument();
    });
    expect(screen.getByRole("menuitem", { name: /Pin/i })).toBeInTheDocument();
    expect(screen.getByRole("menuitem", { name: /Snooze/i })).toBeInTheDocument();
    expect(screen.getByRole("menuitem", { name: /Remind/i })).toBeInTheDocument();
    expect(screen.getByRole("menuitem", { name: /Delegate/i })).toBeInTheDocument();
    expect(screen.getByRole("menuitem", { name: /Mute/i })).toBeInTheDocument();
    expect(screen.getByRole("menuitem", { name: /Pop Out/i })).toBeInTheDocument();
  });

  it("hides overflow menu items whose handlers are not provided", async () => {
    render(<EmailActionsToolbar {...baseProps} />);
    fireEvent.click(screen.getByRole("button", { name: /More actions/i }));

    await waitFor(() => {
      expect(screen.getByRole("menuitem", { name: /Print/i })).toBeInTheDocument();
    });
    expect(screen.queryByRole("menuitem", { name: /^Pin$/i })).toBeNull();
    expect(screen.queryByRole("menuitem", { name: /^Snooze$/i })).toBeNull();
    expect(screen.queryByRole("menuitem", { name: /^Remind$/i })).toBeNull();
    expect(screen.queryByRole("menuitem", { name: /^Delegate$/i })).toBeNull();
    expect(screen.queryByRole("menuitem", { name: /^Mute$/i })).toBeNull();
    expect(screen.queryByRole("menuitem", { name: /Pop Out/i })).toBeNull();
  });

  it("invokes the right handler when each primary action is clicked", () => {
    render(<EmailActionsToolbar {...baseProps} />);

    fireEvent.click(screen.getByRole("button", { name: /^Reply$/i }));
    fireEvent.click(screen.getByRole("button", { name: /Reply with AI/i }));
    fireEvent.click(screen.getByRole("button", { name: /^Forward$/i }));
    fireEvent.click(screen.getByRole("button", { name: /^Delete$/i }));

    expect(baseProps.onReply).toHaveBeenCalledTimes(1);
    expect(baseProps.onReplyWithAI).toHaveBeenCalledTimes(1);
    expect(baseProps.onForward).toHaveBeenCalledTimes(1);
    expect(baseProps.onDelete).toHaveBeenCalledTimes(1);
  });

  it("dispatches overflow menu callbacks correctly", async () => {
    const onPin = vi.fn();
    const onPopOut = vi.fn();
    render(
      <EmailActionsToolbar
        {...baseProps}
        onPin={onPin}
        onPopOut={onPopOut}
      />,
    );
    fireEvent.click(screen.getByRole("button", { name: /More actions/i }));

    await waitFor(() => {
      expect(screen.getByRole("menuitem", { name: /Pin/i })).toBeInTheDocument();
    });

    fireEvent.click(screen.getByRole("menuitem", { name: /Pin/i }));
    expect(onPin).toHaveBeenCalledTimes(1);

    fireEvent.click(screen.getByRole("button", { name: /More actions/i }));
    await waitFor(() => {
      expect(screen.getByRole("menuitem", { name: /Pop Out/i })).toBeInTheDocument();
    });
    fireEvent.click(screen.getByRole("menuitem", { name: /Pop Out/i }));
    expect(onPopOut).toHaveBeenCalledTimes(1);
  });

  it("renders the mute item label flipped to 'Unmute' when isThreadMuted is true", async () => {
    render(
      <EmailActionsToolbar
        {...baseProps}
        onToggleMute={vi.fn()}
        isThreadMuted
      />,
    );
    fireEvent.click(screen.getByRole("button", { name: /More actions/i }));

    await waitFor(() => {
      expect(screen.getByRole("menuitem", { name: /Unmute/i })).toBeInTheDocument();
    });
  });

  it("disables Reply with AI while draftLoading is true", () => {
    render(<EmailActionsToolbar {...baseProps} draftLoading />);
    expect(screen.getByRole("button", { name: /Reply with AI/i })).toBeDisabled();
  });
});
