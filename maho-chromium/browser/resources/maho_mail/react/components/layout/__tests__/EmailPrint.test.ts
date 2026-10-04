import { describe, expect, it } from "vitest";
import { generatePrintHtml } from "../EmailContent";
import type { Email } from "../../../types";

const mockEmail: Email = {
  id: "msg-123",
  account_id: "acc-1",
  folder_id: "inbox",
  uid: 1,
  message_id: "msg-123",
  in_reply_to: null,
  from_address: "sender@example.com",
  from_name: "Sender Name",
  to_addresses: "recipient@example.com",
  cc_addresses: "",
  bcc_addresses: "",
  subject: "Test Email Subject",
  snippet: "Test snippet",
  body_text: "Plain text fallback",
  body_html: "",
  date: "2026-09-02T12:00:00Z",
  raw_size: 100,
  created_at: "2026-09-02T12:00:00Z",
  mdn_requested: null,
  is_read: true,
  is_starred: false,
  is_draft: false,
  has_attachments: false,
};

describe("generatePrintHtml (Finding C-1 regression tests)", () => {
  it("does not load remote images when printing with default privacy settings", () => {
    const result = generatePrintHtml(
      mockEmail,
      '<p>Print me</p><img src="https://tracker.example.test/pixel"><img src="data:image/png;base64,iVBORw0KGgo=" alt="local">',
    );
    const doc = new DOMParser().parseFromString(result, "text/html");

    expect(doc.querySelector('img[src^="https:"]')).toBeNull();
    expect(doc.querySelector('img[alt="local"]')?.getAttribute("src"))
      .toBe("data:image/png;base64,iVBORw0KGgo=");
    expect(doc.body.textContent).toContain("Print me");
  });

  it("sanitizes body_html containing img onerror and script tags, and includes strict CSP meta", () => {
    const maliciousHtml =
      '<p>Hello world</p><img src="x" onerror="alert(1)"><script>alert("xss")</script>';
    const result = generatePrintHtml(mockEmail, maliciousHtml);

    // Strict CSP meta tag must be present in head
    expect(result).toContain(
      '<meta http-equiv="Content-Security-Policy" content="default-src \'none\'; img-src data: blob: cid:; style-src \'unsafe-inline\'">'
    );

    // Malicious script and event handler attributes must be completely stripped
    expect(result).not.toContain("<script");
    expect(result).not.toContain("alert");
    expect(result).not.toContain("onerror");

    // Legitimate content must be preserved
    expect(result).toContain("<p>Hello world</p>");
    expect(result).toContain("<img src=\"x\">");
  });

  it("preserves safe inline styles and data: images in print output", () => {
    const safeHtmlContent =
      '<div style="color: rgb(255, 0, 0);"><img src="data:image/png;base64,iVBORw0KGgo=" alt="logo"><b>Bold text</b></div>';
    const result = generatePrintHtml(mockEmail, safeHtmlContent);

    expect(result).toContain("style=");
    expect(result).toContain("data:image/png;base64,iVBORw0KGgo=");
    expect(result).toContain("<b>Bold text</b>");
  });

  it("escapes malicious header fields and fallback plain text with safeHtml", () => {
    const xssEmail: Email = {
      ...mockEmail,
      subject: '<script>alert("subj")</script>',
      from_name: '<img src=x onerror=alert("name")>',
      from_address: 'xss<test>@evil.com',
      to_addresses: '<script>alert("to")</script>',
      cc_addresses: '<script>alert("cc")</script>',
      body_text: '<script>alert("body")</script>',
    };

    const result = generatePrintHtml(xssEmail, null);

    expect(result).not.toContain("<script");
    expect(result).not.toContain("<img");
    expect(result).toContain("&lt;img");
    expect(result).toContain("&lt;script&gt;alert(&quot;subj&quot;)&lt;/script&gt;");
    expect(result).toContain("&lt;script&gt;alert(&quot;body&quot;)&lt;/script&gt;");
  });

  it("renders plain-text fallback when bodyHtml is null", () => {
    const result = generatePrintHtml(mockEmail, null);
    expect(result).toContain("<pre");
    expect(result).toContain("Plain text fallback");
  });
});
