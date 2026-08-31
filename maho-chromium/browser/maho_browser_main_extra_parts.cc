#include "maho/browser/maho_browser_main_extra_parts.h"

#include <algorithm>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/json/string_escape.h"
#include "base/logging.h"
#include "base/memory/weak_ptr.h"
#include "base/no_destructor.h"
#include "base/threading/scoped_blocking_call.h"
#include "base/supports_user_data.h"
#include "base/path_service.h"
#include "base/power_monitor/power_monitor.h"
#include "base/task/bind_post_task.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "base/trace_event/trace_event.h"
#include "base/uuid.h"
#include "base/values.h"
#include "build/build_config.h"
#include "maho/browser/maho_private_context_policy.h"
#include "maho/browser/ui/views/maho_mini/maho_mini_window.h"

#if BUILDFLAG(IS_WIN)
#include "maho/browser/mcp/maho_mcp_pipe_server_win.h"
#include "maho/browser/mcp/maho_mcp_session_token_win.h"
#else
#include "maho/browser/mcp/maho_mcp_socket_server.h"
#endif
#if BUILDFLAG(IS_MAC)
#include "base/apple/mach_port_rendezvous_mac.h"
#endif
#include "base/base64.h"
#include "base/containers/span.h"
#include "base/run_loop.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/synchronization/waitable_event.h"
#include "chrome/browser/bookmarks/bookmark_model_factory.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/file_select_helper.h"
#include "chrome/browser/notifications/notification_common.h"
#include "chrome/browser/notifications/notification_display_service.h"
#include "chrome/browser/notifications/notification_display_service_impl.h"
#include "chrome/browser/notifications/notification_handler.h"
#include "chrome/browser/prefs/session_startup_pref.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/sessions/exit_type_service.h"
#include "chrome/browser/sessions/session_restore.h"
#include "chrome/browser/sessions/session_tab_helper_factory.h"
#include "chrome/browser/ui/browser_navigator.h"
#include "chrome/browser/ui/browser_navigator_params.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/simple_message_box.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/pref_names.h"
#include "components/bookmarks/browser/bookmark_model.h"
#include "components/bookmarks/browser/bookmark_node.h"
#include "components/prefs/pref_service.h"
#include "components/sessions/content/session_tab_helper.h"
#include "components/viz/common/frame_sinks/copy_output_result.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/file_select_listener.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/isolated_world_ids.h"
#include "maho/browser/ai/maho_ai_settings_migration.h"
#include "maho/browser/ai/maho_artifact_registry.h"
#include "maho/browser/ai/maho_control_activity_service.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/maho_routines_scheduler.h"
#include "maho/browser/maho_space_profile_bridge.h"
#include "maho/browser/maho_tab_id_helper.h"
#include "maho/browser/maho_tab_id_session_helper.h"
#include "maho/browser/maho_tab_registry.h"
#include "maho/browser/mail_helper/maho_mail_helper_version.h"
#include "maho/browser/mail_helper/maho_mail_notification_coordinator.h"
#include "maho/browser/mail_helper/maho_mail_notification_permission.h"
#include "maho/browser/mail_helper/maho_mail_service.h"
#include "maho/browser/mail_helper/maho_mail_service_factory.h"
#include "maho/browser/mcp/maho_mcp_accessibility_handler.h"
#include "maho/browser/mcp/maho_mcp_console_capture.h"
#include "maho/browser/mcp/maho_mcp_firewall.h"
#include "maho/browser/mcp/maho_mcp_input_synthesizer.h"
#include "maho/browser/mcp/maho_mcp_navigation_tracker.h"
#include "maho/browser/mcp/maho_mcp_network_observer.h"
#include "maho/browser/mcp/maho_mcp_screenshot_handler.h"
#include "maho/browser/mcp/maho_mcp_session.h"
#include "maho/browser/net/maho_atc_state.h"
#include "maho/browser/net/maho_content_blocker_update_service.h"
#include "maho/browser/net/maho_content_blocker_update_service_factory.h"
#include "maho/browser/passwords/maho_password_authorization_service.h"
#include "maho/browser/sync/maho_sync_relay_client.h"
#include "maho/browser/ui/downloads/maho_download_bridge_service_factory.h"
#include "maho/browser/ui/theme/maho_color_mixer.h"
#include "maho/browser/ui/theme/maho_space_theme_state.h"
#include "maho/browser/ui/views/changelog/maho_changelog_auto_opener.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_prefs.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_view.h"
#include "maho/browser/ui/views/welcome/maho_welcome_window.h"
#include "maho/browser/ui/webui/maho_ai_prefs.h"
#include "maho/browser/ui/webui/maho_auth_utils.h"
#include "maho/browser/ui/webui/maho_welcome/maho_welcome_prefs.h"
#include "maho/browser/updates/maho_config_manager.h"
#include "maho/browser/updates/maho_update_manager.h"
#include "maho/third_party/maho/maho_bridge.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "net/base/url_util.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/accessibility/ax_action_data.h"
#include "ui/accessibility/ax_enums.mojom-shared.h"
#include "ui/accessibility/ax_mode.h"
#include "ui/accessibility/ax_node.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/accessibility/ax_tree.h"
#include "ui/accessibility/ax_tree_update.h"
#include "ui/base/base_window.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"
#include "ui/color/color_provider_manager.h"
#include "third_party/blink/public/mojom/choosers/file_chooser.mojom.h"
#include "ui/message_center/public/cpp/notification.h"
#include "ui/message_center/public/cpp/notifier_id.h"
#include "url/gurl.h"

namespace {
MahoBrowserMainExtraParts* g_instance = nullptr;
}

MahoBrowserMainExtraParts::MahoBrowserMainExtraParts()
    : mail_notification_coordinators_(
          std::make_unique<maho::MahoMailNotificationCoordinatorRegistry>()) {
  DCHECK(!g_instance);
  g_instance = this;
}

MahoBrowserMainExtraParts::~MahoBrowserMainExtraParts() {
  g_instance = nullptr;
  ShutdownCoreAndServices();
}

// static
MahoBrowserMainExtraParts* MahoBrowserMainExtraParts::GetInstance() {
  return g_instance;
}

size_t
MahoBrowserMainExtraParts::MailNotificationCoordinatorCountForTesting() const {
  return mail_notification_coordinators_
             ? mail_notification_coordinators_->size()
             : 0;
}

namespace {

constexpr int kIsolatedWorldIdForMcpAction =
    content::ISOLATED_WORLD_ID_CONTENT_END + 6;

constexpr char kMahoMailNotificationOrigin[] = "chrome://maho-mail";
constexpr char kMahoMailNotifierId[] = "maho.mail";
  static_assert(static_cast<int>(NotificationHandler::Type::MAHO_MAIL) == 12);

void RouteMahoMailNotification(Profile* profile,
                               const std::string& account_id,
                               const std::string& email_id);

class MahoMailNotificationHandler final : public NotificationHandler {
 public:
  MahoMailNotificationHandler();
  ~MahoMailNotificationHandler() override;

  void RegisterClick(const std::string& notification_id,
                     base::RepeatingClosure callback);
  void Remove(const std::string& notification_id);

  void OnClose(Profile* profile,
               const GURL& origin,
               const std::string& notification_id,
               bool by_user,
               base::OnceClosure completed_closure) override;
  void OnClick(Profile* profile,
               const GURL& origin,
               const std::string& notification_id,
               const std::optional<int>& action_index,
               const std::optional<std::u16string>& reply,
               base::OnceClosure completed_closure) override;

 private:
  std::map<std::string, base::RepeatingClosure> callbacks_;
};

MahoMailNotificationHandler::MahoMailNotificationHandler() = default;

MahoMailNotificationHandler::~MahoMailNotificationHandler() = default;

void MahoMailNotificationHandler::RegisterClick(
    const std::string& notification_id,
    base::RepeatingClosure callback) {
  callbacks_.insert_or_assign(notification_id, std::move(callback));
}

void MahoMailNotificationHandler::Remove(
    const std::string& notification_id) {
  callbacks_.erase(notification_id);
}

void MahoMailNotificationHandler::OnClose(
    Profile* profile,
    const GURL& origin,
    const std::string& notification_id,
    bool by_user,
    base::OnceClosure completed_closure) {
  Remove(notification_id);
  std::move(completed_closure).Run();
}

void MahoMailNotificationHandler::OnClick(
    Profile* profile,
    const GURL& origin,
    const std::string& notification_id,
    const std::optional<int>& action_index,
    const std::optional<std::u16string>& reply,
    base::OnceClosure completed_closure) {
  auto it = callbacks_.find(notification_id);
  if (it != callbacks_.end() && !action_index.has_value()) {
    base::RepeatingClosure callback = it->second;
    callbacks_.erase(it);
    callback.Run();
  }
  std::move(completed_closure).Run();
}

MahoMailNotificationHandler* EnsureMahoMailNotificationHandler(
    Profile* profile) {
  NotificationDisplayServiceImpl* service =
      NotificationDisplayServiceImpl::GetForProfile(profile);
  if (!service) {
    return nullptr;
  }
  NotificationHandler* existing =
      service->GetNotificationHandler(NotificationHandler::Type::MAHO_MAIL);
  if (existing) {
    return static_cast<MahoMailNotificationHandler*>(existing);
  }
  auto handler = std::make_unique<MahoMailNotificationHandler>();
  MahoMailNotificationHandler* result = handler.get();
  service->AddNotificationHandler(NotificationHandler::Type::MAHO_MAIL,
                                  std::move(handler));
  return result;
}

GURL BuildMahoMailDeepLink(const std::string& account_id,
                           const std::string& email_id) {
  return net::AppendOrReplaceQueryParameter(
      net::AppendOrReplaceQueryParameter(GURL(kMahoMailNotificationOrigin),
                                         "accountId", account_id),
      "emailId", email_id);
}

void RouteMahoMailNotification(Profile* profile,
                               const std::string& account_id,
                               const std::string& email_id) {
  if (!profile || !profile->IsRegularProfile()) {
    return;
  }
  const GURL target = BuildMahoMailDeepLink(account_id, email_id);
  bool found = false;
  ForEachCurrentBrowserWindowInterfaceOrderedByActivation(
      [profile, &target, &found](BrowserWindowInterface* browser) {
        if (browser->GetProfile() != profile) {
          return true;
        }
        TabStripModel* tabs = browser->GetTabStripModel();
        if (!tabs) {
          return true;
        }
        for (int index = 0; index < tabs->count(); ++index) {
          content::WebContents* contents = tabs->GetWebContentsAt(index);
          if (!contents ||
              contents->GetLastCommittedURL().scheme() !=
                  GURL(kMahoMailNotificationOrigin).scheme() ||
              contents->GetLastCommittedURL().host() !=
                  GURL(kMahoMailNotificationOrigin).host()) {
            continue;
          }
          browser->GetWindow()->Activate();
          tabs->ActivateTabAt(index);
          NavigateParams params(browser, target, ui::PAGE_TRANSITION_GENERATED);
          params.disposition = WindowOpenDisposition::CURRENT_TAB;
          Navigate(&params);
          found = true;
          return false;
        }
        return true;
      });
  if (!found) {
    NavigateParams params(profile, target, ui::PAGE_TRANSITION_GENERATED);
    params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
    Navigate(&params);
  }
}

void WithdrawMahoMailNotification(Profile* profile,
                                  const std::string& notification_id);

void DisplayMahoMailNotification(
    Profile* profile,
    const maho::MahoMailNotificationPayload& payload,
    base::OnceCallback<void(bool)> completion) {
  if (!profile || !profile->IsRegularProfile()) {
    std::move(completion).Run(false);
    return;
  }

  NotificationDisplayServiceImpl* notification_service =
      NotificationDisplayServiceImpl::GetForProfile(profile);
  MahoMailNotificationHandler* handler =
      EnsureMahoMailNotificationHandler(profile);
  if (!notification_service || !handler) {
    std::move(completion).Run(false);
    return;
  }

  message_center::Notification notification(
      message_center::NOTIFICATION_TYPE_SIMPLE, payload.notification_id,
      base::UTF8ToUTF16(payload.title), base::UTF8ToUTF16(payload.body),
      ui::ImageModel(), std::u16string(), GURL(kMahoMailNotificationOrigin),
      message_center::NotifierId(message_center::NotifierType::SYSTEM_COMPONENT,
                                 kMahoMailNotifierId),
      {}, nullptr);
  notification_service->Display(NotificationHandler::Type::MAHO_MAIL,
                                notification, nullptr);
  handler->RegisterClick(payload.notification_id, payload.click_callback);
  notification_service->GetDisplayed(base::BindOnce(
      [](std::string notification_id,
         base::RepeatingCallback<void(const std::string&)> withdraw,
         base::OnceCallback<void(bool)> completion,
         std::set<std::string> displayed,
         bool supports_synchronization) {
        const bool accepted =
            supports_synchronization &&
            displayed.contains(notification_id);
        if (!accepted) {
          withdraw.Run(notification_id);
        }
        std::move(completion).Run(accepted);
      },
      payload.notification_id,
      base::BindRepeating(&WithdrawMahoMailNotification, profile),
      std::move(completion)));
}

void WithdrawMahoMailNotification(Profile* profile,
                                  const std::string& notification_id) {
  if (NotificationDisplayServiceImpl* service =
          NotificationDisplayServiceImpl::GetForProfile(profile)) {
    if (MahoMailNotificationHandler* handler =
            EnsureMahoMailNotificationHandler(profile)) {
      handler->Remove(notification_id);
    }
    service->Close(NotificationHandler::Type::MAHO_MAIL, notification_id);
  }
}

bool CreateDirectoryOnThreadPool(base::FilePath path) {
  if (path.empty()) {
    return true;
  }
  return base::CreateDirectory(path);
}

std::string ExecuteMcpActionJsBlocking(content::WebContents* wc,
                                       const std::string& script) {
  if (!wc || !wc->GetPrimaryMainFrame()) {
    return std::string();
  }
  base::WeakPtr<content::WebContents> wc_weak = wc->GetWeakPtr();
  std::string result;
  base::RunLoop run_loop(base::RunLoop::Type::kNestableTasksAllowed);
  wc->GetPrimaryMainFrame()->ExecuteJavaScriptInIsolatedWorld(
      base::UTF8ToUTF16(script),
      base::BindOnce(
          [](base::WeakPtr<content::WebContents> wc_weak,
             std::string* out, base::OnceClosure quit, base::Value val) {
            if (wc_weak && val.is_string()) {
              *out = val.GetString();
            }
            std::move(quit).Run();
          },
          wc_weak, &result, run_loop.QuitClosure()),
      kIsolatedWorldIdForMcpAction);
  run_loop.Run();
  return result;
}

std::string ExecuteMcpActionJsAllFramesBlocking(content::WebContents* wc,
                                                const std::string& script) {
  if (!wc) {
    return std::string();
  }
  base::WeakPtr<content::WebContents> wc_weak = wc->GetWeakPtr();
  std::string result;
  std::vector<content::RenderFrameHost*> frames;
  wc->ForEachRenderFrameHost([&](content::RenderFrameHost* rfh) {
    if (rfh && rfh->IsRenderFrameLive()) {
      frames.push_back(rfh);
    }
  });
  for (content::RenderFrameHost* rfh : frames) {
    base::RunLoop run_loop(base::RunLoop::Type::kNestableTasksAllowed);
    std::string frame_result;
    rfh->ExecuteJavaScriptInIsolatedWorld(
        base::UTF8ToUTF16(script),
        base::BindOnce(
            [](base::WeakPtr<content::WebContents> wc_weak,
               std::string* out, base::OnceClosure quit, base::Value val) {
              if (wc_weak && val.is_string()) {
                *out = val.GetString();
              }
              std::move(quit).Run();
            },
            wc_weak, &frame_result, run_loop.QuitClosure()),
        kIsolatedWorldIdForMcpAction);
    run_loop.Run();
    if (!frame_result.empty()) {
      result = frame_result;
    }
  }
  return result;
}

std::optional<gfx::PointF> ComputeViewportCenterCssFromTree(
    const ui::AXTree& tree,
    ui::AXNodeID ax_id,
    bool prefer_leading_edge = false) {
  const ui::AXNode* node = tree.GetFromId(ax_id);
  if (!node) {
    return std::nullopt;
  }
  gfx::RectF bounds = tree.GetTreeBounds(node, /*offscreen=*/nullptr,
                                         /*clip_bounds=*/false);
  const ui::AXNode* curr = node;
  while (curr) {
    const ui::AXNode* parent = curr->parent();
    if (parent && curr->GetRole() == ax::mojom::Role::kRootWebArea) {
      gfx::RectF parent_bounds = tree.GetTreeBounds(parent, nullptr, false);
      if (!parent_bounds.IsEmpty()) {
        bounds.Offset(parent_bounds.OffsetFromOrigin());
      }
    }
    curr = parent;
  }
  if (bounds.IsEmpty()) {
    return std::nullopt;
  }
  gfx::PointF center = bounds.CenterPoint();
  if (prefer_leading_edge) {
    center.set_x(bounds.x() + std::min(bounds.width() / 2.f, 12.f));
  }
  return center;
}

std::optional<gfx::PointF> ComputeViewportCenterCss(
    const ui::AXTreeUpdate& update,
    ui::AXNodeID ax_id,
    float device_scale_factor,
    bool prefer_leading_edge = false) {
  // Preferred: ui::AXTree geometry applies each container's scroll offset and
  // transform, yielding frame-relative (viewport) coordinates. The manual
  // offset-container walk below is a fallback that ignores scroll/transform and
  // therefore mislocates targets whenever the page is scrolled or the target
  // lives inside a scrolled/fixed container such as a modal dialog.
  // Note: AXTree bounds serialized by Blink are already in CSS viewport pixels.
  {
    ui::AXTree tree;
    if (tree.Unserialize(update)) {
      if (std::optional<gfx::PointF> center = ComputeViewportCenterCssFromTree(
              tree, ax_id, prefer_leading_edge)) {
        return center;
      }
    }
  }

  const ui::AXNodeData* target_node = nullptr;
  for (const auto& node : update.nodes) {
    if (node.id == ax_id) {
      target_node = &node;
      break;
    }
  }
  if (!target_node) {
    return std::nullopt;
  }

  gfx::RectF bounds = target_node->relative_bounds.bounds;
  std::unordered_map<ui::AXNodeID, const ui::AXNodeData*> node_map;
  for (const auto& node : update.nodes) {
    node_map[node.id] = &node;
  }

  const ui::AXNodeData* current = target_node;
  int iterations = 0;
  while (current && iterations < 100) {
    ui::AXNodeID cid = current->relative_bounds.offset_container_id;
    const ui::AXNodeData* container = nullptr;
    if (cid > 0) {
      auto it = node_map.find(cid);
      if (it != node_map.end()) {
        container = it->second;
      }
    }
    if (container) {
      bounds.Offset(container->relative_bounds.bounds.OffsetFromOrigin());
    } else {
      break;
    }
    current = container;
    iterations++;
  }

  gfx::PointF center = bounds.CenterPoint();
  if (prefer_leading_edge) {
    center.set_x(bounds.x() +
                 std::min(bounds.width() / 2.f, 12.f));
  }
  return center;
}

// Find an option node (in an opened dropdown/listbox) whose accessible name
// matches `value`, preferring an exact case-insensitive match and otherwise a
// substring match. Excludes the combobox node itself. Used to click custom
// (non-native) dropdown options that carry no @ref and cannot be selected by
// assigning a value.
std::optional<ui::AXNodeID> FindOptionNodeByText(const ui::AXTreeUpdate& update,
                                                 ui::AXNodeID exclude_id,
                                                 const std::string& value) {
  const std::string needle = base::ToLowerASCII(value);
  if (needle.empty()) {
    return std::nullopt;
  }
  std::optional<ui::AXNodeID> substring_match;
  for (const auto& node : update.nodes) {
    if (node.id == exclude_id) {
      continue;
    }
    const std::string& name =
        node.GetStringAttribute(ax::mojom::StringAttribute::kName);
    if (name.empty()) {
      continue;
    }
    const std::string hay = base::ToLowerASCII(name);
    if (hay == needle) {
      return node.id;
    }
    if (!substring_match && hay.find(needle) != std::string::npos) {
      substring_match = node.id;
    }
  }
  return substring_match;
}

std::string DecodeActiveSpaceId(char* active_id_cstr) {
  if (!active_id_cstr) {
    return std::string();
  }

  std::string active_id = active_id_cstr;
  std::optional<base::Value> parsed =
      base::JSONReader::Read(active_id, base::JSON_PARSE_RFC);
  maho_string_free(active_id_cstr);

  if (parsed && parsed->is_string()) {
    return parsed->GetString();
  }

  return active_id;
}

void PopulateBridgeFromCore(MahoCore* core) {
  auto* bridge = maho::MahoSpaceProfileBridge::GetInstance();
  if (!core || !bridge->HydrateFromCore()) {
    return;
  }

  auto apply_initial_theme = []() {
    const MahoSpaceThemeState::ChangedSpaceIds changed_spaces =
        MahoSpaceThemeState::UpdateFromCore();
    auto* bridge = maho::MahoSpaceProfileBridge::GetInstance();
    for (Browser* browser : bridge->GetBrowsersForSpaces(changed_spaces)) {
      maho::MahoSidebarView::RefreshSpaceThemeForBrowser(browser);
    }
  };
  if (content::BrowserThread::CurrentlyOn(content::BrowserThread::UI)) {
    apply_initial_theme();
  } else {
    content::GetUIThreadTaskRunner({})->PostTask(
        FROM_HERE, base::BindOnce(apply_initial_theme));
  }

  LOG(INFO) << "Maho bridge populated: " << bridge->GetRegisteredSpaceCount()
            << " spaces";
}

void NormalizeMcpSnapshotRoles(ui::AXTreeUpdate* update) {
  for (auto& node : update->nodes) {
    if (node.role == ax::mojom::Role::kComboBoxSelect) {
      node.role = ax::mojom::Role::kComboBoxGrouping;
    }
  }
}

bool PerformAxAction(content::WebContents* wc,
                     ui::AXNodeID ax_id,
                     ax::mojom::Action action,
                     const std::string& value = std::string()) {
  if (!wc) {
    return false;
  }
  ui::AXActionData action_data;
  action_data.action = action;
  action_data.target_node_id = ax_id;
  action_data.value = value;

  bool dispatched = false;
  wc->ForEachRenderFrameHost([&](content::RenderFrameHost* rfh) {
    if (rfh && rfh->IsRenderFrameLive()) {
      rfh->AccessibilityPerformAction(action_data);
      dispatched = true;
    }
  });
  return dispatched;
}

void SecureZeroizeString(std::string& value) {
  if (!value.empty()) {
    std::fill_n(static_cast<volatile char*>(&value[0]), value.size(), 0);
    value.clear();
  }
}

std::string OriginForWebContents(content::WebContents* wc) {
  if (!wc) {
    return std::string();
  }
  const GURL url = wc->GetLastCommittedURL();
  if (!url.is_valid() || !url.has_host()) {
    return std::string();
  }
  return url.DeprecatedGetOriginAsURL().spec();
}

std::string FirstCredentialOrigin(const base::DictValue& dict,
                                  const std::string& fallback_origin) {
  if (const auto* origins = dict.FindList("origins")) {
    for (const auto& origin : *origins) {
      if (origin.is_string() && !origin.GetString().empty()) {
        return origin.GetString();
      }
    }
  }
  if (const std::string* signon_realm = dict.FindString("signonRealm")) {
    if (!signon_realm->empty()) {
      return *signon_realm;
    }
  }
  return fallback_origin;
}

std::string UsernameHintFromCredential(const base::DictValue& dict) {
  if (const std::string* username = dict.FindString("usernameHint")) {
    return *username;
  }
  if (const std::string* username = dict.FindString("username")) {
    return *username;
  }
  return std::string();
}

struct BrowserVaultCredentialLease {
  BrowserVaultCredentialLease() = default;
  ~BrowserVaultCredentialLease() {
    SecureZeroizeString(password);
    SecureZeroizeString(totp);
  }
  BrowserVaultCredentialLease(const BrowserVaultCredentialLease&) = delete;
  BrowserVaultCredentialLease& operator=(const BrowserVaultCredentialLease&) =
      delete;
  BrowserVaultCredentialLease(BrowserVaultCredentialLease&& other) noexcept
      : profile_key(std::move(other.profile_key)),
        origin(std::move(other.origin)),
        expires_at(other.expires_at),
        username_hint(std::move(other.username_hint)),
        password(std::move(other.password)),
        totp(std::move(other.totp)) {}
  BrowserVaultCredentialLease& operator=(
      BrowserVaultCredentialLease&& other) noexcept {
    if (this != &other) {
      SecureZeroizeString(password);
      SecureZeroizeString(totp);
      profile_key = std::move(other.profile_key);
      origin = std::move(other.origin);
      expires_at = other.expires_at;
      username_hint = std::move(other.username_hint);
      password = std::move(other.password);
      totp = std::move(other.totp);
    }
    return *this;
  }

  std::string profile_key;
  std::string origin;
  base::TimeTicks expires_at;
  std::string username_hint;
  std::string password;
  std::string totp;
};

// True only when the core affirmatively reports that this profile's Vault holds
// no key material. A missing core, an unreadable status, or any other lock
// state (locked/unlocked/auto-locked) all mean "initialized or unknown" and
// return false, so setup is never re-run over key material that exists.
bool IsVaultUninitialized(MahoCore* core) {
  if (!core) {
    return false;
  }
  char* status_json = maho_vault_status_json(core);
  if (!status_json) {
    return false;
  }
  const std::string status(status_json);
  maho_string_free(status_json);

  std::optional<base::Value> parsed =
      base::JSONReader::Read(status, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return false;
  }
  const base::DictValue* data = parsed->GetDict().FindDict("data");
  if (!data) {
    return false;
  }
  const std::string* lock_state = data->FindString("lockState");
  return lock_state && *lock_state == "uninitialized";
}

}  // namespace

void MahoBrowserMainExtraParts::PostEarlyInitialization() {
  // Keyed-service factories must be registered before Chromium's
  // ChromeBrowserMainExtraPartsProfiles::PreProfileInit() seals the topology.
  maho::ai::EnsureMahoArtifactRegistryFactoryBuilt();
  maho::ai::EnsureMahoControlActivityServiceFactoryBuilt();
  maho::InstallMahoMailNotificationPermissionCallbacks();

  base::FilePath user_data_dir;
  if (base::PathService::Get(chrome::DIR_USER_DATA, &user_data_dir)) {
    storage_path_ = user_data_dir.AppendASCII("MahoCore");
  }

  ui::ColorProviderManager::Get().AppendColorProviderInitializer(
      base::BindRepeating(&AddMahoColorMixers));
}

