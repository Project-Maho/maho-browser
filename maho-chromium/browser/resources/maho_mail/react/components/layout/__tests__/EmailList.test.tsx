import { render, screen, fireEvent, act, waitFor } from "@testing-library/react";
import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import { EmailList } from "../EmailList";
import { TestProviders, createMockEmail } from "../../../test/mocks";
import * as api from "../../../api";

// Mock the API module to prevent actual network calls during search tests
vi.mock("../../../api", async () => {
  const actual = await vi.importActual("../../../api");
  return {
    ...(actual as object),
  searchEmails: vi.fn().mockResolvedValue({ emails: [], total_count: 0, query: "" }),
  naturalLanguageSearch: vi.fn().mockResolvedValue({ emails: [], total_count: 0, query: "" }),
    deleteEmail: vi.fn().mockResolvedValue(undefined),
    moveEmail: vi.fn().mockResolvedValue(undefined),
    markRead: vi.fn().mockResolvedValue(undefined),
    markUnread: vi.fn().mockResolvedValue(undefined),
    batchMarkRead: vi.fn().mockResolvedValue(undefined),
    batchMarkUnread: vi.fn().mockResolvedValue(undefined),
    batchDelete: vi.fn().mockResolvedValue(undefined),
    batchToggleStar: vi.fn().mockResolvedValue(undefined),
    batchMove: vi.fn().mockResolvedValue(undefined),
    md5Hash: vi.fn().mockResolvedValue("mocked-hash"),
  };
});

// @tanstack/react-virtual reads offsetHeight to size the virtual window.
// JSDOM returns 0 for all layout properties, so we stub it to 600px.
const originalDescriptor = Object.getOwnPropertyDescriptor(HTMLElement.prototype, "offsetHeight");

