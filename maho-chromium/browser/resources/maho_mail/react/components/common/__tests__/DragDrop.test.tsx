import { render, screen, fireEvent, act } from "@testing-library/react";
import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import { DraggableEmail, DropTargetFolder } from "../DragDrop";

describe("DragDrop", () => {
  beforeEach(() => {
    vi.useFakeTimers();
    Object.defineProperty(HTMLElement.prototype, "setPointerCapture", {
      configurable: true,
      value: vi.fn(),
    });
  });

  afterEach(() => {
    vi.useRealTimers();
    vi.restoreAllMocks();
  });

  it("only becomes draggable after the hold delay", async () => {
    render(
      <DraggableEmail emailId="email-1">
        <div>Draggable row</div>
      </DraggableEmail>,
    );

    const wrapper = screen.getByText("Draggable row").parentElement as HTMLElement;
    expect(wrapper).toHaveAttribute("draggable", "false");

    fireEvent.mouseDown(wrapper);
    await act(async () => {
      vi.advanceTimersByTime(300);
    });

    expect(wrapper).toHaveAttribute("draggable", "true");
  });

  it("writes the email id to the drag payload after activation", async () => {
    const dataTransfer = {
      setData: vi.fn(),
      effectAllowed: "",
    };

    render(
      <DraggableEmail emailId="email-42">
        <div>Draggable row</div>
      </DraggableEmail>,
    );

    const wrapper = screen.getByText("Draggable row").parentElement as HTMLElement;
    fireEvent.mouseDown(wrapper);
    await act(async () => {
      vi.advanceTimersByTime(300);
    });

    fireEvent.dragStart(wrapper, { dataTransfer });

    expect(dataTransfer.setData).toHaveBeenCalledWith("text/plain", "email-42");
    expect(dataTransfer.effectAllowed).toBe("move");
  });

  it("drops an email into the target folder", () => {
    const onDrop = vi.fn();
    const dataTransfer = {
      dropEffect: "",
      getData: vi.fn().mockReturnValue("email-9"),
    };

    render(
      <DropTargetFolder folderId="archive" onDrop={onDrop}>
        <div>Folder</div>
      </DropTargetFolder>,
    );

    const target = screen.getByText("Folder").parentElement as HTMLElement;
    fireEvent.dragOver(target, { dataTransfer });
    expect(target.className).toContain("ring-primary/50");

    fireEvent.drop(target, { dataTransfer });

    expect(onDrop).toHaveBeenCalledWith("email-9", "archive");
  });
});
