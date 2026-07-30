/**
 * Email HTML processing utilities for security and display
 */

import { blockExternalResources } from './trackerBlocker';
import { isHomographDomain } from './languageDetect';

export { stripTrackers, type TrackerBlockResult } from './trackerBlocker';

// CSS for link safety tooltips and suspicious link highlighting
export const LINK_SAFETY_CSS = `
  a[href] { position: relative; }
  a[href]:hover::after {
    content: attr(href);
    position: absolute;
    left: 0;
    top: 100%;
    z-index: 10000;
    max-width: 400px;
    padding: 4px 8px;
    border-radius: 4px;
    font-size: 11px;
    line-height: 1.4;
    word-break: break-all;
    white-space: normal;
    pointer-events: none;
    background: #1e1e2e;
    color: #cdd6f4;
    border: 1px solid #45475a;
    box-shadow: 0 2px 8px rgba(0,0,0,0.3);
  }
  a.maho-suspicious-link {
    outline: 2px dashed #f38ba8 !important;
    outline-offset: 2px;
  }
  a.maho-suspicious-link::before {
    content: "\\26A0\\FE0F ";
  }
`;

/**
 * Strips remote (http/https) images from HTML, preserving data: and cid: URIs.
 * Returns the processed HTML and the count of blocked images.
 */
export function stripRemoteImages(html: string): { html: string; blockedCount: number } {
  const result = blockExternalResources(html, { blockImages: true, blockTrackers: false });
  return {
    html: result.html,
    blockedCount: result.imagesBlocked
  };
}

/**
 * Extracts the hostname portion from a raw URL-like string WITHOUT
 * triggering the browser's IDN Punycode canonicalisation. Returns the
 * literal characters as the user sees them.
 */
function extractRawHostname(raw: string): string {
  let s = raw.trim();
  if (s.startsWith("http://")) s = s.slice("http://".length);
  else if (s.startsWith("https://")) s = s.slice("https://".length);
  s = s.replace(/^www\./i, "");
  const slash = s.indexOf("/");
  if (slash !== -1) s = s.slice(0, slash);
  const query = s.indexOf("?");
  if (query !== -1) s = s.slice(0, query);
  return s.toLowerCase();
}

/**
 * Decides whether an anchor's (href, visibleText) pair is suspicious in
 * the sense of phishing / IDN homograph attacks. The rules are:
 *
 *  - If visible text is not a URL-shaped token, do not classify (caller
 *    pre-filters those).
 *  - If the canonical hostnames differ, suspicious (existing behaviour).
 *  - If the href hostname is in Punycode form (xn--) and the visible
 *    text hostname is plain ASCII, suspicious — the visible text
 *    misrepresents the destination.
 *  - If the *raw* visible hostname mixes scripts in a homograph-prone
 *    way (Latin + Cyrillic / Greek / Armenian / Cherokee), suspicious
 *    even when canonical hostnames happen to match.
 */
function isSuspiciousLinkPair(href: string, visibleText: string): boolean {
  const urlPattern = /^(?:https?:\/\/|www\.|[\p{Letter}\p{Number}][\p{Letter}\p{Number}-]*\.[\p{Letter}]{2,})/u;
  if (!urlPattern.test(visibleText)) return false;

  let hrefDomain = "";
  try {
    const hrefUrl = href.startsWith("http") ? href : `https://${href}`;
    hrefDomain = new URL(hrefUrl).hostname.replace(/^www\./, "").toLowerCase();
  } catch {
    return false;
  }

  let textDomain = "";
  try {
    const textUrl = visibleText.startsWith("http") ? visibleText : `https://${visibleText}`;
    textDomain = new URL(textUrl).hostname.replace(/^www\./, "").toLowerCase();
  } catch {
    return false;
  }

  if (!hrefDomain || !textDomain) return false;

  if (hrefDomain !== textDomain) return true;

  const rawVisibleHostname = extractRawHostname(visibleText);
  const hrefIsPunycode = hrefDomain.startsWith("xn--") || hrefDomain.includes(".xn--");
  const rawTextHasNonAscii = /[^\p{ASCII}]/u.test(rawVisibleHostname);

  // Case 1: href is Punycode but visible text is pure ASCII → the visible
  // text is impersonating an ASCII-looking domain that the href doesn't
  // actually point to.
  if (hrefIsPunycode && !rawTextHasNonAscii) return true;

  // Case 2: raw visible hostname mixes Latin with a lookalike script.
  if (isHomographDomain(rawVisibleHostname)) return true;

  return false;
}

