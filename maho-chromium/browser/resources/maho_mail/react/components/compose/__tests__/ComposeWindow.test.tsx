import { render, screen, fireEvent, waitFor, act } from "@testing-library/react";
import { useEffect } from "react";
import { describe, it, expect, vi, beforeEach } from "vitest";
import { ComposeWindow } from "../ComposeWindow";
import { TestProviders } from "../../../test/mocks";
import type { AccountSummary } from "../../../types";

const { toastMock, sendEmailMock, listAccountsMock, composeInitPayload, closeSelfMock } = vi.hoisted(() => ({
  toastMock: vi.fn(),
  sendEmailMock: vi.fn().mockResolvedValue(undefined),
  listAccountsMock: vi.fn(),
  composeInitPayload: {
    to: "friend@example.com",
    cc: "",
    bcc: "",
    subject: "Hello",
    body: "Plain text",
    bodyHtml: "<p>Plain text</p>",
    accountId: "acc-1",
  },
  closeSelfMock: vi.fn(),
}));


vi.mock("../../../events.js", () => ({
  emit: vi.fn().mockResolvedValue(undefined),
  listen: vi.fn((event: string, handler: (event: { payload: unknown }) => void) => {
    if (event === "compose:init") {
      queueMicrotask(() => handler({ payload: composeInitPayload }));
    }
    return Promise.resolve(() => {});
  }),
}));

vi.mock("../../../api", () => ({
  listAccounts: listAccountsMock,
  sendEmail: sendEmailMock,
  adjustTone: vi.fn().mockResolvedValue({ adjusted_text: "" }),
  encryptEmailPgp: vi.fn().mockResolvedValue("encrypted body"),
  listSignatures: vi.fn().mockResolvedValue([]),
  listTemplates: vi.fn().mockResolvedValue([]),
}));
vi.mock("../RichTextEditor", () => ({
  RichTextEditor: ({ content, placeholder, onChange }: { content: string; placeholder?: string; onChange: (html: string, text: string) => void }) => {
    return (
      <textarea
        aria-label="compose-body"
        placeholder={placeholder ?? ""}
        value={content}
        onChange={(e) => {
          const val = e.target.value;
          const html = val.startsWith("<p>") ? val : `<p>${val}</p>`;
          onChange(html, val);
        }}
      />
    );
  },
}));

vi.mock("../../../hooks/usePopoutWindow", () => ({
  useComposeInitListener: (onInit: (payload: typeof composeInitPayload) => void) => {
    useEffect(() => {
      onInit(composeInitPayload);
    }, [onInit]);
  },
  emitMailUpdated: vi.fn().mockResolvedValue(undefined),
  announceComposeReady: vi.fn(() => () => {}),
  closeSelf: closeSelfMock,
  usePopoutWindow: () => ({
    openCompose: vi.fn(),
    openReader: vi.fn(),
    close: vi.fn(),
    isPopout: false,
    popoutType: null,
    popoutEmailId: null,
  }),
}));

vi.mock("../../../hooks/useSettings", () => ({
  useSettings: () => ({ undoSendDelay: 1 }),
}));

const mockConfirm = vi.fn().mockResolvedValue(true);
vi.mock("../../ui/ConfirmDialog", () => ({
  useConfirm: () => mockConfirm,
  ConfirmProvider: ({ children }: any) => children,
}));

vi.mock("../../ui/Toast", () => ({
  useToast: () => ({ toast: toastMock }),
}));

vi.mock("../RichTextEditor", () => ({
  RichTextEditor: ({ content, onChange, placeholder }: { content: string; onChange: (html: string, text: string) => void; placeholder?: string }) => (
    <textarea
      aria-label="compose-body"
      placeholder={placeholder ?? ""}
      value={content}
      onChange={(e) => onChange(`<p>${e.target.value}</p>`, e.target.value)}
    />
  ),
}));

vi.mock("../ContactAutocomplete", () => ({
  ContactAutocomplete: ({ value, onChange, placeholder, autoFocus }: { value: string; onChange: (value: string) => void; placeholder?: string; autoFocus?: boolean }) => (
    <input
      aria-label="compose-to"
      placeholder={placeholder ?? ""}
      value={value}
      onChange={(e) => onChange(e.target.value)}
      autoFocus={autoFocus}
    />
  ),
}));

vi.mock("../../common/UndoSendToast", () => ({
  UndoSendToast: ({ onComplete }: { onComplete: () => void }) => (
    <button type="button" onClick={onComplete}>Complete send</button>
  ),
}));



