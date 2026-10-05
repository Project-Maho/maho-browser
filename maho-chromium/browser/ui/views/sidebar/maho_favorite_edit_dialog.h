// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_FAVORITE_EDIT_DIALOG_H_
#define MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_FAVORITE_EDIT_DIALOG_H_

#include <string>

#include "base/functional/callback.h"
#include "ui/gfx/native_ui_types.h"

namespace maho {

// Modal editor for the URL a favorite tile pins ("Edit Pinned Page").
class MahoFavoriteEditDialog {
 public:
  static void Show(gfx::NativeWindow parent,
                   const std::string& initial_url,
                   base::OnceCallback<void(std::string url)> on_accept);

 private:
  MahoFavoriteEditDialog() = delete;
};

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_FAVORITE_EDIT_DIALOG_H_
