import { render, screen, fireEvent } from "@testing-library/react";
import { describe, it, expect, vi } from "vitest";
import { DelegateEmailDialog } from "../DelegateEmailDialog";
import { TestProviders } from "../../../test/mocks";

describe("DelegateEmailDialog", () => {
  it("renders the dialog fields when open", () => {
    render(
      <TestProviders>
        <DelegateEmailDialog isOpen={true} onClose={vi.fn()} onDelegate={vi.fn()} />
      </TestProviders>,
    );

    expect(screen.getByText("Delegate Email")).toBeInTheDocument();
    expect(screen.getByPlaceholderText("Delegate to email...")).toBeInTheDocument();
    expect(screen.getByPlaceholderText("Add a note (optional)...")).toBeInTheDocument();
    expect(screen.getByRole("button", { name: "Delegate" })).toBeDisabled();
  });

  it("submits trimmed email and note", () => {
    const onDelegate = vi.fn();
    render(
      <TestProviders>
        <DelegateEmailDialog isOpen={true} onClose={vi.fn()} onDelegate={onDelegate} />
      </TestProviders>,
    );

    fireEvent.change(screen.getByPlaceholderText("Delegate to email..."), {
      target: { value: "  teammate@example.com  " },
    });
    fireEvent.change(screen.getByPlaceholderText("Add a note (optional)..."), {
      target: { value: "  Please review this thread.  " },
    });
    fireEvent.click(screen.getByRole("button", { name: "Delegate" }));

    expect(onDelegate).toHaveBeenCalledWith("teammate@example.com", "Please review this thread.");
  });

  it("calls onClose from the cancel button", () => {
    const onClose = vi.fn();
    render(
      <TestProviders>
        <DelegateEmailDialog isOpen={true} onClose={onClose} onDelegate={vi.fn()} />
      </TestProviders>,
    );

    fireEvent.click(screen.getByRole("button", { name: "Cancel" }));

    expect(onClose).toHaveBeenCalledTimes(1);
  });
});
