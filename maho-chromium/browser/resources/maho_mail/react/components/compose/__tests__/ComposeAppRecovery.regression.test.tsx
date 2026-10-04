import React from 'react';
import { act, fireEvent, render, screen } from '@testing-library/react';
import { beforeEach, afterEach, describe, expect, it, vi } from 'vitest';
import { App } from '../../../app';
import { TestProviders, createMockAccount, createMockEmail, createMockEmailDetail, createMockFolder } from '../../../test/mocks';
import * as api from '../../../api';
import { toast } from '../../ui/Toast';
import type { ComposeEmailRequest, SendRequestedOptions, ComposeAttachment } from '../../../types';

let lastModalProps: Record<string, any> = {};

vi.mock('../../ui/Toast', () => {
  const toast = vi.fn();
  return { toast, useToast: () => ({ toast }), ToastContainer: () => null };
});

vi.mock('../../../i18n', () => ({}));
vi.mock('../../../mojo_client', () => ({
  handler: {
    listAccounts: async () => ({ ok: true, resultJson: '[{"id":"acc-1","email":"user@example.com","auth_type":"password"}]' }),
    getBrowserUiPrefs: async () => ({ prefsJson: '{}' }),
  },
  callbackRouter: {
    onAccountsChanged: { addListener: vi.fn() },
    onBrowserUiPrefsChanged: { addListener: vi.fn() },
    onLifecycleChanged: { addListener: vi.fn() },
    removeListener: vi.fn(),
  },
}));

vi.mock('../../../api', () => ({
  listAccounts: vi.fn(),
  listFolders: vi.fn(),
  syncFolders: vi.fn(),
  listEmails: vi.fn(),
  getEmail: vi.fn(),
  moveEmail: vi.fn(),
  deleteEmail: vi.fn(),
  reconnectAccount: vi.fn(),
  startOAuth2: vi.fn(),
  listSavedSearches: vi.fn(),
  searchEmails: vi.fn(),
  md5Hash: vi.fn(),
  getAppSetting: vi.fn(),
  startAutoSync: vi.fn(),
  stopAutoSync: vi.fn(),
  startScheduler: vi.fn(),
  stopScheduler: vi.fn(),
  listCalendarEvents: vi.fn(),
  listAccountCalendars: vi.fn(),
  listCalendarCategories: vi.fn(),
  toggleStar: vi.fn(),
  markRead: vi.fn(),
  getReplyContext: vi.fn(),
  sendEmail: vi.fn(),
  saveDraft: vi.fn(),
  updateDraft: vi.fn(),
  getForwardedAttachments: vi.fn(),
  queueEmail: vi.fn(),
  syncFolder: vi.fn(),
}));

vi.mock('../../../hooks/useAuthStatus', () => ({ useAuthStatus: () => ({ authErrors: [], clearAuthError: vi.fn() }) }));
vi.mock('../../../hooks/useReauthRetryQueue', () => ({ useReauthRetryQueue: () => ({ enqueueRetry: vi.fn() }) }));
vi.mock('../../../hooks/useAutoSync', () => ({ useAutoSync: () => {} }));
vi.mock('../../../hooks/useTrayBadge', () => ({ useTrayBadge: () => {} }));
vi.mock('../../../hooks/useAppLifecycle', () => ({ useAppLifecycle: () => {} }));
vi.mock('../../../hooks/useDeepLink', () => ({ useDeepLink: () => {} }));
vi.mock('../../../hooks/useTheme', () => ({ useTheme: () => {} }));
vi.mock('../../../hooks/useNetworkStatus', () => ({ useNetworkStatus: () => ({ isOnline: true, pendingMutationCount: 0 }) }));
vi.mock('../../../hooks/useNotificationSound', () => ({ useNotificationSound: () => ({ playSound: vi.fn() }) }));
vi.mock('../../../hooks/useNotifications', () => ({ useNotifications: () => ({ notify: vi.fn() }) }));
vi.mock('../../../hooks/useSettings', () => ({ useSettings: () => ({ syncInterval: 0, sidebarWidth: 240, emailListWidth: 350, undoSendDelay: 5 }) }));
vi.mock('../../../components/account/AddAccountModal', () => ({ AddAccountModal: () => null }));
vi.mock('../../../components/common/OnboardingTour', () => ({ OnboardingTour: () => null }));
vi.mock('../../../components/calendar/CreateEventDialog', () => ({ CreateEventDialog: () => null }));
vi.mock('../../../components/layout/EmailContent', () => ({ EmailContent: () => null }));

