#include "maho/browser/maho_browser_main_extra_parts.h"
#include "maho/browser/maho_private_context_policy.h"

#include <string>
#include <map>
#include <unordered_map>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/json/string_escape.h"
#include "base/logging.h"
#include "base/memory/weak_ptr.h"
#include "base/no_destructor.h"
#include "base/path_service.h"
#include "base/values.h"
#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/task/sequenced_task_runner.h"
#include "build/build_config.h"

#if BUILDFLAG(IS_WIN)
#include "maho/browser/mcp/maho_mcp_pipe_server_win.h"
#include "maho/browser/mcp/maho_mcp_session_token_win.h"
#else
#include "maho/browser/mcp/maho_mcp_socket_server.h"
#endif
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/sessions/exit_type_service.h"
#include "chrome/browser/prefs/session_startup_pref.h"
#include "chrome/common/pref_names.h"
#include "components/prefs/pref_service.h"
#include "chrome/common/chrome_paths.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"
#include "maho/browser/extensions/maho_extension_state_bridge.h"
#include "maho/browser/ai/maho_ai_settings_migration.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/maho_routines_scheduler.h"
#include "maho/browser/maho_space_profile_bridge.h"
#include "maho/browser/mail_helper/maho_mail_helper_version.h"
#include "maho/browser/mail_helper/maho_mail_service.h"
#include "maho/browser/mail_helper/maho_mail_service_factory.h"
#include "maho/browser/ui/views/side_panel/maho_extension_side_panel_coordinator.h"  // nogncheck

#include "maho/browser/maho_tab_registry.h"
#include "maho/browser/sync/maho_sync_relay_client.h"
#include "maho/browser/updates/maho_update_manager.h"
#include "maho/browser/updates/maho_config_manager.h"
#include "chrome/browser/sessions/session_restore.h"
#include "base/strings/utf_string_conversions.h"
#include "maho/browser/maho_tab_id_helper.h"
#include "maho/browser/maho_tab_id_session_helper.h"
#include "maho/browser/ui/views/welcome/maho_welcome_window.h"
#include "maho/browser/ui/webui/maho_auth_utils.h"
#include "maho/browser/ui/webui/maho_welcome/maho_welcome_prefs.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/sessions/content/session_tab_helper.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/isolated_world_ids.h"
#include "base/base64.h"
#include "base/containers/span.h"
#include "base/run_loop.h"
#include "base/synchronization/waitable_event.h"
#include "components/viz/common/frame_sinks/copy_output_result.h"
#include "ui/accessibility/ax_tree.h"
#include "ui/accessibility/ax_tree_update.h"
#include "maho/browser/mcp/maho_mcp_accessibility_handler.h"
#include "maho/browser/mcp/maho_mcp_console_capture.h"
#include "maho/browser/mcp/maho_mcp_firewall.h"
#include "maho/browser/mcp/maho_mcp_input_synthesizer.h"
#include "maho/browser/mcp/maho_mcp_navigation_tracker.h"
#include "maho/browser/mcp/maho_mcp_screenshot_handler.h"
#include "maho/browser/mcp/maho_mcp_session.h"
#include "maho/browser/mcp/maho_mcp_network_observer.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/accessibility/ax_action_data.h"
#include "ui/accessibility/ax_enums.mojom-shared.h"
#include "ui/accessibility/ax_mode.h"
#include "ui/accessibility/ax_node.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/accessibility/ax_tree.h"
#include "maho/browser/ui/theme/maho_color_mixer.h"
#include "maho/browser/ui/theme/maho_space_theme_state.h"
#include "maho/third_party/maho/maho_bridge.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "ui/color/color_provider_manager.h"
#include "chrome/browser/bookmarks/bookmark_model_factory.h"
#include "components/bookmarks/browser/bookmark_model.h"
#include "components/bookmarks/browser/bookmark_node.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/sessions/session_tab_helper_factory.h"

namespace {
MahoBrowserMainExtraParts* g_instance = nullptr;
}  // namespace

MahoBrowserMainExtraParts::MahoBrowserMainExtraParts() {
  DCHECK(!g_instance);
  g_instance = this;
}

MahoBrowserMainExtraParts::~MahoBrowserMainExtraParts() {
  g_instance = nullptr;
  delete extension_side_panel_coordinator_;
  if (core_) {
    routines_scheduler_.reset();
    maho::core::SaveState(core_);
    maho::SetCore(nullptr);
    maho::core::Destroy(core_);
    core_ = nullptr;
  }
}

// static
MahoBrowserMainExtraParts* MahoBrowserMainExtraParts::GetInstance() {
  return g_instance;
}

namespace {

constexpr int kIsolatedWorldIdForMcpAction = content::ISOLATED_WORLD_ID_CONTENT_END + 6;

std::string ExecuteMcpActionJsBlocking(content::WebContents* wc, const std::string& script) {
  if (!wc || !wc->GetPrimaryMainFrame()) {
    return std::string();
  }
  std::string result;
  base::RunLoop run_loop(base::RunLoop::Type::kNestableTasksAllowed);
  wc->GetPrimaryMainFrame()->ExecuteJavaScriptInIsolatedWorld(
      base::UTF8ToUTF16(script),
      base::BindOnce(
          [](std::string* out, base::OnceClosure quit, base::Value val) {
            if (val.is_string()) {
              *out = val.GetString();
            }
            std::move(quit).Run();
          },
          &result, run_loop.QuitClosure()),
      kIsolatedWorldIdForMcpAction);
  run_loop.Run();
  return result;
}

std::optional<gfx::PointF> ComputeViewportCenterCss(const ui::AXTreeUpdate& update,
                                                     ui::AXNodeID ax_id,
                                                     float device_scale_factor) {
  const ui::AXNodeData* target_node = nullptr;
  for (const auto& node : update.nodes) {
    if (node.id == ax_id) {
      target_node = &node;
      break;
    }
  }
  if (!target_node) return std::nullopt;

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
  if (device_scale_factor > 0.f) {
    center.Scale(1.f / device_scale_factor);
  }
  return center;
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

  char* json_str = maho_core_get_space_view_models(core);
  if (!json_str) {
    return;
  }
  std::string json(json_str);
  maho_string_free(json_str);

  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) {
    return;
  }

