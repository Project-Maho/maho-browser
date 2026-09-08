import { render, screen, fireEvent, act } from "@testing-library/react";
import { describe, it, expect, vi } from "vitest";
import { ConfirmProvider, useConfirm } from "../ConfirmDialog";

function TestConsumer({
  onConfirmReady,
}: {
  onConfirmReady: (confirm: ReturnType<typeof useConfirm>) => void;
}) {
  const confirm = useConfirm();
  onConfirmReady(confirm);
  return <div>Ready</div>;
}

describe("ConfirmDialog concurrency", () => {
  it("settles prior promise to false when a subsequent confirm() supersedes it", async () => {
    let confirmFn!: ReturnType<typeof useConfirm>;

    render(
      <ConfirmProvider>
        <TestConsumer onConfirmReady={(c) => { confirmFn = c; }} />
      </ConfirmProvider>,
    );

    const onFirstSettled = vi.fn();
    const onSecondSettled = vi.fn();

    let p1!: Promise<boolean>;

    await act(async () => {
      p1 = confirmFn({ title: "First Action", message: "Do first?" });
      p1.then(onFirstSettled);
    });

    // p1 dialog is now open, p1 is pending
    expect(screen.getByText("First Action")).toBeInTheDocument();
    expect(onFirstSettled).not.toHaveBeenCalled();

    // Trigger second confirm while first is still pending
    await act(async () => {
      const p2 = confirmFn({ title: "Second Action", message: "Do second?" });
      p2.then(onSecondSettled);
    });

    // The prior promise must have settled with false instead of leaking/hanging
    expect(onFirstSettled).toHaveBeenCalledTimes(1);
    expect(onFirstSettled).toHaveBeenCalledWith(false);

    // Second confirmation is now active
    expect(screen.getByText("Second Action")).toBeInTheDocument();
    expect(onSecondSettled).not.toHaveBeenCalled();

    // Confirm the second action
    await act(async () => {
      fireEvent.click(screen.getByRole("button", { name: "Confirm" }));
    });

    // Second promise resolves to true
    expect(onSecondSettled).toHaveBeenCalledTimes(1);
    expect(onSecondSettled).toHaveBeenCalledWith(true);
  });

  it("settles multiple superseded promises to false in rapid succession", async () => {
    let confirmFn!: ReturnType<typeof useConfirm>;

    render(
      <ConfirmProvider>
        <TestConsumer onConfirmReady={(c) => { confirmFn = c; }} />
      </ConfirmProvider>,
    );

    const settledResults: Array<{ id: number; val: boolean }> = [];

    await act(async () => {
      const p1 = confirmFn({ title: "Action 1", message: "Msg 1" });
      p1.then((val) => settledResults.push({ id: 1, val }));

      const p2 = confirmFn({ title: "Action 2", message: "Msg 2" });
      p2.then((val) => settledResults.push({ id: 2, val }));

      const p3 = confirmFn({ title: "Action 3", message: "Msg 3" });
      p3.then((val) => settledResults.push({ id: 3, val }));
    });

    // p1 and p2 should have been superseded and settled with false
    expect(settledResults).toEqual([
      { id: 1, val: false },
      { id: 2, val: false },
    ]);

    // Now cancel the remaining active dialog (Action 3)
    await act(async () => {
      fireEvent.click(screen.getByRole("button", { name: "Cancel" }));
    });

    expect(settledResults).toEqual([
      { id: 1, val: false },
      { id: 2, val: false },
      { id: 3, val: false },
    ]);
  });
});