vi.mock('../ComposeModal', () => ({
  ComposeModal: (props: any) => {
    lastModalProps = props;
    if (!props.isOpen) return null;
    return (
      <div data-testid="compose-modal">
        <span data-testid="modal-draft-id">{props.initialDraftId ?? ''}</span>
        <span data-testid="modal-subject">{props.initialSubject ?? ''}</span>
        <span data-testid="modal-body">{props.initialBody ?? ''}</span>
        <span data-testid="modal-body-html">{props.initialBodyHtml ?? ''}</span>
        <span data-testid="modal-account">{props.accountId ?? ''}</span>
        <button
          data-testid="simulate-send-btn"
          onClick={async () => {
            const req: ComposeEmailRequest = {
              account_id: props.accountId || 'acc-1',
              to: [props.initialTo || 'to@example.com'],
              subject: props.initialSubject || 'Subj',
              body_text: props.initialBody || 'Body',
              body_html: props.initialBodyHtml,
              attachments: props.initialAttachments,
            };
            const opts: SendRequestedOptions = {
              draftId: props.initialDraftId,
              composition: {
                accountId: props.accountId || 'acc-1',
                to: props.initialTo,
                subject: props.initialSubject,
                body: props.initialBody,
                bodyHtml: props.initialBodyHtml,
                attachments: props.initialAttachments,
                draftId: props.initialDraftId,
                readReceipt: props.initialReadReceipt,
              },
            };
            await props.onSendRequested?.(req, opts);
            props.onClose();
          }}
        >
          Send
        </button>
      </div>
    );
  },
}));

