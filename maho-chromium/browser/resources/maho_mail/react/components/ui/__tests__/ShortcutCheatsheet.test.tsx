import { render, screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { describe, it, expect, vi } from "vitest";
import { ShortcutCheatsheet } from "../ShortcutCheatsheet";

describe("ShortcutCheatsheet", () => {
  it("does not render content when isOpen is false", () => {
    render(
      <ShortcutCheatsheet isOpen={false} onClose={vi.fn()} />,
    );
    expect(screen.queryByText("Keyboard Shortcuts")).not.toBeInTheDocument();
  });

  it("renders shortcut list when isOpen is true", () => {
    render(
      <ShortcutCheatsheet isOpen={true} onClose={vi.fn()} />,
    );
    expect(screen.getByText("Keyboard Shortcuts")).toBeInTheDocument();
  });

  it("calls onClose when close button is clicked", async () => {
    const user = userEvent.setup();
    const onClose = vi.fn();
    render(
      <ShortcutCheatsheet isOpen={true} onClose={onClose} />,
    );
    const closeButton = screen.getByRole("button", { name: "Close" });
    await user.click(closeButton);
    expect(onClose).toHaveBeenCalled();
  });

  it("shows all shortcut keys", () => {
    render(
      <ShortcutCheatsheet isOpen={true} onClose={vi.fn()} />,
    );
    expect(screen.getByText("c")).toBeInTheDocument();
    expect(screen.getByText("j")).toBeInTheDocument();
    expect(screen.getByText("k")).toBeInTheDocument();
    expect(screen.getByText("Enter")).toBeInTheDocument();
    expect(screen.getByText("/")).toBeInTheDocument();
    expect(screen.getByText("Escape")).toBeInTheDocument();
    expect(screen.getByText("r")).toBeInTheDocument();
  });
});
