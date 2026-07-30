import { describe, it, expect, vi, beforeEach } from "vitest";
import { render, screen, fireEvent, waitFor } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { CommandPalette } from "../CommandPalette";
import type { PaletteCommand } from "../../../hooks/useCommandRegistry";

vi.mock("../../../hooks/useFocusTrap", () => ({ useFocusTrap: vi.fn() }));

// Mock scrollIntoView which is not implemented in JSDOM
Element.prototype.scrollIntoView = vi.fn();

const makeCommand = (overrides: Partial<PaletteCommand> = {}): PaletteCommand => ({
  id: `cmd-${Math.random()}`,
  label: "Test Command",
  category: "mail",
  handler: vi.fn(),
  ...overrides,
});

describe("CommandPalette", () => {
  const onClose = vi.fn();

  beforeEach(() => {
    onClose.mockClear();
  });

  it("not rendered when closed", () => {
    const { container } = render(
      <CommandPalette open={false} onClose={onClose} commands={[]} />
    );
    expect(container.firstChild).toBeNull();
  });

  it("renders when open", () => {
    render(<CommandPalette open={true} onClose={onClose} commands={[]} />);
    expect(screen.getByRole("dialog")).toBeInTheDocument();
    expect(screen.getByPlaceholderText("Type a command...")).toBeInTheDocument();
  });

  it("shows commands list", () => {
    const commands: PaletteCommand[] = [
      makeCommand({ id: "cmd-1", label: "Compose Email", category: "compose" }),
      makeCommand({ id: "cmd-2", label: "Search Mail", category: "search" }),
      makeCommand({ id: "cmd-3", label: "View Inbox", category: "view" }),
    ];

    render(<CommandPalette open={true} onClose={onClose} commands={commands} />);
    
    const options = screen.getAllByRole("option");
    expect(options).toHaveLength(3);
    expect(screen.getByText("Compose Email")).toBeInTheDocument();
    expect(screen.getByText("Search Mail")).toBeInTheDocument();
    expect(screen.getByText("View Inbox")).toBeInTheDocument();
  });

  it("search input filters commands", async () => {
    const user = userEvent.setup();
    const commands: PaletteCommand[] = [
      makeCommand({ id: "cmd-1", label: "Compose Email", category: "compose" }),
      makeCommand({ id: "cmd-2", label: "Search Mail", category: "search" }),
      makeCommand({ id: "cmd-3", label: "View Inbox", category: "view" }),
    ];

    render(<CommandPalette open={true} onClose={onClose} commands={commands} />);
    
    const input = screen.getByLabelText("Search commands");
    await user.type(input, "compose");

    await waitFor(() => {
      const options = screen.getAllByRole("option");
      expect(options).toHaveLength(1);
    });
    expect(screen.getByText("Compose Email")).toBeInTheDocument();
    expect(screen.queryByText("Search Mail")).not.toBeInTheDocument();
  });

  it("shows no results message when no commands match", async () => {
    const user = userEvent.setup();
    const commands: PaletteCommand[] = [
      makeCommand({ id: "cmd-1", label: "Compose Email", category: "compose" }),
    ];

    render(<CommandPalette open={true} onClose={onClose} commands={commands} />);
    
    const input = screen.getByLabelText("Search commands");
    await user.type(input, "xyznonexistent");

    await waitFor(() => {
      expect(screen.getByText("No commands found")).toBeInTheDocument();
    });
    expect(screen.queryByRole("option")).not.toBeInTheDocument();
  });

  it("closes palette on Escape key", () => {
    render(<CommandPalette open={true} onClose={onClose} commands={[]} />);
    
    const dialog = screen.getByRole("dialog");
    fireEvent.keyDown(dialog, { key: "Escape" });

    expect(onClose).toHaveBeenCalled();
  });

  it("closes palette on backdrop click", () => {
    render(<CommandPalette open={true} onClose={onClose} commands={[]} />);
    
    const dialog = screen.getByRole("dialog");
    fireEvent.click(dialog);

    expect(onClose).toHaveBeenCalled();
  });

  it("navigates down with ArrowDown key", () => {
    const commands: PaletteCommand[] = [
      makeCommand({ id: "cmd-1", label: "First Command", category: "mail" }),
      makeCommand({ id: "cmd-2", label: "Second Command", category: "mail" }),
      makeCommand({ id: "cmd-3", label: "Third Command", category: "mail" }),
    ];

    render(<CommandPalette open={true} onClose={onClose} commands={commands} />);
    
    const options = screen.getAllByRole("option");
    expect(options[0]).toHaveAttribute("aria-selected", "true");
    expect(options[1]).toHaveAttribute("aria-selected", "false");

    const dialog = screen.getByRole("dialog");
    fireEvent.keyDown(dialog, { key: "ArrowDown" });

    expect(options[0]).toHaveAttribute("aria-selected", "false");
    expect(options[1]).toHaveAttribute("aria-selected", "true");
  });

  it("navigates up with ArrowUp key", () => {
    const commands: PaletteCommand[] = [
      makeCommand({ id: "cmd-1", label: "First Command", category: "mail" }),
      makeCommand({ id: "cmd-2", label: "Second Command", category: "mail" }),
    ];

    render(<CommandPalette open={true} onClose={onClose} commands={commands} />);
    
    const dialog = screen.getByRole("dialog");
    fireEvent.keyDown(dialog, { key: "ArrowDown" });

    const options = screen.getAllByRole("option");
    expect(options[1]).toHaveAttribute("aria-selected", "true");

    fireEvent.keyDown(dialog, { key: "ArrowUp" });

    expect(options[0]).toHaveAttribute("aria-selected", "true");
    expect(options[1]).toHaveAttribute("aria-selected", "false");
  });

  it("ArrowDown does not go past end of list", () => {
    const commands: PaletteCommand[] = [
      makeCommand({ id: "cmd-1", label: "First Command", category: "mail" }),
      makeCommand({ id: "cmd-2", label: "Second Command", category: "mail" }),
    ];

    render(<CommandPalette open={true} onClose={onClose} commands={commands} />);
    
    const dialog = screen.getByRole("dialog");
    fireEvent.keyDown(dialog, { key: "ArrowDown" });
    fireEvent.keyDown(dialog, { key: "ArrowDown" });
    fireEvent.keyDown(dialog, { key: "ArrowDown" });

    const options = screen.getAllByRole("option");
    expect(options[1]).toHaveAttribute("aria-selected", "true");
  });

  it("ArrowUp does not go below 0", () => {
    const commands: PaletteCommand[] = [
      makeCommand({ id: "cmd-1", label: "First Command", category: "mail" }),
      makeCommand({ id: "cmd-2", label: "Second Command", category: "mail" }),
    ];

    render(<CommandPalette open={true} onClose={onClose} commands={commands} />);
    
    const dialog = screen.getByRole("dialog");
    fireEvent.keyDown(dialog, { key: "ArrowUp" });
    fireEvent.keyDown(dialog, { key: "ArrowUp" });

    const options = screen.getAllByRole("option");
    expect(options[0]).toHaveAttribute("aria-selected", "true");
  });

  it("executes command on Enter key", () => {
    const handler = vi.fn();
    const commands: PaletteCommand[] = [
      makeCommand({ id: "cmd-1", label: "First Command", category: "mail", handler }),
      makeCommand({ id: "cmd-2", label: "Second Command", category: "mail" }),
    ];

    render(<CommandPalette open={true} onClose={onClose} commands={commands} />);
    
    const dialog = screen.getByRole("dialog");
    fireEvent.keyDown(dialog, { key: "Enter" });

    expect(handler).toHaveBeenCalled();
    expect(onClose).toHaveBeenCalled();
  });

  it("executes command on click", async () => {
    const user = userEvent.setup();
    const handler = vi.fn();
    const commands: PaletteCommand[] = [
      makeCommand({ id: "cmd-1", label: "Test Command", category: "mail", handler }),
    ];

    render(<CommandPalette open={true} onClose={onClose} commands={commands} />);
    
    const option = screen.getByRole("option");
    await user.click(option);

    expect(handler).toHaveBeenCalled();
    expect(onClose).toHaveBeenCalled();
  });

  it("shows category headers when query is empty", () => {
    const commands: PaletteCommand[] = [
      makeCommand({ id: "cmd-1", label: "Compose Email", category: "compose" }),
      makeCommand({ id: "cmd-2", label: "Search Mail", category: "search" }),
      makeCommand({ id: "cmd-3", label: "View Inbox", category: "view" }),
    ];

    render(<CommandPalette open={true} onClose={onClose} commands={commands} />);
    
    expect(screen.getByText("Compose")).toBeInTheDocument();
    expect(screen.getByText("Search")).toBeInTheDocument();
    expect(screen.getByText("View")).toBeInTheDocument();
  });

  it("filters out disabled commands", () => {
    const enabledHandler = vi.fn();
    const disabledHandler = vi.fn();
    const commands: PaletteCommand[] = [
      makeCommand({ id: "cmd-1", label: "Enabled Command", category: "mail", handler: enabledHandler }),
      makeCommand({ id: "cmd-2", label: "Disabled Command", category: "mail", handler: disabledHandler, enabled: () => false }),
    ];

    render(<CommandPalette open={true} onClose={onClose} commands={commands} />);
    
    const options = screen.getAllByRole("option");
    expect(options).toHaveLength(1);
    expect(screen.getByText("Enabled Command")).toBeInTheDocument();
    expect(screen.queryByText("Disabled Command")).not.toBeInTheDocument();
  });

  it("shows shortcut badge when command has shortcut", () => {
    const commands: PaletteCommand[] = [
      makeCommand({ id: "cmd-1", label: "New Message", category: "compose", shortcut: "⌘N" }),
    ];

    render(<CommandPalette open={true} onClose={onClose} commands={commands} />);
    
    expect(screen.getByText("⌘N")).toBeInTheDocument();
  });
});