describe("ComposeWindow", () => {
  const accounts: AccountSummary[] = [
    { id: "acc-1", email: "sender@example.com", display_name: "Sender", auth_type: "password" },
  ];

  beforeEach(() => {
    vi.clearAllMocks();
    listAccountsMock.mockResolvedValue(accounts);
  });

  it("renders the compose window after initialization", async () => {
    render(<ComposeWindow />, { wrapper: TestProviders });

    await waitFor(() => {
      expect(screen.getByRole("button", { name: /send/i })).toBeInTheDocument();
      expect(screen.getByRole("button", { name: /discard/i })).toBeInTheDocument();
      expect(screen.getByPlaceholderText("recipient@example.com")).toBeInTheDocument();
      expect(screen.getByPlaceholderText("Subject")).toBeInTheDocument();
      expect(screen.getByLabelText("compose-body")).toBeInTheDocument();
    });
  });

  it("closes the current window when Discard is clicked", async () => {
    render(<ComposeWindow />, { wrapper: TestProviders });

    await waitFor(() => {
      expect(screen.getByRole("button", { name: /discard/i })).toBeInTheDocument();
    });

    fireEvent.click(screen.getByRole("button", { name: /discard/i }));

    await waitFor(() => {
      expect(closeSelfMock).toHaveBeenCalledTimes(1);
    });
  });

  it("sends the composed message after confirming the undo toast", async () => {
    render(<ComposeWindow />, { wrapper: TestProviders });

    await waitFor(() => {
      expect(screen.getByRole("button", { name: /send/i })).toBeInTheDocument();
    });

    fireEvent.change(screen.getByLabelText("compose-to"), { target: { value: "friend@example.com" } });
    fireEvent.change(screen.getByPlaceholderText("Subject"), { target: { value: "Hello" } });
    fireEvent.change(screen.getByLabelText("compose-body"), { target: { value: "Plain text" } });

    await waitFor(() => {
      expect(screen.getByLabelText("compose-to")).toHaveValue("friend@example.com");
      expect(screen.getByPlaceholderText("Subject")).toHaveValue("Hello");
      expect(screen.getByLabelText("compose-body")).toHaveValue("<p>Plain text</p>");
    });

    fireEvent.click(screen.getByRole("button", { name: /send/i }));

    await waitFor(() => {
      expect(screen.getByRole("button", { name: /complete send/i })).toBeInTheDocument();
    });

    fireEvent.click(screen.getByRole("button", { name: /complete send/i }));

    await waitFor(() => {
      expect(sendEmailMock).toHaveBeenCalledWith(
        expect.objectContaining({
          account_id: "acc-1",
          to: ["friend@example.com"],
          subject: "Hello",
          body_text: "Plain text",
          body_html: "<p>Plain text</p>",
        }),
      );
    });
  });

  describe("P2-a: attachment limits", () => {
    it("rejects the 11th attachment with a toast error", async () => {
      render(<ComposeWindow />, { wrapper: TestProviders });

      await waitFor(() => {
        expect(screen.getByRole("button", { name: /send/i })).toBeInTheDocument();
      });

      const fileInput = document.querySelector('input[type="file"]') as HTMLInputElement;
      expect(fileInput).toBeTruthy();

      const makeFile = (name: string) => new File(["x"], name, { type: "text/plain" });
      const elevenFiles = Array.from({ length: 11 }, (_, i) => makeFile(`file${i + 1}.txt`));

      await act(async () => {
        fireEvent.change(fileInput, { target: { files: elevenFiles } });
      });

      expect(toastMock).toHaveBeenCalledWith("error", expect.stringContaining("10"));
    });
  });

  describe("P2-g: recipient validation in ComposeWindow", () => {
    it("shows error for invalid email and does not send", async () => {
      composeInitPayload.to = "bad-email";
      render(<ComposeWindow />, { wrapper: TestProviders });

      await waitFor(() => {
        expect(screen.getByRole("button", { name: /send/i })).toBeInTheDocument();
        expect(screen.getByLabelText("compose-to")).toHaveValue("bad-email");
      });

      await act(async () => {
        fireEvent.click(screen.getByRole("button", { name: /send/i }));
      });

      await waitFor(() => {
        expect(screen.getByText(/Invalid email addresses/)).toBeInTheDocument();
      });

      expect(sendEmailMock).not.toHaveBeenCalled();
      composeInitPayload.to = "friend@example.com";
    });
  });

  it("closes the window when Escape key is pressed", async () => {
    render(<ComposeWindow />, { wrapper: TestProviders });

    await waitFor(() => {
      expect(screen.getByRole("button", { name: /send/i })).toBeInTheDocument();
    });

    fireEvent.keyDown(document, { key: "Escape" });

    await waitFor(() => {
      expect(closeSelfMock).toHaveBeenCalledTimes(1);
    });
  });

  it("focuses the To input on mount for a new compose", async () => {
    render(<ComposeWindow />, { wrapper: TestProviders });

    await waitFor(() => {
      expect(screen.getByLabelText("compose-to")).toBeInTheDocument();
    });

    await waitFor(() => {
      expect(screen.getByLabelText("compose-to")).toHaveFocus();
    });
  });
});
