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
  const FIXED_NOW = "2025-04-05T12:00:00.000Z";

  beforeEach(() => {
    vi.useFakeTimers();
    vi.setSystemTime(new Date(FIXED_NOW));
  });

  afterEach(() => {
    vi.useRealTimers();
  });

  it("formats recent dates consistently", () => {
    expect(formatRelativeDate("2025-04-05T11:59:30.000Z")).toBe("just now");
    expect(formatRelativeDate("2025-04-05T11:50:00.000Z")).toBe("10m ago");
    expect(formatRelativeDate("2025-04-05T09:00:00.000Z")).toBe("3h ago");
  });

  it("formats day-level relative dates consistently", () => {
    expect(formatRelativeDate("2025-04-04T10:00:00.000Z")).toBe("yesterday");
    expect(formatRelativeDate("2025-04-02T10:00:00.000Z")).toBe("3d ago");
    const olderDate = new Date("2025-03-20T10:00:00.000Z");
    expect(formatRelativeDate("2025-03-20T10:00:00.000Z")).toBe(
      olderDate.toLocaleDateString("en-US", { month: "short", day: "numeric" })
    );
  });

  it("does not format future dates as 'just now'", () => {
    const futureDateStr = "2025-04-05T13:00:00.000Z";
    const result = formatRelativeDate(futureDateStr);
    expect(result).not.toBe("just now");
    expect(result).toBe(
      new Date(futureDateStr).toLocaleDateString("en-US", {
        month: "short",
        day: "numeric",
      })
    );

    const nearFutureDateStr = "2025-04-05T12:01:00.000Z";
    const nearResult = formatRelativeDate(nearFutureDateStr);
    expect(nearResult).not.toBe("just now");
    expect(nearResult).toBe(
      new Date(nearFutureDateStr).toLocaleDateString("en-US", {
        month: "short",
        day: "numeric",
      })
    );

    const farFutureDateStr = "2025-04-12T12:00:00.000Z";
    const farResult = formatRelativeDate(farFutureDateStr);
    expect(farResult).not.toBe("just now");
    expect(farResult).toBe(
      new Date(farFutureDateStr).toLocaleDateString("en-US", {
        month: "short",
        day: "numeric",
      })
    );
  });

  it("does not format invalid date strings as 'Invalid Date'", () => {
    const resultInvalid = formatRelativeDate("invalid-date-string");
    expect(resultInvalid).not.toBe("Invalid Date");
    expect(resultInvalid).toBe("");

    const resultEmpty = formatRelativeDate("");
    expect(resultEmpty).not.toBe("Invalid Date");
    expect(resultEmpty).toBe("");

    const resultWhitespace = formatRelativeDate("   ");
    expect(resultWhitespace).not.toBe("Invalid Date");
    expect(resultWhitespace).toBe("");

    const resultGarbage = formatRelativeDate("not-a-valid-date");
    expect(resultGarbage).not.toBe("Invalid Date");
    expect(resultGarbage).toBe("");
  });
});
