import { render, screen, act } from "@testing-library/react";
import { describe, it, expect } from "vitest";
import { useToast, Toaster } from "../Toast";

function TestComponent() {
  const { toast } = useToast();
  return (
    <>
      <button onClick={() => toast("success", "Test message")}>Toast</button>
      <button
        onClick={() =>
          toast("error", {
            summary: "Structured error",
            detail: "This is some error detail",
          })
        }
      >
        Structured Toast
      </button>
      <Toaster />
    </>
  );
}

describe("Toast", () => {
  it("shows toast message when triggered", async () => {
    render(<TestComponent />);

    await act(async () => {
      screen.getByText("Toast").click();
    });

    expect(await screen.findByText("Test message")).toBeInTheDocument();
  });

  it("shows structured toast and toggle details", async () => {
    render(<TestComponent />);

    await act(async () => {
      screen.getByText("Structured Toast").click();
    });

    expect(await screen.findByText("Structured error")).toBeInTheDocument();

    // Detail should not be visible initially
    expect(screen.queryByText("This is some error detail")).not.toBeInTheDocument();

    // Click expand details button
    const expandBtn = screen.getByLabelText("Expand details");
    await act(async () => {
      expandBtn.click();
    });

    // Detail should now be visible
    expect(screen.getByText("This is some error detail")).toBeInTheDocument();

    // Click collapse details button
    const collapseBtn = screen.getByLabelText("Collapse details");
    await act(async () => {
      collapseBtn.click();
    });

    // Detail should not be visible again
    expect(screen.queryByText("This is some error detail")).not.toBeInTheDocument();
  });
});
