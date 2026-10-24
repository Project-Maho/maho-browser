import { render, screen, fireEvent, act } from "@testing-library/react";
import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import { ComposeWindow } from "../ComposeWindow";
import { TestProviders } from "../../../test/mocks";

const { initHandlers } = vi.hoisted(() => ({
  initHandlers: [] as Array<(payload: unknown) => void>,
}));

vi.mock("../../../api", () => ({
  listAccounts: vi.fn().mockResolvedValue([
    { id: "acc-1", email: "sender@example.com", display_name: "Sender", auth_type: "password" },
  ]),
  sendEmail: vi.fn().mockResolvedValue(undefined),
  listSignatures: vi.fn().mockResolvedValue([]),
  listTemplates: vi.fn().mockResolvedValue([]),
}));

vi.mock("../../../hooks/usePopoutWindow", () => ({
  useComposeInitListener: (onInit: (payload: unknown) => void) => {
    initHandlers[0] = onInit;
  },
  emitMailUpdated: vi.fn().mockResolvedValue(undefined),
  announceComposeReady: vi.fn(() => () => {}),
  closeSelf: vi.fn(),
}));

vi.mock("../../../hooks/useSettings", () => ({
  useSettings: () => ({ undoSendDelay: 1 }),
}));

vi.mock("../../ui/Toast", () => ({
  useToast: () => ({ toast: vi.fn() }),
}));

vi.mock("../RichTextEditor", () => ({
  RichTextEditor: ({ content, onChange }: { content: string; onChange: (html: string, text: string) => void }) => (
    <textarea aria-label="compose-body" value={content} onChange={(e) => onChange(`<p>${e.target.value}</p>`, e.target.value)} />
  ),
}));

describe("ComposeWindow late init payload", () => {
  beforeEach(() => {
    initHandlers.length = 0;
    vi.useFakeTimers({ shouldAdvanceTime: true });
  });

  afterEach(() => {
    vi.useRealTimers();
  });

  it("keeps typed input when the init payload arrives after the fallback", async () => {
    render(<ComposeWindow />, { wrapper: TestProviders });
    await act(async () => {
      vi.advanceTimersByTime(2000);
    });

    const subject = screen.getByPlaceholderText("Subject");
    fireEvent.input(subject, { target: { value: "My draft" } });
    expect(subject).toHaveValue("My draft");

    await act(async () => {
      initHandlers[0]?.({ accountId: "acc-1", subject: "Late payload" });
    });

    expect(screen.getByPlaceholderText("Subject")).toHaveValue("My draft");
  });

  it("hydrates from a late payload when the user has not typed", async () => {
    render(<ComposeWindow />, { wrapper: TestProviders });
    await act(async () => {
      vi.advanceTimersByTime(2000);
    });

    await act(async () => {
      initHandlers[0]?.({ accountId: "acc-1", subject: "Late payload" });
    });

    expect(screen.getByPlaceholderText("Subject")).toHaveValue("Late payload");
  });
});
