import { describe, it, expect } from "vitest";
import { processLinksForSafety, prepareIframeHtml } from "../emailHtmlProcessing";

/**
 * IDN-homograph link safety tests for `processLinksForSafety` and
 * `prepareIframeHtml`. The link-safety pass must mark any anchor whose
 * displayed text could mislead a reader about the true destination.
 *
 * Two main attack shapes are covered:
 *  1. **Pure-Punycode href, ASCII visible** — visible text claims to be
 *     a legitimate site but the actual href is an IDN puny-encoded
 *     domain.
 *  2. **Homograph visible text, Punycode href that match after
 *     normalization** — visible text contains a Cyrillic / Greek
 *     lookalike (e.g. `а` U+0430 instead of `a` U+0061), and the href
 *     canonicalises to the same Punycode hostname, so a naive
 *     domain-equality check would let the link through unflagged.
 */

function getMarkedLinks(html: string): Array<{ href: string; text: string }> {
  const parser = new DOMParser();
  const doc = parser.parseFromString(html, "text/html");
  const marked = doc.querySelectorAll("a.maho-suspicious-link");
  const out: Array<{ href: string; text: string }> = [];
  marked.forEach((a) => {
    out.push({
      href: a.getAttribute("href") ?? "",
      text: a.textContent ?? "",
    });
  });
  return out;
}

describe("processLinksForSafety (IDN homograph protection)", () => {
  describe("homograph attack — visible Cyrillic, href Punycode (same canonical)", () => {
    it("flags Cyrillic а (U+0430) lookalike pointing to xn--pple-43d.com", () => {
      // Visible text: "аpple.com" with leading Cyrillic 'а' (U+0430).
      // Href: the Punycode form of that exact spoofed domain.
      const html = `<a href="https://xn--pple-43d.com">\u0430pple.com</a>`;
      const result = processLinksForSafety(html);
      const marked = getMarkedLinks(result);
      expect(marked.length).toBeGreaterThan(0);
      expect(marked[0]?.href).toBe("https://xn--pple-43d.com");
    });

    it("flags mixed Latin + Cyrillic in visible text even when href is Punycode-matched", () => {
      // 'goоgle.com' with Cyrillic 'о' (U+043E) in the middle.
      const html = `<a href="https://xn--gogle-fsa.com">go\u043Egle.com</a>`;
      const result = processLinksForSafety(html);
      expect(getMarkedLinks(result).length).toBeGreaterThan(0);
    });
  });

  describe("Punycode href with ASCII-looking visible text", () => {
    it("flags <a href='https://xn--80ak6aa92e.com'>apple.com</a>", () => {
      // Visible text claims apple.com; href is a fully-Punycode IDN.
      const html = `<a href="https://xn--80ak6aa92e.com">apple.com</a>`;
      const result = processLinksForSafety(html);
      expect(getMarkedLinks(result).length).toBeGreaterThan(0);
    });
  });

  describe("legitimate links — must not flag", () => {
    it("does NOT flag plain https://example.com with matching visible text", () => {
      const html = `<a href="https://example.com">https://example.com</a>`;
      const result = processLinksForSafety(html);
      expect(getMarkedLinks(result).length).toBe(0);
    });

    it("does NOT flag non-URL visible text (e.g. 'click here')", () => {
      const html = `<a href="https://xn--pple-43d.com">Click here</a>`;
      // visible text is not a URL → out of scope for link-safety mismatch.
      const result = processLinksForSafety(html);
      expect(getMarkedLinks(result).length).toBe(0);
    });

    it("does NOT flag pure-ASCII matching link", () => {
      const html = `<a href="https://news.example.com">news.example.com</a>`;
      const result = processLinksForSafety(html);
      expect(getMarkedLinks(result).length).toBe(0);
    });

    it("does NOT flag legitimate international domain where text matches href visually and lexically", () => {
      // Same Cyrillic spelling on both sides: it's just a non-English domain.
      // No spoofing claim — visible text and href are identical strings.
      const html = `<a href="https://\u043F\u043E\u0447\u0442\u0430.\u0440\u0444">\u043F\u043E\u0447\u0442\u0430.\u0440\u0444</a>`;
      const result = processLinksForSafety(html);
      expect(getMarkedLinks(result).length).toBe(0);
    });
  });

  describe("existing domain-mismatch behavior is preserved (regression)", () => {
    it("still flags textDomain !== hrefDomain even without homograph", () => {
      const html = `<a href="https://attacker.example">https://bank.example</a>`;
      const result = processLinksForSafety(html);
      expect(getMarkedLinks(result).length).toBeGreaterThan(0);
    });
  });

  describe("prepareIframeHtml integration", () => {
    it('keeps a light canvas behind authored black text in dark mode', () => {
      const result = prepareIframeHtml('<span style="color:#000">Readable text</span>', { blockImages: true, blockTrackers: false, isDark: true });
      const frame = document.createElement('iframe');
      document.body.appendChild(frame);
      try {
        const doc = frame.contentDocument!;
        doc.open();
        doc.write(result.html);
        doc.close();
        const styles = frame.contentWindow!.getComputedStyle(doc.documentElement);
        expect(styles.backgroundColor).toBe('rgb(255, 255, 255)');
        expect(styles.colorScheme).toBe('light');
        expect(frame.contentWindow!.getComputedStyle(doc.querySelector('span')!).color).toBe('rgb(0, 0, 0)');
      } finally {
        frame.remove();
      }
    });
    it("removes message-controlled refresh and base navigation", () => {
      const result = prepareIframeHtml(
        '<html><head><meta http-equiv="ReFrEsH" content="0;url=https://redirect.example.test/"><base href="https://redirect.example.test/"></head><body><p>Original message</p></body></html>',
        { blockImages: true, blockTrackers: true, isDark: false },
      );
      const doc = new DOMParser().parseFromString(result.html, "text/html");

      expect(doc.querySelector('meta[http-equiv="refresh" i]')).toBeNull();
      expect(doc.querySelector("base")).toBeNull();
      expect(doc.body.textContent).toContain("Original message");
    });

    it("marks the same homograph cases when going through the full iframe prep", () => {
      const html = `<a href="https://xn--pple-43d.com">\u0430pple.com</a>`;
      const result = prepareIframeHtml(html, {
        blockImages: true,
        blockTrackers: false,
        isDark: false,
      });
      expect(getMarkedLinks(result.html).length).toBeGreaterThan(0);
    });

    it("routes HTTPS images through the sanitizing proxy when remote images are enabled", () => {
      const result = prepareIframeHtml(
        '<img src="https://images.example.com/newsletter.png" alt="Newsletter">',
        {
          blockImages: false,
          blockTrackers: true,
          isDark: false,
        },
      );

      // Trusted WebUI renderers cannot fetch network images directly, so the
      // surviving external image is rewritten to the chrome://image proxy.
      expect(result.html).toContain(
        `chrome://image?url=${encodeURIComponent("https://images.example.com/newsletter.png")}`,
      );
      expect(result.html).not.toContain('src="https://images.example.com/newsletter.png"');
      expect(result.html).toContain("img-src chrome://image data: blob: cid:");
      expect(result.blockedCount).toBe(0);
    });

    it("blocks uppercase HTTP image schemes when remote images are disabled", () => {
      const result = prepareIframeHtml(
        '<img src="HTTPS://images.example.com/newsletter.png">',
        { blockImages: true, blockTrackers: false, isDark: false },
      );

      expect(result.html).not.toContain("HTTPS://images.example.com/newsletter.png");
      expect(result.blockedCount).toBe(1);
    });
  });
});
