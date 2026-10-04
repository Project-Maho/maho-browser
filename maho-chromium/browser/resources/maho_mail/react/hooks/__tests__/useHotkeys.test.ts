import { renderHook, act, fireEvent } from "@testing-library/react";
import { describe, it, expect, vi, beforeEach } from "vitest";
import { useHotkeys } from "../useHotkeys";

describe("useHotkeys", () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  it("matches ctrl or meta shortcuts and ignores them while typing", () => {
    const handler = vi.fn();
    renderHook(() =>
      useHotkeys([
        { key: "k", ctrl: true, handler },
      ]),
    );

    act(() => {
      fireEvent.keyDown(document, { key: "k", code: "KeyK", ctrlKey: true });
    });
    expect(handler).toHaveBeenCalledTimes(1);

    const input = document.createElement("input");
    document.body.appendChild(input);

    act(() => {
      fireEvent.keyDown(input, { key: "k", code: "KeyK", ctrlKey: true });
    });

    expect(handler).toHaveBeenCalledTimes(1);
    document.body.removeChild(input);
  });

  it("matches shifted printable keys by code", () => {
    const handler = vi.fn();
    renderHook(() =>
      useHotkeys([
        { key: "?", handler },
      ]),
    );

    act(() => {
      fireEvent.keyDown(document, { key: "/", code: "Slash", shiftKey: true });
    });

    expect(handler).toHaveBeenCalledTimes(1);
  });

  it("matches alt modifier shortcuts", () => {
    const handler = vi.fn();
    renderHook(() =>
      useHotkeys([
        { key: "s", alt: true, handler },
      ]),
    );

    act(() => {
      fireEvent.keyDown(document, { key: "s", code: "KeyS", altKey: true });
    });

    expect(handler).toHaveBeenCalledTimes(1);
  });

  it("skips disabled hotkeys and still allows Escape in text fields", () => {
    const disabledHandler = vi.fn();
    const escapeHandler = vi.fn();
    renderHook(() =>
      useHotkeys([
        { key: "x", handler: disabledHandler, disabled: true },
        { key: "Escape", handler: escapeHandler },
      ]),
    );

    act(() => {
      fireEvent.keyDown(document, { key: "x", code: "KeyX" });
    });
    expect(disabledHandler).not.toHaveBeenCalled();

    const textarea = document.createElement("textarea");
    document.body.appendChild(textarea);

    act(() => {
      fireEvent.keyDown(textarea, { key: "Escape", code: "Escape" });
    });

    expect(escapeHandler).toHaveBeenCalledTimes(1);
    document.body.removeChild(textarea);
  });
});
