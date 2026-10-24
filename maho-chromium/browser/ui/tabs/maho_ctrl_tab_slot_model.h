// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_TABS_MAHO_CTRL_TAB_SLOT_MODEL_H_
#define MAHO_BROWSER_UI_TABS_MAHO_CTRL_TAB_SLOT_MODEL_H_

#include <algorithm>
#include <cstddef>

namespace maho {
namespace ctrl_tab_slots {

// The selectable slots the overlay offers: the visible tab cards followed,
// when tabs remain hidden, by one "+N more" slot at index |visible_cards|.
struct SlotModel {
  size_t visible_cards = 0;
  bool has_more = false;

  size_t slot_count() const { return visible_cards + (has_more ? 1 : 0); }
  bool is_more_slot(size_t index) const {
    return has_more && index == visible_cards;
  }
};

// Resolves the cursor after the slot model changed, preserving what it meant:
// a wrapped cursor (pending backward commit on the last snapshot entry) stays
// wrapped; a cursor on "+N more" stays there while the slot exists; otherwise
// it clamps to the last visible card and never onto a newly created more slot,
// which would turn the next Ctrl release into a command-overlay open instead of
// a tab switch.
// True when |cursor| names the "+N more" slot of |model|.
inline bool IsMoreSlotIndex(size_t cursor, const SlotModel& model) {
  return model.has_more && cursor == model.visible_cards;
}

inline size_t ResolveCursorAfterRefit(size_t cursor,
                                      size_t snapshot_size,
                                      const SlotModel& model,
                                      bool was_on_more_slot,
                                      bool was_wrapped) {
  if (snapshot_size == 0) {
    return 0;
  }
  if (was_wrapped) {
    // A wrapped cursor normally names its commit target directly (the
    // quick-release path), so it is kept as-is even when it lies past the
    // visible ring. The one case that must not be kept is a refit that puts it
    // exactly on the "+N more" slot: the pending commit ALWAYS names a tab, so
    // painting the more slot would advertise the command palette while the
    // release switches tabs. With a single visible card the initial forward
    // cursor is 1 and index 1 IS the more slot, so this is reachable on a
    // narrow window.
    //
    // The redirect must land on a CARD, not merely inside the ring: the ring
    // includes the more slot, so clamping into it would leave the cursor on the
    // very index being avoided.
    if (IsMoreSlotIndex(cursor, model)) {
      return model.visible_cards ? model.visible_cards - 1 : 0;
    }
    return std::min(cursor, snapshot_size - 1);
  }
  if (was_on_more_slot && model.has_more) {
    return model.visible_cards;
  }
  if (model.visible_cards == 0) {
    return 0;
  }
  return std::min(cursor, model.visible_cards - 1);
}

// The cursor a Ctrl+Tab press starts on: forward is the most-recently-used tab
// after the active one, backward is the least-recently-used tab (the last
// snapshot entry, the Alt+Tab wrap-to-end convention). A backward cursor may
// sit outside the slot ring; the overlay normalizes it to the last visible card
// when it opens, while a quick release commits the wrapped target.
inline size_t InitialCursor(bool forward, size_t snapshot_size) {
  if (snapshot_size == 0) {
    return 0;
  }
  return forward ? 1u : snapshot_size - 1;
}

// True when |cursor| cannot be highlighted as a card selection: it sits past
// the slot ring (a pending backward wrap-to-end commit) or on the "+N more"
// slot (the forward initial cursor whenever only one card is visible - index 1
// IS the more slot then). The overlay normalizes such a cursor onto the last
// visible card before opening, so the highlighted slot and the committed tab
// agree; a cursor already naming a visible card stays put.
inline bool NeedsRingNormalization(size_t cursor, const SlotModel& model) {
  return cursor >= model.slot_count() || model.is_more_slot(cursor);
}

// The snapshot index a pending quick-release commit activates. A pending
// commit always names a tab, never the "+N more" slot: with a single visible
// card the forward target (1) collides with the more slot index, so the commit
// must bypass the slot model entirely.
inline size_t PendingCommitIndex(size_t pending_index, size_t snapshot_size) {
  if (snapshot_size == 0) {
    return 0;
  }
  return std::min(pending_index, snapshot_size - 1);
}

}  // namespace ctrl_tab_slots
}  // namespace maho

#endif  // MAHO_BROWSER_UI_TABS_MAHO_CTRL_TAB_SLOT_MODEL_H_
