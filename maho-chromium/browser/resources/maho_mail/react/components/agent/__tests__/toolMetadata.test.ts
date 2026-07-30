import { describe, it, expect } from "vitest";
import { getToolCategory, getToolLabel, getToolIcon, getCategoryStyles } from "../toolMetadata";

describe("toolMetadata", () => {
  it("categorizes tools correctly", () => {
    expect(getToolCategory("list_emails")).toBe("read");
    expect(getToolCategory("search_emails")).toBe("search");
    expect(getToolCategory("summarize_email")).toBe("ai");
    expect(getToolCategory("move_email")).toBe("write");
    expect(getToolCategory("non_existent_tool")).toBe("other");
  });

  it("formats labels correctly", () => {
    expect(getToolLabel("list_emails")).toBe("List Emails");
    expect(getToolLabel("send_agent_message")).toBe("Send Agent Message");
    expect(getToolLabel("test")).toBe("Test");
  });

  it("returns icon components", () => {
    expect(getToolIcon("read")).toBeDefined();
    expect(getToolIcon("other")).toBeDefined();
  });

  it("returns styles for categories", () => {
    expect(getCategoryStyles("read")).toHaveProperty("card");
    expect(getCategoryStyles("other")).toHaveProperty("card");
  });
});
