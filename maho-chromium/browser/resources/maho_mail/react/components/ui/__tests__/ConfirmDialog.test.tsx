import { render, screen, act } from "@testing-library/react";
import { describe, it, expect } from "vitest";
import { ConfirmProvider, useConfirm } from "../ConfirmDialog";

function TestComponent() {
  const confirm = useConfirm();
  return <button onClick={async () => void confirm({ title: "Delete", message: "Proceed?" })}>Open</button>;
}

describe("ConfirmDialog", () => {
  it("renders confirm dialog when requested", async () => {
    render(
      <ConfirmProvider>
        <TestComponent />
      </ConfirmProvider>,
    );

    await act(async () => {
      screen.getByText("Open").click();
    });

    expect(screen.getByText("Delete")).toBeInTheDocument();
    expect(screen.getByText("Proceed?")).toBeInTheDocument();
  });
});
