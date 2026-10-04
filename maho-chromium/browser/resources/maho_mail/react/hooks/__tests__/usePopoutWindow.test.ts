import { renderHook, act, waitFor } from "@testing-library/react";
import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import {
  announceComposeReady,
  closePopout,
  focusOrCreate,
  getPopoutEmailId,
  getPopoutType,
  isPopout,
  usePopoutWindow,
} from "../usePopoutWindow";
import { isMobile } from "../../utils/platform";

vi.mock("../../utils/platform", () => ({
  isMobile: vi.fn(),
}));

const {mockToast} = vi.hoisted(() => ({mockToast: vi.fn()}));

vi.mock("../../components/ui/Toast", () => ({
  useToast: () => ({toast: mockToast}),
  Toaster: () => null,
}));

interface FakePopout {
  closed: boolean;
  focus: ReturnType<typeof vi.fn>;
  close: ReturnType<typeof vi.fn>;
  document: { title: string };
}

function createFakePopout(): FakePopout {
  const popout: FakePopout = {
    closed: false,
    focus: vi.fn(),
    close: vi.fn(() => {
      popout.closed = true;
    }),
    document: { title: "" },
  };
  return popout;
}

describe("usePopoutWindow", () => {
  const originalOpen = window.open;
  let openSpy: ReturnType<typeof vi.fn>;
  let popouts: FakePopout[];

  beforeEach(() => {
    vi.clearAllMocks();
    vi.mocked(isMobile).mockReturnValue(false);
    window.history.pushState({}, "", "/");
    popouts = [];
    openSpy = vi.fn(() => {
      const popout = createFakePopout();
      popouts.push(popout);
      return popout as unknown as Window;
    });
    window.open = openSpy as unknown as typeof window.open;
    vi.useFakeTimers();
  });

  afterEach(() => {
    vi.useRealTimers();
    window.open = originalOpen;
    window.history.pushState({}, "", "/");
  });

  it("reads popout metadata from the current URL", () => {
    window.history.pushState({}, "", "/?popout=1&emailId=msg-123");

    expect(isPopout()).toBe(true);
    expect(getPopoutType()).toBe("reader");
    expect(getPopoutEmailId()).toBe("msg-123");
  });

  it("returns no-op actions on mobile", () => {
    vi.mocked(isMobile).mockReturnValue(true);

    const { result } = renderHook(() => usePopoutWindow());

    expect(result.current.isPopout).toBe(false);
    expect(result.current.popoutType).toBeNull();
    expect(result.current.popoutEmailId).toBeNull();
    expect(result.current.openReader).toBeInstanceOf(Function);
    expect(result.current.openCompose).toBeInstanceOf(Function);
    expect(result.current.close).toBeInstanceOf(Function);
  });

  it("focuses an existing popout window and closes it when requested", async () => {
    const created = await focusOrCreate("reader-msg-123", {
      url: "index.html?emailId=msg-123&popout=1",
      title: "Email",
    });
    expect(created).toBe(true);
    expect(popouts).toHaveLength(1);

    const reopened = await focusOrCreate("reader-msg-123", {
      url: "index.html?emailId=msg-123&popout=1",
      title: "Email",
    });

    expect(reopened).toBe(false);
    expect(popouts).toHaveLength(1);
    expect(popouts[0]!.focus).toHaveBeenCalledTimes(1);

    await closePopout("reader-msg-123");

    expect(popouts[0]!.close).toHaveBeenCalledTimes(1);
  });

  it("throws when the browser blocks the popup", async () => {
    openSpy.mockReturnValueOnce(null);

    await expect(
      focusOrCreate("reader-blocked", {url: "index.html", title: "Email"}),
    ).rejects.toThrow(/blocked/i);
  });

  describe("openCompose handshake", () => {
    it("resolves once the popout announces it is ready", async () => {
      const {result} = renderHook(() => usePopoutWindow());

      const payload = {
        to: "recipient@example.com",
        cc: "",
        bcc: "",
        subject: "Subject",
        body: "Body",
        bodyHtml: "<p>Body</p>",
        accountId: "acc-1",
      };

      let done = false;
      const promise = result.current.openCompose(payload).then(() => {
        done = true;
      });

      expect(done).toBe(false);
      expect(openSpy).toHaveBeenCalledTimes(1);
      const composeLabel = openSpy.mock.calls[0]![1] as string;
      expect(composeLabel).toMatch(/^compose-/);

      // The opener registers its compose:ready listener after the window is
      // created, so an immediate ACK would be published to nobody. It also
      // pings the popout after five seconds; advancing timers drives that ping,
      // and announceComposeReady answers it the way a real popout would.
      const openerName = window.name;
      window.name = composeLabel;
      const stopAnnouncing = announceComposeReady();
      window.name = openerName;

      await act(async () => {
        await vi.advanceTimersByTimeAsync(5000);
        await promise;
      });
      stopAnnouncing();

      expect(done).toBe(true);
    });

    it("announces readiness as soon as the popout mounts", async () => {
      const label = "compose-ready-probe";
      const observer = new BroadcastChannel("maho-mail-popout");
      const announced = new Promise<void>((resolve) => {
        observer.onmessage = (event: MessageEvent<{event: string}>) => {
          if (event.data?.event === `compose:ready:${label}`) resolve();
        };
      });

      const openerName = window.name;
      window.name = label;
      const stopAnnouncing = announceComposeReady();
      window.name = openerName;

      await announced;

      stopAnnouncing();
      observer.close();
    });
  });
});