namespace {
const char* ClassToString(MahoPrivateContextClass cls) {
  switch (cls) {
    case MahoPrivateContextClass::kNull:
      return "null";
    case MahoPrivateContextClass::kGuest:
      return "guest";
    case MahoPrivateContextClass::kSystem:
      return "system";
    case MahoPrivateContextClass::kDevToolsOtr:
      return "devtools_otr";
    case MahoPrivateContextClass::kPrimaryIncognito:
      return "primary_incognito";
    case MahoPrivateContextClass::kOtherOtr:
      return "other_otr";
    case MahoPrivateContextClass::kRegular:
      return "regular";
  }
}
}  // namespace

void MahoBrowserMainExtraParts::MaybeInitializeForBrowser(
    BrowserWindowInterface* browser) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  Profile* profile = browser ? browser->GetProfile() : nullptr;
  MahoPrivateContextClass cls =
      profile ? MahoClassifyProfile(profile) : MahoPrivateContextClass::kNull;

  if (cls != MahoPrivateContextClass::kRegular) {
    LOG(INFO) << "MAHO_INIT_AUDIT: class=" << ClassToString(cls)
              << ", decision=deny, stage=MaybeInitializeForBrowser";
    return;
  }

  EnsureMailNotificationCoordinator(profile);

  if (core_ || regular_services_initializing_) {
    LOG(INFO)
        << "MAHO_INIT_AUDIT: class=" << ClassToString(cls)
        << ", decision=already_initialized, stage=MaybeInitializeForBrowser";
    return;
  }

  LOG(INFO) << "MAHO_INIT_AUDIT: class=" << ClassToString(cls)
            << ", decision=allow, stage=MaybeInitializeForBrowser";

  InitializeRegularServicesOnce(profile);
}

namespace {

// Bridges the async maho_core routines FFI callbacks back onto the calling
// sequence, then invokes the MCP session's RunRoutineCallback.
struct RunRoutineFfiCtx {
  maho::MahoMcpBrowserDelegate::RunRoutineCallback callback;
  scoped_refptr<base::SequencedTaskRunner> task_runner;
};

void OnRoutineRunComplete(void* user_data, const char* result_json) {
  auto* ctx = static_cast<RunRoutineFfiCtx*>(user_data);
  std::string json(result_json ? result_json : "");
  ctx->task_runner->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](maho::MahoMcpBrowserDelegate::RunRoutineCallback cb,
             std::string j) {
            std::move(cb).Run(
                maho::MahoMcpBrowserDelegate::RoutineRunError::kNone,
                std::move(j));
          },
          std::move(ctx->callback), std::move(json)));
  delete ctx;
}

void OnRoutineRunError(void* user_data, const char* error) {
  auto* ctx = static_cast<RunRoutineFfiCtx*>(user_data);
  std::string err(error ? error : "Routine execution failed");
  ctx->task_runner->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](maho::MahoMcpBrowserDelegate::RunRoutineCallback cb,
             std::string e) {
            std::move(cb).Run(
                maho::MahoMcpBrowserDelegate::RoutineRunError::
                    kExecutionFailed,
                std::move(e));
          },
          std::move(ctx->callback), std::move(err)));
  delete ctx;
}

// R-8: an MCP target is exposed only if a private-context token built from the
// exact owning browser (and WebContents, when a tab is involved) revalidates
// kMCP. kMCP is denied for every non-regular context, so OTR browsers/tabs are
// non-enumerable and an explicit OTR id is indistinguishable from unknown.
bool IsBrowserMcpEligible(BrowserWindowInterface* browser) {
  if (!browser) {
    return false;
  }
  MahoPrivateContextToken token(browser, nullptr);
  return token.Revalidate(MahoPrivateCapability::kMCP);
}

bool IsWebContentsMcpEligible(BrowserWindowInterface* browser,
                              content::WebContents* web_contents) {
  if (!browser || !web_contents) {
    return false;
  }
  MahoPrivateContextToken token(browser, web_contents);
  return token.Revalidate(MahoPrivateCapability::kMCP);
}

class McpTabIdRegistry {
 public:
  int GetIdFor(content::WebContents* web_contents) {
    if (!web_contents) {
      return 0;
    }
    MahoTabIdHelper::CreateForWebContents(web_contents);
    MahoTabIdHelper* helper =
        MahoTabIdHelper::FromWebContents(web_contents);
    if (!helper || helper->stable_tab_id().empty() ||
        next_id_ == std::numeric_limits<int>::max()) {
      return 0;
    }
    auto [it, inserted] = ids_.try_emplace(helper->stable_tab_id(), next_id_);
    if (inserted) {
      ++next_id_;
    }
    return it->second;
  }

 private:
  std::map<std::string, int> ids_;
  int next_id_ = 1;
};

int McpSessionIdOf(content::WebContents* web_contents) {
  static base::NoDestructor<McpTabIdRegistry> registry;
  return registry->GetIdFor(web_contents);
}

// ---------------------------------------------------------------------------
// Maho: automation file-chooser interception.
//
// While an MCP/agent session holds an exclusive lease on a tab, a file
// chooser opened by that tab is NOT shown natively. The pending selection is
// parked on the WebContents (SupportsUserData, so it dies with the tab) and
// `input.file_upload_select` completes it later. Tabs without an active
// lease keep the normal native dialog flow.
// ---------------------------------------------------------------------------

namespace {

class PendingAutomationFileChooser : public base::SupportsUserData::Data {
 public:
  static void* UserDataKey() {
    static int key = 0;
    return &key;
  }

  PendingAutomationFileChooser(
      scoped_refptr<content::FileSelectListener> listener,
      blink::mojom::FileChooserParams::Mode mode)
      : listener_(std::move(listener)), mode_(mode) {}

  scoped_refptr<content::FileSelectListener> TakeListener() {
    return std::move(listener_);
  }
  blink::mojom::FileChooserParams::Mode mode() const { return mode_; }

 private:
  scoped_refptr<content::FileSelectListener> listener_;
  blink::mojom::FileChooserParams::Mode mode_;
};

}  // namespace

// Called from Browser::RunFileChooser (patched via
// apply_chromium_src_overrides.py). Returns true when the chooser was
// consumed by automation and the native dialog must be suppressed.
}  // namespace

namespace maho {

bool MahoBeginAutomationFileChooser(
    content::RenderFrameHost* render_frame_host,
    scoped_refptr<content::FileSelectListener> listener,
    blink::mojom::FileChooserParams::Mode mode) {
  if (!render_frame_host) {
    return false;
  }
  content::WebContents* web_contents =
      content::WebContents::FromRenderFrameHost(render_frame_host);
  if (!web_contents) {
    return false;
  }
  auto* lease_registry =
      maho::MahoMcpSession::GetLeaseRegistryForBrowserActions();
  if (!lease_registry ||
      !lease_registry->HasActiveLease(McpSessionIdOf(web_contents))) {
    return false;
  }
  // A previous pending chooser on the same tab is superseded; cancel its
  // listener so the page does not stay blocked forever.
  auto* previous = static_cast<PendingAutomationFileChooser*>(
      web_contents->GetUserData(PendingAutomationFileChooser::UserDataKey()));
  if (previous) {
    scoped_refptr<content::FileSelectListener> stale = previous->TakeListener();
    if (stale) {
      stale->FileSelectionCanceled();
    }
    web_contents->RemoveUserData(PendingAutomationFileChooser::UserDataKey());
  }
  web_contents->SetUserData(
      PendingAutomationFileChooser::UserDataKey(),
      std::make_unique<PendingAutomationFileChooser>(std::move(listener),
                                                     mode));
  return true;
}

}  // namespace maho

namespace {

// Completes the pending automation chooser for |web_contents| with |path|.
// Returns false when no chooser is pending.
bool MahoCompleteAutomationFileChooser(content::WebContents* web_contents,
                                       const base::FilePath& path) {
  auto* pending = static_cast<PendingAutomationFileChooser*>(
      web_contents->GetUserData(PendingAutomationFileChooser::UserDataKey()));
  if (!pending) {
    return false;
  }
  scoped_refptr<content::FileSelectListener> listener = pending->TakeListener();
  const blink::mojom::FileChooserParams::Mode mode = pending->mode();
  web_contents->RemoveUserData(PendingAutomationFileChooser::UserDataKey());
  if (!listener) {
    return false;
  }
  std::vector<blink::mojom::FileChooserFileInfoPtr> files;
  files.push_back(blink::mojom::FileChooserFileInfo::NewNativeFile(
      blink::mojom::NativeFileInfo::New(
          path, path.BaseName().AsUTF16Unsafe(), std::vector<std::u16string>())));
  listener->FileSelected(std::move(files), base::FilePath(), mode);
  return true;
}

bool IsMcpSystemSurfaceUrl(const std::string& raw_url) {
  GURL url(raw_url);
  if (!url.is_valid() || url.is_empty()) {
    return true;
  }
  if (url.SchemeIs("about")) {
    return true;
  }
  if (url.SchemeIs("chrome")) {
    return url.host() == "newtab" || url.host() == "downloads" ||
           url.host() == "history";
  }
  return false;
}

bool IsMcpGhostTab(const std::string& raw_url, const std::string& title) {
  return raw_url.empty() && title.empty();
}

std::string GetMcpActiveSpaceIdJson(BrowserWindowInterface* browser,
                                    MahoCore* core) {
  std::string active_space_id;
  if (auto* bridge = maho::MahoSpaceProfileBridge::GetInstance()) {
    active_space_id = bridge->GetActiveSpaceId(browser);
  }
  if (active_space_id.empty() && core) {
    active_space_id = DecodeActiveSpaceId(maho_core_get_active_space_id(core));
  }
  return active_space_id.empty() ? std::string()
                                 : base::GetQuotedJSONString(active_space_id);
}

maho::MahoMcpSession::TabInfo BuildLiveMcpTabInfo(
    BrowserWindowInterface* browser,
    TabStripModel* model,
    int index) {
  maho::MahoMcpSession::TabInfo tab;
  content::WebContents* web_contents =
      model ? model->GetWebContentsAt(index) : nullptr;
  if (!web_contents || !IsWebContentsMcpEligible(browser, web_contents)) {
    return tab;
  }

  if (auto* stable_helper = MahoTabIdHelper::FromWebContents(web_contents)) {
    tab.stable_id = stable_helper->stable_tab_id();
  }
  tab.id = McpSessionIdOf(web_contents);
  tab.title = base::UTF16ToUTF8(web_contents->GetTitle());
  tab.url = web_contents->GetLastCommittedURL().spec();
  tab.is_active = model && model->active_index() == index;
  tab.targetable = tab.id != 0;
  tab.tab_strip_index = index;
  return tab;
}

std::map<std::string, maho::MahoMcpSession::TabInfo> BuildLiveMcpTabsByStableId(
    BrowserWindowInterface* browser,
    TabStripModel* model) {
  std::map<std::string, maho::MahoMcpSession::TabInfo> live_tabs;
  if (!browser || !model) {
    return live_tabs;
  }
  for (int i = 0; i < model->count(); ++i) {
    maho::MahoMcpSession::TabInfo tab = BuildLiveMcpTabInfo(browser, model, i);
    if (!tab.stable_id.empty() && tab.targetable) {
      live_tabs[tab.stable_id] = std::move(tab);
    }
  }
  return live_tabs;
}

void AppendMcpTabsFromSidebarNode(
    const base::Value& node_value,
    const std::map<std::string, maho::MahoMcpSession::TabInfo>& live_tabs,
    std::set<std::string>* seen_stable_ids,
    std::vector<maho::MahoMcpSession::TabInfo>* tabs) {
  const auto* dict = node_value.GetIfDict();
  if (!dict) {
    return;
  }

  const std::string* kind = dict->FindString("kind");
  if (!kind) {
    return;
  }

  if (*kind == "folder") {
    if (const auto* children = dict->FindList("children")) {
      for (const auto& child : *children) {
        AppendMcpTabsFromSidebarNode(child, live_tabs, seen_stable_ids, tabs);
      }
    }
    return;
  }

  if (*kind != "tab") {
    return;
  }

  const std::string* stable_id = dict->FindString("id");
  if (!stable_id || stable_id->empty()) {
    return;
  }
  if (!seen_stable_ids->insert(*stable_id).second) {
    return;
  }
  if (dict->FindBool("isPrivate").value_or(false)) {
    return;
  }
  const std::string* lifecycle = dict->FindString("lifecycleState");
  if (lifecycle && *lifecycle == "archived") {
    return;
  }

  auto live_it = live_tabs.find(*stable_id);
  const bool is_live_active =
      live_it != live_tabs.end() && live_it->second.is_active;
  const std::string* raw_title = dict->FindString("customTitle");
  if (!raw_title || raw_title->empty()) {
    raw_title = dict->FindString("title");
  }
  const std::string title = raw_title ? *raw_title : std::string();
  const std::string* raw_url = dict->FindString("url");
  const std::string url = raw_url ? *raw_url : std::string();
  if (!is_live_active &&
      (IsMcpGhostTab(url, title) || IsMcpSystemSurfaceUrl(url))) {
    return;
  }

  maho::MahoMcpSession::TabInfo tab;
  if (live_it != live_tabs.end()) {
    tab = live_it->second;
  } else {
    tab.id = 0;
    tab.targetable = false;
    tab.tab_strip_index = -1;
    tab.is_active = false;
  }
  tab.stable_id = *stable_id;
  if (!tab.targetable) {
    tab.title = title;
    tab.url = url;
  }
  tabs->push_back(std::move(tab));
}

std::vector<maho::MahoMcpSession::TabInfo> BuildMcpSidebarTabInventory(
    BrowserWindowInterface* browser,
    TabStripModel* model,
    MahoCore* core) {
  std::vector<maho::MahoMcpSession::TabInfo> tabs;
  if (!core) {
    return tabs;
  }

  std::string space_id_json = GetMcpActiveSpaceIdJson(browser, core);
  if (space_id_json.empty()) {
    return tabs;
  }

  char* raw = maho_core_get_sidebar_state_v2(core, space_id_json.c_str());
  if (!raw) {
    return tabs;
  }
  std::string sidebar_json(raw);
  maho_string_free(raw);

  std::optional<base::Value> parsed =
      base::JSONReader::Read(sidebar_json, base::JSON_PARSE_RFC);
  const auto* root = parsed ? parsed->GetIfDict() : nullptr;
  const auto* tree = root ? root->FindList("tree") : nullptr;
  if (!tree) {
    return tabs;
  }

  const auto live_tabs = BuildLiveMcpTabsByStableId(browser, model);
  std::set<std::string> seen_stable_ids;
  for (const auto& node : *tree) {
    AppendMcpTabsFromSidebarNode(node, live_tabs, &seen_stable_ids, &tabs);
  }
  return tabs;
}

std::vector<maho::MahoMcpSession::TabInfo> BuildLiveMcpTabStripInventory(
    BrowserWindowInterface* browser,
    TabStripModel* model) {
  std::vector<maho::MahoMcpSession::TabInfo> tabs;
  if (!browser || !model) {
    return tabs;
  }
  for (int i = 0; i < model->count(); ++i) {
    maho::MahoMcpSession::TabInfo tab = BuildLiveMcpTabInfo(browser, model, i);
    if (tab.targetable) {
      tabs.push_back(std::move(tab));
    }
  }
  return tabs;
}

bool IsInputValueMatching(const std::string& observed,
                          const std::string& target,
                          bool is_protected) {
  // 1. Exact match
  if (observed == target) {
    return true;
  }

  // 2. Whitespace trimmed match
  std::string trimmed_obs;
  std::string trimmed_tgt;
  base::TrimWhitespaceASCII(observed, base::TRIM_ALL, &trimmed_obs);
  base::TrimWhitespaceASCII(target, base::TRIM_ALL, &trimmed_tgt);
  if (trimmed_obs == trimmed_tgt) {
    return true;
  }

  // 3. Case-insensitive fallback
  if (base::EqualsCaseInsensitiveASCII(observed, target) ||
      base::EqualsCaseInsensitiveASCII(trimmed_obs, trimmed_tgt)) {
    return true;
  }

  // 4. Protected password/PIN field handling
  if (is_protected && !target.empty()) {
    if (observed.length() == target.length()) {
      return true;
    }
    const std::u16string obs_u16 = base::UTF8ToUTF16(observed);
    const std::u16string tgt_u16 = base::UTF8ToUTF16(target);
    if (obs_u16.length() == tgt_u16.length()) {
      return true;
    }
    if (!obs_u16.empty()) {
      bool all_bullets = true;
      for (char16_t c : obs_u16) {
        if (c != u'*' && c != 0x2022 && c != 0x25CF && c != 0x00B7 &&
            c != 0x25E6 && c != 0x2219 && c != 0xFF0A && c != 0x2981) {
          all_bullets = false;
          break;
        }
      }
      if (all_bullets) {
        return true;
      }
    }
  }

  // 5. Phone number / numeric / formatting normalization:
  // Strip non-digit characters (e.g. '-', ' ', '(', ')', '.', '/', '+').
  // If target contains digits and the stripped digits match (or international
  // prefix +82 vs 010 matches), treat as matched.
  std::string obs_digits;
  obs_digits.reserve(observed.size());
  for (char c : observed) {
    if (base::IsAsciiDigit(c)) {
      obs_digits.push_back(c);
    }
  }

  std::string tgt_digits;
  tgt_digits.reserve(target.size());
  for (char c : target) {
    if (base::IsAsciiDigit(c)) {
      tgt_digits.push_back(c);
    }
  }

  if (!tgt_digits.empty()) {
    if (obs_digits == tgt_digits) {
      return true;
    }
    // International prefix +82 vs 010 (or 82 vs 0)
    if (base::StartsWith(tgt_digits, "0") &&
        base::StartsWith(obs_digits, "82") &&
        tgt_digits.substr(1) == obs_digits.substr(2)) {
      return true;
    }
    if (base::StartsWith(obs_digits, "0") &&
        base::StartsWith(tgt_digits, "82") &&
        obs_digits.substr(1) == tgt_digits.substr(2)) {
      return true;
    }
  }

  // 6. Formatting characters stripped comparison (e.g., spaces, dashes, commas, slashes)
  auto strip_formatting = [](const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
      if (c != '-' && c != ' ' && c != '(' && c != ')' && c != '.' &&
          c != '/' && c != '+' && c != ',' && c != ':' && c != '$' &&
          c != '#' && c != '%' && c != '_') {
        out.push_back(c);
      }
    }
    return out;
  };
  std::string stripped_obs = strip_formatting(observed);
  std::string stripped_tgt = strip_formatting(target);
  if (!stripped_tgt.empty() &&
      (stripped_obs == stripped_tgt ||
       base::EqualsCaseInsensitiveASCII(stripped_obs, stripped_tgt))) {
    return true;
  }

  return false;
}

void ReadAxNodeValue(const ui::AXTreeUpdate& tree_update,
                     ui::AXNodeID node_id,
                     std::string* out_value,
                     bool* out_found,
                     bool* out_editable,
                     bool* out_protected = nullptr) {
  *out_found = false;
  *out_editable = false;
  if (out_protected) {
    *out_protected = false;
  }
  out_value->clear();
  for (const ui::AXNodeData& node : tree_update.nodes) {
    if (node.id == node_id) {
      *out_found = true;
      *out_editable =
          (node.role == ax::mojom::Role::kTextField ||
           node.role == ax::mojom::Role::kTextFieldWithComboBox ||
           node.role == ax::mojom::Role::kSearchBox ||
           node.role == ax::mojom::Role::kComboBoxMenuButton ||
           node.role == ax::mojom::Role::kComboBoxGrouping ||
           node.role == ax::mojom::Role::kListBox ||
           node.role == ax::mojom::Role::kSpinButton ||
           node.HasState(ax::mojom::State::kEditable) ||
           node.HasState(ax::mojom::State::kRichlyEditable));
      if (out_protected) {
        *out_protected = node.HasState(ax::mojom::State::kProtected);
      }
      if (node.HasStringAttribute(ax::mojom::StringAttribute::kValue)) {
        *out_value =
            node.GetStringAttribute(ax::mojom::StringAttribute::kValue);
      }
      if (out_value->empty()) {
        for (const auto& attr : node.html_attributes) {
          if (attr.first == "value" && !attr.second.empty()) {
            *out_value = attr.second;
            break;
          }
        }
      }
      if (out_value->empty() && !node.child_ids.empty()) {
        for (ui::AXNodeID child_id : node.child_ids) {
          for (const ui::AXNodeData& child_node : tree_update.nodes) {
            if (child_node.id == child_id) {
              if (child_node.HasStringAttribute(ax::mojom::StringAttribute::kName)) {
                *out_value = child_node.GetStringAttribute(ax::mojom::StringAttribute::kName);
              } else if (child_node.HasStringAttribute(ax::mojom::StringAttribute::kValue)) {
                *out_value = child_node.GetStringAttribute(ax::mojom::StringAttribute::kValue);
              }
              break;
            }
          }
          if (!out_value->empty()) {
            break;
          }
        }
      }
      break;
    }
  }
}

class MahoMcpBrowserDelegateImpl : public maho::MahoMcpBrowserDelegate {
 public:
  MahoMcpBrowserDelegateImpl() {
    ui_weak_ptr_ = weak_factory_.GetWeakPtr();
    vault_lock_state_subscription_ =
        maho::AddVaultLockStateChangeCallback(base::BindRepeating(
            &MahoMcpBrowserDelegateImpl::OnVaultLockStateChanged,
            base::Unretained(this)));
  }
  ~MahoMcpBrowserDelegateImpl() override = default;

  void PublishControlActivity(const std::string& activity_id,
                              const std::string& controller_session_id,
                              const std::string& controller_label,
                              const maho::ResolvedMahoMcpTarget& target,
                              MahoBrowserToolRegistry::Category category,
                              MahoBrowserToolRegistry::Sensitivity sensitivity,
                              maho::MahoMcpActivityPhase phase,
                              uint64_t revision) override {
    if (!content::BrowserThread::CurrentlyOn(content::BrowserThread::UI)) {
      content::GetUIThreadTaskRunner({})->PostTask(
          FROM_HERE,
          base::BindOnce(&MahoMcpBrowserDelegateImpl::PublishControlActivity,
                         ui_weak_ptr_, activity_id, controller_session_id,
                         controller_label, target, category, sensitivity, phase,
                         revision));
      return;
    }
    auto* service =
        maho::ai::MahoControlActivityService::GetForProfile(GetProfile());
    if (!service) {
      return;
    }
    if (phase == maho::MahoMcpActivityPhase::kReading ||
        phase == maho::MahoMcpActivityPhase::kActing) {
      maho::ai::StartControlActivityParams params;
      params.session_id = activity_id;
      params.controller_session_id = controller_session_id;
      params.controller_display_name = controller_label;
      params.controller_type = maho::ai::ControllerType::kRemoteClient;
      params.control_plane = maho::ai::ControlPlane::kMcp;
      switch (category) {
        case MahoBrowserToolRegistry::Category::kInput:
          params.category = maho::ai::ActivityCategory::kFormEntry;
          break;
        case MahoBrowserToolRegistry::Category::kMail:
        case MahoBrowserToolRegistry::Category::kVault:
          params.category = maho::ai::ActivityCategory::kAccountChange;
          break;
        case MahoBrowserToolRegistry::Category::kUnknown:
          params.category = maho::ai::ActivityCategory::kOther;
          break;
        default:
          params.category = maho::ai::ActivityCategory::kBrowsing;
          break;
      }
      switch (sensitivity) {
        case MahoBrowserToolRegistry::Sensitivity::kCredential:
          params.sensitivity = maho::ai::ActivitySensitivity::kHigh;
          break;
        case MahoBrowserToolRegistry::Sensitivity::kSensitive:
          params.sensitivity = maho::ai::ActivitySensitivity::kMedium;
          break;
        case MahoBrowserToolRegistry::Sensitivity::kUnknown:
        case MahoBrowserToolRegistry::Sensitivity::kLow:
          params.sensitivity = maho::ai::ActivitySensitivity::kLow;
          break;
      }
      params.start_receipt = {maho::ai::ActivityReceiptKind::kStart, "started",
                              "[redacted]"};
      if (target.valid) {
        ForEachCurrentBrowserWindowInterfaceOrderedByActivation(
            [&params, &target](BrowserWindowInterface* browser) {
              if (!browser ||
                  browser->GetSessionID().id() != target.browser_id ||
                  !browser->GetTabStripModel()) {
                return true;
              }
              TabStripModel* model = browser->GetTabStripModel();
              for (int index = 0; index < model->count(); ++index) {
                content::WebContents* contents = model->GetWebContentsAt(index);
                if (McpSessionIdOf(contents) == target.tab_id) {
                  auto* helper =
                      sessions::SessionTabHelper::FromWebContents(contents);
                  const int64_t canonical_tab_id =
                      helper ? helper->session_id().id() : target.tab_id;
                  params.target =
                      maho::ai::ControlTarget{target.browser_id, canonical_tab_id,
                                              contents->GetPrimaryMainFrame()
                                                  ->GetLastCommittedOrigin(),
                                              contents->GetTitle()};
                  return false;
                }
              }
              return false;
            });
      }
      service->StartActivity(std::move(params));
      if (phase == maho::MahoMcpActivityPhase::kActing) {
        maho::ai::ControlActivityUpdate update;
        update.event_revision = 2;
        update.state = maho::ai::ControlActivityState::kActing;
        service->ApplyUpdate(activity_id, std::move(update));
      }
      return;
    }
    if (phase == maho::MahoMcpActivityPhase::kDisconnected) {
      content::GetUIThreadTaskRunner({})->PostDelayedTask(
          FROM_HERE,
          base::BindOnce(
              [](base::WeakPtr<MahoMcpBrowserDelegateImpl> delegate,
                 std::string act_id, uint64_t rev) {
                if (!delegate) return;
                auto* s = maho::ai::MahoControlActivityService::GetForProfile(
                    delegate->GetProfile());
                if (s) {
                  s->Disconnect(act_id, rev);
                }
              },
              ui_weak_ptr_, activity_id, revision),
          base::Seconds(3));
      return;
    }
    if (phase == maho::MahoMcpActivityPhase::kCompleted) {
      // Hold the controlled visual state for a brief grace window so the user
      // can clearly observe which tab was manipulated by the agent.
      content::GetUIThreadTaskRunner({})->PostDelayedTask(
          FROM_HERE,
          base::BindOnce(
              [](base::WeakPtr<MahoMcpBrowserDelegateImpl> delegate,
                 std::string act_id) {
                if (!delegate) return;
                delegate->PublishControlActivity(
                    act_id, /*controller_session_id=*/"",
                    /*controller_label=*/"",
                    /*target=*/maho::ResolvedMahoMcpTarget{},
                    /*category=*/MahoBrowserToolRegistry::Category::kUnknown,
                    /*sensitivity=*/
                    MahoBrowserToolRegistry::Sensitivity::kLow,
                    /*phase=*/maho::MahoMcpActivityPhase::kDisconnected,
                    /*revision=*/0);
              },
              ui_weak_ptr_, activity_id),
          base::Seconds(3));
      return;
    }
    maho::ai::ControlActivityUpdate update;
    const auto activity = service->GetActivity(activity_id);
    if (!activity) {
      return;
    }
    update.event_revision = activity->event_revision + 1;
    update.state = phase == maho::MahoMcpActivityPhase::kCompleted
                       ? maho::ai::ControlActivityState::kCompleted
                       : maho::ai::ControlActivityState::kFailed;
    update.receipt = maho::ai::ActivityReceipt{
        phase == maho::MahoMcpActivityPhase::kCompleted
            ? maho::ai::ActivityReceiptKind::kResult
            : maho::ai::ActivityReceiptKind::kFailure,
        phase == maho::MahoMcpActivityPhase::kCompleted ? "completed"
                                                        : "failed",
        "[redacted]"};
    service->ApplyUpdate(activity_id, std::move(update));
  }