  for (const auto& item : parsed->GetList()) {
    const auto* dict = item.GetIfDict();
    if (!dict) {
      continue;
    }
    const std::string* id = dict->FindString("id");
    if (!id) {
      continue;
    }
    const std::string* profile_id = dict->FindString("profile_id");
    std::string pid_str = profile_id ? *profile_id : std::string();
    std::optional<base::FilePath> resolved_path =
        maho::MahoSpaceProfileBridge::ProfileBasenameForId(pid_str);
    if (!resolved_path) {
      LOG(WARNING) << "Maho: Skipping hydration for space id: " << *id
                   << " due to invalid profile_id: " << pid_str;
      continue;
    }

    bridge->RegisterSpace(*id, *resolved_path);
  }

  bridge->SetActiveSpaceId(
      DecodeActiveSpaceId(maho_core_get_active_space_id(core)));

  auto apply_initial_theme = []() {
    MahoSpaceThemeState::UpdateFromCore();
    // Reset the color cache so the first window's ColorProvider picks up the
    // restored Space theme; otherwise the initial paint uses the un-themed base
    // colors until an unrelated theme event resets it.
    ui::ColorProviderManager::Get().ResetColorProviderCache();
  };
  if (content::BrowserThread::CurrentlyOn(content::BrowserThread::UI)) {
    apply_initial_theme();
  } else {
    content::GetUIThreadTaskRunner({})->PostTask(
        FROM_HERE, base::BindOnce(apply_initial_theme));
  }

  LOG(INFO) << "Maho bridge populated: "
            << bridge->GetRegisteredSpaceCount() << " spaces";
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
  if (!wc || !wc->GetPrimaryMainFrame()) {
    return false;
  }
  ui::AXActionData action_data;
  action_data.action = action;
  action_data.target_node_id = ax_id;
  action_data.value = value;
  wc->GetPrimaryMainFrame()->AccessibilityPerformAction(action_data);
  return true;
}

void DispatchActiveElementValueEvents(content::WebContents* wc,
                                      const std::string& value) {
  if (!wc || !wc->GetPrimaryMainFrame()) {
    return;
  }
  const std::string value_json = base::GetQuotedJSONString(value);
  const std::string script =
      "(()=>{const e=document.activeElement;if(!e)return false;"
      "if('value' in e){e.value=" +
      value_json +
      ";e.dispatchEvent(new Event('input',{bubbles:true}));"
      "e.dispatchEvent(new Event('change',{bubbles:true}));return true;}"
      "return false;})()";
  wc->GetPrimaryMainFrame()->ExecuteJavaScriptInIsolatedWorld(
      base::UTF8ToUTF16(script), base::NullCallback(),
      kIsolatedWorldIdForMcpAction);
}

}  // namespace

void MahoBrowserMainExtraParts::PostEarlyInitialization() {
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
    case MahoPrivateContextClass::kNull: return "null";
    case MahoPrivateContextClass::kGuest: return "guest";
    case MahoPrivateContextClass::kSystem: return "system";
    case MahoPrivateContextClass::kDevToolsOtr: return "devtools_otr";
    case MahoPrivateContextClass::kPrimaryIncognito: return "primary_incognito";
    case MahoPrivateContextClass::kOtherOtr: return "other_otr";
    case MahoPrivateContextClass::kRegular: return "regular";
  }
}
}  // namespace

void MahoBrowserMainExtraParts::MaybeInitializeForBrowser(BrowserWindowInterface* browser) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  Profile* profile = browser ? browser->GetProfile() : nullptr;
  MahoPrivateContextClass cls = profile ? ClassifyProfileForTesting(profile) : MahoPrivateContextClass::kNull;

  if (cls != MahoPrivateContextClass::kRegular) {
    LOG(INFO) << "MAHO_INIT_AUDIT: class=" << ClassToString(cls)
              << ", decision=deny, stage=MaybeInitializeForBrowser";
    return;
  }

  if (core_) {
    LOG(INFO) << "MAHO_INIT_AUDIT: class=" << ClassToString(cls)
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
             std::string j) { std::move(cb).Run(true, std::move(j)); },
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
             std::string e) { std::move(cb).Run(false, std::move(e)); },
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

int McpSessionIdOf(content::WebContents* web_contents) {
  if (!web_contents) {
    return 0;
  }
  sessions::SessionTabHelper* helper =
      sessions::SessionTabHelper::FromWebContents(web_contents);
  return helper ? helper->session_id().id() : 0;
}

