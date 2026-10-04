import { act, renderHook } from "@testing-library/react";
import { afterEach, expect, it, vi } from "vitest";
import { useComposeAttachments } from "../useComposeAttachments";
const readers: ControlledReader[] = [];
class ControlledReader {
  result: string | null = null;
  onload: (() => void) | null = null;
  onerror: (() => void) | null = null;
  readAsDataURL() { readers.push(this); }
}
afterEach(() => { vi.unstubAllGlobals(); readers.length = 0; });
function setup() {
  vi.stubGlobal("FileReader", ControlledReader);
  const toast = vi.fn();
  return { ...renderHook(() => useComposeAttachments({ isOpen: true, prevIsOpenRef: { current: false }, t: k => k, toast })), toast };
}
it("C06 tracks pending reads and accepts empty files", async () => {
  const { result } = setup();
  let completion!: Promise<void>;
  act(() => { completion = result.current.handleFileSelected([new File([], "empty.txt")] as unknown as FileList); });
  expect(result.current).toHaveProperty("isReading", true);
  await act(async () => { readers[0].result = "data:text/plain;base64,"; readers[0].onload?.(); await completion; });
  expect(result.current.attachments).toEqual([{ filename: "empty.txt", mime_type: "text/plain", data: "" }]);
  expect(result.current).toHaveProperty("isReading", false);
});
it("C06 reports read failure without rejecting the discarded picker promise", async () => {
  const { result, toast } = setup();
  let completion!: Promise<void>;
  act(() => { completion = result.current.handleFileSelected([new File(["x"], "broken.txt")] as unknown as FileList); });
  await act(async () => { readers[0].onerror?.(); await expect(completion).resolves.toBeUndefined(); });
  expect(toast).toHaveBeenCalledWith("error", expect.any(String));
  expect(result.current.attachments).toEqual([]);
});