  void RevokeVaultCredentialLeases() {
    vault_credential_expiry_timer_.Stop();
    vault_credential_handles_.clear();
    vault_credential_grants_.clear();
  }

  void AddVaultCredentialLeaseForTesting(base::TimeTicks expires_at,
                                         bool promoted,
                                         Profile* lease_profile) {
    BrowserVaultCredentialLease lease;
    lease.profile_key =
        lease_profile ? maho::GetProfileIdentityKey(lease_profile->GetPath(),
                                                    lease_profile->GetPrefs())
                      : "test-profile";
    lease.origin = "https://test.example/";
    lease.expires_at = expires_at;
    lease.password = "test-secret";
    if (promoted) {
      vault_credential_grants_.emplace("test-grant", std::move(lease));
    } else {
      vault_credential_handles_.emplace("test-handle", std::move(lease));
    }
    ScheduleVaultCredentialExpiry();
  }

  void SweepVaultCredentialLeasesForTesting(base::TimeTicks now) {
    SweepExpiredVaultCredentialLeases(now);
  }

  bool PromoteVaultCredentialLeaseForTesting(Profile* resolved_profile,
                                             base::TimeTicks now) {
    SweepExpiredVaultCredentialLeases(now);
    auto it = vault_credential_handles_.find("test-handle");
    if (it == vault_credential_handles_.end() ||
        !IsVaultCredentialLeaseProfileAllowed(it->second, resolved_profile) ||
        it->second.expires_at <= now) {
      return false;
    }
    BrowserVaultCredentialLease lease = std::move(it->second);
    vault_credential_handles_.erase(it);
    vault_credential_grants_.emplace("test-grant", std::move(lease));
    ScheduleVaultCredentialExpiry();
    return true;
  }

  size_t VaultCredentialLeaseCountForTesting() const {
    return vault_credential_handles_.size() + vault_credential_grants_.size();
  }

std::string FormatMailActionDetails(const std::string& action_name,
                                    const std::string& request_json) {
  std::optional<base::Value> parsed =
      base::JSONReader::Read(request_json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return request_json;
  }
  const base::DictValue& dict = parsed->GetDict();
  std::string formatted;

  if (action_name == "Add Mail Account") {
    if (const std::string* email = dict.FindString("email")) {
      formatted += "• Account: " + *email + "\n";
    }
    if (const std::string* name = dict.FindString("display_name")) {
      formatted += "• Name: " + *name + "\n";
    }
    if (const std::string* host = dict.FindString("imap_host")) {
      formatted += "• IMAP: " + *host;
      if (std::optional<int> port = dict.FindInt("imap_port")) {
        formatted += ":" + base::NumberToString(*port);
      }
      formatted += "\n";
    }
    if (const std::string* host = dict.FindString("smtp_host")) {
      formatted += "• SMTP: " + *host;
      if (std::optional<int> port = dict.FindInt("smtp_port")) {
        formatted += ":" + base::NumberToString(*port);
      }
      formatted += "\n";
    }
    if (dict.FindString("password")) {
      formatted += "• Password: ••••••••\n";
    }
  } else if (action_name == "Send Email" || action_name == "Save Mail Draft" || action_name == "Queue Email") {
    if (const std::string* to = dict.FindString("to")) {
      formatted += "• To: " + *to + "\n";
    }
    if (const std::string* subject = dict.FindString("subject")) {
      formatted += "• Subject: " + *subject + "\n";
    }
  }

  if (formatted.empty()) {
    base::DictValue sanitized = dict.Clone();
    if (sanitized.FindString("password")) {
      sanitized.Set("password", "••••••••");
    }
    if (sanitized.FindString("oauth2_access_token")) {
      sanitized.Set("oauth2_access_token", "[REDACTED]");
    }
    if (sanitized.FindString("oauth2_refresh_token")) {
      sanitized.Set("oauth2_refresh_token", "[REDACTED]");
    }
    base::JSONWriter::WriteWithOptions(
        sanitized, base::JSONWriter::OPTIONS_PRETTY_PRINT, &formatted);
  }

  return formatted;
}

  bool ConfirmMailWriteAction(const std::string& action_name,
                              const std::string& details) {
    std::string title = "Maho Mail - Permission Requested";
    std::string message =
        "An AI agent is requesting permission to perform the following "
        "action:\n\n[" +
        action_name + "]\nDetails:\n" + details +
        "\n\nDo you allow this action?";
    chrome::MessageBoxResult result = chrome::ShowQuestionMessageBoxSync(
        gfx::NativeWindow(), base::UTF8ToUTF16(title),
        base::UTF8ToUTF16(message));
    return result == chrome::MESSAGE_BOX_RESULT_YES;
  }

  maho::ai::MailAuthorizationContext GetMailAuthorizationContext() override {
    maho::ai::MailAuthorizationContext context;
    Profile* profile = GetProfile();
    maho::MahoMailService* service = GetMailService();
    if (!profile) {
      return context;
    }
    PrefService* prefs = profile->GetPrefs();
    context.feature_enabled = maho::sidebar_prefs::IsMahoMailEnabled(prefs);
    if (service) {
      const auto helper_state = service->lifecycle_state();
      context.helper_ready =
          helper_state == maho::MahoMailService::LifecycleState::kReady;
      context.helper_starting =
          helper_state == maho::MahoMailService::LifecycleState::kStarting;
      context.helper_generation = service->generation();
    }
    context.read_allowed = prefs->GetBoolean(maho::ai_prefs::kMailReadAllowed);
    const std::string policy =
        prefs->GetString(maho::ai_prefs::kApprovalPolicy);
    context.global_policy = policy == "deny" ? maho::ai::MailGlobalPolicy::kDeny
                            : policy == "allow"
                                ? maho::ai::MailGlobalPolicy::kAllow
                                : maho::ai::MailGlobalPolicy::kPrompt;
    return context;
  }

  bool ConfirmMailToolApproval(std::string_view tool_name,
                               std::string_view redacted_arguments) override {
    return ConfirmMailWriteAction(std::string(tool_name),
                                  std::string(redacted_arguments));
  }

  bool ConfirmCredentialTypingApproval(
      std::string_view tool_name,
      const maho::ResolvedMahoMcpTarget& target) override {
    const std::string title = "Maho Browser - Credential Permission Requested";
    const std::string details =
        "Credential typing into browser " +
        base::NumberToString(target.browser_id) + ", tab " +
        base::NumberToString(target.tab_id) +
        ". The credential value is intentionally not shown.";
    const std::string message =
        "An AI agent is requesting permission to perform the following "
        "credential action:\n\n[" +
        std::string(tool_name) + "]\nDetails:\n" + details +
        "\n\nDo you allow this action?";
    const chrome::MessageBoxResult result = chrome::ShowQuestionMessageBoxSync(
        gfx::NativeWindow(), base::UTF8ToUTF16(title),
        base::UTF8ToUTF16(message));
    return result == chrome::MESSAGE_BOX_RESULT_YES;
  }

  bool ConfirmBrowserActionApproval(
      std::string_view tool_name,
      const maho::ResolvedMahoMcpTarget& target) override {
    return ConfirmMailWriteAction(
        std::string(tool_name),
        "Browser " + base::NumberToString(target.browser_id) + ", tab " +
            base::NumberToString(target.tab_id));
  }

  maho::MahoMcpFeatureGates GetFeatureGates() override {
    Profile* profile = GetProfile();
    PrefService* prefs = profile ? profile->GetPrefs() : nullptr;
    const int32_t tier =
        maho::GetCore() ? maho_account_get_tier(maho::GetCore()) : 0;
    return {
        .mail_enabled =
            prefs && maho::sidebar_prefs::IsMahoMailEnabled(prefs),
        .routines_enabled = tier >= 2,
        .vault_enabled = maho::GetCore() != nullptr,
    };
  }

  std::string ListArtifacts(std::string_view session_id) override {
    maho::ai::MahoArtifactRegistry* registry =
        maho::ai::MahoArtifactRegistry::GetForProfile(GetProfile());
    if (!registry) {
      return "[]";
    }
    base::ListValue list;
    for (const maho::ai::MahoArtifact& artifact :
         registry->ListArtifacts(session_id)) {
      base::DictValue item;
      item.Set("artifact_id", artifact.artifact_id);
      item.Set("session_id", artifact.session_id);
      item.Set("display_name", artifact.display_name);
      item.Set("mime_type", artifact.mime_type);
      item.Set("size_bytes", static_cast<double>(artifact.size_bytes));
      item.Set("created_at_ms", static_cast<double>(artifact.created_at_ms));
      list.Append(std::move(item));
    }
    std::string json;
    base::JSONWriter::Write(list, &json);
    return json;
  }

  void ExportArtifact(
      std::string artifact_id,
      base::FilePath destination,
      maho::MahoMcpBrowserDelegate::ExportArtifactCallback callback) override {
    maho::ai::MahoArtifactRegistry* registry =
        maho::ai::MahoArtifactRegistry::GetForProfile(GetProfile());
    std::optional<maho::ai::MahoArtifact> artifact =
        registry ? registry->GetArtifact(artifact_id) : std::nullopt;
    if (!registry || !artifact || !destination.IsAbsolute()) {
      std::move(callback).Run(false, "Artifact or destination is unavailable");
      return;
    }
    base::FilePath artifact_root = registry->artifact_root();
    base::ThreadPool::PostTaskAndReplyWithResult(
        FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
        base::BindOnce(
            [](base::FilePath artifact_root, std::string storage_rel_path,
               base::FilePath destination) -> std::string {
              const base::FilePath parent = destination.DirName();
              base::FilePath normalized_parent;
              if (parent.empty() ||
                  !base::NormalizeFilePath(parent, &normalized_parent) ||
                  normalized_parent != parent) {
                return "Artifact export destination parent is unavailable";
              }
              if (base::PathExists(destination)) {
                base::FilePath normalized_destination;
                if (!base::NormalizeFilePath(destination,
                                             &normalized_destination) ||
                    normalized_destination != destination ||
                    !base::PathIsWritable(destination) ||
                    base::DirectoryExists(destination)) {
                  return "Artifact export destination is unsafe";
                }
              }
              std::optional<base::FilePath> source =
                  maho::ai::MahoArtifactRegistry::ResolveContainedStoragePath(
                      artifact_root, storage_rel_path);
              if (!source || !base::CopyFile(*source, destination)) {
                return "Artifact export failed";
              }
              return std::string();
            },
            std::move(artifact_root), artifact->storage_rel_path,
            std::move(destination)),
        base::BindOnce(
            [](maho::MahoMcpBrowserDelegate::ExportArtifactCallback callback,
               std::string error) {
              std::move(callback).Run(error.empty(), std::move(error));
            },
            std::move(callback)));
  }

  std::string ListRoutines() override {
    MahoCore* core = maho::GetCore();
    if (!core) {
      return "[]";
    }
    char* json = maho_routines_list_all(core);
    if (!json) {
      return "[]";
    }
    std::string out(json);
    maho_string_free(json);
    return out;
  }

  void RunRoutine(
      const std::string& id,
      maho::MahoMcpBrowserDelegate::RunRoutineCallback callback) override {
    MahoCore* core = maho::GetCore();
    if (!core) {
      std::move(callback).Run(
          maho::MahoMcpBrowserDelegate::RoutineRunError::kUnavailable,
          "Maho core unavailable");
      return;
    }
    int32_t tier = maho_account_get_tier(core);
    if (tier != 2) {
      std::move(callback).Run(
          maho::MahoMcpBrowserDelegate::RoutineRunError::kTierLocked,
          "Routines are available on Max tier only");
      return;
    }
    auto* ctx = new RunRoutineFfiCtx{
        std::move(callback), base::SequencedTaskRunner::GetCurrentDefault()};
    maho_routines_run(core, id.c_str(), tier, &OnRoutineRunComplete,
                      &OnRoutineRunError, ctx);
  }

  void MailListAccounts(
      maho::MahoMcpBrowserDelegate::MailReadCallback callback) override {
    maho::MahoMailService* service = GetMailService();
    if (!service) {
      std::move(callback).Run(false, "Mail service unavailable");
      return;
    }
    service->ListAccounts(std::move(callback));
  }

  void MailListFolders(
      const std::string& account_id,
      maho::MahoMcpBrowserDelegate::MailReadCallback callback) override {
    maho::MahoMailService* service = GetMailService();
    if (!service) {
      std::move(callback).Run(false, "Mail service unavailable");
      return;
    }
    service->ListFolders(account_id, std::move(callback));
  }

  void MailListEmails(
      const std::string& account_id,
      const std::string& folder_id,
      int64_t limit,
      int64_t offset,
      maho::MahoMcpBrowserDelegate::MailReadCallback callback) override {
    maho::MahoMailService* service = GetMailService();
    if (!service) {
      std::move(callback).Run(false, "Mail service unavailable");
      return;
    }
    service->ListEmails(account_id, folder_id, limit, offset,
                        std::move(callback));
  }

  void MailGetEmail(
      const std::string& email_id,
      maho::MahoMcpBrowserDelegate::MailReadCallback callback) override {
    maho::MahoMailService* service = GetMailService();
    if (!service) {
      std::move(callback).Run(false, "Mail service unavailable");
      return;
    }
    service->GetEmail(email_id, std::move(callback));
  }

  void MailSearchEmails(
      const std::string& query_json,
      maho::MahoMcpBrowserDelegate::MailReadCallback callback) override {
    maho::MahoMailService* service = GetMailService();
    if (!service) {
      std::move(callback).Run(false, "Mail service unavailable");
      return;
    }
    service->SearchEmails(query_json, std::move(callback));
  }

  void MailListThread(
      const std::string& account_id,
      const std::string& message_id,
      maho::MahoMcpBrowserDelegate::MailReadCallback callback) override {
    maho::MahoMailService* service = GetMailService();
    if (!service) {
      std::move(callback).Run(false, "Mail service unavailable");
      return;
    }
    service->ListThread(account_id, message_id, std::move(callback));
  }

  void MailAddAccount(
      const std::string& request_json,
      maho::MahoMcpBrowserDelegate::MailReadCallback callback) override {
    if (!ConfirmMailWriteAction("Add Mail Account",
                                FormatMailActionDetails("Add Mail Account", request_json))) {
      std::move(callback).Run(false, "Permission denied by user");
      return;
    }
    maho::MahoMailService* service = GetMailService();
    if (!service) {
      std::move(callback).Run(false, "Mail service unavailable");
      return;
    }
    service->AddAccount(request_json, std::move(callback));
  }

  void MailTestConnection(
      const std::string& params_json,
      maho::MahoMcpBrowserDelegate::MailReadCallback callback) override {
    maho::MahoMailService* service = GetMailService();
    if (!service) {
      std::move(callback).Run(false, "Mail service unavailable");
      return;
    }
    service->TestConnection(params_json, std::move(callback));
  }

  void MailDeleteAccount(
      const std::string& account_id,
      maho::MahoMcpBrowserDelegate::MailReadCallback callback) override {
    if (!ConfirmMailWriteAction("Delete Mail Account",
                                "Account ID: " + account_id)) {
      std::move(callback).Run(false, "Permission denied by user");
      return;
    }
    maho::MahoMailService* service = GetMailService();
    if (!service) {
      std::move(callback).Run(false, "Mail service unavailable");
      return;
    }
    service->DeleteAccount(account_id, std::move(callback));
  }

  void MailOAuthStartUrl(
      const std::string& provider,
      const std::string& client_id,
      const std::string& redirect_uri,
      maho::MahoMcpBrowserDelegate::MailReadCallback callback) override {
    maho::MahoMailService* service = GetMailService();
    if (!service) {
      std::move(callback).Run(false, "Mail service unavailable");
      return;
    }
    service->OAuthStartUrl(provider, client_id, redirect_uri, "{}",
                           std::move(callback));
  }

  void MailOAuthComplete(
      const std::string& state,
      const std::string& code,
      maho::MahoMcpBrowserDelegate::MailReadCallback callback) override {
    maho::MahoMailService* service = GetMailService();
    if (!service) {
      std::move(callback).Run(false, "Mail service unavailable");
      return;
    }
    service->OAuthComplete(state, code, std::move(callback));
  }

  void MailReconnectAccount(
      const std::string& account_id,
      maho::MahoMcpBrowserDelegate::MailReadCallback callback) override {
    if (!ConfirmMailWriteAction("Reconnect Mail Account",
                                "Account ID: " + account_id)) {
      std::move(callback).Run(false, "Permission denied by user");
      return;
    }
    maho::MahoMailService* service = GetMailService();
    if (!service) {
      std::move(callback).Run(false, "Mail service unavailable");
      return;
    }
    service->ReconnectAccount(account_id, std::move(callback));
  }

  void MailImportMigrationArchive(
      const std::string& archive_json,
      maho::MahoMcpBrowserDelegate::MailReadCallback callback) override {
    if (!ConfirmMailWriteAction("Import Migration Archive",
                                "Archive JSON: " + archive_json)) {
      std::move(callback).Run(false, "Permission denied by user");
      return;
    }
    maho::MahoMailService* service = GetMailService();
    if (!service) {
      std::move(callback).Run(false, "Mail service unavailable");
      return;
    }
    service->ImportMigrationArchive(archive_json, std::move(callback));
  }

  void MailExtractOtp(
      const std::string& account_id,
      const std::string& folder_id,
      const std::string& query,
      int64_t max_age_seconds,
      maho::MahoMcpBrowserDelegate::MailReadCallback callback) override {
    maho::MahoMailService* service = GetMailService();
    if (!service) {
      std::move(callback).Run(false, "Mail service unavailable");
      return;
    }
    service->ExtractOtp(account_id, folder_id, query, max_age_seconds,
                        std::move(callback));
  }

  void MailSendEmail(
      const std::string& request_json,
      bool already_authorized,
      maho::MahoMcpBrowserDelegate::MailReadCallback callback) override {
    if (!already_authorized &&
        !ConfirmMailWriteAction("Send Email",
                                FormatMailActionDetails("Send Email", request_json))) {
      std::move(callback).Run(false, "Permission denied by user");
      return;
    }
    maho::MahoMailService* service = GetMailService();
    if (!service) {
      std::move(callback).Run(false, "Mail service unavailable");
      return;
    }
    service->SendEmail(request_json, std::move(callback));
  }

  void MailSaveDraft(
      const std::string& request_json,
      bool already_authorized,
      maho::MahoMcpBrowserDelegate::MailReadCallback callback) override {
    if (!already_authorized &&
        !ConfirmMailWriteAction("Save Mail Draft",
                                FormatMailActionDetails("Save Mail Draft", request_json))) {
      std::move(callback).Run(false, "Permission denied by user");
      return;
    }
    maho::MahoMailService* service = GetMailService();
    if (!service) {
      std::move(callback).Run(false, "Mail service unavailable");
      return;
    }
    service->SaveDraft(request_json, std::move(callback));
  }

  void MailUpdateDraft(
      const std::string& draft_id,
      const std::string& request_json,
      bool already_authorized,
      maho::MahoMcpBrowserDelegate::MailReadCallback callback) override {
    if (!already_authorized &&
        !ConfirmMailWriteAction(
            "Update Mail Draft",
            "Draft ID: " + draft_id + "\nRequest JSON: " + request_json)) {
      std::move(callback).Run(false, "Permission denied by user");
      return;
    }
    maho::MahoMailService* service = GetMailService();
    if (!service) {
      std::move(callback).Run(false, "Mail service unavailable");
      return;
    }
    service->UpdateDraft(draft_id, request_json, std::move(callback));
  }

  void MailQueueEmail(
      const std::string& request_json,
      maho::MahoMcpBrowserDelegate::MailReadCallback callback) override {
    if (!ConfirmMailWriteAction("Queue Email",
                                FormatMailActionDetails("Queue Email", request_json))) {
      std::move(callback).Run(false, "Permission denied by user");
      return;
    }
    maho::MahoMailService* service = GetMailService();
    if (!service) {
      std::move(callback).Run(false, "Mail service unavailable");
      return;
    }
    service->QueueEmail(request_json, std::move(callback));
  }

  std::vector<maho::MahoMcpSession::TabInfo> GetTabList() override {
    std::vector<maho::MahoMcpSession::TabInfo> list;
    ForEachCurrentBrowserWindowInterfaceOrderedByActivation(
        [&list](BrowserWindowInterface* browser) {
          if (browser && browser->GetTabStripModel() &&
              IsBrowserMcpEligible(browser)) {
            TabStripModel* model = browser->GetTabStripModel();
            std::vector<maho::MahoMcpSession::TabInfo> live_tabs =
                BuildLiveMcpTabStripInventory(browser, model);
            if (!live_tabs.empty()) {
              for (auto& tab : live_tabs) {
                list.push_back(std::move(tab));
              }
              return true;
            }

            std::vector<maho::MahoMcpSession::TabInfo> sidebar_tabs =
                BuildMcpSidebarTabInventory(browser, model, maho::GetCore());
            for (auto& tab : sidebar_tabs) {
              list.push_back(std::move(tab));
            }
          }
          return true;
        });
    return list;
  }

  content::WebContents* GetActiveWebContents() {
    // R-8: consider only the frontmost (most-recently-active) window. If that
    // window is OTR/ineligible, the active target is OTR and callers must be
    // denied (-32005) rather than silently retargeted to another window.
    content::WebContents* active_wc = nullptr;
    bool inspected_frontmost = false;
    ForEachCurrentBrowserWindowInterfaceOrderedByActivation(
        [&active_wc, &inspected_frontmost](BrowserWindowInterface* browser) {
          if (inspected_frontmost) {
            return false;
          }
          inspected_frontmost = true;
          if (browser && browser->GetTabStripModel() &&
              IsBrowserMcpEligible(browser)) {
            content::WebContents* wc =
                browser->GetTabStripModel()->GetActiveWebContents();
            if (wc && IsWebContentsMcpEligible(browser, wc)) {
              active_wc = wc;
            }
          }
          return false;
        });
    return active_wc;
  }

  content::WebContents* GetWebContentsForTabId(int tab_id) {
    content::WebContents* match = nullptr;
    ForEachCurrentBrowserWindowInterfaceOrderedByActivation(
        [&match, tab_id](BrowserWindowInterface* browser) {
          if (browser && browser->GetTabStripModel() &&
              IsBrowserMcpEligible(browser)) {
            TabStripModel* model = browser->GetTabStripModel();
            for (int i = 0; i < model->count(); ++i) {
              content::WebContents* c = model->GetWebContentsAt(i);
              if (c) {
                if (McpSessionIdOf(c) == tab_id) {
                  if (IsWebContentsMcpEligible(browser, c)) {
                    match = c;
                  }
                  return false;
                }
              }
            }
          }
          return true;
        });
    return match;
  }

  std::vector<maho::MahoMcpSession::ConsoleMessage> GetConsoleMessages(
      int tab_id) override {
    std::vector<maho::MahoMcpSession::ConsoleMessage> list;
    content::WebContents* wc =
        (tab_id == 0) ? GetActiveWebContents() : GetWebContentsForTabId(tab_id);
    if (wc) {
      maho::MahoMcpConsoleCapture::CreateForWebContents(wc);
      maho::MahoMcpConsoleCapture* capture =
          maho::MahoMcpConsoleCapture::FromWebContents(wc);
      if (capture) {
        for (const auto& msg : capture->messages()) {
          maho::MahoMcpSession::ConsoleMessage m;
          m.level = msg.level;
          m.message = msg.message;
          m.source_url = msg.source_url;
          m.line = msg.line;
          m.timestamp_ms = msg.timestamp_ms;
          list.push_back(m);
        }
      }
    }
    return list;
  }
  std::vector<maho::MahoMcpSession::NavigationEvent> GetNavigationEvents(
      int tab_id,
      int64_t since_ms) override {
    std::vector<maho::MahoMcpSession::NavigationEvent> list;
    content::WebContents* wc =
        (tab_id == 0) ? GetActiveWebContents() : GetWebContentsForTabId(tab_id);
    if (wc) {
      maho::MahoMcpNavigationTracker::CreateForWebContents(wc);
      maho::MahoMcpNavigationTracker* tracker =
          maho::MahoMcpNavigationTracker::FromWebContents(wc);
      if (tracker) {
        for (const auto& ev : tracker->events()) {
          if (ev.timestamp_ms > since_ms) {
            maho::MahoMcpSession::NavigationEvent out;
            out.url = ev.url;
            out.title = ev.title;
            out.status_code = ev.status_code;
            out.timestamp_ms = ev.timestamp_ms;
            list.push_back(std::move(out));
          }
        }
      }
    }
    return list;
  }

  maho::MahoMcpBrowserDelegate::PageContentResult GetPageContent(
      int tab_id) override {
    maho::MahoMcpBrowserDelegate::PageContentResult result;
    content::WebContents* wc =
        (tab_id == 0) ? GetActiveWebContents() : GetWebContentsForTabId(tab_id);
    if (wc) {
      result.url = wc->GetLastCommittedURL().spec();
      result.title = base::UTF16ToUTF8(wc->GetTitle());
      result.text = ExecuteMcpActionJsBlocking(
          wc,
          "document.documentElement ? document.documentElement.outerHTML : ''");
    }
    return result;
  }

  maho::MahoMcpBrowserDelegate::PageContextResult GetPageContext(
      int tab_id) override {
    maho::MahoMcpBrowserDelegate::PageContextResult result;
    content::WebContents* wc =
        (tab_id == 0) ? GetActiveWebContents() : GetWebContentsForTabId(tab_id);
    if (wc) {
      result.url = wc->GetLastCommittedURL().spec();
      result.title = base::UTF16ToUTF8(wc->GetTitle());
      result.content = ExecuteMcpActionJsBlocking(
          wc, "document.body ? document.body.innerText : ''");
    }
    return result;
  }

