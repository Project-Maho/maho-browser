import { render, screen, fireEvent, act } from "@testing-library/react";
import { describe, it, expect, vi, afterEach } from "vitest";
import { UndoSendToast } from "../UndoSendToast";

describe("UndoSendToast", () => {
  afterEach(() => {
    vi.useRealTimers();
  });

  it("shows the countdown and calls onComplete when the delay expires", async () => {
    const onComplete = vi.fn();
    vi.useFakeTimers();

    render(<UndoSendToast delaySeconds={2} onUndo={vi.fn()} onComplete={onComplete} />);

    expect(screen.getByText("Sending email…")).toBeInTheDocument();
    expect(screen.getByText("2")).toBeInTheDocument();

    await act(async () => {
      vi.advanceTimersByTime(2000);
    });

    expect(onComplete).toHaveBeenCalledTimes(1);
    expect(screen.queryByText("Sending email…")).not.toBeInTheDocument();
  });

  it("calls onUndo and cancels completion", async () => {
    const onUndo = vi.fn();
    const onComplete = vi.fn();
    vi.useFakeTimers();

    render(<UndoSendToast delaySeconds={3} onUndo={onUndo} onComplete={onComplete} />);

    fireEvent.click(screen.getByRole("button", { name: "Undo" }));

    expect(onUndo).toHaveBeenCalledTimes(1);
    expect(screen.queryByText("Sending email…")).not.toBeInTheDocument();

    await act(async () => {
      vi.advanceTimersByTime(3000);
    });

    expect(onComplete).not.toHaveBeenCalled();
  });
});
