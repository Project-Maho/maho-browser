#include "maho/browser/ui/webui/maho_space_manage/maho_space_manage_page_handler.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/task/thread_pool.h"
#include "base/values.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "content/public/browser/web_contents.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/maho_space_profile_bridge.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_container_view.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "ui/views/view_utils.h"

namespace {

std::vector<maho_space_manage::mojom::SpaceInfoPtr> BuildSpacesOnPool(
    const std::string& active_space_id) {
  std::vector<maho_space_manage::mojom::SpaceInfoPtr> spaces;
  MahoCore* core = maho::GetCore();
  if (!core) {
    return spaces;
  }

  char* json_str = maho_core_get_space_view_models(core);
  if (!json_str) {
    return spaces;
  }
  std::string json(json_str);
  maho_string_free(json_str);

  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) {
    return spaces;
  }

  for (const auto& item : parsed->GetList()) {
    const auto* dict = item.GetIfDict();
    if (!dict) {
      continue;
    }
    const std::string* id = dict->FindString("id");
    const std::string* name = dict->FindString("name");
    if (!id || !name) {
      continue;
    }

    auto space = maho_space_manage::mojom::SpaceInfo::New();
    space->id = *id;
    space->name = *name;
    const std::string* icon = dict->FindString("icon");
    const std::string* color = dict->FindString("color");
    space->icon = icon ? *icon : std::string();
    space->color = color ? *color : std::string("#5F8BFF");
    space->active = (*id == active_space_id);
    spaces.push_back(std::move(space));
  }

  return spaces;
}

}  // namespace

MahoSpaceManagePageHandler::MahoSpaceManagePageHandler(
    mojo::PendingReceiver<maho_space_manage::mojom::PageHandler> receiver,
    mojo::PendingRemote<maho_space_manage::mojom::Page> page,
    Browser* browser,
    content::WebContents* web_contents)
    : receiver_(this, std::move(receiver)),
      page_(std::move(page)),
      browser_(browser),
      web_contents_(web_contents) {}

MahoSpaceManagePageHandler::~MahoSpaceManagePageHandler() = default;

void MahoSpaceManagePageHandler::GetSpaces(GetSpacesCallback callback) {
  std::string active_space_id;
  if (auto* bridge = maho::MahoSpaceProfileBridge::GetInstance(); bridge) {
    active_space_id = bridge->GetActiveSpaceId();
  }

  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::TaskPriority::USER_VISIBLE, base::MayBlock()},
      base::BindOnce(&BuildSpacesOnPool, active_space_id),
      std::move(callback));
}

void MahoSpaceManagePageHandler::SwitchToSpace(
    const std::string& space_id,
    SwitchToSpaceCallback callback) {
  bool success = false;
  if (auto* bridge = maho::MahoSpaceProfileBridge::GetInstance(); bridge) {
    success = bridge->SwitchToSpace(space_id);
  }
  std::move(callback).Run(success);
}

void MahoSpaceManagePageHandler::OpenCreateSpace() {
  if (web_contents_) {
    web_contents_->ClosePage();
  }

  BrowserView* browser_view =
      BrowserView::GetBrowserViewForBrowser(browser_);
  if (!browser_view) {
    return;
  }
  for (views::View* child : browser_view->children()) {
    auto* container =
        views::AsViewClass<maho::MahoSidebarContainerView>(child);
    if (container) {
      container->ShowCreateSpace();
      return;
    }
  }
}

void MahoSpaceManagePageHandler::OpenSpaceConfig(const std::string& space_id) {
  if (space_id.empty()) {
    return;
  }
  // TODO(maho): Reconnect browser-hosted space config dialog after its GN
  // ownership is split out cleanly. This unrelated path should not block the
  // Maho settings WebUI build and verification flow.
}

void MahoSpaceManagePageHandler::CloseDialog() {
  if (web_contents_) {
    web_contents_->ClosePage();
  }
}