  maho::MahoMcpBrowserDelegate::SearchResult SearchInPage(
      int tab_id,
      const std::string& query) override {
    maho::MahoMcpBrowserDelegate::SearchResult result;
    content::WebContents* wc =
        (tab_id == 0) ? GetActiveWebContents() : GetWebContentsForTabId(tab_id);
    if (wc) {
      std::string escaped_query = base::GetQuotedJSONString(query);
      std::string script =
          "(()=>{"
          "const text = (document.body ? document.body.innerText : "
          "'').toLowerCase();"
          "const q = (" +
          escaped_query +
          ").toLowerCase();"
          "if(!q) return JSON.stringify({match_count:0,active_match_index:-1});"
          "let count = 0;"
          "let pos = text.indexOf(q);"
          "while(pos !== -1){"
          "  count++;"
          "  pos = text.indexOf(q, pos + q.length);"
          "}"
          "return JSON.stringify({match_count:count,active_match_index:count > "
          "0 ? 0 : -1});"
          "})()";
      std::string js_res = ExecuteMcpActionJsBlocking(wc, script);
      std::optional<base::Value> parsed =
          base::JSONReader::Read(js_res, base::JSON_PARSE_RFC);
      if (parsed && parsed->is_dict()) {
        const auto& dict = parsed->GetDict();
        result.match_count = dict.FindInt("match_count").value_or(0);
        result.active_match_index =
            dict.FindInt("active_match_index").value_or(-1);
      }
    }
    return result;
  }

  maho::MahoMcpBrowserDelegate::QuerySelectorResult QuerySelector(
      int tab_id,
      const std::string& selector) override {
    maho::MahoMcpBrowserDelegate::QuerySelectorResult result;
    content::WebContents* wc =
        (tab_id == 0) ? GetActiveWebContents() : GetWebContentsForTabId(tab_id);
    if (wc) {
      std::string escaped_sel = base::GetQuotedJSONString(selector);
      std::string script =
          "(()=>{"
          "const el = document.querySelector(" +
          escaped_sel +
          ");"
          "if(!el) return '';"
          "window.__mcp_elements = window.__mcp_elements || [];"
          "let idx = window.__mcp_elements.indexOf(el);"
          "if(idx === -1){"
          "  idx = window.__mcp_elements.length;"
          "  window.__mcp_elements.push(el);"
          "}"
          "return JSON.stringify({ref_id:'ref_' + idx, "
          "tag:el.tagName.toLowerCase()});"
          "})()";
      std::string js_res = ExecuteMcpActionJsBlocking(wc, script);
      std::optional<base::Value> parsed =
          base::JSONReader::Read(js_res, base::JSON_PARSE_RFC);
      if (parsed && parsed->is_dict()) {
        const auto& dict = parsed->GetDict();
        if (const std::string* ref_id = dict.FindString("ref_id")) {
          result.ref_id = *ref_id;
        }
        if (const std::string* tag = dict.FindString("tag")) {
          result.tag = *tag;
        }
      }
    }
    return result;
  }

  std::string GetElementText(int tab_id, const std::string& ref_id) override {
    content::WebContents* wc =
        (tab_id == 0) ? GetActiveWebContents() : GetWebContentsForTabId(tab_id);
    if (wc) {
      std::string escaped_ref = base::GetQuotedJSONString(ref_id);
      std::string script =
          "(()=>{"
          "const refId = " +
          escaped_ref +
          ";"
          "const idx = parseInt(refId.replace('ref_', ''));"
          "if(window.__mcp_elements && window.__mcp_elements[idx]){"
          "  const el = window.__mcp_elements[idx];"
          "  return el.innerText || el.textContent || '';"
          "}"
          "return '';"
          "})()";
      return ExecuteMcpActionJsBlocking(wc, script);
    }
    return std::string();
  }

  std::string GetElementAttribute(int tab_id,
                                  const std::string& ref_id,
                                  const std::string& attribute) override {
    content::WebContents* wc =
        (tab_id == 0) ? GetActiveWebContents() : GetWebContentsForTabId(tab_id);
    if (wc) {
      std::string escaped_ref = base::GetQuotedJSONString(ref_id);
      std::string escaped_attr = base::GetQuotedJSONString(attribute);
      std::string script =
          "(()=>{"
          "const refId = " +
          escaped_ref +
          ";"
          "const attrName = " +
          escaped_attr +
          ";"
          "const idx = parseInt(refId.replace('ref_', ''));"
          "if(window.__mcp_elements && window.__mcp_elements[idx]){"
          "  const el = window.__mcp_elements[idx];"
          "  const val = el.getAttribute(attrName);"
          "  return val !== null ? val : '';"
          "}"
          "return '';"
          "})()";
      return ExecuteMcpActionJsBlocking(wc, script);
    }
    return std::string();
  }

