import { render, screen, fireEvent, act, waitFor } from "@testing-library/react";
import { describe, it, expect, vi, beforeEach } from "vitest";
import { EmailContent } from "../EmailContent";
import {
  TestProviders,
  createMockAttachment,
  createMockEmailDetail,
} from "../../../test/mocks";

vi.mock("@browsermt/bergamot-translator/translator.js", () => ({
  LatencyOptimisedTranslator: vi.fn(() => ({
    translate: vi.fn(async () => ({ target: { text: "" } })),
    delete: vi.fn(),
  })),
}));

let mockTranslatedText: string | null = null;
let mockTranslatedHtml: string | null = null;
let mockPartialFailureCount = 0;
const mockTranslate = vi.fn();
const mockTranslateHtml = vi.fn();
const mockClearTranslation = vi.fn();
let mockTargetLang = "ko";

vi.mock("../../../hooks/useTranslation", () => ({
  useTranslation: () => ({
    translatedText: mockTranslatedText,
    translatedHtml: mockTranslatedHtml,
    isTranslating: false,
    isModelLoading: false,
    error: null,
    fromLang: "en",
    toLang: "ko",
    translate: mockTranslate,
    translateHtml: mockTranslateHtml,
    setFromLang: vi.fn(),
    setToLang: vi.fn(),
    clearTranslation: mockClearTranslation,
    cleanup: vi.fn(),
    SUPPORTED_LANGUAGES: [
      { code: "en", name: "English" },
      { code: "ko", name: "Korean" },
    ],
    partialFailureCount: mockPartialFailureCount,
  }),
  SUPPORTED_LANGUAGES: [
    { code: "en", name: "English" },
    { code: "ko", name: "Korean" },
  ],
  SUPPORTED_LANGUAGE_CODES: ["en", "ko"],
  getTranslationTargetLang: () => mockTargetLang,
  saveTranslationTargetLang: (lang: string) => {
    mockTargetLang = lang;
  },
}));

const mockConfirm = vi.fn();
vi.mock("../../ui/ConfirmDialog", () => ({
  useConfirm: () => mockConfirm,
  ConfirmProvider: ({ children }: any) => children,
}));

const mockToast = vi.fn();
vi.mock("../../ui/Toast", () => ({
  useToast: () => ({
    toast: mockToast,
  }),
  ToastProvider: ({ children }: any) => children,
}));

vi.mock("../../../api", async (importOriginal) => {
  const actual = await importOriginal<typeof import("../../../api")>();
  return {
    ...actual,
    downloadAttachment: vi.fn(),
    openAttachment: vi.fn(),
  };
});
import * as api from "../../../api";

