#include "maho_space_create_dialog.h"

#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "maho/components/constants/webui_url_constants.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace {

constexpr int kDialogWidth = 980;
constexpr int kDialogHeight = 612;

}  // namespace

void MahoSpaceCreateDialog::Open(Browser* parent_browser) {
  if (!parent_browser) {
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
  params.user_title = "Create a Space";

  Browser* popup = Browser::Create(params);
  chrome::AddTabAt(popup, GURL(maho::kMahoSpaceCreateURL),
                   -1, true);

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
