#include "maho/browser/maho_browser_main_extra_parts.h"

#include <string>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/json/string_escape.h"
#include "base/logging.h"
#include "base/memory/weak_ptr.h"
#include "base/no_destructor.h"
#include "base/path_service.h"
#include "base/values.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
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
#include "maho/browser/maho_tab_registry.h"
#include "maho/browser/updates/maho_update_manager.h"
#include "chrome/browser/sessions/session_restore.h"
#include "base/strings/utf_string_conversions.h"
#include "maho/browser/maho_tab_id_helper.h"
#include "maho/browser/maho_tab_id_session_helper.h"
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
#include "maho/browser/mcp/maho_mcp_input_synthesizer.h"
#include "maho/browser/mcp/maho_mcp_navigation_tracker.h"
#include "maho/browser/mcp/maho_mcp_screenshot_handler.h"
#include "maho/browser/mcp/maho_mcp_session.h"
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

MahoBrowserMainExtraParts::MahoBrowserMainExtraParts() = default;

MahoBrowserMainExtraParts::~MahoBrowserMainExtraParts() {
  if (core_) {
    routines_scheduler_.reset();
    maho::core::SaveState(core_);
    maho::SetCore(nullptr);
    maho::core::Destroy(core_);
    core_ = nullptr;
  }
}

namespace {

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

  if (content::BrowserThread::CurrentlyOn(content::BrowserThread::UI)) {
    MahoSpaceThemeState::UpdateFromCore();
  } else {
    content::GetUIThreadTaskRunner({})->PostTask(
        FROM_HERE, base::BindOnce(&MahoSpaceThemeState::UpdateFromCore));
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
      content::ISOLATED_WORLD_ID_GLOBAL);
}

}  // namespace

void MahoBrowserMainExtraParts::PostEarlyInitialization() {
  base::FilePath user_data_dir;
  if (base::PathService::Get(chrome::DIR_USER_DATA, &user_data_dir)) {
    storage_path_ = user_data_dir.AppendASCII("MahoCore");
    base::CreateDirectory(storage_path_);
  }

  if (!storage_path_.empty()) {
    core_ = maho::core::CreateWithStorage(storage_path_.AsUTF8Unsafe());
  }
  if (!core_) {
    core_ = maho::core::Create();
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
  } else {
    LOG(ERROR) << "Maho core initialization failed";
  }

  ui::ColorProviderManager::Get().AppendColorProviderInitializer(
      base::BindRepeating(&AddMahoColorMixers));
}

namespace {

class MahoMcpBrowserDelegateImpl : public maho::MahoMcpBrowserDelegate {
 public:
  ~MahoMcpBrowserDelegateImpl() override = default;

  std::vector<maho::MahoMcpSession::TabInfo> GetTabList() override {
    std::vector<maho::MahoMcpSession::TabInfo> list;
    ForEachCurrentBrowserWindowInterfaceOrderedByActivation(
        [&list](BrowserWindowInterface* browser) {
          if (browser && browser->GetTabStripModel()) {
            TabStripModel* model = browser->GetTabStripModel();
            for (int i = 0; i < model->count(); ++i) {
              content::WebContents* wc = model->GetWebContentsAt(i);
              if (wc) {
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
    content::WebContents* active_wc = nullptr;
    ForEachCurrentBrowserWindowInterfaceOrderedByActivation(
        [&active_wc](BrowserWindowInterface* browser) {
          if (browser && browser->GetTabStripModel()) {
            active_wc = browser->GetTabStripModel()->GetActiveWebContents();
            if (active_wc) {
              return false;
            }
          }
          return true;
        });
    return active_wc;
  }

  content::WebContents* GetWebContentsForTabId(int tab_id) {
    content::WebContents* match = nullptr;
    ForEachCurrentBrowserWindowInterfaceOrderedByActivation(
        [&match, tab_id](BrowserWindowInterface* browser) {
          if (browser && browser->GetTabStripModel()) {
            TabStripModel* model = browser->GetTabStripModel();
            for (int i = 0; i < model->count(); ++i) {
              content::WebContents* c = model->GetWebContentsAt(i);
              if (c) {
                sessions::SessionTabHelper* helper =
                    sessions::SessionTabHelper::FromWebContents(c);
                if (helper && helper->session_id().id() == tab_id) {
                  match = c;
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
              PerformAxAction(wc, ax, ax::mojom::Action::kDoDefault);
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
              PerformAxAction(wc, ax, ax::mojom::Action::kFocus);
              PerformAxAction(wc, ax, ax::mojom::Action::kSetValue, value);
              DispatchActiveElementValueEvents(wc, value);
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
              PerformAxAction(wc, ax, ax::mojom::Action::kFocus);
              PerformAxAction(wc, ax, ax::mojom::Action::kSetValue,
                              select_value);
              DispatchActiveElementValueEvents(wc, select_value);
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
                maho::MahoMcpInputSynthesizer::Hover(wc, ax);
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
                if (browser && browser->GetTabStripModel()) {
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

 private:
  base::WeakPtrFactory<MahoMcpBrowserDelegateImpl> weak_factory_{this};
};

}  // namespace

void MahoBrowserMainExtraParts::PostProfileInit(Profile* profile,
                                                 bool is_initial_profile) {
  if (!is_initial_profile) {
    return;
  }
  if (profile && !profile->IsOffTheRecord()) {
    // Register MCP browser delegate
    static base::NoDestructor<MahoMcpBrowserDelegateImpl> mcp_delegate;
    maho::MahoMcpSession::SetBrowserDelegate(mcp_delegate.get());

    extension_state_bridge_ =
        std::make_unique<maho::MahoExtensionStateBridge>(profile);

    maho::MigrateAiSettingsFromMacOSDefaults(profile);

    maho::MahoTabIdSessionHelper::GetForProfile(profile);

    maho::MahoUpdateManager::GetInstance()->Initialize(profile);

    // If user hasn't explicitly set the value, override the default to LAST
    // so SessionService restore kicks in.
    PrefService* prefs = profile->GetPrefs();
    const PrefService::Preference* p = prefs->FindPreference(prefs::kRestoreOnStartup);
    if (p && p->IsDefaultValue()) {
      SessionStartupPref::SetStartupPref(profile,
          SessionStartupPref(SessionStartupPref::LAST));
    }

    // MCP server uses net::UnixDomainServerSocket (POSIX) or Windows overlapped
    // I/O via base::win::ObjectWatcher; both require an IO message pump, so
    // Start() must run on the IO thread. Construct on UI, bounce Start() to
    // IO. Destruction is also posted to IO in PostMainMessageLoopRun so every
    // access to the SEQUENCE_CHECKER-guarded state stays on that sequence.
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
