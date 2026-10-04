import React from 'react';
import {act, fireEvent, render, renderHook, screen, within} from '@testing-library/react';
import {beforeEach, describe, expect, it, vi} from 'vitest';
import {App} from '../app';
import {useMailClient} from '../hooks/useMailClient';
import {EmailList} from '../components/layout/EmailList';
import {TestProviders, createMockAccount, createMockEmail, createMockEmailDetail, createMockFolder} from '../test/mocks';
import * as api from '../api';

vi.mock('../i18n', () => ({}));
vi.mock('../mojo_client', () => ({
  handler: {listAccounts: async () => ({ok: true, resultJson: '[{"id":"acc-1","email":"user@example.com","auth_type":"password"}]'}), getBrowserUiPrefs: async () => ({prefsJson: '{}'})},
  callbackRouter: {onAccountsChanged: {addListener: vi.fn()}, onBrowserUiPrefsChanged: {addListener: vi.fn()}, onLifecycleChanged: {addListener: vi.fn()}, removeListener: vi.fn()},
}));
vi.mock('../api', () => ({
  listAccounts: vi.fn(), listFolders: vi.fn(), syncFolders: vi.fn(), listEmails: vi.fn(), getEmail: vi.fn(),
  moveEmail: vi.fn(), deleteEmail: vi.fn(), reconnectAccount: vi.fn(), startOAuth2: vi.fn(),
  listSavedSearches: vi.fn(), searchEmails: vi.fn(), md5Hash: vi.fn(), getAppSetting: vi.fn(),
  startAutoSync: vi.fn(), stopAutoSync: vi.fn(), startScheduler: vi.fn(), stopScheduler: vi.fn(),
  listCalendarEvents: vi.fn(), listAccountCalendars: vi.fn(), listCalendarCategories: vi.fn(),
  toggleStar: vi.fn(), markRead: vi.fn(), getReplyContext: vi.fn(),
}));
vi.mock('../hooks/useAuthStatus', () => ({useAuthStatus: () => ({authErrors: [], clearAuthError: vi.fn()})}));
vi.mock('../hooks/useReauthRetryQueue', () => ({useReauthRetryQueue: () => ({enqueueRetry: vi.fn()})}));
vi.mock('../hooks/useAutoSync', () => ({useAutoSync: () => {}}));
vi.mock('../hooks/useTrayBadge', () => ({useTrayBadge: () => {}}));
vi.mock('../hooks/useAppLifecycle', () => ({useAppLifecycle: () => {}}));
vi.mock('../hooks/useDeepLink', () => ({useDeepLink: () => {}}));
vi.mock('../hooks/useTheme', () => ({useTheme: () => {}}));
vi.mock('../hooks/useNetworkStatus', () => ({useNetworkStatus: () => ({isOnline: true, pendingMutationCount: 0})}));
vi.mock('../hooks/useNotificationSound', () => ({useNotificationSound: () => ({playSound: vi.fn()})}));
vi.mock('../hooks/useNotifications', () => ({useNotifications: () => ({notify: vi.fn()})}));
vi.mock('../hooks/useSettings', () => ({useSettings: () => ({syncInterval: 0, sidebarWidth: 240, emailListWidth: 350})}));
vi.mock('../components/compose/ComposeModal', () => ({ComposeModal: ({isOpen}: {isOpen: boolean}) => isOpen ? <div data-testid="mail-compose" /> : null}));
vi.mock('../components/account/AddAccountModal', () => ({AddAccountModal: () => null}));
vi.mock('../components/common/OnboardingTour', () => ({OnboardingTour: () => null}));
vi.mock('../components/calendar/CreateEventDialog', () => ({CreateEventDialog: () => null}));
vi.mock('../components/layout/EmailContent', () => ({EmailContent: (p: {emailDetail: ReturnType<typeof createMockEmailDetail> | null; onBack: () => void; authRecoveryError: unknown; onReconnectAccount: () => void}) => <div>
  {p.emailDetail && <><span data-testid="reader-id">{p.emailDetail.email.id}</span><button onClick={p.onBack}>Back</button></>}
  {p.authRecoveryError ? <button onClick={p.onReconnectAccount}>Reconnect</button> : null}
</div>}));

const email = createMockEmail({id: 'fixture', subject: 'Reader fixture', is_read: true});
const folders = [createMockFolder({folder_type: 'Inbox'}), createMockFolder({id: 'archive-1', folder_type: 'Archive'})];

