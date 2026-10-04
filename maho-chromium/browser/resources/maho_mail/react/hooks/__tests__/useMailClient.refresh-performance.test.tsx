import {act, renderHook} from "@testing-library/react";
import {beforeEach, describe, expect, it, vi} from "vitest";
import {useMailClient} from "../useMailClient";
import type {AccountSummary, EmailSummary} from "../../types";
import * as api from "../../api";

vi.mock("../../api", () => ({
  listAccounts: vi.fn(), listFolders: vi.fn(), listEmails: vi.fn(), syncFolder: vi.fn(),
}));

const accounts: AccountSummary[] = ["one", "two", "three"].map(id => ({
  id, email: id + "@example.com", display_name: id, auth_type: "password",
}));

function email(accountId: string, index: number): EmailSummary {
  return {
    id: accountId + index, account_id: accountId, folder_id: accountId + "-inbox",
    uid: index, message_id: accountId + index, subject: "Message " + index,
    from_address: "sender@example.com", from_name: "Sender",
    date: new Date(Date.UTC(2026, 0, 1, 0, 0, index)).toISOString(), snippet: "",
    is_read: true, is_starred: false, is_draft: false, has_attachments: false,
  };
}

describe("aggregate refresh request bounds", () => {
  beforeEach(() => vi.resetAllMocks());

  it.each(['child', 'aggregate'] as const)('ignores stale %s sync results after selecting another mailbox', async (mode) => {
    vi.mocked(api.listAccounts).mockResolvedValue([accounts[0]]);
    vi.mocked(api.listFolders).mockResolvedValue([
      { id: 'one-inbox', account_id: 'one', name: 'Inbox', path: 'INBOX', folder_type: 'Inbox', unread_count: 0, total_count: 1 },
      { id: 'sent', account_id: 'one', name: 'Sent', path: 'Sent', folder_type: 'Sent', unread_count: 0, total_count: 1 },
    ]);
    const sent = { ...email('one', 2), folder_id: 'sent' };
    vi.mocked(api.listEmails).mockImplementation(async (_account, folder) => folder === 'sent' ? [sent] : [email('one', 1)]);
    const pending = Promise.withResolvers<EmailSummary[]>();
    vi.mocked(api.syncFolder).mockReturnValue(pending.promise);
    const { result } = renderHook(() => useMailClient());
    await act(async () => {});
    await act(async () => { await result.current.loadAllFolders(); });
    if (mode === 'child') await act(async () => { result.current.selectChild('one', 'one-inbox'); });
    let sync: Promise<void>;
    act(() => { sync = result.current.syncCurrentFolder(); });
    await act(async () => { result.current.selectChild('one', 'sent'); });
    expect(result.current.emails).toEqual([sent]);
    await act(async () => { pending.resolve([email('one', 99)]); await sync!; });
    expect(result.current.emails).toEqual([sent]);
    expect(result.current.selectedFolderId).toBe('sent');
  });

  it("preserves each account's loaded depth through repeated refresh and pagination", async () => {
    const ready = Promise.withResolvers<AccountSummary[]>();
    vi.mocked(api.listAccounts).mockReturnValue(ready.promise);
    vi.mocked(api.listFolders).mockImplementation(async accountId => [{
      id: accountId + "-inbox", account_id: accountId, name: "Inbox", path: "INBOX",
      folder_type: "Inbox", unread_count: 0, total_count: 500,
    }]);
    const mailbox = new Map(accounts.map(account => [
      account.id, Array.from({length: account.id === "three" ? 20 : 500}, (_, i) => email(account.id, i)),
    ]));
    vi.mocked(api.listEmails).mockImplementation(async (accountId, _folderId, limit, offset) =>
      (mailbox.get(accountId) ?? []).slice(offset, offset + limit));

    const {result, unmount} = renderHook(() => useMailClient());
    try {
      await act(async () => ready.resolve(accounts));
      await act(async () => { await result.current.loadAllFolders(); });
      expect(result.current.emails).toHaveLength(120);
      const initialIds = result.current.emails.map(item => item.id);

      for (let refresh = 0; refresh < 2; ++refresh) {
        vi.mocked(api.listEmails).mockClear();
        await act(async () => { await result.current.loadAllFolders(); });
        expect(api.listEmails).toHaveBeenCalledTimes(3);
        for (const account of accounts) {
          expect(api.listEmails).toHaveBeenCalledWith(account.id, account.id + "-inbox", 50, 0);
        }
        expect(result.current.emails.map(item => item.id)).toEqual(initialIds);
      }

      await act(async () => { await result.current.loadMoreEmails(); });
      expect(result.current.emails).toHaveLength(220);
      vi.mocked(api.listEmails).mockClear();
      await act(async () => { await result.current.loadAllFolders(); });
      expect(api.listEmails).toHaveBeenCalledWith("one", "one-inbox", 100, 0);
      expect(api.listEmails).toHaveBeenCalledWith("two", "two-inbox", 100, 0);
      expect(api.listEmails).toHaveBeenCalledWith("three", "three-inbox", 50, 0);
      expect(result.current.emails).toHaveLength(220);
      expect(result.current.hasMore).toBe(true);
    } finally {
      unmount();
    }
  });
});

