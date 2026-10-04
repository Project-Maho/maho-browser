import { beforeEach, describe, expect, it, vi } from 'vitest';

const handler = vi.hoisted(() => ({
  exportPgpKey: vi.fn(), encryptEmailPgp: vi.fn(), decryptEmailPgp: vi.fn(),
  signEmailPgp: vi.fn(), encryptAttachmentPgp: vi.fn(), verifyEmailPgp: vi.fn(),
  importSmimeIdentity: vi.fn(), exportSmimeCert: vi.fn(), signEmailSmime: vi.fn(),
  encryptEmailSmime: vi.fn(), decryptEmailSmime: vi.fn(), verifyEmailSmime: vi.fn(),
}));
vi.mock('../mojo_client.js', () => ({ handler }));
import * as api from '../api/index';

// Narrow transport fixtures follow crypto_api.rs's Serialize/Deserialize structs.
// Payloads are opaque wire data, not generated keys/certificates or crypto mocks.
const text = 'wire payload\r\nUTF-8: \u00e9\u2603';
const account = 'account-wire';
const recipients = ['recipient@example.test'];
const json = (value: unknown) => JSON.stringify(value);
const reply = (value: unknown) => ({ ok: true, resultJson: json(value) });

beforeEach(() => vi.resetAllMocks());

const stringCases = [
  { name: 'exportPgpKey', run: () => api.exportPgpKey('key-wire', false), field: 'key_data', args: ['key-wire', false] },
  { name: 'encryptEmailPgp', run: () => api.encryptEmailPgp(account, recipients, text), field: 'armored', args: [json({ account_id: account, recipient_emails: recipients, plaintext: text })] },
  { name: 'decryptEmailPgp', run: () => api.decryptEmailPgp(account, text), field: 'plaintext', args: [json({ account_id: account, ciphertext: text })] },
  { name: 'signEmailPgp', run: () => api.signEmailPgp(account, text), field: 'armored', args: [json({ account_id: account, message: text })] },
  { name: 'exportSmimeCert', run: () => api.exportSmimeCert('identity-wire'), field: 'cert_pem', args: ['identity-wire'] },
  { name: 'signEmailSmime', run: () => api.signEmailSmime(account, text), field: 'smime', args: [json({ account_id: account, body: text })] },
  { name: 'encryptEmailSmime', run: () => api.encryptEmailSmime({ account_id: account, recipient_emails: recipients, recipient_certs_pem: [], body: text, body_html: '<p>private</p>', attachments: [{ filename: 'private.txt', mime_type: 'text/plain', data: 'cHJpdmF0ZQ==' }], sign: true }), field: 'smime', args: [json({ account_id: account, recipient_emails: recipients, recipient_certs_pem: [], body: text, body_html: '<p>private</p>', attachments: [{ filename: 'private.txt', mime_type: 'text/plain', data: 'cHJpdmF0ZQ==' }], sign: true })] },
  { name: 'decryptEmailSmime', run: () => api.decryptEmailSmime(account, text), field: 'decrypted', args: [json({ account_id: account, encrypted_body: text })] },
] as const;

describe('crypto FFI wire boundary', () => {
  it.each(stringCases)('$name unwraps the native response field', async ({ name, run, field, args }) => {
    handler[name].mockResolvedValue(reply({ [field]: text }));
    expect(await run()).toBe(text);
    expect(handler[name]).toHaveBeenCalledExactlyOnceWith(...args);
  });

  it.each(stringCases)('$name propagates native failure', async ({ name, run }) => {
    handler[name].mockResolvedValue({ ok: false, resultJson: 'crypto-wire-error' });
    await expect(run()).rejects.toThrow('crypto-wire-error');
  });

  it('encodes attachment bytes as standard base64 for the native parser', async () => {
    handler.encryptAttachmentPgp.mockResolvedValue(reply({ encrypted_data: 'AAECf4D/' }));
    await api.encryptAttachmentPgp(account, recipients, [0, 1, 2, 127, 128, 255]);
    expect(handler.encryptAttachmentPgp).toHaveBeenCalledExactlyOnceWith(json({
      account_id: account, recipient_emails: recipients, plaintext: 'AAECf4D/',
    }));
  });

  it('decodes native attachment base64 back to caller bytes', async () => {
    handler.encryptAttachmentPgp.mockResolvedValue(reply({ encrypted_data: 'AAECf4D/' }));
    expect(await api.encryptAttachmentPgp(account, recipients, [])).toEqual([0, 1, 2, 127, 128, 255]);
  });

  it('encodes PKCS12 bytes as standard base64 without changing identity metadata', async () => {
    const identity = { id: 'identity-wire', account_id: account, email: 'sender@example.test', subject: '', issuer: '', serial_number: '1', fingerprint: '', not_before: '', not_after: '', is_default: false, created_at: '' };
    handler.importSmimeIdentity.mockResolvedValue(reply(identity));
    expect(await api.importSmimeIdentity(account, [0, 1, 2, 127, 128, 255], 'password-wire')).toEqual(identity);
    expect(handler.importSmimeIdentity).toHaveBeenCalledExactlyOnceWith(json({ account_id: account, p12_data: 'AAECf4D/', password: 'password-wire' }));
  });

  it.each([true, false])('preserves PGP verification status %s and nullable metadata', async (valid) => {
    const result = { is_valid: valid, signer_email: null, fingerprint: null, error: valid ? null : 'invalid-signature' };
    handler.verifyEmailPgp.mockResolvedValue(reply(result));
    expect(await api.verifyEmailPgp('sender@example.test', text, 'signature-wire')).toEqual(result);
    expect(handler.verifyEmailPgp).toHaveBeenCalledExactlyOnceWith(json({ sender_email: 'sender@example.test', message: text, signature: 'signature-wire' }));
  });

  it.each([[true, true], [true, false], [false, false]])('preserves S/MIME valid=%s trusted=%s independently', async (valid, trusted) => {
    const result = { valid, trusted, signer_email: null, signer_subject: null };
    handler.verifyEmailSmime.mockResolvedValue(reply(result));
    expect(await api.verifyEmailSmime(text)).toEqual(result);
    expect(handler.verifyEmailSmime).toHaveBeenCalledExactlyOnceWith(text);
  });
});