beforeEach(() => {
  vi.resetAllMocks();
  localStorage.clear();
  Object.defineProperty(HTMLElement.prototype, 'offsetHeight', {configurable: true, get: () => 600});
  vi.mocked(api.listAccounts).mockResolvedValue([createMockAccount()]);
  vi.mocked(api.listFolders).mockResolvedValue(folders);
  vi.mocked(api.syncFolders).mockResolvedValue(folders.map(f => ({...f, unread_count: 4})));
  vi.mocked(api.listEmails).mockResolvedValue([email]);
  vi.mocked(api.getEmail).mockResolvedValue(createMockEmailDetail({email}));
  vi.mocked(api.listSavedSearches).mockResolvedValue([{id: 'saved', name: 'Fixture search', query: 'invoice', account_id: null, created_at: ''}]);
  vi.mocked(api.searchEmails).mockResolvedValue({emails: [email], total_count: 1, query: 'invoice'});
  vi.mocked(api.getAppSetting).mockResolvedValue(null);
  vi.mocked(api.md5Hash).mockResolvedValue('hash');
  vi.mocked(api.startOAuth2).mockResolvedValue({auth_url: '', state: ''});
  vi.mocked(api.reconnectAccount).mockResolvedValue(undefined);
  vi.mocked(api.listCalendarEvents).mockResolvedValue([]);
  vi.mocked(api.listAccountCalendars).mockResolvedValue([]);
  vi.mocked(api.listCalendarCategories).mockResolvedValue([]);
});

async function mountShell() {
  await act(async () => {render(<App />, {wrapper: TestProviders});});
  expect(screen.getByText('Reader fixture')).toBeInTheDocument();
}
async function openEmail() {
  await act(async () => {fireEvent.click(screen.getByText('Reader fixture'));});
}

