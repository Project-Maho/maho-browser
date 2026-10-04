import { render, screen, fireEvent, act } from "@testing-library/react";
import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import { ReaderWindow } from "../ReaderWindow";
import { createMockEmailDetail, TestProviders } from "../../../test/mocks";
import type { ComposeEmailRequest, SendEmailResult } from "../../../types";

const sendEmailMock = vi.fn<[ComposeEmailRequest], Promise<SendEmailResult>>();
const getEmailMock = vi.fn();
const markReadMock = vi.fn();
const emitMailUpdatedMock = vi.fn();

vi.mock("../../../api", () => ({
  getEmail: (...args: unknown[]) => getEmailMock(...args),
  markRead: (...args: unknown[]) => markReadMock(...args),
  sendEmail: (req: ComposeEmailRequest) => sendEmailMock(req),
  md5Hash: vi.fn().mockResolvedValue("mock-hash"),
  toggleStar: vi.fn().mockResolvedValue(undefined),
  deleteEmail: vi.fn().mockResolvedValue(undefined),
  getReplyContext: vi.fn(),
}));

vi.mock("../../../hooks/usePopoutWindow", () => ({
  useMailUpdatedListener: vi.fn(),
  usePopoutWindow: () => ({
    openCompose: vi.fn(),
    openReader: vi.fn(),
  }),
  emitMailUpdated: (...args: unknown[]) => emitMailUpdatedMock(...args),
  closeSelf: vi.fn(),
}));

vi.mock("../../../hooks/useSettings", () => ({
  useSettings: () => ({
    undoSendDelay: 5,
  }),
}));

vi.mock("../../ui/Toast", () => ({
  useToast: () => ({
    toast: vi.fn(),
  }),
}));

describe("DEFECT-06: ReaderWindow undo-send close flush", () => {
  const mockEmail = createMockEmailDetail({
    email: {
      id: "email-1",
      account_id: "acc-1",
      from_address: "alice@example.com",
      from_name: "Alice",
      to_addresses: "user@example.com",
      subject: "Important Project Update",
      body_text: "Please review the notes.",
      is_read: true,
    },
  });

  beforeEach(() => {
    vi.clearAllMocks();
    vi.useFakeTimers();
    getEmailMock.mockResolvedValue(mockEmail);
    markReadMock.mockResolvedValue(undefined);
    sendEmailMock.mockResolvedValue({ status: "sent" });
  });

  afterEach(() => {
    vi.useRealTimers();
  });

  it("flushes pending quick reply send when window triggers beforeunload during undo countdown", async () => {
    render(<ReaderWindow emailId="email-1" />, { wrapper: TestProviders });

    // Allow getEmail promise to resolve
    await act(async () => {
      await Promise.resolve();
    });

    const textarea = screen.getByPlaceholderText("Quick reply...");
    fireEvent.change(textarea, { target: { value: "Got it, thanks Alice!" } });

    const sendBtn = screen.getByRole("button", { name: "Send reply" });
    await act(async () => {
      fireEvent.click(sendBtn);
    });

    // Advance 2 seconds into the 5-second countdown
    act(() => {
      vi.advanceTimersByTime(2000);
    });

    // During undo delay, email has not been dispatched to backend yet
    expect(sendEmailMock).not.toHaveBeenCalled();

    // User closes the window: browser fires beforeunload
    await act(async () => {
      window.dispatchEvent(new Event("beforeunload"));
    });

    // Pending quick reply must be flushed immediately to prevent message loss
    expect(sendEmailMock).toHaveBeenCalledTimes(1);
    expect(sendEmailMock).toHaveBeenCalledWith(
      expect.objectContaining({
        account_id: "acc-1",
        to: ["alice@example.com"],
        subject: "Re: Important Project Update",
        body_text: "Got it, thanks Alice!",
      }),
    );
  });

  it("flushes pending quick reply send on component unmount during undo countdown", async () => {
    const { unmount } = render(<ReaderWindow emailId="email-1" />, { wrapper: TestProviders });

    await act(async () => {
      await Promise.resolve();
    });

    const textarea = screen.getByPlaceholderText("Quick reply...");
    fireEvent.change(textarea, { target: { value: "Will do immediately." } });

    const sendBtn = screen.getByRole("button", { name: "Send reply" });
    await act(async () => {
      fireEvent.click(sendBtn);
    });

    // Advance 1 second into undo delay
    act(() => {
      vi.advanceTimersByTime(1000);
    });

    expect(sendEmailMock).not.toHaveBeenCalled();

    // Popout unmounts on close
    await act(async () => {
      unmount();
    });

    expect(sendEmailMock).toHaveBeenCalledTimes(1);
    expect(sendEmailMock).toHaveBeenCalledWith(
      expect.objectContaining({
        account_id: "acc-1",
        to: ["alice@example.com"],
        body_text: "Will do immediately.",
      }),
    );
  });

  it("does NOT flush quick reply send if user clicked Undo before closing", async () => {
    const { unmount } = render(<ReaderWindow emailId="email-1" />, { wrapper: TestProviders });

    await act(async () => {
      await Promise.resolve();
    });

    const textarea = screen.getByPlaceholderText("Quick reply...");
    fireEvent.change(textarea, { target: { value: "Accidental reply draft" } });

    const sendBtn = screen.getByRole("button", { name: "Send reply" });
    await act(async () => {
      fireEvent.click(sendBtn);
    });

    // Undo toast is shown, user clicks Undo
    const undoBtn = screen.getByRole("button", { name: /undo/i });
    await act(async () => {
      fireEvent.click(undoBtn);
    });

    // User closes window after undoing
    await act(async () => {
      window.dispatchEvent(new Event("beforeunload"));
      unmount();
    });

    // Canceled send must NOT be sent
    expect(sendEmailMock).not.toHaveBeenCalled();
  });

  it("does not send duplicate email on close if undo countdown completed normally", async () => {
    const { unmount } = render(<ReaderWindow emailId="email-1" />, { wrapper: TestProviders });

    await act(async () => {
      await Promise.resolve();
    });

    const textarea = screen.getByPlaceholderText("Quick reply...");
    fireEvent.change(textarea, { target: { value: "Finished send normally." } });

    const sendBtn = screen.getByRole("button", { name: "Send reply" });
    await act(async () => {
      fireEvent.click(sendBtn);
    });

    // Allow full 5 seconds countdown to elapse
    await act(async () => {
      vi.advanceTimersByTime(5000);
    });

    expect(sendEmailMock).toHaveBeenCalledTimes(1);

    // Subsequent window close must not re-dispatch
    await act(async () => {
      window.dispatchEvent(new Event("beforeunload"));
      unmount();
    });

    expect(sendEmailMock).toHaveBeenCalledTimes(1);
  });
});
