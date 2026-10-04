import { describe, it, expect } from "vitest";
import { isValidEmail } from "../../../utils/email";

describe("isValidEmail", () => {
  it("accepts valid plain addresses", () => {
    expect(isValidEmail("user@example.com")).toBe(true);
    expect(isValidEmail("user+tag@example.com")).toBe(true);
    expect(isValidEmail("first.last@sub.domain.co")).toBe(true);
  });

  it("accepts Name <addr> format", () => {
    expect(isValidEmail("John Doe <john@example.com>")).toBe(true);
  });

  it("rejects addresses without @", () => {
    expect(isValidEmail("noatsign")).toBe(false);
  });

  it("rejects addresses without domain TLD", () => {
    expect(isValidEmail("user@localhost")).toBe(false);
  });

  it("rejects empty string", () => {
    expect(isValidEmail("")).toBe(false);
    expect(isValidEmail("   ")).toBe(false);
  });
});

describe("P2-e: template subject with Re:/Fwd: prefix (unit logic)", () => {
  function applyTemplateSubject(currentSubject: string, templateSubject: string): string {
    if (currentSubject.startsWith("Re:") || currentSubject.startsWith("Fwd:")) {
      const prefix = currentSubject.split(":")[0] + ":";
      return `${prefix} ${templateSubject}`;
    }
    return templateSubject;
  }

  it("preserves Re: prefix", () => {
    expect(applyTemplateSubject("Re: Meeting notes", "Follow up")).toBe("Re: Follow up");
  });

  it("preserves Fwd: prefix", () => {
    expect(applyTemplateSubject("Fwd: Original topic", "New template")).toBe("Fwd: New template");
  });

  it("replaces subject when no prefix", () => {
    expect(applyTemplateSubject("", "Template subject")).toBe("Template subject");
    expect(applyTemplateSubject("Plain subject", "Template")).toBe("Template");
  });
});
