import { describe, it, expect } from "vitest";
import { splitBodyForTone } from "../ComposeForm";

describe("splitBodyForTone boundary cases", () => {
  it("preserves paragraphs starting with 'On ' as userText when not a quote header", () => {
    const body = "Hello team,\n\nOn Friday we will deploy the release.";
    const html = "<p>Hello team,</p><p>On Friday we will deploy the release.</p>";
    const result = splitBodyForTone(body, html);

    expect(result.userText).toBe("Hello team,\n\nOn Friday we will deploy the release.");
    expect(result.quoteText).toBe("");
  });

  it("preserves genuine 'On ... wrote:' reply quotes in quoteText", () => {
    const body = "Sounds good.\n\nOn Jan 15, 2026, alice@example.com wrote:\n> Can we deploy?";
    const html =
      "<p>Sounds good.</p><br><br><p>On Jan 15, 2026, alice@example.com wrote:</p><blockquote>Can we deploy?</blockquote>";
    const result = splitBodyForTone(body, html);

    expect(result.userText).toBe("Sounds good.");
    expect(result.quoteText).toBe("\n\nOn Jan 15, 2026, alice@example.com wrote:\n> Can we deploy?");
  });

  it("preserves forwarded message headers and text in quoteText", () => {
    const body =
      "FYI\n\n---------- Forwarded message ---------\nFrom: bob@example.com\nSubject: Update";
    const html =
      "<p>FYI</p><br><br><p>---------- Forwarded message ---------</p><p>From: bob@example.com</p>";
    const result = splitBodyForTone(body, html);

    expect(result.userText).toBe("FYI");
    expect(result.quoteText).toBe(
      "\n\n---------- Forwarded message ---------\nFrom: bob@example.com\nSubject: Update"
    );
  });
});
