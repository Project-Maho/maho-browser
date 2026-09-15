import { renderHook, act, fireEvent } from "@testing-library/react";
import { describe, it, expect, vi, beforeEach } from "vitest";
import { useHotkeys } from "../useHotkeys";

describe("useHotkeys meta modifier (DEFECT-02)", () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  it("fires hotkey handler when meta: true is specified and metaKey is pressed (macOS Cmd shortcut)", () => {
    const mockHandler = vi.fn();
    renderHook(() =>
      useHotkeys([
        { key: "k", meta: true, handler: mockHandler },
      ]),
    );

    act(() => {
      fireEvent.keyDown(document, { key: "k", code: "KeyK", metaKey: true, ctrlKey: false });
    });

    expect(mockHandler).toHaveBeenCalledTimes(1);
  });

  it("does not fire meta: true hotkey when only ctrlKey is pressed", () => {
    const mockHandler = vi.fn();
    renderHook(() =>
      useHotkeys([
        { key: "k", meta: true, handler: mockHandler },
      ]),
    );

    act(() => {
      fireEvent.keyDown(document, { key: "k", code: "KeyK", metaKey: false, ctrlKey: true });
    });

    expect(mockHandler).not.toHaveBeenCalled();
  });

  it("does not fire meta: true hotkey when neither modifier is pressed", () => {
    const mockHandler = vi.fn();
    renderHook(() =>
      useHotkeys([
        { key: "k", meta: true, handler: mockHandler },
      ]),
    );

    act(() => {
      fireEvent.keyDown(document, { key: "k", code: "KeyK", metaKey: false, ctrlKey: false });
    });

    expect(mockHandler).not.toHaveBeenCalled();
  });

  it("requires both modifiers when both ctrl: true and meta: true are specified", () => {
    const mockHandler = vi.fn();
    renderHook(() =>
      useHotkeys([
        { key: "k", ctrl: true, meta: true, handler: mockHandler },
      ]),
    );

    act(() => {
      fireEvent.keyDown(document, { key: "k", code: "KeyK", metaKey: true, ctrlKey: false });
    });
    expect(mockHandler).not.toHaveBeenCalled();

    act(() => {
      fireEvent.keyDown(document, { key: "k", code: "KeyK", metaKey: false, ctrlKey: true });
    });
    expect(mockHandler).not.toHaveBeenCalled();

    act(() => {
      fireEvent.keyDown(document, { key: "k", code: "KeyK", metaKey: true, ctrlKey: true });
    });
    expect(mockHandler).toHaveBeenCalledTimes(1);
  });

  it("does not fire plain key hotkey when metaKey is pressed", () => {
    const mockHandler = vi.fn();
    renderHook(() =>
      useHotkeys([
        { key: "c", handler: mockHandler },
      ]),
    );

    act(() => {
      fireEvent.keyDown(document, { key: "c", code: "KeyC", metaKey: true });
    });

    expect(mockHandler).not.toHaveBeenCalled();
  });
});
