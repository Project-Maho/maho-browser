import { describe, it, expect, vi, beforeEach } from "vitest";
import { renderHook, act } from "@testing-library/react";
import { useComposeAttachments, MAX_FILE_SIZE, MAX_TOTAL_SIZE, MAX_FILE_COUNT } from "../useComposeAttachments";
import type { MutableRefObject } from "react";

const TEST_LIMITS = { maxFileSize: 1024, maxTotalSize: 2048, maxFileCount: 3 };

function makeParams(overrides: Partial<{
  isOpen: boolean;
  prevIsOpenRef: MutableRefObject<boolean>;
  t: (key: string, params?: Record<string, unknown>) => string;
  toast: (type: string, message: string) => void;
  _limits: { maxFileSize: number; maxTotalSize: number; maxFileCount: number };
}> = {}) {
  return {
    isOpen: true,
    prevIsOpenRef: { current: false } as MutableRefObject<boolean>,
    t: (key: string, _params?: Record<string, unknown>) => key,
    toast: vi.fn() as (type: string, message: string) => void,
    _limits: TEST_LIMITS,
    ...overrides,
  };
}

function createMockFile(name: string, size: number, type = "text/plain"): File {
  const content = new Uint8Array(Math.min(size, 64));
  const file = new File([content], name, { type });
  if (size > 64) {
    Object.defineProperty(file, "size", { value: size, writable: false });
  }
  return file;
}

function createFileList(files: File[]): FileList {
  const list = Object.assign(files, {
    item: (i: number) => files[i] ?? null,
  });
  return list as unknown as FileList;
}

