import {act, renderHook} from '@testing-library/react';
import {beforeEach, expect, it, vi} from 'vitest';
import {useMailClient} from '../useMailClient';
import {handler} from '../../mojo_client';
import {createMockAccount, createMockEmail, createMockFolder} from '../../test/mocks';

vi.mock('../../mojo_client', () => ({handler: {
  listAccounts: vi.fn(), listFolders: vi.fn(), listEmails: vi.fn(), syncFolders: vi.fn(),
}}));

const response = (value: unknown) => ({ok: true, resultJson: JSON.stringify(value)});
const accounts = [createMockAccount({id: 'A'}), createMockAccount({id: 'B'})];
const folder = (id: string) => createMockFolder({id: `${id}-inbox`, account_id: id, folder_type: 'Inbox'});
const page = (id: string, count = 50) => Array.from({length: count}, (_, i) => createMockEmail({id: `${id}-${i}`, account_id: id, folder_id: `${id}-inbox`, is_read: true}));

beforeEach(() => {
  vi.resetAllMocks();
  vi.mocked(handler.listAccounts).mockResolvedValue(response(accounts));
  vi.mocked(handler.listFolders).mockImplementation(async id => response([folder(id)]));
  vi.mocked(handler.listEmails).mockImplementation(async id => response(page(id)));
  vi.mocked(handler.syncFolders).mockResolvedValue(response({new_messages: 0}));
});

it('S01 retains folders and Inbox after the real API parses a sync count response', async () => {
  const {result} = renderHook(() => useMailClient());
  await act(async () => {await result.current.loadAccounts();});
  await act(async () => {await result.current.loadAllFolders();});
  await act(async () => {await result.current.syncAllFolders();});
  expect(handler.syncFolders).toHaveBeenCalledWith('A');
  expect(result.current.folders.map(f => f.id)).toEqual(['A-inbox', 'B-inbox']);
  expect(result.current.emails).toHaveLength(100);
  expect(handler.listFolders).toHaveBeenCalledTimes(4);
});

it.each(['child', 'aggregate'] as const)('S08 discards a late %s page after switching to B', async mode => {
  const {result} = renderHook(() => useMailClient());
  await act(async () => {await result.current.loadAccounts();});
  await act(async () => {await result.current.loadAllFolders();});
  if (mode === 'child') await act(async () => {result.current.selectChild('A', 'A-inbox');});
  let release!: (value: ReturnType<typeof response>) => void;
  const pending = new Promise<ReturnType<typeof response>>(resolve => {release = resolve;});
  vi.mocked(handler.listEmails).mockImplementation(async (id, _folder, _limit, offset) => {
    if (id === 'A' && offset === 50n) return pending;
    return response(page(id, offset === 0n ? 50 : 0));
  });
  let oldRequest!: Promise<void>;
  act(() => {oldRequest = result.current.loadMoreEmails();});
  expect(handler.listEmails).toHaveBeenCalledWith('A', 'A-inbox', 50n, 50n);
  await act(async () => {result.current.selectChild('B', 'B-inbox');});
  expect(result.current.emails).toEqual(page('B'));
  await act(async () => {release(response(page('late-A', 1))); await oldRequest;});
  expect(result.current.emails).toEqual(page('B'));
  expect(result.current.hasMore).toBe(true);
  expect(result.current.loading).toBe(false);
  await act(async () => {await result.current.loadMoreEmails();});
  expect(handler.listEmails).toHaveBeenLastCalledWith('B', 'B-inbox', 50n, 50n);
});
