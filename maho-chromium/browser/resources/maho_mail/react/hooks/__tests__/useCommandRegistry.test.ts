import { describe, it, expect, vi } from "vitest";
import { renderHook } from "@testing-library/react";
import {
  calculateScore,
  groupByCategory,
  useCommandRegistry,
  type PaletteCommand,
} from "../useCommandRegistry";

const makeCommand = (overrides: Partial<PaletteCommand> = {}): PaletteCommand => ({
  id: "cmd-1",
  label: "Test Command",
  category: "mail",
  handler: vi.fn(),
  ...overrides,
});

describe("calculateScore", () => {
  it("returns score including 100 for prefix match (term at start of label)", () => {
    const command = makeCommand({ label: "Compose Email" });
    const score = calculateScore(command, ["com"]);
    expect(score).toBeGreaterThanOrEqual(100);
  });

  it("returns score including 50 for word-start match (space before term)", () => {
    const command = makeCommand({ label: "Compose Email" });
    const score = calculateScore(command, ["email"]);
    expect(score).toBeGreaterThanOrEqual(50);
  });

  it("returns score including 50 for hyphen word-start match", () => {
    const command = makeCommand({ label: "Rich-text Editor" });
    const score = calculateScore(command, ["text"]);
    expect(score).toBeGreaterThanOrEqual(50);
  });

  it("returns score including 20 for substring match", () => {
    const command = makeCommand({ label: "Compose" });
    const score = calculateScore(command, ["pos"]);
    expect(score).toBeGreaterThanOrEqual(20);
  });

  it("returns score including 10 for keyword match", () => {
    const command = makeCommand({
      label: "Go to Inbox",
      keywords: ["inbox", "mail"],
    });
    const score = calculateScore(command, ["inbox"]);
    expect(score).toBeGreaterThanOrEqual(10);
  });

  it("returns score including 5 for category match", () => {
    const command = makeCommand({ category: "mail" });
    const score = calculateScore(command, ["mail"]);
    expect(score).toBeGreaterThanOrEqual(5);
  });

  it("accumulates scores from multiple terms", () => {
    const command = makeCommand({ label: "Compose Email" });
    const scoreSingle = calculateScore(command, ["com"]);
    const scoreMultiple = calculateScore(command, ["com", "email"]);
    expect(scoreMultiple).toBeGreaterThan(scoreSingle);
  });

  it("returns 0 when term does not match anything", () => {
    const command = makeCommand({ label: "Compose" });
    const score = calculateScore(command, ["xyz"]);
    expect(score).toBe(0);
  });

  it("skips empty terms (no score change)", () => {
    const command = makeCommand({ label: "Compose Email" });
    const scoreWithEmpty = calculateScore(command, ["com", ""]);
    const scoreWithoutEmpty = calculateScore(command, ["com"]);
    expect(scoreWithEmpty).toBe(scoreWithoutEmpty);
  });

  it("maintains correct score ordering: prefix > word-start > substring > keyword > category", () => {
    const prefixCmd = makeCommand({ label: "Mail Archive", category: "view" });
    const wordStartCmd = makeCommand({ label: "New Mail", category: "view" });
    const substringCmd = makeCommand({ label: "Email Archive", category: "view" });
    const keywordCmd = makeCommand({ label: "Archive", keywords: ["mail"], category: "view" });
    const categoryCmd = makeCommand({ label: "Archive", category: "mail" });

    const prefixScore = calculateScore(prefixCmd, ["mail"]);
    const wordStartScore = calculateScore(wordStartCmd, ["mail"]);
    const substringScore = calculateScore(substringCmd, ["mail"]);
    const keywordScore = calculateScore(keywordCmd, ["mail"]);
    const categoryScore = calculateScore(categoryCmd, ["mail"]);

    expect(prefixScore).toBeGreaterThan(wordStartScore);
    expect(wordStartScore).toBeGreaterThan(substringScore);
    expect(substringScore).toBeGreaterThan(keywordScore);
    expect(keywordScore).toBeGreaterThan(categoryScore);
  });
});

