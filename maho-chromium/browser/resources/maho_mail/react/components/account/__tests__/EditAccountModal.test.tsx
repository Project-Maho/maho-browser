import { render, screen, fireEvent, waitFor } from "@testing-library/react";
import { describe, it, expect, vi, beforeEach } from "vitest";
import { EditAccountModal } from "../EditAccountModal";
import { TestProviders } from "../../../test/mocks";
import type { AccountResponse } from "../../../types";

const { toastMock, getAccountMock, testConnectionMock, updateAccountMock } = vi.hoisted(() => ({
  toastMock: vi.fn(),
  getAccountMock: vi.fn(),
  testConnectionMock: vi.fn(),
  updateAccountMock: vi.fn(),
}));

vi.mock("../../../api", () => ({
  getAccount: getAccountMock,
  testConnection: testConnectionMock,
  updateAccount: updateAccountMock,
}));

vi.mock("../../ui/Toast", () => ({
  useToast: () => ({ toast: toastMock }),
}));

describe("EditAccountModal", () => {
  const account: AccountResponse = {
    id: "acc-1",
    email: "user@example.com",
    display_name: "User One",
    auth_type: "password",
    imap_host: "imap.example.com",
    imap_port: 993,
    imap_encryption: "Tls",
    smtp_host: "smtp.example.com",
    smtp_port: 587,
    smtp_encryption: "StartTls",
    username: "user@example.com",
    created_at: "2024-01-01T00:00:00Z",
    updated_at: "2024-01-01T00:00:00Z",
    auto_draft_enabled: false,
  };

  beforeEach(() => {
    vi.clearAllMocks();
    getAccountMock.mockResolvedValue(account);
    testConnectionMock.mockResolvedValue(true);
    updateAccountMock.mockResolvedValue(undefined);
  });

  it("returns null when closed", async () => {
    render(
      <EditAccountModal
        accountId="acc-1"
        isOpen={false}
        onClose={vi.fn()}
        onAccountUpdated={vi.fn()}
      />,
      { wrapper: TestProviders },
    );

    await waitFor(() => {
      expect(screen.queryByRole("dialog")).not.toBeInTheDocument();
    });
  });

  it("renders account fields after loading", async () => {
    render(
      <EditAccountModal
        accountId="acc-1"
        isOpen={true}
        onClose={vi.fn()}
        onAccountUpdated={vi.fn()}
      />,
      { wrapper: TestProviders },
    );

    expect(await screen.findByRole("dialog")).toBeInTheDocument();
    expect(await screen.findByRole("heading", { name: "Edit Account" })).toBeInTheDocument();
    expect(await screen.findByLabelText(/email address/i)).toHaveValue("user@example.com");
    expect(await screen.findByLabelText(/display name/i)).toHaveValue("User One");
    expect(await screen.findByLabelText(/username/i)).toHaveValue("user@example.com");
    expect(await screen.findByLabelText(/password/i)).toBeInTheDocument();
  });

  it("tests the connection with the current form values", async () => {
    render(
      <EditAccountModal
        accountId="acc-1"
        isOpen={true}
        onClose={vi.fn()}
        onAccountUpdated={vi.fn()}
      />,
      { wrapper: TestProviders },
    );

    expect(await screen.findByRole("button", { name: /test connection/i })).toBeEnabled();

    fireEvent.click(screen.getByRole("button", { name: /test connection/i }));

    await waitFor(() => {
      expect(testConnectionMock).toHaveBeenCalledWith(
        expect.objectContaining({
          email: "user@example.com",
          imap_host: "imap.example.com",
          smtp_host: "smtp.example.com",
        }),
      );
    });
  });

  it("saves edits and notifies the parent", async () => {
    const onClose = vi.fn();
    const onAccountUpdated = vi.fn();

    render(
      <EditAccountModal
        accountId="acc-1"
        isOpen={true}
        onClose={onClose}
        onAccountUpdated={onAccountUpdated}
      />,
      { wrapper: TestProviders },
    );

    expect(await screen.findByLabelText(/email address/i)).toBeInTheDocument();

    fireEvent.change(screen.getByLabelText(/email address/i), { target: { value: "updated@example.com" } });
    fireEvent.click(screen.getByRole("button", { name: /save/i }));

    await waitFor(() => {
      expect(updateAccountMock).toHaveBeenCalledWith(
        expect.objectContaining({
          id: "acc-1",
          email: "updated@example.com",
        }),
      );
      expect(onAccountUpdated).toHaveBeenCalledTimes(1);
      expect(onClose).toHaveBeenCalledTimes(1);
    });
  });
});
