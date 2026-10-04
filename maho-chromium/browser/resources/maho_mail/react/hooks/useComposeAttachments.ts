import { useState, useEffect, useRef, useCallback, type MutableRefObject } from "react";
import type { ComposeAttachment } from "../types";

export const MAX_FILE_SIZE = 25 * 1024 * 1024;
export const MAX_TOTAL_SIZE = 50 * 1024 * 1024;
export const MAX_FILE_COUNT = 10;

export interface UseComposeAttachmentsParams {
  isOpen: boolean;
  prevIsOpenRef: MutableRefObject<boolean>;
  t: (key: string, params?: Record<string, unknown>) => string;
  toast: (type: string, message: string) => void;
  /** @internal Test-only override for file limits */
  _limits?: { maxFileSize: number; maxTotalSize: number; maxFileCount: number };
}

export interface UseComposeAttachmentsReturn {
  attachments: ComposeAttachment[];
  setAttachments: React.Dispatch<React.SetStateAction<ComposeAttachment[]>>;
  isDragActive: boolean;
  isReading: boolean;
  readingRef: MutableRefObject<boolean>;
  handleFileSelected: (files: FileList | null) => Promise<void>;
  removeAttachment: (filename: string) => void;
  clearAttachments: () => void;
  handleDragEnter: (e: React.DragEvent<HTMLDivElement>) => void;
  handleDragOver: (e: React.DragEvent<HTMLDivElement>) => void;
  handleDragLeave: (e: React.DragEvent<HTMLDivElement>) => void;
  handleDrop: (e: React.DragEvent<HTMLDivElement>) => void;
}

function hasDraggedFiles(dataTransfer: DataTransfer | null): boolean {
  return Array.from(dataTransfer?.types ?? []).includes("Files");
}

function getMimeType(filename: string): string {
  const ext = filename.split(".").pop()?.toLowerCase() ?? "";
  const mimeMap: Record<string, string> = {
    pdf: "application/pdf",
    doc: "application/msword",
    docx: "application/vnd.openxmlformats-officedocument.wordprocessingml.document",
    xls: "application/vnd.ms-excel",
    xlsx: "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet",
    ppt: "application/vnd.ms-powerpoint",
    pptx: "application/vnd.openxmlformats-officedocument.presentationml.presentation",
    png: "image/png",
    jpg: "image/jpeg",
    jpeg: "image/jpeg",
    gif: "image/gif",
    svg: "image/svg+xml",
    webp: "image/webp",
    txt: "text/plain",
    csv: "text/csv",
    html: "text/html",
    zip: "application/zip",
    gz: "application/gzip",
    mp3: "audio/mpeg",
    mp4: "video/mp4",
  };
  return mimeMap[ext] ?? "application/octet-stream";
}

function readFileAsBase64(file: File): Promise<string> {
  return new Promise<string>((resolve, reject) => {
    const reader = new FileReader();

    reader.onload = () => {
      if (typeof reader.result !== "string") {
        reject(new Error(`Failed to read "${file.name}"`));
        return;
      }
      const encoded = reader.result.split(",")[1];
      if (encoded === undefined) {
        reject(new Error(`Failed to encode "${file.name}"`));
        return;
      }
      resolve(encoded);
    };

    reader.onerror = () => {
      reject(new Error(`Failed to read "${file.name}"`));
    };

    reader.readAsDataURL(file);
  });
}

