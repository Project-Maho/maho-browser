import { describe, it, expect, vi, beforeEach } from "vitest";
import { renderHook, act } from "@testing-library/react";
import { useComposeSecurity } from "../useComposeSecurity";

function makeParams(isOpen = false, prevIsOpenCurrent = false) {
  const prevIsOpenRef = { current: prevIsOpenCurrent };
  return { isOpen, prevIsOpenRef: prevIsOpenRef as React.MutableRefObject<boolean> };
}

describe("useComposeSecurity", () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  it("initial state is all false", () => {
    const { result } = renderHook(() => useComposeSecurity(makeParams()));

    expect(result.current.pgpEncrypt).toBe(false);
    expect(result.current.smimeSign).toBe(false);
    expect(result.current.smimeEncrypt).toBe(false);
    expect(result.current.requestReadReceipt).toBe(false);
  });

  it("togglePgpEncrypt(true) clears smimeSign and smimeEncrypt", () => {
    const { result } = renderHook(() => useComposeSecurity(makeParams()));

    // First enable smimeSign and smimeEncrypt won't both be true due to mutex,
    // but let's set pgp and verify it clears the smime flags
    act(() => { result.current.toggleSmimeSign(true); });
    expect(result.current.smimeSign).toBe(true);

    act(() => { result.current.togglePgpEncrypt(true); });
    expect(result.current.pgpEncrypt).toBe(true);
    expect(result.current.smimeSign).toBe(false);
    expect(result.current.smimeEncrypt).toBe(false);
  });

  it("toggleSmimeSign(true) clears pgpEncrypt and smimeEncrypt", () => {
    const { result } = renderHook(() => useComposeSecurity(makeParams()));

    act(() => { result.current.togglePgpEncrypt(true); });
    expect(result.current.pgpEncrypt).toBe(true);

    act(() => { result.current.toggleSmimeSign(true); });
    expect(result.current.smimeSign).toBe(true);
    expect(result.current.pgpEncrypt).toBe(false);
    expect(result.current.smimeEncrypt).toBe(false);
  });

  it("toggleSmimeEncrypt(true) clears pgpEncrypt and smimeSign", () => {
    const { result } = renderHook(() => useComposeSecurity(makeParams()));

    act(() => { result.current.togglePgpEncrypt(true); });
    expect(result.current.pgpEncrypt).toBe(true);

    act(() => { result.current.toggleSmimeEncrypt(true); });
    expect(result.current.smimeEncrypt).toBe(true);
    expect(result.current.pgpEncrypt).toBe(false);
    expect(result.current.smimeSign).toBe(false);
  });

  it("toggling false does not clear others", () => {
    const { result } = renderHook(() => useComposeSecurity(makeParams()));

    // Enable pgp
    act(() => { result.current.togglePgpEncrypt(true); });
    expect(result.current.pgpEncrypt).toBe(true);

    // Toggle smimeSign OFF — should not touch pgpEncrypt
    act(() => { result.current.toggleSmimeSign(false); });
    expect(result.current.pgpEncrypt).toBe(true);
    expect(result.current.smimeSign).toBe(false);

    // Toggle smimeEncrypt OFF — should not touch pgpEncrypt
    act(() => { result.current.toggleSmimeEncrypt(false); });
    expect(result.current.pgpEncrypt).toBe(true);
    expect(result.current.smimeEncrypt).toBe(false);
  });

  it("setRequestReadReceipt is independent (no mutex)", () => {
    const { result } = renderHook(() => useComposeSecurity(makeParams()));

    act(() => { result.current.togglePgpEncrypt(true); });
    act(() => { result.current.setRequestReadReceipt(true); });

    expect(result.current.requestReadReceipt).toBe(true);
    expect(result.current.pgpEncrypt).toBe(true);
  });

  it("first-open transition resets all toggles to false", () => {
    const prevIsOpenRef = { current: false };
    const { result, rerender } = renderHook(
      ({ isOpen }) => useComposeSecurity({ isOpen, prevIsOpenRef: prevIsOpenRef as React.MutableRefObject<boolean> }),
      { initialProps: { isOpen: true } },
    );

    // Enable something
    act(() => { result.current.togglePgpEncrypt(true); });
    act(() => { result.current.setRequestReadReceipt(true); });
    expect(result.current.pgpEncrypt).toBe(true);
    expect(result.current.requestReadReceipt).toBe(true);

    // Simulate close
    rerender({ isOpen: false });

    // Simulate re-open (prevIsOpenRef.current will be false because hook resets it)
    rerender({ isOpen: true });

    expect(result.current.pgpEncrypt).toBe(false);
    expect(result.current.smimeSign).toBe(false);
    expect(result.current.smimeEncrypt).toBe(false);
    expect(result.current.requestReadReceipt).toBe(false);
  });

  it("P0-4: smimeSign and smimeEncrypt cannot both be true simultaneously", () => {
    const { result } = renderHook(() => useComposeSecurity(makeParams()));

    // Enable sign
    act(() => { result.current.toggleSmimeSign(true); });
    expect(result.current.smimeSign).toBe(true);
    expect(result.current.smimeEncrypt).toBe(false);

    // Enable encrypt — should clear sign
    act(() => { result.current.toggleSmimeEncrypt(true); });
    expect(result.current.smimeEncrypt).toBe(true);
    expect(result.current.smimeSign).toBe(false);

    // Reverse: enable sign again — should clear encrypt
    act(() => { result.current.toggleSmimeSign(true); });
    expect(result.current.smimeSign).toBe(true);
    expect(result.current.smimeEncrypt).toBe(false);
  });
});