  bool WaitForSelector(int tab_id,
                       const std::string& selector,
                       int timeout_ms) override {
    base::TimeTicks start = base::TimeTicks::Now();
    base::TimeDelta timeout = base::Milliseconds(timeout_ms);
    std::string escaped_sel = base::GetQuotedJSONString(selector);
    std::string script =
        "document.querySelector(" + escaped_sel + ") ? 'true' : 'false'";
    while (true) {
      // Defect A (UAF) fix: re-resolve the WebContents on every iteration
      // instead of caching a raw pointer across the nested
      // kNestableTasksAllowed RunLoop pumps below. A concurrent
      // browser_tab_close runs as a nestable task inside those pumps and can
      // destroy the WebContents mid-wait; caching the pointer would dereference
      // freed memory. Mirrors the re-resolve pattern already used at
      // CaptureVisibleViewFromTab / OnScreenshotCaptured /
      // OnElementScreenshotCaptured.
      content::WebContents* wc = (tab_id == 0) ? GetActiveWebContents()
                                               : GetWebContentsForTabId(tab_id);
      if (!wc) {
        // Tab was closed (or never existed): clean false, no dangling deref.
        return false;
      }

      std::string res = ExecuteMcpActionJsBlocking(wc, script);
      if (res == "true") {
        // Present now (also covers timeout_ms == 0 / already-present).
        return true;
      }

      // Defect B fix: check the deadline after the query but before sleeping,
      // so a timeout returns found:false immediately (no >100ms overrun, no
      // hang) rather than always sleeping one extra 100ms interval.
      if (base::TimeTicks::Now() - start >= timeout) {
        return false;
      }

      base::RunLoop run_loop(base::RunLoop::Type::kNestableTasksAllowed);
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
          FROM_HERE, run_loop.QuitClosure(), base::Milliseconds(100));
      run_loop.Run();
    }
  }

  bool WaitForAutoQuiet(int tab_id, int timeout_ms) override {
    base::TimeTicks start = base::TimeTicks::Now();
    base::TimeDelta timeout = base::Milliseconds(timeout_ms > 0 ? timeout_ms : 10000);
    while (true) {
      content::WebContents* wc = (tab_id == 0) ? GetActiveWebContents()
                                               : GetWebContentsForTabId(tab_id);
      if (!wc) {
        return false;
      }
      if (!wc->IsLoading() && !wc->IsWaitingForResponse()) {
        return true;
      }
      if (base::TimeTicks::Now() - start >= timeout) {
        return false;
      }
      base::RunLoop run_loop(base::RunLoop::Type::kNestableTasksAllowed);
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
          FROM_HERE, run_loop.QuitClosure(), base::Milliseconds(50));
      run_loop.Run();
    }
  }

  maho::MahoMcpBrowserDelegate::LocatorResolution ResolveLocator(
      int tab_id,
      const maho::MahoMcpBrowserDelegate::LocatorParams& params,
      const maho::MahoMcpSession::RefTable& refs) override {
    content::WebContents* wc =
        (tab_id == 0) ? GetActiveWebContents() : GetWebContentsForTabId(tab_id);
    maho::MahoMcpBrowserDelegate::LocatorResolution res;
    if (!wc) {
      res.error_code = "locator_detached";
      res.hint = "Tab not found or detached";
      return res;
    }

    if (params.ref.has_value()) {
      auto it = refs.find(*params.ref);
      if (it == refs.end()) {
        res.error_code = "locator_not_found";
        res.hint = "Ref " + base::NumberToString(*params.ref) +
                   " not found in session ref table";
        return res;
      }
      ui::AXNodeID ax_id = it->second;
      ui::AXTreeUpdate update = SnapshotAXTree(wc);
      const ui::AXNodeData* target_node = nullptr;
      for (const ui::AXNodeData& node : update.nodes) {
        if (node.id == ax_id) {
          target_node = &node;
          break;
        }
      }
      if (!target_node) {
        res.error_code = "locator_detached";
        res.hint = "Element is detached from DOM";
        return res;
      }
      if (target_node->GetRestriction() == ax::mojom::Restriction::kDisabled) {
        res.error_code = "locator_not_actionable";
        res.hint = "Element is disabled";
        return res;
      }
      if (target_node->HasState(ax::mojom::State::kInvisible) ||
          target_node->HasState(ax::mojom::State::kIgnored)) {
        res.error_code = "locator_obscured";
        res.hint = "Element is obscured or not visible";
        return res;
      }
      res.success = true;
      res.ax_id = ax_id;
      return res;
    }

    if (params.css.has_value()) {
      std::string escaped_css = base::GetQuotedJSONString(*params.css);
      std::string script =
          "(() => {\n"
          "  try {\n"
          "    const els = document.querySelectorAll(" + escaped_css + ");\n"
          "    if (els.length === 0) return JSON.stringify({count: 0});\n"
          "    if (els.length > 1) return JSON.stringify({count: els.length});\n"
          "    const el = els[0];\n"
          "    const rect = el.getBoundingClientRect();\n"
          "    const style = window.getComputedStyle(el);\n"
          "    const visible = style.display !== 'none' && style.visibility !== 'hidden' && style.opacity !== '0' && rect.width > 0 && rect.height > 0;\n"
          "    const disabled = el.disabled === true || el.getAttribute('aria-disabled') === 'true';\n"
          "    const attached = el.isConnected;\n"
          "    const id = el.id || '';\n"
          "    const name = el.getAttribute('aria-label') || el.innerText || el.value || '';\n"
          "    return JSON.stringify({count: 1, attached: attached, visible: visible, disabled: disabled, id: id, name: name});\n"
          "  } catch (e) {\n"
          "    return JSON.stringify({error: e.message});\n"
          "  }\n"
          "})()";
      std::string js_res = ExecuteMcpActionJsBlocking(wc, script);
      std::optional<base::Value> parsed_json =
          base::JSONReader::Read(js_res, base::JSON_PARSE_RFC);
      if (!parsed_json || !parsed_json->is_dict()) {
        res.error_code = "locator_not_found";
        res.hint = "Failed to query selector: " + *params.css;
        return res;
      }
      const auto& dict = parsed_json->GetDict();
      int count = dict.FindInt("count").value_or(0);
      if (count == 0) {
        res.error_code = "locator_not_found";
        res.hint = "No element matches selector: " + *params.css;
        return res;
      }
      if (count > 1) {
        res.error_code = "locator_ambiguous";
        res.matches = count;
        res.hint = "Selector matched multiple elements (" + base::NumberToString(count) + "); refine selector";
        return res;
      }
      bool attached = dict.FindBool("attached").value_or(false);
      if (!attached) {
        res.error_code = "locator_detached";
        res.hint = "Element is detached from DOM";
        return res;
      }
      bool disabled = dict.FindBool("disabled").value_or(false);
      if (disabled) {
        res.error_code = "locator_not_actionable";
        res.hint = "Element is disabled";
        return res;
      }
      bool visible = dict.FindBool("visible").value_or(false);
      if (!visible) {
        res.error_code = "locator_obscured";
        res.hint = "Element is obscured or not visible";
        return res;
      }

      ui::AXTreeUpdate update = SnapshotAXTree(wc);
      const std::string* el_id = dict.FindString("id");
      const std::string* el_name = dict.FindString("name");
      ui::AXNodeID matched_ax_id = 0;
      if (el_id && !el_id->empty()) {
        for (const auto& node : update.nodes) {
          if (node.GetStringAttribute(ax::mojom::StringAttribute::kHtmlId) == *el_id) {
            matched_ax_id = node.id;
            break;
          }
        }
      }
      if (matched_ax_id == 0 && el_name && !el_name->empty()) {
        for (const auto& node : update.nodes) {
          if (node.GetStringAttribute(ax::mojom::StringAttribute::kName) == *el_name) {
            matched_ax_id = node.id;
            break;
          }
        }
      }
      if (matched_ax_id == 0 && !update.nodes.empty()) {
        matched_ax_id = update.nodes[0].id;
      }
      res.success = true;
      res.ax_id = matched_ax_id;
      return res;
    }

    if (params.role.has_value() && params.name.has_value()) {
      ui::AXTreeUpdate update = SnapshotAXTree(wc);
      std::vector<const ui::AXNodeData*> matches;
      std::string search_role = base::ToLowerASCII(*params.role);
      std::string search_name = *params.name;
      for (const auto& node : update.nodes) {
        std::string role_str = base::ToLowerASCII(maho::MahoMcpAccessibilityHandler::RoleToString(node.role));
        bool role_matched = (role_str == search_role);
        if (!role_matched) {
          if (search_role == "button" && (role_str == "button" || role_str == "popupbutton" || role_str == "togglebutton")) {
            role_matched = true;
          } else if ((search_role == "textbox" || search_role == "textfield" || search_role == "input") &&
                     (role_str == "textfield" || role_str == "searchbox")) {
            role_matched = true;
          }
        }
        if (!role_matched) continue;

        std::string node_name = node.GetStringAttribute(ax::mojom::StringAttribute::kName);
        bool name_matched = false;
        if (params.exact) {
          name_matched = base::EqualsCaseInsensitiveASCII(node_name, search_name);
        } else {
          name_matched = (base::ToLowerASCII(node_name).find(base::ToLowerASCII(search_name)) != std::string::npos);
        }
        if (name_matched) {
          matches.push_back(&node);
        }
      }
      if (matches.empty()) {
        res.error_code = "locator_not_found";
        res.hint = "No element matches role '" + *params.role + "' and name '" + *params.name + "'";
        return res;
      }
      if (matches.size() > 1) {
        res.error_code = "locator_ambiguous";
        res.matches = static_cast<int>(matches.size());
        res.hint = "Multiple elements (" + base::NumberToString(matches.size()) + ") match role and name; refine selector";
        return res;
      }
      const auto* target = matches[0];
      if (target->GetRestriction() == ax::mojom::Restriction::kDisabled) {
        res.error_code = "locator_not_actionable";
        res.hint = "Element is disabled";
        return res;
      }
      if (target->HasState(ax::mojom::State::kInvisible) ||
          target->HasState(ax::mojom::State::kIgnored)) {
        res.error_code = "locator_obscured";
        res.hint = "Element is obscured or not visible";
        return res;
      }
      res.success = true;
      res.ax_id = target->id;
      return res;
    }

    res.error_code = "locator_not_found";
    res.hint = "Invalid locator parameters";
    return res;
  }

  void SameOriginFetch(
      int tab_id,
      const GURL& url,
      const std::string& method,
      const std::string& body,
      const std::string& headers_json,
      maho::MahoMcpBrowserDelegate::SameOriginFetchCallback callback) override {
    content::WebContents* wc =
        (tab_id == 0) ? GetActiveWebContents() : GetWebContentsForTabId(tab_id);
    if (!wc || !wc->GetPrimaryMainFrame()) {
      maho::MahoMcpBrowserDelegate::SameOriginFetchResult result;
      result.error = "No WebContents available for same-origin fetch";
      std::move(callback).Run(std::move(result));
      return;
    }
    const GURL page_url = wc->GetLastCommittedURL();
    if (!page_url.is_valid() ||
        page_url.DeprecatedGetOriginAsURL() != url.DeprecatedGetOriginAsURL()) {
      maho::MahoMcpBrowserDelegate::SameOriginFetchResult result;
      result.error =
          "Same-origin fetch blocked: URL origin does not match target tab";
      std::move(callback).Run(std::move(result));
      return;
    }

    constexpr size_t kMaxFetchTextBytes = 1024 * 1024;
    const std::string fetch_id =
        base::Uuid::GenerateRandomV4().AsLowercaseString();
    const std::string fetch_id_json = base::GetQuotedJSONString(fetch_id);
    const std::string start_script =
        "(()=>{window.__maho_mcp_fetches=window.__maho_mcp_fetches||{};"
        "const id=" +
        fetch_id_json +
        ";window.__maho_mcp_fetches[id]={done:false};"
        "(async()=>{try{"
        "const response=await fetch(" +
        base::GetQuotedJSONString(url.spec()) +
        ",{method:" + base::GetQuotedJSONString(method) +
        ",credentials:'include',redirect:'error',headers:" + headers_json +
        (method == "POST" ? ",body:" + base::GetQuotedJSONString(body)
                          : std::string()) +
        "});"
        "if(new URL(response.url).origin!==location.origin)throw new "
        "Error('cross-origin response blocked');"
        "const full=await response.text();"
        "const text=full.slice(0," +
        base::NumberToString(kMaxFetchTextBytes) +
        ");"
        "window.__maho_mcp_fetches[id]={done:true,value:JSON.stringify({"
        "success:true,status:response.status,final_url:response.url,content_"
        "type:response.headers.get('content-type')||'',text,truncated:text."
        "length<full.length})};"
        "}catch(error){window.__maho_mcp_fetches[id]={done:true,value:JSON."
        "stringify({success:false,error:String(error&&error.message||error)})};"
        "}})();"
        "return 'started';})()";
    if (ExecuteMcpActionJsBlocking(wc, start_script) != "started") {
      maho::MahoMcpBrowserDelegate::SameOriginFetchResult result;
      result.error = "Same-origin fetch failed to start";
      std::move(callback).Run(std::move(result));
      return;
    }

    const std::string poll_script =
        "(()=>{const "
        "entry=window.__maho_mcp_fetches&&window.__maho_mcp_fetches[" +
        fetch_id_json +
        "];if(!entry||!entry.done)return '';const value=entry.value||'';delete "
        "window.__maho_mcp_fetches[" +
        fetch_id_json + "];return value;})()";
    const base::TimeTicks deadline = base::TimeTicks::Now() + base::Seconds(30);
    std::string serialized;
    while (base::TimeTicks::Now() < deadline) {
      content::WebContents* current = (tab_id == 0)
                                          ? GetActiveWebContents()
                                          : GetWebContentsForTabId(tab_id);
      if (!current) {
        break;
      }
      serialized = ExecuteMcpActionJsBlocking(current, poll_script);
      if (!serialized.empty()) {
        break;
      }
      base::RunLoop run_loop(base::RunLoop::Type::kNestableTasksAllowed);
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
          FROM_HERE, run_loop.QuitClosure(), base::Milliseconds(50));
      run_loop.Run();
    }

    maho::MahoMcpBrowserDelegate::SameOriginFetchResult result;
    if (serialized.empty()) {
      result.error = "Same-origin fetch timed out after 30 seconds";
      std::move(callback).Run(std::move(result));
      return;
    }
    std::optional<base::Value> parsed =
        base::JSONReader::Read(serialized, base::JSON_PARSE_RFC);
    if (!parsed || !parsed->is_dict()) {
      result.error = "Same-origin fetch returned invalid JSON";
      std::move(callback).Run(std::move(result));
      return;
    }
    const base::DictValue& dict = parsed->GetDict();
    result.success = dict.FindBool("success").value_or(false);
    result.error =
        dict.FindString("error") ? *dict.FindString("error") : std::string();
    if (result.success) {
      result.status = dict.FindInt("status").value_or(0);
      result.final_url = dict.FindString("final_url")
                             ? *dict.FindString("final_url")
                             : std::string();
      result.content_type = dict.FindString("content_type")
                                ? *dict.FindString("content_type")
                                : std::string();
      result.text =
          dict.FindString("text") ? *dict.FindString("text") : std::string();
      result.truncated = dict.FindBool("truncated").value_or(false);
      const GURL final_url(result.final_url);
      if (!final_url.is_valid() || final_url.DeprecatedGetOriginAsURL() !=
                                       url.DeprecatedGetOriginAsURL()) {
        result = {};
        result.error = "Same-origin fetch blocked a cross-origin response";
      } else if (result.text.size() > kMaxFetchTextBytes) {
        result.text.resize(kMaxFetchTextBytes);
        result.truncated = true;
      }
    }
    std::move(callback).Run(std::move(result));
  }

  int CreateNewTab(const GURL& url) override {
    int new_tab_id = 0;
    ForEachCurrentBrowserWindowInterfaceOrderedByActivation(
        [&new_tab_id, &url](BrowserWindowInterface* browser) {
          if (browser && browser->GetTabStripModel() &&
              IsBrowserMcpEligible(browser)) {
            TabStripModel* model = browser->GetTabStripModel();
            content::WebContents::CreateParams params(browser->GetProfile());
            std::unique_ptr<content::WebContents> new_contents =
                content::WebContents::Create(params);
            if (new_contents) {
              CreateSessionServiceTabHelper(new_contents.get());
              MahoTabIdHelper::CreateForWebContents(new_contents.get());
              new_tab_id = McpSessionIdOf(new_contents.get());
              if (!url.is_empty() && url.spec() != "about:blank") {
                content::NavigationController::LoadURLParams nav_params(url);
                nav_params.transition_type = ui::PageTransitionFromInt(
                    ui::PAGE_TRANSITION_TYPED |
                    ui::PAGE_TRANSITION_FROM_ADDRESS_BAR);
                new_contents->GetController().LoadURLWithParams(nav_params);
              }
              model->AppendWebContents(std::move(new_contents),
                                       /*foreground=*/true);
            }
            return false;
          }
          return true;
        });
    return new_tab_id;
  }

  bool CloseTab(int tab_id) override {
    bool closed = false;
    ForEachCurrentBrowserWindowInterfaceOrderedByActivation(
        [tab_id, &closed](BrowserWindowInterface* browser) {
          if (browser && browser->GetTabStripModel() &&
              IsBrowserMcpEligible(browser)) {
            TabStripModel* model = browser->GetTabStripModel();
            for (int i = 0; i < model->count(); ++i) {
              content::WebContents* c = model->GetWebContentsAt(i);
              if (c) {
                if (McpSessionIdOf(c) == tab_id) {
                  model->CloseWebContentsAt(
                      i, TabCloseTypes::CLOSE_CREATE_HISTORICAL_TAB);
                  closed = true;
                  return false;
                }
              }
            }
          }
          return true;
        });
    return closed;
  }

  maho::MahoMcpTargetResolution ResolveTabTarget(
      int requested_tab_id) override {
    PruneStaleTargetTokens();
    maho::MahoMcpTargetResolution resolution;
    EligibleTab tab = ResolveEligibleTab(requested_tab_id);
    if (!tab.web_contents) {
      resolution.error = (requested_tab_id == 0)
                             ? maho::MahoMcpTargetError::kNoEligibleActiveTab
                             : maho::MahoMcpTargetError::kTabNotFound;
      return resolution;
    }
    // Bind an owned token to the EXACT resolved browser+WebContents. The token
    // holds weak pointers to both, so close/reuse (the weak WebContents dies),
    // active-switch retarget, a tab move to another window (strip-membership
    // check), OTR, and profile change are all rejected at revalidation without
    // re-deriving a target from a raw integer id.
    auto token = std::make_unique<MahoPrivateContextToken>(tab.browser,
                                                           tab.web_contents);
    if (!token->Revalidate(MahoPrivateCapability::kMCP)) {
      resolution.error = (requested_tab_id == 0)
                             ? maho::MahoMcpTargetError::kNoEligibleActiveTab
                             : maho::MahoMcpTargetError::kTabNotFound;
      return resolution;
    }
    const int64_t generation = ++generation_counter_;
    resolution.target.valid = true;
    resolution.target.tab_id = McpSessionIdOf(tab.web_contents);
    resolution.target.browser_id = tab.browser->GetSessionID().id();
    resolution.target.generation = generation;
    StoreTargetToken(generation, std::move(token));
    return resolution;
  }

  maho::MahoMcpTargetResolution ResolveProfileTarget() override {
    PruneStaleTargetTokens();
    maho::MahoMcpTargetResolution resolution;
    BrowserWindowInterface* eligible_browser = nullptr;
    ForEachCurrentBrowserWindowInterfaceOrderedByActivation(
        [&eligible_browser](BrowserWindowInterface* browser) {
          if (browser && browser->GetTabStripModel() &&
              IsBrowserMcpEligible(browser)) {
            eligible_browser = browser;
            return false;
          }
          return true;
        });
    if (!eligible_browser) {
      resolution.error = maho::MahoMcpTargetError::kNoEligibleActiveBrowser;
      return resolution;
    }
    auto token =
        std::make_unique<MahoPrivateContextToken>(eligible_browser, nullptr);
    if (!token->Revalidate(MahoPrivateCapability::kMCP)) {
      resolution.error = maho::MahoMcpTargetError::kNoEligibleActiveBrowser;
      return resolution;
    }
    const int64_t generation = ++generation_counter_;
    resolution.target.valid = true;
    resolution.target.browser_id = eligible_browser->GetSessionID().id();
    resolution.target.generation = generation;
    StoreTargetToken(generation, std::move(token));
    return resolution;
  }

  bool RevalidateTarget(const maho::ResolvedMahoMcpTarget& target) override {
    if (!target.valid) {
      return false;
    }
    // Authorized only if the exact token minted at resolution time is still
    // present under its generation AND still revalidates kMCP. A missing
    // generation (evicted / never ours) or a token whose bound browser or
    // WebContents died, moved strips, or changed profile fails closed — a
    // reused SessionID does NOT rescue it because the token binds the original
    // (now-dead) WebContents, not the integer id.
    auto it = resolved_target_tokens_.find(target.generation);
    if (it == resolved_target_tokens_.end() || !it->second) {
      return false;
    }
    if (!it->second->Revalidate(MahoPrivateCapability::kMCP)) {
      resolved_target_tokens_.erase(it);
      return false;
    }
    return true;
  }

  Profile* GetProfile() {
    Profile* profile = nullptr;
    ForEachCurrentBrowserWindowInterfaceOrderedByActivation(
        [&profile](BrowserWindowInterface* browser) {
          if (browser && IsBrowserMcpEligible(browser)) {
            profile = browser->GetProfile();
            return false;
          }
          return true;
        });
    return profile;
  }

  // Profile-keyed mail broker for the frontmost eligible profile, or nullptr.
  // Helper teardown is owned by KeyedService::Shutdown, never by this delegate.
  maho::MahoMailService* GetMailService() {
    Profile* profile = GetProfile();
    if (!profile) {
      return nullptr;
    }
    return maho::MahoMailServiceFactory::GetForProfile(profile);
  }

  struct EligibleTab {
    RAW_PTR_EXCLUSION BrowserWindowInterface* browser = nullptr;
    RAW_PTR_EXCLUSION content::WebContents* web_contents = nullptr;
  };

  // Resolves the (browser, WebContents) pair for a tool target. For an omitted
  // request (0) only the frontmost/most-recently-active window is considered:
  // if it is OTR/ineligible the active target IS OTR and the pair stays empty
  // (denied), never retargeting to another window. For an explicit id the pair
  // is the eligible owning tab, or empty.
  EligibleTab ResolveEligibleTab(int requested_tab_id) {
    EligibleTab result;
    if (requested_tab_id == 0) {
      bool inspected_frontmost = false;
      ForEachCurrentBrowserWindowInterfaceOrderedByActivation(
          [&result, &inspected_frontmost](BrowserWindowInterface* browser) {
            if (inspected_frontmost) {
              return false;
            }
            inspected_frontmost = true;
            if (browser && browser->GetTabStripModel() &&
                IsBrowserMcpEligible(browser)) {
              content::WebContents* wc =
                  browser->GetTabStripModel()->GetActiveWebContents();
              if (wc && IsWebContentsMcpEligible(browser, wc)) {
                result.browser = browser;
                result.web_contents = wc;
              }
            }
            return false;
          });
      return result;
    }
    ForEachCurrentBrowserWindowInterfaceOrderedByActivation(
        [&result, requested_tab_id](BrowserWindowInterface* browser) {
          if (browser && browser->GetTabStripModel() &&
              IsBrowserMcpEligible(browser)) {
            TabStripModel* model = browser->GetTabStripModel();
            for (int i = 0; i < model->count(); ++i) {
              content::WebContents* c = model->GetWebContentsAt(i);
              if (c) {
                if (McpSessionIdOf(c) == requested_tab_id) {
                  if (IsWebContentsMcpEligible(browser, c)) {
                    result.browser = browser;
                    result.web_contents = c;
                  }
                  return false;
                }
              }
            }
          }
          return true;
        });
    return result;
  }

  void StoreTargetToken(int64_t generation,
                        std::unique_ptr<MahoPrivateContextToken> token) {
    resolved_target_tokens_[generation] = std::move(token);
    // Bound-memory guard: the map is ordered by ascending generation, so the
    // begin() entry is the oldest. Evicted generations fail closed on later
    // revalidation.
    while (resolved_target_tokens_.size() > kMaxResolvedTargets) {
      resolved_target_tokens_.erase(resolved_target_tokens_.begin());
    }
  }

  void PruneStaleTargetTokens() {
    for (auto it = resolved_target_tokens_.begin();
         it != resolved_target_tokens_.end();) {
      if (!it->second || !it->second->Revalidate(MahoPrivateCapability::kMCP)) {
        it = resolved_target_tokens_.erase(it);
      } else {
        ++it;
      }
    }
  }

  void RecursiveSearchBookmarks(
      const ::bookmarks::BookmarkNode* node,
      const std::string& query_lower,
      std::vector<maho::MahoMcpBrowserDelegate::BookmarkInfo>& results) {
    if (!node) {
      return;
    }
    if (node->is_url()) {
      std::string title = base::UTF16ToUTF8(node->GetTitle());
      std::string url = node->url().spec();
      std::string title_lower = base::ToLowerASCII(title);
      std::string url_lower = base::ToLowerASCII(url);
      if (query_lower.empty() ||
          title_lower.find(query_lower) != std::string::npos ||
          url_lower.find(query_lower) != std::string::npos) {
        maho::MahoMcpBrowserDelegate::BookmarkInfo info;
        info.id = node->id();
        info.title = title;
        info.url = url;
        if (node->parent()) {
          info.folder = base::UTF16ToUTF8(node->parent()->GetTitle());
        }
        results.push_back(info);
      }
    }
    for (const auto& child : node->children()) {
      RecursiveSearchBookmarks(child.get(), query_lower, results);
    }
  }

  std::vector<maho::MahoMcpBrowserDelegate::BookmarkInfo> SearchBookmarks(
      const std::string& query) override {
    std::vector<maho::MahoMcpBrowserDelegate::BookmarkInfo> results;
    Profile* profile = GetProfile();
    if (!profile) {
      return results;
    }
    ::bookmarks::BookmarkModel* bookmark_model =
        BookmarkModelFactory::GetForBrowserContext(profile);
    if (bookmark_model && bookmark_model->loaded()) {
      std::string query_lower = base::ToLowerASCII(query);
      RecursiveSearchBookmarks(bookmark_model->root_node(), query_lower,
                               results);
    }
    return results;
  }

  std::vector<maho::MahoMcpBrowserDelegate::HistoryEntry> SearchHistory(
      const std::string& query,
      size_t max_results) override {
    std::vector<maho::MahoMcpBrowserDelegate::HistoryEntry> results;
    MahoCore* core = maho::GetCore();
    if (!core) {
      return results;
    }
    // Honor the requested count up to a hard ceiling so bulk callers (history
    // export) are not silently truncated; maho-core respects the passed limit.
    constexpr size_t kMaxHistoryResults = 100000;
    const size_t limit = (max_results == 0) ? 50
                                            : (max_results > kMaxHistoryResults
                                                   ? kMaxHistoryResults
                                                   : max_results);
    std::string json = maho::core::SearchHistory(core, query.c_str(), limit);
    if (json.empty()) {
      return results;
    }
    std::optional<base::Value> parsed =
        base::JSONReader::Read(json, base::JSON_PARSE_RFC);
    if (!parsed || !parsed->is_list()) {
      return results;
    }
    for (const auto& entry : parsed->GetList()) {
      if (!entry.is_list()) {
        continue;
      }
      const auto& tuple = entry.GetList();
      if (tuple.size() < 2) {
        continue;
      }
      const std::string* url = tuple[0].GetIfString();
      const std::string* title = tuple[1].GetIfString();
      if (!url || url->empty()) {
        continue;
      }
      maho::MahoMcpBrowserDelegate::HistoryEntry h;
      h.url = *url;
      h.title = title ? *title : "";
      h.visited_at =
          tuple.size() >= 3 ? tuple[2].GetIfDouble().value_or(0.0) : 0.0;
      results.push_back(std::move(h));
    }
    return results;
  }

  maho::MahoMcpBrowserDelegate::BookmarkInfo CreateBookmark(
      const std::string& title,
      const GURL& url,
      const std::string& folder) override {
    maho::MahoMcpBrowserDelegate::BookmarkInfo result;
    Profile* profile = GetProfile();
    if (!profile) {
      return result;
    }
    ::bookmarks::BookmarkModel* bookmark_model =
        BookmarkModelFactory::GetForBrowserContext(profile);
    if (!bookmark_model || !bookmark_model->loaded()) {
      return result;
    }

    const ::bookmarks::BookmarkNode* parent =
        bookmark_model->bookmark_bar_node();
    if (!folder.empty()) {
      const ::bookmarks::BookmarkNode* found_folder = nullptr;
      for (const auto& child : parent->children()) {
        if (child->is_folder() &&
            base::UTF16ToUTF8(child->GetTitle()) == folder) {
          found_folder = child.get();
          break;
        }
      }
      if (!found_folder) {
        parent = bookmark_model->AddFolder(parent, parent->children().size(),
                                           base::UTF8ToUTF16(folder));
      } else {
        parent = found_folder;
      }
    }

    const ::bookmarks::BookmarkNode* node = bookmark_model->AddURL(
        parent, parent->children().size(), base::UTF8ToUTF16(title), url);
    if (node) {
      result.id = node->id();
      result.title = title;
      result.url = url.spec();
      result.folder = folder;
    }
    return result;
  }

  using RefFrameSignature =
      std::vector<std::pair<content::GlobalRenderFrameHostId, std::string>>;
  struct CachedRefNode {
    ax::mojom::Role role = ax::mojom::Role::kUnknown;
    bool has_checked_state = false;
    ax::mojom::CheckedState checked_state = ax::mojom::CheckedState::kNone;
    bool is_protected = false;
    std::string autocomplete;
    std::string accessible_name;
    std::optional<gfx::PointF> center;
    std::optional<gfx::PointF> leading_center;
  };
  struct CachedRefSnapshot {
    uint64_t navigation_epoch = 0;
    RefFrameSignature frame_signature;
    std::unordered_map<ui::AXNodeID, CachedRefNode> nodes;
  };
  struct RefSnapshotGuard {
    int tab_id = 0;
    uint64_t navigation_epoch = 0;
    RefFrameSignature frame_signature;
  };

  RefFrameSignature CaptureRefFrameSignature(content::WebContents* wc) {
    RefFrameSignature signature;
    if (!wc) {
      return signature;
    }
    wc->ForEachRenderFrameHost([&](content::RenderFrameHost* rfh) {
      if (!rfh || !rfh->IsRenderFrameLive()) {
        return;
      }
      const auto global_id = rfh->GetGlobalId();
      signature.push_back(
          {global_id, rfh->GetLastCommittedURL().spec()});
    });
    std::sort(signature.begin(), signature.end());
    return signature;
  }

  int ResolveRefCacheTabId(int tab_id, content::WebContents* wc) const {
    return tab_id == 0 && wc ? McpSessionIdOf(wc) : tab_id;
  }

  void InvalidateCachedRefSnapshot(int tab_id, content::WebContents* wc) {
    ref_snapshot_cache_.erase(ResolveRefCacheTabId(tab_id, wc));
  }

  const CachedRefSnapshot* GetValidCachedRefSnapshot(
      content::WebContents* wc,
      int tab_id) {
    const int resolved_tab_id = ResolveRefCacheTabId(tab_id, wc);
    auto it = ref_snapshot_cache_.find(resolved_tab_id);
    if (it == ref_snapshot_cache_.end()) {
      return nullptr;
    }
    if (!wc ||
        it->second.navigation_epoch != GetNavigationEpoch(resolved_tab_id) ||
        it->second.frame_signature != CaptureRefFrameSignature(wc)) {
      ref_snapshot_cache_.erase(it);
      return nullptr;
    }
    return &it->second;
  }

  std::optional<CachedRefNode> GetCachedRefNode(content::WebContents* wc,
                                                int tab_id,
                                                ui::AXNodeID ax_id) {
    const CachedRefSnapshot* snapshot = GetValidCachedRefSnapshot(wc, tab_id);
    if (!snapshot) {
      return std::nullopt;
    }
    auto it = snapshot->nodes.find(ax_id);
    return it == snapshot->nodes.end()
               ? std::nullopt
               : std::optional<CachedRefNode>(it->second);
  }

  void StoreRefSnapshotCache(
      content::WebContents* wc,
      int tab_id,
      uint64_t navigation_epoch,
      const RefFrameSignature& frame_signature,
      const ui::AXTree& tree,
      const maho::MahoMcpAccessibilityHandler::RefTable& refs) {
    CachedRefSnapshot cache;
    cache.navigation_epoch = navigation_epoch;
    cache.frame_signature = frame_signature;
    for (const auto& ref_entry : refs) {
      const ui::AXNodeID ax_id = ref_entry.second;
      const ui::AXNode* node = tree.GetFromId(ax_id);
      if (!node) {
        continue;
      }
      const ui::AXNodeData& data = node->data();
      CachedRefNode cached;
      cached.role = data.role;
      cached.has_checked_state = data.HasCheckedState();
      cached.checked_state = cached.has_checked_state
                                 ? data.GetCheckedState()
                                 : ax::mojom::CheckedState::kNone;
      cached.is_protected = data.HasState(ax::mojom::State::kProtected);
      cached.autocomplete =
          data.GetStringAttribute(ax::mojom::StringAttribute::kAutoComplete);
      cached.accessible_name =
          data.GetStringAttribute(ax::mojom::StringAttribute::kName);
      cached.center = ComputeViewportCenterCssFromTree(tree, ax_id);
      cached.leading_center = ComputeViewportCenterCssFromTree(
          tree, ax_id, /*prefer_leading_edge=*/true);
      cache.nodes.emplace(ax_id, std::move(cached));
    }
    const int resolved_tab_id = ResolveRefCacheTabId(tab_id, wc);
    if (ref_snapshot_cache_.find(resolved_tab_id) ==
            ref_snapshot_cache_.end() &&
        ref_snapshot_cache_.size() >= kMaxCachedRefSnapshots) {
      ref_snapshot_cache_.erase(ref_snapshot_cache_.begin());
    }
    ref_snapshot_cache_[resolved_tab_id] = std::move(cache);
  }

  // Synchronously snapshot the AX tree of `wc` via RequestAXTreeSnapshot.
  // This bypasses the visibility gate on live AX mode — session-restored
  // hidden tabs still return a real tree because the snapshot is a one-shot
  // Mojo request to the renderer that runs independently of live AX state.
  // The delegate contract is synchronous today while RequestAXTreeSnapshot is
  // asynchronous Mojo. A nestable RunLoop is therefore required here to
  // service the renderer reply on the UI thread without deadlocking. Keep this
  // bridge until the delegate contract itself becomes async; the node cap below
  // bounds pathological all-frame snapshots in the meantime.
  // Returns an empty AXTreeUpdate on failure.
  ui::AXTreeUpdate SnapshotAXTree(content::WebContents* wc) {
    ui::AXTreeUpdate update;
    if (!wc) {
      return update;
    }
    base::WeakPtr<content::WebContents> wc_weak = wc->GetWeakPtr();
    base::WeakPtr<MahoMcpBrowserDelegateImpl> delegate_weak = ui_weak_ptr_;
    base::RunLoop run_loop(base::RunLoop::Type::kNestableTasksAllowed);
    wc->RequestAXTreeSnapshot(
        base::BindOnce(
            [](base::WeakPtr<content::WebContents> wc_weak,
               base::WeakPtr<MahoMcpBrowserDelegateImpl> delegate_weak,
               ui::AXTreeUpdate* out, base::OnceClosure quit,
               ui::AXTreeUpdate& u) {
              if (wc_weak && delegate_weak) {
                *out = std::move(u);
              }
              std::move(quit).Run();
            },
            wc_weak, delegate_weak, &update, run_loop.QuitClosure()),
        ui::kAXModeComplete,
        /*max_nodes=*/kMaxAxSnapshotNodes,
        /*timeout=*/base::Seconds(5),
        content::WebContents::AXTreeSnapshotPolicy::kAll);
    run_loop.Run();
    return update;
  }

  std::string GetPageText(int tab_id) override {
    content::WebContents* wc =
        (tab_id == 0) ? GetActiveWebContents() : GetWebContentsForTabId(tab_id);
    if (!wc) {
      return std::string();
    }
    ui::AXTreeUpdate update = SnapshotAXTree(wc);
    if (update.nodes.empty()) {
      return std::string();
    }
    ui::AXTree tree;
    if (!tree.Unserialize(update)) {
      return std::string();
    }
    const ui::AXNode* root = tree.root();
    if (!root) {
      return std::string();
    }

    std::string out;
    std::vector<const ui::AXNode*> stack{root};
    while (!stack.empty()) {
      const ui::AXNode* node = stack.back();
      stack.pop_back();
      const std::string& name =
          node->data().GetStringAttribute(ax::mojom::StringAttribute::kName);
      const auto role = node->GetRole();
      if (!name.empty() && (role == ax::mojom::Role::kStaticText ||
                            role == ax::mojom::Role::kInlineTextBox ||
                            role == ax::mojom::Role::kHeading ||
                            role == ax::mojom::Role::kParagraph ||
                            role == ax::mojom::Role::kLink ||
                            role == ax::mojom::Role::kButton)) {
        if (!out.empty()) {
          out.push_back('\n');
        }
        out.append(name);
      }
      for (auto it = node->children().rbegin(); it != node->children().rend();
           ++it) {
        stack.push_back(*it);
      }
    }
    return out;
  }

  base::Value GetAccessibilitySnapshot(
      int tab_id,
      maho::MahoMcpSession::RefTable* out_refs) override {
    uint64_t ignored_snapshot_token = 0;
    return GetAccessibilitySnapshot(tab_id, out_refs,
                                    &ignored_snapshot_token);
  }

  base::Value GetAccessibilitySnapshot(
      int tab_id,
      maho::MahoMcpSession::RefTable* out_refs,
      uint64_t* out_snapshot_token) override {
    content::WebContents* wc =
        (tab_id == 0) ? GetActiveWebContents() : GetWebContentsForTabId(tab_id);
    base::DictValue empty;
    empty.Set("role", "WebArea");
    empty.Set("children", base::ListValue());
    if (!wc) {
      return base::Value(std::move(empty));
    }
    maho::MahoMcpNavigationTracker::CreateForWebContents(wc);
    const uint64_t navigation_epoch = GetNavigationEpoch(tab_id);
    if (navigation_epoch == 0) {
      return base::Value(std::move(empty));
    }
    InvalidateCachedRefSnapshot(tab_id, wc);
    const RefFrameSignature frame_signature = CaptureRefFrameSignature(wc);
    ui::AXTreeUpdate update = SnapshotAXTree(wc);
    if (navigation_epoch != GetNavigationEpoch(tab_id) ||
        frame_signature != CaptureRefFrameSignature(wc)) {
      if (out_refs) {
        out_refs->clear();
      }
      return base::Value(std::move(empty));
    }
    if (update.nodes.empty()) {
      return base::Value(std::move(empty));
    }
    NormalizeMcpSnapshotRoles(&update);
    ui::AXTree tree;
    if (!tree.Unserialize(update)) {
      return base::Value(std::move(empty));
    }

    maho::MahoMcpAccessibilityHandler::RefTable local_refs;
    base::Value snapshot =
        maho::MahoMcpAccessibilityHandler::BuildSnapshot(tree, local_refs);
    StoreRefSnapshotCache(wc, tab_id, navigation_epoch, frame_signature, tree,
                          local_refs);
    if (out_refs) {
      out_refs->clear();
      for (const auto& pair : local_refs) {
        out_refs->emplace(pair.first, pair.second);
      }
    }
    if (out_snapshot_token) {
      const uint64_t token = ++next_ref_snapshot_token_;
      if (ref_snapshot_guards_.size() == kMaxRefSnapshotGuards) {
        ref_snapshot_guards_.erase(ref_snapshot_guards_.begin());
      }
      ref_snapshot_guards_[token] = {McpSessionIdOf(wc), navigation_epoch,
                                      frame_signature};
      *out_snapshot_token = token;
    }
    return snapshot;
  }

  AccessibilitySnapshotV2Result GetAccessibilitySnapshotV2(
      const AccessibilitySnapshotV2Params& params,
      maho::MahoMcpSession::RefTable* out_refs,
      maho::MahoMcpAccessibilityHandler::ObservationCache& observation_cache,
      uint64_t token_sequence) override {
    AccessibilitySnapshotV2Result result;
    const int tab_id = params.tab_id;
    content::WebContents* wc =
        (tab_id == 0) ? GetActiveWebContents() : GetWebContentsForTabId(tab_id);
    if (!wc) {
      result.snapshot_token = "s_" + std::to_string(tab_id) + "_" +
                              std::to_string(token_sequence);
      result.tab.id = tab_id;
      result.tree = "WebArea\n";
      result.bytes = result.tree.size();
      return result;
    }

    maho::MahoMcpNavigationTracker::CreateForWebContents(wc);
    const uint64_t navigation_epoch = GetNavigationEpoch(tab_id);
    const RefFrameSignature frame_signature = CaptureRefFrameSignature(wc);
    std::string frame_sig_str;
    for (const auto& entry : frame_signature) {
      if (!frame_sig_str.empty()) {
        frame_sig_str += ",";
      }
      frame_sig_str += std::to_string(entry.first.child_id.value()) + ":" +
                       std::to_string(entry.first.frame_routing_id) +
                       ":" + entry.second;
    }

    base::TimeTicks t0 = base::TimeTicks::Now();
    ui::AXTreeUpdate update = SnapshotAXTree(wc);
    base::TimeTicks t1 = base::TimeTicks::Now();
    result.renderer_snapshot_ms = (t1 - t0).InMillisecondsF();

    if (navigation_epoch != GetNavigationEpoch(tab_id) ||
        frame_signature != CaptureRefFrameSignature(wc) ||
        update.nodes.empty()) {
      if (out_refs) {
        out_refs->clear();
      }
      result.snapshot_token = "s_" + std::to_string(tab_id) + "_" +
                              std::to_string(token_sequence);
      result.tab.id = tab_id;
      result.tab.url = wc->GetLastCommittedURL().spec();
      result.tab.title = base::UTF16ToUTF8(wc->GetTitle());
      result.tree = "WebArea\n";
      result.bytes = result.tree.size();
      return result;
    }

    NormalizeMcpSnapshotRoles(&update);
    ui::AXTree tree;
    if (!tree.Unserialize(update)) {
      result.snapshot_token = "s_" + std::to_string(tab_id) + "_" +
                              std::to_string(token_sequence);
      result.tab.id = tab_id;
      result.tab.url = wc->GetLastCommittedURL().spec();
      result.tab.title = base::UTF16ToUTF8(wc->GetTitle());
      result.tree = "WebArea\n";
      result.bytes = result.tree.size();
      return result;
    }

    maho::MahoMcpAccessibilityHandler::SnapshotV2Options opts;
    if (params.mode == "compact") {
      opts.mode =
          maho::MahoMcpAccessibilityHandler::SnapshotMode::kCompact;
    } else if (params.mode == "full") {
      opts.mode = maho::MahoMcpAccessibilityHandler::SnapshotMode::kFull;
    } else {
      opts.mode =
          maho::MahoMcpAccessibilityHandler::SnapshotMode::kInteractive;
    }
    opts.include_hidden = params.include_hidden;
    opts.scope_selector = params.scope_selector;
    opts.scope_ref = params.scope_ref;
    opts.since_snapshot_token = params.since_snapshot_token;
    opts.max_bytes = params.max_bytes;
    opts.max_depth = params.max_depth;

    maho::MahoMcpAccessibilityHandler::SnapshotV2Result::TabMetadata tab_meta;
    tab_meta.id = (tab_id != 0) ? tab_id : 1;
    tab_meta.url = wc->GetLastCommittedURL().spec();
    tab_meta.title = base::UTF16ToUTF8(wc->GetTitle());

    maho::MahoMcpAccessibilityHandler::RefTable local_refs;
    base::TimeTicks t2 = base::TimeTicks::Now();
    auto v2_res = maho::MahoMcpAccessibilityHandler::BuildSnapshotV2(
        tree, opts, local_refs, tab_meta, navigation_epoch, frame_sig_str,
        observation_cache, token_sequence);
    base::TimeTicks t3 = base::TimeTicks::Now();
    result.ax_serialize_ms = (t3 - t2).InMillisecondsF();

    StoreRefSnapshotCache(wc, tab_id, navigation_epoch, frame_signature, tree,
                          local_refs);
    if (out_refs) {
      out_refs->clear();
      for (const auto& pair : local_refs) {
        out_refs->emplace(pair.first, pair.second);
      }
    }

    result.snapshot_token = v2_res.snapshot_token;
    result.tab.id = tab_meta.id;
    result.tab.url = tab_meta.url;
    result.tab.title = tab_meta.title;
    result.tree = std::move(v2_res.tree);
    result.diff = std::move(v2_res.diff);
    result.captured_nodes = v2_res.stats.captured_nodes;
    result.serialized_nodes = v2_res.stats.serialized_nodes;
    result.bytes = v2_res.stats.bytes;
    result.truncated = v2_res.stats.truncated;
    return result;
  }

  bool RevalidateRefSnapshot(const maho::ResolvedMahoMcpTarget& target,
                             uint64_t snapshot_token) override {
    auto guard = ref_snapshot_guards_.find(snapshot_token);
    if (guard == ref_snapshot_guards_.end() || !target.valid ||
        target.tab_id != guard->second.tab_id ||
        GetNavigationEpoch(target.tab_id) != guard->second.navigation_epoch) {
      return false;
    }
    content::WebContents* wc = GetWebContentsForTabId(target.tab_id);
    return wc &&
           CaptureRefFrameSignature(wc) == guard->second.frame_signature;
  }

  uint64_t GetNavigationEpoch(int tab_id) {
    content::WebContents* wc =
        tab_id == 0 ? GetActiveWebContents() : GetWebContentsForTabId(tab_id);
    if (!wc) {
      return 0;
    }
    const content::NavigationEntry* entry =
        wc->GetController().GetLastCommittedEntry();
    return entry ? static_cast<uint64_t>(entry->GetUniqueID()) : 0;
  }

  void CaptureFullPagePngBase64(
      int tab_id,
      base::OnceCallback<void(std::string)> done) override {
    content::WebContents* wc =
        (tab_id == 0) ? GetActiveWebContents() : GetWebContentsForTabId(tab_id);
    if (!wc) {
      std::move(done).Run(std::string());
      return;
    }
    content::RenderWidgetHostView* view = wc->GetRenderWidgetHostView();
    if (!view) {
      std::move(done).Run(std::string());
      return;
    }
    int resolved_tab_id = tab_id;
    if (resolved_tab_id == 0) {
      resolved_tab_id = McpSessionIdOf(wc);
    }
    // Wake HIDDEN tab so compositor renders before capture.
    const bool was_hidden = wc->GetVisibility() == content::Visibility::HIDDEN;
    if (was_hidden) {
      wc->WasShown();
    }
    // For newly-shown tabs, wait 300ms for compositor to render a frame.
    if (was_hidden) {
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
          FROM_HERE,
          base::BindOnce(&MahoMcpBrowserDelegateImpl::CaptureVisibleViewFromTab,
                         weak_factory_.GetWeakPtr(), resolved_tab_id,
                         was_hidden, std::move(done)),
          base::Milliseconds(300));
    } else {
      view->CopyFromSurface(
          gfx::Rect(), gfx::Size(), base::Seconds(5),
          base::BindOnce(&MahoMcpBrowserDelegateImpl::OnScreenshotCaptured,
                         weak_factory_.GetWeakPtr(), resolved_tab_id,
                         was_hidden, std::move(done)));
    }
  }

  void CaptureVisibleViewFromTab(int tab_id,
                                 bool was_hidden,
                                 base::OnceCallback<void(std::string)> done) {
    content::WebContents* wc = GetWebContentsForTabId(tab_id);
    if (!wc) {
      std::move(done).Run(std::string());
      return;
    }

    content::RenderWidgetHostView* view = wc->GetRenderWidgetHostView();
    if (!view) {
      if (was_hidden) {
        wc->WasHidden();
      }
      std::move(done).Run(std::string());
      return;
    }

    view->CopyFromSurface(
        gfx::Rect(), gfx::Size(), base::Seconds(5),
        base::BindOnce(&MahoMcpBrowserDelegateImpl::OnScreenshotCaptured,
                       weak_factory_.GetWeakPtr(), tab_id, was_hidden,
                       std::move(done)));
  }

  void OnScreenshotCaptured(int tab_id,
                            bool was_hidden,
                            base::OnceCallback<void(std::string)> done,
                            const content::CopyFromSurfaceResult& res) {
    // Restore HIDDEN visibility if we forced Shown.
    if (was_hidden) {
      if (content::WebContents* wc = GetWebContentsForTabId(tab_id)) {
        wc->WasHidden();
      }
    }
    if (!res.has_value()) {
      std::move(done).Run(std::string());
      return;
    }
    SkBitmap captured = res.value().bitmap;
    if (captured.drawsNothing()) {
      std::move(done).Run(std::string());
      return;
    }
    content::WebContents* wc = GetWebContentsForTabId(tab_id);
    ui::AXTreeUpdate ax_update;
    if (wc) {
      ax_update = SnapshotAXTree(wc);
    }
    ui::AXTree ax_tree;
    const ui::AXTree* tree =
        ax_tree.Unserialize(ax_update) ? &ax_tree : nullptr;
    maho::MahoMcpScreenshotHandler::CaptureFullPage(
        captured, tree,
        base::BindOnce(
            [](base::OnceCallback<void(std::string)> done,
               std::vector<uint8_t> png) {
              if (png.empty()) {
                std::move(done).Run(std::string());
                return;
              }
              std::move(done).Run(
                  base::Base64Encode(base::span<const uint8_t>(png)));
            },
            std::move(done)));
  }

  void CaptureElementPngBase64(
      int tab_id,
      ui::AXNodeID ax_id,
      base::OnceCallback<void(std::string)> done) override {
    content::WebContents* wc =
        (tab_id == 0) ? GetActiveWebContents() : GetWebContentsForTabId(tab_id);
    if (!wc) {
      std::move(done).Run(std::string());
      return;
    }
    content::RenderWidgetHostView* view = wc->GetRenderWidgetHostView();
    if (!view) {
      std::move(done).Run(std::string());
      return;
    }
    int resolved_tab_id = tab_id;
    if (resolved_tab_id == 0) {
      resolved_tab_id = McpSessionIdOf(wc);
    }
    const bool was_hidden = wc->GetVisibility() == content::Visibility::HIDDEN;
    if (was_hidden) {
      wc->WasShown();
    }
    if (was_hidden) {
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
          FROM_HERE,
          base::BindOnce(
              &MahoMcpBrowserDelegateImpl::CaptureVisibleViewFromTabForElement,
              weak_factory_.GetWeakPtr(), resolved_tab_id, was_hidden, ax_id,
              std::move(done)),
          base::Milliseconds(300));
    } else {
      view->CopyFromSurface(
          gfx::Rect(), gfx::Size(), base::Seconds(5),
          base::BindOnce(
              &MahoMcpBrowserDelegateImpl::OnElementScreenshotCaptured,
              weak_factory_.GetWeakPtr(), resolved_tab_id, was_hidden, ax_id,
              std::move(done)));
    }
  }

  void CaptureVisibleViewFromTabForElement(
      int tab_id,
      bool was_hidden,
      ui::AXNodeID ax_id,
      base::OnceCallback<void(std::string)> done) {
    content::WebContents* wc = GetWebContentsForTabId(tab_id);
    if (!wc) {
      std::move(done).Run(std::string());
      return;
    }
    content::RenderWidgetHostView* view = wc->GetRenderWidgetHostView();
    if (!view) {
      if (was_hidden) {
        wc->WasHidden();
      }
      std::move(done).Run(std::string());
      return;
    }
    view->CopyFromSurface(
        gfx::Rect(), gfx::Size(), base::Seconds(5),
        base::BindOnce(&MahoMcpBrowserDelegateImpl::OnElementScreenshotCaptured,
                       weak_factory_.GetWeakPtr(), tab_id, was_hidden, ax_id,
                       std::move(done)));
  }

  void OnElementScreenshotCaptured(int tab_id,
                                   bool was_hidden,
                                   ui::AXNodeID ax_id,
                                   base::OnceCallback<void(std::string)> done,
                                   const content::CopyFromSurfaceResult& res) {
    if (was_hidden) {
      if (content::WebContents* wc = GetWebContentsForTabId(tab_id)) {
        wc->WasHidden();
      }
    }
    if (!res.has_value()) {
      std::move(done).Run(std::string());
      return;
    }
    SkBitmap captured = res.value().bitmap;
    if (captured.drawsNothing()) {
      std::move(done).Run(std::string());
      return;
    }
    content::WebContents* wc = GetWebContentsForTabId(tab_id);
    ui::AXTreeUpdate ax_update;
    if (wc) {
      ax_update = SnapshotAXTree(wc);
    }
    ui::AXTree ax_tree;
    const ui::AXTree* tree =
        ax_tree.Unserialize(ax_update) ? &ax_tree : nullptr;
    maho::MahoMcpScreenshotHandler::CaptureElement(
        captured, tree, ax_id,
        base::BindOnce(
            [](base::OnceCallback<void(std::string)> done,
               std::vector<uint8_t> png) {
              if (png.empty()) {
                std::move(done).Run(std::string());
                return;
              }
              std::move(done).Run(
                  base::Base64Encode(base::span<const uint8_t>(png)));
            },
            std::move(done)));
  }

  bool Scroll(int tab_id,
              const std::string& direction,
              int pixels,
              std::optional<ui::AXNodeID> ax_id) override {
    content::GetUIThreadTaskRunner({})->PostTask(
        FROM_HERE,
        base::BindOnce(
            [](MahoMcpBrowserDelegateImpl* delegate, int id, std::string dir,
               int px, std::optional<ui::AXNodeID> ax) {
              content::WebContents* wc =
                  (id == 0) ? delegate->GetActiveWebContents()
                            : delegate->GetWebContentsForTabId(id);
              if (wc) {
                delegate->InvalidateCachedRefSnapshot(id, wc);
                maho::MahoMcpInputSynthesizer::Scroll(wc, dir, px, ax);
              }
            },
            base::Unretained(this), tab_id, direction, pixels, ax_id));
    return true;
  }

  bool Click(int tab_id, ui::AXNodeID ax_id) override {
    ClickVerified(tab_id, ax_id, base::DoNothing());
    return true;
  }

  void ClickVerified(
      int tab_id,
      ui::AXNodeID ax_id,
      base::OnceCallback<void(maho::InputActionOutcome)> done) override {
    if (!content::BrowserThread::CurrentlyOn(content::BrowserThread::UI)) {
      if (base::SequencedTaskRunner::HasCurrentDefault()) {
        done = base::BindPostTaskToCurrentDefault(std::move(done));
      }
      content::GetUIThreadTaskRunner({})->PostTask(
          FROM_HERE,
          base::BindOnce(&MahoMcpBrowserDelegateImpl::ClickVerified,
                         ui_weak_ptr_, tab_id, ax_id, std::move(done)));
      return;
    }

    content::WebContents* wc =
        (tab_id == 0) ? GetActiveWebContents() : GetWebContentsForTabId(tab_id);
    if (!wc) {
      maho::InputActionOutcome outcome;
      outcome.dispatched = false;
      outcome.reason = "no_web_contents";
      outcome.method = "trusted_input";
      std::move(done).Run(std::move(outcome));
      return;
    }

    bool target_found = false;
    ax::mojom::Role target_role = ax::mojom::Role::kUnknown;
    bool target_has_checked_state = false;
    ax::mojom::CheckedState target_checked_state =
        ax::mojom::CheckedState::kNone;
    std::string target_name;
    std::optional<gfx::PointF> center;

    if (std::optional<CachedRefNode> cached =
            GetCachedRefNode(wc, tab_id, ax_id)) {
      target_found = true;
      target_role = cached->role;
      target_has_checked_state = cached->has_checked_state;
      target_checked_state = cached->checked_state;
      target_name = cached->accessible_name;
      center = target_role == ax::mojom::Role::kCheckBox
                   ? cached->leading_center
                   : cached->center;
    } else {
      ui::AXTreeUpdate update = SnapshotAXTree(wc);
      const ui::AXNodeData* target_node = nullptr;
      for (const ui::AXNodeData& node : update.nodes) {
        if (node.id == ax_id) {
          target_node = &node;
          break;
        }
      }
      if (target_node) {
        target_found = true;
        target_role = target_node->role;
        target_has_checked_state = target_node->HasCheckedState();
        target_checked_state =
            target_has_checked_state ? target_node->GetCheckedState()
                                     : ax::mojom::CheckedState::kNone;
        target_name = target_node->GetStringAttribute(
            ax::mojom::StringAttribute::kName);
        content::RenderWidgetHostView* view = wc->GetRenderWidgetHostView();
        const float device_scale_factor =
            view ? view->GetDeviceScaleFactor() : 1.f;
        center = ComputeViewportCenterCss(
            update, ax_id, device_scale_factor,
            target_role == ax::mojom::Role::kCheckBox);
      }
    }
    // Coordinates describe the exact layout captured with the RefTable. An
    // input action can mutate layout, so consume this cache entry once.
    InvalidateCachedRefSnapshot(tab_id, wc);

    if (!target_found) {
      maho::InputActionOutcome outcome;
      outcome.dispatched = false;
      outcome.reason = "node_gone";
      outcome.method = "trusted_input";
      std::move(done).Run(std::move(outcome));
      return;
    }

    const bool is_toggleable =
        target_role == ax::mojom::Role::kCheckBox ||
        target_role == ax::mojom::Role::kRadioButton ||
        target_role == ax::mojom::Role::kSwitch ||
        target_role == ax::mojom::Role::kMenuItemCheckBox ||
        target_role == ax::mojom::Role::kMenuItemRadio;
    const bool is_radio =
        target_role == ax::mojom::Role::kRadioButton ||
        target_role == ax::mojom::Role::kMenuItemRadio;
    const ax::mojom::CheckedState pre_checked =
        target_has_checked_state ? target_checked_state
                                 : ax::mojom::CheckedState::kNone;

    bool clicked = false;
    std::string method = "trusted_input";
    if (center) {
      const int cx = std::lround(center->x());
      const int cy = std::lround(center->y());
      // Trusted input: Blink expands a real mouse press+release into the
      // full pointerdown -> mousedown -> pointerup -> mouseup -> click sequence.
      clicked = maho::MahoMcpInputSynthesizer::ClickAt(wc, cx, cy, {});
    }

    // Name-based DOM fallback: re-finds an element by accessible name and
    // clicks the FIRST match, so it must only run when trusted input did NOT
    // dispatch. Running it after a successful trusted click would click a
    // different element on pages with duplicate accessible names (e.g. two
    // radio groups both labelled 동의함) and silently corrupt that element's
    // state while the target itself was already handled.
    if (!clicked && !target_name.empty()) {
        const std::string js_script =
            "(()=>{const target = " + base::GetQuotedJSONString(target_name) + ".trim().toLowerCase();"
            "const chks = Array.from(document.querySelectorAll('input[type=checkbox], input[type=radio]'));"
            "for (const c of chks) {"
            "  const elName = (c.getAttribute('name') || c.getAttribute('aria-label') || c.id || '').trim().toLowerCase();"
            "  const elLabel = c.labels && c.labels[0] ? c.labels[0].innerText.trim().toLowerCase() : '';"
            "  const parentText = (c.parentElement ? c.parentElement.innerText : '').trim().toLowerCase();"
            "  if ((elName.length > 0 && (elName === target || elName.includes(target))) ||"
            "      (elLabel.length > 0 && (elLabel === target || elLabel.includes(target))) ||"
            "      (parentText.length > 0 && parentText.includes(target))) {"
            "    if (typeof c.click === 'function') {"
            "      c.click();"
            "    } else {"
            "      c.checked = !c.checked;"
            "      c.dispatchEvent(new Event('change', {bubbles: true, cancelable: true}));"
            "    }"
            "    return 'toggled';"
            "  }"
            "}"
            "const elements = Array.from(document.querySelectorAll('button, a, input[type=button], input[type=submit], [role=button], [role=link], [role=tab]'));"
            "let bestMatch = null;"
            "for (const el of elements) {"
            "  const img = el.querySelector('img');"
            "  const imgAlt = (img ? (img.getAttribute('alt') || img.getAttribute('title') || '') : '').trim().toLowerCase();"
            "  const text = (el.innerText || el.value || el.textContent || el.getAttribute('aria-label') || el.getAttribute('title') || imgAlt || '').trim().toLowerCase();"
            "  if (text.length > 0 || imgAlt.length > 0) {"
            "    if (text === target || imgAlt === target || el.getAttribute('aria-label') === target) {"
            "      bestMatch = el; break;"
            "    } else if (text.includes(target) || imgAlt.includes(target) || (target.length >= 2 && text.length >= 2 && target.includes(text))) {"
            "      if (!bestMatch) bestMatch = el;"
            "    }"
            "  }"
            "}"
            "if (bestMatch) {"
            "  bestMatch.dispatchEvent(new MouseEvent('mousedown', {bubbles: true, cancelable: true, view: window}));"
            "  bestMatch.dispatchEvent(new MouseEvent('mouseup', {bubbles: true, cancelable: true, view: window}));"
            "  bestMatch.dispatchEvent(new MouseEvent('click', {bubbles: true, cancelable: true, view: window}));"
            "  if (typeof bestMatch.click === 'function') bestMatch.click();"
            "  return 'clicked';"
            "}"
            "return '';})()";
        std::string res = ExecuteMcpActionJsAllFramesBlocking(wc, js_script);
        if (res == "clicked" || res == "toggled") {
          clicked = true;
        }
    }

    // Also perform semantic DOM default action across frames to guarantee click
    // delivery to event listeners inside subframes/custom controls.
    if (PerformAxAction(wc, ax_id, ax::mojom::Action::kDoDefault)) {
      clicked = true;
    }
    if (!clicked) {
      method = "ax_fallback";
    }

    if (!clicked) {
      maho::InputActionOutcome outcome;
      outcome.dispatched = false;
      outcome.reason = "click_failed";
      outcome.method = method;
      std::move(done).Run(std::move(outcome));
      return;
    }

    if (is_toggleable) {
      VerifyToggleableClickAsync(wc->GetWeakPtr(), ax_id, pre_checked, is_radio,
                                 method, std::move(done), /*attempts_left=*/4);
      return;
    }

    // Non-toggleable roles (buttons, links, generic)
    maho::InputActionOutcome outcome;
    outcome.dispatched = true;
    outcome.verified = std::nullopt;
    outcome.method = method;
    outcome.reason = "unverifiable_role";
    std::move(done).Run(std::move(outcome));
  }

  void VerifyToggleableClickAsync(
      base::WeakPtr<content::WebContents> wc_weak,
      ui::AXNodeID ax_id,
      ax::mojom::CheckedState pre_checked,
      bool is_radio,
      std::string method,
      base::OnceCallback<void(maho::InputActionOutcome)> done,
      int attempts_left) {
    content::GetUIThreadTaskRunner({})->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(
            [](base::WeakPtr<MahoMcpBrowserDelegateImpl> delegate,
               base::WeakPtr<content::WebContents> wc_weak, ui::AXNodeID ax,
               ax::mojom::CheckedState pre_checked, bool is_radio,
               std::string method,
               base::OnceCallback<void(maho::InputActionOutcome)> done,
               int attempts_left) {
              if (!delegate) {
                maho::InputActionOutcome outcome;
                outcome.dispatched = false;
                outcome.reason = "delegate_destroyed";
                outcome.method = method;
                std::move(done).Run(std::move(outcome));
                return;
              }
              content::WebContents* current_wc = wc_weak.get();
              if (!current_wc) {
                maho::InputActionOutcome outcome;
                outcome.dispatched = false;
                outcome.reason = "no_web_contents";
                outcome.method = method;
                std::move(done).Run(std::move(outcome));
                return;
              }

              ui::AXTreeUpdate post_tree = delegate->SnapshotAXTree(current_wc);
              const ui::AXNodeData* post_node = nullptr;
              for (const ui::AXNodeData& node : post_tree.nodes) {
                if (node.id == ax) {
                  post_node = &node;
                  break;
                }
              }

              if (post_node) {
                const ax::mojom::CheckedState post_checked =
                    post_node->HasCheckedState()
                        ? post_node->GetCheckedState()
                        : ax::mojom::CheckedState::kNone;
                bool toggled = is_radio ? (post_checked == ax::mojom::CheckedState::kTrue)
                                        : (post_checked != pre_checked);
                if (toggled) {
                  maho::InputActionOutcome outcome;
                  outcome.dispatched = true;
                  outcome.verified = true;
                  outcome.method = method;
                  std::move(done).Run(std::move(outcome));
                  return;
                }
              }

              if (attempts_left > 1) {
                delegate->VerifyToggleableClickAsync(
                    wc_weak, ax, pre_checked, is_radio, method,
                    std::move(done), attempts_left - 1);
                return;
              }

              maho::InputActionOutcome outcome;
              outcome.dispatched = true;
              outcome.method = method;
              outcome.verified = false;
              outcome.reason = post_node ? "state_not_toggled" : "node_gone";
              std::move(done).Run(std::move(outcome));
            },
            ui_weak_ptr_, wc_weak, ax_id, pre_checked, is_radio, method,
            std::move(done), attempts_left),
        base::Milliseconds(60));
  }

  bool RequiresDeferredVerifiedInputResponses() const override { return true; }

  bool Type(int tab_id, ui::AXNodeID ax_id, const std::string& text) override {
    TypeVerified(tab_id, ax_id, text, base::DoNothing());
    return true;
  }

  void TypeVerified(
      int tab_id,
      ui::AXNodeID ax_id,
      const std::string& text,
      base::OnceCallback<void(maho::InputActionOutcome)> done) override {
    if (!content::BrowserThread::CurrentlyOn(content::BrowserThread::UI)) {
      if (base::SequencedTaskRunner::HasCurrentDefault()) {
        done = base::BindPostTaskToCurrentDefault(std::move(done));
      }
      content::GetUIThreadTaskRunner({})->PostTask(
          FROM_HERE,
          base::BindOnce(&MahoMcpBrowserDelegateImpl::TypeVerified,
                         ui_weak_ptr_, tab_id, ax_id, text, std::move(done)));
      return;
    }

    content::WebContents* wc =
        (tab_id == 0) ? GetActiveWebContents() : GetWebContentsForTabId(tab_id);
    if (!wc) {
      maho::InputActionOutcome outcome;
      outcome.dispatched = false;
      outcome.reason = "no_web_contents";
      outcome.method = "trusted_input";
      std::move(done).Run(std::move(outcome));
      return;
    }

    content::RenderWidgetHostView* view = wc->GetRenderWidgetHostView();
    const bool initial_has_focus = view ? view->HasFocus() : false;
    const float device_scale_factor =
        view ? view->GetDeviceScaleFactor() : 1.f;

    std::optional<gfx::PointF> center;
    if (std::optional<CachedRefNode> cached =
            GetCachedRefNode(wc, tab_id, ax_id)) {
      center = cached->center;
    } else {
      ui::AXTreeUpdate update = SnapshotAXTree(wc);
      center = ComputeViewportCenterCss(update, ax_id, device_scale_factor);
    }
    // Typing can synchronously reflow or validate the page; consume the point
    // captured with the RefTable before dispatching input.
    InvalidateCachedRefSnapshot(tab_id, wc);

    auto on_typed = base::BindOnce(
        [](base::WeakPtr<MahoMcpBrowserDelegateImpl> delegate,
           base::WeakPtr<content::WebContents> wc_weak, int target_tab_id,
           ui::AXNodeID ax, std::string target_text, bool initial_focus,
           base::OnceCallback<void(maho::InputActionOutcome)> done_cb,
           bool typed_success) {
          if (!delegate) {
            maho::InputActionOutcome outcome;
            outcome.dispatched = false;
            outcome.reason = "delegate_destroyed";
            outcome.method = "trusted_input";
            std::move(done_cb).Run(std::move(outcome));
            return;
          }
          if (typed_success) {
            delegate->VerifyTypedInputAsync(
                wc_weak, target_tab_id, ax, std::move(target_text),
                initial_focus, std::move(done_cb), /*attempts_left=*/6);
          } else {
            delegate->PerformAxFallback(
                wc_weak, target_tab_id, ax, std::move(target_text),
                initial_focus, std::move(done_cb));
          }
        },
        ui_weak_ptr_, wc->GetWeakPtr(), tab_id, ax_id, text, initial_has_focus,
        std::move(done));

    if (center) {
      const int cx = std::lround(center->x());
      const int cy = std::lround(center->y());
      // Focus the field with a trusted click whose pointer events travel a
      // bounded interpolated trajectory, then continue typing once the full
      // click sequence has dispatched. A short settle delay still lets the
      // clicked frame (including child iframes) establish focus in
      // WebContents before key events are dispatched.
      maho::MahoMcpInputSynthesizer::ClickAtAsync(
          wc, cx, cy, {},
          /*sensitive=*/true,
          maho::MahoActionMarkerService::Kind::kClick,
          maho::ClickMotionProfile{},
          base::BindOnce(
              [](base::WeakPtr<content::WebContents> wc_weak,
                 std::string text_to_type,
                 base::OnceCallback<void(bool)> typed_cb,
                 bool clicked) {
                if (!clicked || !wc_weak) {
                  std::move(typed_cb).Run(false);
                  return;
                }
                content::GetUIThreadTaskRunner({})->PostDelayedTask(
                    FROM_HERE,
                    base::BindOnce(
                        [](base::WeakPtr<content::WebContents> wc_weak,
                           std::string text_to_type,
                           base::OnceCallback<void(bool)> typed_cb) {
                          content::WebContents* current_wc = wc_weak.get();
                          if (!current_wc) {
                            std::move(typed_cb).Run(false);
                            return;
                          }
                          const std::vector<std::string> select_all_modifiers =
#if BUILDFLAG(IS_MAC)
                              {"meta"};
#else
                              {"ctrl"};
#endif
                          maho::MahoMcpInputSynthesizer::KeyPress(
                              current_wc, "a", select_all_modifiers);
                          maho::MahoMcpInputSynthesizer::KeyPress(
                              current_wc, "Backspace", {});
                          maho::MahoMcpInputSynthesizer::KeyPress(
                              current_wc, "Delete", {});
                          maho::MahoMcpInputSynthesizer::TypeTextAsync(
                              current_wc, text_to_type, maho::TypingOptions{},
                              std::move(typed_cb));
                        },
                        wc_weak, std::move(text_to_type),
                        std::move(typed_cb)),
                    base::Milliseconds(30));
              },
              wc->GetWeakPtr(), text, std::move(on_typed)));
      return;
    }

    // If no center found or click failed, immediately execute AX fallback path.
    std::move(on_typed).Run(/*typed_success=*/false);
  }

  void VerifyTypedInputAsync(
      base::WeakPtr<content::WebContents> wc_weak,
      int target_tab_id,
      ui::AXNodeID ax,
      std::string target_text,
      bool initial_focus,
      base::OnceCallback<void(maho::InputActionOutcome)> done_cb,
      int attempts_left) {
    content::WebContents* current_wc = wc_weak.get();
    if (!current_wc) {
      maho::InputActionOutcome outcome;
      outcome.dispatched = false;
      outcome.reason = "no_web_contents";
      outcome.method = "trusted_input";
      std::move(done_cb).Run(std::move(outcome));
      return;
    }

    ui::AXTreeUpdate post_tree = SnapshotAXTree(current_wc);
    std::string observed_val;
    bool node_found = false;
    bool node_editable = false;
    bool is_protected = false;
    ReadAxNodeValue(post_tree, ax, &observed_val, &node_found, &node_editable,
                    &is_protected);

    if (node_found &&
        IsInputValueMatching(observed_val, target_text, is_protected)) {
      maho::InputActionOutcome outcome;
      outcome.dispatched = true;
      outcome.verified = true;
      outcome.method = "trusted_input";
      std::move(done_cb).Run(std::move(outcome));
      return;
    }

    if (attempts_left > 1) {
      content::GetUIThreadTaskRunner({})->PostDelayedTask(
          FROM_HERE,
          base::BindOnce(&MahoMcpBrowserDelegateImpl::VerifyTypedInputAsync,
                         ui_weak_ptr_, wc_weak, target_tab_id, ax,
                         std::move(target_text), initial_focus,
                         std::move(done_cb), attempts_left - 1),
          base::Milliseconds(80));
      return;
    }

    // Retries exhausted: fall back to AX mutation.
    PerformAxFallback(wc_weak, target_tab_id, ax, std::move(target_text),
                      initial_focus, std::move(done_cb));
  }

  void PerformAxFallback(
      base::WeakPtr<content::WebContents> wc_weak,
      int target_tab_id,
      ui::AXNodeID ax,
      std::string target_text,
      bool initial_focus,
      base::OnceCallback<void(maho::InputActionOutcome)> done_cb) {
    content::WebContents* current_wc = wc_weak.get();
    if (!current_wc) {
      maho::InputActionOutcome outcome;
      outcome.dispatched = false;
      outcome.reason = "no_web_contents";
      outcome.method = "ax_fallback";
      std::move(done_cb).Run(std::move(outcome));
      return;
    }

    // Target-bound fallback mutation:
    // Check credential protections first.
    maho::MahoMcpFieldMetadata meta = GetFieldMetadata(target_tab_id, ax);
    if (maho::MahoMcpSession::IsCredentialField(meta)) {
      maho::InputActionOutcome outcome;
      outcome.dispatched = false;
      outcome.verified = false;
      outcome.reason = "credential_field";
      outcome.method = "ax_fallback";
      std::move(done_cb).Run(std::move(outcome));
      return;
    }

    ui::AXTreeUpdate pre_ax_tree = SnapshotAXTree(current_wc);
    std::string pre_ax_val;
    bool pre_ax_found = false;
    bool pre_ax_editable = false;
    bool pre_ax_protected = false;
    ReadAxNodeValue(pre_ax_tree, ax, &pre_ax_val, &pre_ax_found,
                    &pre_ax_editable, &pre_ax_protected);
    if (!pre_ax_found) {
      maho::InputActionOutcome outcome;
      outcome.dispatched = false;
      outcome.verified = false;
      outcome.reason = "node_gone";
      outcome.method = "ax_fallback";
      std::move(done_cb).Run(std::move(outcome));
      return;
    }
    if (!pre_ax_editable) {
      maho::InputActionOutcome outcome;
      outcome.dispatched = false;
      outcome.verified = false;
      outcome.reason = "element_not_editable";
      outcome.method = "ax_fallback";
      std::move(done_cb).Run(std::move(outcome));
      return;
    }

    // Focus target AX node (best effort across all live frames)
    PerformAxAction(current_wc, ax, ax::mojom::Action::kFocus);

    // Mutate target AX node specifically across all live frames
    PerformAxAction(current_wc, ax, ax::mojom::Action::kSetValue, target_text);

    // Also set value directly via DOM script across all frames and fire input/change/blur
    // events so framework-controlled forms (React/Vue) synchronize their state.
    for (const auto& node : pre_ax_tree.nodes) {
      if (node.id == ax && node.HasStringAttribute(ax::mojom::StringAttribute::kName)) {
        const std::string name = node.GetStringAttribute(ax::mojom::StringAttribute::kName);
        if (!name.empty()) {
          const std::string script =
              "(()=>{const target = " + base::GetQuotedJSONString(name) + ".trim().toLowerCase();"
              "const val = " + base::GetQuotedJSONString(target_text) + ";"
              "const inputs = Array.from(document.querySelectorAll('input, textarea'));"
              "for (const el of inputs) {"
              "  const elName = (el.getAttribute('name') || el.getAttribute('placeholder') || el.getAttribute('aria-label') || el.id || '').trim().toLowerCase();"
              "  const elLabel = el.labels && el.labels[0] ? el.labels[0].innerText.trim().toLowerCase() : '';"
              "  const parentText = (el.parentElement ? el.parentElement.innerText : '').trim().toLowerCase();"
              "  if ((elName.length > 0 && (elName === target || elName.includes(target))) ||"
              "      (elLabel.length > 0 && (elLabel === target || elLabel.includes(target))) ||"
              "      (parentText.length > 0 && parentText.includes(target))) {"
              "    el.focus();"
              "    el.value = val;"
              "    el.dispatchEvent(new Event('input', { bubbles: true, cancelable: true }));"
              "    el.dispatchEvent(new Event('change', { bubbles: true, cancelable: true }));"
              "    el.blur();"
              "    return 'ok';"
              "  }"
              "}"
              "return '';})()";
          ExecuteMcpActionJsAllFramesBlocking(current_wc, script);
        }
        break;
      }
    }

    // AX set-value carries full UTF-8 text, unlike the trusted keyboard
    // synthesizer's physical-key path. Let the renderer apply that action
    // before reading its accessibility value back for verification.
    VerifyAxFallbackAsync(wc_weak, ax, std::move(target_text), initial_focus,
                          std::move(done_cb), /*attempts_left=*/6);
  }

  void VerifyAxFallbackAsync(
      base::WeakPtr<content::WebContents> wc_weak,
      ui::AXNodeID ax,
      std::string target_text,
      bool initial_focus,
      base::OnceCallback<void(maho::InputActionOutcome)> done_cb,
      int attempts_left) {
    content::GetUIThreadTaskRunner({})->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(
            [](base::WeakPtr<MahoMcpBrowserDelegateImpl> delegate,
               base::WeakPtr<content::WebContents> wc_weak, ui::AXNodeID ax,
               std::string target_text, bool initial_focus,
               base::OnceCallback<void(maho::InputActionOutcome)> done_cb,
               int attempts_left) {
              if (!delegate) {
                maho::InputActionOutcome outcome;
                outcome.dispatched = false;
                outcome.reason = "delegate_destroyed";
                outcome.method = "ax_fallback";
                std::move(done_cb).Run(std::move(outcome));
                return;
              }
              content::WebContents* current_wc = wc_weak.get();
              if (!current_wc) {
                maho::InputActionOutcome outcome;
                outcome.dispatched = false;
                outcome.reason = "no_web_contents";
                outcome.method = "ax_fallback";
                std::move(done_cb).Run(std::move(outcome));
                return;
              }
              ui::AXTreeUpdate post_ax_tree =
                  delegate->SnapshotAXTree(current_wc);
              std::string post_ax_val;
              bool post_ax_found = false;
              bool post_ax_editable = false;
              bool post_ax_protected = false;
              ReadAxNodeValue(post_ax_tree, ax, &post_ax_val, &post_ax_found,
                              &post_ax_editable, &post_ax_protected);

              if (post_ax_found &&
                  IsInputValueMatching(post_ax_val, target_text,
                                       post_ax_protected)) {
                maho::InputActionOutcome outcome;
                outcome.dispatched = true;
                outcome.verified = true;
                outcome.method = "ax_fallback";
                std::move(done_cb).Run(std::move(outcome));
                return;
              }

              if (attempts_left > 1) {
                delegate->VerifyAxFallbackAsync(
                    wc_weak, ax, std::move(target_text), initial_focus,
                    std::move(done_cb), attempts_left - 1);
                return;
              }

              maho::InputActionOutcome outcome;
              outcome.dispatched = true;
              outcome.method = "ax_fallback";
              outcome.verified = false;
              outcome.reason = !initial_focus
                                   ? "window_not_focused"
                                   : !post_ax_found
                                         ? "node_gone"
                                         : !post_ax_editable
                                               ? "element_not_editable"
                                               : "value_mismatch";
              std::move(done_cb).Run(std::move(outcome));
            },
            ui_weak_ptr_, wc_weak, ax, std::move(target_text), initial_focus,
            std::move(done_cb), attempts_left),
        base::Milliseconds(50));
  }

  bool SelectFileForPendingChooser(int tab_id,
                                   const std::string& path) override {
    content::WebContents* wc =
        (tab_id == 0) ? GetActiveWebContents() : GetWebContentsForTabId(tab_id);
    const base::FilePath supplied = base::FilePath::FromUTF8Unsafe(path);
    if (!wc || !supplied.IsAbsolute() || supplied.ReferencesParent()) {
      return false;
    }
    return MahoCompleteAutomationFileChooser(wc, supplied);
  }

  maho::MahoMcpFieldMetadata GetFieldMetadata(int tab_id,
                                              ui::AXNodeID ax_id) override {
    maho::MahoMcpFieldMetadata meta;
    content::WebContents* wc =
        (tab_id == 0) ? GetActiveWebContents() : GetWebContentsForTabId(tab_id);
    if (!wc) {
      return meta;
    }
    if (std::optional<CachedRefNode> cached =
            GetCachedRefNode(wc, tab_id, ax_id)) {
      meta.classified = true;
      meta.is_protected = cached->is_protected;
      meta.autocomplete = base::ToLowerASCII(cached->autocomplete);
      meta.accessible_name = base::ToLowerASCII(cached->accessible_name);
      return meta;
    }

    ui::AXTreeUpdate update = SnapshotAXTree(wc);
    for (const ui::AXNodeData& node : update.nodes) {
      if (node.id != ax_id) {
        continue;
      }
      meta.classified = true;
      meta.is_protected = node.HasState(ax::mojom::State::kProtected);
      meta.autocomplete = base::ToLowerASCII(
          node.GetStringAttribute(ax::mojom::StringAttribute::kAutoComplete));
      meta.accessible_name = base::ToLowerASCII(
          node.GetStringAttribute(ax::mojom::StringAttribute::kName));
      break;
    }
    return meta;
  }

  bool Select(int tab_id,
              ui::AXNodeID ax_id,
              const std::string& value) override {
    content::GetUIThreadTaskRunner({})->PostTask(
        FROM_HERE,
        base::BindOnce(
            [](base::WeakPtr<MahoMcpBrowserDelegateImpl> delegate, int id,
               ui::AXNodeID ax, std::string select_value) {
              if (!delegate) {
                return;
              }
              content::WebContents* wc =
                  (id == 0) ? delegate->GetActiveWebContents()
                            : delegate->GetWebContentsForTabId(id);
              if (wc) {
                delegate->InvalidateCachedRefSnapshot(id, wc);
                ui::AXTreeUpdate update = delegate->SnapshotAXTree(wc);
                content::RenderWidgetHostView* view =
                    wc->GetRenderWidgetHostView();
                const float device_scale_factor =
                    view ? view->GetDeviceScaleFactor() : 1.f;
                std::optional<gfx::PointF> center =
                    ComputeViewportCenterCss(update, ax, device_scale_factor);
                bool selected = false;
                if (center) {
                  const int cx = std::lround(center->x());
                  const int cy = std::lround(center->y());
                  // Open the (possibly custom) dropdown with a trusted click.
                  maho::MahoMcpInputSynthesizer::ClickAt(wc, cx, cy, {});
                  // Re-snapshot to locate the option now that the list is
                  // open, then click it with a trusted click. Custom options
                  // carry no @ref and are not selectable by value assignment.
                  ui::AXTreeUpdate opened = delegate->SnapshotAXTree(wc);
                  std::optional<ui::AXNodeID> option_id =
                      FindOptionNodeByText(opened, ax, select_value);
                  if (option_id) {
                    std::optional<gfx::PointF> option_center =
                        ComputeViewportCenterCss(opened, *option_id,
                                                 device_scale_factor);
                    if (option_center) {
                      selected = maho::MahoMcpInputSynthesizer::ClickAt(
                          wc, std::lround(option_center->x()),
                          std::lround(option_center->y()), {});
                    }
                  }
                  if (!selected) {
                    // Native <select> fallback: assign value + fire change.
                    const std::string value_json =
                        base::GetQuotedJSONString(select_value);
                    const std::string script =
                        "(()=>{const el=document.elementFromPoint(" +
                        base::NumberToString(cx) + "," +
                        base::NumberToString(cy) +
                        ");"
                        "if(el){el.focus();if('value' in el){el.value=" +
                        value_json +
                        ";el.dispatchEvent(new Event('input',{bubbles:true}));"
                        "el.dispatchEvent(new Event('change',{bubbles:true}));"
                        "}return 'ok';}return 'nopoint';})()";
                    selected = ExecuteMcpActionJsBlocking(wc, script) == "ok";
                  }
                }
                if (!selected) {
                  maho::MahoMcpFieldMetadata meta =
                      delegate->GetFieldMetadata(id, ax);
                  if (!maho::MahoMcpSession::IsCredentialField(meta)) {
                    PerformAxAction(wc, ax, ax::mojom::Action::kFocus);
                    PerformAxAction(wc, ax, ax::mojom::Action::kSetValue,
                                    select_value);
                  }
                }
              }
            },
            ui_weak_ptr_, tab_id, ax_id, value));
    return true;
  }

  bool Hover(int tab_id, ui::AXNodeID ax_id) override {
    content::GetUIThreadTaskRunner({})->PostTask(
        FROM_HERE,
        base::BindOnce(
            [](base::WeakPtr<MahoMcpBrowserDelegateImpl> delegate, int id,
               ui::AXNodeID ax) {
              if (!delegate) {
                return;
              }
              content::WebContents* wc =
                  (id == 0) ? delegate->GetActiveWebContents()
                            : delegate->GetWebContentsForTabId(id);
              if (wc) {
                delegate->InvalidateCachedRefSnapshot(id, wc);
                ui::AXTreeUpdate update = delegate->SnapshotAXTree(wc);
                content::RenderWidgetHostView* view =
                    wc->GetRenderWidgetHostView();
                const float device_scale_factor =
                    view ? view->GetDeviceScaleFactor() : 1.f;
                std::optional<gfx::PointF> center =
                    ComputeViewportCenterCss(update, ax, device_scale_factor);
                if (center) {
                  const std::string x_str =
                      base::NumberToString(std::lround(center->x()));
                  const std::string y_str =
                      base::NumberToString(std::lround(center->y()));
                  const std::string script =
                      "(()=>{const el=document.elementFromPoint(" + x_str +
                      "," + y_str +
                      ");"
                      "if(el){"
                      "el.dispatchEvent(new "
                      "MouseEvent('mouseover',{bubbles:true,cancelable:true,"
                      "view:window}));"
                      "el.dispatchEvent(new "
                      "MouseEvent('mouseenter',{bubbles:false,cancelable:false,"
                      "view:window}));"
                      "el.dispatchEvent(new "
                      "MouseEvent('mousemove',{bubbles:true,cancelable:true,"
                      "view:window}));"
                      "return 'ok';"
                      "}"
                      "return 'nopoint';})()";
                  ExecuteMcpActionJsBlocking(wc, script);
                  maho::MahoMcpInputSynthesizer::HoverAt(
                      wc, std::lround(center->x()), std::lround(center->y()));
                }
              }
            },
            ui_weak_ptr_, tab_id, ax_id));
    return true;
  }

  bool KeyPress(int tab_id,
                const std::string& key,
                const std::vector<std::string>& modifiers) override {
    content::GetUIThreadTaskRunner({})->PostTask(
        FROM_HERE, base::BindOnce(
                       [](MahoMcpBrowserDelegateImpl* delegate, int id,
                          std::string k, std::vector<std::string> mods) {
                         content::WebContents* wc =
                             (id == 0) ? delegate->GetActiveWebContents()
                                       : delegate->GetWebContentsForTabId(id);
                         if (wc) {
                           delegate->InvalidateCachedRefSnapshot(id, wc);
                           wc->Focus();
                           if (wc->GetRenderWidgetHostView()) {
                             wc->GetRenderWidgetHostView()->Focus();
                           }
                           maho::MahoMcpInputSynthesizer::KeyPress(wc, k, mods);
                         }
                       },
                       base::Unretained(this), tab_id, key, modifiers));
    return true;
  }

  bool ActivateTab(int tab_id) override {
    content::GetUIThreadTaskRunner({})->PostTask(
        FROM_HERE,
        base::BindOnce(
            [](int target_id) {
              ForEachCurrentBrowserWindowInterfaceOrderedByActivation(
                  [target_id](BrowserWindowInterface* browser) {
                    if (browser && browser->GetTabStripModel() &&
                        IsBrowserMcpEligible(browser)) {
                      TabStripModel* model = browser->GetTabStripModel();
                      for (int i = 0; i < model->count(); ++i) {
                        content::WebContents* c = model->GetWebContentsAt(i);
                        if (c) {
                          if (McpSessionIdOf(c) == target_id) {
                            model->ActivateTabAt(
                                i, TabStripUserGestureDetails(
                                       TabStripUserGestureDetails::GestureType::
                                           kOther));
                            return false;
                          }
                        }
                      }
                    }
                    return true;
                  });
            },
            tab_id));
    return true;
  }

  bool Navigate(int tab_id, const GURL& url) override {
    content::GetUIThreadTaskRunner({})->PostTask(
        FROM_HERE,
        base::BindOnce(
            [](MahoMcpBrowserDelegateImpl* delegate, int id,
               const GURL& nav_url) {
              content::WebContents* wc =
                  (id == 0) ? delegate->GetActiveWebContents()
                            : delegate->GetWebContentsForTabId(id);
              if (wc) {
                delegate->InvalidateCachedRefSnapshot(id, wc);
                content::NavigationController::LoadURLParams params(nav_url);
                params.transition_type = ui::PageTransitionFromInt(
                    ui::PAGE_TRANSITION_TYPED |
                    ui::PAGE_TRANSITION_FROM_ADDRESS_BAR);
                wc->GetController().LoadURLWithParams(params);
              }
            },
            base::Unretained(this), tab_id, url));
    return true;
  }

  bool SetViewportSize(int tab_id, int width, int height) override {
    content::GetUIThreadTaskRunner({})->PostTask(
        FROM_HERE,
        base::BindOnce(
            [](MahoMcpBrowserDelegateImpl* delegate, int id, int w, int h) {
              content::WebContents* wc =
                  (id == 0) ? delegate->GetActiveWebContents()
                            : delegate->GetWebContentsForTabId(id);
              if (wc && wc->GetRenderWidgetHostView()) {
                wc->GetRenderWidgetHostView()->SetSize(gfx::Size(w, h));
              }
            },
            base::Unretained(this), tab_id, width, height));
    return true;
  }

  std::vector<maho::MahoMcpBrowserDelegate::VaultCredentialSummary>
  VaultListCredentialsForActivePage(int tab_id) override {
    SweepExpiredVaultCredentialLeases(base::TimeTicks::Now());
    std::vector<maho::MahoMcpBrowserDelegate::VaultCredentialSummary> summaries;
    content::WebContents* wc =
        (tab_id == 0) ? GetActiveWebContents() : GetWebContentsForTabId(tab_id);
    const std::string page_origin = OriginForWebContents(wc);
    MahoCore* core = maho::GetCore();
    if (!core || page_origin.empty()) {
      return summaries;
    }

    // Profile isolation: secrets may only be leased for a page owned by the
    // profile that owns the browser-global core. Secondary regular profiles,
    // guest/system profiles, and OTR/incognito tabs fail closed here, before
    // any credential is read.
    Profile* profile =
        wc ? Profile::FromBrowserContext(wc->GetBrowserContext()) : nullptr;
    if (!maho::IsPasswordManagerAllowedForProfile(profile)) {
      return summaries;
    }
    const std::string profile_key =
        maho::GetProfileIdentityKey(profile->GetPath(), profile->GetPrefs());
    maho::core::VaultBackendSession session =
        maho::CreateVaultBackendSessionForProfile(profile);
    if (!session.is_valid()) {
      return summaries;
    }
    base::DictValue request;
    request.Set("origin", page_origin);
    std::string request_json;
    base::JSONWriter::Write(request, &request_json);

    if (!AuthorizeAgentCredentialUse(profile_key, page_origin)) {
      SecureZeroizeString(request_json);
      return summaries;
    }

    // Keep the profile/exact-origin/agent-action grant check immediately next
    // to the secret-bearing backend execution. Any lifecycle lock or teardown
    // that lands before this point revokes the shared grant and fails closed.
    if (!maho::passwords::MahoPasswordAuthorizationService::Get()
             ->ConsumeAuthorization(
                 profile_key, page_origin,
                 maho::passwords::PasswordAuthorizationAction::
                     kAgentCredentialUse)) {
      SecureZeroizeString(request_json);
      return summaries;
    }
    maho::core::VaultBackendResult result =
        session.ExecuteCredentials(request_json);
    SecureZeroizeString(request_json);
    if (!result.is_valid() ||
        result.status() != maho::core::VaultBackendResultStatus::kSuccess) {
      return summaries;
    }

    std::string response_json = result.ConsumeJson();
    std::optional<base::Value> parsed =
        base::JSONReader::Read(response_json, base::JSON_PARSE_RFC);
    SecureZeroizeString(response_json);
    if (!parsed || !parsed->is_list()) {
      return summaries;
    }

    for (const auto& item : parsed->GetList()) {
      if (!item.is_dict()) {
        continue;
      }
      const base::DictValue& dict = item.GetDict();
      const std::string* password = dict.FindString("password");
      if (!password || password->empty()) {
        continue;
      }
      const std::string origin = FirstCredentialOrigin(dict, page_origin);
      if (origin != page_origin) {
        continue;
      }

      const std::string handle =
          "vh_" + base::Uuid::GenerateRandomV4().AsLowercaseString();
      BrowserVaultCredentialLease lease;
      lease.profile_key = profile_key;
      lease.origin = origin;
      lease.expires_at = base::TimeTicks::Now() +
                         maho::passwords::MahoPasswordAuthorizationService::
                             kAuthorizationLifetime;
      lease.username_hint = UsernameHintFromCredential(dict);
      lease.password = *password;
      if (const std::string* totp = dict.FindString("totp")) {
        lease.totp = *totp;
      }

      maho::MahoMcpBrowserDelegate::VaultCredentialSummary summary;
      summary.handle = handle;
      summary.username_hint = lease.username_hint;
      summary.origin = lease.origin;
      vault_credential_handles_.emplace(handle, std::move(lease));
      summaries.push_back(std::move(summary));
    }
    TrimVaultCredentialMaps();
    ScheduleVaultCredentialExpiry();
    return summaries;
  }

  std::optional<std::string> VaultRequestCredentialUse(
      int tab_id,
      const std::string& handle,
      const std::string& origin) override {
    const base::TimeTicks now = base::TimeTicks::Now();
    SweepExpiredVaultCredentialLeases(now);
    content::WebContents* wc =
        (tab_id == 0) ? GetActiveWebContents() : GetWebContentsForTabId(tab_id);
    Profile* profile =
        wc ? Profile::FromBrowserContext(wc->GetBrowserContext()) : nullptr;
    if (OriginForWebContents(wc) != origin) {
      return std::nullopt;
    }
    auto it = vault_credential_handles_.find(handle);
    if (it == vault_credential_handles_.end() ||
        !IsVaultCredentialLeaseProfileAllowed(it->second, profile) ||
        it->second.origin != origin || it->second.expires_at <= now) {
      return std::nullopt;
    }
    BrowserVaultCredentialLease lease = std::move(it->second);
    vault_credential_handles_.erase(it);

    const std::string grant_handle =
        "vg_" + base::Uuid::GenerateRandomV4().AsLowercaseString();
    vault_credential_grants_.emplace(grant_handle, std::move(lease));
    TrimVaultCredentialMaps();
    ScheduleVaultCredentialExpiry();
    return grant_handle;
  }

  bool VaultFillCredential(int tab_id,
                           const std::string& grant_handle,
                           ui::AXNodeID ax_id) override {
    return FillVaultGrant(tab_id, grant_handle, ax_id, false);
  }

  bool VaultFillTotp(int tab_id,
                     const std::string& grant_handle,
                     ui::AXNodeID ax_id) override {
    return FillVaultGrant(tab_id, grant_handle, ax_id, true);
  }

  void StartNetworkCapture(const std::string& capture_id,
                           int tab_id,
                           const maho::ResolvedMahoMcpTarget& target,
                           StartNetworkCaptureCallback callback) override {
    callback = base::BindPostTask(content::GetIOThreadTaskRunner({}),
                                  std::move(callback));
    if (!content::BrowserThread::CurrentlyOn(content::BrowserThread::UI)) {
      content::GetUIThreadTaskRunner({})->PostTask(
          FROM_HERE,
          base::BindOnce(&MahoMcpBrowserDelegateImpl::StartNetworkCaptureOnUi,
                         ui_weak_ptr_, capture_id, tab_id, target,
                         std::move(callback)));
      return;
    }
    StartNetworkCaptureOnUi(capture_id, tab_id, target, std::move(callback));
  }

  void StopNetworkCapture(const std::string& capture_id,
                          const maho::ResolvedMahoMcpTarget& target,
                          StopNetworkCaptureCallback callback) override {
    callback = base::BindPostTask(content::GetIOThreadTaskRunner({}),
                                  std::move(callback));
    if (!content::BrowserThread::CurrentlyOn(content::BrowserThread::UI)) {
      content::GetUIThreadTaskRunner({})->PostTask(
          FROM_HERE,
          base::BindOnce(&MahoMcpBrowserDelegateImpl::StopNetworkCaptureOnUi,
                         ui_weak_ptr_, capture_id, target,
                         std::move(callback)));
      return;
    }
    StopNetworkCaptureOnUi(capture_id, target, std::move(callback));
  }

  void CancelNetworkCapture(const std::string& capture_id) override {
    if (!content::BrowserThread::CurrentlyOn(content::BrowserThread::UI)) {
      content::GetUIThreadTaskRunner({})->PostTask(
          FROM_HERE,
          base::BindOnce(&MahoMcpBrowserDelegateImpl::CancelNetworkCaptureOnUi,
                         ui_weak_ptr_, capture_id, true));
      return;
    }
    CancelNetworkCaptureOnUi(capture_id);
  }

 private:
  void StartNetworkCaptureOnUi(const std::string& capture_id,
                               int tab_id,
                               const maho::ResolvedMahoMcpTarget& target,
                               StartNetworkCaptureCallback callback) {
    if (canceled_network_capture_ids_.erase(capture_id)) {
      std::move(callback).Run(StartNetworkCaptureResult{});
      return;
    }
    if (target.valid && !RevalidateTarget(target)) {
      StartNetworkCaptureResult result;
      result.target_valid = false;
      std::move(callback).Run(std::move(result));
      return;
    }
    content::WebContents* wc =
        (tab_id == 0) ? GetActiveWebContents() : GetWebContentsForTabId(tab_id);
    if (!wc || capture_id.empty() ||
        network_observers_.find(capture_id) != network_observers_.end()) {
      std::move(callback).Run(StartNetworkCaptureResult{});
      return;
    }
    // Resolve the concrete tab id we bound to so an omitted request stays
    // fixed to this tab even if the active tab later changes.
    int resolved_tab_id = tab_id;
    if (resolved_tab_id == 0) {
      resolved_tab_id = McpSessionIdOf(wc);
    }
    auto observer = std::make_unique<maho::MahoMcpNetworkObserver>();
    observer->StartCapture(wc);
    network_observers_.emplace(capture_id, std::move(observer));
    StartNetworkCaptureResult result;
    result.success = true;
    result.tab_id = resolved_tab_id;
    std::move(callback).Run(std::move(result));
  }

  void StopNetworkCaptureOnUi(const std::string& capture_id,
                              const maho::ResolvedMahoMcpTarget& target,
                              StopNetworkCaptureCallback callback) {
    if (target.valid && !RevalidateTarget(target)) {
      CancelNetworkCaptureOnUi(capture_id, false);
      StopNetworkCaptureResult result;
      result.target_valid = false;
      std::move(callback).Run(std::move(result));
      return;
    }
    StopNetworkCaptureResult result;
    auto it = network_observers_.find(capture_id);
    if (it == network_observers_.end()) {
      std::move(callback).Run(std::move(result));
      return;
    }
    std::unique_ptr<maho::MahoMcpNetworkObserver> observer =
        std::move(it->second);
    network_observers_.erase(it);
    observer->StopCapture();
    result.har = observer->GetHar();
    std::move(callback).Run(std::move(result));
  }

  void CancelNetworkCaptureOnUi(const std::string& capture_id,
                                bool record_tombstone = true) {
    auto it = network_observers_.find(capture_id);
    if (it == network_observers_.end()) {
      if (record_tombstone) {
        canceled_network_capture_ids_.insert(capture_id);
      }
      return;
    }
    std::unique_ptr<maho::MahoMcpNetworkObserver> observer =
        std::move(it->second);
    network_observers_.erase(it);
    observer->StopCapture();
  }

  bool IsVaultCredentialLeaseProfileAllowed(
      const BrowserVaultCredentialLease& lease,
      Profile* profile) const {
    return maho::IsPasswordManagerAllowedForProfile(profile) &&
           lease.profile_key == maho::GetProfileIdentityKey(
                                    profile->GetPath(), profile->GetPrefs());
  }

  bool AuthorizeAgentCredentialUse(const std::string& profile_key,
                                   const std::string& page_origin) {
    auto* authorization_service =
        maho::passwords::MahoPasswordAuthorizationService::Get();
    if (authorization_service->ConsumeAuthorization(
            profile_key, page_origin,
            maho::passwords::PasswordAuthorizationAction::
                kAgentCredentialUse)) {
      return true;
    }

    const auto request_key = std::make_pair(profile_key, page_origin);
    if (!pending_agent_credential_authorizations_.insert(request_key).second) {
      return false;
    }
    authorization_service->AuthorizeForProfile(
        profile_key, page_origin,
        maho::passwords::PasswordAuthorizationAction::kAgentCredentialUse,
        u"Allow the AI agent to use credentials for this site",
        base::BindOnce(
            &MahoMcpBrowserDelegateImpl::OnAgentCredentialAuthorizationComplete,
            weak_factory_.GetWeakPtr(), request_key));
    return false;
  }

  void OnAgentCredentialAuthorizationComplete(
      std::pair<std::string, std::string> request_key,
      bool) {
    pending_agent_credential_authorizations_.erase(request_key);
  }

  void OnVaultLockStateChanged(bool locked) {
    if (locked) {
      RevokeVaultCredentialLeases();
    }
  }

  void SweepExpiredVaultCredentialLeases(base::TimeTicks now) {
    auto erase_expired = [now](auto& leases) {
      for (auto it = leases.begin(); it != leases.end();) {
        if (it->second.expires_at <= now) {
          it = leases.erase(it);
        } else {
          ++it;
        }
      }
    };
    erase_expired(vault_credential_handles_);
    erase_expired(vault_credential_grants_);
    ScheduleVaultCredentialExpiry();
  }

  void ScheduleVaultCredentialExpiry() {
    std::optional<base::TimeTicks> next_expiry;
    auto include_expiry = [&next_expiry](const auto& leases) {
      for (const auto& entry : leases) {
        const BrowserVaultCredentialLease& lease = entry.second;
        if (!next_expiry || lease.expires_at < *next_expiry) {
          next_expiry = lease.expires_at;
        }
      }
    };
    include_expiry(vault_credential_handles_);
    include_expiry(vault_credential_grants_);
    if (!next_expiry) {
      vault_credential_expiry_timer_.Stop();
      return;
    }
    vault_credential_expiry_timer_.Start(
        FROM_HERE,
        std::max(base::TimeDelta(), *next_expiry - base::TimeTicks::Now()),
        base::BindOnce(
            &MahoMcpBrowserDelegateImpl::OnVaultCredentialExpiryTimer,
            base::Unretained(this)));
  }

  void OnVaultCredentialExpiryTimer() {
    SweepExpiredVaultCredentialLeases(base::TimeTicks::Now());
  }

  void TrimVaultCredentialMaps() {
    if (vault_credential_handles_.size() > kMaxVaultCredentialHandles) {
      vault_credential_handles_.clear();
    }
    if (vault_credential_grants_.size() > kMaxVaultCredentialHandles) {
      vault_credential_grants_.clear();
    }
  }

  bool FillVaultGrant(int tab_id,
                      const std::string& grant_handle,
                      ui::AXNodeID ax_id,
                      bool fill_totp) {
    SweepExpiredVaultCredentialLeases(base::TimeTicks::Now());
    auto it = vault_credential_grants_.find(grant_handle);
    if (it == vault_credential_grants_.end()) {
      return false;
    }
    BrowserVaultCredentialLease lease = std::move(it->second);
    vault_credential_grants_.erase(it);

    content::WebContents* wc =
        (tab_id == 0) ? GetActiveWebContents() : GetWebContentsForTabId(tab_id);
    Profile* profile =
        wc ? Profile::FromBrowserContext(wc->GetBrowserContext()) : nullptr;
    const std::string page_origin = OriginForWebContents(wc);
    if (!maho::IsPasswordManagerAllowedForProfile(profile) ||
        lease.profile_key != maho::GetProfileIdentityKey(profile->GetPath(),
                                                         profile->GetPrefs()) ||
        lease.origin != page_origin ||
        lease.expires_at <= base::TimeTicks::Now()) {
      return false;
    }

    const std::string& leased_value = fill_totp ? lease.totp : lease.password;
    if (leased_value.empty()) {
      return false;
    }

    // Final fail-closed check against the exact page context immediately
    // before exposing the one-shot plaintext lease to the fill path.
    if (!maho::passwords::MahoPasswordAuthorizationService::Get()
             ->ConsumeAuthorization(
                 lease.profile_key, page_origin,
                 maho::passwords::PasswordAuthorizationAction::
                     kAgentCredentialUse)) {
      return false;
    }
    return FillVaultValueIntoAxNode(wc, ax_id, leased_value);
  }

  bool FillVaultValueIntoAxNode(content::WebContents* wc,
                                ui::AXNodeID ax_id,
                                const std::string& value) {
    DCHECK(wc);
    ui::AXTreeUpdate update = SnapshotAXTree(wc);
    content::RenderWidgetHostView* view = wc->GetRenderWidgetHostView();
    const float device_scale_factor = view ? view->GetDeviceScaleFactor() : 1.f;
    std::optional<gfx::PointF> center =
        ComputeViewportCenterCss(update, ax_id, device_scale_factor);
    bool js_filled = false;
    if (center) {
      const std::string x_str = base::NumberToString(std::lround(center->x()));
      const std::string y_str = base::NumberToString(std::lround(center->y()));
      const std::string value_json = base::GetQuotedJSONString(value);
      const std::string script =
          "(()=>{const el=document.elementFromPoint(" + x_str + "," + y_str +
          ");"
          "if(el){"
          "el.focus();"
          "if('value' in el){"
          "el.value=" +
          value_json +
          ";"
          "el.dispatchEvent(new Event('input',{bubbles:true}));"
          "el.dispatchEvent(new Event('change',{bubbles:true}));"
          "}"
          "return 'ok';"
          "}"
          "return 'nopoint';})()";
      js_filled = ExecuteMcpActionJsBlocking(wc, script) == "ok";
      maho::MahoMcpInputSynthesizer::HoverAt(wc, std::lround(center->x()),
                                             std::lround(center->y()));
    }
    if (!js_filled) {
      PerformAxAction(wc, ax_id, ax::mojom::Action::kFocus);
      PerformAxAction(wc, ax_id, ax::mojom::Action::kSetValue, value);
      js_filled = true;
    }
    return js_filled;
  }

  std::unordered_map<std::string, std::unique_ptr<maho::MahoMcpNetworkObserver>>
      network_observers_;
  std::set<std::string> canceled_network_capture_ids_;
  static constexpr size_t kMaxVaultCredentialHandles = 1024;
  std::unordered_map<std::string, BrowserVaultCredentialLease>
      vault_credential_handles_;
  std::unordered_map<std::string, BrowserVaultCredentialLease>
      vault_credential_grants_;
  base::CallbackListSubscription vault_lock_state_subscription_;
  base::OneShotTimer vault_credential_expiry_timer_;
  std::set<std::pair<std::string, std::string>>
      pending_agent_credential_authorizations_;
  static constexpr size_t kMaxAxSnapshotNodes = 12000;
  static constexpr size_t kMaxCachedRefSnapshots = 8;
  static constexpr size_t kMaxRefSnapshotGuards = 64;
  std::map<int, CachedRefSnapshot> ref_snapshot_cache_;
  uint64_t next_ref_snapshot_token_ = 0;
  std::map<uint64_t, RefSnapshotGuard> ref_snapshot_guards_;
  static constexpr size_t kMaxResolvedTargets = 1024;
  int64_t generation_counter_ = 0;
  // Owned per-generation identity tokens minted by ResolveTab/ProfileTarget and
  // checked by RevalidateTarget. Keyed by generation so a target is only valid
  // while its exact token still authorizes kMCP.
  std::map<int64_t, std::unique_ptr<MahoPrivateContextToken>>
      resolved_target_tokens_;
  base::WeakPtr<MahoMcpBrowserDelegateImpl> ui_weak_ptr_;
  base::WeakPtrFactory<MahoMcpBrowserDelegateImpl> weak_factory_{this};
};

