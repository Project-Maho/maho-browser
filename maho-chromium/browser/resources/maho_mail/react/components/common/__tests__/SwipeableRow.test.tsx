import { render, screen, fireEvent } from "@testing-library/react";
import { describe, it, expect, vi } from "vitest";
import { SwipeableRow } from "../SwipeableRow";

describe("SwipeableRow", () => {
  it("renders children unchanged when swipe actions are disabled", () => {
    render(<SwipeableRow disabled> <div>Mail row</div> </SwipeableRow>);

    expect(screen.getByText("Mail row")).toBeInTheDocument();
    expect(screen.queryByText("Archive")).not.toBeInTheDocument();
  });

  it("calls the left swipe handler after a left drag past the threshold", () => {
    const onSwipeLeft = vi.fn();
    render(
      <SwipeableRow onSwipeLeft={onSwipeLeft} leftLabel="Archive message">
        <div>Mail row</div>
      </SwipeableRow>,
    );

    const row = screen.getByText("Mail row").parentElement as HTMLElement;
    Object.defineProperty(row, "setPointerCapture", { configurable: true, value: vi.fn() });
    fireEvent.pointerDown(row, { clientX: 120, pointerId: 1 });
    fireEvent.pointerMove(row, { clientX: 20, pointerId: 1 });
    fireEvent.pointerUp(row);

    expect(onSwipeLeft).toHaveBeenCalledTimes(1);
  });

  it("calls the right swipe handler after a right drag past the threshold", () => {
    const onSwipeRight = vi.fn();
    render(
      <SwipeableRow onSwipeRight={onSwipeRight} rightLabel="Mark read">
        <div>Mail row</div>
      </SwipeableRow>,
    );

    const row = screen.getByText("Mail row").parentElement as HTMLElement;
    Object.defineProperty(row, "setPointerCapture", { configurable: true, value: vi.fn() });
    fireEvent.pointerDown(row, { clientX: 20, pointerId: 2 });
    fireEvent.pointerMove(row, { clientX: 140, pointerId: 2 });
    fireEvent.pointerUp(row);

    expect(onSwipeRight).toHaveBeenCalledTimes(1);
  });
});
