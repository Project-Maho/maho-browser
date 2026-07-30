import DOMPurify from "dompurify";

/**
 * Allowed HTML tags for email rendering.
 * Kept intentionally narrow — no script, no iframe, no object, no form.
 */
const ALLOWED_TAGS: string[] = [
  "p",
  "br",
  "b",
  "i",
  "u",
  "strong",
  "em",
  "a",
  "blockquote",
  "ul",
  "ol",
  "li",
  "pre",
  "code",
  "span",
  "div",
  "img",
  "table",
  "thead",
  "tbody",
  "tfoot",
  "tr",
  "td",
  "th",
  "h1",
  "h2",
  "h3",
  "h4",
  "h5",
  "h6",
  "hr",
];

/**
 * Allowed attributes.
 * - `style` is kept so email layout is not destroyed (DOMPurify removes
 *   expression() and javascript: from style values by default).
 * - Event-handler attributes (on*) are never in this list so DOMPurify
 *   will strip them regardless of FORCE_BODY.
 */
const ALLOWED_ATTR: string[] = [
  "href",
  "src",
  "alt",
  "title",
  "class",
  "colspan",
  "rowspan",
  "style",
];

/**
 * Sanitize untrusted email HTML before embedding into Tiptap editor content
 * or SMTP bodies.  Call this once at the quote/signature construction boundary
 * — not on every render.
 *
 * Security properties:
 *  - Script tags and all on* event-handler attributes are removed.
 *  - javascript: scheme URLs are removed from href and src.
 *  - Only explicitly listed tags and attributes survive.
 *  - Inline base64 data: image URLs are preserved (DOMPurify allows
 *    data:image/* by default when ADD_DATA_URI_TAGS includes "img").
 *  - style values are kept but DOMPurify strips url(javascript:) and
 *    CSS expression() payloads.
 */
export function sanitizeEmailHtml(html: string): string {
  if (!html) return "";

  return DOMPurify.sanitize(html, {
    ALLOWED_TAGS,
    ALLOWED_ATTR,
    // Allow inline base64 image data URIs (legitimate email attachments)
    ADD_DATA_URI_TAGS: ["img"],
    // Prevent DOM clobbering
    SANITIZE_DOM: true,
    // Return a string, not a DOM node
    RETURN_DOM: false,
    RETURN_DOM_FRAGMENT: false,
  });
}
