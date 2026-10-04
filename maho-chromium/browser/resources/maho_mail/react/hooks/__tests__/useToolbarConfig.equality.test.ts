import { renderHook, act, waitFor } from "@testing-library/react";
import { describe, it, expect, vi, beforeEach } from "vitest";
import { configsEqual, useToolbarConfig } from "../useToolbarConfig";
import type { ActionId, ToolbarConfig } from "../../types/toolbar";
import * as api from "../../api";

vi.mock("../../api", () => ({
  getAppSetting: vi.fn().mockResolvedValue(null),
  setAppSetting: vi.fn().mockResolvedValue(undefined),
}));

describe("configsEqual shortcut comparison (DEFECT-03)", () => {
  it("returns false when incoming config b contains new shortcuts not present in a", () => {
    const current: ToolbarConfig = {
      primary: [],
      overflow: [],
      shortcuts: {} as Record<ActionId, string | null>,
    };
    const incoming: ToolbarConfig = {
      primary: [],
      overflow: [],
      shortcuts: { reply: "r" } as Record<ActionId, string | null>,
    };

    // Minimal failing scenario from verification.md:
    // a.shortcuts is empty, b.shortcuts contains { reply: "r" }
    // configsEqual must return false because key sets differ
    expect(configsEqual(current, incoming)).toBe(false);
  });

  it("is symmetric when one config contains extra shortcut keys", () => {
    const configA: ToolbarConfig = {
      primary: ["reply"],
      overflow: [],
      shortcuts: { reply: "r" } as Record<ActionId, string | null>,
    };
    const configB: ToolbarConfig = {
      primary: ["reply"],
      overflow: [],
      shortcuts: { reply: "r", forward: "f" } as Record<ActionId, string | null>,
    };

    // Comparing configA (1 key) to configB (2 keys) must return false
    expect(configsEqual(configA, configB)).toBe(false);
    // Comparing configB (2 keys) to configA (1 key) must also return false
    expect(configsEqual(configB, configA)).toBe(false);
  });

  it("returns false when configs have the same number of shortcuts but disjoint action keys", () => {
    const configA: ToolbarConfig = {
      primary: [],
      overflow: [],
      shortcuts: { reply: "r" } as Record<ActionId, string | null>,
    };
    const configB: ToolbarConfig = {
      primary: [],
      overflow: [],
      shortcuts: { forward: "r" } as Record<ActionId, string | null>,
    };

    expect(configsEqual(configA, configB)).toBe(false);
    expect(configsEqual(configB, configA)).toBe(false);
  });

  it("returns false when shortcuts have the same keys but different values", () => {
    const configA: ToolbarConfig = {
      primary: [],
      overflow: [],
      shortcuts: { reply: "r" } as Record<ActionId, string | null>,
    };
    const configB: ToolbarConfig = {
      primary: [],
      overflow: [],
      shortcuts: { reply: "x" } as Record<ActionId, string | null>,
    };

    expect(configsEqual(configA, configB)).toBe(false);
  });

  it("returns true when configs have identical primary, overflow, and shortcuts", () => {
    const configA: ToolbarConfig = {
      primary: ["reply", "forward"],
      overflow: ["delete"],
      shortcuts: {
        reply: "r",
        forward: "f",
        delete: "Delete",
      } as Record<ActionId, string | null>,
    };
    const configB: ToolbarConfig = {
      primary: ["reply", "forward"],
      overflow: ["delete"],
      shortcuts: {
        reply: "r",
        forward: "f",
        delete: "Delete",
      } as Record<ActionId, string | null>,
    };

    expect(configsEqual(configA, configB)).toBe(true);
  });

  it("returns false when primary or overflow action order or contents differ", () => {
    const base: ToolbarConfig = {
      primary: ["reply"],
      overflow: ["forward"],
      shortcuts: { reply: "r", forward: "f" } as Record<ActionId, string | null>,
    };

    expect(configsEqual(base, { ...base, primary: ["forward"] })).toBe(false);
    expect(configsEqual(base, { ...base, overflow: ["reply"] })).toBe(false);
    expect(configsEqual(base, { ...base, primary: ["reply", "star"] })).toBe(false);
  });
});

describe("useToolbarConfig hook equality integration (DEFECT-03)", () => {
  beforeEach(() => {
    localStorage.clear();
    vi.clearAllMocks();
  });

  it("updates hook config state when remote settings add new action shortcuts", async () => {
    const initialConfig: ToolbarConfig = {
      primary: ["reply"],
      overflow: ["forward"],
      shortcuts: { reply: "r" } as Record<ActionId, string | null>,
    };
    localStorage.setItem("maho-toolbar-config-cache", JSON.stringify(initialConfig));

    const remoteConfig: ToolbarConfig = {
      primary: ["reply"],
      overflow: ["forward"],
      shortcuts: { reply: "r", forward: "f" } as Record<ActionId, string | null>,
    };
    vi.mocked(api.getAppSetting).mockResolvedValue(JSON.stringify(remoteConfig));

    const { result } = renderHook(() => useToolbarConfig());

    await waitFor(() => {
      expect(result.current.config.shortcuts.forward).toBe("f");
    });
  });

  it("updates hook config state when storage event arrives with new action shortcuts", async () => {
    const initialConfig: ToolbarConfig = {
      primary: ["reply"],
      overflow: [],
      shortcuts: { reply: "r" } as Record<ActionId, string | null>,
    };
    localStorage.setItem("maho-toolbar-config-cache", JSON.stringify(initialConfig));
    vi.mocked(api.getAppSetting).mockResolvedValue(null);

    const { result } = renderHook(() => useToolbarConfig());

    expect(result.current.config.shortcuts.reply).toBe("r");

    const updatedConfig: ToolbarConfig = {
      primary: ["reply"],
      overflow: ["forward"],
      shortcuts: { reply: "r", forward: "f" } as Record<ActionId, string | null>,
    };
    localStorage.setItem("maho-toolbar-config-cache", JSON.stringify(updatedConfig));

    act(() => {
      window.dispatchEvent(new Event("maho-toolbar-config-changed"));
    });

    await waitFor(() => {
      expect(result.current.config.shortcuts.forward).toBe("f");
    });
  });
});
