import { renderHook } from "@testing-library/react";
import { describe, it, expect, beforeEach, afterEach } from "vitest";
import { useTrayBadge } from "../useTrayBadge";

describe("useTrayBadge", () => {
  beforeEach(() => {
    document.title = "Maho Mail";
  });

  afterEach(() => {
    document.title = "Maho Mail";
  });

  it("updates the document title when there are unread messages", () => {
    const { unmount } = renderHook(() => useTrayBadge(3));

    expect(document.title).toBe("(3) Maho Mail");

    unmount();
  });

  it("keeps the default title when unread count is zero", () => {
    renderHook(() => useTrayBadge(0));

    expect(document.title).toBe("Maho Mail");
  });

  it("restores the default title when the hook unmounts", () => {
    const { unmount } = renderHook(() => useTrayBadge(8));

    expect(document.title).toBe("(8) Maho Mail");

    unmount();

    expect(document.title).toBe("Maho Mail");
  });
});
