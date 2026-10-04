import { act, fireEvent, render, screen } from "@testing-library/react";
import { beforeEach, expect, it, vi } from "vitest";
import { ComposeForm } from "../ComposeForm";
import { TestProviders } from "../../../test/mocks";
import * as api from "../../../api";
vi.mock("../../../api", () => ({
 listSignatures: vi.fn().mockResolvedValue([]), listTemplates: vi.fn().mockResolvedValue([]),
 searchContacts: vi.fn().mockResolvedValue([]), searchContactGroups: vi.fn().mockResolvedValue([]),
 adjustTone: vi.fn().mockResolvedValue({ adjusted_text: "REWRITTEN" }),
 getForwardedAttachments: vi.fn().mockResolvedValue([{ filename: "f.txt", mime_type: "text/plain", data: "eA==" }]),
 sendEmail: vi.fn().mockResolvedValue(undefined), scheduleSend: vi.fn().mockResolvedValue(undefined),
 saveDraft: vi.fn().mockResolvedValue("d"), updateDraft: vi.fn().mockResolvedValue(undefined), deleteEmail: vi.fn().mockResolvedValue(undefined),
 encryptEmailPgp: vi.fn().mockResolvedValue("CIPHER"), signEmailSmime: vi.fn().mockResolvedValue("SIGNED"), encryptEmailSmime: vi.fn().mockResolvedValue("SMIME"),
}));
vi.mock("../../../hooks/usePopoutWindow", () => ({ emitMailUpdated: vi.fn().mockResolvedValue(undefined) }));
beforeEach(() => vi.clearAllMocks());
async function setup(props = {}) {
 const onClose = vi.fn();
 await act(async () => { render(<ComposeForm accountId="a" accounts={[]} onClose={onClose} initialTo="r@example.com" initialBody="secret" initialBodyHtml="<p><strong>secret</strong></p>" {...props} />, { wrapper: TestProviders }); });
 return onClose;
}
it("C12 rewrites only authored text and restores exact HTML on Undo", async () => {
 const signatureText = "\n\n--\nSignature";
 const quoteText = "\n\nOn yesterday, sender wrote:\n> quote";
 const signatureHtml = '<br><br><div data-maho-signature="true">Signature</div>';
 const quoteHtml = '<br><br><p>On yesterday, sender wrote:</p><blockquote>quote</blockquote>';
 const originalHtml = '<p><strong>author</strong></p>' + signatureHtml + quoteHtml;
 await setup({ initialBody: "author" + signatureText + quoteText, initialBodyHtml: originalHtml });
 fireEvent.click(screen.getByRole("button", { name: "Rewrite message" }));
 await act(async () => { fireEvent.click(screen.getByRole("button", { name: "professional" })); });
 expect(api.adjustTone).toHaveBeenCalledWith({ text: "author", tone: "professional" });
 await act(async () => { fireEvent.click(screen.getByRole("button", { name: "Send", exact: true })); });
 expect(api.sendEmail).toHaveBeenLastCalledWith(expect.objectContaining({ body_text: "REWRITTEN" + signatureText + quoteText, body_html: "REWRITTEN" + signatureHtml + quoteHtml }));
 fireEvent.click(screen.getByRole("button", { name: "Undo Tone" }));
 await act(async () => { fireEvent.click(screen.getByRole("button", { name: "Send", exact: true })); });
 expect(api.sendEmail).toHaveBeenLastCalledWith(expect.objectContaining({ body_html: originalHtml }));
});
it("C06 pending file read blocks send until exact FileReader completion", async () => {
 let reader!: FileReader;
 const read = vi.spyOn(FileReader.prototype, "readAsDataURL").mockImplementation(function(this: FileReader) { reader = this; });
 try {
 await setup();
 const input = document.querySelector('input[type="file"][multiple]')!;
 fireEvent.change(input, { target: { files: [new File(["x"], "pending.txt", { type: "text/plain" })] } });
 fireEvent.click(screen.getByRole("button", { name: "Send", exact: true }));
 expect(api.sendEmail).not.toHaveBeenCalled();
 await act(async () => { Object.defineProperty(reader, "result", { value: "data:text/plain;base64,eA==" }); reader.dispatchEvent(new ProgressEvent("load")); });
 await act(async () => { fireEvent.click(screen.getByRole("button", { name: "Send", exact: true })); });
 expect(api.sendEmail).toHaveBeenCalledWith(expect.objectContaining({ attachments: [{ filename: "pending.txt", mime_type: "text/plain", data: "eA==" }] }));
 } finally { read.mockRestore(); }
});
it("C04 loads source attachments before forwarding", async () => {
 await setup({ forwardEmailUid: 9, forwardFolderId: "inbox" });
 await act(async () => { fireEvent.click(screen.getByRole("button", { name: "Send", exact: true })); });
 expect(api.getForwardedAttachments).toHaveBeenCalledWith("a", 9, "inbox");
 expect(api.sendEmail).toHaveBeenCalledWith(expect.objectContaining({ attachments: [{ filename: "f.txt", mime_type: "text/plain", data: "eA==" }] }));
});
it("C03 retains hydrated draft attachments", async () => {
 const attachments = [{ filename: "draft.txt", mime_type: "text/plain", data: "eA==" }];
 await setup({ initialAttachments: attachments, initialDraftId: "draft" });
 await act(async () => { fireEvent.click(screen.getByRole("button", { name: "Save Draft" })); });
 expect(api.updateDraft).toHaveBeenCalledWith("draft", expect.objectContaining({ attachments }));
});
it("C11 converts HTML immediately when switching to plain text", async () => {
 await setup();
 await act(async () => { fireEvent.click(screen.getByRole("button", { name: "Plain Text" })); });
 await act(async () => { fireEvent.click(screen.getByRole("button", { name: "Switch" })); });
 await act(async () => { fireEvent.click(screen.getByRole("button", { name: "Send", exact: true })); });
 expect(api.sendEmail).toHaveBeenCalledWith(expect.objectContaining({ body_html: "secret" }));
});
it("C14 latches scheduling before transport resolves", async () => {
 let resolve!: () => void;
 vi.mocked(api.scheduleSend).mockImplementationOnce(() => new Promise<void>(r => { resolve = r; }));
 await setup();
 fireEvent.click(screen.getByRole("button", { name: "Schedule", exact: true }));
 const option = screen.getByRole("button", { name: "In 1 hour" });
 await act(async () => { fireEvent.click(option); fireEvent.click(option); });
 expect(api.scheduleSend).toHaveBeenCalledTimes(1);
 await act(async () => resolve());
});
it.each([['S/MIME Sign', 'SIGNED'], ['S/MIME', 'SMIME']])("C01 schedule applies selected %s transform", async (mode, body) => {
 await setup();
 fireEvent.click(screen.getByRole("button", { name: mode, exact: true }));
 fireEvent.click(screen.getByRole("button", { name: "Schedule", exact: true }));
 await act(async () => { fireEvent.click(screen.getByRole("button", { name: "In 1 hour" })); });
 expect(api.scheduleSend).toHaveBeenCalledWith(expect.objectContaining({ body_text: body }));
});
it("C01 crypto failure blocks scheduling and C14 allows retry", async () => {
 vi.mocked(api.encryptEmailPgp).mockRejectedValueOnce(new Error("crypto unavailable"));
 await setup();
 fireEvent.click(screen.getByRole("button", { name: "PGP", exact: true }));
 fireEvent.click(screen.getByRole("button", { name: "Schedule", exact: true }));
 await act(async () => { fireEvent.click(screen.getByRole("button", { name: "In 1 hour" })); });
 expect(api.scheduleSend).not.toHaveBeenCalled();
 await act(async () => { fireEvent.click(screen.getByRole("button", { name: "In 1 hour" })); });
 expect(api.scheduleSend).toHaveBeenCalledTimes(1);
});
it("C01 applies PGP before scheduling", async () => {
 await setup();
 fireEvent.click(screen.getByRole("button", { name: "PGP", exact: true }));
 fireEvent.click(screen.getByRole("button", { name: "Schedule", exact: true }));
 await act(async () => { fireEvent.click(screen.getByRole("button", { name: "In 1 hour" })); });
 expect(api.scheduleSend).toHaveBeenCalledWith(expect.objectContaining({ body_text: "CIPHER" }));
});
it("C05 removing a restored attachment marks the draft dirty", async () => {
 await setup({ initialBody: "", initialBodyHtml: "", initialTo: "", initialAttachments: [{ filename: "draft.txt", mime_type: "text/plain", data: "eA==" }] });
 fireEvent.click(screen.getByRole("listitem").querySelector("button")!);
 const event = new Event("beforeunload", { cancelable: true });
 window.dispatchEvent(event);
 expect(event.defaultPrevented).toBe(true);
});
it("C05 uncommitted recipient input protects unload", async () => {
 await setup({ initialBody: "", initialBodyHtml: "", initialTo: "" });
 fireEvent.change(screen.getByRole("combobox"), { target: { value: "unfinished" } });
 const event = new Event("beforeunload", { cancelable: true });
 window.dispatchEvent(event);
 expect(event.defaultPrevented).toBe(true);
});
it("C05 receipt-only edits protect unload", async () => {
 await setup({ initialBody: "", initialBodyHtml: "", initialTo: "" });
 fireEvent.click(screen.getByRole("checkbox"));
 const event = new Event("beforeunload", { cancelable: true });
 window.dispatchEvent(event);
 expect(event.defaultPrevented).toBe(true);
});
it("C09 Escape closes schedule rather than composition", async () => {
 const close = await setup({ initialBody: "", initialBodyHtml: "", initialTo: "" });
 fireEvent.click(screen.getByRole("button", { name: "Schedule", exact: true }));
 fireEvent.keyDown(document, { key: "Escape" });
 expect(screen.queryByRole("button", { name: "In 1 hour" })).toBeNull();
 expect(close).not.toHaveBeenCalled();
});
