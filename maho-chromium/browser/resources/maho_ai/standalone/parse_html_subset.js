/**
 * Standalone-only shim for chrome://resources/js/parse_html_subset.js.
 * Matches the real signature: sanitizeInnerHtml(html, {tags}) → TrustedHTML-like string.
 * Filters the parsed DOM to only the allowed tag names before serialising.
 */
export function sanitizeInnerHtml(html, {tags = []} = {}) {
  const allowedTags = new Set(tags.map(t => t.toUpperCase()));
  const doc = new DOMParser().parseFromString(html, 'text/html');

  function sanitizeNode(node) {
    if (node.nodeType === Node.TEXT_NODE) {
      return node.cloneNode(false);
    }
    if (node.nodeType !== Node.ELEMENT_NODE) {
      return null;
    }
    if (!allowedTags.has(node.tagName)) {
      // Drop disallowed element but keep its children as a fragment
      const frag = document.createDocumentFragment();
      for (const child of Array.from(node.childNodes)) {
        const sanitized = sanitizeNode(child);
        if (sanitized) {
          frag.appendChild(sanitized);
        }
      }
      return frag;
    }
    const clone = document.createElement(node.tagName.toLowerCase());
    // Allowlist safe attributes
    for (const attr of Array.from(node.attributes)) {
      if (attr.name === 'href' || attr.name === 'target' || attr.name === 'rel') {
        clone.setAttribute(attr.name, attr.value);
      }
    }
    for (const child of Array.from(node.childNodes)) {
      const sanitized = sanitizeNode(child);
      if (sanitized) {
        clone.appendChild(sanitized);
      }
    }
    return clone;
  }

  const out = document.createElement('div');
  for (const child of Array.from(doc.body.childNodes)) {
    const sanitized = sanitizeNode(child);
    if (sanitized) {
      out.appendChild(sanitized);
    }
  }
  return out.innerHTML;
}
