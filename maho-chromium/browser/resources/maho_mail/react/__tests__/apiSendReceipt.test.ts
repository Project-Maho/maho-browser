import { describe, expect, it, vi } from 'vitest';
import { sendEmail } from '../api';

const { send } = vi.hoisted(() => ({ send: vi.fn() }));
vi.mock('../mojo_client.js', () => ({ handler: { sendEmail: send } }));

const request = { account_id: 'account', to: ['recipient@example.com'], subject: 'subject', body_text: 'body' };

describe('send receipt boundary', () => {
  it.each(['sent', 'queued', 'uncertain'])('accepts the %s status', async (status) => {
    send.mockResolvedValueOnce({ ok: true, resultJson: JSON.stringify({ status }) });
    expect(await sendEmail(request)).toEqual({ status });
    expect(send).toHaveBeenCalledOnce();
    expect(JSON.parse(send.mock.calls[0][0])).toEqual(request);
  });

  it.each([{}, null, { status: 'unknown' }, [], 'sent'])('rejects malformed receipt %j', async (receipt) => {
    send.mockResolvedValueOnce({ ok: true, resultJson: JSON.stringify(receipt) });
    await expect(sendEmail(request)).rejects.toBeInstanceOf(Error);
  });

  it('preserves backend rejection', async () => {
    send.mockResolvedValueOnce({ ok: false, resultJson: 'rejected' });
    await expect(sendEmail(request)).rejects.toBeInstanceOf(Error);
  });
});
