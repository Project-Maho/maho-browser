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
      a.setAttribute("href", "#");
      a.setAttribute("role", "button");
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
 * Rewrites an external http(s) resource URL to the browser's sanitizing image
 * proxy (chrome://image?url=<original>). Trusted chrome:// WebUI renderers
 * cannot fetch/decode network images directly, so the browser process fetches
 * and re-encodes them. Non-http(s) URLs (data:, cid:, blob:, already-proxied)
 * are returned unchanged.
 */
export function toSanitizedImageUrl(url: string | null | undefined): string | null {
  if (!url) return null;
  const trimmed = url.trim();
  if (!/^https?:\/\//i.test(trimmed)) return null;
  return `chrome://image?url=${encodeURIComponent(trimmed)}`;
}

/**
 * Routes surviving external images through the sanitizing proxy so they can
 * actually load inside the trusted WebUI iframe. Operates on img/src,
 * img|source/srcset, and inline style url() references.
 */
export function proxyRemoteImages(doc: Document): void {
  const rewriteSrcset = (value: string): string =>
    value
      .split(",")
      .map((part) => {
        const match = part.trim().match(/^(\S+)(.*)$/);
        if (!match) return part;
        const proxied = toSanitizedImageUrl(match[1]);
        return proxied ? `${proxied}${match[2]}` : part;
      })
      .join(", ");

  for (const img of Array.from(doc.querySelectorAll("img"))) {
    const proxied = toSanitizedImageUrl(img.getAttribute("src"));
    if (proxied) img.setAttribute("src", proxied);
    const srcset = img.getAttribute("srcset");
    if (srcset) img.setAttribute("srcset", rewriteSrcset(srcset));
  }
  for (const source of Array.from(doc.querySelectorAll("source"))) {
    const srcset = source.getAttribute("srcset");
    if (srcset) source.setAttribute("srcset", rewriteSrcset(srcset));
  }
  for (const el of Array.from(doc.querySelectorAll<HTMLElement>("[style]"))) {
    const style = el.getAttribute("style");
    if (!style || !/url\(/i.test(style)) continue;
    const rewritten = style.replace(
      /url\(\s*(['"]?)([^)]*)\1\s*\)/gi,
      (match, _quote, rawUrl) => {
        const proxied = toSanitizedImageUrl(rawUrl);
        return proxied ? `url("${proxied}")` : match;
      },
    );
    if (rewritten !== style) el.setAttribute("style", rewritten);
  }
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
  for (const element of doc.querySelectorAll('meta[http-equiv="refresh" i], base')) {
    element.remove();
  }
  markSuspiciousLinks(doc);
  linkifyEmailAddresses(doc);

  // 2b. When remote images are allowed, route them through the sanitizing
  // chrome://image proxy so they load inside the trusted WebUI iframe.
  if (!options.blockImages) {
    proxyRemoteImages(doc);
  }

  // 3. Inject CSP meta tag and style block into head
  let head = doc.querySelector("head");
  if (!head) {
    head = doc.createElement("head");
    doc.documentElement.insertBefore(head, doc.body);
  }

  const cspMeta = doc.createElement("meta");
  cspMeta.setAttribute("http-equiv", "Content-Security-Policy");
  const imgCSP = options.blockImages ? "data: blob: cid:" : "chrome://image data: blob: cid:";
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
    /*
     * Render the message body as a readable light island regardless of the
     * app theme (the behavior Gmail / Apple Mail use in dark mode). Emails are
     * authored assuming a light background, and a forced dark background here
     * canNOT recolor the message's own element-level text colors (emails very
     * commonly hardcode color:#000 on spans/divs/font tags). The old
     * html.dark body background:#18181b override therefore left dark-authored
     * text invisible on a dark surface. color-scheme:light keeps UA defaults
     * (form controls, canvas) light even when the OS/app is dark. Do not
     * reintroduce a dark background for untrusted email HTML.
     */
    html { color-scheme: light; background: #ffffff; }
    body {
      font-family: ${fontFamily};
      font-size: ${fontSize}px;
      margin: 0;
      padding: 16px;
      word-break: break-word;
      background: #ffffff;
      color: #18181b;
    }
    a { color: #2563eb; }
    .maho-email-link { cursor: pointer; text-decoration: underline; }
    .maho-email-link:hover { opacity: 0.8; }
    img { max-width: 100%; height: auto; }
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