/**
 * In-place: mark every <a href> in `doc` whose visible text creates a
 * phishing-shaped mismatch with the href. Used by both the legacy
 * `processLinksForSafety` helper and by `prepareIframeHtml`'s pipeline.
 */
function markSuspiciousLinks(doc: Document): void {
  const links = doc.querySelectorAll("a[href]");
  links.forEach((a) => {
    const href = a.getAttribute("href") ?? "";
    const visibleText = a.textContent?.trim() ?? "";
    if (isSuspiciousLinkPair(href, visibleText)) {
      a.classList.add("maho-suspicious-link");
      a.setAttribute("data-maho-warning", "suspicious");
    }
  });
}

/**
 * Wrap every plain-text email address occurrence in <a class="maho-email-link">
 * so the parent iframe host can intercept clicks (see EmailBodyView). Skips
 * text already inside an <a>, <script>, or <style>. The regex matches common
 * RFC 5322 local-part + domain shapes without supporting full RFC 5322
 * obscurities (folding, comments, quoted local-parts) — those are extremely
 * rare in body text and a false-negative is harmless.
 */
function linkifyEmailAddresses(doc: Document): void {
  if (!doc.body) return;
  const EMAIL_RE = /[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}/g;
  const walker = doc.createTreeWalker(doc.body, NodeFilter.SHOW_TEXT, {
    acceptNode(node) {
      if (!node.nodeValue || !node.nodeValue.includes("@")) {
        return NodeFilter.FILTER_REJECT;
      }
      let parent = node.parentElement;
      while (parent) {
        const tag = parent.tagName;
        if (tag === "A" || tag === "SCRIPT" || tag === "STYLE") {
          return NodeFilter.FILTER_REJECT;
        }
        parent = parent.parentElement;
      }
      return NodeFilter.FILTER_ACCEPT;
    },
  });

  const candidates: Text[] = [];
  for (let current = walker.nextNode(); current; current = walker.nextNode()) {
    candidates.push(current as Text);
  }

  for (const text of candidates) {
    const content = text.nodeValue ?? "";
    EMAIL_RE.lastIndex = 0;
    const matches: { index: number; length: number; email: string }[] = [];
    for (;;) {
      const m = EMAIL_RE.exec(content);
      if (!m) break;
      matches.push({ index: m.index, length: m[0].length, email: m[0] });
    }
    if (matches.length === 0) continue;

    const frag = doc.createDocumentFragment();
    let cursor = 0;
    for (const match of matches) {
      if (match.index > cursor) {
        frag.appendChild(doc.createTextNode(content.slice(cursor, match.index)));
      }
      const a = doc.createElement("a");
      a.className = "maho-email-link";
      a.setAttribute("data-email", match.email);
      a.setAttribute("href", "javascript:void(0)");
      a.textContent = match.email;
      frag.appendChild(a);
      cursor = match.index + match.length;
    }
    if (cursor < content.length) {
      frag.appendChild(doc.createTextNode(content.slice(cursor)));
    }
    text.parentNode?.replaceChild(frag, text);
  }
}

/**
 * Marks links as suspicious when visible text is a URL whose domain
 * differs from the href domain, or when an IDN homograph attack is
 * detected via Punycode / mixed-script analysis.
 */
export function processLinksForSafety(html: string): string {
  const parser = new DOMParser();
  const doc = parser.parseFromString(html, "text/html");
  markSuspiciousLinks(doc);
  return doc.documentElement.innerHTML;
}

/**
 * Prepares fully sandboxed HTML content for iframe rendering, wrapping styling and observers.
 */
