import { renderHook, act } from "@testing-library/react";
import { describe, it, expect, vi, beforeEach } from "vitest";
import { onOpenUrl } from "@tauri-apps/plugin-deep-link";
import { useDeepLink } from "../useDeepLink";

vi.mock("@tauri-apps/plugin-deep-link", () => ({ onOpenUrl: vi.fn() }));

const onOpenUrlMock = vi.mocked(onOpenUrl);
let deepLinkHandler: ((urls: string[]) => void) | null = null;

describe("useDeepLink", () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  it("registers a deep link listener and unlistens on cleanup", async () => {
    const unlisten = vi.fn();
    onOpenUrlMock.mockImplementationOnce((handler) => {
      deepLinkHandler = handler;
      return Promise.resolve(unlisten);
    });

    const onNavigateToEmail = vi.fn();
    const onComposeToRecipient = vi.fn();

    const { unmount } = renderHook(() =>
      useDeepLink({ onNavigateToEmail, onComposeToRecipient })
    );

    await act(async () => {
      await Promise.resolve();
    });

    expect(onOpenUrlMock).toHaveBeenCalledTimes(1);
    unmount();

    await act(async () => {
      await Promise.resolve();
    });

    expect(unlisten).toHaveBeenCalledTimes(1);
  });

  it("routes email and compose URLs to the provided handlers", async () => {
    const unlisten = vi.fn();
    onOpenUrlMock.mockImplementationOnce((handler) => {
      deepLinkHandler = handler;
      return Promise.resolve(unlisten);
    });

    const onNavigateToEmail = vi.fn();
    const onComposeToRecipient = vi.fn();

    renderHook(() =>
      useDeepLink({ onNavigateToEmail, onComposeToRecipient })
    );

    expect(onOpenUrlMock).toHaveBeenCalledTimes(1);

    await act(async () => {
      deepLinkHandler?.([
        "mahomail://email/msg-123",
        "mahomail://compose?to=person%40example.com",
      ]);
    });

    expect(onNavigateToEmail).toHaveBeenCalledWith("msg-123");
    expect(onComposeToRecipient).toHaveBeenCalledWith("person@example.com");
  });

  it("ignores unsupported or malformed deep links", async () => {
    const unlisten = vi.fn();
    onOpenUrlMock.mockImplementationOnce((handler) => {
      deepLinkHandler = handler;
      return Promise.resolve(unlisten);
    });

    const onNavigateToEmail = vi.fn();
    const onComposeToRecipient = vi.fn();

    renderHook(() =>
      useDeepLink({ onNavigateToEmail, onComposeToRecipient })
    );

    await act(async () => {
      deepLinkHandler?.([
        "mahomail://oauth/callback?code=abc123&state=xyz789",
        "mahomail://archive/msg-123",
        "not-a-url",
        "https://example.com",
      ]);
    });

    expect(onNavigateToEmail).not.toHaveBeenCalled();
    expect(onComposeToRecipient).not.toHaveBeenCalled();
  });
});
