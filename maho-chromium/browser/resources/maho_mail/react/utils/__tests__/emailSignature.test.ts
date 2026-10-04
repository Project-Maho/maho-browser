import { describe, expect, it } from "vitest";

import { stripIncomingSignature, stripIncomingSignatureHtml } from "../emailSignature";

describe("stripIncomingSignature (plain text)", () => {
  it('strips RFC 3676 "-- \\n" (dash dash SPACE newline) delimiter', () => {
    const input = "Hello world\n\n-- \nJohn Doe\njohn@example.com";
    const result = stripIncomingSignature(input);
    expect(result).toBe("Hello world");
  });

  it('strips RFC 2646 "--\\n" (dash dash newline, no space) delimiter', () => {
    const input = "Hello world\n\n--\nJohn Doe\njohn@example.com";
    const result = stripIncomingSignature(input);
    expect(result).toBe("Hello world");
  });

  it("returns body unchanged when no signature delimiter present", () => {
    const input = "Hello world\nThis is a normal email body.";
    const result = stripIncomingSignature(input);
    expect(result).toBe("Hello world\nThis is a normal email body.");
  });

  it("strips multiline signature content", () => {
    const input = "Message body\n\n-- \nBest regards,\nJane Smith\nCompany Inc.\ntel: +1 555-1234";
    const result = stripIncomingSignature(input);
    expect(result).toBe("Message body");
  });

  it("strips signature but does not touch nested dash-like content within the body", () => {
    const input = "Check this list:\n- item one\n- item two\n\n-- \nSig line";
    const result = stripIncomingSignature(input);
    expect(result).toBe("Check this list:\n- item one\n- item two");
  });

  it("handles empty string input", () => {
    expect(stripIncomingSignature("")).toBe("");
  });

  it("strips from the FIRST delimiter, not the last", () => {
    // Body may contain embedded sig-like patterns; only the first \n\n--[ ]\n wins
    const input = "First part\n\n-- \nSig\n\n-- \nSig2";
    const result = stripIncomingSignature(input);
    expect(result).toBe("First part");
  });
});

describe("stripIncomingSignatureHtml", () => {
  it("strips a simple data-maho-signature div", () => {
    const input = '<p>Hello</p><br><br><div data-maho-signature="true" class="email-signature">--<br>John</div>';
    const result = stripIncomingSignatureHtml(input);
    expect(result).toBe("<p>Hello</p>");
  });

  it("strips a signature div with nested divs (does not stop at first </div>)", () => {
    const input =
      '<p>Hello</p><br><br><div data-maho-signature="true" class="email-signature"><div class="inner"><span>Name</span></div><div class="contact">john@example.com</div></div>';
    const result = stripIncomingSignatureHtml(input);
    expect(result).toBe("<p>Hello</p>");
  });

  it("returns HTML unchanged when no signature div present", () => {
    const input = "<p>Hello world</p><p>Normal paragraph</p>";
    const result = stripIncomingSignatureHtml(input);
    expect(result).toBe("<p>Hello world</p><p>Normal paragraph</p>");
  });

  it("handles empty string", () => {
    expect(stripIncomingSignatureHtml("")).toBe("");
  });

  it("strips everything from <br><br><div data-maho-signature to end", () => {
    const input = '<p>Body</p><br><br><div data-maho-signature="true">Sig content here without closing tag';
    const result = stripIncomingSignatureHtml(input);
    expect(result).toBe("<p>Body</p>");
  });
});