MahoMcpBrowserDelegateImpl* GetMcpBrowserDelegate() {
  static base::NoDestructor<MahoMcpBrowserDelegateImpl> mcp_delegate;
  return mcp_delegate.get();
}

}  // namespace

void MahoBrowserMainExtraParts::AddMcpVaultCredentialLeaseForTesting(
    base::TimeTicks expires_at,
    bool promoted,
    Profile* lease_profile) {
  GetMcpBrowserDelegate()->AddVaultCredentialLeaseForTesting(
      expires_at, promoted, lease_profile);
}

void MahoBrowserMainExtraParts::SweepMcpVaultCredentialLeasesForTesting(
    base::TimeTicks now) {
  GetMcpBrowserDelegate()->SweepVaultCredentialLeasesForTesting(now);
}

bool MahoBrowserMainExtraParts::PromoteMcpVaultCredentialLeaseForTesting(
    Profile* resolved_profile,
    base::TimeTicks now) {
  return GetMcpBrowserDelegate()->PromoteVaultCredentialLeaseForTesting(
      resolved_profile, now);
}

size_t MahoBrowserMainExtraParts::McpVaultCredentialLeaseCountForTesting()
    const {
  return GetMcpBrowserDelegate()->VaultCredentialLeaseCountForTesting();
}

