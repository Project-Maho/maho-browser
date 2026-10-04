import {act, fireEvent, render, screen} from "@testing-library/react";
import {beforeEach, expect, it, vi} from "vitest";
import * as api from "../../../api";
import {TestProviders} from "../../../test/mocks";
import {ComposeForm} from "../ComposeForm";

vi.mock("../../../api", () => ({
  listSignatures: vi.fn().mockResolvedValue([]),
  listTemplates: vi.fn().mockResolvedValue([]),
  sendEmail: vi.fn(),
  signEmailSmime: vi.fn(),
  encryptEmailSmime: vi.fn(),
  encryptEmailPgp: vi.fn(),
}));
vi.mock("../../ui/Toast", () => ({useToast: () => ({toast: vi.fn()})}));
vi.mock("../../../hooks/usePopoutWindow", () => ({emitMailUpdated: vi.fn().mockResolvedValue(undefined)}));

beforeEach(() => {
  vi.clearAllMocks();
  // JSDOM has no layout; ProseMirror uses these Range APIs when focusing replies.
  Range.prototype.getClientRects = () => [] as unknown as DOMRectList;
  Range.prototype.getBoundingClientRect = () => new DOMRect();
  vi.mocked(api.sendEmail).mockResolvedValue({status: "sent"});
  vi.mocked(api.signEmailSmime).mockResolvedValue("signed-wire");
  vi.mocked(api.encryptEmailPgp).mockResolvedValue("pgp-wire");
  vi.mocked(api.encryptEmailSmime).mockResolvedValue("encrypted-smime-wire");
});

it.each(['PGP', 'S/MIME', 'S/MIME Sign'])('reapplies %s after a snapshot is remounted', async (mode) => {
  const accepted = vi.fn();
  let view: ReturnType<typeof render>;
  await act(async () => {
    view = render(<ComposeForm accountId="account" accounts={[]} onClose={vi.fn()}
      initialTo="recipient@example.invalid" initialBody="private" onSendRequested={accepted} />, {wrapper: TestProviders});
  });
  fireEvent.click(screen.getByRole('button', { name: mode, exact: true }));
  await send();
  const snapshot = accepted.mock.calls[0][1].composition;
  view!.unmount();
  await act(async () => {
    render(<ComposeForm accountId="account" accounts={[]} onClose={vi.fn()}
      initialTo={snapshot.to} initialBody={snapshot.body}
      initialPgpEncrypt={snapshot.pgpEncrypt} initialSmimeSign={snapshot.smimeSign}
      initialSmimeEncrypt={snapshot.smimeEncrypt} />, {wrapper: TestProviders});
  });
  await send();
  expect(vi.mocked(api.sendEmail).mock.calls[0][0].body_text).toBe(
    mode === 'PGP' ? 'pgp-wire' : mode === 'S/MIME' ? 'encrypted-smime-wire' : 'signed-wire');
});

it('restores a complete reply body without reintroducing its removed quote', async () => {
  const replyTo = { original_subject: 'Thread', original_from: 'sender@example.invalid', original_date: '2026-09-14T10:00:00Z', original_body_text: 'old quote', original_body_html: '<p>old quote</p>', original_message_id: '<thread>' };
  await act(async () => {
    render(<ComposeForm accountId="account" accounts={[]} onClose={vi.fn()}
      initialTo="recipient@example.invalid" initialBody="edited reply" initialBodyHtml="<p>edited reply</p>"
      initialBodyIsComplete replyTo={replyTo} />, {wrapper: TestProviders});
  });
  await send();
  expect(vi.mocked(api.sendEmail).mock.calls[0][0]).toMatchObject({ body_text: 'edited reply', body_html: '<p>edited reply</p>', in_reply_to: '<thread>' });
});

it('sends threading metadata restored from a saved draft', async () => {
  await act(async () => {
    render(<ComposeForm accountId="account" accounts={[]} onClose={vi.fn()}
      initialTo="recipient@example.invalid" initialBody="reply" initialInReplyTo="<parent>"
      initialReferences="<root> <parent>" />, {wrapper: TestProviders});
  });
  await send();
  expect(vi.mocked(api.sendEmail).mock.calls[0][0]).toMatchObject({ in_reply_to: '<parent>', references: '<root> <parent>' });
});

