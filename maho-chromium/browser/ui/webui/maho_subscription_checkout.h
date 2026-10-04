// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_WEBUI_MAHO_SUBSCRIPTION_CHECKOUT_H_
#define MAHO_BROWSER_UI_WEBUI_MAHO_SUBSCRIPTION_CHECKOUT_H_

#include <string>
#include <string_view>

#include "base/strings/escape.h"

namespace maho::webui {

// LemonSqueezy monthly subscription buy links (store: noveling.lemonsqueezy.com).
// Product 1269989: Pro variant 1985817 ($5, buy-link 28dbf9f8...), Max variant
// 1985825 ($20, buy-link 9f85ab78...). Each variant has its own per-variant buy
// link; the numeric variant IDs are NOT usable as checkout URLs (they 404) and
// belong instead in the relay's LS_VARIANT_PRO / LS_VARIANT_MAX webhook-mapping
// env. If a tier's link is ever cleared, keep a "TODO_REPLACE_WITH_" prefix so
// IsSubscriptionCheckoutPlaceholder() blocks the malformed URL from opening.
inline constexpr std::string_view kProMonthlyCheckoutUrl =
    "https://noveling.lemonsqueezy.com/checkout/buy/"
    "28dbf9f8-97e5-4d04-9ee2-30fc34f97794";
inline constexpr std::string_view kMaxMonthlyCheckoutUrl =
    "https://noveling.lemonsqueezy.com/checkout/buy/"
    "9f85ab78-6ce3-4914-a681-c07f51efa92c";

// Single "Maho AI Pay-as-you-go credits" pay-what-you-want product (variant
// 1988896, relay LS_VARIANT_PAYG). One buy link for all credit top-ups: the
// buyer chooses any amount >= the product minimum in the LemonSqueezy checkout,
// and the relay webhook credits the amount actually paid. There are no longer
// fixed $10/$50/$100 pack links.
inline constexpr std::string_view kPaygCreditsCheckoutUrl =
    "https://noveling.lemonsqueezy.com/checkout/buy/"
    "93f5ccbc-cd1c-4c4a-94a0-5ff4ad8d2c35";

inline std::string_view SubscriptionCheckoutUrlForTier(std::string_view tier) {
  if (tier == "pro") {
    return kProMonthlyCheckoutUrl;
  }
  if (tier == "max") {
    return kMaxMonthlyCheckoutUrl;
  }
  return {};
}

inline bool IsSubscriptionCheckoutPlaceholder(std::string_view checkout_url) {
  return checkout_url.starts_with("TODO_REPLACE_WITH_");
}

inline std::string BuildSubscriptionCheckoutUrl(
    std::string_view checkout_url_template,
    std::string_view user_id) {
  if (checkout_url_template.empty() || user_id.empty() ||
      IsSubscriptionCheckoutPlaceholder(checkout_url_template)) {
    return {};
  }

  std::string checkout_url(checkout_url_template);
  if (!user_id.empty()) {
    checkout_url += "?checkout[custom][user_id]=";
    checkout_url += base::EscapeQueryParamValue(user_id, false);
  }
  return checkout_url;
}

// Builds the pay-what-you-want credits checkout URL, attributing the purchase to
// `user_id` via checkout custom data (same contract as subscriptions). Returns
// empty when `user_id` is empty so callers fail closed rather than open an
// unattributable checkout that would credit no account.
inline std::string BuildPaygCreditsCheckoutUrl(std::string_view user_id) {
  return BuildSubscriptionCheckoutUrl(kPaygCreditsCheckoutUrl, user_id);
}

}  // namespace maho::webui

#endif  // MAHO_BROWSER_UI_WEBUI_MAHO_SUBSCRIPTION_CHECKOUT_H_
