import { describe, it, expect, vi, beforeEach, afterEach } from "vitest";
import { renderHook, act } from "@testing-library/react";
import { useComposeDraft } from "../useComposeDraft";

const saveDraftMock = vi.fn().mockResolvedValue("draft-new-1");
const updateDraftMock = vi.fn().mockResolvedValue(undefined);

vi.mock("../../api", () => ({
  saveDraft: (...args: unknown[]) => saveDraftMock(...args),
  updateDraft: (...args: unknown[]) => updateDraftMock(...args),
}));

const mockToast = vi.fn();
vi.mock("../../components/ui/Toast", () => ({
  useToast: () => ({
    toast: mockToast,
  }),
  Toaster: () => null,
}));

function defaultProps() {
  return {
    isOpen: true,
    autoSaveDrafts: true,
    effectiveAccountId: "acc-1",
    subject: "Test subject",
    body: "Test body",
    bodyHtml: "<p>Test body</p>",
    to: "alice@example.com",
    cc: "",
    bcc: "",
    attachments: [] as [],
    requestReadReceipt: false,
  };
}

describe("useComposeDraft", () => {
  beforeEach(() => {
    vi.clearAllMocks();
    saveDraftMock.mockResolvedValue("draft-new-1");
    updateDraftMock.mockResolvedValue(undefined);
    vi.useFakeTimers();
  });

  afterEach(() => {
    vi.useRealTimers();
  });

  it("starts with isDirtyRef = false", () => {
    const { result } = renderHook(() => useComposeDraft(defaultProps()));

    expect(result.current.isDirtyRef.current).toBe(false);
  });

  it("starts with draftId = null", () => {
    const { result } = renderHook(() => useComposeDraft(defaultProps()));

    expect(result.current.draftId).toBeNull();
  });

  it("starts with draftSavedAt = null", () => {
    const { result } = renderHook(() => useComposeDraft(defaultProps()));

    expect(result.current.draftSavedAt).toBeNull();
  });

  it("auto-save fires after 30s when dirty and calls saveDraft when no draftId", async () => {
    const { result } = renderHook(() => useComposeDraft(defaultProps()));

    // Mark as dirty
    act(() => {
      result.current.isDirtyRef.current = true;
    });

    // Advance 30s
    await act(async () => {
      vi.advanceTimersByTime(30_000);
    });

    expect(saveDraftMock).toHaveBeenCalledTimes(1);
    expect(saveDraftMock).toHaveBeenCalledWith(
      expect.objectContaining({
        account_id: "acc-1",
        subject: "Test subject",
        body_text: "Test body",
        body_html: "<p>Test body</p>",
        to: ["alice@example.com"],
      }),
    );

    // After save, isDirtyRef should be reset
    expect(result.current.isDirtyRef.current).toBe(false);
    expect(result.current.draftId).toBe("draft-new-1");
    expect(result.current.draftSavedAt).not.toBeNull();
  });

  it("includes attachments and read_receipt in saveDraft / buildRequest", async () => {
    const mockAttachments = [
      { filename: "doc.pdf", mime_type: "application/pdf", data: "base64data" },
    ];
    const { result } = renderHook(() =>
      useComposeDraft({
        ...defaultProps(),
        attachments: mockAttachments,
        requestReadReceipt: true,
      }),
    );

    act(() => {
      result.current.isDirtyRef.current = true;
    });

    await act(async () => {
      vi.advanceTimersByTime(30_000);
    });

    expect(saveDraftMock).toHaveBeenCalledTimes(1);
    expect(saveDraftMock).toHaveBeenCalledWith(
      expect.objectContaining({
        account_id: "acc-1",
        to: ["alice@example.com"],
        attachments: mockAttachments,
        read_receipt: true,
      }),
    );
  });

  it("calls updateDraft when draftId already exists", async () => {
    const { result } = renderHook(() => useComposeDraft(defaultProps()));

    // Set an existing draft ID
    act(() => {
      result.current.setDraftId("existing-draft-42");
    });

    // Mark as dirty
    act(() => {
      result.current.isDirtyRef.current = true;
    });

    // Advance 30s
    await act(async () => {
      vi.advanceTimersByTime(30_000);
    });

    expect(updateDraftMock).toHaveBeenCalledTimes(1);
    expect(updateDraftMock).toHaveBeenCalledWith(
      "existing-draft-42",
      expect.objectContaining({ account_id: "acc-1" }),
    );
    expect(saveDraftMock).not.toHaveBeenCalled();
  });

  it("does not auto-save when not dirty", async () => {
    renderHook(() => useComposeDraft(defaultProps()));

    await act(async () => {
      vi.advanceTimersByTime(30_000);
    });

    expect(saveDraftMock).not.toHaveBeenCalled();
    expect(updateDraftMock).not.toHaveBeenCalled();
  });

  it("does not auto-save when autoSaveDrafts is false", async () => {
    const { result } = renderHook(() =>
      useComposeDraft({ ...defaultProps(), autoSaveDrafts: false }),
    );

    act(() => {
      result.current.isDirtyRef.current = true;
    });

    await act(async () => {
      vi.advanceTimersByTime(30_000);
    });

    expect(saveDraftMock).not.toHaveBeenCalled();
  });

  it("does not auto-save when modal is closed", async () => {
    const { result } = renderHook(() =>
      useComposeDraft({ ...defaultProps(), isOpen: false }),
    );

    act(() => {
      result.current.isDirtyRef.current = true;
    });

    await act(async () => {
      vi.advanceTimersByTime(30_000);
    });

    expect(saveDraftMock).not.toHaveBeenCalled();
  });

  it("P1-3 fix: keystroke during in-flight save re-sets dirty flag", async () => {
    // Simulate: save starts (isDirty=true), during save a keystroke sets isDirty again
    // The hook should NOT clear isDirty after save if it was re-set during the await
    let resolvePromise: (value: string) => void;
    saveDraftMock.mockImplementation(
      () => new Promise<string>((resolve) => { resolvePromise = resolve; }),
    );

    const { result } = renderHook(
      (props) => useComposeDraft(props),
      { initialProps: defaultProps() },
    );

    // Mark dirty
    act(() => {
      result.current.isDirtyRef.current = true;
    });

    // Trigger auto-save (starts the in-flight save)
    act(() => {
      vi.advanceTimersByTime(30_000);
    });

    // The save is now in-flight. isDirtyRef should have been flipped BEFORE the await.
    // (this is the P1-3 fix: flip flag before await so re-sets are not lost)
    expect(result.current.isDirtyRef.current).toBe(false);

    // Simulate keystroke during in-flight save: new content arrives, dirty flag re-set
    act(() => {
      result.current.isDirtyRef.current = true;
    });

    // Now resolve the save promise
    await act(async () => {
      resolvePromise!("draft-new-1");
    });

    // After save completes, isDirtyRef should STILL be true (keystroke happened during save)
    expect(result.current.isDirtyRef.current).toBe(true);
  });

  it("saveDraft manual trigger calls api.saveDraft when no draftId", async () => {
    const { result } = renderHook(() =>
      useComposeDraft({ ...defaultProps(), autoSaveDrafts: true }),
    );

    act(() => {
      result.current.isDirtyRef.current = true;
    });

    await act(async () => {
      vi.advanceTimersByTime(30_000);
    });

    expect(saveDraftMock).toHaveBeenCalledTimes(1);
    expect(result.current.draftId).toBe("draft-new-1");
    expect(result.current.draftSavedAt).not.toBeNull();
  });

  it("shows toast error and re-marks dirty on auto-save failure", async () => {
    saveDraftMock.mockRejectedValue(new Error("Database write locked"));

    const { result } = renderHook(() => useComposeDraft(defaultProps()));

    // Mark as dirty
    act(() => {
      result.current.isDirtyRef.current = true;
    });

    // Advance 30s
    await act(async () => {
      vi.advanceTimersByTime(30_000);
    });

    expect(saveDraftMock).toHaveBeenCalledTimes(1);
    expect(result.current.isDirtyRef.current).toBe(true); // Should remain dirty on failure
    expect(mockToast).toHaveBeenCalledWith("error", "Auto-save failed: Database write locked");
  });
});
