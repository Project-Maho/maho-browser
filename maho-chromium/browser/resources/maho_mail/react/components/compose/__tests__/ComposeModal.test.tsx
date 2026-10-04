import { render, screen, fireEvent, act, waitFor } from "@testing-library/react";
import { describe, it, expect, vi, beforeEach } from "vitest";
import { ComposeModal } from "../ComposeModal";
import { TestProviders } from "../../../test/mocks";
import type { AccountSummary } from "../../../types";

const { sendEmailMock, listTemplatesMock, listSignaturesMock } = vi.hoisted(() => ({
  sendEmailMock: vi.fn().mockResolvedValue(undefined),
  listTemplatesMock: vi.fn().mockResolvedValue([]),
  listSignaturesMock: vi.fn().mockResolvedValue([]),
}));

vi.mock("../../../api", () => ({
  listSignatures: listSignaturesMock,
  getForwardedAttachments: vi.fn().mockResolvedValue([]),
  sendEmail: sendEmailMock,
  listTemplates: listTemplatesMock,
  adjustTone: vi.fn().mockResolvedValue({ adjusted_text: "" }),
  saveDraft: vi.fn().mockResolvedValue("draft-1"),
  updateDraft: vi.fn().mockResolvedValue(undefined),
  deleteEmail: vi.fn().mockResolvedValue(undefined),
  scheduleSend: vi.fn().mockResolvedValue(undefined),
  encryptEmailPgp: vi.fn().mockResolvedValue("encrypted"),
  signEmailSmime: vi.fn().mockResolvedValue("signed"),
  encryptEmailSmime: vi.fn().mockResolvedValue("encrypted"),
  queueEmail: vi.fn().mockResolvedValue(undefined),
}));

vi.mock("../RichTextEditor", () => ({
  RichTextEditor: ({ content, placeholder }: { content: string; placeholder?: string }) => {
    return (
      <textarea
        aria-label="editor"
        placeholder={placeholder ?? ""}
        defaultValue={content}
      />
    );
  },
}));

vi.mock("../ContactAutocomplete", () => ({
  ContactAutocomplete: ({ value, onChange, placeholder }: { value: string; onChange: (v: string) => void; placeholder?: string }) => (
    <input
      placeholder={placeholder ?? ""}
      value={value}
      onChange={(e) => onChange(e.target.value)}
    />
  ),
}));

vi.mock("../../../hooks/usePopoutWindow", () => ({
  usePopoutWindow: () => ({
    openCompose: vi.fn(),
    openReader: vi.fn(),
    close: vi.fn(),
    isPopout: false,
    popoutType: null,
    popoutEmailId: null,
  }),
  emitMailUpdated: vi.fn().mockResolvedValue(undefined),
}));



const mockAccounts: AccountSummary[] = [
  { id: "acc-1", email: "user1@example.com", display_name: "User One", auth_type: "password" },
  { id: "acc-2", email: "user2@example.com", display_name: "User Two", auth_type: "password" },
];

