/**
 * Utilities for stripping incoming sender signatures from email bodies
 * before quoting them in replies or forwards.
 *
 * This is intentionally separate from the Maho-internal signature
 * insertion helpers (buildSignatureHtml / buildSignatureText in
 * ComposeModal.tsx).  Those helpers write "--\n" (no trailing space) as
 * the Maho sig-block delimiter and must continue to work unchanged.
 *
 * These functions handle *incoming* signatures, which may use either:
 *   - RFC 2646 "--\n"   (dash dash newline)
 *   - RFC 3676 "-- \n"  (dash dash SPACE newline)  ← most clients use this
 */

/**
 * Strip a plain-text email signature from incoming body text.
 *
 * Matches the first occurrence of \n\n--[ ]\n (with optional trailing
 * space before the newline) and removes everything from that point on.
 */
export function stripIncomingSignature(text: string): string {
  // Match \n\n-- \n or \n\n--\n (RFC 3676 and RFC 2646)
  const match = text.match(/\n\n-- ?\n/);
  if (!match || match.index === undefined) return text;
  return text.slice(0, match.index);
}

/**
 * Strip an HTML email signature from incoming HTML body.
 *
 * Matches the Maho data-maho-signature marker pattern that incoming
 * clients may use, as well as stripping from the <br><br><div
 * data-maho-signature anchor forward.  Uses a greedy tail match so
 * nested <div> elements inside the signature are included in the strip.
 */
export function stripIncomingSignatureHtml(html: string): string {
  // Match <br><br><div data-maho-signature="true" ... > to end of string.
  // Greedy [\s\S]* ensures nested divs are included rather than stopping
  // at the first </div>.
  const match = html.match(/<br><br><div data-maho-signature="true"/);
  if (!match || match.index === undefined) return html;
  return html.slice(0, match.index);
}