export function useComposeAttachments({
  isOpen,
  prevIsOpenRef,
  t,
  toast,
  _limits,
}: UseComposeAttachmentsParams): UseComposeAttachmentsReturn {
  const maxFileSize = _limits?.maxFileSize ?? MAX_FILE_SIZE;
  const maxTotalSize = _limits?.maxTotalSize ?? MAX_TOTAL_SIZE;
  const maxFileCount = _limits?.maxFileCount ?? MAX_FILE_COUNT;

  const [attachments, setAttachments] = useState<ComposeAttachment[]>([]);
  const [isDragActive, setIsDragActive] = useState(false);
  const [isReading, setIsReading] = useState(false);
  const readingRef = useRef(false);
  const dragDepthRef = useRef(0);
  const attachmentsRef = useRef<ComposeAttachment[]>(attachments);

  // Keep ref synced with state for use in async callbacks
  useEffect(() => {
    attachmentsRef.current = attachments;
  }, [attachments]);

  // Clear attachments on first-open transition (reset between sessions)
  useEffect(() => {
    if (isOpen && !prevIsOpenRef.current) {
      setAttachments([]);
      setIsDragActive(false);
      dragDepthRef.current = 0;
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [isOpen]);

  const clearAttachments = useCallback(() => {
    setAttachments([]);
    setIsDragActive(false);
    dragDepthRef.current = 0;
  }, []);

  const removeAttachment = useCallback((filename: string) => {
    setAttachments((prev) => {
      const idx = prev.findIndex((a) => a.filename === filename);
      if (idx === -1) return prev;
      return prev.filter((_, i) => i !== idx);
    });
  }, []);

  const handleFileSelected = useCallback(
    async (files: FileList | null) => {
      if (!files || files.length === 0) return;
      if (readingRef.current) {
        toast("error", "Please wait for attachments to finish loading.");
        return;
      }

      const incoming = Array.from(files);
      const current = attachmentsRef.current;

      if (current.length + incoming.length > maxFileCount) {
        toast("error", t("compose.attachmentTooMany", { max: maxFileCount }));
        return;
      }

      let currentTotal = current.reduce(
        (sum, a) => sum + Math.ceil((a.data.length * 3) / 4),
        0,
      );

      const acceptedFiles: File[] = [];

      for (const file of incoming) {
        if (file.size > maxFileSize) {
          toast("error", t("compose.exceedsFileSize", { name: file.name }));
          continue;
        }

        if (currentTotal + file.size > maxTotalSize) {
          toast("error", t("compose.attachmentTotalTooLarge", { max: 50 }));
          continue;
        }

        currentTotal += file.size;
        acceptedFiles.push(file);
      }

      if (acceptedFiles.length === 0) return;

      readingRef.current = true;
      setIsReading(true);
      try {
      const newAttachments = await Promise.all(
        acceptedFiles.map(async (file) => {
          const data = await readFileAsBase64(file);
          return {
            filename: file.name,
            mime_type: file.type || getMimeType(file.name),
            data,
          };
        }),
      );

      attachmentsRef.current = [...attachmentsRef.current, ...newAttachments];
      setAttachments(attachmentsRef.current);
      } catch (error) {
        toast("error", error instanceof Error ? error.message : "Failed to read attachment");
      } finally {
        readingRef.current = false;
        setIsReading(false);
      }
    },
    [t, toast, maxFileSize, maxTotalSize, maxFileCount],
  );

  const handleDragEnter = useCallback(
    (e: React.DragEvent<HTMLDivElement>) => {
      if (!hasDraggedFiles(e.dataTransfer)) return;
      e.preventDefault();
      e.stopPropagation();
      dragDepthRef.current += 1;
      setIsDragActive(true);
    },
    [],
  );

  const handleDragOver = useCallback(
    (e: React.DragEvent<HTMLDivElement>) => {
      if (!hasDraggedFiles(e.dataTransfer)) return;
      e.preventDefault();
      e.stopPropagation();
      e.dataTransfer.dropEffect = "copy";
      setIsDragActive(true);
    },
    [],
  );

  const handleDragLeave = useCallback(
    (e: React.DragEvent<HTMLDivElement>) => {
      if (!hasDraggedFiles(e.dataTransfer)) return;
      e.preventDefault();
      e.stopPropagation();
      dragDepthRef.current = Math.max(0, dragDepthRef.current - 1);
      if (dragDepthRef.current === 0) {
        setIsDragActive(false);
      }
    },
    [],
  );

  const handleDrop = useCallback(
    (e: React.DragEvent<HTMLDivElement>) => {
      if (!hasDraggedFiles(e.dataTransfer)) return;
      e.preventDefault();
      e.stopPropagation();
      dragDepthRef.current = 0;
      setIsDragActive(false);

      const droppedFiles = e.dataTransfer.files;
      if (!droppedFiles || droppedFiles.length === 0) return;

      void handleFileSelected(droppedFiles);
    },
    [handleFileSelected],
  );

  return {
    attachments,
    setAttachments,
    isDragActive,
    isReading,
    readingRef,
    handleFileSelected,
    removeAttachment,
    clearAttachments,
    handleDragEnter,
    handleDragOver,
    handleDragLeave,
    handleDrop,
  };
}
