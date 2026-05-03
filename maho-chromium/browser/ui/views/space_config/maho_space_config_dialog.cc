#include "maho/browser/ui/views/space_config/maho_space_config_dialog.h"

#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "maho/components/constants/webui_url_constants.h"
#include "net/base/url_util.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace {

constexpr int kDialogWidth = 420;
constexpr int kDialogHeight = 280;

const char* FocusToString(maho_space_config::mojom::InitialFocus focus) {
  switch (focus) {
    case maho_space_config::mojom::InitialFocus::kName:
      return "name";
    case maho_space_config::mojom::InitialFocus::kIcon:
      return "icon";
    case maho_space_config::mojom::InitialFocus::kColor:
      return "color";
    case maho_space_config::mojom::InitialFocus::kProfile:
      return "profile";
  }
  return "name";
}

}  // namespace

void MahoSpaceConfigDialog::Open(
    Browser* parent_browser,
    const std::string& space_id,
    maho_space_config::mojom::InitialFocus focus) {
  if (!parent_browser || space_id.empty()) {
    return;
  }

  gfx::Rect parent_bounds = parent_browser->window()->GetBounds();
  int x = parent_bounds.x() + (parent_bounds.width() - kDialogWidth) / 2;
  int y = parent_bounds.y() + (parent_bounds.height() - kDialogHeight) / 2;

  Browser::CreateParams params(Browser::TYPE_POPUP,
                               parent_browser->profile(),
                               true);
  params.initial_bounds = gfx::Rect(x, y, kDialogWidth, kDialogHeight);
  params.omit_from_session_restore = true;

  GURL url(maho::kMahoSpaceConfigURL);
  url = net::AppendQueryParameter(url, "space_id", space_id);
  url = net::AppendQueryParameter(url, "focus", FocusToString(focus));

  Browser* popup = Browser::Create(params);
  chrome::AddTabAt(popup, url, -1, true);

  BrowserView* browser_view =
      BrowserView::GetBrowserViewForBrowser(popup);
  if (browser_view) {
    views::Widget* widget = browser_view->GetWidget();
    if (widget) {
      widget->SetZOrderLevel(ui::ZOrderLevel::kFloatingWindow);
    }
  }

  popup->window()->Show();
}
