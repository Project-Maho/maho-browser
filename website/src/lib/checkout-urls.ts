/**
 * Shared source of truth for LemonSqueezy checkout URLs used across the website.
 *
 * Kept in sync with:
 * - maho/crates/maho-cli/src/credits.rs (checkout_url_for_pack)
 * - maho-chromium/browser/ui/webui/maho_ai/maho_ai_page_handler.cc (GetBuyCreditsUrl)
 *
 * Backend variant IDs for these packs (from ADR 0010): 4001, 4002, 4003.
 */

export const LS_STORE_BASE = 'https://maho.lemonsqueezy.com/buy';

export const CREDIT_PACK_SLUGS = {
  10: 'credit-pack-10',
  50: 'credit-pack-50',
  100: 'credit-pack-100',
} as const;

export type CreditPackSize = keyof typeof CREDIT_PACK_SLUGS;

export function creditPackUrl(size: CreditPackSize): string {
  return `${LS_STORE_BASE}/${CREDIT_PACK_SLUGS[size]}`;
}