describe("EmailContent", () => {
  it("shows useful keyboard guidance when no email is selected", () => {
    render(
      <EmailContent
        emailDetail={null}
        loading={false}
        onReply={vi.fn()}
        onForward={vi.fn()}
        onDelete={vi.fn()}
        onToggleStar={vi.fn()}
      />,
      { wrapper: TestProviders },
    );

    expect(screen.getByText("Select an email to read")).toBeInTheDocument();
    expect(screen.getByText(/J\/K/)).toBeInTheDocument();
    expect(screen.getByText(/Enter/)).toBeInTheDocument();
    expect(screen.getByText("C", { selector: "kbd" })).toBeInTheDocument();
  });

  beforeEach(() => {
    vi.clearAllMocks();
    mockToast.mockClear();
    mockTranslatedText = null;
    mockTranslatedHtml = null;
    mockPartialFailureCount = 0;
    mockTargetLang = "ko";
    localStorage.clear();
  });

  it("shows empty state when no emailDetail", async () => {
    render(
      <EmailContent
        emailDetail={null}
        loading={false}
        onReply={vi.fn()}
        onForward={vi.fn()}
        onDelete={vi.fn()}
        onToggleStar={vi.fn()}
      />,
      { wrapper: TestProviders },
    );

    await waitFor(() => {
      expect(screen.getByText("Select an email to read")).toBeInTheDocument();
    });
  });

  it("guides the user to reconnect when loading a message fails authentication", () => {
    const onReconnectAccount = vi.fn();

    render(
      <EmailContent
        emailDetail={null}
        loading={false}
        authRecoveryError={{
          emailId: "e1",
          accountEmail: "alice@example.com",
          message: "[AUTHENTICATIONFAILED] Invalid credentials",
        }}
        onReconnectAccount={onReconnectAccount}
        onReply={vi.fn()}
        onForward={vi.fn()}
        onDelete={vi.fn()}
        onToggleStar={vi.fn()}
      />,
      { wrapper: TestProviders },
    );

    expect(screen.getByRole("alert")).toHaveTextContent("Sign in to continue");
    expect(screen.getByText(/alice@example.com/)).toBeInTheDocument();
    fireEvent.click(screen.getByRole("button", { name: "Sign in again" }));
    expect(onReconnectAccount).toHaveBeenCalledTimes(1);
    expect(mockToast).not.toHaveBeenCalledWith("error", expect.stringContaining("AUTHENTICATIONFAILED"));
  });

  it("shows loading skeleton when loading", async () => {
    render(
      <EmailContent
        emailDetail={null}
        loading={true}
        onReply={vi.fn()}
        onForward={vi.fn()}
        onDelete={vi.fn()}
        onToggleStar={vi.fn()}
      />,
      { wrapper: TestProviders },
    );

    await act(async () => {});
    await waitFor(() => {
      expect(document.querySelectorAll(".animate-pulse").length).toBeGreaterThan(0);
    });
  });

  it("renders subject, from, to, and date", async () => {
    const detail = createMockEmailDetail({
      email: {
        id: "e1",
        subject: "Hello world",
        from_name: "Alice",
        from_address: "alice@example.com",
        to_addresses: "bob@example.com",
        date: new Date().toISOString(),
      },
    });

    render(
      <EmailContent
        emailDetail={detail}
        loading={false}
        onReply={vi.fn()}
        onForward={vi.fn()}
        onDelete={vi.fn()}
        onToggleStar={vi.fn()}
      />,
      { wrapper: TestProviders },
    );

    await waitFor(() => {
      expect(screen.getByText("Hello world")).toBeInTheDocument();
      expect(screen.getByText("Alice")).toBeInTheDocument();
      expect(screen.getByText("<alice@example.com>")).toBeInTheDocument();
      expect(screen.getByText("To:")).toBeInTheDocument();
      expect(screen.getByRole("button", { name: "bob@example.com" })).toBeInTheDocument();
      expect(screen.getByText("just now")).toBeInTheDocument();
    });
  });

  it("exposes AI actions via a single header dropdown rather than an inline row", async () => {
    const detail = createMockEmailDetail({
      email: { id: "e1", subject: "S", date: new Date().toISOString() },
    });

    render(
      <EmailContent
        emailDetail={detail}
        loading={false}
        onReply={vi.fn()}
        onForward={vi.fn()}
        onDelete={vi.fn()}
        onToggleStar={vi.fn()}
      />,
      { wrapper: TestProviders },
    );

    await waitFor(() => {
      expect(screen.getByRole("button", { name: /AI actions/i })).toBeInTheDocument();
    });
    expect(screen.queryByText("AI Actions")).toBeNull();
    expect(screen.queryByText("Summarize")).toBeNull();

    fireEvent.click(screen.getByRole("button", { name: /AI actions/i }));

    await waitFor(() => {
      expect(screen.getByRole("menuitem", { name: /Summarize/i })).toBeInTheDocument();
    });
    expect(screen.getByRole("menuitem", { name: /Draft Reply/i })).toBeInTheDocument();
    expect(screen.getByRole("menuitem", { name: /Classify/i })).toBeInTheDocument();
    expect(screen.getByRole("menuitem", { name: /Translate/i })).toBeInTheDocument();
  });

  it("calls onReply/onForward/onDelete when action buttons clicked", async () => {
    const onReply = vi.fn();
    const onForward = vi.fn();
    const onDelete = vi.fn();
    const detail = createMockEmailDetail({
      email: { id: "e1", subject: "S", date: new Date().toISOString() },
    });

    render(
      <EmailContent
        emailDetail={detail}
        loading={false}
        onReply={onReply}
        onForward={onForward}
        onDelete={onDelete}
        onToggleStar={vi.fn()}
      />,
      { wrapper: TestProviders },
    );

    fireEvent.click(screen.getByText("Reply"));
    fireEvent.click(screen.getByText("Forward"));
    fireEvent.click(screen.getByText("Delete"));

    await waitFor(() => {
      expect(onReply).toHaveBeenCalledWith("e1");
      expect(onForward).toHaveBeenCalledWith("e1");
      expect(onDelete).toHaveBeenCalledWith("e1");
    });
  });

  it("shows attachments section when attachments exist", async () => {
    const att = createMockAttachment({
      id: "a1",
      filename: "file.txt",
      size: 100,
      email_id: "e1",
    });
    const detail = createMockEmailDetail({
      email: { id: "e1", subject: "S", date: new Date().toISOString() },
      attachments: [att],
    });

    render(
      <EmailContent
        emailDetail={detail}
        loading={false}
        onReply={vi.fn()}
        onForward={vi.fn()}
        onDelete={vi.fn()}
        onToggleStar={vi.fn()}
      />,
      { wrapper: TestProviders },
    );

    await waitFor(() => {
      expect(screen.getByText(/1 attachment/)).toBeInTheDocument();
      expect(screen.getByText("file.txt")).toBeInTheDocument();
      expect(screen.getByText("(100 B)")).toBeInTheDocument();
    });
  });

  it("shows danger confirmation dialog for executable attachments", async () => {
    const att = createMockAttachment({
      id: "a1",
      filename: "malware.exe",
      size: 100,
      email_id: "e1",
    });
    const detail = createMockEmailDetail({
      email: { id: "e1", subject: "S", date: new Date().toISOString() },
      attachments: [att],
    });

    render(
      <EmailContent
        emailDetail={detail}
        loading={false}
        onReply={vi.fn()}
        onForward={vi.fn()}
        onDelete={vi.fn()}
        onToggleStar={vi.fn()}
      />,
      { wrapper: TestProviders },
    );

    mockConfirm.mockResolvedValue(false);

    const openButton = screen.getByText("Open");
    fireEvent.click(openButton);

    await waitFor(() => {
      expect(mockConfirm).toHaveBeenCalled();
      expect(api.downloadAttachment).not.toHaveBeenCalled();
    });
  });

  it("does not show confirmation dialog for safe attachments", async () => {
    vi.mocked(api.downloadAttachment).mockResolvedValue("opaque-capability");
    vi.mocked(api.openAttachment).mockResolvedValue(undefined);
    const att = createMockAttachment({
      id: "a1",
      filename: "report.pdf",
      size: 100,
      email_id: "e1",
    });
    const detail = createMockEmailDetail({
      email: { id: "e1", subject: "S", date: new Date().toISOString() },
      attachments: [att],
    });

    render(
      <EmailContent
        emailDetail={detail}
        loading={false}
        onReply={vi.fn()}
        onForward={vi.fn()}
        onDelete={vi.fn()}
        onToggleStar={vi.fn()}
      />,
      { wrapper: TestProviders },
    );

    const openButton = screen.getByText("Open");
    fireEvent.click(openButton);

    await waitFor(() => {
      expect(mockConfirm).not.toHaveBeenCalled();
      expect(api.downloadAttachment).toHaveBeenCalled();
      expect(api.openAttachment).toHaveBeenCalledWith("opaque-capability");
    });
  });

  it("displays homograph warning chip for potential IDN homograph sender address", async () => {
    const detail = createMockEmailDetail({
      email: {
        id: "e1",
        subject: "Urgent security update",
        from_name: "Apple Support",
        from_address: "support@xn--apple-43d.com",
        date: new Date().toISOString(),
      },
    });

    render(
      <EmailContent
        emailDetail={detail}
        loading={false}
        onReply={vi.fn()}
        onForward={vi.fn()}
        onDelete={vi.fn()}
        onToggleStar={vi.fn()}
      />,
      { wrapper: TestProviders },
    );

    await waitFor(() => {
      expect(screen.getByText("Homograph Risk")).toBeInTheDocument();
    });
  });

  it("does NOT render the full-width AI Email Summary banner (chrome reduction)", async () => {
    const detail = createMockEmailDetail({
      email: { id: "e1", subject: "Inbox subject", date: new Date().toISOString() },
    });

    render(
      <EmailContent
        emailDetail={detail}
        loading={false}
        onReply={vi.fn()}
        onForward={vi.fn()}
        onDelete={vi.fn()}
        onToggleStar={vi.fn()}
      />,
      { wrapper: TestProviders },
    );

    await waitFor(() => {
      expect(screen.getByText("Inbox subject")).toBeInTheDocument();
    });

    expect(document.querySelector('[data-testid="email-summary-banner"]')).toBeNull();
    expect(screen.queryByText("AI Email Summary")).toBeNull();
  });

  it("does NOT render the full-width Translation banner with language dropdowns (chrome reduction)", async () => {
    const detail = createMockEmailDetail({
      email: { id: "e1", subject: "Body language test", date: new Date().toISOString() },
    });

    render(
      <EmailContent
        emailDetail={detail}
        loading={false}
        onReply={vi.fn()}
        onForward={vi.fn()}
        onDelete={vi.fn()}
        onToggleStar={vi.fn()}
      />,
      { wrapper: TestProviders },
    );

    await waitFor(() => {
      expect(screen.getByText("Body language test")).toBeInTheDocument();
    });

    expect(screen.queryByText(/Translation$/)).toBeNull();
    expect(screen.queryByLabelText(/Close translation/i)).toBeNull();
  });

  it("manual Translate uses the saved target language, not a hardcoded default", async () => {
    mockTargetLang = "fr";
    const detail = createMockEmailDetail({
      email: { id: "e1", subject: "Manual target", body_html: "<p>Hola</p>", date: new Date().toISOString() },
    });

    render(
      <EmailContent
        emailDetail={detail}
        loading={false}
        onReply={vi.fn()}
        onForward={vi.fn()}
        onDelete={vi.fn()}
        onToggleStar={vi.fn()}
      />,
      { wrapper: TestProviders },
    );

    fireEvent.click(await screen.findByRole("button", { name: /AI actions/i }));
    fireEvent.click(await screen.findByRole("menuitem", { name: /Translate/i }));

    expect(mockTranslateHtml).toHaveBeenCalledWith("<p>Hola</p>", undefined, "fr");
  });

  it("auto-triggers translateHtml for BYOK + HTML <= 8KB", async () => {
    localStorage.setItem(
      "maho-translation-config",
      JSON.stringify({
        provider: "byok",
        defaultTargetLang: "ko",
        alwaysTranslateFrom: ["en"],
      })
    );

    const detail = createMockEmailDetail({
      email: {
        id: "e1",
        body_text: "Hello World",
        body_html: "<div>Hello World</div>",
        date: new Date().toISOString(),
      },
    });

    render(
      <EmailContent
        emailDetail={detail}
        loading={false}
        onReply={vi.fn()}
        onForward={vi.fn()}
        onDelete={vi.fn()}
        onToggleStar={vi.fn()}
      />,
      { wrapper: TestProviders }
    );

    await waitFor(() => {
      expect(mockTranslateHtml).toHaveBeenCalledWith("<div>Hello World</div>", "en", "ko");
    });
  });

  it("skips auto-trigger for HTML > 8KB", async () => {
    localStorage.setItem(
      "maho-translation-config",
      JSON.stringify({
        provider: "byok",
        defaultTargetLang: "ko",
        alwaysTranslateFrom: ["en"],
      })
    );

    const detail = createMockEmailDetail({
      email: {
        id: "e1",
        body_text: "Hello World",
        body_html: "a".repeat(9000),
        date: new Date().toISOString(),
      },
    });

    render(
      <EmailContent
        emailDetail={detail}
        loading={false}
        onReply={vi.fn()}
        onForward={vi.fn()}
        onDelete={vi.fn()}
        onToggleStar={vi.fn()}
      />,
      { wrapper: TestProviders }
    );

    await act(async () => {});
    expect(mockTranslateHtml).not.toHaveBeenCalled();
  });

  it("auto-trigger uses plain translate when provider is local even if HTML present", async () => {
    localStorage.setItem(
      "maho-translation-config",
      JSON.stringify({
        provider: "local",
        defaultTargetLang: "ko",
        alwaysTranslateFrom: ["en"],
      })
    );

    const detail = createMockEmailDetail({
      email: {
        id: "e1",
        body_text: "Hello World",
        body_html: "<div>Hello HTML</div>",
        date: new Date().toISOString(),
      },
    });

    render(
      <EmailContent
        emailDetail={detail}
        loading={false}
        onReply={vi.fn()}
        onForward={vi.fn()}
        onDelete={vi.fn()}
        onToggleStar={vi.fn()}
      />,
      { wrapper: TestProviders }
    );

    await waitFor(() => {
      expect(mockTranslate).toHaveBeenCalledWith("Hello World", "en", "ko");
      expect(mockToast).toHaveBeenCalledWith("info", expect.stringContaining("Local translation model is plain-text only"));
      expect(localStorage.getItem("maho-qwen-html-translation-hint-shown")).toBe("true");
    });
    expect(mockTranslateHtml).not.toHaveBeenCalled();
  });

  it("manual Translate button calls translateHtml for BYOK+HTML", async () => {
    localStorage.setItem(
      "maho-translation-config",
      JSON.stringify({ provider: "byok", defaultTargetLang: "ko" })
    );

    const detail = createMockEmailDetail({
      email: {
        id: "e1",
        body_text: "Hello World",
        body_html: "<div>Hello HTML</div>",
        date: new Date().toISOString(),
      },
    });

    render(
      <EmailContent
        emailDetail={detail}
        loading={false}
        onReply={vi.fn()}
        onForward={vi.fn()}
        onDelete={vi.fn()}
        onToggleStar={vi.fn()}
      />,
      { wrapper: TestProviders }
    );

    fireEvent.click(screen.getByRole("button", { name: /AI actions/i }));
    await waitFor(() => {
      expect(screen.getByRole("menuitem", { name: /Translate/i })).toBeInTheDocument();
    });

    fireEvent.click(screen.getByRole("menuitem", { name: /Translate/i }));

    expect(mockTranslateHtml).toHaveBeenCalledWith("<div>Hello HTML</div>", undefined, "ko");
    expect(mockTranslate).not.toHaveBeenCalled();
  });

  it("manual Translate button calls translate for local+HTML and shows Qwen hint toast once", async () => {
    localStorage.setItem(
      "maho-translation-config",
      JSON.stringify({ provider: "local", defaultTargetLang: "ko" })
    );

    const detail = createMockEmailDetail({
      email: {
        id: "e1",
        body_text: "Hello World",
        body_html: "<div>Hello HTML</div>",
        date: new Date().toISOString(),
      },
    });

    render(
      <EmailContent
        emailDetail={detail}
        loading={false}
        onReply={vi.fn()}
        onForward={vi.fn()}
        onDelete={vi.fn()}
        onToggleStar={vi.fn()}
      />,
      { wrapper: TestProviders }
    );

    fireEvent.click(screen.getByRole("button", { name: /AI actions/i }));
    await waitFor(() => {
      expect(screen.getByRole("menuitem", { name: /Translate/i })).toBeInTheDocument();
    });
    fireEvent.click(screen.getByRole("menuitem", { name: /Translate/i }));

    expect(mockTranslate).toHaveBeenCalledWith("Hello World", undefined, "ko");
    expect(mockTranslateHtml).not.toHaveBeenCalled();
    expect(localStorage.getItem("maho-qwen-html-translation-hint-shown")).toBe("true");
  });

  it("displays a toast notification when partialFailureCount > 0", async () => {
    mockPartialFailureCount = 2;

    const detail = createMockEmailDetail({
      email: {
        id: "e1",
        body_text: "Hello",
        date: new Date().toISOString(),
      },
    });

    render(
      <EmailContent
        emailDetail={detail}
        loading={false}
        onReply={vi.fn()}
        onForward={vi.fn()}
        onDelete={vi.fn()}
        onToggleStar={vi.fn()}
      />,
      { wrapper: TestProviders }
    );

    await waitFor(() => {
      expect(mockToast).toHaveBeenCalledWith("info", expect.stringContaining("2 paragraphs could not be translated cleanly"));
    });
  });

  it("auto-trigger shows Qwen hint toast once for local+HTML", async () => {
    localStorage.setItem(
      "maho-translation-config",
      JSON.stringify({
        provider: "local",
        defaultTargetLang: "ko",
        alwaysTranslateFrom: ["en"],
      })
    );

    const detail = createMockEmailDetail({
      email: {
        id: "e1",
        body_text: "Hello World",
        body_html: "<div>Hello World</div>",
        date: new Date().toISOString(),
      },
    });

    render(
      <EmailContent
        emailDetail={detail}
        loading={false}
        onReply={vi.fn()}
        onForward={vi.fn()}
        onDelete={vi.fn()}
        onToggleStar={vi.fn()}
      />,
      { wrapper: TestProviders }
    );

    await waitFor(() => {
      expect(mockToast).toHaveBeenCalledWith(
        "info",
        expect.stringContaining("Local translation model is plain-text only"),
      );
    });
    expect(localStorage.getItem("maho-qwen-html-translation-hint-shown")).toBe("true");
  });

  describe("attachment card layout", () => {
    function renderWithAttachment(filename: string) {
      const att = createMockAttachment({
        id: "a1",
        filename,
        size: 100,
        email_id: "e1",
      });
      const detail = createMockEmailDetail({
        email: { id: "e1", subject: "S", date: new Date().toISOString() },
        attachments: [att],
      });
      render(
        <EmailContent
          emailDetail={detail}
          loading={false}
          onReply={vi.fn()}
          onForward={vi.fn()}
          onDelete={vi.fn()}
          onToggleStar={vi.fn()}
        />,
        { wrapper: TestProviders },
      );
    }

    it("does NOT use a hardcoded fixed width (w-48) on attachment cards", async () => {
      renderWithAttachment("report.pdf");
      await waitFor(() => {
        expect(screen.getByText("report.pdf")).toBeInTheDocument();
      });
      const card = screen.getByText("report.pdf").closest('[class*="rounded-lg"]');
      expect(card).not.toBeNull();
      expect(card?.className).not.toMatch(/\bw-48\b/);
    });

    it("declares a flexible min/max width on attachment cards so long filenames have room", async () => {
      renderWithAttachment("report.pdf");
      await waitFor(() => {
        expect(screen.getByText("report.pdf")).toBeInTheDocument();
      });
      const card = screen.getByText("report.pdf").closest('[class*="rounded-lg"]');
      expect(card?.className).toMatch(/min-w-\[\d+rem\]/);
      expect(card?.className).toMatch(/\bmax-w-/);
    });

    it("renders an overlong filename without applying CSS truncate", async () => {
      renderWithAttachment(
        "extremely-long-quarterly-report-2026-Q3-final-revision-with-appendix.pdf",
      );
      await waitFor(() => {
        expect(
          screen.getByText(/extremely-long-quarterly-report/),
        ).toBeInTheDocument();
      });
      const nameEl = screen.getByText(
        /extremely-long-quarterly-report-2026-Q3-final-revision-with-appendix\.pdf/,
      );
      expect(nameEl.className).not.toMatch(/\btruncate\b/);
    });

    it("does not request a file-backed inline preview for image attachments", async () => {
      const att = createMockAttachment({
        id: "img-1",
        filename: "photo.png",
        mime_type: "image/png",
        size: 100,
        email_id: "e1",
      });
      const detail = createMockEmailDetail({
        email: {id: "e1", subject: "S", date: new Date().toISOString()},
        attachments: [att],
      });

      render(
        <EmailContent
          emailDetail={detail}
          loading={false}
          onReply={vi.fn()}
          onForward={vi.fn()}
          onDelete={vi.fn()}
          onToggleStar={vi.fn()}
        />,
        {wrapper: TestProviders},
      );

      expect(await screen.findByText("Inline preview unavailable")).toBeInTheDocument();
      expect(api.downloadAttachment).not.toHaveBeenCalled();
    });
  });

  describe("subject click-to-copy", () => {
    it("copies the subject text to the clipboard when the subject is clicked", async () => {
      const mockWriteText = vi.fn().mockResolvedValue(undefined);
      Object.defineProperty(globalThis.navigator, "clipboard", {
        value: { writeText: mockWriteText },
        configurable: true,
        writable: true,
      });

      render(
        <EmailContent
          emailDetail={createMockEmailDetail({ email: { subject: "HENNGE Admission Challenge" } })}
          onReply={vi.fn()}
          onForward={vi.fn()}
          onDelete={vi.fn()}
          onToggleStar={vi.fn()}
        />,
        { wrapper: TestProviders },
      );

      const subjectBtn = await screen.findByRole("button", { name: /copy subject/i });
      fireEvent.click(subjectBtn);

      await waitFor(() => {
        expect(mockWriteText).toHaveBeenCalledWith("HENNGE Admission Challenge");
      });
    });

    it("opens the sender address popover when the From sender row is clicked", async () => {
      render(
        <EmailContent
          emailDetail={createMockEmailDetail({
            email: {
              from_address: "no-reply@tablecheck.com",
              from_name: "TableCheck",
            },
          })}
          onReply={vi.fn()}
          onForward={vi.fn()}
          onDelete={vi.fn()}
          onToggleStar={vi.fn()}
        />,
        { wrapper: TestProviders },
      );

      const senderTrigger = await screen.findByRole("button", { name: /sender options for TableCheck/i });
      fireEvent.click(senderTrigger);

      await waitFor(() => {
        expect(screen.getByRole("button", { name: /compose an email/i })).toBeInTheDocument();
      });
      expect(screen.getByRole("button", { name: /copy email address/i })).toBeInTheDocument();
    });
  });
});