describe("App compose recovery & draft integration", () => {
  const folders = [createMockFolder({ folder_type: "Inbox" }), createMockFolder({ folder_type: "Drafts", id: "drafts-1" })];

  beforeEach(() => {
    vi.resetAllMocks();
    lastModalProps = {};
    vi.useFakeTimers();
    Object.defineProperty(HTMLElement.prototype, 'offsetHeight', { configurable: true, get: () => 600 });
    vi.mocked(api.listAccounts).mockResolvedValue([createMockAccount({ id: "acc-1", email: "user@example.com" })]);
    vi.mocked(api.listFolders).mockResolvedValue(folders);
    vi.mocked(api.syncFolders).mockResolvedValue(folders.map(f => ({ ...f, unread_count: 0 })));
    vi.mocked(api.listEmails).mockResolvedValue([]);
    vi.mocked(api.syncFolder).mockResolvedValue([]);
    vi.mocked(api.listSavedSearches).mockResolvedValue([]);
    vi.mocked(api.getAppSetting).mockResolvedValue(null);
    vi.mocked(api.md5Hash).mockResolvedValue('hash');
    vi.mocked(api.sendEmail).mockResolvedValue({ status: 'sent' });
    vi.mocked(api.saveDraft).mockResolvedValue('saved-pending');
    vi.mocked(api.updateDraft).mockResolvedValue(undefined);
    vi.mocked(api.deleteEmail).mockResolvedValue(undefined);
    vi.mocked(api.getForwardedAttachments).mockResolvedValue([
      { filename: "hydrated.pdf", mime_type: "application/pdf", data: "PDFBYTES" },
    ]);
  });

  afterEach(() => {
    vi.clearAllTimers();
    vi.useRealTimers();
  });

  it('refuses to open a draft whose attachment bytes cannot be loaded', async () => {
    const email = createMockEmail({ id: 'broken', subject: 'Broken draft', is_draft: true });
    vi.mocked(api.listEmails).mockResolvedValue([email]);
    vi.mocked(api.getEmail).mockResolvedValue(createMockEmailDetail({ email, attachments: [{ id: 'a', email_id: email.id, filename: 'real.pdf', mime_type: 'application/pdf', size: 100, part_id: '1', content_id: null }] }));
    vi.mocked(api.getForwardedAttachments).mockRejectedValue(new Error('hydration failed'));
    await act(async () => { render(<App />, { wrapper: TestProviders }); });
    await act(async () => { fireEvent.click(screen.getByText('Broken draft')); });
    expect(lastModalProps.isOpen).toBe(false);
    expect(toast).toHaveBeenCalledWith('error', 'hydration failed');
    expect(api.saveDraft).not.toHaveBeenCalled();
    expect(api.sendEmail).not.toHaveBeenCalled();
  });

  it('retains security requirements when undo restores plaintext', async () => {
    await act(async () => { render(<App />, { wrapper: TestProviders }); });
    await act(async () => {
      await lastModalProps.onSendRequested({ account_id: 'acc-1', to: ['a@example.com'], subject: 'A', body_text: 'ciphertext' },
        { composition: { accountId: 'acc-1', body: 'secret', pgpEncrypt: true, smimeSign: false, smimeEncrypt: false } });
    });
    await act(async () => { fireEvent.click(screen.getByRole('button', { name: 'Undo' })); });
    expect(lastModalProps.initialBody).toBe('secret');
    expect(lastModalProps.initialPgpEncrypt).toBe(true);
  });

  it('persists pending plaintext and guards navigation before delivery', async () => {
    await act(async () => { render(<App />, { wrapper: TestProviders }); });
    await act(async () => {
      await lastModalProps.onSendRequested({ account_id: 'acc-1', to: ['a@example.com'], subject: 'A', body_text: 'ciphertext' },
        { composition: { accountId: 'acc-1', to: 'a@example.com', subject: 'A', body: 'secret', bodyHtml: '<b>secret</b>' } });
    });
    expect(api.saveDraft).toHaveBeenCalledWith(expect.objectContaining({ body_text: 'secret', body_html: '<b>secret</b>' }));
    const unload = new Event('beforeunload', { cancelable: true });
    window.dispatchEvent(unload);
    expect(unload.defaultPrevented).toBe(true);
    expect(api.sendEmail).not.toHaveBeenCalled();
  });

  it.each(['undo', 'failure'])('defers %s recovery until the active composition closes', async (mode) => {
    vi.mocked(api.sendEmail).mockRejectedValueOnce(new Error('delivery failed'));
    await act(async () => { render(<App />, { wrapper: TestProviders }); });
    await act(async () => {
      await lastModalProps.onSendRequested({ account_id: 'acc-1', to: ['a@example.com'], subject: 'A', body_text: 'A body' },
        { composition: { accountId: 'acc-1', to: 'a@example.com', subject: 'A', body: 'A body' } });
    });
    await act(async () => { fireEvent.click(screen.getByRole('button', { name: 'Compose' })); });
    const activeProps = lastModalProps;
    await act(async () => {
      if (mode === 'undo') fireEvent.click(screen.getByRole('button', { name: 'Undo' }));
      else await vi.advanceTimersByTimeAsync(5000);
    });
    expect(lastModalProps.initialBody).toBe(activeProps.initialBody);
    expect(lastModalProps.initialTo).toBe(activeProps.initialTo);
    await act(async () => { lastModalProps.onClose(); });
    expect(lastModalProps.initialBody).toBe('A body');
    expect(lastModalProps.initialTo).toBe('a@example.com');
  });

  it('forwards an unselected row using its own attachment source and account', async () => {
    const email = createMockEmail({ id: 'forward-row', subject: 'Forward this row', account_id: 'acc-source', uid: 42, folder_id: 'source-folder', has_attachments: true });
    vi.mocked(api.listEmails).mockResolvedValue([email]);
    vi.mocked(api.getEmail).mockResolvedValue(createMockEmailDetail({ email }));
    vi.mocked(api.getReplyContext).mockResolvedValue({ original_subject: email.subject, original_from: 'sender@example.com', original_date: email.date, original_body_text: 'body', original_body_html: null });
    await act(async () => { render(<App />, { wrapper: TestProviders }); });
    await act(async () => { fireEvent.contextMenu(screen.getByText('Forward this row')); });
    await act(async () => { fireEvent.click(screen.getByText('Forward')); });
    expect(lastModalProps.forwardEmailUid).toBe(42);
    expect(lastModalProps.forwardFolderId).toBe('source-folder');
    expect(lastModalProps.accountId).toBe('acc-source');
  });

  it("C02 multiple delayed sends do not overwrite each other", async () => {
    await act(async () => {
      render(<App />, { wrapper: TestProviders });
    });

    // Open compose for message 1
    await act(async () => {
      fireEvent.click(screen.getByRole("button", { name: "Compose" }));
    });

    // Simulate send of message 1
    await act(async () => {
      lastModalProps.onSendRequested(
        { account_id: "acc-1", to: ["m1@example.com"], subject: "Msg 1", body_text: "Text 1" },
        { draftId: "draft-1", composition: { accountId: "acc-1", to: "m1@example.com", subject: "Msg 1", body: "Text 1", draftId: "draft-1" } }
      );
      lastModalProps.onClose();
    });

    // Message 1 Undo button should be visible
    expect(screen.getAllByRole("button", { name: "Undo" })).toHaveLength(1);

    // Open compose for message 2
    await act(async () => {
      fireEvent.click(screen.getByRole("button", { name: "Compose" }));
    });

    // Simulate send of message 2
    await act(async () => {
      lastModalProps.onSendRequested(
        { account_id: "acc-1", to: ["m2@example.com"], subject: "Msg 2", body_text: "Text 2" },
        { draftId: "draft-2", composition: { accountId: "acc-1", to: "m2@example.com", subject: "Msg 2", body: "Text 2", draftId: "draft-2" } }
      );
      lastModalProps.onClose();
    });

    // Both messages must have active Undo affordance (message 1 was NOT overwritten)
    expect(screen.getAllByRole("button", { name: "Undo" })).toHaveLength(2);
  });

  it("C02 Undo restores full composition with original account/draft and does not delete draft", async () => {
    await act(async () => {
      render(<App />, { wrapper: TestProviders });
    });

    await act(async () => {
      fireEvent.click(screen.getByRole("button", { name: "Compose" }));
    });

    await act(async () => {
      lastModalProps.onSendRequested(
        { account_id: "acc-1", to: ["client@example.com"], subject: "Confidential", body_text: "Body text" },
        { draftId: "draft-xyz", composition: { accountId: "acc-1", to: "client@example.com", subject: "Confidential", body: "Body text", draftId: "draft-xyz" } }
      );
      lastModalProps.onClose();
    });

    // Click Undo
    const undoBtn = screen.getByRole("button", { name: "Undo" });
    await act(async () => {
      fireEvent.click(undoBtn);
    });

    // ComposeModal should be restored with original draftId, subject, body
    expect(lastModalProps.isOpen).toBe(true);
    expect(lastModalProps.initialDraftId).toBe("draft-xyz");
    expect(lastModalProps.initialSubject).toBe("Confidential");
    expect(lastModalProps.initialBody).toBe("Body text");

    // Draft was NOT deleted
    expect(api.deleteEmail).not.toHaveBeenCalled();
  });

  it("C02 send failure restores full composition and does not silently queue mobile duplicate", async () => {
    vi.mocked(api.sendEmail).mockRejectedValueOnce(new Error("SMTP 550 Relay access denied"));

    await act(async () => {
      render(<App />, { wrapper: TestProviders });
    });

    await act(async () => {
      fireEvent.click(screen.getByRole("button", { name: "Compose" }));
    });

    await act(async () => {
      lastModalProps.onSendRequested(
        { account_id: "acc-1", to: ["dest@example.com"], subject: "Failed Send", body_text: "Lost text" },
        { draftId: "draft-fail", composition: { accountId: "acc-1", to: "dest@example.com", subject: "Failed Send", body: "Lost text", draftId: "draft-fail" } }
      );
      lastModalProps.onClose();
    });

    // Advance timer past delay
    await act(async () => {
      vi.advanceTimersByTime(6000);
    });

    // Must restore composition on failure so work is not lost
    expect(lastModalProps.isOpen).toBe(true);
    expect(lastModalProps.initialDraftId).toBe("draft-fail");
    expect(lastModalProps.initialSubject).toBe("Failed Send");
    expect(lastModalProps.initialBody).toBe("Lost text");

    // Must NOT delete draft on failure
    expect(api.deleteEmail).not.toHaveBeenCalled();
    // Must NOT call queueEmail (no silent mobile duplicate queue)
    expect(api.queueEmail).not.toHaveBeenCalled();
  });

  it("C02 deletes draft only after actual acknowledged delivery", async () => {
    await act(async () => {
      render(<App />, { wrapper: TestProviders });
    });

    await act(async () => {
      fireEvent.click(screen.getByRole("button", { name: "Compose" }));
    });

    await act(async () => {
      lastModalProps.onSendRequested(
        { account_id: "acc-1", to: ["dest@example.com"], subject: "Success", body_text: "Sent" },
        { draftId: "draft-del", composition: { accountId: "acc-1", to: "dest@example.com", subject: "Success", body: "Sent", draftId: "draft-del" } }
      );
      lastModalProps.onClose();
    });

    // Before timer completes, draft is NOT deleted
    expect(api.deleteEmail).not.toHaveBeenCalled();

    // Advance timer past delay
    await act(async () => {
      vi.advanceTimersByTime(6000);
    });

    // Now sendEmail was called and succeeded, so deleteEmail is called for the draft!
    expect(api.sendEmail).toHaveBeenCalledTimes(1);
    expect(api.deleteEmail).toHaveBeenCalledWith("draft-del");
  });

  it.each([
    ['sent', 'success'], ['queued', 'info'], ['uncertain', 'warning'],
  ] as const)('reports shell %s receipt without a second queue', async (status, severity) => {
    vi.mocked(api.sendEmail).mockResolvedValueOnce({ status });
    await act(async () => { render(<App />, { wrapper: TestProviders }); });
    await act(async () => { fireEvent.click(screen.getByRole('button', { name: 'Compose' })); });
    await act(async () => {
      lastModalProps.onSendRequested(
        { account_id: 'acc-1', to: ['dest@example.com'], subject: 'Receipt', body_text: 'Body' },
        { draftId: 'receipt-draft', composition: { accountId: 'acc-1', to: 'dest@example.com', subject: 'Receipt', body: 'Body', draftId: 'receipt-draft' } },
      );
      lastModalProps.onClose();
    });
    vi.mocked(toast).mockClear();
    await act(async () => { await vi.advanceTimersByTimeAsync(5000); });
    expect(api.sendEmail).toHaveBeenCalledOnce();
    expect(api.queueEmail).not.toHaveBeenCalled();
    expect(api.deleteEmail).toHaveBeenCalledWith('receipt-draft');
    expect(vi.mocked(toast).mock.calls.map(([kind]) => kind)).toEqual([severity]);
    expect(screen.queryByRole('button', { name: 'Undo' })).not.toBeInTheDocument();
  });

  it("C03 openDraftForEditing restores complete draft with html and attachments", async () => {
    const draftEmail = createMockEmail({
      id: "draft-detail-1",
      folder_id: "folder-1",
      account_id: "acc-special",
      subject: "Complete Draft",
      body_text: "Plain text",
      body_html: "<b>Rich HTML</b>",
      has_attachments: true,
      mdn_requested: "receipt",
      is_draft: true,
      to_addresses: "recipient@example.com",
    });

    vi.mocked(api.listEmails).mockResolvedValue([draftEmail]);
    vi.mocked(api.syncFolder).mockResolvedValue([draftEmail]);
    vi.mocked(api.getEmail).mockResolvedValue(
      createMockEmailDetail({
        email: draftEmail,
        attachments: [{ id: "a1", filename: "hydrated.pdf", mime_type: "application/pdf", size: 100, part_id: "1", email_id: "draft-detail-1", content_id: null }],
      })
    );

    await act(async () => {
      render(<App />, { wrapper: TestProviders });
    });

    // Click on draft in email list
    const draftItem = screen.getByText("Complete Draft");
    await act(async () => {
      fireEvent.click(draftItem);
    });

    expect(lastModalProps.isOpen).toBe(true);
    expect(lastModalProps.initialDraftId).toBe("draft-detail-1");
    expect(lastModalProps.initialBodyHtml).toBe("<b>Rich HTML</b>");
    expect(lastModalProps.initialAttachments).toEqual([
      { filename: "hydrated.pdf", mime_type: "application/pdf", data: "PDFBYTES" },
    ]);
    expect(lastModalProps.accountId).toBe("acc-special");
    expect(lastModalProps.initialReadReceipt).toBe(true);
  });
});
