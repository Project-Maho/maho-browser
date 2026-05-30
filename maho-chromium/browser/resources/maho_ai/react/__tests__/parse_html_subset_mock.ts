export interface SanitizeInnerHtmlOpts {
  substitutions?: string[];
  attrs?: string[];
  tags?: string[];
}

/**
 * Test-only mock for chrome://resources/js/parse_html_subset.js.
 *
 * Trigger tokens:
 *   CRASH_PRIMARY   — throws on first sanitize call (but not on the fallback
 *                     that already contains '렌더 실패').
 *   CRASH_SECONDARY — throws on every call whose rawString contains the token
 *                     (primary rendered output, raw-text fallback wrapper, but
 *                     NOT the short error sentence that has no token).
 *   CRASH_ALL       — throws unconditionally; exercises the last-resort branch
 *                     that returns a plain string instead of TrustedHTML.
 */
export function sanitizeInnerHtml(
    rawString: string, opts?: SanitizeInnerHtmlOpts): string {
  opts = opts || {};

  if (rawString.includes('CRASH_ALL')) {
    throw new Error('sanitizeInnerHtml: CRASH_ALL — always throws');
  }

  if (rawString.includes('CRASH_PRIMARY') && !rawString.includes('렌더 실패')) {
    throw new Error('Tag CRASH_PRIMARY is not supported');
  }

  if (rawString.includes('CRASH_SECONDARY')) {
    throw new Error('Tag CRASH_SECONDARY is not supported');
  }

  if (rawString.includes('rel="') && (!opts.attrs || !opts.attrs.includes('rel'))) {
    throw new Error('Attribute rel is not supported');
  }

  return rawString;
}
