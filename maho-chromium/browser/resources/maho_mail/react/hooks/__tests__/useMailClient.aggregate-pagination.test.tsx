import { describe, it, expect, vi, beforeEach } from "vitest";
import { renderHook, waitFor, act } from "@testing-library/react";
import { useMailClient } from "../useMailClient";

vi.mock("../../api", () => ({
  listAccounts: vi.fn(),
  listFolders: vi.fn(),
  syncFolders: vi.fn(),
  listEmails: vi.fn(),
  syncFolder: vi.fn(),
  getEmail: vi.fn(),
  markRead: vi.fn(),
  toggleStar: vi.fn(),
  deleteEmail: vi.fn(),
}));

import * as api from "../../api";

describe("useMailClient aggregate pagination with per-account offsets", () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  it("loads more emails with independent per-account offsets", async () => {
    const accounts = [
      { id: "acc-1", email: "alice@example.com", display_name: "Alice", auth_type: "password" as const },
      { id: "acc-2", email: "bob@example.com", display_name: "Bob", auth_type: "password" as const },
    ];

    const foldersAcc1 = [
      { id: "inbox-1", account_id: "acc-1", name: "Inbox", path: "INBOX", folder_type: "Inbox" as const, unread_count: 5, total_count: 100 },
    ];

    const foldersAcc2 = [
      { id: "inbox-2", account_id: "acc-2", name: "Inbox", path: "INBOX", folder_type: "Inbox" as const, unread_count: 3, total_count: 80 },
    ];

    // First page: acc-1 returns 2 emails, acc-2 returns 1 email
    const firstPageAcc1 = [
      { id: "e1", account_id: "acc-1", folder_id: "inbox-1", uid: 1, message_id: "<msg-1@test>", subject: "Email 1", from_address: "a@example.com", from_name: "A", date: "2026-01-03T00:00:00Z", snippet: "Snippet 1", is_read: false, is_starred: false, is_draft: false, has_attachments: false },
      { id: "e2", account_id: "acc-1", folder_id: "inbox-1", uid: 2, message_id: "<msg-2@test>", subject: "Email 2", from_address: "a@example.com", from_name: "A", date: "2026-01-02T00:00:00Z", snippet: "Snippet 2", is_read: true, is_starred: false, is_draft: false, has_attachments: false },
    ];

    const firstPageAcc2 = [
      { id: "e3", account_id: "acc-2", folder_id: "inbox-2", uid: 3, message_id: "<msg-3@test>", subject: "Email 3", from_address: "b@example.com", from_name: "B", date: "2026-01-01T00:00:00Z", snippet: "Snippet 3", is_read: true, is_starred: false, is_draft: false, has_attachments: false },
    ];

    // Second page: acc-1 returns 1 more email (offset 2), acc-2 returns 1 more email (offset 1)
    const secondPageAcc1 = [
      { id: "e4", account_id: "acc-1", folder_id: "inbox-1", uid: 4, message_id: "<msg-4@test>", subject: "Email 4", from_address: "a@example.com", from_name: "A", date: "2025-12-31T00:00:00Z", snippet: "Snippet 4", is_read: true, is_starred: false, is_draft: false, has_attachments: false },
    ];

    const secondPageAcc2 = [
      { id: "e5", account_id: "acc-2", folder_id: "inbox-2", uid: 5, message_id: "<msg-5@test>", subject: "Email 5", from_address: "b@example.com", from_name: "B", date: "2025-12-30T00:00:00Z", snippet: "Snippet 5", is_read: true, is_starred: false, is_draft: false, has_attachments: false },
    ];

    vi.mocked(api.listAccounts).mockResolvedValue(accounts);
    vi.mocked(api.listFolders)
      .mockResolvedValueOnce(foldersAcc1)
      .mockResolvedValueOnce(foldersAcc2);

    // First load: both accounts at offset 0
    vi.mocked(api.listEmails)
      .mockResolvedValueOnce(firstPageAcc1)
      .mockResolvedValueOnce(firstPageAcc2)
      // Second load: acc-1 at offset 2, acc-2 at offset 1
      .mockResolvedValueOnce(secondPageAcc1)
      .mockResolvedValueOnce(secondPageAcc2);

    const { result } = renderHook(() => useMailClient());

    await waitFor(() => {
      expect(result.current.accounts).toHaveLength(2);
    });

    await act(async () => {
      await result.current.loadAllFolders();
    });

    // Wait for initial load
    await waitFor(() => {
      expect(result.current.emails).toHaveLength(3);
    });

    // Verify first page loaded with correct offsets
    expect(api.listEmails).toHaveBeenCalledWith("acc-1", "inbox-1", 50, 0);
    expect(api.listEmails).toHaveBeenCalledWith("acc-2", "inbox-2", 50, 0);

    // Load more
    await act(async () => {
      await result.current.loadMoreEmails();
    });

    // Verify second page loaded with correct per-account offsets
    // acc-1 should be at offset 2 (returned 2 emails), acc-2 at offset 1 (returned 1 email)
    expect(api.listEmails).toHaveBeenCalledWith("acc-1", "inbox-1", 50, 2);
    expect(api.listEmails).toHaveBeenCalledWith("acc-2", "inbox-2", 50, 1);

    // Should have all 5 emails now
    expect(result.current.emails).toHaveLength(5);
  });

  it("handles different accounts returning different page sizes correctly", async () => {
    const accounts = [
      { id: "acc-1", email: "alice@example.com", display_name: "Alice", auth_type: "password" as const },
      { id: "acc-2", email: "bob@example.com", display_name: "Bob", auth_type: "password" as const },
      { id: "acc-3", email: "carol@example.com", display_name: "Carol", auth_type: "password" as const },
    ];

    const foldersAcc1 = [
      { id: "inbox-1", account_id: "acc-1", name: "Inbox", path: "INBOX", folder_type: "Inbox" as const, unread_count: 5, total_count: 100 },
    ];
    const foldersAcc2 = [
      { id: "inbox-2", account_id: "acc-2", name: "Inbox", path: "INBOX", folder_type: "Inbox" as const, unread_count: 3, total_count: 80 },
    ];
    const foldersAcc3 = [
      { id: "inbox-3", account_id: "acc-3", name: "Inbox", path: "INBOX", folder_type: "Inbox" as const, unread_count: 1, total_count: 50 },
    ];

    // Different page sizes returned by each account
    const page1Acc1 = [
      { id: "a1", account_id: "acc-1", folder_id: "inbox-1", uid: 1, message_id: "<msg-1@test>", subject: "A1", from_address: "a@example.com", from_name: "A", date: "2026-01-05T00:00:00Z", snippet: "", is_read: false, is_starred: false, is_draft: false, has_attachments: false },
      { id: "a2", account_id: "acc-1", folder_id: "inbox-1", uid: 2, message_id: "<msg-2@test>", subject: "A2", from_address: "a@example.com", from_name: "A", date: "2026-01-04T00:00:00Z", snippet: "", is_read: true, is_starred: false, is_draft: false, has_attachments: false },
      { id: "a3", account_id: "acc-1", folder_id: "inbox-1", uid: 3, message_id: "<msg-3@test>", subject: "A3", from_address: "a@example.com", from_name: "A", date: "2026-01-03T00:00:00Z", snippet: "", is_read: true, is_starred: false, is_draft: false, has_attachments: false },
    ];

    const page1Acc2 = [
      { id: "b1", account_id: "acc-2", folder_id: "inbox-2", uid: 4, message_id: "<msg-4@test>", subject: "B1", from_address: "b@example.com", from_name: "B", date: "2026-01-02T00:00:00Z", snippet: "", is_read: true, is_starred: false, is_draft: false, has_attachments: false },
    ];

    const page1Acc3 = [
      { id: "c1", account_id: "acc-3", folder_id: "inbox-3", uid: 5, message_id: "<msg-5@test>", subject: "C1", from_address: "c@example.com", from_name: "C", date: "2026-01-01T00:00:00Z", snippet: "", is_read: true, is_starred: false, is_draft: false, has_attachments: false },
      { id: "c2", account_id: "acc-3", folder_id: "inbox-3", uid: 6, message_id: "<msg-6@test>", subject: "C2", from_address: "c@example.com", from_name: "C", date: "2025-12-31T00:00:00Z", snippet: "", is_read: true, is_starred: false, is_draft: false, has_attachments: false },
    ];

    // Second page results
    const page2Acc1 = [
      { id: "a4", account_id: "acc-1", folder_id: "inbox-1", uid: 7, message_id: "<msg-7@test>", subject: "A4", from_address: "a@example.com", from_name: "A", date: "2025-12-30T00:00:00Z", snippet: "", is_read: true, is_starred: false, is_draft: false, has_attachments: false },
    ];
    const page2Acc2 = [
      { id: "b2", account_id: "acc-2", folder_id: "inbox-2", uid: 8, message_id: "<msg-8@test>", subject: "B2", from_address: "b@example.com", from_name: "B", date: "2025-12-29T00:00:00Z", snippet: "", is_read: true, is_starred: false, is_draft: false, has_attachments: false },
      { id: "b3", account_id: "acc-2", folder_id: "inbox-2", uid: 9, message_id: "<msg-9@test>", subject: "B3", from_address: "b@example.com", from_name: "B", date: "2025-12-28T00:00:00Z", snippet: "", is_read: true, is_starred: false, is_draft: false, has_attachments: false },
    ];
    const page2Acc3: typeof page1Acc3 = []; // Empty - no more emails

    vi.mocked(api.listAccounts).mockResolvedValue(accounts);
    vi.mocked(api.listFolders)
      .mockResolvedValueOnce(foldersAcc1)
      .mockResolvedValueOnce(foldersAcc2)
      .mockResolvedValueOnce(foldersAcc3);

    vi.mocked(api.listEmails)
      // First load
      .mockResolvedValueOnce(page1Acc1)
      .mockResolvedValueOnce(page1Acc2)
      .mockResolvedValueOnce(page1Acc3)
      // Second load - should use correct offsets
      .mockResolvedValueOnce(page2Acc1)
      .mockResolvedValueOnce(page2Acc2)
      .mockResolvedValueOnce(page2Acc3);

    const { result } = renderHook(() => useMailClient());

    await waitFor(() => {
      expect(result.current.accounts).toHaveLength(3);
    });

    await act(async () => {
      await result.current.loadAllFolders();
    });

    await waitFor(() => {
      expect(result.current.emails).toHaveLength(6);
    });

    // Verify initial offsets (all 0)
    expect(api.listEmails).toHaveBeenCalledWith("acc-1", "inbox-1", 50, 0);
    expect(api.listEmails).toHaveBeenCalledWith("acc-2", "inbox-2", 50, 0);
    expect(api.listEmails).toHaveBeenCalledWith("acc-3", "inbox-3", 50, 0);

    // Load more
    await act(async () => {
      await result.current.loadMoreEmails();
    });

    // Verify offsets advanced independently based on actual returned counts
    // acc-1: returned 3 emails -> offset should be 3
    // acc-2: returned 1 email -> offset should be 1
    // acc-3: returned 2 emails -> offset should be 2
    expect(api.listEmails).toHaveBeenCalledWith("acc-1", "inbox-1", 50, 3);
    expect(api.listEmails).toHaveBeenCalledWith("acc-2", "inbox-2", 50, 1);
    expect(api.listEmails).toHaveBeenCalledWith("acc-3", "inbox-3", 50, 2);

    // Should have 6 + 3 = 9 emails total
    expect(result.current.emails).toHaveLength(9);
  });

  it("deduplicates emails by id when merging results", async () => {
    const accounts = [
      { id: "acc-1", email: "alice@example.com", display_name: "Alice", auth_type: "password" as const },
      { id: "acc-2", email: "bob@example.com", display_name: "Bob", auth_type: "password" as const },
    ];

    const foldersAcc1 = [
      { id: "inbox-1", account_id: "acc-1", name: "Inbox", path: "INBOX", folder_type: "Inbox" as const, unread_count: 5, total_count: 10 },
    ];

    const foldersAcc2 = [
      { id: "inbox-2", account_id: "acc-2", name: "Inbox", path: "INBOX", folder_type: "Inbox" as const, unread_count: 3, total_count: 8 },
    ];

    // First page
    const firstPageAcc1 = [
      { id: "e1", account_id: "acc-1", folder_id: "inbox-1", uid: 1, message_id: "<msg-1@test>", subject: "Email 1", from_address: "a@example.com", from_name: "A", date: "2026-01-03T00:00:00Z", snippet: "Snippet 1", is_read: false, is_starred: false, is_draft: false, has_attachments: false },
    ];

    const firstPageAcc2 = [
      { id: "e2", account_id: "acc-2", folder_id: "inbox-2", uid: 2, message_id: "<msg-2@test>", subject: "Email 2", from_address: "b@example.com", from_name: "B", date: "2026-01-02T00:00:00Z", snippet: "Snippet 2", is_read: true, is_starred: false, is_draft: false, has_attachments: false },
    ];

    // Second page - includes duplicate of e1 (should be deduplicated)
    const secondPageAcc1 = [
      { id: "e1", account_id: "acc-1", folder_id: "inbox-1", uid: 1, message_id: "<msg-1@test>", subject: "Email 1", from_address: "a@example.com", from_name: "A", date: "2026-01-03T00:00:00Z", snippet: "Snippet 1", is_read: false, is_starred: false, is_draft: false, has_attachments: false },
      { id: "e3", account_id: "acc-1", folder_id: "inbox-1", uid: 3, message_id: "<msg-3@test>", subject: "Email 3", from_address: "a@example.com", from_name: "A", date: "2026-01-01T00:00:00Z", snippet: "Snippet 3", is_read: true, is_starred: false, is_draft: false, has_attachments: false },
    ];

    const secondPageAcc2 = [
      { id: "e4", account_id: "acc-2", folder_id: "inbox-2", uid: 4, message_id: "<msg-4@test>", subject: "Email 4", from_address: "b@example.com", from_name: "B", date: "2025-12-31T00:00:00Z", snippet: "Snippet 4", is_read: true, is_starred: false, is_draft: false, has_attachments: false },
    ];

    vi.mocked(api.listAccounts).mockResolvedValue(accounts);
    vi.mocked(api.listFolders)
      .mockResolvedValueOnce(foldersAcc1)
      .mockResolvedValueOnce(foldersAcc2);

    vi.mocked(api.listEmails)
      .mockResolvedValueOnce(firstPageAcc1)
      .mockResolvedValueOnce(firstPageAcc2)
      .mockResolvedValueOnce(secondPageAcc1)
      .mockResolvedValueOnce(secondPageAcc2);

    const { result } = renderHook(() => useMailClient());

    await waitFor(() => {
      expect(result.current.accounts).toHaveLength(2);
    });

    await act(async () => {
      await result.current.loadAllFolders();
    });

    await waitFor(() => {
      expect(result.current.emails).toHaveLength(2);
    });

    // Load more - should deduplicate e1
    await act(async () => {
      await result.current.loadMoreEmails();
    });

    // Should have 4 emails (e1, e2, e3, e4) not 5 (e1 duplicated)
    expect(result.current.emails).toHaveLength(4);
    const emailIds = result.current.emails.map((e) => e.id);
    expect(emailIds).toContain("e1");
    expect(emailIds).toContain("e2");
    expect(emailIds).toContain("e3");
    expect(emailIds).toContain("e4");
  });

  it("resets offsets when aggregate selection changes folder type", async () => {
    const accounts = [
      { id: "acc-1", email: "alice@example.com", display_name: "Alice", auth_type: "password" as const },
    ];

    const folders = [
      { id: "inbox-1", account_id: "acc-1", name: "Inbox", path: "INBOX", folder_type: "Inbox" as const, unread_count: 5, total_count: 100 },
      { id: "sent-1", account_id: "acc-1", name: "Sent", path: "Sent", folder_type: "Sent" as const, unread_count: 0, total_count: 50 },
    ];

    const inboxEmails = [
      { id: "i1", account_id: "acc-1", folder_id: "inbox-1", uid: 1, message_id: "<msg-1@test>", subject: "Inbox 1", from_address: "a@example.com", from_name: "A", date: "2026-01-02T00:00:00Z", snippet: "", is_read: false, is_starred: false, is_draft: false, has_attachments: false },
    ];

    const sentEmails = [
      { id: "s1", account_id: "acc-1", folder_id: "sent-1", uid: 2, message_id: "<msg-2@test>", subject: "Sent 1", from_address: "a@example.com", from_name: "A", date: "2026-01-01T00:00:00Z", snippet: "", is_read: true, is_starred: false, is_draft: false, has_attachments: false },
    ];

    vi.mocked(api.listAccounts).mockResolvedValue(accounts);
    vi.mocked(api.listFolders).mockResolvedValue(folders);

    // First call for Inbox, second for Sent
    vi.mocked(api.listEmails)
      .mockResolvedValueOnce(inboxEmails)
      .mockResolvedValueOnce(sentEmails);

    const { result } = renderHook(() => useMailClient());

    await waitFor(() => {
      expect(result.current.accounts).toHaveLength(1);
    });

    await act(async () => {
      await result.current.loadAllFolders();
    });

    await waitFor(() => {
      expect(result.current.emails).toHaveLength(1);
    });

    // First load should be at offset 0
    expect(api.listEmails).toHaveBeenLastCalledWith("acc-1", "inbox-1", 50, 0);

    // Switch to Sent aggregate
    act(() => {
      result.current.selectAggregate("Sent");
    });

    await waitFor(() => {
      expect(result.current.selection).toEqual({ type: "aggregate", folderType: "Sent" });
    });

    await waitFor(() => {
      expect(result.current.emails).toHaveLength(1);
    });

    // Should reset and load from offset 0 for the new folder type
    expect(api.listEmails).toHaveBeenLastCalledWith("acc-1", "sent-1", 50, 0);
  });

  it("resets offsets when leaving aggregate mode", async () => {
    const accounts = [
      { id: "acc-1", email: "alice@example.com", display_name: "Alice", auth_type: "password" as const },
    ];

    const folders = [
      { id: "inbox-1", account_id: "acc-1", name: "Inbox", path: "INBOX", folder_type: "Inbox" as const, unread_count: 5, total_count: 100 },
    ];

    const emails = [
      { id: "e1", account_id: "acc-1", folder_id: "inbox-1", uid: 1, message_id: "<msg-1@test>", subject: "Email 1", from_address: "a@example.com", from_name: "A", date: "2026-01-01T00:00:00Z", snippet: "", is_read: false, is_starred: false, is_draft: false, has_attachments: false },
    ];

    vi.mocked(api.listAccounts).mockResolvedValue(accounts);
    vi.mocked(api.listFolders).mockResolvedValue(folders);
    vi.mocked(api.listEmails).mockResolvedValue(emails);

    const { result } = renderHook(() => useMailClient());

    await waitFor(() => {
      expect(result.current.accounts).toHaveLength(1);
    });

    await act(async () => {
      await result.current.loadAllFolders();
    });

    // Currently in aggregate mode
    expect(result.current.selection?.type).toBe("aggregate");

    // Switch to child mode
    act(() => {
      result.current.selectChild("acc-1", "inbox-1");
    });

    await waitFor(() => {
      expect(result.current.selection?.type).toBe("child");
    });

    // When returning to aggregate mode, offsets should be reset
    act(() => {
      result.current.selectAggregate("Inbox");
    });

    await waitFor(() => {
      expect(result.current.selection).toEqual({ type: "aggregate", folderType: "Inbox" });
    });

    // Should load from offset 0 again
    const aggregateCalls = vi.mocked(api.listEmails).mock.calls.filter(
      (call) => call[0] === "acc-1" && call[1] === "inbox-1" && call[3] === 0
    );
    expect(aggregateCalls.length).toBeGreaterThanOrEqual(2);
  });

  it("does not skip emails when accounts return different counts", async () => {
    const accounts = [
      { id: "acc-1", email: "alice@example.com", display_name: "Alice", auth_type: "password" as const },
      { id: "acc-2", email: "bob@example.com", display_name: "Bob", auth_type: "password" as const },
    ];

    const foldersAcc1 = [
      { id: "inbox-1", account_id: "acc-1", name: "Inbox", path: "INBOX", folder_type: "Inbox" as const, unread_count: 5, total_count: 10 },
    ];

    const foldersAcc2 = [
      { id: "inbox-2", account_id: "acc-2", name: "Inbox", path: "INBOX", folder_type: "Inbox" as const, unread_count: 3, total_count: 8 },
    ];

    // Create 10 emails for each account with sequential dates
    const createEmail = (id: string, accountId: string, folderId: string, date: string) => ({
      id,
      account_id: accountId,
      folder_id: folderId,
      uid: parseInt(id.replace(/\D/g, ""), 10),
      message_id: `<${id}@test>`,
      subject: `Email ${id}`,
      from_address: "test@example.com",
      from_name: "Test",
      date,
      snippet: "",
      is_read: true,
      is_starred: false,
      is_draft: false,
      has_attachments: false,
    });

    const acc1Emails = Array.from({ length: 10 }, (_, i) =>
      createEmail(`a${i + 1}`, "acc-1", "inbox-1", `2026-01-${String(20 - i).padStart(2, "0")}T00:00:00Z`)
    );

    const acc2Emails = Array.from({ length: 10 }, (_, i) =>
      createEmail(`b${i + 1}`, "acc-2", "inbox-2", `2026-01-${String(10 - i).padStart(2, "0")}T00:00:00Z`)
    );

    vi.mocked(api.listAccounts).mockResolvedValue(accounts);
    vi.mocked(api.listFolders)
      .mockResolvedValueOnce(foldersAcc1)
      .mockResolvedValueOnce(foldersAcc2);

    // Mock to return 3 emails per page for acc-1 and 2 for acc-2
    let acc1CallCount = 0;
    let acc2CallCount = 0;

    vi.mocked(api.listEmails).mockImplementation((accountId: string, _folderId: string, _limit?: number, _offset?: number) => {
      if (accountId === "acc-1") {
        const pageSize = 3;
        const start = acc1CallCount * pageSize;
        acc1CallCount++;
        return Promise.resolve(acc1Emails.slice(start, start + pageSize));
      } else {
        const pageSize = 2;
        const start = acc2CallCount * pageSize;
        acc2CallCount++;
        return Promise.resolve(acc2Emails.slice(start, start + pageSize));
      }
    });

    const { result } = renderHook(() => useMailClient());

    await waitFor(() => {
      expect(result.current.accounts).toHaveLength(2);
    });

    await act(async () => {
      await result.current.loadAllFolders();
    });

    // Initial load: 3 from acc-1 + 2 from acc-2 = 5 emails
    await waitFor(() => {
      expect(result.current.emails).toHaveLength(5);
    });

    // Load more multiple times (need 4 acc-1 calls and 5 acc-2 calls to get all 10 each)
    // Initial load: acc-1 offset 0, acc-2 offset 0
    // LoadMore 1: acc-1 offset 3, acc-2 offset 2
    // LoadMore 2: acc-1 offset 6, acc-2 offset 4
    // LoadMore 3: acc-1 offset 9, acc-2 offset 6
    // LoadMore 4: acc-1 done (returns 1), acc-2 offset 8
    // LoadMore 5: acc-1 done, acc-2 done (returns 2)
    await act(async () => {
      await result.current.loadMoreEmails();
    });

    await act(async () => {
      await result.current.loadMoreEmails();
    });

    await act(async () => {
      await result.current.loadMoreEmails();
    });

    await act(async () => {
      await result.current.loadMoreEmails();
    });

    await act(async () => {
      await result.current.loadMoreEmails();
    });

    // Should have loaded all 20 emails without skipping any
    const loadedIds = result.current.emails.map((e) => e.id);
    
    // Verify all acc-1 emails are present
    for (let i = 1; i <= 10; i++) {
      expect(loadedIds).toContain(`a${i}`);
    }
    
    // Verify all acc-2 emails are present
    for (let i = 1; i <= 10; i++) {
      expect(loadedIds).toContain(`b${i}`);
    }

    expect(result.current.emails).toHaveLength(20);
  });

  it("resets per-account offsets after an aggregate syncCurrentFolder so loadMoreEmails does not use stale offsets", async () => {
    const accounts = [
      { id: "acc-1", email: "alice@example.com", display_name: "Alice", auth_type: "password" as const },
    ];

    const folders = [
      { id: "inbox-1", account_id: "acc-1", name: "Inbox", path: "INBOX", folder_type: "Inbox" as const, unread_count: 5, total_count: 100 },
    ];

    const createEmail = (id: string, date: string) => ({
      id,
      account_id: "acc-1",
      folder_id: "inbox-1",
      uid: parseInt(id.replace(/\D/g, ""), 10),
      message_id: `<${id}@test>`,
      subject: `Email ${id}`,
      from_address: "a@example.com",
      from_name: "A",
      date,
      snippet: "",
      is_read: true,
      is_starred: false,
      is_draft: false,
      has_attachments: false,
    });

    // Initial aggregate load returns 2 emails for acc-1 (offset 0 -> advances offset to 2).
    const initialLoad = [createEmail("e1", "2026-01-05T00:00:00Z"), createEmail("e2", "2026-01-04T00:00:00Z")];

    // syncCurrentFolder replaces the folder's full email set with a smaller, freshly-synced set
    // (e.g. server-side pruning / a resync that legitimately returns fewer emails than before).
    const syncedSet = [createEmail("e1", "2026-01-05T00:00:00Z")];

    // After sync, the *correct* next page (starting from offset 0, since the resync reset state)
    // is e3. The bug: stale aggregateOffsets (still 2 from before the sync) causes loadMoreEmails
    // to request offset 2 instead of offset 1 (or 0), skipping/misaligning pagination relative to
    // the freshly synced list.
    const correctNextPage = [createEmail("e3", "2026-01-03T00:00:00Z")];

    vi.mocked(api.listAccounts).mockResolvedValue(accounts);
    vi.mocked(api.listFolders).mockResolvedValue(folders);
    vi.mocked(api.listEmails)
      .mockResolvedValueOnce(initialLoad) // initial aggregate load
      .mockResolvedValueOnce(correctNextPage); // loadMoreEmails after sync
    vi.mocked(api.syncFolder).mockResolvedValue(syncedSet);

    const { result } = renderHook(() => useMailClient());

    await waitFor(() => {
      expect(result.current.accounts).toHaveLength(1);
    });

    await act(async () => {
      await result.current.loadAllFolders();
    });

    await waitFor(() => {
      expect(result.current.emails).toHaveLength(2);
    });

    // Sanity: initial load happened at offset 0, advancing the per-account offset to 2.
    expect(api.listEmails).toHaveBeenNthCalledWith(1, "acc-1", "inbox-1", 50, 0);

    // Sync the current (aggregate) folder — this replaces `emails` with `syncedSet` (length 1),
    // which no longer matches the per-account offset (2) left over from the pre-sync load.
    await act(async () => {
      await result.current.syncCurrentFolder();
    });

    await waitFor(() => {
      expect(result.current.emails).toHaveLength(1);
    });

    // Load the next page. Pagination must be based on the post-sync state, so the request
    // must not reuse the stale pre-sync offset of 2.
    await act(async () => {
      await result.current.loadMoreEmails();
    });

    const listEmailsCalls = vi.mocked(api.listEmails).mock.calls;
    const loadMoreCall = listEmailsCalls[listEmailsCalls.length - 1];

    // RED (pre-fix): this asserts offset 2 was NOT used for the post-sync loadMoreEmails call,
    // proving aggregateOffsets was reset to reflect the synced data rather than left stale.
    expect(loadMoreCall).not.toEqual(["acc-1", "inbox-1", 50, 2]);
  });
});
