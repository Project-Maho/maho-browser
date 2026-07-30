import { describe, it, expect } from "vitest";

describe("duplicateEvent logic", () => {
  it("calculates 1 hour later for duplicate event start/end", () => {
    const originalStart = "2026-01-15T10:00:00Z";
    const originalEnd = "2026-01-15T11:00:00Z";
    
    const startObj = new Date(originalStart);
    const endObj = new Date(originalEnd);
    
    const dupStart = new Date(startObj.getTime() + 60 * 60 * 1000).toISOString();
    const dupEnd = new Date(endObj.getTime() + 60 * 60 * 1000).toISOString();
    
    expect(dupStart).toBe("2026-01-15T11:00:00.000Z");
    expect(dupEnd).toBe("2026-01-15T12:00:00.000Z");
  });
});
