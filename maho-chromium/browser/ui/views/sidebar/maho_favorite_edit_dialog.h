// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_FAVORITE_EDIT_DIALOG_H_
#define MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_FAVORITE_EDIT_DIALOG_H_

#include <string>

#include "base/functional/callback.h"
#include "ui/gfx/native_ui_types.h"

namespace maho {

// Small modal native Views dialog used by the favorites grid context menu to
// edit a single favorite tile: its glyph (a curated emoji that replaces the
// favicon), its display name, and the URL the tile pins.
//
// The dialog owns nothing beyond its widget; the caller receives the edited
// values once through |on_accept| when the user presses Save. Cancel (or any
// other dismissal) drops the callback without running it.
class MahoFavoriteEditDialog {
 public:
  // Which control receives focus when the dialog opens. Maps 1:1 onto the
  // three favorites context-menu entries that open this dialog.
  enum class Focus {
    kName,
    kIcon,
    kUrl,
  };

  // |initial_icon| is the currently applied custom glyph in UTF-8, empty when
  // the tile renders its favicon. |initial_url| is the pinned URL when one is
  // set, otherwise the live tab URL. |on_accept| runs on the UI thread with
  // the edited name, glyph (empty means "reset to favicon"), and URL.
  static void Show(
      gfx::NativeWindow parent,
      Focus focus,
      const std::u16string& initial_name,
      const std::string& initial_icon,
      const std::string& initial_url,
      base::OnceCallback<void(std::u16string name,
                              std::string icon,
                              std::string url)> on_accept);

 private:
  MahoFavoriteEditDialog() = delete;
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_FAVORITE_EDIT_DIALOG_H_