it('keeps the editor open until the shell has persisted the send', async () => {
  const pending = Promise.withResolvers<void>();
  const accepted = vi.fn(() => pending.promise);
  const onClose = vi.fn();
  await act(async () => {
    render(<ComposeForm accountId="account" accounts={[]} onClose={onClose}
      initialTo="recipient@example.invalid" initialBody="private" onSendRequested={accepted} />, {wrapper: TestProviders});
  });
  await send();
  expect(accepted).toHaveBeenCalledOnce();
  expect(onClose).not.toHaveBeenCalled();
  await act(async () => { pending.resolve(); await pending.promise; });
  expect(onClose).toHaveBeenCalledOnce();
});

it('keeps the editor open when pending-send persistence fails', async () => {
  const onClose = vi.fn();
  await act(async () => {
    render(<ComposeForm accountId="account" accounts={[]} onClose={onClose}
      initialTo="recipient@example.invalid" initialBody="private"
      onSendRequested={async () => { throw new Error('draft storage failed'); }} />, {wrapper: TestProviders});
  });
  await send();
  expect(onClose).not.toHaveBeenCalled();
  expect(api.sendEmail).not.toHaveBeenCalled();
  expect(screen.getByText('draft storage failed')).toBeInTheDocument();
});

async function compose() {
  await act(async () => {
    render(<ComposeForm accountId="account" accounts={[]} onClose={vi.fn()}
      initialTo="recipient@example.invalid" initialBody="private" />, {wrapper: TestProviders});
  });
}

async function send() {
  await act(async () => { fireEvent.click(screen.getByRole("button", {name: "Send", exact: true})); });
}

it("preserves the existing S/MIME sign-only path", async () => {
  await compose();
  fireEvent.click(screen.getByRole("button", {name: "S/MIME Sign", exact: true}));
  await send();

  expect(api.signEmailSmime).toHaveBeenCalledExactlyOnceWith("account", "private");
  expect(api.encryptEmailSmime).not.toHaveBeenCalled();
  expect(api.encryptEmailPgp).not.toHaveBeenCalled();
  expect(api.sendEmail).toHaveBeenCalledOnce();
  expect(vi.mocked(api.sendEmail).mock.calls[0]?.[0].body_text).toBe("signed-wire");
});

it("preserves the existing PGP encryption path", async () => {
  await compose();
  fireEvent.click(screen.getByRole("button", {name: "PGP", exact: true}));
  await send();

  expect(api.encryptEmailPgp).toHaveBeenCalledExactlyOnceWith("account", ["recipient@example.invalid"], "private");
  expect(api.signEmailSmime).not.toHaveBeenCalled();
  expect(api.encryptEmailSmime).not.toHaveBeenCalled();
  expect(api.sendEmail).toHaveBeenCalledOnce();
  expect(vi.mocked(api.sendEmail).mock.calls[0]?.[0].body_text).toBe("pgp-wire");
});

it("does not dispatch when the sign-only native operation fails", async () => {
  vi.mocked(api.signEmailSmime).mockRejectedValueOnce(new Error("fixture signing denied"));
  await compose();
  fireEvent.click(screen.getByRole("button", {name: "S/MIME Sign", exact: true}));
  await send();

  expect(api.signEmailSmime).toHaveBeenCalledOnce();
  expect(api.sendEmail).not.toHaveBeenCalled();
});

it("keeps signing, S/MIME encryption and PGP mutually exclusive", async () => {
  await compose();
  const signed = screen.getByRole("button", {name: "S/MIME Sign", exact: true});
  const smime = screen.getByRole("button", {name: "S/MIME", exact: true});
  const pgp = screen.getByRole("button", {name: "PGP", exact: true});
  fireEvent.click(signed);
  expect(signed).toHaveAttribute("aria-pressed", "true");
  fireEvent.click(smime);
  expect(signed).toHaveAttribute("aria-pressed", "false");
  expect(smime).toHaveAttribute("aria-pressed", "true");
  fireEvent.click(pgp);
  expect(pgp).toHaveAttribute("aria-pressed", "true");
  expect(smime).toHaveAttribute("aria-pressed", "false");
  expect(signed).toHaveAttribute("aria-pressed", "false");
});
