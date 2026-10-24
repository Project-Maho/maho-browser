// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_TABS_MAHO_CTRL_TAB_LAYOUT_H_
#define MAHO_BROWSER_UI_TABS_MAHO_CTRL_TAB_LAYOUT_H_

#include <algorithm>
#include <cstddef>

namespace maho {
namespace ctrl_tab_layout {

inline constexpr int kOverlayPadding = 24;
inline constexpr int kOverlayBorderThickness = 1;
inline constexpr int kCardSpacing = 12;
inline constexpr int kMoreAffordanceWidth = 120;
inline constexpr int kNaturalCardWidth = 220;
inline constexpr int kMinCardWidth = 140;

// The overlay fitted to a window width: how many tab cards to show and how wide
// each one is.
struct Layout {
  size_t visible_cards = 0;
  int card_width = 0;
};

// Overlay width for |cards| tab cards |card_width| wide each, plus the "+N more"
// slot when |has_more| is true.  |card_width| = 0 reports only the fixed part
// (padding, border, spacing and the more slot).
inline int PreferredWidth(size_t cards, bool has_more, int card_width) {
  const int card_count = static_cast<int>(cards);
  const int slot_count = card_count + (has_more ? 1 : 0);
  int width = 2 * (kOverlayPadding + kOverlayBorderThickness) +
              card_count * std::max(card_width, 0);
  if (has_more) {
    width += kMoreAffordanceWidth;
  }
  if (slot_count > 1) {
    width += (slot_count - 1) * kCardSpacing;
  }
  return width;
}

// Fits the overlay into |available_width|: starts from min(total_tabs,
// preferred_max) cards at the natural width, narrows the cards toward
// kMinCardWidth when that alone does not fit, and only then drops cards -- down
// to a single card -- so a narrow window still gets a fully visible overlay
// instead of one clipped by the widget bounds.
inline Layout Fit(int available_width,
                  size_t total_tabs,
                  size_t preferred_max) {
  Layout layout;
  size_t cards = std::min(total_tabs, std::max<size_t>(preferred_max, 1u));
  if (cards == 0) {
    return layout;
  }

  const auto fixed_width = [total_tabs](size_t card_count) {
    return PreferredWidth(card_count, total_tabs > card_count,
                          /*card_width=*/0);
  };

  for (; cards > 0; --cards) {
    const int room = available_width - fixed_width(cards);
    const int per_card = room / static_cast<int>(cards);
    if (per_card >= kNaturalCardWidth) {
      layout.visible_cards = cards;
      layout.card_width = 0;
      return layout;
    }
    if (per_card >= kMinCardWidth) {
      layout.visible_cards = cards;
      layout.card_width = per_card;
      return layout;
    }
  }

  layout.visible_cards = 1;
  layout.card_width = kMinCardWidth;
  return layout;
}

}  // namespace ctrl_tab_layout
}  // namespace maho

#endif  // MAHO_BROWSER_UI_TABS_MAHO_CTRL_TAB_LAYOUT_H_
