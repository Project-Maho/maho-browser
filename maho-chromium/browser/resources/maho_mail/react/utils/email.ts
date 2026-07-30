/**
 * Email validation utility.
 *
 * Basic RFC 5322-inspired regex that blocks obviously invalid addresses
 * (no @, no domain TLD) while allowing plus-aliasing, dots, hyphens, etc.
 */

const EMAIL_REGEX = /^[^\s@]+@[^\s@]+\.[^\s@]{2,}$/;

/**
 * Validates an email address string. Supports "Name <addr>" format by
 * extracting the angle-bracketed address first.
 */
export function isValidEmail(email: string): boolean {
  const trimmed = email.trim();
  if (!trimmed) return false;

  // Extract address from "Name <addr>" format
  const angleMatch = trimmed.match(/<(.+?)>/);
  const addr = angleMatch ? angleMatch[1].trim() : trimmed;

  return EMAIL_REGEX.test(addr);
}