describe("EmailList", () => {
  beforeEach(() => {
    vi.clearAllMocks();
    localStorage.clear();
    vi.mocked(api.searchEmails).mockResolvedValue({ emails: [], total_count: 0, query: "" });
    vi.mocked(api.naturalLanguageSearch).mockResolvedValue({ emails: [], total_count: 0, query: "" });
    Object.defineProperty(HTMLElement.prototype, "offsetHeight", {
      configurable: true,
      get() { return 600; },
    });
  });

  afterEach(() => {
    if (originalDescriptor) {
      Object.defineProperty(HTMLElement.prototype, "offsetHeight", originalDescriptor);
    }
  });

  it("renders email rows with subject, sender, and snippet", async () => {
    const emails = [
      createMockEmail({ id: "e1", subject: "Subject 1", from_name: "Alice", snippet: "Snippet 1" }),
      createMockEmail({ id: "e2", subject: "Subject 2", from_name: "Bob", snippet: "Snippet 2" }),
    ];

    render(
      <EmailList
        emails={emails}
        selectedEmailId={null}
        onSelectEmail={vi.fn()}
        onToggleStar={vi.fn()}
        loading={false}
      />,
      { wrapper: TestProviders },
    );

    await act(async () => {});

    await waitFor(() => {
      expect(screen.getByText("Subject 1")).toBeInTheDocument();
      expect(screen.getByText("Alice")).toBeInTheDocument();
      expect(screen.getByText("Snippet 1")).toBeInTheDocument();
    });
  });

  it("highlights selected email", async () => {
    const emails = [
      createMockEmail({ id: "e1", subject: "Selected", from_name: "Alice", snippet: "S" }),
      createMockEmail({ id: "e2", subject: "Other", from_name: "Bob", snippet: "O" }),
    ];

    render(
      <EmailList
        emails={emails}
        selectedEmailId={"e1"}
        onSelectEmail={vi.fn()}
        onToggleStar={vi.fn()}
        loading={false}
      />,
      { wrapper: TestProviders },
    );

    const row = screen.getByText("Selected").closest("[role=\"row\"]");
    expect(row).not.toBeNull();
    await waitFor(() => {
      expect(row).toHaveClass("bg-primary/10");
    });
  });

  it("calls onSelectEmail when row is clicked", async () => {
    const onSelectEmail = vi.fn();
    const emails = [createMockEmail({ id: "e1", subject: "Clickable", from_name: "Alice" })];

    render(
      <EmailList
        emails={emails}
        selectedEmailId={null}
        onSelectEmail={onSelectEmail}
        onToggleStar={vi.fn()}
        loading={false}
      />,
      { wrapper: TestProviders },
    );

    const row = screen.getByText("Clickable").closest("[role=\"row\"]");
    expect(row).not.toBeNull();
    fireEvent.click(row as Element);
    await waitFor(() => expect(onSelectEmail).toHaveBeenCalledWith("e1"));
  });

  it("shows pinned state immediately after pin click without selecting the row", async () => {
    const onSelectEmail = vi.fn();
    const onPin = vi.fn().mockResolvedValue(undefined);
    const emails = [createMockEmail({ id: "e1", subject: "Pin me", from_name: "Alice" })];

    render(
      <EmailList
        emails={emails}
        selectedEmailId={null}
        onSelectEmail={onSelectEmail}
        onToggleStar={vi.fn()}
        onPin={onPin}
        loading={false}
      />,
      { wrapper: TestProviders },
    );

    await act(async () => {});

    const pinButton = screen.getByTitle("Pin");
    fireEvent.click(pinButton);

    await waitFor(() => {
      expect(onPin).toHaveBeenCalledWith("e1", true);
      expect(onSelectEmail).not.toHaveBeenCalled();
      expect(screen.getByTitle("Unpin")).toHaveClass("bg-primary/10", "text-primary");
    });
  });

  it("shows unpinned state immediately after clicking an active pin", async () => {
    const onPin = vi.fn().mockResolvedValue(undefined);
    const emails = [createMockEmail({ id: "e1", subject: "Unpin me", from_name: "Alice" })];

    render(
      <EmailList
        emails={emails}
        selectedEmailId={null}
        onSelectEmail={vi.fn()}
        onToggleStar={vi.fn()}
        onPin={onPin}
        loading={false}
      />,
      { wrapper: TestProviders },
    );

    await act(async () => {});

    fireEvent.click(screen.getByTitle("Pin"));

    await waitFor(() => {
      expect(screen.getByTitle("Unpin")).toBeInTheDocument();
    });

    fireEvent.click(screen.getByTitle("Unpin"));

    await waitFor(() => {
      expect(onPin).toHaveBeenNthCalledWith(1, "e1", true);
      expect(onPin).toHaveBeenNthCalledWith(2, "e1", false);
      expect(screen.getByTitle("Pin")).toHaveClass("text-muted-foreground");
    });
  });

  it("shows search input", async () => {
    render(
      <EmailList
        emails={[]}
        selectedEmailId={null}
        onSelectEmail={vi.fn()}
        onToggleStar={vi.fn()}
        loading={false}
      />,
      { wrapper: TestProviders },
    );

    await waitFor(() => {
      expect(screen.getByPlaceholderText("Search emails...")).toBeInTheDocument();
    });
  });

  it("does not expose a Mail-specific Agent action", async () => {
    render(
      <EmailList
        emails={[]}
        selectedEmailId={null}
        onSelectEmail={vi.fn()}
        onToggleStar={vi.fn()}
        onCompose={vi.fn()}
        onSync={vi.fn()}
        loading={false}
      />,
      { wrapper: TestProviders },
    );

    await waitFor(() => {
      expect(screen.queryByRole("button", { name: /agent/i })).not.toBeInTheDocument();
      expect(screen.queryByTitle(/agent/i)).not.toBeInTheDocument();
    });
  });

  it("presents mailbox context and Compose as the primary action", async () => {
    render(
      <EmailList
        emails={[]}
        selectedEmailId={null}
        onSelectEmail={vi.fn()}
        onToggleStar={vi.fn()}
        onCompose={vi.fn()}
        onSync={vi.fn()}
        mailboxTitle="Inbox"
        loading={false}
      />,
      { wrapper: TestProviders },
    );

    expect(screen.getByRole("heading", { name: "Inbox" })).toBeInTheDocument();
    expect(screen.getByRole("button", { name: /compose/i })).toHaveTextContent(/compose/i);
    expect(screen.getByRole("button", { name: /ask about mail/i })).toBeInTheDocument();
  });

  it("shows search filter button", async () => {
    render(
      <EmailList
        emails={[]}
        selectedEmailId={null}
        onSelectEmail={vi.fn()}
        onToggleStar={vi.fn()}
        loading={false}
      />,
      { wrapper: TestProviders },
    );

    await waitFor(() => {
      expect(screen.getByTitle("Search filters")).toBeInTheDocument();
    });
  });

  it("shows natural language search button", async () => {
    render(
      <EmailList
        emails={[]}
        selectedEmailId={null}
        onSelectEmail={vi.fn()}
        onToggleStar={vi.fn()}
        loading={false}
      />,
      { wrapper: TestProviders },
    );

    await waitFor(() => {
      expect(screen.getByTitle("Natural language search")).toBeInTheDocument();
    });
  });

  it("has search input that accepts user input", async () => {
    render(
      <EmailList
        emails={[]}
        selectedEmailId={null}
        onSelectEmail={vi.fn()}
        onToggleStar={vi.fn()}
        loading={false}
      />,
      { wrapper: TestProviders },
    );

    const searchInput = screen.getByPlaceholderText("Search emails...");

    await waitFor(() => {
      expect(searchInput).toBeInTheDocument();
    });

    fireEvent.change(searchInput, { target: { value: "test query" } });

    expect(searchInput).toHaveValue("test query");
  });

  it("shows a visible scope selector near the search input", async () => {
    render(
      <EmailList
        emails={[]}
        selectedEmailId={null}
        onSelectEmail={vi.fn()}
        onToggleStar={vi.fn()}
        loading={false}
        accountId="acc-1"
        folderId="folder-1"
      />,
      { wrapper: TestProviders },
    );

    expect(screen.getByText("Scope")).toBeInTheDocument();
    expect(screen.getByRole("combobox")).toBeInTheDocument();
    expect(screen.getByText("Current view")).toBeInTheDocument();
  });

  it("shows a search results header with total count and scope during active search", async () => {
    const resultEmail = createMockEmail({
      id: "search-1",
      subject: "Alpha update",
      from_name: "Alice Alpha",
      snippet: "Alpha snippet",
    });
    vi.mocked(api.searchEmails).mockResolvedValue({
      emails: [resultEmail],
      total_count: 12,
      query: "alpha",
    });

    render(
      <EmailList
        emails={[]}
        selectedEmailId={null}
        onSelectEmail={vi.fn()}
        onToggleStar={vi.fn()}
        loading={false}
        accountId="acc-1"
        folderId="folder-1"
      />,
      { wrapper: TestProviders },
    );

    fireEvent.change(screen.getByPlaceholderText("Search emails..."), {
      target: { value: "alpha" },
    });

    await waitFor(() => {
      expect(vi.mocked(api.searchEmails)).toHaveBeenCalled();
    }, { timeout: 3000 });

    await waitFor(() => {
      expect(screen.getByText("Search results")).toBeInTheDocument();
      expect(screen.getAllByText(/12 results in Current folder/i).length).toBeGreaterThan(0);
      expect(screen.getByText('"alpha"')).toBeInTheDocument();
    }, { timeout: 3000 });
  });

  it("highlights literal free-text matches in sender, subject, and snippet", async () => {
    const resultEmail = createMockEmail({
      id: "search-2",
      subject: "Alpha roadmap",
      from_name: "Alice Alpha",
      snippet: "Preview alpha launch",
    });
    vi.mocked(api.searchEmails).mockResolvedValue({
      emails: [resultEmail],
      total_count: 1,
      query: "alpha",
    });

    const { container } = render(
      <EmailList
        emails={[]}
        selectedEmailId={null}
        onSelectEmail={vi.fn()}
        onToggleStar={vi.fn()}
        loading={false}
      />,
      { wrapper: TestProviders },
    );

    fireEvent.change(screen.getByPlaceholderText("Search emails..."), {
      target: { value: "alpha" },
    });

    await waitFor(() => {
      expect(container.querySelectorAll(".bg-primary\\/15").length).toBeGreaterThanOrEqual(3);
    });
  });

  it("does not apply literal highlighting for natural language search input", async () => {
    const resultEmail = createMockEmail({
      id: "search-3",
      subject: "Alpha roadmap",
      from_name: "Alice Alpha",
      snippet: "Preview alpha launch",
    });
    vi.mocked(api.naturalLanguageSearch).mockResolvedValue({
      emails: [resultEmail],
      total_count: 1,
      query: "show me alpha emails",
    });

    const { container } = render(
      <EmailList
        emails={[]}
        selectedEmailId={null}
        onSelectEmail={vi.fn()}
        onToggleStar={vi.fn()}
        loading={false}
        accountId="acc-1"
        folderId="folder-1"
      />,
      { wrapper: TestProviders },
    );

    fireEvent.change(screen.getByPlaceholderText("Search emails..."), {
      target: { value: "? show me alpha emails" },
    });

    // Wait for the API to be called and results to render
    await waitFor(() => {
      expect(vi.mocked(api.naturalLanguageSearch)).toHaveBeenCalled();
    }, { timeout: 2000 });

    await waitFor(() => {
      expect(screen.getByText("Search results")).toBeInTheDocument();
    }, { timeout: 2000 });

    expect(screen.getByText(/natural language search/i)).toBeInTheDocument();
    expect(container.querySelectorAll(".bg-primary\\/15")).toHaveLength(0);
  });

  it("displays different empty state when search query is entered", async () => {
    render(
      <EmailList
        emails={[]}
        selectedEmailId={null}
        onSelectEmail={vi.fn()}
        onToggleStar={vi.fn()}
        loading={false}
      />,
      { wrapper: TestProviders },
    );

    const searchInput = screen.getByPlaceholderText("Search emails...");

    await waitFor(() => {
      expect(searchInput).toBeInTheDocument();
    });

    fireEvent.change(searchInput, { target: { value: "nonexistent" } });

    await waitFor(() => {
      expect(screen.getByDisplayValue("nonexistent")).toBeInTheDocument();
    });
  });

  it("shows an idle search panel with recent searches and tips when the search input is focused", async () => {
    localStorage.setItem("maho-search-history", JSON.stringify([
      { query: "project phoenix", filters: {} },
    ]));

    render(
      <EmailList
        emails={[]}
        selectedEmailId={null}
        onSelectEmail={vi.fn()}
        onToggleStar={vi.fn()}
        loading={false}
      />,
      { wrapper: TestProviders },
    );

    const searchInput = screen.getByPlaceholderText("Search emails...");
    fireEvent.focus(searchInput);

    await waitFor(() => {
      expect(screen.getByText("Search your mail")).toBeInTheDocument();
      expect(screen.getByText("Recent searches")).toBeInTheDocument();
      expect(screen.getByText("project phoenix")).toBeInTheDocument();
      expect(screen.getByText("Tips")).toBeInTheDocument();
      expect(screen.getByText("Exact phrase")).toBeInTheDocument();
    });
  });

  it("loads malformed recent search storage safely and falls back to tips-only idle state", async () => {
    localStorage.setItem("maho-search-history", "{not-valid-json");

    render(
      <EmailList
        emails={[]}
        selectedEmailId={null}
        onSelectEmail={vi.fn()}
        onToggleStar={vi.fn()}
        loading={false}
      />,
      { wrapper: TestProviders },
    );

    fireEvent.focus(screen.getByPlaceholderText("Search emails..."));

    await waitFor(() => {
      expect(screen.getByText("Search your mail")).toBeInTheDocument();
      expect(screen.queryByText("Recent searches")).not.toBeInTheDocument();
      expect(screen.getByText("Tips")).toBeInTheDocument();
    });
  });

  it("restores a recent search chip into the current query", async () => {
    localStorage.setItem("maho-search-history", JSON.stringify([
      { query: "invoice", filters: {} },
    ]));

    render(
      <EmailList
        emails={[]}
        selectedEmailId={null}
        onSelectEmail={vi.fn()}
        onToggleStar={vi.fn()}
        loading={false}
      />,
      { wrapper: TestProviders },
    );

    const searchInput = screen.getByPlaceholderText("Search emails...");
    fireEvent.focus(searchInput);

    await waitFor(() => {
      expect(screen.getByRole("button", { name: "invoice" })).toBeInTheDocument();
    });

    fireEvent.click(screen.getByRole("button", { name: "invoice" }));

    await waitFor(() => {
      expect(searchInput).toHaveValue("invoice");
    });
  });

  it("applies a clickable tip to the current query using supported UI behavior", async () => {
    render(
      <EmailList
        emails={[]}
        selectedEmailId={null}
        onSelectEmail={vi.fn()}
        onToggleStar={vi.fn()}
        loading={false}
      />,
      { wrapper: TestProviders },
    );

    const searchInput = screen.getByPlaceholderText("Search emails...");
    fireEvent.focus(searchInput);

    await waitFor(() => {
      expect(screen.getByRole("button", { name: /Exact phrase/i })).toBeInTheDocument();
    });

    fireEvent.click(screen.getByRole("button", { name: /Exact phrase/i }));

    await waitFor(() => {
      expect(searchInput).toHaveValue('"quarterly review"');
    });
  });

  it("persists a successful search into recent history", async () => {
    vi.mocked(api.searchEmails).mockResolvedValue({
      emails: [createMockEmail({ id: "persist-1", subject: "Invoice update" })],
      total_count: 1,
      query: "invoice",
    });

    render(
      <EmailList
        emails={[]}
        selectedEmailId={null}
        onSelectEmail={vi.fn()}
        onToggleStar={vi.fn()}
        loading={false}
      />,
      { wrapper: TestProviders },
    );

    fireEvent.change(screen.getByPlaceholderText("Search emails..."), {
      target: { value: "invoice" },
    });

    await waitFor(() => {
      expect(vi.mocked(api.searchEmails)).toHaveBeenCalled();
    });

    await waitFor(() => {
      const stored = localStorage.getItem("maho-search-history");
      expect(stored).not.toBeNull();
      expect(JSON.parse(stored ?? "[]")).toEqual([
        {
          query: "invoice",
          filters: {},
        },
      ]);
    });
  });

  it("shows empty state when no emails", async () => {
    render(
      <EmailList
        emails={[]}
        selectedEmailId={null}
        onSelectEmail={vi.fn()}
        onToggleStar={vi.fn()}
        loading={false}
      />,
      { wrapper: TestProviders },
    );

    await waitFor(() => {
      expect(screen.getByText("All caught up")).toBeInTheDocument();
    });
  });

  it("shows a retryable sync error instead of all caught up", () => {
    const onSync = vi.fn();
    render(
      <EmailList
        emails={[]}
        selectedEmailId={null}
        onSelectEmail={vi.fn()}
        onToggleStar={vi.fn()}
        loading={false}
        syncError="Failed to sync Gmail inbox"
        onSync={onSync}
      />,
      { wrapper: TestProviders },
    );

    expect(screen.getByRole("alert")).toHaveTextContent("Failed to sync Gmail inbox");
    expect(screen.queryByText("All caught up")).not.toBeInTheDocument();
    fireEvent.click(screen.getByRole("button", { name: "Try again" }));
    expect(onSync).toHaveBeenCalledTimes(1);
  });

  it("shows loading skeleton when loading and no emails", async () => {
    render(
      <EmailList
        emails={[]}
        selectedEmailId={null}
        onSelectEmail={vi.fn()}
        onToggleStar={vi.fn()}
        loading={true}
      />,
      { wrapper: TestProviders },
    );

    await act(async () => {});
    await waitFor(() => {
      expect(document.querySelectorAll(".animate-pulse").length).toBeGreaterThan(0);
    });
  });
});