describe('reader shell regressions', () => {
  it('R-16 folder menu focuses actions and Escape returns focus', async () => {
    vi.mocked(api.listFolders).mockResolvedValue([...folders, createMockFolder({id: 'custom', name: 'Projects', path: 'Projects', folder_type: 'Custom'})]);
    await mountShell();
    await act(async () => {fireEvent.click(screen.getByLabelText('Expand Custom Folders section'));});
    const trigger = screen.getByRole('button', {name: 'Actions for Projects'});
    trigger.focus();
    await act(async () => {fireEvent.click(trigger);});
    const items = screen.getAllByRole('menuitem');
    expect(items[0]).toHaveFocus();
    await act(async () => {fireEvent.keyDown(document.activeElement ?? document.body, {key: 'ArrowDown'});});
    expect(items[1]).toHaveFocus();
    await act(async () => {fireEvent.keyDown(document.activeElement ?? document.body, {key: 'Escape'});});
    expect(screen.queryByRole('menu')).toBeNull();
    expect(trigger).toHaveFocus();
  });
  it('R-24 discards a pending page when the saved query changes', async () => {
    const page = Array.from({length: 50}, (_, i) => createMockEmail({id: `match-${i}`}));
    let complete = (_: {emails: typeof page; total_count: number; query: string}) => {throw new Error('missing callback');};
    const pending = new Promise<{emails: typeof page; total_count: number; query: string}>(resolve => {complete = resolve;});
    vi.mocked(api.searchEmails).mockResolvedValueOnce({emails: page, total_count: 51, query: 'invoice'})
      .mockReturnValueOnce(pending).mockResolvedValueOnce({emails: [email], total_count: 1, query: 'new'});
    const props = {emails: [], selectedEmailId: null, onSelectEmail: vi.fn(), onToggleStar: vi.fn(), loading: false};
    const view = render(<EmailList {...props} searchRequest={{query: 'invoice'}} />, {wrapper: TestProviders});
    await act(async () => {});
    try {
      await act(async () => {fireEvent.click(screen.getByRole('button', {name: 'Load more search results'}));});
      await act(async () => {view.rerender(<EmailList {...props} searchRequest={{query: 'new'}} />);});
    } finally {
      await act(async () => {complete({emails: [createMockEmail({subject: 'Stale page'})], total_count: 51, query: 'invoice'}); await pending;});
    }
    expect(screen.getByText('Reader fixture')).toBeInTheDocument();
    expect(screen.queryByText('Stale page')).toBeNull();
    expect(screen.queryByRole('button', {name: 'Load more search results'})).toBeNull();
  });
  it.each([760, 820])('R-2 folder drawer selects a mailbox at %s px', async width => {
    Object.defineProperty(window, 'innerWidth', {configurable: true, value: width});
    await mountShell();
    await act(async () => {fireEvent.click(screen.getByRole('button', {name: 'Open mailbox folders'}));});
    const drawer = screen.getByRole('dialog', {name: 'Mailbox folders'});
    await act(async () => {fireEvent.click(within(drawer).getByText('Archive'));});
    expect(screen.queryByRole('dialog', {name: 'Mailbox folders'})).toBeNull();
    expect(document.querySelector('[data-email-list-pane]')).toHaveTextContent('Archive');
  });
  it.each([
    {key: 'r'}, {key: 'f'}, {key: 's'}, {key: 'Delete'}, {key: 'u'},
    {key: 'R', shiftKey: true}, {key: 'Enter'}, {key: '/'},
    {key: 'r', metaKey: true, binding: 'Ctrl+r'},
    {key: '#', code: 'Digit3', shiftKey: true, binding: '#'},
  ])('Calendar does not consume mail binding $key $binding', async input => {
    if (input.binding) localStorage.setItem('maho-toolbar-config-cache', JSON.stringify({shortcuts: {reply: input.binding}}));
    await mountShell();
    await openEmail();
    await act(async () => {fireEvent.click(screen.getByText('Calendar'));});
    const event = new KeyboardEvent('keydown', {...input, bubbles: true, cancelable: true});
    await act(async () => {document.body.dispatchEvent(event);});
    expect(event.defaultPrevented).toBe(false);
    expect(screen.queryByRole('alertdialog')).toBeNull();
    expect(screen.queryByTestId('mail-compose')).toBeNull();
  });
  it('R-11 loads keyword matches beyond fifty without folder pagination', async () => {
    Object.defineProperty(HTMLElement.prototype, 'offsetHeight', {configurable: true, get: () => 10000});
    const page = Array.from({length: 50}, (_, i) => createMockEmail({id: `match-${i}`, subject: `Match ${i}`}));
    vi.mocked(api.searchEmails).mockResolvedValueOnce({emails: page, total_count: 51, query: 'invoice'})
      .mockResolvedValueOnce({emails: [email], total_count: 51, query: 'invoice'});
    const onLoadMore = vi.fn();
    await act(async () => {render(<EmailList emails={[]} selectedEmailId={null} onSelectEmail={vi.fn()} onToggleStar={vi.fn()} loading={false} accountId="acc-1" folderId="folder-1" searchRequest={{query: 'invoice'}} onLoadMore={onLoadMore} hasMore />, {wrapper: TestProviders});});
    await act(async () => {fireEvent.click(screen.getByRole('button', {name: 'Load more search results'}));});
    expect(api.searchEmails).toHaveBeenLastCalledWith(expect.objectContaining({query: 'invoice', account_id: 'acc-1', folder_id: 'folder-1', limit: 50, offset: 50}));
    expect(screen.getByText('Reader fixture')).toBeInTheDocument();
    expect(screen.queryByRole('button', {name: 'Load more search results'})).toBeNull();
    expect(onLoadMore).not.toHaveBeenCalled();
  });
  it.each(['c', 'j', 'k', 'e'])('R-8/R-9 calendar view owns %s instead of hidden mail', async key => {
    await mountShell();
    await openEmail();
    await act(async () => {fireEvent.click(screen.getByText('Calendar'));});
    expect(document.querySelector('[data-email-list-pane]')).toBeNull();
    expect(api.listCalendarEvents).toHaveBeenCalled();
    vi.mocked(api.getEmail).mockClear();
    await act(async () => {fireEvent.keyDown(document.body, {key});});
    expect(screen.queryByTestId('mail-compose')).toBeNull();
    expect(api.getEmail).not.toHaveBeenCalled();
    expect(api.moveEmail).not.toHaveBeenCalled();
  });
  it('R-3 runs a saved search when the list mounts for that request', async () => {
    await act(async () => {render(<EmailList emails={[]} selectedEmailId={null} onSelectEmail={vi.fn()} onToggleStar={vi.fn()} loading={false} searchRequest={{query: 'invoice'}} />, {wrapper: TestProviders});});
    expect(screen.getByPlaceholderText('Search emails...')).toHaveValue('invoice');
    expect(api.searchEmails).toHaveBeenCalledWith(expect.objectContaining({query: 'invoice'}));
  });
  it('R-1 keeps the existing list mounted during folder synchronization', async () => {
    await mountShell();
    let complete = (_value: typeof folders) => {throw new Error('missing callback');};
    const pending = new Promise<typeof folders>(resolve => {complete = resolve;});
    vi.mocked(api.syncFolders).mockReturnValue(pending);
    const row = screen.getByText('Reader fixture');
    try {
      await act(async () => {fireEvent.click(screen.getByRole('button', {name: 'Sync folders'}));});
      expect(row.isConnected).toBe(true);
    } finally {
      await act(async () => {complete(folders); await pending;});
    }
  });
  it('R-1 preserves the reader and loaded pages when folder counts refresh', async () => {
    const page = Array.from({length: 50}, (_, index) => createMockEmail({id: `page-${index}`, is_read: true}));
    vi.mocked(api.listEmails).mockResolvedValueOnce(page).mockResolvedValueOnce([email]).mockResolvedValue([...page, email]);
    const {result} = renderHook(() => useMailClient());
    await act(async () => {await result.current.loadAccounts();});
    await act(async () => {await result.current.loadAllFolders();});
    await act(async () => {await result.current.loadMoreEmails();});
    await act(async () => {await result.current.selectEmail('fixture');});
    const calls = vi.mocked(api.listEmails).mock.calls.length;
    await act(async () => {await result.current.syncAllFolders();});
    expect(result.current.selectedEmail?.email.id).toBe('fixture');
    expect(api.listEmails).toHaveBeenCalledTimes(calls + 1);
    expect(api.listEmails).toHaveBeenLastCalledWith('acc-1', 'folder-1', 51, 0);
    expect(result.current.emails).toHaveLength(51);
  });
  it('R-2 shows the narrow list initially and Back restores it', async () => {
    await mountShell();
    const pane = document.querySelector('[data-email-list-pane]');
    if (!pane) throw new Error('email list pane missing');
    expect(pane.classList.contains('hidden')).toBe(false);
    await openEmail();
    await act(async () => {fireEvent.click(screen.getByText('Back'));});
    expect(screen.queryByTestId('reader-id')).toBeNull();
    expect(pane.classList.contains('hidden')).toBe(false);
  });
  it('R-3 saved searches populate the real search input and execute', async () => {
    await mountShell();
    await act(async () => {fireEvent.click(screen.getByText('Fixture search'));});
    expect(screen.getByPlaceholderText('Search emails...')).toHaveValue('invoice');
    expect(api.searchEmails).toHaveBeenCalledWith(expect.objectContaining({query: 'invoice'}));
  });
  it('R-4 recovery reconnects the failing account without choosing Gmail', async () => {
    vi.mocked(api.getEmail).mockRejectedValue(new Error('invalid_grant'));
    await mountShell();
    await openEmail();
    await act(async () => {fireEvent.click(screen.getByText('Reconnect'));});
    expect(api.reconnectAccount).toHaveBeenCalledWith('acc-1');
    expect(api.startOAuth2).not.toHaveBeenCalled();
  });
  it.each(['Archive', 'Delete'])('R-5 %s removes the folder row after success', async action => {
    await mountShell();
    await act(async () => {fireEvent.click(screen.getByTitle(action));});
    if (action === 'Delete') await act(async () => {fireEvent.click(within(screen.getByRole('alertdialog')).getByRole('button', {name: 'Delete'}));});
    expect(screen.queryByText('Reader fixture')).toBeNull();
  });
  it('R-6 archive hotkey keeps Inbox and uses the selected account archive', async () => {
    const other = createMockAccount({id: 'acc-2'});
    vi.mocked(api.listAccounts).mockResolvedValue([other, createMockAccount()]);
    vi.mocked(api.listFolders).mockImplementation(async id => id === 'acc-2' ? [createMockFolder({account_id: id, id: 'archive-2', folder_type: 'Archive'})] : folders);
    await mountShell();
    await openEmail();
    await act(async () => {fireEvent.keyDown(document, {key: 'e'});});
    expect(api.moveEmail).toHaveBeenCalledWith('fixture', 'archive-1');
    expect(screen.queryByText('Reader fixture')).toBeNull();
    expect(document.querySelector('[data-email-list-pane]')).toHaveTextContent('Inbox');
  });
  it('R-5 does not remove a row when deletion fails', async () => {
    vi.mocked(api.deleteEmail).mockRejectedValue(new Error('offline'));
    await mountShell();
    await act(async () => {fireEvent.click(screen.getByTitle('Delete'));});
    await act(async () => {fireEvent.click(within(screen.getByRole('alertdialog')).getByRole('button', {name: 'Delete'}));});
    expect(screen.getByText('Reader fixture')).toBeInTheDocument();
  });
});
