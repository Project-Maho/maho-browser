import { describe, it, expect, vi, beforeEach } from "vitest";
import { renderHook, act } from "@testing-library/react";
import { useComposeDraft } from "../useComposeDraft";
import * as api from "../../api";

vi.mock("../../api", () => ({
  saveDraft: vi.fn(),
  updateDraft: vi.fn(),
}));

vi.mock("../../components/ui/Toast", () => ({
  useToast: () => ({ toast: vi.fn() }),
  Toaster: () => null,
}));

describe("useComposeDraft regression", () => {
  beforeEach(() => {
    vi.clearAllMocks();
    vi.mocked(api.saveDraft).mockResolvedValue("draft-123");
    vi.mocked(api.updateDraft).mockResolvedValue(undefined);
  });

  it("C08 parses quoted comma-containing recipient into single mailbox without splitting", async () => {
    const { result } = renderHook(() =>
      useComposeDraft({
        isOpen: true,
        autoSaveDrafts: false,
        effectiveAccountId: "acc-1",
        subject: "Subj",
        body: "Body",
        bodyHtml: "<p>Body</p>",
        to: '"Doe, Jane" <jane@example.com>',
        cc: "",
        bcc: "",
        attachments: [],
        requestReadReceipt: false,
      })
    );

    await act(async () => {
      await result.current.saveDraft();
    });

    expect(api.saveDraft).toHaveBeenCalledWith(
      expect.objectContaining({
        to: ['"Doe, Jane" <jane@example.com>'],
      })
    );
  });

  it("keeps bcc, body_html, attachments, read_receipt, in_reply_to, and references in draft request", async () => {
    const attachments = [{ filename: "doc.txt", mime_type: "text/plain", data: "eA==" }];
    const { result } = renderHook(() =>
      useComposeDraft({
        isOpen: true,
        autoSaveDrafts: false,
        effectiveAccountId: "acc-1",
        subject: "Subj",
        body: "Body",
        bodyHtml: "<p>Body</p>",
        to: "alice@example.com",
        cc: "bob@example.com",
        bcc: "carol@example.com",
        attachments,
        requestReadReceipt: true,
        inReplyTo: "<msg1@example.com>",
        references: "<root@example.com> <msg1@example.com>",
      })
    );

    await act(async () => {
      await result.current.saveDraft();
    });

    expect(api.saveDraft).toHaveBeenCalledWith(
      expect.objectContaining({
        account_id: "acc-1",
        to: ["alice@example.com"],
        cc: ["bob@example.com"],
        bcc: ["carol@example.com"],
        subject: "Subj",
        body_text: "Body",
        body_html: "<p>Body</p>",
        attachments,
        read_receipt: true,
        in_reply_to: "<msg1@example.com>",
        references: "<root@example.com> <msg1@example.com>",
      })
    );
  });

  it("C06/C05 gates draft saving while attachment reading is in flight", async () => {
    const { result } = renderHook(() =>
      useComposeDraft({
        isOpen: true,
        autoSaveDrafts: false,
        effectiveAccountId: "acc-1",
        subject: "Subj",
        body: "Body",
        bodyHtml: "<p>Body</p>",
        to: "alice@example.com",
        cc: "",
        bcc: "",
        attachments: [],
        requestReadReceipt: false,
        isReadingAttachments: true,
      })
    );

    await act(async () => {
      await result.current.saveDraft();
    });

    expect(api.saveDraft).not.toHaveBeenCalled();
    expect(api.updateDraft).not.toHaveBeenCalled();
  });

  it("prevents concurrent saves while a save is in flight", async () => {
    let resolveSave!: (id: string) => void;
    vi.mocked(api.saveDraft).mockImplementation(
      () => new Promise((resolve) => { resolveSave = resolve; })
    );

    const { result } = renderHook(() =>
      useComposeDraft({
        isOpen: true,
        autoSaveDrafts: false,
        effectiveAccountId: "acc-1",
        subject: "Subj",
        body: "Body",
        bodyHtml: "<p>Body</p>",
        to: "alice@example.com",
        cc: "",
        bcc: "",
        attachments: [],
        requestReadReceipt: false,
      })
    );

    let firstDone = false;
    let p1: Promise<void>;
    act(() => {
      p1 = result.current.saveDraft().then(() => { firstDone = true; });
    });

    await act(async () => {
      await result.current.saveDraft();
    });

    expect(api.saveDraft).toHaveBeenCalledTimes(1);

    await act(async () => {
      resolveSave("draft-123");
      await p1;
    });
    expect(firstDone).toBe(true);
  });

  it("captures acknowledged snapshot and does not drop later edits made during save", async () => {
    let resolveSave!: (id: string) => void;
    vi.mocked(api.saveDraft).mockImplementation(
      () => new Promise((resolve) => { resolveSave = resolve; })
    );
    const onDraftSaved = vi.fn();

    let currentBody = "Initial Body";
    const { result, rerender } = renderHook(
      (props: { body: string }) =>
        useComposeDraft({
          isOpen: true,
          autoSaveDrafts: false,
          effectiveAccountId: "acc-1",
          subject: "Subj",
          body: props.body,
          bodyHtml: `<p>${props.body}</p>`,
          to: "alice@example.com",
          cc: "",
          bcc: "",
          attachments: [],
          requestReadReceipt: false,
          onDraftSaved,
        }),
      { initialProps: { body: currentBody } }
    );

    act(() => {
      result.current.isDirtyRef.current = true;
    });

    let savePromise: Promise<void>;
    act(() => {
      savePromise = result.current.saveDraft();
    });

    currentBody = "Body with edits during save";
    act(() => {
      rerender({ body: currentBody });
    });

    await act(async () => {
      resolveSave("draft-1");
      await savePromise;
    });

    expect(onDraftSaved).toHaveBeenCalledWith(
      expect.objectContaining({
        draftId: "draft-1",
        body: "Initial Body",
      })
    );

    expect(result.current.isDirtyRef.current).toBe(true);
  });
});
