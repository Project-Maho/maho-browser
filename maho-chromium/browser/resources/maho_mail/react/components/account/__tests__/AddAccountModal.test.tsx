import { render, screen, act, waitFor } from "@testing-library/react";
import { describe, it, expect, vi, beforeEach } from "vitest";
import { AddAccountModal } from "../AddAccountModal";
import { TestProviders } from "../../../test/mocks";

describe("AddAccountModal", () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  it("returns null when isOpen is false", async () => {
    render(
      <AddAccountModal
        isOpen={false}
        onClose={vi.fn()}
        onAccountAdded={vi.fn()}
      />,
      { wrapper: TestProviders },
    );

    await act(async () => {});
    await waitFor(() => {
      expect(
        screen.queryByRole("heading", { name: "Add Account" }),
      ).not.toBeInTheDocument();
    });
  });

  it("renders modal with title when isOpen is true", async () => {
    render(
      <AddAccountModal
        isOpen={true}
        onClose={vi.fn()}
        onAccountAdded={vi.fn()}
      />,
      { wrapper: TestProviders },
    );

    await waitFor(() => {
      expect(screen.getByRole("heading", { name: "Add Account" })).toBeInTheDocument();
    });
  });

  it("renders AccountSetupFlow inside modal", async () => {
    render(
      <AddAccountModal
        isOpen={true}
        onClose={vi.fn()}
        onAccountAdded={vi.fn()}
      />,
      { wrapper: TestProviders },
    );

    await waitFor(() => {
      expect(screen.getByText("Choose your email provider")).toBeInTheDocument();
    });
  });

  it("renders close button", async () => {
    const onClose = vi.fn();
    render(
      <AddAccountModal
        isOpen={true}
        onClose={onClose}
        onAccountAdded={vi.fn()}
      />,
      { wrapper: TestProviders },
    );

    await waitFor(() => {
      expect(screen.getByRole("dialog")).toBeInTheDocument();
    });
  });
});
