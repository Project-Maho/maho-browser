import { describe, it, expect } from "vitest";
import { splitBodyForTone } from "../ComposeModal";

describe("splitBodyForTone", () => {
  it("returns full text as userText when there is no signature or quote", () => {
    const result = splitBodyForTone("Hello world", "<p>Hello world</p>");
    expect(result.userText).toBe("Hello world");
    expect(result.signatureText).toBe("");
    expect(result.quoteText).toBe("");
  });

  it("extracts signature text and leaves it out of userText", () => {
    const body = "Hello world\n\n--\nJohn Doe";
    const html = "<p>Hello world</p><br><br><div data-maho-signature=\"true\" class=\"email-signature\">--<br>John Doe</div>";
    const result = splitBodyForTone(body, html);
    expect(result.userText).toBe("Hello world");
    expect(result.signatureText).toBe("\n\n--\nJohn Doe");
    expect(result.quoteText).toBe("");
    expect(result.signatureHtml).toContain("data-maho-signature");
  });

  it("extracts quote text and leaves it out of userText", () => {
    const body = "My reply\n\nOn Mon, John Doe wrote:\n> original message";
    const html = "<p>My reply</p><br><br><p>On Mon, John Doe wrote:</p><blockquote>original message</blockquote>";
    const result = splitBodyForTone(body, html);
    expect(result.userText).toBe("My reply");
    expect(result.quoteText).toContain("\n\nOn Mon, John Doe wrote:");
    expect(result.signatureText).toBe("");
  });

  it("extracts both signature and quote, userText has neither", () => {
    const body = "My reply\n\n--\nJohn Doe\n\nOn Mon, Alice wrote:\n> hey";
    const html =
      "<p>My reply</p>" +
      "<br><br><div data-maho-signature=\"true\" class=\"email-signature\">--<br>John Doe</div>" +
      "<br><br><p>On Mon, Alice wrote:</p><blockquote>hey</blockquote>";
    const result = splitBodyForTone(body, html);
    expect(result.userText).toBe("My reply");
    expect(result.signatureText).toContain("--\nJohn Doe");
    expect(result.quoteText).toContain("\n\nOn Mon, Alice wrote:");
  });

  it("PGP assembly: encrypted userText + signatureText + quoteText reconstructs the body", () => {
    const userText = "My reply";
    const sigText = "\n\n--\nJohn Doe";
    const quoteText = "\n\nOn Mon, Alice wrote:\n> hey";
    const body = userText + sigText + quoteText;
    const result = splitBodyForTone(body, "<p></p>");

    const fakeEncrypted = "-----BEGIN PGP MESSAGE-----\nencrypted\n-----END PGP MESSAGE-----";
    const assembled = fakeEncrypted + result.signatureText + result.quoteText;

    expect(assembled).toContain(fakeEncrypted);
    expect(assembled).toContain(sigText);
    expect(assembled).toContain(quoteText);
    expect(assembled).not.toContain(userText);
  });

  it("returns empty strings for all parts on empty input", () => {
    const result = splitBodyForTone("", "");
    expect(result.userText).toBe("");
    expect(result.userHtml).toBe("");
    expect(result.signatureText).toBe("");
    expect(result.signatureHtml).toBe("");
    expect(result.quoteText).toBe("");
    expect(result.quoteHtml).toBe("");
  });
});