describe("groupByCategory", () => {
  it("groups commands by category in correct order: mail, compose, navigate, search, view, ai, settings", () => {
    const commands: PaletteCommand[] = [
      makeCommand({ id: "settings-1", category: "settings", label: "Settings 1" }),
      makeCommand({ id: "mail-1", category: "mail", label: "Mail 1" }),
      makeCommand({ id: "ai-1", category: "ai", label: "AI 1" }),
      makeCommand({ id: "compose-1", category: "compose", label: "Compose 1" }),
      makeCommand({ id: "search-1", category: "search", label: "Search 1" }),
      makeCommand({ id: "navigate-1", category: "navigate", label: "Navigate 1" }),
      makeCommand({ id: "view-1", category: "view", label: "View 1" }),
    ];

    const result = groupByCategory(commands);
    const resultIds = result.map((cmd) => cmd.id);

    expect(resultIds).toEqual([
      "mail-1",
      "compose-1",
      "navigate-1",
      "search-1",
      "view-1",
      "ai-1",
      "settings-1",
    ]);
  });

  it("returns empty array when given empty array", () => {
    const result = groupByCategory([]);
    expect(result).toEqual([]);
  });

  it("returns just commands from single category when all commands have same category", () => {
    const commands: PaletteCommand[] = [
      makeCommand({ id: "mail-1", category: "mail", label: "Mail 1" }),
      makeCommand({ id: "mail-2", category: "mail", label: "Mail 2" }),
    ];

    const result = groupByCategory(commands);
    expect(result).toHaveLength(2);
    expect(result.every((cmd) => cmd.category === "mail")).toBe(true);
  });

  it("preserves order within same category", () => {
    const commands: PaletteCommand[] = [
      makeCommand({ id: "mail-1", category: "mail", label: "Mail 1" }),
      makeCommand({ id: "mail-2", category: "mail", label: "Mail 2" }),
      makeCommand({ id: "mail-3", category: "mail", label: "Mail 3" }),
    ];

    const result = groupByCategory(commands);
    const resultIds = result.map((cmd) => cmd.id);
    expect(resultIds).toEqual(["mail-1", "mail-2", "mail-3"]);
  });
});

describe("useCommandRegistry", () => {
  it("returns up to 15 enabled commands grouped by category when query is empty", () => {
    const commands: PaletteCommand[] = Array.from({ length: 20 }, (_, i) =>
      makeCommand({
        id: `cmd-${i}`,
        category: i % 2 === 0 ? "mail" : "compose",
        label: `Command ${i}`,
      })
    );

    const { result } = renderHook(() => useCommandRegistry(commands));
    const searchResults = result.current.search("");

    expect(searchResults).toHaveLength(15);
  });

  it("returns scored results sorted by score when query has matches", () => {
    const commands: PaletteCommand[] = [
      makeCommand({ id: "substring", label: "Compose Email", category: "compose" }),
      makeCommand({ id: "prefix", label: "Mail Archive", category: "mail" }),
      makeCommand({ id: "word-start", label: "New Mail", category: "mail" }),
    ];

    const { result } = renderHook(() => useCommandRegistry(commands));
    const searchResults = result.current.search("mail");

    expect(searchResults[0].id).toBe("prefix");
    expect(searchResults[1].id).toBe("word-start");
    expect(searchResults[2].id).toBe("substring");
  });

  it("filters out disabled commands (enabled() returns false)", () => {
    const commands: PaletteCommand[] = [
      makeCommand({ id: "enabled", label: "Enabled Command" }),
      makeCommand({
        id: "disabled",
        label: "Disabled Command",
        enabled: () => false,
      }),
    ];

    const { result } = renderHook(() => useCommandRegistry(commands));
    const searchResults = result.current.search("");

    expect(searchResults).toHaveLength(1);
    expect(searchResults[0].id).toBe("enabled");
  });

  it("includes commands without enabled property", () => {
    const command: PaletteCommand = {
      id: "no-enabled",
      label: "No Enabled Property",
      category: "mail",
      handler: vi.fn(),
    };

    const { result } = renderHook(() => useCommandRegistry([command]));
    const searchResults = result.current.search("");

    expect(searchResults).toHaveLength(1);
    expect(searchResults[0].id).toBe("no-enabled");
  });

  it("returns empty array when query has no matches", () => {
    const commands: PaletteCommand[] = [
      makeCommand({ id: "cmd-1", label: "Compose Email" }),
      makeCommand({ id: "cmd-2", label: "Send Message" }),
    ];

    const { result } = renderHook(() => useCommandRegistry(commands));
    const searchResults = result.current.search("xyz");

    expect(searchResults).toEqual([]);
  });

  it("caps results at 15 items", () => {
    const commands: PaletteCommand[] = Array.from({ length: 25 }, (_, i) =>
      makeCommand({
        id: `cmd-${i}`,
        label: `Command ${i} Mail`,
        category: "mail",
      })
    );

    const { result } = renderHook(() => useCommandRegistry(commands));
    const searchResults = result.current.search("mail");

    expect(searchResults).toHaveLength(15);
  });
});
