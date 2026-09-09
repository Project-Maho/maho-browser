import { describe, it, expect, vi, beforeEach } from "vitest";
import { renderHook, act } from "@testing-library/react";
import { useOptimisticMutation } from "../useOptimisticMutation";

const mockToast = vi.fn();
vi.mock("../../components/ui/Toast", () => ({
  useToast: () => ({
    toast: mockToast,
  }),
  Toaster: () => null,
}));

describe("useOptimisticMutation", () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  it("applies optimistic update immediately, then executes mutate successfully", async () => {
    const mutate = vi.fn().mockResolvedValue("success-data");
    const applyOptimistic = vi.fn();
    const rollback = vi.fn();

    const { result } = renderHook(() =>
      useOptimisticMutation({
        mutate,
        applyOptimistic,
        rollback,
      })
    );

    let data;
    await act(async () => {
      data = await result.current.execute("param-val");
    });

    expect(applyOptimistic).toHaveBeenCalledWith("param-val");
    expect(mutate).toHaveBeenCalledWith("param-val");
    expect(rollback).not.toHaveBeenCalled();
    expect(data).toBe("success-data");
  });

  it("settles cleanly without rethrowing on mutation failure for fire-and-forget callers", async () => {
    const error = new Error("Network offline");
    const mutate = vi.fn().mockRejectedValue(error);
    const applyOptimistic = vi.fn();
    const rollback = vi.fn();

    const unhandledRejections: unknown[] = [];
    const handleUnhandledRejection = (reason: unknown) => {
      unhandledRejections.push(reason);
    };
    process.on("unhandledRejection", handleUnhandledRejection);

    try {
      const { result } = renderHook(() =>
        useOptimisticMutation({
          mutate,
          applyOptimistic,
          rollback,
        })
      );

      let executionResult: unknown;
      await act(async () => {
        executionResult = await result.current.execute("param-val");
      });

      expect(executionResult).toBeUndefined();
      expect(unhandledRejections).toHaveLength(0);
      expect(result.current.error).toBe(error);
      expect(result.current.isLoading).toBe(false);
      expect(applyOptimistic).toHaveBeenCalledWith("param-val");
      expect(mutate).toHaveBeenCalledWith("param-val");
      expect(rollback).toHaveBeenCalledWith("param-val", error);
      expect(mockToast).toHaveBeenCalledWith("error", "Action failed: Network offline");
    } finally {
      process.off("unhandledRejection", handleUnhandledRejection);
    }
  });

  it("rolls back state and displays toast on mutation failure", async () => {
    const error = new Error("Network offline");
    const mutate = vi.fn().mockRejectedValue(error);
    const applyOptimistic = vi.fn();
    const rollback = vi.fn();

    const { result } = renderHook(() =>
      useOptimisticMutation({
        mutate,
        applyOptimistic,
        rollback,
      })
    );

    await act(async () => {
      const res = await result.current.execute("param-val");
      expect(res).toBeUndefined();
    });

    expect(result.current.error).toBe(error);
    expect(applyOptimistic).toHaveBeenCalledWith("param-val");
    expect(mutate).toHaveBeenCalledWith("param-val");
    expect(rollback).toHaveBeenCalledWith("param-val", error);
    expect(mockToast).toHaveBeenCalledWith("error", "Action failed: Network offline");
  });

  it("invokes onSuccess with the data and variables after a successful mutation", async () => {
    const mutate = vi.fn().mockResolvedValue({ id: "x" });
    const applyOptimistic = vi.fn();
    const rollback = vi.fn();
    const onSuccess = vi.fn();

    const { result } = renderHook(() =>
      useOptimisticMutation({
        mutate,
        applyOptimistic,
        rollback,
        onSuccess,
      })
    );

    await act(async () => {
      await result.current.execute("variable-1");
    });

    expect(onSuccess).toHaveBeenCalledTimes(1);
    expect(onSuccess).toHaveBeenCalledWith({ id: "x" }, "variable-1");
    expect(rollback).not.toHaveBeenCalled();
  });

  it("does not invoke onSuccess when the mutation fails", async () => {
    const mutate = vi.fn().mockRejectedValue(new Error("boom"));
    const applyOptimistic = vi.fn();
    const rollback = vi.fn();
    const onSuccess = vi.fn();

    const { result } = renderHook(() =>
      useOptimisticMutation({
        mutate,
        applyOptimistic,
        rollback,
        onSuccess,
      })
    );

    await act(async () => {
      const res = await result.current.execute("v");
      expect(res).toBeUndefined();
    });

    expect(result.current.error).toEqual(new Error("boom"));
    expect(onSuccess).not.toHaveBeenCalled();
    expect(rollback).toHaveBeenCalled();
  });
});
