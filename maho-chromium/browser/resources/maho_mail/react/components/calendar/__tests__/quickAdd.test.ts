import { describe, it, expect } from "vitest";
import * as chrono from "chrono-node";

describe("quickAdd date parsing", () => {
  it("parses english tomorrow meeting", () => {
    const text = "tomorrow 3pm meeting";
    const parsed = chrono.parse(text);
    expect(parsed.length).toBeGreaterThan(0);
    const date = parsed[0].start.date();
    expect(date).toBeInstanceOf(Date);
  });

  it("returns zero results for unsupported korean text (triggering full form fallback)", () => {
    const text = "내일 오후 3시 회의";
    const parsed = chrono.parse(text);
    // Since Korean is not supported in this chrono version, it returns 0 results
    expect(parsed.length).toBe(0);
  });
});
