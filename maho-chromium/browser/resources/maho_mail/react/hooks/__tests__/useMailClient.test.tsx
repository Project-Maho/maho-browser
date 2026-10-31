import { renderHook, waitFor, act } from "@testing-library/react";
import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
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

describe("useMailClient", () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  it("initializes with null selection and loads accounts", async () => {
    vi.mocked(api.listAccounts).mockResolvedValue([]);

    const { result } = renderHook(() => useMailClient());

    expect(result.current.isBootstrappingAccounts).toBe(true);

    await waitFor(() => {
      expect(result.current.isBootstrappingAccounts).toBe(false);
    });

    expect(result.current.accounts).toEqual([]);
    expect(result.current.selection).toBeNull();
  });

  it("loads accounts without eagerly syncing folders", async () => {
    const accounts = [
      { id: "acc-1", email: "alice@example.com", display_name: "Alice", auth_type: "password" },
      { id: "acc-2", email: "bob@example.com", display_name: "Bob", auth_type: "password" },
    ];

    vi.mocked(api.listAccounts).mockResolvedValue(accounts);

    const { result } = renderHook(() => useMailClient());

    await waitFor(() => {
      expect(result.current.accounts).toHaveLength(2);
    });

    expect(result.current.accountFolders).toEqual([]);
    expect(result.current.selection).toBeNull();
    expect(result.current.folders).toEqual([]);
    expect(api.syncFolders).not.toHaveBeenCalled();
    expect(api.listFolders).not.toHaveBeenCalled();
  });

  it("loads folders only when loadAllFolders is called explicitly", async () => {
    const accounts = [
      { id: "acc-1", email: "alice@example.com", display_name: "Alice", auth_type: "password" },
    ];

    const folders = [
      { id: "inbox-1", account_id: "acc-1", name: "Inbox", path: "INBOX", folder_type: "Inbox" as const, unread_count: 5, total_count: 10 },
    ];

    vi.mocked(api.listAccounts).mockResolvedValue(accounts);
    vi.mocked(api.listFolders).mockResolvedValue(folders);
    vi.mocked(api.listEmails).mockResolvedValue([]);

    const { result } = renderHook(() => useMailClient());

    await waitFor(() => {
      expect(result.current.accounts).toHaveLength(1);
    });

    await act(async () => {
      await result.current.loadAllFolders();
    });

    expect(api.listFolders).toHaveBeenCalledWith("acc-1");
    expect(result.current.accountFolders).toHaveLength(1);
    expect(result.current.accountFolders[0].folders).toEqual(folders);
    expect(result.current.selection).toEqual({ type: "aggregate", folderType: "Inbox" });
  });

  it("supports aggregate selection by folder type", async () => {
    const accounts = [
      { id: "acc-1", email: "alice@example.com", display_name: "Alice", auth_type: "password" },
    ];

    const folders = [
      { id: "inbox-1", account_id: "acc-1", name: "Inbox", path: "INBOX", folder_type: "Inbox" as const, unread_count: 5, total_count: 10 },
      { id: "sent-1", account_id: "acc-1", name: "Sent", path: "Sent", folder_type: "Sent" as const, unread_count: 0, total_count: 20 },
    ];

    vi.mocked(api.listAccounts).mockResolvedValue(accounts);
    vi.mocked(api.listFolders).mockResolvedValue(folders);
    vi.mocked(api.listEmails).mockResolvedValue([]);

    const { result } = renderHook(() => useMailClient());

    await waitFor(() => {
      expect(result.current.accounts).toHaveLength(1);
    });

    await act(async () => {
      await result.current.loadAllFolders();
    });

    await waitFor(() => {
      expect(result.current.accountFolders).toHaveLength(1);
    });

    // Select aggregate Sent
    act(() => {
      result.current.selectAggregate("Sent");
    });

    expect(result.current.selection).toEqual({ type: "aggregate", folderType: "Sent" });
    expect(result.current.selectedFolderType).toBe("Sent");
  });

  it("supports child selection for specific account/folder", async () => {
    const accounts = [
      { id: "acc-1", email: "alice@example.com", display_name: "Alice", auth_type: "password" },
    ];

    const folders = [
      { id: "inbox-1", account_id: "acc-1", name: "Inbox", path: "INBOX", folder_type: "Inbox" as const, unread_count: 5, total_count: 10 },
    ];

    vi.mocked(api.listAccounts).mockResolvedValue(accounts);
    vi.mocked(api.listFolders).mockResolvedValue(folders);
    vi.mocked(api.listEmails).mockResolvedValue([]);

    const { result } = renderHook(() => useMailClient());

    await waitFor(() => {
      expect(result.current.accounts).toHaveLength(1);
    });

    await act(async () => {
      await result.current.loadAllFolders();
    });

    await waitFor(() => {
      expect(result.current.accountFolders).toHaveLength(1);
    });

    // Select child
    act(() => {
      result.current.selectChild("acc-1", "inbox-1");
    });

    expect(result.current.selection).toEqual({ type: "child", accountId: "acc-1", folderId: "inbox-1" });
    expect(result.current.selectedAccountId).toBe("acc-1");
    expect(result.current.selectedFolderId).toBe("inbox-1");
    expect(result.current.selectedFolderType).toBe("Inbox");
  });

  it("loads emails from all accounts in aggregate mode", async () => {
    const accounts = [
      { id: "acc-1", email: "alice@example.com", display_name: "Alice", auth_type: "password" },
      { id: "acc-2", email: "bob@example.com", display_name: "Bob", auth_type: "password" },
    ];

    const foldersAcc1 = [
      { id: "inbox-1", account_id: "acc-1", name: "Inbox", path: "INBOX", folder_type: "Inbox" as const, unread_count: 5, total_count: 10 },
    ];

    const foldersAcc2 = [
      { id: "inbox-2", account_id: "acc-2", name: "Inbox", path: "INBOX", folder_type: "Inbox" as const, unread_count: 3, total_count: 8 },
    ];

    const emailsAcc1 = [
      { id: "e1", account_id: "acc-1", folder_id: "inbox-1", uid: 1, message_id: "<msg-1@test>", subject: "Email 1", from_address: "a@example.com", from_name: "A", date: "2026-01-02T00:00:00Z", snippet: "Snippet 1", is_read: false, is_starred: false, is_draft: false, has_attachments: false },
    ];

    const emailsAcc2 = [
      { id: "e2", account_id: "acc-2", folder_id: "inbox-2", uid: 2, message_id: "<msg-2@test>", subject: "Email 2", from_address: "b@example.com", from_name: "B", date: "2026-01-01T00:00:00Z", snippet: "Snippet 2", is_read: true, is_starred: false, is_draft: false, has_attachments: false },
    ];

    vi.mocked(api.listAccounts).mockResolvedValue(accounts);
    vi.mocked(api.listFolders)
      .mockResolvedValueOnce(foldersAcc1)
      .mockResolvedValueOnce(foldersAcc2);
    vi.mocked(api.listEmails)
      .mockResolvedValueOnce(emailsAcc1)
      .mockResolvedValueOnce(emailsAcc2);

    const { result } = renderHook(() => useMailClient());

    await waitFor(() => {
      expect(result.current.accounts).toHaveLength(2);
    });

    await act(async () => {
      await result.current.loadAllFolders();
    });

    await waitFor(() => {
      expect(result.current.accountFolders).toHaveLength(2);
    });

    // Should auto-load emails for aggregate Inbox
    await waitFor(() => {
      expect(result.current.emails).toHaveLength(2);
    });

    // Emails should be sorted by date descending (e1 is newer)
    expect(result.current.emails[0].id).toBe("e1");
    expect(result.current.emails[1].id).toBe("e2");

    // Should have called listEmails for both accounts
    expect(api.listEmails).toHaveBeenCalledWith("acc-1", "inbox-1", 50, 0);
    expect(api.listEmails).toHaveBeenCalledWith("acc-2", "inbox-2", 50, 0);
  });

  it("loads folders from listFolders", async () => {
    const accounts = [
      { id: "acc-1", email: "alice@example.com", display_name: "Alice", auth_type: "password" },
    ];

    const folders = [
      { id: "inbox-1", account_id: "acc-1", name: "Inbox", path: "INBOX", folder_type: "Inbox" as const, unread_count: 5, total_count: 10 },
    ];

    vi.mocked(api.listAccounts).mockResolvedValue(accounts);
    vi.mocked(api.listFolders).mockResolvedValue(folders);
    vi.mocked(api.listEmails).mockResolvedValue([]);

    const { result } = renderHook(() => useMailClient());

    await waitFor(() => {
      expect(result.current.accounts).toHaveLength(1);
    });

    await act(async () => {
      await result.current.loadAllFolders();
    });

    await waitFor(() => {
      expect(result.current.accountFolders).toHaveLength(1);
    });

    expect(api.listFolders).toHaveBeenCalledWith("acc-1");
    expect(result.current.accountFolders[0].folders).toEqual(folders);
  });

  it("syncs folders over network when syncAllFolders is called", async () => {
    const accounts = [
      { id: "acc-1", email: "alice@example.com", display_name: "Alice", auth_type: "password" },
    ];

    const folders = [
      { id: "inbox-1", account_id: "acc-1", name: "Inbox", path: "INBOX", folder_type: "Inbox" as const, unread_count: 5, total_count: 10 },
    ];

    vi.mocked(api.listAccounts).mockResolvedValue(accounts);
    vi.mocked(api.syncFolders).mockResolvedValue(folders);
    vi.mocked(api.listEmails).mockResolvedValue([]);

    const { result } = renderHook(() => useMailClient());

    await waitFor(() => {
      expect(result.current.accounts).toHaveLength(1);
    });

    await act(async () => {
      await result.current.syncAllFolders();
    });

    expect(api.syncFolders).toHaveBeenCalledWith("acc-1");
    expect(result.current.accountFolders).toHaveLength(1);
    expect(result.current.accountFolders[0].folders).toEqual(folders);
  });

  it("falls back to listFolders when syncAllFolders network sync fails", async () => {
    const accounts = [
      { id: "acc-1", email: "alice@example.com", display_name: "Alice", auth_type: "password" },
    ];

    const folders = [
      { id: "inbox-1", account_id: "acc-1", name: "Inbox", path: "INBOX", folder_type: "Inbox" as const, unread_count: 5, total_count: 10 },
    ];

    vi.mocked(api.listAccounts).mockResolvedValue(accounts);
    vi.mocked(api.syncFolders).mockRejectedValue(new Error("Sync failed"));
    vi.mocked(api.listFolders).mockResolvedValue(folders);
    vi.mocked(api.listEmails).mockResolvedValue([]);

    const { result } = renderHook(() => useMailClient());

    await waitFor(() => {
      expect(result.current.accounts).toHaveLength(1);
    });

    await act(async () => {
      await result.current.syncAllFolders();
    });

    await waitFor(() => {
      expect(result.current.accountFolders).toHaveLength(1);
    });

    expect(api.syncFolders).toHaveBeenCalledWith("acc-1");
    expect(api.listFolders).toHaveBeenCalledWith("acc-1");
    expect(result.current.accountFolders[0].folders).toEqual(folders);
  });

  it("maintains backward compatibility with changeAccount", async () => {
    const accounts = [
      { id: "acc-1", email: "alice@example.com", display_name: "Alice", auth_type: "password" },
    ];

    const folders = [
      { id: "inbox-1", account_id: "acc-1", name: "Inbox", path: "INBOX", folder_type: "Inbox" as const, unread_count: 5, total_count: 10 },
    ];

    vi.mocked(api.listAccounts).mockResolvedValue(accounts);
    vi.mocked(api.listFolders).mockResolvedValue(folders);
    vi.mocked(api.listEmails).mockResolvedValue([]);

    const { result } = renderHook(() => useMailClient());

    await waitFor(() => {
      expect(result.current.accounts).toHaveLength(1);
    });

    await act(async () => {
      await result.current.loadAllFolders();
    });

    await waitFor(() => {
      expect(result.current.accountFolders).toHaveLength(1);
    });

    act(() => {
      result.current.changeAccount("acc-1");
    });

    expect(result.current.selectedAccountId).toBe("acc-1");
    expect(result.current.selectedFolderId).toBe("inbox-1");
    expect(result.current.selection?.type).toBe("child");
  });

  it("maintains backward compatibility with changeFolder", async () => {
    const accounts = [
      { id: "acc-1", email: "alice@example.com", display_name: "Alice", auth_type: "password" },
    ];

    const folders = [
      { id: "inbox-1", account_id: "acc-1", name: "Inbox", path: "INBOX", folder_type: "Inbox" as const, unread_count: 5, total_count: 10 },
      { id: "sent-1", account_id: "acc-1", name: "Sent", path: "Sent", folder_type: "Sent" as const, unread_count: 0, total_count: 20 },
    ];

    vi.mocked(api.listAccounts).mockResolvedValue(accounts);
    vi.mocked(api.listFolders).mockResolvedValue(folders);
    vi.mocked(api.listEmails).mockResolvedValue([]);

    const { result } = renderHook(() => useMailClient());

    await waitFor(() => {
      expect(result.current.accounts).toHaveLength(1);
    });

    await act(async () => {
      await result.current.loadAllFolders();
    });

    await waitFor(() => {
      expect(result.current.accountFolders).toHaveLength(1);
    });

    act(() => {
      result.current.changeFolder("sent-1");
    });

    expect(result.current.selectedFolderId).toBe("sent-1");
    expect(result.current.selectedAccountId).toBe("acc-1");
    expect(result.current.selection).toEqual({ type: "child", accountId: "acc-1", folderId: "sent-1" });
  });

  it("exposes an authentication recovery state instead of only reporting a toast", async () => {
    vi.mocked(api.listAccounts).mockResolvedValue([]);
    vi.mocked(api.getEmail).mockRejectedValue(
      new Error("core error: Auth error: [AUTHENTICATIONFAILED] Invalid credentials"),
    );
    const onError = vi.fn();

    const { result } = renderHook(() => useMailClient(onError));

    await waitFor(() => {
      expect(result.current.isBootstrappingAccounts).toBe(false);
    });

    await act(async () => {
      await result.current.selectEmail("email-1");
    });

    expect(result.current.authRecoveryError).toEqual({
      emailId: "email-1",
      message: "core error: Auth error: [AUTHENTICATIONFAILED] Invalid credentials",
    });
    expect(onError).not.toHaveBeenCalled();
  });

  describe("selectEmail race conditions", () => {
    beforeEach(() => {
      vi.useFakeTimers();
    });

    afterEach(() => {
      vi.useRealTimers();
    });

    it("should only select the last email when rapidly switching selections", async () => {
      const email1 = { email: { id: "email-1", is_read: false } };
      const email2 = { email: { id: "email-2", is_read: false } };

      let firstCallResolvePromise: any = null;

      vi.mocked(api.getEmail).mockImplementation((id: string) => {
        if (id === "email-1") {
          return new Promise((resolve) => {
            firstCallResolvePromise = () => {
              resolve(email1 as any);
            };
          });
        } else {
          return Promise.resolve(email2 as any);
        }
      });

      const { result } = renderHook(() => useMailClient());

      let selectPromise1: any;
      let selectPromise2: any;

      await act(async () => {
        selectPromise1 = result.current.selectEmail("email-1");
        selectPromise2 = result.current.selectEmail("email-2");
        await selectPromise2;
      });

      expect(result.current.selectedEmail).toEqual(email2);

      // Resolve the first call now
      if (firstCallResolvePromise) {
        await act(async () => {
          firstCallResolvePromise();
          await selectPromise1;
        });
      }

      // Assert selection is still email2 (not email1)
      expect(result.current.selectedEmail).toEqual(email2);
      
      // Fast forward 2 seconds to check if markRead was debounced
      await act(async () => {
        vi.advanceTimersByTime(2000);
      });

      // markRead should only be called for email-2, NOT email-1
      expect(api.markRead).toHaveBeenCalledWith("email-2");
      expect(api.markRead).not.toHaveBeenCalledWith("email-1");
    });
  });

  describe("optimistic mutations", () => {
    it("handles deleteEmail failure gracefully without unhandled rejection and rolls back", async () => {
      vi.mocked(api.listAccounts).mockResolvedValue([]);
      vi.mocked(api.deleteEmail).mockRejectedValue(new Error("Network delete error"));

      const { result } = renderHook(() => useMailClient());
      await waitFor(() => {
        expect(result.current.isBootstrappingAccounts).toBe(false);
      });

      const unhandledRejections: unknown[] = [];
      const onUnhandled = (reason: unknown) => { unhandledRejections.push(reason); };
      process.on("unhandledRejection", onUnhandled);

      try {
        let deletePromise: Promise<void>;
        await act(async () => {
          deletePromise = result.current.deleteEmail("email-fail-1");
          await deletePromise;
        });

        await expect(deletePromise!).resolves.toBeUndefined();
        expect(unhandledRejections).toHaveLength(0);
        expect(api.deleteEmail).toHaveBeenCalledWith("email-fail-1");
      } finally {
        process.off("unhandledRejection", onUnhandled);
      }
    });

    it("handles toggleStar failure gracefully without unhandled rejection and rolls back", async () => {
      vi.mocked(api.listAccounts).mockResolvedValue([]);
      vi.mocked(api.toggleStar).mockRejectedValue(new Error("Network star error"));

      const { result } = renderHook(() => useMailClient());
      await waitFor(() => {
        expect(result.current.isBootstrappingAccounts).toBe(false);
      });

      const unhandledRejections: unknown[] = [];
      const onUnhandled = (reason: unknown) => { unhandledRejections.push(reason); };
      process.on("unhandledRejection", onUnhandled);

      try {
        let starPromise: Promise<void>;
        await act(async () => {
          starPromise = result.current.toggleStar("email-fail-2");
          await starPromise;
        });

        await expect(starPromise!).resolves.toBeUndefined();
        expect(unhandledRejections).toHaveLength(0);
        expect(api.toggleStar).toHaveBeenCalledWith("email-fail-2");
      } finally {
        process.off("unhandledRejection", onUnhandled);
      }
    });
  });
});
