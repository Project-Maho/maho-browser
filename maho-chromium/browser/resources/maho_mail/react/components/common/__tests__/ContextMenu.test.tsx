import { render, screen, fireEvent } from "@testing-library/react";
import { describe, it, expect, vi } from "vitest";
import { ContextMenu } from "../ContextMenu";
import { createMockEmail } from "../../../test/mocks";

describe("ContextMenu", () => {
  it("opens on right click and shows matching actions", () => {
    const email = createMockEmail({ is_read: false, is_starred: false });

    render(
      <ContextMenu
        email={email}
        onMarkRead={vi.fn()}
        onToggleStar={vi.fn()}
        onPin={vi.fn()}
        onSnooze={vi.fn()}
        onArchive={vi.fn()}
        onDelete={vi.fn()}
        onReply={vi.fn()}
        onForward={vi.fn()}
      >
        <div>mail row</div>
      </ContextMenu>,
    );

    fireEvent.contextMenu(screen.getByText("mail row"), { clientX: 24, clientY: 32 });

    expect(screen.getByRole("menu")).toBeInTheDocument();
    expect(screen.getByRole("menuitem", { name: "Reply" })).toBeInTheDocument();
    expect(screen.getByRole("menuitem", { name: "Mark as read" })).toBeInTheDocument();
    expect(screen.getByRole("menuitem", { name: "Star" })).toBeInTheDocument();
    expect(screen.getByRole("menuitem", { name: "Pin" })).toBeInTheDocument();
    expect(screen.getByRole("menuitem", { name: "Delete" })).toBeInTheDocument();
  });

  it("invokes the selected callback and closes the menu", () => {
    const onMarkRead = vi.fn();
    const email = createMockEmail({ is_read: false });

    render(
      <ContextMenu email={email} onMarkRead={onMarkRead}>
        <div>mail row</div>
      </ContextMenu>,
    );

    fireEvent.contextMenu(screen.getByText("mail row"), { clientX: 20, clientY: 20 });
    fireEvent.click(screen.getByRole("menuitem", { name: "Mark as read" }));

    expect(onMarkRead).toHaveBeenCalledWith(email.id);
    expect(screen.queryByRole("menu")).not.toBeInTheDocument();
  });

  it("closes when Escape is pressed", () => {
    const email = createMockEmail();

    render(
      <ContextMenu email={email} onArchive={vi.fn()}>
        <div>mail row</div>
      </ContextMenu>,
    );

    fireEvent.contextMenu(screen.getByText("mail row"), { clientX: 20, clientY: 20 });
    fireEvent.keyDown(screen.getByRole("menu"), { key: "Escape" });

    expect(screen.queryByRole("menu")).not.toBeInTheDocument();
  });
});
