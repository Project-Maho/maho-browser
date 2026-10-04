import { render, screen, fireEvent, waitFor } from "@testing-library/react";
import type { ComponentProps } from "react";
import { describe, it, expect, vi, beforeEach } from "vitest";
import { Sidebar } from "../Sidebar";
import { createMockAccount, createMockFolder } from "../../../test/mocks";
import { ConfirmProvider } from "../../ui/ConfirmDialog";

function renderSidebar(overrides: Partial<ComponentProps<typeof Sidebar>> = {}) {
  const props: ComponentProps<typeof Sidebar> = {
    accounts: [createMockAccount()],
    folders: [],
    selectedFolderId: null,
    onSelectFolder: vi.fn(),
    onOpenSettings: vi.fn(),
    loading: false,
    ...overrides,
  };

  return render(
    <ConfirmProvider>
      <Sidebar {...props} />
    </ConfirmProvider>
  );
}

describe("Sidebar", () => {
  it("groups navigation into primary mailboxes, views, folders, and more", () => {
    renderSidebar({
      folders: [
        createMockFolder({ id: "inbox", name: "Inbox", folder_type: "Inbox" }),
        createMockFolder({ id: "custom", name: "Projects", folder_type: "Custom" }),
      ],
    });

    expect(screen.getByText("Inbox")).toBeInTheDocument();
    expect(screen.getByText("Starred")).toBeInTheDocument();
    expect(screen.getByText("Snoozed")).toBeInTheDocument();
    expect(screen.getByText("Views")).toBeInTheDocument();
    expect(screen.getAllByText("Folders").length).toBeGreaterThan(0);
    expect(screen.getByText("More")).toBeInTheDocument();
    expect(screen.getByText("Calendar")).toBeInTheDocument();
  });

  beforeEach(() => {
    vi.clearAllMocks();
    localStorage.clear();
  });

  it("renders the redesigned shell with Views, Folders, and More sections", () => {
    renderSidebar({ updateAvailable: true, updateVersion: "1.2.0" });

    // Section headers
    expect(screen.getByText("Views")).toBeInTheDocument();
    expect(screen.getByText("More")).toBeInTheDocument();

    // Folders section items
    expect(screen.getByText("Archive")).toBeInTheDocument();
    expect(screen.getByText("Trash")).toBeInTheDocument();
    expect(screen.getByText("Spam")).toBeInTheDocument();

    expect(screen.getByText("Pins")).toBeInTheDocument();
    expect(screen.getByText("Reminders")).toBeInTheDocument();
    expect(screen.getByText("Calendar")).toBeInTheDocument();

    // Footer items
    expect(screen.queryByText("Update available")).not.toBeInTheDocument();
    expect(screen.getByText("Settings")).toBeInTheDocument();
  });

  it("renders primary parent rows (Inbox, Pins, Sent, Trash, Drafts) with expand/collapse", () => {
    const gmailAcc = createMockAccount({
      id: "acc-gmail",
      email: "user@gmail.com",
      display_name: "Gmail User",
    });
    const naverAcc = createMockAccount({
      id: "acc-naver",
      email: "user@naver.com",
      display_name: "Naver User",
    });
    const folders = [
      createMockFolder({
        id: "inbox-gmail",
        name: "INBOX",
        folder_type: "Inbox",
        account_id: gmailAcc.id,
        unread_count: 5,
      }),
      createMockFolder({
        id: "inbox-naver",
        name: "Inbox",
        folder_type: "Inbox",
        account_id: naverAcc.id,
        unread_count: 3,
      }),
      createMockFolder({
        id: "sent-gmail",
        name: "Sent",
        folder_type: "Sent",
        account_id: gmailAcc.id,
        unread_count: 0,
      }),
    ];

    renderSidebar({ accounts: [gmailAcc, naverAcc], folders });

    // Primary parent rows should be visible
    expect(screen.getByText("Inbox")).toBeInTheDocument();
    expect(screen.getByText("Sent")).toBeInTheDocument();

    // Aggregate unread count (5 + 3 = 8)
    expect(screen.getByText("8")).toBeInTheDocument();

    // Expand/collapse functionality
    const expandButtons = screen.getAllByLabelText(/expand .*|collapse .*/i);
    expect(expandButtons.length).toBeGreaterThan(0);

    // Initially Inbox is expanded (default), child accounts visible
    expect(screen.getByText("user@gmail.com")).toBeInTheDocument();
    expect(screen.getByText("user@naver.com")).toBeInTheDocument();

    fireEvent.click(screen.getByLabelText("Collapse Inbox"));
    expect(screen.queryByText("user@gmail.com")).not.toBeInTheDocument();
    expect(screen.queryByText("user@naver.com")).not.toBeInTheDocument();

    fireEvent.click(screen.getByLabelText("Expand Inbox"));
    expect(screen.getByText("user@gmail.com")).toBeInTheDocument();
    expect(screen.getByText("user@naver.com")).toBeInTheDocument();

    expect(screen.queryAllByText("user@gmail.com")).toHaveLength(1);
    fireEvent.click(screen.getByLabelText("Expand Sent"));
    expect(screen.queryAllByText("user@gmail.com")).toHaveLength(2);

    fireEvent.click(screen.getByLabelText("Collapse Sent"));
    expect(screen.queryAllByText("user@gmail.com")).toHaveLength(1);
  });

  it("renders nested child account rows under the correct parent groups", () => {
    const gmailAcc = createMockAccount({
      id: "acc-gmail",
      email: "user@gmail.com",
      display_name: "Gmail User",
    });
    const outlookAcc = createMockAccount({
      id: "acc-outlook",
      email: "user@outlook.com",
      display_name: "Outlook User",
    });
    const folders = [
      createMockFolder({
        id: "inbox-gmail",
        name: "INBOX",
        folder_type: "Inbox",
        account_id: gmailAcc.id,
        unread_count: 5,
      }),
      createMockFolder({
        id: "inbox-outlook",
        name: "Inbox",
        folder_type: "Inbox",
        account_id: outlookAcc.id,
        unread_count: 2,
      }),
      createMockFolder({
        id: "sent-gmail",
        name: "Sent",
        folder_type: "Sent",
        account_id: gmailAcc.id,
        unread_count: 0,
      }),
    ];

    renderSidebar({ accounts: [gmailAcc, outlookAcc], folders });

    // Child rows should show account emails
    expect(screen.getByText("user@gmail.com")).toBeInTheDocument();
    expect(screen.getByText("user@outlook.com")).toBeInTheDocument();

    // Individual unread counts on child rows
    expect(screen.getByText("5")).toBeInTheDocument();
    expect(screen.getByText("2")).toBeInTheDocument();
  });

  it("uses More as a section rather than an inert list item", () => {
    renderSidebar();

    const moreLabel = screen.getByRole("heading", { name: "More" });
    const moreButton = screen.queryByRole("button", { name: "More" });

    expect(moreLabel).toBeInTheDocument();
    expect(moreButton).not.toBeInTheDocument();
  });

  it("renders a visible gutter collapse control in expanded mode", () => {
    const onToggleCollapsed = vi.fn();

    renderSidebar({ onToggleCollapsed });

    const collapseButton = screen.getByRole("button", { name: "Collapse sidebar" });

    expect(collapseButton).toBeInTheDocument();
    expect(collapseButton).toHaveAccessibleName("Collapse sidebar");
    expect(collapseButton).toHaveAttribute("title", "Collapse sidebar");

    fireEvent.click(collapseButton);
    expect(onToggleCollapsed).toHaveBeenCalledOnce();
  });

  it("does not render a dedicated bottom shell toggle in expanded mode", () => {
    renderSidebar();

    expect(screen.queryByRole("button", { name: "Expand sidebar" })).not.toBeInTheDocument();
  });

  it("renders More section items as clickable rows", () => {
    renderSidebar();

    expect(screen.getByRole("button", { name: /archive/i })).toBeInTheDocument();
    expect(screen.getByRole("button", { name: /trash/i })).toBeInTheDocument();
  });

  it("renders Views section with contextual destinations", () => {
    renderSidebar();

    expect(screen.getByText("Views")).toBeInTheDocument();

    expect(screen.getByText("Pins")).toBeInTheDocument();
    expect(screen.getByText("Reminders")).toBeInTheDocument();
    expect(screen.getByText("Calendar")).toBeInTheDocument();
  });

  it("keeps aggregate parent selection separate from child folder selection", async () => {
    const onSelectAggregate = vi.fn();
    const onSelectFolder = vi.fn();
    const acc = createMockAccount({ id: "acc-1", email: "alice@example.com", display_name: "Alice" });
    const folders = [
      createMockFolder({
        id: "inbox",
        name: "Inbox",
        folder_type: "Inbox",
        account_id: acc.id,
        unread_count: 3,
      }),
    ];

    renderSidebar({
      accounts: [acc],
      folders,
      selectedFolderType: "Inbox",
      onSelectAggregate,
      onSelectFolder,
    });

    // Click on parent row (Inbox) to select aggregate
    const inboxButton = screen.getByText("Inbox").closest("button");
    expect(inboxButton).toBeInTheDocument();
    fireEvent.click(inboxButton!);
    expect(onSelectAggregate).toHaveBeenCalledWith("Inbox");

    // Click on child account row to select specific folder
    const accountButton = screen.getByText("alice@example.com").closest("button");
    expect(accountButton).toBeInTheDocument();
    fireEvent.click(accountButton!);
    expect(onSelectFolder).toHaveBeenCalledWith("inbox");
  });

  it("keeps the footer rows visible and actionable", async () => {
    const onOpenSettings = vi.fn();
    const onUpdateClick = vi.fn();
    renderSidebar({
      onOpenSettings,
      onUpdateClick,
      updateAvailable: true,
      updateVersion: "2.0.0",
    });

    expect(screen.queryByText("Update available")).not.toBeInTheDocument();
    expect(onUpdateClick).not.toHaveBeenCalled();

    // Settings row visible and clickable
    expect(screen.getByText("Settings")).toBeInTheDocument();
    fireEvent.click(screen.getByText("Settings"));
    await waitFor(() => expect(onOpenSettings).toHaveBeenCalledOnce());
  });

  it("does not show update row when updateAvailable is false", () => {
    renderSidebar({ updateAvailable: false });

    expect(screen.queryByText("Update available")).not.toBeInTheDocument();
    expect(screen.getByText("Settings")).toBeInTheDocument();
  });

  it("renders icon-only collapsed mode with More preserved as a normal item", () => {
    renderSidebar({ collapsed: true });

    expect(screen.queryByText("Folders")).not.toBeInTheDocument();
    expect(screen.queryByRole("button", { name: "More" })).not.toBeInTheDocument();
    expect(screen.getByTitle("Archive")).toBeInTheDocument();
    expect(screen.queryByRole("button", { name: "Expand sidebar" })).not.toBeInTheDocument();
  });

  it("renders custom folders in a single global Custom Folders section", () => {
    const gmailAcc = createMockAccount({ id: "acc-gmail", email: "user@gmail.com", display_name: "Gmail User" });
    const outlookAcc = createMockAccount({ id: "acc-outlook", email: "user@outlook.com", display_name: "Outlook User" });
    const folders = [
      createMockFolder({ id: "inbox-gmail", name: "INBOX", folder_type: "Inbox", account_id: gmailAcc.id, unread_count: 1 }),
      createMockFolder({ id: "custom-gmail", name: "Projects", path: "Projects", folder_type: "Custom", account_id: gmailAcc.id }),
      createMockFolder({ id: "custom-outlook", name: "Clients", path: "Clients", folder_type: "Custom", account_id: outlookAcc.id }),
    ];

    renderSidebar({ accounts: [gmailAcc, outlookAcc], folders });

    expect(screen.getByText("Custom Folders")).toBeInTheDocument();
    expect(screen.queryByLabelText("Expand user@gmail.com folders")).not.toBeInTheDocument();
    expect(screen.queryByLabelText("Expand user@outlook.com folders")).not.toBeInTheDocument();

    expect(screen.queryByText("Projects")).not.toBeInTheDocument();
    expect(screen.queryByText("Clients")).not.toBeInTheDocument();

    fireEvent.click(screen.getByLabelText("Expand Custom Folders section"));
    expect(screen.getByText("Projects")).toBeInTheDocument();
    expect(screen.getByText("Clients")).toBeInTheDocument();
  });

  it("supports expand/collapse of the global custom folder tree with nested children", () => {
    const account = createMockAccount({ id: "acc-custom", email: "custom@example.com", display_name: "Custom" });
    const folders = [
      createMockFolder({ id: "custom-parent", name: "Projects", path: "Projects", folder_type: "Custom", account_id: account.id }),
      createMockFolder({ id: "custom-child", name: "Launch", path: "Projects/Launch", folder_type: "Custom", account_id: account.id }),
    ];

    renderSidebar({ accounts: [account], folders });

    expect(screen.queryByText("Projects")).not.toBeInTheDocument();
    expect(screen.queryByText("Launch")).not.toBeInTheDocument();

    fireEvent.click(screen.getByLabelText("Expand Custom Folders section"));
    expect(screen.getByText("Projects")).toBeInTheDocument();

    fireEvent.click(screen.getByLabelText("Expand Projects"));
    expect(screen.getByText("Launch")).toBeInTheDocument();

    fireEvent.click(screen.getByLabelText("Collapse Custom Folders section"));
    expect(screen.queryByText("Projects")).not.toBeInTheDocument();
    expect(screen.queryByText("Launch")).not.toBeInTheDocument();
  });

  it("renders a global Custom Folders section when custom folders exist", () => {
    const account = createMockAccount({ id: "acc-1", email: "user@example.com", display_name: "User" });
    const folders = [
      createMockFolder({ id: "custom-1", name: "Work", path: "Work", folder_type: "Custom", account_id: account.id }),
    ];
    
    renderSidebar({ accounts: [account], folders });

    expect(screen.getByText("Custom Folders")).toBeInTheDocument();
    expect(screen.getByRole("button", { name: "Expand Custom Folders section" })).toBeInTheDocument();
  });

  it("renders a matching gutter expand control in collapsed mode", () => {
    const onToggleCollapsed = vi.fn();

    renderSidebar({ collapsed: true, onToggleCollapsed });

    expect(screen.queryByText("Folders")).not.toBeInTheDocument();
    expect(screen.getByLabelText("Inbox")).toBeInTheDocument();
    expect(screen.getByRole("button", { name: "Settings" })).toBeInTheDocument();
    expect(screen.queryByRole("button", { name: "Collapse sidebar" })).not.toBeInTheDocument();

    const expandButton = screen.getByRole("button", { name: "Expand sidebar" });

    expect(expandButton).toBeInTheDocument();
    expect(expandButton).toHaveAttribute("title", "Expand sidebar");

    fireEvent.click(expandButton);

    expect(onToggleCollapsed).toHaveBeenCalledOnce();
  });

  it("does not render a dedicated bottom shell toggle in collapsed mode", () => {
    renderSidebar({ collapsed: true, onToggleCollapsed: vi.fn() });

    expect(screen.getAllByTitle("Settings")).toHaveLength(1);
    expect(screen.getAllByTitle("Archive")).toHaveLength(1);
    expect(screen.getByTitle("Expand sidebar")).toBeInTheDocument();
  });

  it("does not show a generic online status in expanded mode", () => {
    renderSidebar({ isOnline: true });

    expect(screen.queryByRole("status", { name: "Network online" })).not.toBeInTheDocument();
    expect(screen.queryByText("Online")).not.toBeInTheDocument();
  });

  it("shows offline network status in expanded mode", () => {
    renderSidebar({ isOnline: false });

    const status = screen.getByRole("status", { name: "Network offline" });
    expect(status).toBeInTheDocument();
    expect(screen.getByText("Offline")).toBeInTheDocument();
  });

  it("does not show a generic online indicator in collapsed mode", () => {
    renderSidebar({ collapsed: true, isOnline: true });

    expect(screen.queryByRole("status", { name: "Network online" })).not.toBeInTheDocument();
  });

  it("shows offline network status in collapsed mode", () => {
    renderSidebar({ collapsed: true, isOnline: false });

    const status = screen.getByRole("status", { name: "Network offline" });
    expect(status).toBeInTheDocument();
  });
});