void MahoBrowserMainExtraParts::PostProfileInit(Profile* profile,
                                                bool is_initial_profile) {
  TRACE_EVENT0("browser", "MahoBrowserMainExtraParts::PostProfileInit");
  const bool is_regular = profile ? profile->IsRegularProfile() : false;
  const bool incognito_switch =
      base::CommandLine::ForCurrentProcess()->HasSwitch(switches::kIncognito);

  // Must be startup-time (not browser-triggered): the login gate suppresses
  // the startup browser, so browser-triggered init would deadlock.
  if (incognito_switch) {
    return;
  }
  if (profile && is_regular) {
    EnsureMailNotificationCoordinator(profile);
    maho::MahoMcpSession::SetBrowserDelegate(GetMcpBrowserDelegate());
    if (auto* artifact_registry =
            maho::ai::MahoArtifactRegistry::GetForProfile(profile)) {
      artifact_registry->PrepareArtifactRoot();
    }
  }
  if (is_initial_profile && profile && is_regular && !core_ &&
      !regular_services_initializing_) {
    PrefService* prefs = profile->GetPrefs();
    const bool login_gate_bypassed =
        base::CommandLine::ForCurrentProcess()->HasSwitch(
            "maho-disable-login-gate");
    const bool has_valid_relay_session =
        maho::auth::HasValidRelaySession(prefs);
    const bool gate_value = maho::welcome::IsLoginGateActive(
        login_gate_bypassed, has_valid_relay_session);

    prefs->SetBoolean(maho::welcome::kLoginGateActive, gate_value);

    InitializeRegularServicesOnce(profile);
  }
}

