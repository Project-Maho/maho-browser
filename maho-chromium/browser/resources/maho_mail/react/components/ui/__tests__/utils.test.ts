import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { formatFileSize, formatRelativeDate, getInitials } from "../utils";

describe("formatFileSize", () => {
  it("formats bytes", () => {
    expect(formatFileSize(0)).toBe("0 B");
    expect(formatFileSize(512)).toBe("512 B");
  });

  it("formats KB", () => {
    expect(formatFileSize(1024)).toBe("1.0 KB");
    expect(formatFileSize(1536)).toBe("1.5 KB");
  });

  it("formats MB", () => {
    expect(formatFileSize(1024 * 1024)).toBe("1.0 MB");
    expect(formatFileSize(5 * 1024 * 1024 + 512 * 1024)).toBe("5.5 MB");
  });
});

describe("getInitials", () => {
  it("gets initials from full name", () => {
    expect(getInitials("John Doe")).toBe("JD");
  });

  it("gets initials from email", () => {
    expect(getInitials("john@example.com")).toBe("JE");
  });

  it("handles single word", () => {
    expect(getInitials("Madonna")).toBe("M");
  });
});

describe("formatRelativeDate", () => {
  beforeEach(() => {
    vi.useFakeTimers();
    vi.setSystemTime(new Date("2025-04-05T12:00:00.000Z"));
  });

  afterEach(() => {
    vi.useRealTimers();
  });

  it("formats recent dates consistently", () => {
    expect(formatRelativeDate("2025-04-05T11:59:30.000Z")).toBe("just now");
    expect(formatRelativeDate("2025-04-05T11:50:00.000Z")).toBe("10m ago");
    expect(formatRelativeDate("2025-04-05T09:00:00.000Z")).toBe("3h ago");
  });
});
