import { renderHook, waitFor, act } from "@testing-library/react";
import { describe, it, expect, vi, beforeEach } from "vitest";
import { useMailClient, MAIL_LIST_SNAPSHOT_KEY } from "../useMailClient";

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

const account = { id: "acc-1", email: "alice@example.com", display_name: "Alice", auth_type: "oauth2" };
const inbox = {
  id: "inbox-1", account_id: "acc-1", name: "Inbox", path: "INBOX",
  folder_type: "Inbox" as const, unread_count: 1, total_count: 2,
};
function email(id: string, subject: string) {
  return {
    id, account_id: "acc-1", folder_id: "inbox-1", uid: 1, message_id: `<${id}>`,
    subject, from_address: "bob@example.com", from_name: "Bob",
    date: "2026-09-29T10:00:00Z", snippet: "", is_read: false,
    is_starred: false, is_draft: false, has_attachments: false,
  };
}

function never<T>(): Promise<T> {
  return new Promise<T>(() => {});
}

describe("useMailClient list snapshot", () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  it("persists the landing Inbox once it has loaded", async () => {
    vi.mocked(api.listAccounts).mockResolvedValue([account]);
    vi.mocked(api.listFolders).mockResolvedValue([inbox]);
    vi.mocked(api.listEmails).mockResolvedValue([email("e1", "Hello")]);

    const { result } = renderHook(() => useMailClient());
    await waitFor(() => expect(result.current.accounts).toHaveLength(1));
    await act(async () => {
      await result.current.loadAllFolders();
    });
    await waitFor(() => expect(result.current.emails).toHaveLength(1));

    await waitFor(() => {
      const saved = JSON.parse(localStorage.getItem(MAIL_LIST_SNAPSHOT_KEY) ?? "null");
      expect(saved?.emails.map((e: { id: string }) => e.id)).toEqual(["e1"]);
    });
  });

  it("paints the saved list immediately while the helper is still starting", () => {
    localStorage.setItem(MAIL_LIST_SNAPSHOT_KEY, JSON.stringify({
      accounts: [account],
      accountFolders: [{ account, folders: [inbox] }],
      emails: [email("e1", "Hello")],
    }));
    vi.mocked(api.listAccounts).mockReturnValue(never());

    const { result } = renderHook(() => useMailClient());

    expect(result.current.isBootstrappingAccounts).toBe(false);
    expect(result.current.selection).toEqual({ type: "aggregate", folderType: "Inbox" });
    expect(result.current.emails.map((e) => e.id)).toEqual(["e1"]);
    expect(api.listEmails).not.toHaveBeenCalled();
  });

  it("replaces the saved list with live data once the helper answers", async () => {
    localStorage.setItem(MAIL_LIST_SNAPSHOT_KEY, JSON.stringify({
      accounts: [account],
      accountFolders: [{ account, folders: [inbox] }],
      emails: [email("old", "Stale")],
    }));
    vi.mocked(api.listAccounts).mockResolvedValue([account]);
    vi.mocked(api.listFolders).mockResolvedValue([inbox]);
    vi.mocked(api.listEmails).mockResolvedValue([email("new", "Fresh")]);

    const { result } = renderHook(() => useMailClient());

    await waitFor(() => expect(result.current.emails.map((e) => e.id)).toEqual(["new"]));
    expect(api.listFolders).toHaveBeenCalledWith("acc-1");
  });

  it("drops the saved list when the last account is gone", async () => {
    localStorage.setItem(MAIL_LIST_SNAPSHOT_KEY, JSON.stringify({
      accounts: [account],
      accountFolders: [{ account, folders: [inbox] }],
      emails: [email("e1", "Hello")],
    }));
    vi.mocked(api.listAccounts).mockResolvedValue([]);

    const { result } = renderHook(() => useMailClient());

    await waitFor(() => expect(result.current.accounts).toEqual([]));
    expect(result.current.emails).toEqual([]);
    expect(localStorage.getItem(MAIL_LIST_SNAPSHOT_KEY)).toBeNull();
  });
});