void MahoBrowserMainExtraParts::PostMainMessageLoopRun() {
#if BUILDFLAG(IS_WIN)
  if (mcp_pipe_server_) {
    content::GetIOThreadTaskRunner({})->PostTask(
        FROM_HERE,
        base::BindOnce(
            [](std::unique_ptr<maho::MahoMcpPipeServer> server,
               std::unique_ptr<maho::MahoMcpSessionToken> token) {
              server.reset();
            },
            std::move(mcp_pipe_server_), std::move(mcp_session_token_)));
  } else {
    mcp_session_token_.reset();
  }
#else
  if (mcp_socket_server_) {
    content::GetIOThreadTaskRunner({})->DeleteSoon(
        FROM_HERE, std::move(mcp_socket_server_));
  }
#endif

  ProfileManager* manager = g_browser_process->profile_manager();
  if (manager) {
    for (Profile* profile : manager->GetLoadedProfiles()) {
      if (profile && profile->IsRegularProfile()) {
        // ExitTypeService::SetCurrentSessionExitType stashes (does not write
        // to prefs) when waiting_for_user_to_ack_crash_ is true — so a
        // perpetually-Crashed profile never auto-recovers. Write the prefs
        // value directly to break the loop, then mirror via the service so
        // any in-process state stays consistent. CommitPendingWrite forces
        // an immediate disk flush before the message loop tears down.
        profile->GetPrefs()->SetString(prefs::kSessionExitType, "Normal");
        profile->GetPrefs()->CommitPendingWrite();
        if (auto* svc = ExitTypeService::GetInstanceForProfile(profile)) {
          svc->SetCurrentSessionExitType(ExitType::kClean);
        }
      }
    }
  }

  MahoSpaceThemeState::Clear();
  ShutdownCoreAndServices();
  if (core_) {
    LOG(INFO) << "Maho core destroyed";
  }
}

void MahoBrowserMainExtraParts::OnSuspend() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  LockVaultForLifecycle();
}

void MahoBrowserMainExtraParts::StartObservingPowerSuspend() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (observing_power_suspend_) {
    return;
  }
  auto* power_monitor = base::PowerMonitor::GetInstance();
  if (!power_monitor->IsInitialized()) {
    return;
  }
  observing_power_suspend_ = true;
  if (power_monitor->AddPowerSuspendObserverAndReturnSuspendedState(this)) {
    LockVaultForLifecycle();
  }
}

void MahoBrowserMainExtraParts::StopObservingPowerSuspend() {
  if (!observing_power_suspend_) {
    return;
  }
  base::PowerMonitor::GetInstance()->RemovePowerSuspendObserver(this);
  observing_power_suspend_ = false;
}

void MahoBrowserMainExtraParts::LockVaultForLifecycle() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  maho::core::LockVault(core_);
  maho::passwords::MahoPasswordAuthorizationService::Get()->RevokeAll();
  GetMcpBrowserDelegate()->RevokeVaultCredentialLeases();
  maho::NotifyVaultLockStateChanged(/*locked=*/true);
}

void MahoBrowserMainExtraParts::ShutdownCoreAndServices() {
  mail_notification_coordinators_->Clear();
  if (!core_) {
    return;
  }
  // Idempotent teardown, in strict order so no late updater callback can touch
  // a destroyed core: quiesce blocker work; clear the process-wide designated
  // updater; stop each loaded regular-profile updater service (only if the
  // browser process/profile manager still exists, and never constructing a
  // service); stop routines; save; SetCore(nullptr); destroy; null the core.
  StopObservingPowerSuspend();
  maho::core::LockVault(core_);
  maho::passwords::MahoPasswordAuthorizationService::Get()->RevokeAll();
  GetMcpBrowserDelegate()->RevokeVaultCredentialLeases();
  maho::QuiesceBlockerWork();
  maho::MahoContentBlockerUpdateService::SetDesignatedUpdater(nullptr);
  if (g_browser_process) {
    if (ProfileManager* pm = g_browser_process->profile_manager()) {
      for (Profile* p : pm->GetLoadedProfiles()) {
        if (p && p->IsRegularProfile()) {
          if (auto* svc = maho::MahoContentBlockerUpdateServiceFactory::
                  GetForProfileIfExists(p)) {
            svc->Shutdown();
          }
        }
      }
    }
  }
  routines_scheduler_.reset();
  // The Vault was locked above; tell every lock-state subscriber before the
  // core goes away. This also restores the fail-closed broadcast baseline so a
  // later core cannot inherit a stale "unlocked" state.
  maho::NotifyVaultLockStateChanged(/*locked=*/true);
  maho::core::SaveState(core_);
  maho::QuiesceCoreTasksAndWait();
  maho::SetCore(nullptr);
  maho::core::Destroy(core_);
  core_ = nullptr;
}

void MahoBrowserMainExtraParts::MaybeLaunchMailHelper(Profile* profile) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!profile || !profile->IsRegularProfile()) {
    return;
  }

  base::FilePath crashpad_database;
  base::FilePath user_data_dir;
  if (base::PathService::Get(chrome::DIR_USER_DATA, &user_data_dir)) {
    crashpad_database = user_data_dir.AppendASCII("Crashpad");
  }

  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&CreateDirectoryOnThreadPool, crashpad_database),
      base::BindOnce(&MahoBrowserMainExtraParts::OnMailHelperCrashpadReady,
                     weak_factory_.GetWeakPtr(), profile, crashpad_database));
}

void MahoBrowserMainExtraParts::OnMailHelperCrashpadReady(
    Profile* profile,
    const base::FilePath& crashpad_database,
    bool crashpad_directory_ready) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!crashpad_directory_ready && !crashpad_database.empty()) {
    LOG(WARNING) << "Maho mail helper Crashpad directory creation failed: "
                 << crashpad_database.value();
  }

  if (!profile || !profile->IsRegularProfile()) {
    return;
  }

  maho::MahoMailService* service =
      maho::MahoMailServiceFactory::GetForProfile(profile);
  if (!service) {
    return;
  }

  service->EnsureHelperLaunched(maho::mail_helper::kMahoMailHelperVersion,
                                profile->GetPath().AsUTF8Unsafe(),
                                crashpad_database);
}

void MahoBrowserMainExtraParts::InitializeRegularServicesOnce(
    Profile* profile) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (core_ || regular_services_initializing_) {
    return;
  }
  regular_services_initializing_ = true;
  regular_profile_ = profile;
  EnsureMailNotificationCoordinator(profile);

  // Core initialization remains one-time; notification ownership is per
  // regular profile and is established independently above.

#if BUILDFLAG(IS_MAC)
  // Ensure the Mach-port rendezvous server exists before launching the mail
  // helper subprocess; the helper's mojo endpoint recovery does a bootstrap
  // lookup for this server keyed by the browser's bundle id + pid. Creating
  // this process-lifetime singleton here (well before both the startup
  // MaybeLaunchMailHelper() call and any later welcome-login-triggered launch)
  // registers the bootstrap service so MachPortRendezvousClient::GetInstance()
  // in the child resolves instead of returning null (invalid endpoint).
  base::MachPortRendezvousServerMac::GetInstance();
#endif

  LOG(INFO) << "MAHO_INIT_AUDIT: class=regular, decision=allow, "
               "stage=core_init_begin";

  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&CreateDirectoryOnThreadPool, storage_path_),
      base::BindOnce(&MahoBrowserMainExtraParts::OnRegularStorageDirectoryReady,
                     weak_factory_.GetWeakPtr(), profile));
}

void MahoBrowserMainExtraParts::EnsureMailNotificationCoordinator(
    Profile* profile) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!profile || !profile->IsRegularProfile() ||
      mail_notification_coordinators_->Contains(profile)) {
    return;
  }
  if (!EnsureMahoMailNotificationHandler(profile)) {
    return;
  }
  mail_notification_coordinators_->Ensure(
      profile,
      base::BindRepeating(
          [](Profile* profile)
              -> std::unique_ptr<maho::MahoMailNotificationCoordinator> {
            maho::MahoMailService* mail_service =
                maho::MahoMailServiceFactory::GetForProfile(profile);
            if (!mail_service) {
              return nullptr;
            }
            return std::make_unique<maho::MahoMailNotificationCoordinator>(
                profile, mail_service,
                base::BindRepeating(&DisplayMahoMailNotification, profile),
                base::BindRepeating(&WithdrawMahoMailNotification, profile),
                base::BindRepeating(&RouteMahoMailNotification, profile),
                base::BindRepeating(
                    [](maho::MahoMailService* service) {
                      return service->behavior_snapshot();
                    },
                    mail_service));
          }));
}

void MahoBrowserMainExtraParts::OnRegularStorageDirectoryReady(
    Profile* profile,
    bool storage_directory_ready) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  if (!regular_services_initializing_) {
    return;
  }
  if (!profile || !profile->IsRegularProfile()) {
    regular_services_initializing_ = false;
    return;
  }
  if (!storage_directory_ready) {
    LOG(WARNING) << "Maho storage directory creation failed: "
                 << storage_path_.value();
  }

  base::FilePath key_path = storage_path_.AppendASCII("maho_storage.key");
  base::FilePath database_path = storage_path_.AppendASCII("maho.db");
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(
          [](base::FilePath key_path, base::FilePath database_path) {
            TRACE_EVENT0("browser", "Maho::DeriveAndSetStorageKey");
            const auto storage_key_setup =
                maho::core::DeriveAndSetStorageKey(
                    key_path.AsUTF8Unsafe(), database_path.AsUTF8Unsafe());
            LOG(INFO) << "Maho Vault preflight state: "
                      << maho::core::VaultPreflightStateName(
                             storage_key_setup.vault_preflight_state);
            return storage_key_setup.storage_key_ready;
          },
          std::move(key_path), std::move(database_path)),
      base::BindOnce(
          &MahoBrowserMainExtraParts::OnStorageKeySetupComplete,
          weak_factory_.GetWeakPtr(), profile));
}

void MahoBrowserMainExtraParts::OnStorageKeySetupComplete(
    Profile* profile,
    bool storage_key_ready) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  if (!regular_services_initializing_) {
    return;
  }

  if (storage_key_ready) {
    TRACE_EVENT0("browser", "Maho::CreateWithStorage");
    core_ = maho::core::CreateWithStorage(storage_path_.AsUTF8Unsafe());
  } else {
    LOG(ERROR) << "Maho storage key derivation/setting failed; password "
                  "manager/persistent storage will be disabled.";
  }

  if (core_) {
    maho::SetCoreForProfile(core_, profile);
    maho::core::InstallLogCallback();
    {
      TRACE_EVENT0("browser", "Maho::LoadState");
      maho::core::LoadState(core_);
    }
    LOG(INFO) << "Maho core readiness after LoadState: "
              << static_cast<int>(maho_core_readiness_status(core_));
    StartObservingPowerSuspend();
    LOG(INFO) << "Maho core initialized, version: " << maho::core::Version();
    PopulateBridgeFromCore(core_);
    maho::MahoTabRegistry::Get();

    // Initialize the MahoAtcState with the initial set of rules from the core.
    maho::PostCoreTask<bool>(
        FROM_HERE, base::BindOnce([]() {
          MahoCore* core = maho::GetCore();
          if (!core) {
            return false;
          }
          char* json_str = maho_core_get_atc_rules(core);
          if (!json_str) {
            return false;
          }
          std::string json(json_str);
          maho_string_free(json_str);
          return maho::HasEnabledTrafficRuleInJson(json);
        }),
        base::BindOnce([](bool has_enabled_rules) {
          maho::MahoAtcState::SetHasEnabledRules(has_enabled_rules);
        }));

    routines_scheduler_ = std::make_unique<maho::MahoRoutinesScheduler>(core_);
    routines_scheduler_->Start();

    // This core-owning initial regular profile owns the sole process-wide
    // content-blocker updater: construct it, designate it, and start it
    // according to the persisted mode (0 == native; anything else is inert).
    if (auto* update_service =
            maho::MahoContentBlockerUpdateServiceFactory::GetForProfile(
                profile)) {
      maho::MahoContentBlockerUpdateService::SetDesignatedUpdater(
          update_service);
      update_service->OnModeChanged(maho::core::GetContentBlockingMode(core_) ==
                                    0);
    }

    // Max-tier gated + fire-and-forget inside the FFI (no-op otherwise).
    maho_routines_fire_event(core_, "{\"kind\":\"on_startup\"}");
  } else {
    LOG(ERROR) << "Maho core initialization failed";
  }

  LOG(INFO) << "MAHO_INIT_AUDIT: class=regular, decision=allow, "
               "stage=core_init_complete";

  LOG(INFO) << "MAHO_INIT_AUDIT: class=regular, decision=allow, "
               "stage=profile_init_begin";

  PrefService* prefs = profile->GetPrefs();
  // Tests that rely on the standard InProcessBrowserTest startup browser pass
  // --maho-disable-login-gate so the onboarding gate does not suppress the
  // initial browser window (which would leave InProcessBrowserTest::browser()
  // null). Default behavior is unchanged when the switch is absent.
  const bool login_gate_bypassed =
      base::CommandLine::ForCurrentProcess()->HasSwitch(
          "maho-disable-login-gate");
  const bool has_valid_relay_session = maho::auth::HasValidRelaySession(prefs);
  bool needs_login_gate = maho::welcome::IsLoginGateActive(
      login_gate_bypassed, has_valid_relay_session);

  // Reconcile the onboarding flag against the Vault's real on-disk state.
  // `kWelcomeCompleted` lives in profile Preferences while the Vault's key
  // material lives in the profile database, so the two can diverge (a reset,
  // replaced, or recreated database under an already-onboarded profile).
  // Onboarding is the only path that initializes the Vault, so a diverged
  // profile would otherwise keep an uninitialized Vault for its whole life and
  // refuse every native password write. Only an affirmatively reported
  // uninitialized Vault re-opens setup; an unreadable status never does.
  if (maho::welcome::RequiresVaultSetupRepair(
          prefs->GetBoolean(maho::welcome::kWelcomeCompleted),
          IsVaultUninitialized(core_))) {
    LOG(WARNING) << "Maho Vault holds no key material for an onboarded "
                    "profile; re-opening Vault setup so password saves are "
                    "not silently discarded.";
    prefs->SetBoolean(maho::welcome::kWelcomeCompleted, false);
    needs_login_gate = true;
  }

  prefs->SetBoolean(maho::welcome::kLoginGateActive, needs_login_gate);

  if (needs_login_gate) {
    maho::MahoWelcomeWindow::Show(profile);
  }

  maho::MahoMcpSession::SetBrowserDelegate(GetMcpBrowserDelegate());

  sync_relay_client_ = std::make_unique<maho::MahoSyncRelayClient>(profile);
  sync_relay_client_->StartPolling();

  maho::MigrateAiSettingsFromMacOSDefaults(profile);

  maho::MahoTabIdSessionHelper::GetForProfile(profile);

  maho::MahoUpdateManager::GetInstance()->Initialize(
      profile, g_browser_process ? g_browser_process->local_state() : nullptr);
  maho::updates::MahoConfigManager::GetInstance()->Initialize(profile);
  maho::MahoChangelogAutoOpener::RegisterForProfile(profile);

  MaybeLaunchMailHelper(profile);

  maho::MahoDownloadBridgeServiceFactory::GetForProfile(profile);

  // If user hasn't explicitly set the value, override the default to LAST
  // so SessionService restore kicks in.
  const PrefService::Preference* p =
      prefs->FindPreference(prefs::kRestoreOnStartup);
  if (p && p->IsDefaultValue()) {
    SessionStartupPref::SetStartupPref(
        profile, SessionStartupPref(SessionStartupPref::LAST));
  }

  // MCP server
#if BUILDFLAG(IS_WIN)
  mcp_session_token_ = maho::MahoMcpSessionToken::Create();
  if (mcp_session_token_) {
    base::FilePath token_path =
        maho::MahoMcpSessionToken::GetSessionTokenPath();
    if (!token_path.empty()) {
      mcp_session_token_->WriteEncryptedToDisk(token_path);
    }
  }
  mcp_pipe_server_ =
      std::make_unique<maho::MahoMcpPipeServer>(mcp_session_token_.get());
  content::GetIOThreadTaskRunner({})->PostTask(
      FROM_HERE,
      base::BindOnce(base::IgnoreResult(&maho::MahoMcpPipeServer::Start),
                     base::Unretained(mcp_pipe_server_.get())));
#else
  mcp_socket_server_ =
      std::make_unique<maho::MahoMcpSocketServer>(storage_path_);
  content::GetIOThreadTaskRunner({})->PostTask(
      FROM_HERE,
      base::BindOnce(base::IgnoreResult(&maho::MahoMcpSocketServer::Start),
                     base::Unretained(mcp_socket_server_.get())));
#endif

  maho::RegisterGlobalShortcut(profile);

  LOG(INFO) << "MAHO_INIT_AUDIT: class=regular, decision=allow, "
               "stage=profile_init_complete";
  regular_services_initializing_ = false;
}
