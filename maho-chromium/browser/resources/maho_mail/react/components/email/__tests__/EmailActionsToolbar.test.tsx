import { describe, it, expect, vi, beforeEach } from "vitest";
import { render, screen, fireEvent, waitFor } from "@testing-library/react";
import {
  EmailActionsToolbar,
  clampContextMenuPosition,
  CONTEXT_MENU_WIDTH,
  CONTEXT_MENU_HEIGHT,
} from "../EmailActionsToolbar";

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

  it("clamps context menu within viewport minus menu size when opened near right or bottom edge", () => {
    render(<EmailActionsToolbar {...baseProps} />);
    const replyBtn = screen.getByRole("button", { name: /^Reply$/i });

    // Open near right and bottom edges of the viewport (jsdom default: 1024 x 768)
    fireEvent.contextMenu(replyBtn, { clientX: 1000, clientY: 750 });

    const menu = screen.getByRole("menu", { name: "Action options" });
    expect(menu).toBeInTheDocument();

    const expectedClampedX = window.innerWidth - CONTEXT_MENU_WIDTH; // 1024 - 200 = 824
    const expectedClampedY = window.innerHeight - CONTEXT_MENU_HEIGHT; // 768 - 120 = 648

    expect(menu.style.left).toBe(`${expectedClampedX}px`);
    expect(menu.style.top).toBe(`${expectedClampedY}px`);
  });

  it("preserves context menu position when opened safely within viewport bounds", () => {
    render(<EmailActionsToolbar {...baseProps} />);
    const replyBtn = screen.getByRole("button", { name: /^Reply$/i });

    fireEvent.contextMenu(replyBtn, { clientX: 150, clientY: 200 });

    const menu = screen.getByRole("menu", { name: "Action options" });
    expect(menu).toBeInTheDocument();
    expect(menu.style.left).toBe("150px");
    expect(menu.style.top).toBe("200px");
  });

  it("clamps context menu to 0 when opened at negative coordinates", () => {
    render(<EmailActionsToolbar {...baseProps} />);
    const replyBtn = screen.getByRole("button", { name: /^Reply$/i });

    fireEvent.contextMenu(replyBtn, { clientX: -25, clientY: -15 });

    const menu = screen.getByRole("menu", { name: "Action options" });
    expect(menu).toBeInTheDocument();
    expect(menu.style.left).toBe("0px");
    expect(menu.style.top).toBe("0px");
  });

  it("clamps context menu when triggered from overflow menu items near viewport boundary", async () => {
    render(
      <EmailActionsToolbar
        {...baseProps}
        onPin={vi.fn()}
      />,
    );

    fireEvent.click(screen.getByRole("button", { name: /More actions/i }));
    await waitFor(() => {
      expect(screen.getByRole("menuitem", { name: /Pin/i })).toBeInTheDocument();
    });

    fireEvent.contextMenu(screen.getByRole("menuitem", { name: /Pin/i }), {
      clientX: 1010,
      clientY: 760,
    });

    const menu = screen.getByRole("menu", { name: "Action options" });
    expect(menu).toBeInTheDocument();

    const expectedClampedX = window.innerWidth - CONTEXT_MENU_WIDTH; // 824
    const expectedClampedY = window.innerHeight - CONTEXT_MENU_HEIGHT; // 648

    expect(menu.style.left).toBe(`${expectedClampedX}px`);
    expect(menu.style.top).toBe(`${expectedClampedY}px`);
  });
});

describe("clampContextMenuPosition helper", () => {
  it("clamps coordinates exceeding viewport minus menu dimensions", () => {
    const pos = clampContextMenuPosition(950, 700, 200, 120, 1000, 750);
    expect(pos.x).toBe(800); // 1000 - 200
    expect(pos.y).toBe(630); // 750 - 120
    expect(pos.left).toBe(800);
    expect(pos.top).toBe(630);
  });

  it("clamps negative coordinates to 0", () => {
    const pos = clampContextMenuPosition(-50, -100, 200, 120, 1000, 750);
    expect(pos.x).toBe(0);
    expect(pos.y).toBe(0);
  });

  it("clamps to 0 when viewport is smaller than menu size without negative values", () => {
    const pos = clampContextMenuPosition(100, 100, 200, 120, 150, 80);
    expect(pos.x).toBe(0);
    expect(pos.y).toBe(0);
  });

  it("preserves coordinates strictly within valid bounds", () => {
    const pos = clampContextMenuPosition(350, 250, 200, 120, 1000, 750);
    expect(pos.x).toBe(350);
    expect(pos.y).toBe(250);
  });
});
