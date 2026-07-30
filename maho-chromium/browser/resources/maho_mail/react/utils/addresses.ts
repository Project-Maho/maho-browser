/**
 * Helpers for normalizing email address strings stored in the local DB.
 *
 * The Rust backend persists `to_addresses`/`cc_addresses`/`bcc_addresses`
 * as a JSON array serialized to text (see `crates/maho-core/src/models/email.rs`),
 * e.g. `["alice@example.com","bob@example.com"]`. The frontend `Email` type
 * keeps these as raw `string` for transport, so anything rendering them must
 * parse first or it ends up showing the literal JSON.
 *
 * Display names parsed from IMAP envelopes can also retain the surrounding
 * RFC 2822 quoted-string delimiters (e.g. `"롯데카드"`). Strip them before
 * showing to the user.
 */

/**
 * Parse a `to_addresses` / `cc_addresses` / `bcc_addresses` field into a
 * plain array of trimmed addresses. Falls back to splitting on commas for
 * legacy rows that may not be JSON-encoded.
 */
export function parseAddressList(raw: string | null | undefined): string[] {
  if (!raw) return [];
  const trimmed = raw.trim();
  if (!trimmed) return [];

  if (trimmed.startsWith("[")) {
    try {
      const parsed = JSON.parse(trimmed) as unknown;
      if (Array.isArray(parsed)) {
        return parsed
          .map((entry) =>
            typeof entry === "string" ? entry.trim() : String(entry).trim(),
          )
          .filter(Boolean);
      }
    } catch {
      // fall through to comma split
    }
  }

  return trimmed
    .split(",")
    .map((entry) => entry.trim())
    .filter(Boolean);
}

/**
 * Format an address-list field for display in headers (e.g. "a@x.com, b@y.com").
 * Returns an empty string when the field is empty or an empty JSON array.
 */
export function formatAddressList(raw: string | null | undefined): string {
  return parseAddressList(raw).join(", ");
}

/**
 * Strip the surrounding RFC 2822 quoted-string delimiters that some IMAP
 * servers leave on display names (e.g. `"롯데카드"` → `롯데카드`).
 * Also unescapes any backslash-escaped characters inside the quoted string.
 */
export function sanitizeDisplayName(
  name: string | null | undefined,
): string | null {
  if (!name) return null;
  let trimmed = name.trim();
  // Strip a single matching pair of surrounding quotes (single or double).
  while (
    trimmed.length >= 2 &&
    ((trimmed.startsWith('"') && trimmed.endsWith('"')) ||
      (trimmed.startsWith("'") && trimmed.endsWith("'")))
  ) {
    trimmed = trimmed.slice(1, -1).trim();
  }
  // Unescape `\"` and `\\` that were valid inside the RFC 2822 quoted string.
  trimmed = trimmed.replace(/\\(["\\])/g, "$1").trim();
  return trimmed || null;
}
