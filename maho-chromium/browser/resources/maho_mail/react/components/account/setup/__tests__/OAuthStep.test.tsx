import { cleanup, fireEvent, render, screen, waitFor } from "@testing-library/react";
import type { ReactNode } from "react";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { OAuthStep } from '../OAuthStep';

const apiMocks = vi.hoisted(() => ({
  startOAuth2: vi.fn<(provider: string) => Promise<string>>(),
  signInWithOAuth2: vi.fn<(provider: string) => Promise<{ account_id: string; email: string }>>(),
  syncFolders: vi.fn<(accountId: string) => Promise<readonly unknown[]>>(),
  listAccounts: vi.fn<() => Promise<{ id: string }[]>>(),
  cancelOAuth: vi.fn<(state: string) => Promise<boolean>>(),
}));

vi.mock("../../../../api", () => apiMocks);

vi.mock("../../../../events.js", () => ({
  listen: vi.fn((_event: string, _handler: (event: unknown) => void) =>
    Promise.resolve(() => {}),
  ),
}));

vi.mock("../../../ui", () => ({
  Button: ({ children, loading, onClick }: {
    readonly children: ReactNode;
    readonly loading?: boolean;
    readonly onClick: () => void;
  }) => (
    <button type="button" disabled={loading} onClick={onClick}>
      {children}
    </button>
  ),
}));

vi.mock("../../../ui/Toast", () => ({
  useToast: () => ({ toast: vi.fn() }),
}));

describe("OAuthStep", () => {
  afterEach(cleanup);

  beforeEach(() => {
    vi.clearAllMocks();
    apiMocks.startOAuth2.mockResolvedValue("state_from_cpp");
    apiMocks.listAccounts.mockResolvedValue([{ id: "account-1" }]);
    apiMocks.signInWithOAuth2.mockResolvedValue({ account_id: "account-1", email: "user@example.com" });
    apiMocks.syncFolders.mockResolvedValue([]);
    apiMocks.cancelOAuth.mockResolvedValue(true);
  });

  it("cancels the backend OAuth flow with its state when the user cancels", async () => {
    render(<OAuthStep provider="gmail" onBack={vi.fn()} />);
    fireEvent.click(screen.getByRole("button", { name: "Sign in with Google" }));
    await waitFor(() => expect(apiMocks.startOAuth2).toHaveBeenCalledTimes(1));

    fireEvent.click(await screen.findByRole("button", { name: "Cancel" }));

    expect(apiMocks.cancelOAuth).toHaveBeenCalledWith("state_from_cpp");
  });

  it("cancels the pending OAuth flow on Back and on unmount", async () => {
    const onBack = vi.fn();
    const { unmount } = render(<OAuthStep provider="gmail" onBack={onBack} />);
    fireEvent.click(screen.getByRole("button", { name: "Sign in with Google" }));
    await waitFor(() => expect(apiMocks.startOAuth2).toHaveBeenCalledTimes(1));
    await waitFor(() => expect(screen.getByRole("button", { name: "Cancel" })).toBeInTheDocument());

    fireEvent.click(screen.getByText("Back"));
    expect(onBack).toHaveBeenCalled();
    expect(apiMocks.cancelOAuth).toHaveBeenCalledTimes(1);

    unmount();
    expect(apiMocks.cancelOAuth).toHaveBeenCalledTimes(1);
  });

  it("renders sign in with Google for gmail provider", () => {
    render(<OAuthStep provider="gmail" onBack={vi.fn()} />);
    expect(screen.getByRole("heading", { name: "Sign in with Google" })).toBeInTheDocument();
  });

  it("renders sign in with Microsoft for outlook provider", () => {
    render(<OAuthStep provider="outlook" onBack={vi.fn()} />);
    expect(screen.getByRole("heading", { name: "Sign in with Microsoft" })).toBeInTheDocument();
  });

  it("renders back button", () => {
    render(<OAuthStep provider="gmail" onBack={vi.fn()} />);
    expect(screen.getByText("Back")).toBeInTheDocument();
  });

  it("calls onBack when back button clicked", () => {
    const onBack = vi.fn();
    render(<OAuthStep provider="gmail" onBack={onBack} />);
    fireEvent.click(screen.getByText("Back"));
    expect(onBack).toHaveBeenCalled();
  });

  it.each(["gmail", "outlook"] as const)(
    "starts typed OAuth exactly once for %s without requiring callers to consume state",
    async (provider) => {
      render(<OAuthStep provider={provider} onBack={vi.fn()} />);

      fireEvent.click(screen.getByRole("button", { name: /Sign in with/ }));

      await waitFor(() => {
        expect(apiMocks.startOAuth2).toHaveBeenCalledTimes(1);
      });
      expect(apiMocks.startOAuth2).toHaveBeenCalledWith(provider);
      expect(apiMocks.signInWithOAuth2).not.toHaveBeenCalled();
      expect(apiMocks.syncFolders).not.toHaveBeenCalled();
    },
  );

  it("renders a useful error when OAuth initiation rejects", async () => {
    apiMocks.startOAuth2.mockRejectedValueOnce(new Error("OAuth service unavailable"));
    render(<OAuthStep provider="gmail" onBack={vi.fn()} />);

    fireEvent.click(screen.getByRole("button", { name: "Sign in with Google" }));

    expect(await screen.findByText("OAuth service unavailable")).toBeInTheDocument();
  });

  it("advances via onSuccess when accounts-changed fires after browser approval", async () => {
    const { listen } = await import("../../../../events.js");
    const onSuccess = vi.fn();
    // Snapshot before sign-in has the existing account; the new one appears after approval.
    apiMocks.listAccounts
      .mockResolvedValueOnce([{ id: "existing" }])
      .mockResolvedValue([{ id: "existing" }, { id: "account-1" }]);
    render(<OAuthStep provider="gmail" onBack={vi.fn()} onSuccess={onSuccess} />);

    fireEvent.click(screen.getByRole("button", { name: "Sign in with Google" }));

    await waitFor(() => {
      expect(apiMocks.startOAuth2).toHaveBeenCalledTimes(1);
    });
    // Simulate the loopback listener onboarding the account in the
    // background: the helper emits accounts-changed once done.
    await waitFor(() => {
      expect(vi.mocked(listen)).toHaveBeenCalledWith(
        "accounts-changed",
        expect.any(Function),
      );
    });
    const handler = vi.mocked(listen).mock.calls[0]?.[1] as
      | ((event: { event: string; payload: unknown }) => void)
      | undefined;
    expect(handler).toBeDefined();
    await waitFor(async () => {
      await handler?.({ event: "accounts-changed", payload: {} });
    });
    await waitFor(() => {
      expect(onSuccess).toHaveBeenCalledTimes(1);
    });
    expect(onSuccess).toHaveBeenCalledWith("account-1");
  });
});
