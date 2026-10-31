import { describe, expect, it } from "vitest";
import { sanitizeEmailHtml } from "../sanitizeHtml";

describe("sanitizeEmailHtml", () => {
  it("strips script tags", () => {
    const input = '<p>Hello</p><script>alert("xss")</script>';
    const result = sanitizeEmailHtml(input);
    expect(result).not.toContain("<script");
    expect(result).not.toContain("alert");
    expect(result).toContain("<p>Hello</p>");
  });

  it("strips event handler attributes", () => {
    const input = '<img src="photo.jpg" onerror="alert(1)" onload="evil()">';
    const result = sanitizeEmailHtml(input);
    expect(result).not.toContain("onerror");
    expect(result).not.toContain("onload");
    expect(result).not.toContain("evil()");
  });

  it("strips onclick and other inline event handlers", () => {
    const input = '<a href="https://example.com" onclick="steal()">click me</a>';
    const result = sanitizeEmailHtml(input);
    expect(result).not.toContain("onclick");
    expect(result).not.toContain("steal()");
    expect(result).toContain("https://example.com");
  });

  it("strips javascript: URLs from href", () => {
    const input = '<a href="javascript:alert(1)">click</a>';
    const result = sanitizeEmailHtml(input);
    expect(result).not.toContain("javascript:");
  });

  it("strips javascript: URLs from img src", () => {
    const input = '<img src="javascript:alert(1)">';
    const result = sanitizeEmailHtml(input);
    expect(result).not.toContain("javascript:");
  });

  it("preserves allowed tags", () => {
    const input =
      "<p>Hello <b>bold</b> <i>italic</i> <u>underline</u> <strong>strong</strong> <em>em</em></p>";
    const result = sanitizeEmailHtml(input);
    expect(result).toContain("<p>");
    expect(result).toContain("<b>");
    expect(result).toContain("<i>");
    expect(result).toContain("<u>");
    expect(result).toContain("<strong>");
    expect(result).toContain("<em>");
  });

  it("preserves blockquote, ul, ol, li", () => {
    const input =
      "<blockquote><ul><li>item 1</li><li>item 2</li></ul></blockquote>";
    const result = sanitizeEmailHtml(input);
    expect(result).toContain("<blockquote>");
    expect(result).toContain("<ul>");
    expect(result).toContain("<li>");
    expect(result).toContain("item 1");
  });

  it("preserves table structure", () => {
    const input =
      "<table><thead><tr><th>A</th></tr></thead><tbody><tr><td colspan='2'>B</td></tr></tbody></table>";
    const result = sanitizeEmailHtml(input);
    expect(result).toContain("<table>");
    expect(result).toContain("<thead>");
    expect(result).toContain("<tbody>");
    expect(result).toContain("<tr>");
    expect(result).toContain("<th>");
    expect(result).toContain("<td");
    expect(result).toContain("colspan");
  });

  it("preserves heading tags h1-h6", () => {
    const input = "<h1>One</h1><h2>Two</h2><h3>Three</h3><h6>Six</h6>";
    const result = sanitizeEmailHtml(input);
    expect(result).toContain("<h1>");
    expect(result).toContain("<h2>");
    expect(result).toContain("<h3>");
    expect(result).toContain("<h6>");
  });

  it("preserves inline base64 data: URL on img (legitimate)", () => {
    const base64 =
      "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwADhQGAWjR9awAAAABJRU5ErkJggg==";
    const input = `<img src="${base64}" alt="1px">`;
    const result = sanitizeEmailHtml(input);
    expect(result).toContain("data:image/png;base64,");
    expect(result).toContain('alt="1px"');
  });

  it("preserves https href on anchor", () => {
    const input = '<a href="https://example.com" title="example">link</a>';
    const result = sanitizeEmailHtml(input);
    expect(result).toContain('href="https://example.com"');
    expect(result).toContain("link");
  });

  it("preserves pre and code tags", () => {
    const input = "<pre><code>const x = 1;</code></pre>";
    const result = sanitizeEmailHtml(input);
    expect(result).toContain("<pre>");
    expect(result).toContain("<code>");
    expect(result).toContain("const x = 1;");
  });

  it("preserves br and hr", () => {
    const input = "<p>line1</p><br><hr>";
    const result = sanitizeEmailHtml(input);
    expect(result).toContain("<br");
    expect(result).toContain("<hr");
  });

  it("strips unknown/dangerous tags like object and iframe", () => {
    const input =
      '<p>text</p><iframe src="https://evil.com"></iframe><object data="evil.swf"></object>';
    const result = sanitizeEmailHtml(input);
    expect(result).not.toContain("<iframe");
    expect(result).not.toContain("<object");
    expect(result).toContain("<p>text</p>");
  });

  it("handles empty string gracefully", () => {
    expect(sanitizeEmailHtml("")).toBe("");
  });

  it("handles plain text without HTML tags", () => {
    const result = sanitizeEmailHtml("Hello world");
    expect(result).toContain("Hello world");
  });
});

describe("Trusted Types default-policy sanitizer (__mahoSanitizeHtml)", () => {
  it("keeps the body when DOMPurify's own parse re-enters the default policy", () => {
    // On chrome://maho-mail every DOMParser.parseFromString call is a Trusted
    // Types sink routed through the default policy, including the one inside
    // DOMPurify. Emulate that routing and check the message body survives.
    const hook = globalThis.__mahoSanitizeHtml!;
    const original = DOMParser.prototype.parseFromString;
    DOMParser.prototype.parseFromString = function (
      this: DOMParser,
      input: string,
      type: DOMParserSupportedType,
    ) {
      return original.call(this, hook(String(input)), type);
    };
    try {
      const out = hook('<div dir="ltr">Body text from Gmail</div><script>alert(1)</script>');
      expect(out).toContain("Body text from Gmail");
      expect(out).not.toContain("<script");
    } finally {
      DOMParser.prototype.parseFromString = original;
    }
  });
});