export function prepareIframeHtml(
  html: string,
  options: {
    readonly blockImages: boolean;
    readonly blockTrackers: boolean;
    readonly isDark: boolean;
    readonly fontFamily?: "system-ui" | "Georgia" | "Courier New";
    readonly fontSize?: number;
  }
): { html: string; blockedCount: number; trackersBlocked: number; trackerDomains: string[] } {
  // 1. Block external resources / trackers
  const trackerResult = blockExternalResources(html, {
    blockImages: options.blockImages,
    blockTrackers: options.blockTrackers,
  });

  // 2. Process links for safety (homograph + domain mismatch)
  const parser = new DOMParser();
  const doc = parser.parseFromString(trackerResult.html, "text/html");
  markSuspiciousLinks(doc);
  linkifyEmailAddresses(doc);

  // 3. Inject CSP meta tag and style block into head
  let head = doc.querySelector("head");
  if (!head) {
    head = doc.createElement("head");
    doc.documentElement.insertBefore(head, doc.body);
  }

  const cspMeta = doc.createElement("meta");
  cspMeta.setAttribute("http-equiv", "Content-Security-Policy");
  const imgCSP = options.blockImages ? "data: cid:" : "* data: cid:";
  cspMeta.setAttribute(
    "content",
    `default-src 'none'; img-src ${imgCSP}; style-src 'unsafe-inline'; script-src 'none'; form-action 'none'; base-uri 'none'; frame-src 'none';`
  );
  head.appendChild(cspMeta);

  const fontFamily = options.fontFamily === "Georgia"
    ? "Georgia, 'Times New Roman', serif"
    : options.fontFamily === "Courier New"
      ? "'Courier New', Courier, monospace"
      : "-apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif";
  const fontSize = Math.min(Math.max(options.fontSize ?? 14, 10), 24);
  const style = doc.createElement("style");
  style.textContent = `
    body {
      font-family: ${fontFamily};
      font-size: ${fontSize}px;
      margin: 0;
      padding: 16px;
      word-break: break-word;
      background: #ffffff;
      color: #18181b;
    }
    html.dark body {
      background: #18181b !important;
      color: #d4d4d8 !important;
    }
    a { color: #2563eb; }
    html.dark a { color: #60a5fa !important; }
    .maho-email-link { cursor: pointer; text-decoration: underline; }
    .maho-email-link:hover { opacity: 0.8; }
    img { max-width: 100%; height: auto; }
    html.dark img { opacity: 0.9; }
    pre { overflow-x: auto; }
    ${LINK_SAFETY_CSS}
  `;
  head.appendChild(style);

  // Set initial theme class on HTML element
  if (options.isDark) {
    doc.documentElement.classList.add("dark");
  } else {
    doc.documentElement.classList.remove("dark");
  }

  return {
    html: "<!DOCTYPE html>\n" + doc.documentElement.outerHTML,
    blockedCount: trackerResult.imagesBlocked,
    trackersBlocked: trackerResult.trackersBlocked,
    trackerDomains: trackerResult.trackerDomains,
  };
}

/**
 * Wraps HTML content with dark mode CSS styles for iframe display.
 * @deprecated Use prepareIframeHtml for unified sandbox setup
 */
export function wrapHtmlForDarkMode(html: string): string {
  const darkCss = `<style>
    body { background: #18181b !important; color: #d4d4d8 !important; font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; margin: 0; padding: 16px; word-break: break-word; }
    a { color: #60a5fa !important; }
    img { max-width: 100%; height: auto; opacity: 0.9; }
    pre { overflow-x: auto; }
    ${LINK_SAFETY_CSS}
  </style>`;
  if (html.includes("<head>")) {
    return html.replace("<head>", `<head>${darkCss}`);
  }
  return `<html><head>${darkCss}</head><body>${html}</body></html>`;
}

/**
 * Wraps HTML content with base CSS styles for iframe display (light mode).
 * @deprecated Use prepareIframeHtml for unified sandbox setup
 */
export function wrapHtmlBase(html: string): string {
  const baseCss = `<style>
    body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; margin: 0; padding: 16px; word-break: break-word; }
    img { max-width: 100%; height: auto; }
    pre { overflow-x: auto; }
    ${LINK_SAFETY_CSS}
  </style>`;
  if (html.includes("<head>")) {
    return html.replace("<head>", `<head>${baseCss}`);
  }
  return `<html><head>${baseCss}</head><body>${html}</body></html>`;
}