describe("useComposeAttachments", () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  it("exports correct constant values", () => {
    expect(MAX_FILE_SIZE).toBe(25 * 1024 * 1024);
    expect(MAX_TOTAL_SIZE).toBe(50 * 1024 * 1024);
    expect(MAX_FILE_COUNT).toBe(10);
  });

  it("initial state is empty attachments and isDragActive false", () => {
    const params = makeParams();
    const { result } = renderHook(() => useComposeAttachments(params));

    expect(result.current.attachments).toEqual([]);
    expect(result.current.isDragActive).toBe(false);
  });

  it("handleFileSelected adds files within limits", async () => {
    const params = makeParams();
    const { result } = renderHook(() => useComposeAttachments(params));

    const files = createFileList([createMockFile("doc.txt", 100, "text/plain")]);

    await act(async () => {
      await result.current.handleFileSelected(files);
    });

    expect(result.current.attachments).toHaveLength(1);
    expect(result.current.attachments[0].filename).toBe("doc.txt");
    expect(result.current.attachments[0].mime_type).toBe("text/plain");
    expect(result.current.attachments[0].data).toBeTruthy();
  });

  it("handleFileSelected rejects file exceeding max file size with toast", async () => {
    const toast = vi.fn();
    const params = makeParams({ toast });
    const { result } = renderHook(() => useComposeAttachments(params));

    const bigFile = createMockFile("huge.bin", TEST_LIMITS.maxFileSize + 1);
    const files = createFileList([bigFile]);

    await act(async () => {
      await result.current.handleFileSelected(files);
    });

    expect(result.current.attachments).toHaveLength(0);
    expect(toast).toHaveBeenCalledWith("error", expect.stringContaining("compose.exceedsFileSize"));
  });

  it("handleFileSelected rejects when total size exceeds limit", async () => {
    const toast = vi.fn();
    const params = makeParams({ toast });
    const { result } = renderHook(() => useComposeAttachments(params));

    // Add a file that fits individually (800 < 1024) and under total (800 < 2048)
    const file1 = createMockFile("a.bin", 800);
    await act(async () => {
      await result.current.handleFileSelected(createFileList([file1]));
    });

    expect(result.current.attachments).toHaveLength(1);

    // Seed stored data to simulate realistic stored size for total calculation
    // decoded bytes = data.length * 3/4. We want decoded ~1500 so file2 (800) pushes total > 2048
    const dataLen = Math.ceil(1500 * 4 / 3);
    act(() => {
      result.current.setAttachments([
        { filename: "a.bin", mime_type: "text/plain", data: "A".repeat(dataLen) },
      ]);
    });

    // Second file (800 bytes): currentTotal(1500) + 800 = 2300 > 2048
    const file2 = createMockFile("b.bin", 800);
    await act(async () => {
      await result.current.handleFileSelected(createFileList([file2]));
    });

    expect(result.current.attachments).toHaveLength(1);
    expect(toast).toHaveBeenCalledWith("error", expect.stringContaining("compose.attachmentTotalTooLarge"));
  });

  it("handleFileSelected rejects when count exceeds limit", async () => {
    const toast = vi.fn();
    const params = makeParams({ toast });
    const { result } = renderHook(() => useComposeAttachments(params));

    for (let i = 0; i < TEST_LIMITS.maxFileCount; i++) {
      await act(async () => {
        await result.current.handleFileSelected(
          createFileList([createMockFile(`file${i}.txt`, 10)]),
        );
      });
    }

    expect(result.current.attachments).toHaveLength(TEST_LIMITS.maxFileCount);

    await act(async () => {
      await result.current.handleFileSelected(
        createFileList([createMockFile("overflow.txt", 10)]),
      );
    });

    expect(result.current.attachments).toHaveLength(TEST_LIMITS.maxFileCount);
    expect(toast).toHaveBeenCalledWith("error", expect.stringContaining("compose.attachmentTooMany"));
  });

  it("removeAttachment removes by filename", async () => {
    const params = makeParams();
    const { result } = renderHook(() => useComposeAttachments(params));

    await act(async () => {
      await result.current.handleFileSelected(
        createFileList([
          createMockFile("a.txt", 10),
          createMockFile("b.txt", 10),
        ]),
      );
    });

    expect(result.current.attachments).toHaveLength(2);

    act(() => {
      result.current.removeAttachment("a.txt");
    });

    expect(result.current.attachments).toHaveLength(1);
    expect(result.current.attachments[0].filename).toBe("b.txt");
  });

  it("clearAttachments empties the array", async () => {
    const params = makeParams();
    const { result } = renderHook(() => useComposeAttachments(params));

    await act(async () => {
      await result.current.handleFileSelected(
        createFileList([createMockFile("a.txt", 10)]),
      );
    });

    expect(result.current.attachments).toHaveLength(1);

    act(() => {
      result.current.clearAttachments();
    });

    expect(result.current.attachments).toHaveLength(0);
  });

  it("handleDragEnter sets isDragActive true", () => {
    const params = makeParams();
    const { result } = renderHook(() => useComposeAttachments(params));

    act(() => {
      result.current.handleDragEnter({
        preventDefault: vi.fn(),
        stopPropagation: vi.fn(),
        dataTransfer: { types: ["Files"] },
      } as unknown as React.DragEvent<HTMLDivElement>);
    });

    expect(result.current.isDragActive).toBe(true);
  });

  it("handleDragLeave sets isDragActive false when depth reaches 0", () => {
    const params = makeParams();
    const { result } = renderHook(() => useComposeAttachments(params));

    act(() => {
      result.current.handleDragEnter({
        preventDefault: vi.fn(),
        stopPropagation: vi.fn(),
        dataTransfer: { types: ["Files"] },
      } as unknown as React.DragEvent<HTMLDivElement>);
    });

    expect(result.current.isDragActive).toBe(true);

    act(() => {
      result.current.handleDragLeave({
        preventDefault: vi.fn(),
        stopPropagation: vi.fn(),
        dataTransfer: { types: ["Files"] },
      } as unknown as React.DragEvent<HTMLDivElement>);
    });

    expect(result.current.isDragActive).toBe(false);
  });

  it("handleDrop calls handleFileSelected and clears drag state", async () => {
    const params = makeParams();
    const { result } = renderHook(() => useComposeAttachments(params));

    act(() => {
      result.current.handleDragEnter({
        preventDefault: vi.fn(),
        stopPropagation: vi.fn(),
        dataTransfer: { types: ["Files"] },
      } as unknown as React.DragEvent<HTMLDivElement>);
    });

    expect(result.current.isDragActive).toBe(true);

    const files = createFileList([createMockFile("dropped.txt", 50)]);

    await act(async () => {
      result.current.handleDrop({
        preventDefault: vi.fn(),
        stopPropagation: vi.fn(),
        dataTransfer: { types: ["Files"], files },
      } as unknown as React.DragEvent<HTMLDivElement>);
      await new Promise((r) => setTimeout(r, 50));
    });

    expect(result.current.isDragActive).toBe(false);
    expect(result.current.attachments).toHaveLength(1);
    expect(result.current.attachments[0].filename).toBe("dropped.txt");
  });

  it("first-open transition clears prior attachments", () => {
    const prevIsOpenRef = { current: true } as MutableRefObject<boolean>;
    const params = makeParams({ isOpen: false, prevIsOpenRef });
    const { result, rerender } = renderHook(
      (props) => useComposeAttachments(props),
      { initialProps: params },
    );

    act(() => {
      result.current.setAttachments([
        { filename: "leftover.txt", mime_type: "text/plain", data: "YQ==" },
      ]);
    });

    expect(result.current.attachments).toHaveLength(1);

    prevIsOpenRef.current = false;
    rerender({ ...params, isOpen: true, prevIsOpenRef });

    expect(result.current.attachments).toHaveLength(0);
    expect(result.current.isDragActive).toBe(false);
  });
});