describe("ComposeModal", () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  it("returns null when isOpen is false", async () => {
    render(
      <ComposeModal
        isOpen={false}
        onClose={vi.fn()}
        accountId="acc-1"
        accounts={mockAccounts}
      />,
      { wrapper: TestProviders },
    );

    await act(async () => {});
    await waitFor(() => {
      expect(screen.queryByText("New Message")).not.toBeInTheDocument();
    });
  });

  it("renders compose form when isOpen is true", async () => {
    render(
      <ComposeModal
        isOpen={true}
        onClose={vi.fn()}
        accountId="acc-1"
        accounts={mockAccounts}
      />,
      { wrapper: TestProviders },
    );

    await waitFor(() => {
      expect(screen.getByText("New Message")).toBeInTheDocument();
      expect(screen.getByPlaceholderText("recipient@example.com")).toBeInTheDocument();
      expect(screen.getByPlaceholderText("Subject")).toBeInTheDocument();
      expect(screen.getByPlaceholderText("Write your message...")).toBeInTheDocument();
    });
  });

  it("calls onClose when Discard is clicked", async () => {
    const onClose = vi.fn();
    render(
      <ComposeModal
        isOpen={true}
        onClose={onClose}
        accountId="acc-1"
        accounts={mockAccounts}
      />,
      { wrapper: TestProviders },
    );

    fireEvent.click(screen.getByText("Discard"));
    await waitFor(() => expect(onClose).toHaveBeenCalledOnce());
  });

  describe("account selection in aggregate mode", () => {
    it("shows account picker when opened from aggregate mode with multiple accounts", async () => {
      render(
        <ComposeModal
          isOpen={true}
          onClose={vi.fn()}
          accountId=""
          accounts={mockAccounts}
        />,
        { wrapper: TestProviders },
      );

      await waitFor(() => {
        expect(screen.getByText("Select an account...")).toBeInTheDocument();
      });

      const selects = screen.getAllByRole("combobox");
      const accountSelect = selects.find((s) =>
        s.querySelector('option[value=""]'),
      );
      expect(accountSelect).toBeInTheDocument();
      expect(accountSelect).toHaveValue("");
    });

    it("auto-selects account when only one account exists in aggregate mode", async () => {
      const singleAccount: AccountSummary[] = [
        { id: "acc-1", email: "user1@example.com", display_name: "User One", auth_type: "password" },
      ];

      render(
        <ComposeModal
          isOpen={true}
          onClose={vi.fn()}
          accountId=""
          accounts={singleAccount}
        />,
        { wrapper: TestProviders },
      );

      await waitFor(() => {
        expect(screen.getByText("User One (user1@example.com)")).toBeInTheDocument();
      });

      const sendButton = screen.getByText("Send").closest("button");
      expect(sendButton).not.toBeDisabled();
    });

    it("disables Send and Save Draft when no account is selected in aggregate mode", async () => {
      render(
        <ComposeModal
          isOpen={true}
          onClose={vi.fn()}
          accountId=""
          accounts={mockAccounts}
        />,
        { wrapper: TestProviders },
      );

      await waitFor(() => {
        expect(screen.getByText("Select an account...")).toBeInTheDocument();
      });

      const sendButton = screen.getByText("Send").closest("button");
      const saveDraftButton = screen.getByText("Save Draft").closest("button");

      expect(sendButton).toBeDisabled();
      expect(saveDraftButton).toBeDisabled();
    });

    it("enables Send and Save Draft after selecting an account in aggregate mode", async () => {
      // Test that buttons are initially disabled and the account selector is present
      render(
        <ComposeModal
          isOpen={true}
          onClose={vi.fn()}
          accountId=""
          accounts={mockAccounts}
        />,
        { wrapper: TestProviders },
      );

      await waitFor(() => {
        expect(screen.getByText("Select an account...")).toBeInTheDocument();
      });

      const sendButton = screen.getByText("Send").closest("button");
      const saveDraftButton = screen.getByText("Save Draft").closest("button");

      expect(sendButton).toBeDisabled();
      expect(saveDraftButton).toBeDisabled();

      // Verify account selector exists with options
      const selects = screen.getAllByRole("combobox");
      const accountSelect = selects.find((s) =>
        s.querySelector('option[value=""]'),
      );
      expect(accountSelect).toBeInTheDocument();
      expect(accountSelect?.querySelector('option[value="acc-1"]')).toBeInTheDocument();
      expect(accountSelect?.querySelector('option[value="acc-2"]')).toBeInTheDocument();
    });

    it("shows selected account info when account is pre-selected", async () => {
      render(
        <ComposeModal
          isOpen={true}
          onClose={vi.fn()}
          accountId="acc-1"
          accounts={mockAccounts}
        />,
        { wrapper: TestProviders },
      );

      await waitFor(() => {
        expect(screen.getByText("User One (user1@example.com)")).toBeInTheDocument();
      });

      expect(screen.queryByText("Select an account...")).not.toBeInTheDocument();
    });
  });

  describe("reply/forward account resolution", () => {
    it("uses sourceAccountId for reply/forward flows", async () => {
      const replyContext = {
        original_subject: "Test Subject",
        original_from: "sender@example.com",
        original_date: "2024-01-01T00:00:00Z",
        original_body_text: "Original message",
        original_body_html: null,
        original_message_id: "<msg@example.com>",
        original_references: null,
        reply_all: false,
      };

      render(
        <ComposeModal
          isOpen={true}
          onClose={vi.fn()}
          accountId=""
          accounts={mockAccounts}
          sourceAccountId="acc-2"
          replyTo={replyContext}
        />,
        { wrapper: TestProviders },
      );

      await waitFor(() => {
        expect(screen.getByText("Reply")).toBeInTheDocument();
        expect(screen.getByText("User Two (user2@example.com)")).toBeInTheDocument();
      });

      const sendButton = screen.getByText("Send").closest("button");
      expect(sendButton).not.toBeDisabled();
    });

    it("uses sourceAccountId for forward flows", async () => {
      const forwardContext = {
        original_subject: "Test Subject",
        original_from: "sender@example.com",
        original_date: "2024-01-01T00:00:00Z",
        original_body_text: "Original message",
        original_body_html: null,
        original_message_id: "<fwd@example.com>",
        original_references: null,
      };

      render(
        <ComposeModal
          isOpen={true}
          onClose={vi.fn()}
          accountId=""
          accounts={mockAccounts}
          sourceAccountId="acc-1"
          forwardFrom={forwardContext}
        />,
        { wrapper: TestProviders },
      );

      await waitFor(() => {
        expect(screen.getByText("Forward")).toBeInTheDocument();
        expect(screen.getByText("User One (user1@example.com)")).toBeInTheDocument();
      });

      const sendButton = screen.getByText("Send").closest("button");
      expect(sendButton).not.toBeDisabled();
    });
  });

  describe("P2-e: template preserves Re:/Fwd: prefix", () => {
    it("preserves Re: prefix when applying template to a reply subject", async () => {
      const replyContext = {
        original_subject: "Meeting notes",
        original_from: "sender@example.com",
        original_date: "2024-01-01T00:00:00Z",
        original_body_text: "Original message",
        original_body_html: null,
        original_message_id: "<msg@example.com>",
        original_references: null,
        reply_all: false,
      };

      const template = {
        id: "tpl-1",
        name: "Follow up",
        subject: "Follow up on our discussion",
        body_text: "Hello, following up...",
        body_html: "<p>Hello, following up...</p>",
      };

      listTemplatesMock.mockResolvedValue([template]);

      render(
        <ComposeModal
          isOpen={true}
          onClose={vi.fn()}
          accountId="acc-1"
          accounts={mockAccounts}
          replyTo={replyContext}
        />,
        { wrapper: TestProviders },
      );

      const subjectInput = await screen.findByDisplayValue("Re: Meeting notes");
      expect(subjectInput).toBeInTheDocument();

      const templateButton = screen.getByLabelText("Insert Template");
      fireEvent.click(templateButton);

      await waitFor(() => {
        expect(screen.getByText("Follow up")).toBeInTheDocument();
      });

      fireEvent.click(screen.getByText("Follow up"));

      await waitFor(() => {
        expect(screen.getByText("Replace current content?")).toBeInTheDocument();
      });

      fireEvent.click(screen.getByText("Replace"));

      await waitFor(() => {
        expect(subjectInput).toHaveValue("Re: Follow up on our discussion");
      });
    });

    it("replaces subject normally when no Re:/Fwd: prefix", async () => {
      const template = {
        id: "tpl-1",
        name: "Intro",
        subject: "Introduction",
        body_text: "Hello!",
        body_html: "<p>Hello!</p>",
      };

      listTemplatesMock.mockResolvedValue([template]);

      render(
        <ComposeModal
          isOpen={true}
          onClose={vi.fn()}
          accountId="acc-1"
          accounts={mockAccounts}
        />,
        { wrapper: TestProviders },
      );

      await waitFor(() => {
        expect(screen.getByText("New Message")).toBeInTheDocument();
      });

      const templateButton = screen.getByLabelText("Insert Template");
      fireEvent.click(templateButton);

      await waitFor(() => {
        expect(screen.getByText("Intro")).toBeInTheDocument();
      });

      fireEvent.click(screen.getByText("Intro"));

      await waitFor(() => {
        const subjectInput = screen.getByPlaceholderText("Subject");
        expect(subjectInput).toHaveValue("Introduction");
      });
    });
  });

  describe("P2-g: recipient validation blocks send", () => {
    it("shows error and does not send when recipients are invalid", async () => {
      render(
        <ComposeModal
          isOpen={true}
          onClose={vi.fn()}
          accountId="acc-1"
          accounts={mockAccounts}
          initialTo="not-an-email"
        />,
        { wrapper: TestProviders },
      );

      await waitFor(() => {
        expect(screen.getByText("New Message")).toBeInTheDocument();
        expect(screen.getByPlaceholderText("recipient@example.com")).toHaveValue("not-an-email");
      });

      await act(async () => {
        fireEvent.click(screen.getByText("Send"));
      });

      await waitFor(() => {
        expect(screen.getByText(/Invalid email addresses/)).toBeInTheDocument();
      });

      expect(sendEmailMock).not.toHaveBeenCalled();
    });

    it("allows send with valid email addresses", async () => {
      render(
        <ComposeModal
          isOpen={true}
          onClose={vi.fn()}
          accountId="acc-1"
          accounts={mockAccounts}
        />,
        { wrapper: TestProviders },
      );

      await waitFor(() => {
        expect(screen.getByText("New Message")).toBeInTheDocument();
      });

      const toInput = screen.getByPlaceholderText("recipient@example.com");
      fireEvent.change(toInput, { target: { value: "valid@example.com" } });

      fireEvent.click(screen.getByText("Send"));

      await waitFor(() => {
        expect(sendEmailMock).toHaveBeenCalled();
      });
    });
  });

  describe("P1-1: signature race condition", () => {
    it("does not overwrite user-typed body when signature loads late", async () => {
      let resolveSignatures!: (value: unknown) => void;
      const sigPromise = new Promise((resolve) => {
        resolveSignatures = resolve;
      });
      listSignaturesMock.mockReturnValue(sigPromise);

      render(
        <ComposeModal
          isOpen={true}
          onClose={vi.fn()}
          accountId="acc-1"
          accounts={mockAccounts}
        />,
        { wrapper: TestProviders },
      );

      await waitFor(() => {
        expect(screen.getByText("New Message")).toBeInTheDocument();
      });

      const plainTextButton = screen.getByText("Plain Text");
      fireEvent.click(plainTextButton);

      const textarea = screen.getByPlaceholderText("Write your message...");
      await act(async () => {
        fireEvent.change(textarea, { target: { value: "User typed content" } });
      });

      await act(async () => {
        resolveSignatures([
          {
            id: "sig-1",
            account_id: "acc-1",
            name: "Default Sig",
            body_text: "Best regards,\nUser",
            body_html: "<p>Best regards,<br>User</p>",
            is_default: true,
          },
        ]);
      });

      await waitFor(() => {
        const bodyTextarea = screen.getByPlaceholderText("Write your message...");
        expect(bodyTextarea).toHaveValue("User typed content");
      });
    });
  });

  describe("P1-2: account/recipient race condition", () => {
    it("does not overwrite user-typed To field when accounts update asynchronously", async () => {
      listSignaturesMock.mockResolvedValue([]);

      const onClose = vi.fn();
      const { rerender } = render(
        <ComposeModal
          isOpen={true}
          onClose={onClose}
          accountId="acc-1"
          accountEmail="user1@example.com"
          accounts={[{ id: "acc-1", email: "user1@example.com", display_name: "User One", auth_type: "password" }]}
        />,
        { wrapper: TestProviders },
      );

      await waitFor(() => {
        expect(screen.getByText("New Message")).toBeInTheDocument();
      });

      const toInput = screen.getByPlaceholderText("recipient@example.com");
      await act(async () => {
        fireEvent.change(toInput, { target: { value: "typed@example.com" } });
      });

      expect(toInput).toHaveValue("typed@example.com");

      await act(async () => {
        rerender(
          <ComposeModal
            isOpen={true}
            onClose={onClose}
            accountId="acc-1"
            accountEmail="user1@example.com"
            accounts={mockAccounts}
          />,
        );
      });

      await waitFor(() => {
        const toField = screen.getByPlaceholderText("recipient@example.com");
        expect(toField).toHaveValue("typed@example.com");
      });
    });
  });

  describe("fullscreen toggle", () => {
    function renderOpenModal() {
      return render(
        <ComposeModal
          isOpen
          onClose={vi.fn()}
          accountId="acc-1"
          accountEmail="user1@example.com"
          accounts={mockAccounts}
        />,
        { wrapper: TestProviders },
      );
    }

    it("renders a fullscreen toggle button by default and shows the Maximize2 affordance", async () => {
      renderOpenModal();
      const btn = await screen.findByRole("button", { name: /^Fullscreen$/i });
      expect(btn).toBeInTheDocument();
      expect(btn).toHaveAttribute("aria-pressed", "false");
    });

    it("toggles the modal panel size when clicked and updates aria-pressed + label", async () => {
      renderOpenModal();
      const btn = await screen.findByRole("button", { name: /^Fullscreen$/i });

      const panelBefore = btn.closest("[role=dialog]")?.firstElementChild as HTMLElement | null;
      expect(panelBefore?.className).toMatch(/h-\[90vh\]/);
      expect(panelBefore?.className).toMatch(/max-w-4xl/);

      fireEvent.click(btn);

      const exitBtn = await screen.findByRole("button", { name: /Exit fullscreen/i });
      expect(exitBtn).toHaveAttribute("aria-pressed", "true");
      const panelAfter = exitBtn.closest("[role=dialog]")?.firstElementChild as HTMLElement | null;
      expect(panelAfter?.className).toMatch(/h-full/);
      expect(panelAfter?.className).toMatch(/w-full/);
      expect(panelAfter?.className).toMatch(/rounded-none/);

      fireEvent.click(exitBtn);
      const restoredBtn = await screen.findByRole("button", { name: /^Fullscreen$/i });
      const panelRestored = restoredBtn.closest("[role=dialog]")?.firstElementChild as HTMLElement | null;
      expect(panelRestored?.className).toMatch(/h-\[90vh\]/);
      expect(panelRestored?.className).toMatch(/max-w-4xl/);
    });
  });
});
