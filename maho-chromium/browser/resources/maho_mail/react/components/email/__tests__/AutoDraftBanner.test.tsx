import { act, fireEvent, render, screen, cleanup } from '@testing-library/react';
import { afterEach, expect, it, vi } from 'vitest';
import { AutoDraftBanner } from '../AutoDraftBanner';
import * as api from '../../../api';

vi.mock('../../../api', () => ({
  getAutoDraftForEmail: vi.fn(),
  updateAutoDraftStatus: vi.fn().mockResolvedValue(undefined),
}));
afterEach(() => { cleanup(); vi.clearAllMocks(); });

it('does not dispatch generation on mount or email change; dispatches only on click', async () => {
  vi.mocked(api.getAutoDraftForEmail).mockResolvedValue(null);
  const view = render(<AutoDraftBanner accountId="acc1" emailId="em1" onAccept={vi.fn()} />);
  await act(async () => {});
  expect(api.getAutoDraftForEmail).not.toHaveBeenCalled();
  view.rerender(<AutoDraftBanner accountId="acc2" emailId="em2" onAccept={vi.fn()} />);
  await act(async () => {});
  expect(api.getAutoDraftForEmail).not.toHaveBeenCalled();
  await act(async () => { fireEvent.click(screen.getByRole('button', { name: /generate/i })); });
  expect(api.getAutoDraftForEmail).toHaveBeenCalledExactlyOnceWith('acc2', 'em2');
});

it('accepts the cached draft returned by an intentional request', async () => {
  vi.mocked(api.getAutoDraftForEmail).mockResolvedValue({ id: 'draft1', email_id: 'em1', draft_content: 'cached reply', status: 'pending' });
  const accept = vi.fn();
  render(<AutoDraftBanner accountId="acc1" emailId="em1" onAccept={accept} />);
  await act(async () => { fireEvent.click(screen.getByRole('button', { name: /generate/i })); });
  fireEvent.click(screen.getByRole('button', { name: 'Accept' }));
  expect(accept).toHaveBeenCalledExactlyOnceWith('cached reply');
  expect(api.updateAutoDraftStatus).toHaveBeenCalledExactlyOnceWith('draft1', 'accepted');
});