class MahoMcpBrowserDelegateImpl : public maho::MahoMcpBrowserDelegate {
 public:
  MahoMcpBrowserDelegateImpl() {
    ui_weak_ptr_ = weak_factory_.GetWeakPtr();
  }
  ~MahoMcpBrowserDelegateImpl() override = default;

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
      std::move(callback).Run(false, "Maho core unavailable");
      return;
    }
    int32_t tier = maho_account_get_tier(core);
    if (tier < 0) {
      tier = 0;
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

  std::vector<maho::MahoMcpSession::TabInfo> GetTabList() override {
    std::vector<maho::MahoMcpSession::TabInfo> list;
    ForEachCurrentBrowserWindowInterfaceOrderedByActivation(
        [&list](BrowserWindowInterface* browser) {
          if (browser && browser->GetTabStripModel() &&
              IsBrowserMcpEligible(browser)) {
            TabStripModel* model = browser->GetTabStripModel();
            for (int i = 0; i < model->count(); ++i) {
              content::WebContents* wc = model->GetWebContentsAt(i);
              if (wc && IsWebContentsMcpEligible(browser, wc)) {
                sessions::SessionTabHelper* helper =
                    sessions::SessionTabHelper::FromWebContents(wc);
                if (helper) {
                  maho::MahoMcpSession::TabInfo t;
                  t.id = helper->session_id().id();
                  t.title = base::UTF16ToUTF8(wc->GetTitle());
                  t.url = wc->GetLastCommittedURL().spec();
                  t.is_active = (wc->GetVisibility() == content::Visibility::VISIBLE);
                  list.push_back(t);
                }
              }
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
                sessions::SessionTabHelper* helper =
                    sessions::SessionTabHelper::FromWebContents(c);
                if (helper && helper->session_id().id() == tab_id) {
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

  std::vector<maho::MahoMcpSession::ConsoleMessage> GetConsoleMessages(int tab_id) override {
    std::vector<maho::MahoMcpSession::ConsoleMessage> list;
    content::WebContents* wc = (tab_id == 0) ? GetActiveWebContents() : GetWebContentsForTabId(tab_id);
    if (wc) {
      maho::MahoMcpConsoleCapture::CreateForWebContents(wc);
      maho::MahoMcpConsoleCapture* capture = maho::MahoMcpConsoleCapture::FromWebContents(wc);
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
      int tab_id, int64_t since_ms) override {
    std::vector<maho::MahoMcpSession::NavigationEvent> list;
    content::WebContents* wc = (tab_id == 0)
                                   ? GetActiveWebContents()
                                   : GetWebContentsForTabId(tab_id);
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

  maho::MahoMcpBrowserDelegate::PageContentResult GetPageContent(int tab_id) override {
    maho::MahoMcpBrowserDelegate::PageContentResult result;
    content::WebContents* wc = (tab_id == 0)
                                   ? GetActiveWebContents()
                                   : GetWebContentsForTabId(tab_id);
    if (wc) {
      result.url = wc->GetLastCommittedURL().spec();
      result.title = base::UTF16ToUTF8(wc->GetTitle());
      result.text = ExecuteMcpActionJsBlocking(wc, "document.documentElement ? document.documentElement.outerHTML : ''");
    }
    return result;
  }

  maho::MahoMcpBrowserDelegate::PageContextResult GetPageContext(int tab_id) override {
    maho::MahoMcpBrowserDelegate::PageContextResult result;
    content::WebContents* wc = (tab_id == 0)
                                   ? GetActiveWebContents()
                                   : GetWebContentsForTabId(tab_id);
    if (wc) {
      result.url = wc->GetLastCommittedURL().spec();
      result.title = base::UTF16ToUTF8(wc->GetTitle());
      result.content = ExecuteMcpActionJsBlocking(wc, "document.body ? document.body.innerText : ''");
    }
    return result;
  }

  maho::MahoMcpBrowserDelegate::SearchResult SearchInPage(int tab_id, const std::string& query) override {
    maho::MahoMcpBrowserDelegate::SearchResult result;
    content::WebContents* wc = (tab_id == 0)
                                   ? GetActiveWebContents()
                                   : GetWebContentsForTabId(tab_id);
    if (wc) {
      std::string escaped_query = base::GetQuotedJSONString(query);
      std::string script =
          "(()=>{"
          "const text = (document.body ? document.body.innerText : '').toLowerCase();"
          "const q = (" + escaped_query + ").toLowerCase();"
          "if(!q) return JSON.stringify({match_count:0,active_match_index:-1});"
          "let count = 0;"
          "let pos = text.indexOf(q);"
          "while(pos !== -1){"
          "  count++;"
          "  pos = text.indexOf(q, pos + q.length);"
          "}"
          "return JSON.stringify({match_count:count,active_match_index:count > 0 ? 0 : -1});"
          "})()";
      std::string js_res = ExecuteMcpActionJsBlocking(wc, script);
      std::optional<base::Value> parsed = base::JSONReader::Read(js_res, base::JSON_PARSE_RFC);
      if (parsed && parsed->is_dict()) {
        const auto& dict = parsed->GetDict();
        result.match_count = dict.FindInt("match_count").value_or(0);
        result.active_match_index = dict.FindInt("active_match_index").value_or(-1);
      }
    }
    return result;
  }

  maho::MahoMcpBrowserDelegate::QuerySelectorResult QuerySelector(int tab_id, const std::string& selector) override {
    maho::MahoMcpBrowserDelegate::QuerySelectorResult result;
    content::WebContents* wc = (tab_id == 0)
                                   ? GetActiveWebContents()
                                   : GetWebContentsForTabId(tab_id);
    if (wc) {
      std::string escaped_sel = base::GetQuotedJSONString(selector);
      std::string script =
          "(()=>{"
          "const el = document.querySelector(" + escaped_sel + ");"
          "if(!el) return '';"
          "window.__mcp_elements = window.__mcp_elements || [];"
          "let idx = window.__mcp_elements.indexOf(el);"
          "if(idx === -1){"
          "  idx = window.__mcp_elements.length;"
          "  window.__mcp_elements.push(el);"
          "}"
          "return JSON.stringify({ref_id:'ref_' + idx, tag:el.tagName.toLowerCase()});"
          "})()";
      std::string js_res = ExecuteMcpActionJsBlocking(wc, script);
      std::optional<base::Value> parsed = base::JSONReader::Read(js_res, base::JSON_PARSE_RFC);
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
    content::WebContents* wc = (tab_id == 0)
                                   ? GetActiveWebContents()
                                   : GetWebContentsForTabId(tab_id);
    if (wc) {
      std::string escaped_ref = base::GetQuotedJSONString(ref_id);
      std::string script =
          "(()=>{"
          "const refId = " + escaped_ref + ";"
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

  std::string GetElementAttribute(int tab_id, const std::string& ref_id, const std::string& attribute) override {
    content::WebContents* wc = (tab_id == 0)
                                   ? GetActiveWebContents()
                                   : GetWebContentsForTabId(tab_id);
    if (wc) {
      std::string escaped_ref = base::GetQuotedJSONString(ref_id);
      std::string escaped_attr = base::GetQuotedJSONString(attribute);
      std::string script =
          "(()=>{"
          "const refId = " + escaped_ref + ";"
          "const attrName = " + escaped_attr + ";"
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

  bool WaitForSelector(int tab_id, const std::string& selector, int timeout_ms) override {
    base::TimeTicks start = base::TimeTicks::Now();
    base::TimeDelta timeout = base::Milliseconds(timeout_ms);
    std::string escaped_sel = base::GetQuotedJSONString(selector);
    std::string script = "document.querySelector(" + escaped_sel + ") ? 'true' : 'false'";
    while (true) {
      // Defect A (UAF) fix: re-resolve the WebContents on every iteration
      // instead of caching a raw pointer across the nested kNestableTasksAllowed
      // RunLoop pumps below. A concurrent browser_tab_close runs as a nestable
      // task inside those pumps and can destroy the WebContents mid-wait; caching
      // the pointer would dereference freed memory. Mirrors the re-resolve
      // pattern already used at CaptureVisibleViewFromTab / OnScreenshotCaptured
      // / OnElementScreenshotCaptured.
      content::WebContents* wc = (tab_id == 0)
                                     ? GetActiveWebContents()
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
              sessions::SessionTabHelper* helper =
                  sessions::SessionTabHelper::FromWebContents(new_contents.get());
              if (helper) {
                new_tab_id = helper->session_id().id();
              }
              if (!url.is_empty() && url.spec() != "about:blank") {
                content::NavigationController::LoadURLParams nav_params(url);
                nav_params.transition_type = ui::PageTransitionFromInt(
                    ui::PAGE_TRANSITION_TYPED | ui::PAGE_TRANSITION_FROM_ADDRESS_BAR);
                new_contents->GetController().LoadURLWithParams(nav_params);
              }
              model->AppendWebContents(std::move(new_contents), /*foreground=*/true);
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
                sessions::SessionTabHelper* helper =
                    sessions::SessionTabHelper::FromWebContents(c);
                if (helper && helper->session_id().id() == tab_id) {
                  model->CloseWebContentsAt(i, TabCloseTypes::CLOSE_CREATE_HISTORICAL_TAB);
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

  maho::MahoMcpTargetResolution ResolveTabTarget(int requested_tab_id) override {
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
                sessions::SessionTabHelper* helper =
                    sessions::SessionTabHelper::FromWebContents(c);
                if (helper && helper->session_id().id() == requested_tab_id) {
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
      if (!it->second ||
          !it->second->Revalidate(MahoPrivateCapability::kMCP)) {
        it = resolved_target_tokens_.erase(it);
      } else {
        ++it;
      }
    }
  }

  void RecursiveSearchBookmarks(const ::bookmarks::BookmarkNode* node,
                                const std::string& query_lower,
                                std::vector<maho::MahoMcpBrowserDelegate::BookmarkInfo>& results) {
    if (!node) return;
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

  std::vector<maho::MahoMcpBrowserDelegate::BookmarkInfo> SearchBookmarks(const std::string& query) override {
    std::vector<maho::MahoMcpBrowserDelegate::BookmarkInfo> results;
    Profile* profile = GetProfile();
    if (!profile) return results;
    ::bookmarks::BookmarkModel* bookmark_model =
        BookmarkModelFactory::GetForBrowserContext(profile);
    if (bookmark_model && bookmark_model->loaded()) {
      std::string query_lower = base::ToLowerASCII(query);
      RecursiveSearchBookmarks(bookmark_model->root_node(), query_lower, results);
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
    const size_t limit =
        (max_results == 0)
            ? 50
            : (max_results > kMaxHistoryResults ? kMaxHistoryResults
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

  maho::MahoMcpBrowserDelegate::BookmarkInfo CreateBookmark(const std::string& title, const GURL& url, const std::string& folder) override {
    maho::MahoMcpBrowserDelegate::BookmarkInfo result;
    Profile* profile = GetProfile();
    if (!profile) return result;
    ::bookmarks::BookmarkModel* bookmark_model =
        BookmarkModelFactory::GetForBrowserContext(profile);
    if (!bookmark_model || !bookmark_model->loaded()) return result;

    const ::bookmarks::BookmarkNode* parent = bookmark_model->bookmark_bar_node();
    if (!folder.empty()) {
      const ::bookmarks::BookmarkNode* found_folder = nullptr;
      for (const auto& child : parent->children()) {
        if (child->is_folder() && base::UTF16ToUTF8(child->GetTitle()) == folder) {
          found_folder = child.get();
          break;
        }
      }
      if (!found_folder) {
        parent = bookmark_model->AddFolder(parent, parent->children().size(), base::UTF8ToUTF16(folder));
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

  // Synchronously snapshot the AX tree of `wc` via RequestAXTreeSnapshot.
  // This bypasses the visibility gate on live AX mode — session-restored
  // hidden tabs still return a real tree because the snapshot is a one-shot
  // Mojo request to the renderer that runs independently of live AX state.
  // MUST be called on UI thread; uses a nested RunLoop to await the Mojo
  // reply. Returns an empty AXTreeUpdate on failure.
  ui::AXTreeUpdate SnapshotAXTree(content::WebContents* wc) {
    ui::AXTreeUpdate update;
    if (!wc) return update;
    base::RunLoop run_loop(base::RunLoop::Type::kNestableTasksAllowed);
    wc->RequestAXTreeSnapshot(
        base::BindOnce(
            [](ui::AXTreeUpdate* out, base::OnceClosure quit,
               ui::AXTreeUpdate& u) {
              *out = std::move(u);
              std::move(quit).Run();
            },
            &update, run_loop.QuitClosure()),
        ui::kAXModeWebContentsOnly,
        /*max_nodes=*/0,
        /*timeout=*/base::Seconds(5),
        content::WebContents::AXTreeSnapshotPolicy::kAll);
    run_loop.Run();
    return update;
  }

  std::string GetPageText(int tab_id) override {
    content::WebContents* wc = (tab_id == 0)
                                   ? GetActiveWebContents()
                                   : GetWebContentsForTabId(tab_id);
    if (!wc) return std::string();
    ui::AXTreeUpdate update = SnapshotAXTree(wc);
    if (update.nodes.empty()) return std::string();
    ui::AXTree tree;
    if (!tree.Unserialize(update)) return std::string();
    const ui::AXNode* root = tree.root();
    if (!root) return std::string();

    std::string out;
    std::vector<const ui::AXNode*> stack{root};
    while (!stack.empty()) {
      const ui::AXNode* node = stack.back();
      stack.pop_back();
      const std::string& name = node->data().GetStringAttribute(
          ax::mojom::StringAttribute::kName);
      const auto role = node->GetRole();
      if (!name.empty() &&
          (role == ax::mojom::Role::kStaticText ||
           role == ax::mojom::Role::kInlineTextBox ||
           role == ax::mojom::Role::kHeading ||
           role == ax::mojom::Role::kParagraph ||
           role == ax::mojom::Role::kLink ||
           role == ax::mojom::Role::kButton)) {
        if (!out.empty()) out.push_back('\n');
        out.append(name);
      }
      for (auto it = node->children().rbegin();
           it != node->children().rend(); ++it) {
        stack.push_back(*it);
      }
    }
    return out;
  }

  base::Value GetAccessibilitySnapshot(
      int tab_id,
      maho::MahoMcpSession::RefTable* out_refs) override {
    content::WebContents* wc = (tab_id == 0)
                                   ? GetActiveWebContents()
                                   : GetWebContentsForTabId(tab_id);
    base::DictValue empty;
    empty.Set("role", "WebArea");
    empty.Set("children", base::ListValue());
    if (!wc) return base::Value(std::move(empty));
    ui::AXTreeUpdate update = SnapshotAXTree(wc);
    if (update.nodes.empty()) return base::Value(std::move(empty));
    NormalizeMcpSnapshotRoles(&update);
    ui::AXTree tree;
    if (!tree.Unserialize(update)) return base::Value(std::move(empty));

    maho::MahoMcpAccessibilityHandler::RefTable local_refs;
    base::Value snapshot =
        maho::MahoMcpAccessibilityHandler::BuildSnapshot(tree, local_refs);
    if (out_refs) {
      out_refs->clear();
      for (const auto& pair : local_refs) {
        out_refs->emplace(pair.first, pair.second);
      }
    }
    return snapshot;
  }

  void CaptureFullPagePngBase64(
      int tab_id,
      base::OnceCallback<void(std::string)> done) override {
    content::WebContents* wc = (tab_id == 0)
                                   ? GetActiveWebContents()
                                   : GetWebContentsForTabId(tab_id);
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
      sessions::SessionTabHelper* helper =
          sessions::SessionTabHelper::FromWebContents(wc);
      if (helper) {
        resolved_tab_id = helper->session_id().id();
      }
    }
    // Wake HIDDEN tab so compositor renders before capture.
    const bool was_hidden =
        wc->GetVisibility() == content::Visibility::HIDDEN;
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
      view->CopyFromSurface(gfx::Rect(), gfx::Size(), base::Seconds(5),
                            base::BindOnce(
                                &MahoMcpBrowserDelegateImpl::OnScreenshotCaptured,
                                weak_factory_.GetWeakPtr(), resolved_tab_id,
                                was_hidden, std::move(done)));
    }
  }

  void CaptureVisibleViewFromTab(
      int tab_id,
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

    view->CopyFromSurface(gfx::Rect(), gfx::Size(), base::Seconds(5),
                          base::BindOnce(
                              &MahoMcpBrowserDelegateImpl::OnScreenshotCaptured,
                              weak_factory_.GetWeakPtr(), tab_id, was_hidden,
                              std::move(done)));
  }

  void OnScreenshotCaptured(int tab_id, bool was_hidden,
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
    content::WebContents* wc = (tab_id == 0)
                                   ? GetActiveWebContents()
                                   : GetWebContentsForTabId(tab_id);
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
      sessions::SessionTabHelper* helper =
          sessions::SessionTabHelper::FromWebContents(wc);
      if (helper) {
        resolved_tab_id = helper->session_id().id();
      }
    }
    const bool was_hidden =
        wc->GetVisibility() == content::Visibility::HIDDEN;
    if (was_hidden) {
      wc->WasShown();
    }
    if (was_hidden) {
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
          FROM_HERE,
          base::BindOnce(&MahoMcpBrowserDelegateImpl::CaptureVisibleViewFromTabForElement,
                         weak_factory_.GetWeakPtr(), resolved_tab_id,
                         was_hidden, ax_id, std::move(done)),
          base::Milliseconds(300));
    } else {
      view->CopyFromSurface(gfx::Rect(), gfx::Size(), base::Seconds(5),
                            base::BindOnce(
                                &MahoMcpBrowserDelegateImpl::OnElementScreenshotCaptured,
                                weak_factory_.GetWeakPtr(), resolved_tab_id,
                                was_hidden, ax_id, std::move(done)));
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
    view->CopyFromSurface(gfx::Rect(), gfx::Size(), base::Seconds(5),
                          base::BindOnce(
                              &MahoMcpBrowserDelegateImpl::OnElementScreenshotCaptured,
                              weak_factory_.GetWeakPtr(), tab_id, was_hidden,
                              ax_id, std::move(done)));
  }

  void OnElementScreenshotCaptured(int tab_id, bool was_hidden,
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
            [](MahoMcpBrowserDelegateImpl* delegate, int id, std::string dir, int px, std::optional<ui::AXNodeID> ax) {
              content::WebContents* wc = (id == 0) ? delegate->GetActiveWebContents() : delegate->GetWebContentsForTabId(id);
              if (wc) {
                maho::MahoMcpInputSynthesizer::Scroll(wc, dir, px, ax);
              }
            },
            base::Unretained(this), tab_id, direction, pixels, ax_id));
    return true;
  }

  bool Click(int tab_id, ui::AXNodeID ax_id) override {
    content::GetUIThreadTaskRunner({})->PostTask(
        FROM_HERE,
        base::BindOnce(
            [](MahoMcpBrowserDelegateImpl* delegate, int id,
               ui::AXNodeID ax) {
              content::WebContents* wc =
                  (id == 0) ? delegate->GetActiveWebContents()
                            : delegate->GetWebContentsForTabId(id);
              if (wc) {
                ui::AXTreeUpdate update = delegate->SnapshotAXTree(wc);
                content::RenderWidgetHostView* view = wc->GetRenderWidgetHostView();
                const float device_scale_factor = view ? view->GetDeviceScaleFactor() : 1.f;
                std::optional<gfx::PointF> center = ComputeViewportCenterCss(update, ax, device_scale_factor);
                bool js_clicked = false;
                if (center) {
                  const std::string x_str = base::NumberToString(std::lround(center->x()));
                  const std::string y_str = base::NumberToString(std::lround(center->y()));
                  const std::string script =
                      "(()=>{const el=document.elementFromPoint(" + x_str + "," + y_str + ");"
                      "if(el){"
                      "el.dispatchEvent(new MouseEvent('mousedown',{bubbles:true,cancelable:true,view:window}));"
                      "el.dispatchEvent(new MouseEvent('mouseup',{bubbles:true,cancelable:true,view:window}));"
                      "el.click();"
                      "return 'ok';"
                      "}"
                      "return 'nopoint';})()";
                  std::string js_res = ExecuteMcpActionJsBlocking(wc, script);
                  if (js_res == "ok") {
                    js_clicked = true;
                  }
                  maho::MahoMcpInputSynthesizer::HoverAt(wc, std::lround(center->x()), std::lround(center->y()));
                }
                if (!js_clicked) {
                  PerformAxAction(wc, ax, ax::mojom::Action::kDoDefault);
                }
              }
            },
            base::Unretained(this), tab_id, ax_id));
    return true;
  }

  bool Type(int tab_id, ui::AXNodeID ax_id, const std::string& text) override {
    content::GetUIThreadTaskRunner({})->PostTask(
        FROM_HERE,
        base::BindOnce(
            [](MahoMcpBrowserDelegateImpl* delegate, int id,
               ui::AXNodeID ax, std::string value) {
              content::WebContents* wc =
                  (id == 0) ? delegate->GetActiveWebContents()
                            : delegate->GetWebContentsForTabId(id);
              if (wc) {
                ui::AXTreeUpdate update = delegate->SnapshotAXTree(wc);
                content::RenderWidgetHostView* view = wc->GetRenderWidgetHostView();
                const float device_scale_factor = view ? view->GetDeviceScaleFactor() : 1.f;
                std::optional<gfx::PointF> center = ComputeViewportCenterCss(update, ax, device_scale_factor);
                bool js_typed = false;
                if (center) {
                  const std::string x_str = base::NumberToString(std::lround(center->x()));
                  const std::string y_str = base::NumberToString(std::lround(center->y()));
                  const std::string value_json = base::GetQuotedJSONString(value);
                  const std::string script =
                      "(()=>{const el=document.elementFromPoint(" + x_str + "," + y_str + ");"
                      "if(el){"
                      "el.focus();"
                      "if('value' in el){"
                      "el.value=" + value_json + ";"
                      "el.dispatchEvent(new Event('input',{bubbles:true}));"
                      "el.dispatchEvent(new Event('change',{bubbles:true}));"
                      "}"
                      "return 'ok';"
                      "}"
                      "return 'nopoint';})()";
                  std::string js_res = ExecuteMcpActionJsBlocking(wc, script);
                  if (js_res == "ok") {
                    js_typed = true;
                  }
                  maho::MahoMcpInputSynthesizer::HoverAt(wc, std::lround(center->x()), std::lround(center->y()));
                }
                if (!js_typed) {
                  PerformAxAction(wc, ax, ax::mojom::Action::kFocus);
                  PerformAxAction(wc, ax, ax::mojom::Action::kSetValue, value);
                  DispatchActiveElementValueEvents(wc, value);
                }
              }
            },
            base::Unretained(this), tab_id, ax_id, text));
    return true;
  }

  bool Select(int tab_id,
              ui::AXNodeID ax_id,
              const std::string& value) override {
    content::GetUIThreadTaskRunner({})->PostTask(
        FROM_HERE,
        base::BindOnce(
            [](MahoMcpBrowserDelegateImpl* delegate, int id,
               ui::AXNodeID ax, std::string select_value) {
              content::WebContents* wc =
                  (id == 0) ? delegate->GetActiveWebContents()
                            : delegate->GetWebContentsForTabId(id);
              if (wc) {
                ui::AXTreeUpdate update = delegate->SnapshotAXTree(wc);
                content::RenderWidgetHostView* view = wc->GetRenderWidgetHostView();
                const float device_scale_factor = view ? view->GetDeviceScaleFactor() : 1.f;
                std::optional<gfx::PointF> center = ComputeViewportCenterCss(update, ax, device_scale_factor);
                bool js_selected = false;
                if (center) {
                  const std::string x_str = base::NumberToString(std::lround(center->x()));
                  const std::string y_str = base::NumberToString(std::lround(center->y()));
                  const std::string value_json = base::GetQuotedJSONString(select_value);
                  const std::string script =
                      "(()=>{const el=document.elementFromPoint(" + x_str + "," + y_str + ");"
                      "if(el){"
                      "el.focus();"
                      "if('value' in el){"
                      "el.value=" + value_json + ";"
                      "el.dispatchEvent(new Event('input',{bubbles:true}));"
                      "el.dispatchEvent(new Event('change',{bubbles:true}));"
                      "}"
                      "return 'ok';"
                      "}"
                      "return 'nopoint';})()";
                  std::string js_res = ExecuteMcpActionJsBlocking(wc, script);
                  if (js_res == "ok") {
                    js_selected = true;
                  }
                  maho::MahoMcpInputSynthesizer::HoverAt(wc, std::lround(center->x()), std::lround(center->y()));
                }
                if (!js_selected) {
                  PerformAxAction(wc, ax, ax::mojom::Action::kFocus);
                  PerformAxAction(wc, ax, ax::mojom::Action::kSetValue, select_value);
                  DispatchActiveElementValueEvents(wc, select_value);
                }
              }
            },
            base::Unretained(this), tab_id, ax_id, value));
    return true;
  }

  bool Hover(int tab_id, ui::AXNodeID ax_id) override {
    content::GetUIThreadTaskRunner({})->PostTask(
        FROM_HERE,
        base::BindOnce(
            [](MahoMcpBrowserDelegateImpl* delegate, int id, ui::AXNodeID ax) {
              content::WebContents* wc = (id == 0) ? delegate->GetActiveWebContents() : delegate->GetWebContentsForTabId(id);
              if (wc) {
                ui::AXTreeUpdate update = delegate->SnapshotAXTree(wc);
                content::RenderWidgetHostView* view = wc->GetRenderWidgetHostView();
                const float device_scale_factor = view ? view->GetDeviceScaleFactor() : 1.f;
                std::optional<gfx::PointF> center = ComputeViewportCenterCss(update, ax, device_scale_factor);
                if (center) {
                  const std::string x_str = base::NumberToString(std::lround(center->x()));
                  const std::string y_str = base::NumberToString(std::lround(center->y()));
                  const std::string script =
                      "(()=>{const el=document.elementFromPoint(" + x_str + "," + y_str + ");"
                      "if(el){"
                      "el.dispatchEvent(new MouseEvent('mouseover',{bubbles:true,cancelable:true,view:window}));"
                      "el.dispatchEvent(new MouseEvent('mouseenter',{bubbles:false,cancelable:false,view:window}));"
                      "el.dispatchEvent(new MouseEvent('mousemove',{bubbles:true,cancelable:true,view:window}));"
                      "return 'ok';"
                      "}"
                      "return 'nopoint';})()";
                  ExecuteMcpActionJsBlocking(wc, script);
                  maho::MahoMcpInputSynthesizer::HoverAt(wc, std::lround(center->x()), std::lround(center->y()));
                }
              }
            },
            base::Unretained(this), tab_id, ax_id));
    return true;
  }

  bool KeyPress(int tab_id,
                const std::string& key,
                const std::vector<std::string>& modifiers) override {
    content::GetUIThreadTaskRunner({})->PostTask(
        FROM_HERE,
        base::BindOnce(
            [](MahoMcpBrowserDelegateImpl* delegate, int id, std::string k, std::vector<std::string> mods) {
              content::WebContents* wc = (id == 0) ? delegate->GetActiveWebContents() : delegate->GetWebContentsForTabId(id);
              if (wc) {
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
        base::BindOnce([](int target_id) {
          ForEachCurrentBrowserWindowInterfaceOrderedByActivation(
              [target_id](BrowserWindowInterface* browser) {
                if (browser && browser->GetTabStripModel() &&
                    IsBrowserMcpEligible(browser)) {
                  TabStripModel* model = browser->GetTabStripModel();
                  for (int i = 0; i < model->count(); ++i) {
                    content::WebContents* c = model->GetWebContentsAt(i);
                    if (c) {
                      sessions::SessionTabHelper* helper =
                          sessions::SessionTabHelper::FromWebContents(c);
                      if (helper && helper->session_id().id() == target_id) {
                        model->ActivateTabAt(i, TabStripUserGestureDetails(
                            TabStripUserGestureDetails::GestureType::kOther));
                        return false;
                      }
                    }
                  }
                }
                return true;
              });
        }, tab_id));
    return true;
  }

  bool Navigate(int tab_id, const GURL& url) override {
    content::GetUIThreadTaskRunner({})->PostTask(
        FROM_HERE,
        base::BindOnce(
            [](MahoMcpBrowserDelegateImpl* delegate, int id, const GURL& nav_url) {
              content::WebContents* wc = (id == 0) ? delegate->GetActiveWebContents() : delegate->GetWebContentsForTabId(id);
              if (wc) {
                content::NavigationController::LoadURLParams params(nav_url);
                params.transition_type = ui::PageTransitionFromInt(
                    ui::PAGE_TRANSITION_TYPED | ui::PAGE_TRANSITION_FROM_ADDRESS_BAR);
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
              content::WebContents* wc = (id == 0) ? delegate->GetActiveWebContents() : delegate->GetWebContentsForTabId(id);
              if (wc && wc->GetRenderWidgetHostView()) {
                wc->GetRenderWidgetHostView()->SetSize(gfx::Size(w, h));
              }
            },
            base::Unretained(this), tab_id, width, height));
    return true;
  }

  void StartNetworkCapture(
      const std::string& capture_id,
      int tab_id,
      StartNetworkCaptureCallback callback) override {
    if (!content::BrowserThread::CurrentlyOn(content::BrowserThread::UI)) {
      content::GetUIThreadTaskRunner({})->PostTask(
          FROM_HERE,
          base::BindOnce(
              &MahoMcpBrowserDelegateImpl::StartNetworkCaptureOnUi,
              ui_weak_ptr_, capture_id, tab_id, std::move(callback)));
      return;
    }
    StartNetworkCaptureOnUi(capture_id, tab_id, std::move(callback));
  }

  void StopNetworkCapture(
      const std::string& capture_id,
      StopNetworkCaptureCallback callback) override {
    if (!content::BrowserThread::CurrentlyOn(content::BrowserThread::UI)) {
      content::GetUIThreadTaskRunner({})->PostTask(
          FROM_HERE,
          base::BindOnce(&MahoMcpBrowserDelegateImpl::StopNetworkCaptureOnUi,
                         ui_weak_ptr_, capture_id, std::move(callback)));
      return;
    }
    StopNetworkCaptureOnUi(capture_id, std::move(callback));
  }

  void CancelNetworkCapture(const std::string& capture_id) override {
    if (!content::BrowserThread::CurrentlyOn(content::BrowserThread::UI)) {
      content::GetUIThreadTaskRunner({})->PostTask(
          FROM_HERE,
          base::BindOnce(&MahoMcpBrowserDelegateImpl::CancelNetworkCaptureOnUi,
                         ui_weak_ptr_, capture_id));
      return;
    }
    CancelNetworkCaptureOnUi(capture_id);
  }

 private:
  void StartNetworkCaptureOnUi(
      const std::string& capture_id,
      int tab_id,
      StartNetworkCaptureCallback callback) {
    content::WebContents* wc = (tab_id == 0)
                                   ? GetActiveWebContents()
                                   : GetWebContentsForTabId(tab_id);
    if (!wc || capture_id.empty() ||
        network_observers_.find(capture_id) != network_observers_.end()) {
      std::move(callback).Run(StartNetworkCaptureResult{});
      return;
    }
    // Resolve the concrete tab id we bound to so an omitted request stays
    // fixed to this tab even if the active tab later changes.
    int resolved_tab_id = tab_id;
    if (resolved_tab_id == 0) {
      if (sessions::SessionTabHelper* helper =
              sessions::SessionTabHelper::FromWebContents(wc)) {
        resolved_tab_id = helper->session_id().id();
      }
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
                              StopNetworkCaptureCallback callback) {
    auto it = network_observers_.find(capture_id);
    if (it == network_observers_.end()) {
      std::move(callback).Run(std::nullopt);
      return;
    }
    std::unique_ptr<maho::MahoMcpNetworkObserver> observer =
        std::move(it->second);
    network_observers_.erase(it);
    observer->StopCapture();
    auto har = observer->GetHar();
    std::move(callback).Run(std::move(har));
  }

  void CancelNetworkCaptureOnUi(const std::string& capture_id) {
    auto it = network_observers_.find(capture_id);
    if (it == network_observers_.end()) {
      return;
    }
    std::unique_ptr<maho::MahoMcpNetworkObserver> observer =
        std::move(it->second);
    network_observers_.erase(it);
    observer->StopCapture();
  }

  std::unordered_map<std::string,
                     std::unique_ptr<maho::MahoMcpNetworkObserver>>
      network_observers_;
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

}  // namespace

void MahoBrowserMainExtraParts::PostProfileInit(Profile* profile,
                                                 bool is_initial_profile) {
  // Must be startup-time (not browser-triggered): the login gate suppresses
  // the startup browser, so browser-triggered init would deadlock.
  if (is_initial_profile && profile && profile->IsRegularProfile() && !core_) {
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
            std::move(mcp_pipe_server_),
            std::move(mcp_session_token_)));
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

  if (core_) {
    MahoSpaceThemeState::Clear();
    routines_scheduler_.reset();
    maho::core::SaveState(core_);
    maho::SetCore(nullptr);
    maho::core::Destroy(core_);
    core_ = nullptr;
    LOG(INFO) << "Maho core destroyed";
  }
}

void MahoBrowserMainExtraParts::MaybeLaunchMailHelper(Profile* profile) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!profile || !profile->IsRegularProfile()) {
    return;
  }

  PrefService* prefs = profile->GetPrefs();
  if (!prefs || !maho::auth::HasValidRelaySession(prefs)) {
    LOG(INFO) << "[MahoMailService] Skipping launch; profile has no "
                 "valid mail session.";
    return;
  }

  maho::MahoMailService* service =
      maho::MahoMailServiceFactory::GetForProfile(profile);
  if (!service) {
    return;
  }

  base::FilePath crashpad_database;
  base::FilePath user_data_dir;
  if (base::PathService::Get(chrome::DIR_USER_DATA, &user_data_dir)) {
    crashpad_database = user_data_dir.AppendASCII("Crashpad");
    base::CreateDirectory(crashpad_database);
  }

  service->EnsureHelperLaunched(maho::mail_helper::kMahoMailHelperVersion,
                                profile->GetPath().AsUTF8Unsafe(),
                                crashpad_database);
}

void MahoBrowserMainExtraParts::InitializeRegularServicesOnce(Profile* profile) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  regular_profile_ = profile;

  LOG(INFO) << "MAHO_INIT_AUDIT: class=regular, decision=allow, stage=core_init_begin";

  base::CreateDirectory(storage_path_);

  base::FilePath key_path = storage_path_.AppendASCII("maho_storage.key");
  if (maho::core::DeriveAndSetStorageKey(key_path.AsUTF8Unsafe())) {
    core_ = maho::core::CreateWithStorage(storage_path_.AsUTF8Unsafe());
  } else {
    LOG(ERROR) << "Maho storage key derivation/setting failed; password manager/persistent storage will be disabled.";
  }

  if (core_) {
    maho::SetCore(core_);
    maho::core::InstallLogCallback();
    maho::core::LoadState(core_);
    LOG(INFO) << "Maho core initialized, version: " << maho::core::Version();
    PopulateBridgeFromCore(core_);
    maho::MahoTabRegistry::Get();

    routines_scheduler_ = std::make_unique<maho::MahoRoutinesScheduler>(core_);
    routines_scheduler_->Start();

    // Max-tier gated + fire-and-forget inside the FFI (no-op otherwise).
    maho_routines_fire_event(core_, "{\"kind\":\"on_startup\"}");
  } else {
    LOG(ERROR) << "Maho core initialization failed";
  }

  LOG(INFO) << "MAHO_INIT_AUDIT: class=regular, decision=allow, stage=core_init_complete";

  LOG(INFO) << "MAHO_INIT_AUDIT: class=regular, decision=allow, stage=profile_init_begin";

  PrefService* prefs = profile->GetPrefs();
  // Tests that rely on the standard InProcessBrowserTest startup browser pass
  // --maho-disable-login-gate so the onboarding gate does not suppress the
  // initial browser window (which would leave InProcessBrowserTest::browser()
  // null). Default behavior is unchanged when the switch is absent.
  const bool needs_login_gate =
      !base::CommandLine::ForCurrentProcess()->HasSwitch(
          "maho-disable-login-gate") &&
      !maho::auth::HasValidRelaySession(prefs);
  prefs->SetBoolean(maho::welcome::kLoginGateActive, needs_login_gate);
  if (needs_login_gate) {
    maho::MahoWelcomeWindow::Show(profile);
  }

  // Register MCP browser delegate
  static base::NoDestructor<MahoMcpBrowserDelegateImpl> mcp_delegate;
  maho::MahoMcpSession::SetBrowserDelegate(mcp_delegate.get());

  extension_state_bridge_ =
      std::make_unique<maho::MahoExtensionStateBridge>(profile);

  extension_side_panel_coordinator_ =
      new maho::MahoExtensionSidePanelCoordinator(profile);

  sync_relay_client_ =
      std::make_unique<maho::MahoSyncRelayClient>(profile);
  sync_relay_client_->StartPolling();

  maho::MigrateAiSettingsFromMacOSDefaults(profile);

  maho::MahoTabIdSessionHelper::GetForProfile(profile);

  maho::MahoUpdateManager::GetInstance()->Initialize(profile);
  maho::updates::MahoConfigManager::GetInstance()->Initialize(profile);

  MaybeLaunchMailHelper(profile);


  // If user hasn't explicitly set the value, override the default to LAST
  // so SessionService restore kicks in.
  const PrefService::Preference* p = prefs->FindPreference(prefs::kRestoreOnStartup);
  if (p && p->IsDefaultValue()) {
    SessionStartupPref::SetStartupPref(profile,
        SessionStartupPref(SessionStartupPref::LAST));
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

  LOG(INFO) << "MAHO_INIT_AUDIT: class=regular, decision=allow, stage=profile_init_complete";
}
