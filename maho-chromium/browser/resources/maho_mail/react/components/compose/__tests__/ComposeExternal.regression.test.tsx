import { act, fireEvent, render, screen } from "@testing-library/react";
import { expect, it, vi } from "vitest";
import { ComposeForm } from "../ComposeForm";
import { TestProviders } from "../../../test/mocks";
import * as api from "../../../api";
import { toast } from "../../ui/Toast";
vi.mock("../../ui/Toast", () => {
 const toast = vi.fn();
 return { toast, useToast: () => ({ toast }) };
});
vi.mock("../../../api", () => ({
 listSignatures: vi.fn().mockResolvedValue([]), listTemplates: vi.fn().mockResolvedValue([]),
 deleteEmail: vi.fn().mockResolvedValue(undefined),
 // Faithful native default (crypto_api.rs): no resolvable recipient certificate -> validation_err,
 // never a fabricated success envelope. Individual cases override with their own behavior.
 encryptEmailSmime: vi.fn().mockRejectedValue(new Error("No usable S/MIME certificate for recipient@example.com")),
 signEmailSmime: vi.fn(),
 sendEmail: vi.fn().mockResolvedValue({ status: "sent" }),
 queueEmail: vi.fn(),
}));
vi.mock("../../../hooks/usePopoutWindow", () => ({ emitMailUpdated: vi.fn().mockResolvedValue(undefined) }));
it("C02 keeps existing draft until the shell acknowledges transport success", async () => {
 await act(async () => { render(<ComposeForm accountId="a" accounts={[]} onClose={vi.fn()} onSendRequested={vi.fn()} initialDraftId="draft" initialTo="r@example.com" initialBody="recover me" />, { wrapper: TestProviders }); });
 await act(async () => { fireEvent.click(screen.getByRole("button", { name: "Send", exact: true })); });
 expect(api.deleteEmail).not.toHaveBeenCalled();
});
it("C07 does not encrypt to sender alone when recipient certificate is unavailable", async () => {
 vi.mocked(api.sendEmail).mockClear();
 vi.mocked(api.encryptEmailSmime).mockClear().mockRejectedValueOnce(new Error("No usable recipient certificate"));
 await act(async () => { render(<ComposeForm accountId="a" accounts={[]} onClose={vi.fn()} initialTo="recipient@example.com" initialBody="private" />, { wrapper: TestProviders }); });
 fireEvent.click(screen.getByRole("button", { name: "S/MIME", exact: true }));
 await act(async () => { fireEvent.click(screen.getByRole("button", { name: "Send", exact: true })); });
 expect(api.sendEmail).not.toHaveBeenCalled();
 expect(api.encryptEmailSmime).toHaveBeenCalledOnce();
});

it("C07 forwards all recipients and complete content without plaintext leakage", async () => {
 vi.clearAllMocks();
 vi.mocked(api.encryptEmailSmime).mockResolvedValueOnce("protected-mime-entity");
 const attachments = [{ filename: "private.txt", mime_type: "text/plain", data: "cHJpdmF0ZQ==" }];
 await act(async () => { render(<ComposeForm accountId="a" accounts={[]} onClose={vi.fn()}
   initialTo="to@example.com" initialCc="cc@example.com" initialBcc="bcc@example.com"
   initialBody="private" initialBodyHtml="<p>private</p>" initialAttachments={attachments}
 />, { wrapper: TestProviders }); });
 fireEvent.click(screen.getByRole("button", { name: "S/MIME", exact: true }));

 await act(async () => { fireEvent.click(screen.getByRole("button", { name: "Send", exact: true })); });

 expect(api.encryptEmailSmime).toHaveBeenCalledExactlyOnceWith({
   account_id: "a", recipient_emails: ["to@example.com", "cc@example.com", "bcc@example.com"],
   recipient_certs_pem: [], body: "private", body_html: "<p>private</p>", attachments, sign: false,
 });
 expect(api.signEmailSmime).not.toHaveBeenCalled();
 expect(api.sendEmail).toHaveBeenCalledOnce();
 const request = vi.mocked(api.sendEmail).mock.calls[0][0];
 expect(request.body_text).toBe("protected-mime-entity");
 expect(request.body_html).toBeUndefined();
 expect(request.attachments).toBeUndefined();
});

it.each([
 ["sent", "success"], ["queued", "info"], ["uncertain", "warning"],
] as const)("uses truthful severity for %s without duplicate submission", async (status, severity) => {
 const close = vi.fn();
 vi.mocked(api.sendEmail).mockResolvedValueOnce({ status });
 await act(async () => { render(<ComposeForm accountId="a" accounts={[]} onClose={close}
  initialTo="recipient@example.com" initialSubject="subject" initialBody="body" />, { wrapper: TestProviders }); });
 await act(async () => { fireEvent.click(screen.getByRole("button", { name: "Send", exact: true })); });
 expect(api.sendEmail).toHaveBeenCalledOnce();
 expect(api.queueEmail).not.toHaveBeenCalled();
 expect(close).toHaveBeenCalledOnce();
 expect(vi.mocked(toast).mock.calls.map(([kind]) => kind)).toEqual([severity]);
});
