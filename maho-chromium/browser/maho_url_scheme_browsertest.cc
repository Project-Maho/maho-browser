// Copyright 2026 Maho Browser. All rights reserved.

#ifndef HAS_OUT_OF_PROC_TEST_RUNNER
#define HAS_OUT_OF_PROC_TEST_RUNNER
#endif

#include "maho/browser/maho_url_scheme.h"

#include <memory>
#include <string>
#include <utility>

#include "base/check.h"
#include "base/command_line.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/logging.h"
#include "base/memory/raw_ptr.h"
#include "base/run_loop.h"
#include "base/task/single_thread_task_runner.h"
#include "base/task/thread_pool/thread_pool_instance.h"
#include "base/test/bind.h"
#include "base/test/scoped_run_loop_timeout.h"
#include "base/test/test_future.h"
#include "base/test/test_timeouts.h"
#include "base/threading/thread_restrictions.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/create_browser_window.h"

// These desktop fixtures use CreateBrowserWindow, whose non-Android factory
// creates Browser instances. Borrowing the concrete pointer does not own it.
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/child_process_security_policy.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/security_principal.h"
#include "content/public/browser/site_instance.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/test_navigation_observer.h"
#include "maho/browser/maho_browser_main_extra_parts.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/mcp/maho_mcp_session.h"
#include "maho/browser/ui/maho_settings_navigation.h"
#include "maho/browser/ui/views/sidebar/maho_sidebar_prefs.h"
#include "maho/browser/ui/views/welcome/maho_welcome_window.h"
#include "maho/browser/ui/webui/maho_account_prefs.h"
#include "maho/browser/ui/webui/maho_ai_prefs.h"
#include "maho/browser/ui/webui/maho_auth_utils.h"
#include "maho/browser/ui/webui/maho_welcome/maho_welcome_page_handler.h"
#include "maho/browser/ui/webui/maho_welcome/maho_welcome_prefs.h"
#include "maho/components/constants/webui_url_constants.h"
#include "maho/third_party/maho/maho_ffi.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/accessibility/ax_mode.h"
#include "ui/accessibility/ax_node.h"
#include "ui/accessibility/ax_tree.h"
#include "ui/accessibility/ax_tree_update.h"
#include "ui/base/page_transition_types.h"
#include "ui/shell_dialogs/select_file_dialog_factory.h"
#include "ui/shell_dialogs/select_file_policy.h"
#include "ui/shell_dialogs/selected_file_info.h"
#include "ui/views/test/widget_test.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace maho {
namespace {

constexpr char kDisableLoginGateSwitch[] = "maho-disable-login-gate";

content::WebContents* ActiveWebContents(Browser* browser) {
  return browser->GetTabStripModel()->GetActiveWebContents();
}

content::NavigationEntry* LastCommittedEntry(content::WebContents* contents) {
  return contents->GetController().GetLastCommittedEntry();
}

GURL RequiredActualUrlForAlias(const GURL& alias_url) {
  GURL actual_url;
  CHECK(MapMahoUrlAliasToActualUrl(alias_url, &actual_url));
  return actual_url;
}

GURL WithPane(const GURL& base_url, const std::string& pane) {
  if (pane.empty()) {
    return base_url;
  }
  GURL::Replacements replacements;
  const std::string query = "pane=" + pane;
  replacements.SetQueryStr(query);
  return base_url.ReplaceComponents(replacements);
}

void ExpectEntryUrls(content::NavigationEntry* entry,
                     const GURL& expected_actual_url,
                     const GURL& expected_virtual_url) {
  ASSERT_NE(nullptr, entry);
  EXPECT_EQ(expected_actual_url, entry->GetURL());
  EXPECT_EQ(expected_virtual_url, entry->GetVirtualURL());
}

void ExpectCommittedWebUI(content::WebContents* contents,
                          const GURL& expected_actual_url,
                          const GURL& expected_virtual_url) {
  ASSERT_NE(nullptr, contents);
  content::NavigationEntry* entry = LastCommittedEntry(contents);
  ExpectEntryUrls(entry, expected_actual_url, expected_virtual_url);

  content::RenderFrameHost* main_frame = contents->GetPrimaryMainFrame();
  ASSERT_NE(nullptr, main_frame);
  EXPECT_EQ(expected_actual_url, main_frame->GetLastCommittedURL());
  EXPECT_NE("maho", main_frame->GetLastCommittedOrigin().scheme());
  EXPECT_NE("maho",
            main_frame->GetSiteInstance()
                ->GetSecurityPrincipal()
                .GetDeprecatedSiteURL()
                .scheme());

  const int child_id = main_frame->GetProcess()->GetID().GetUnsafeValue();
  EXPECT_TRUE(content::ChildProcessSecurityPolicy::GetInstance()
                  ->HasWebUIBindings(child_id));
}

content::NavigationEntry* NavigateAndGetEntry(Browser* browser,
                                              const GURL& url) {
  if (!ui_test_utils::NavigateToURL(browser, url)) {
    return nullptr;
  }
  return LastCommittedEntry(ActiveWebContents(browser));
}

void WaitForActiveTabLoad(Browser* browser) {
  ASSERT_TRUE(content::WaitForLoadStop(ActiveWebContents(browser)));
}

void ReloadActiveTabAndExpectUrls(Browser* browser,
                                  const GURL& expected_actual_url,
                                  const GURL& expected_virtual_url) {
  content::WebContents* contents = ActiveWebContents(browser);
  content::TestNavigationObserver reload_observer(contents);
  contents->GetController().Reload(content::ReloadType::NORMAL,
                                   /*check_for_repost=*/true);
  reload_observer.Wait();
  ExpectCommittedWebUI(contents, expected_actual_url, expected_virtual_url);
}

void NavigateAwayAndBackToExpectUrls(Browser* browser,
                                     const GURL& expected_actual_url,
                                     const GURL& expected_virtual_url) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser, expected_virtual_url));
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser, GURL("about:blank")));

  content::WebContents* contents = ActiveWebContents(browser);
  ASSERT_TRUE(contents->GetController().CanGoBack());
  content::TestNavigationObserver back_observer(contents);
  contents->GetController().GoBack();
  back_observer.Wait();
  ExpectCommittedWebUI(contents, expected_actual_url, expected_virtual_url);

  ASSERT_TRUE(contents->GetController().CanGoForward());
  content::TestNavigationObserver forward_observer(contents);
  contents->GetController().GoForward();
  forward_observer.Wait();
  content::NavigationEntry* forward_entry = LastCommittedEntry(contents);
  ASSERT_NE(nullptr, forward_entry);
  EXPECT_EQ(GURL("about:blank"), forward_entry->GetURL());
  EXPECT_EQ(GURL("about:blank"), forward_entry->GetVirtualURL());
}

}  // namespace

class MahoUrlSchemeBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    InProcessBrowserTest::SetUpCommandLine(command_line);
    command_line->AppendSwitch(kDisableLoginGateSwitch);
  }
};

class MahoSettingsNavigationAliasTest : public MahoUrlSchemeBrowserTest {};

namespace {
class MahoWelcomeTrustedInputBrowserTest : public MahoUrlSchemeBrowserTest {
 protected:
  void TearDownOnMainThread() override {
    if (auto* contents = MahoWelcomeWindow::GetHostedWebContents()) {
      auto* window = MahoWelcomeWindow::FromWebContents(contents);
      base::test::ScopedRunLoopTimeout timeout(
          FROM_HERE, TestTimeouts::action_max_timeout());
      views::test::WidgetDestroyedWaiter destroyed(window->GetWidget());
      window->Close();
      destroyed.Wait();
    }
    InProcessBrowserTest::TearDownOnMainThread();
  }
};

IN_PROC_BROWSER_TEST_F(MahoWelcomeTrustedInputBrowserTest,
                       StandaloneWelcomeTrustedClickSelectsSignup) {
  base::test::ScopedRunLoopTimeout timeout(
      FROM_HERE, TestTimeouts::action_max_timeout());
  base::test::TestFuture<void> core_ready;
  auto subscription = AddCoreReadyCallback(core_ready.GetRepeatingCallback());
  if (!GetCore()) {
    ASSERT_TRUE(core_ready.Wait());
  }

  // Use the real frameless welcome Widget, not a browser tab navigated to its
  // URL. This is the WebContents returned by the standalone MCP target lookup.
  if (!MahoWelcomeWindow::GetHostedWebContents()) {
    content::TestNavigationObserver navigation{GURL(kMahoWelcomeURL)};
    navigation.StartWatchingNewWebContents();
    MahoWelcomeWindow::Show(browser()->GetProfile());
    navigation.Wait();
    ASSERT_TRUE(navigation.last_navigation_succeeded());
  }
  auto* contents = MahoWelcomeWindow::GetHostedWebContents();
  ASSERT_TRUE(contents);
  auto* window = MahoWelcomeWindow::FromWebContents(contents);
  ASSERT_TRUE(window);
  ASSERT_TRUE(window->GetWidget()->IsVisible());
  EXPECT_EQ(-1, browser()->GetTabStripModel()->GetIndexOfWebContents(contents));
  ASSERT_TRUE(content::WaitForLoadStop(contents));

  // Subscribe to React's initial DOM before checking readiness. No polling or
  // user gesture is introduced by the test's DOM inspection.
  ASSERT_EQ(true, content::EvalJs(contents, R"JS(
    new Promise(resolve => {
      const ready = () => {
        if (document.querySelectorAll('[role="tab"]').length !== 2 ||
            !document.querySelector('input[type="password"]')) return;
        observer.disconnect();
        resolve(true);
      };
      const observer = new MutationObserver(ready);
      observer.observe(document.documentElement, {childList: true, subtree: true});
      ready();
    })
  )JS", content::EXECUTE_SCRIPT_NO_USER_GESTURE));
  content::WaitForCopyableViewInWebContents(contents);

  // Arm the state-change observer and trusted-event listener BEFORE dispatch.
  // Read the same inactive-tab CSS target as the CLI, but never invoke click(),
  // dispatchEvent(), focus(), or a React/store callback from JavaScript.
  const auto target = content::EvalJs(contents, R"JS(
    (() => {
      const tabs = document.querySelectorAll('[role="tab"]');
      const signin = tabs[0];
      const signup = document.querySelector('[role="tab"][data-state="inactive"]');
      const password = document.querySelector('input[type="password"]');
      if (signup !== tabs[1] || signin.getAttribute('aria-selected') !== 'true' ||
          signup.getAttribute('aria-selected') !== 'false' ||
          password.autocomplete !== 'current-password') {
        throw new Error('Welcome must start in fresh-profile sign-in mode');
      }
      const rect = signup.getBoundingClientRect();
      const x = Math.round(rect.x + rect.width / 2);
      const y = Math.round(rect.y + rect.height / 2);
      if (!rect.width || !rect.height ||
          !signup.contains(document.elementFromPoint(x, y))) {
        throw new Error('Inactive auth tab is not hit-testable');
      }
      // Bounded, read-only evidence: document capture also sees misdirected
      // input and clicks from the delegate's additional AX default action.
      const identify = node => node && ({tag: node.nodeName, id: node.id});
      const state = () => ({
        signupSelected: signup.getAttribute('aria-selected'),
        signinSelected: signin.getAttribute('aria-selected'),
        passwordAutocomplete:
            document.querySelector('input[type="password"]')?.autocomplete,
        activeElement: identify(document.activeElement),
        hasFocus: document.hasFocus(), visibility: document.visibilityState,
        signupConnected: signup.isConnected,
        rect: signup.getBoundingClientRect().toJSON()
      });
      const evidence = {
        initial: state(), domCenter: [x, y],
        hitTarget: identify(document.elementFromPoint(x, y)),
        devicePixelRatio, viewport: [innerWidth, innerHeight],
        scroll: [scrollX, scrollY], events: [], changes: [],
        eventCount: 0, changeCount: 0, trustedSignupPointerdown: false,
        resolved: false
      };
      for (const type of ['pointerdown', 'mousedown', 'pointerup', 'mouseup',
                          'click', 'focusin', 'focusout']) {
        document.addEventListener(type, event => {
          ++evidence.eventCount;
          if (evidence.events.length < 32) evidence.events.push({
            type, trusted: event.isTrusted, target: identify(event.target),
            pathTarget: identify(event.composedPath()[0]),
            client: [event.clientX, event.clientY],
            screen: [event.screenX, event.screenY],
            button: event.button, buttons: event.buttons, detail: event.detail,
            pointerType: event.pointerType, state: state()
          });
        }, {capture: true});
      }
      const diagnostics = new MutationObserver(() => {
        ++evidence.changeCount;
        if (evidence.changes.length < 16) evidence.changes.push(state());
      });
      diagnostics.observe(document.documentElement,
                          {attributes: true, childList: true, subtree: true});
      window.welcomeSignupEvidence = () =>
          JSON.stringify({...evidence, current: state()});
      window.welcomeTrustedSignup = new Promise(resolve => {
        let trusted = false;
        const changed = () => {
          if (!trusted || signup.getAttribute('aria-selected') !== 'true' ||
              signin.getAttribute('aria-selected') !== 'false' ||
              document.querySelector('input[type="password"]').autocomplete !==
                  'new-password') return;
          observer.disconnect();
          evidence.resolved = true;
          resolve(true);
        };
        const observer = new MutationObserver(changed);
        observer.observe(document.documentElement,
                         {attributes: true, childList: true, subtree: true});
        signup.addEventListener('pointerdown', event => {
          trusted = event.isTrusted;
          evidence.trustedSignupPointerdown = trusted;
          changed();
        }, {once: true, capture: true});
      });
      return [x, y];
    })()
  )JS", content::EXECUTE_SCRIPT_NO_USER_GESTURE);
  ASSERT_TRUE(target.is_ok()) << target;
  const auto& point = target.ExtractList();
  ASSERT_EQ(2u, point.size());

  // This factory returns the production delegate; only unused Mail callbacks
  // are replaceable. Resolve the hosted target from its real MCP inventory.
  auto delegate = CreateMailDelegateForTesting({}, {});
  int tab_id = 0;
  for (const auto& tab : delegate->GetTabList()) {
    if (tab.url == contents->GetLastCommittedURL().spec()) {
      ASSERT_EQ(0, tab_id) << "Standalone welcome target must be unique";
      ASSERT_TRUE(tab.targetable);
      ASSERT_EQ(-1, tab.tab_strip_index);
      tab_id = tab.id;
    }
  }
  ASSERT_NE(0, tab_id);
  const auto signup_id = content::EvalJs(contents, R"JS(
    document.querySelector('[role="tab"][data-state="inactive"]').id
  )JS", content::EXECUTE_SCRIPT_NO_USER_GESTURE);
  ASSERT_TRUE(signup_id.is_ok()) << signup_id;
  ASSERT_FALSE(signup_id.ExtractString().empty());

  MahoMcpBrowserDelegate::LocatorParams locator;
  locator.tab_id = tab_id;
  locator.css = R"([role="tab"][data-state="inactive"])";
  const auto resolved = delegate->ResolveLocator(tab_id, locator, {});
  ASSERT_TRUE(resolved.success) << resolved.error_code << ": " << resolved.hint;

  // Inspect real AX data only AFTER resolution, without priming the delegate's
  // ref cache or replacing its snapshot. A successful CSS query must not map
  // to the document root (the production resolver's current fallback).
  base::test::TestFuture<ui::AXTreeUpdate> snapshot;
  contents->RequestAXTreeSnapshot(
      base::BindLambdaForTesting(
          [&](ui::AXTreeUpdate& update) { snapshot.SetValue(update); }),
      ui::kAXModeComplete, /*max_nodes=*/12000,
      TestTimeouts::action_timeout(),
      content::WebContents::AXTreeSnapshotPolicy::kAll);
  ASSERT_TRUE(snapshot.Wait());
  EXPECT_NE(snapshot.Get().root_id, resolved.ax_id);
  const ui::AXNodeData* resolved_node = nullptr;
  for (const auto& node : snapshot.Get().nodes) {
    if (node.id == resolved.ax_id) {
      resolved_node = &node;
      break;
    }
  }
  ASSERT_TRUE(resolved_node);
  EXPECT_EQ(ax::mojom::Role::kTab, resolved_node->role);
  EXPECT_EQ(signup_id.ExtractString(), resolved_node->GetStringAttribute(
                                         ax::mojom::StringAttribute::kHtmlId));

  // This independent snapshot is diagnostic, not the delegate's cached tree.
  // GetTreeBounds applies offset-container scroll and transforms, just as the
  // production center calculation does. Log the bounded ancestor chain too.
  ui::AXTree diagnostic_tree;
  if (diagnostic_tree.Unserialize(snapshot.Get())) {
    const ui::AXNode* node = diagnostic_tree.GetFromId(resolved.ax_id);
    if (node) {
      const auto bounds = diagnostic_tree.GetTreeBounds(node, nullptr, false);
      LOG(ERROR) << "Welcome AX snapshot id=" << resolved.ax_id
                 << " tree_bounds=" << bounds.ToString()
                 << " tree_center=" << bounds.CenterPoint().ToString();
      int depth = 0;
      for (; node && depth < 16; node = node->parent(), ++depth) {
        LOG(ERROR) << "Welcome AX ancestor id=" << node->id()
                   << " relative=" << node->data().relative_bounds.ToString()
                   << " scroll_x="
                   << node->GetIntAttribute(ax::mojom::IntAttribute::kScrollX)
                   << " scroll_y="
                   << node->GetIntAttribute(ax::mojom::IntAttribute::kScrollY);
      }
      LOG(ERROR) << "Welcome AX ancestor chain truncated=" << (node != nullptr);
    }
  } else {
    LOG(ERROR) << "Welcome diagnostic AX tree error=" << diagnostic_tree.error();
  }
  if (auto* view = contents->GetRenderWidgetHostView()) {
    LOG(ERROR) << "Welcome view bounds=" << view->GetViewBounds().ToString()
               << " device_scale_factor=" << view->GetDeviceScaleFactor()
               << " focused=" << view->HasFocus()
               << " widget_active=" << window->GetWidget()->IsActive();
  }
  LOG(ERROR) << "Welcome before ClickVerified: "
             << content::EvalJs(contents, "window.welcomeSignupEvidence()",
                                content::EXECUTE_SCRIPT_NO_USER_GESTURE);

  // ClickVerified still exercises the native synthesizer. Its dispatch ACK
  // alone cannot pass: the pre-armed observer requires trusted pointer input
  // on signup AND the real UI transition, rejecting DOM/AX-only fallbacks.
  base::test::TestFuture<InputActionOutcome> clicked;
  delegate->ClickVerified(tab_id, resolved.ax_id, clicked.GetCallback());
  ASSERT_TRUE(clicked.Wait());
  ASSERT_TRUE(clicked.Get().dispatched) << clicked.Get().reason;
  EXPECT_EQ("trusted_input", clicked.Get().method);
  LOG(ERROR) << "Welcome dispatch ACK method=" << clicked.Get().method
             << " reason=" << clicked.Get().reason << " evidence="
             << content::EvalJs(contents, "window.welcomeSignupEvidence()",
                                content::EXECUTE_SCRIPT_NO_USER_GESTURE);
  const auto selected = content::EvalJs(contents, "window.welcomeTrustedSignup",
                                       content::EXECUTE_SCRIPT_NO_USER_GESTURE);
  // Retain EvalJs's bounded promise timeout and the original success condition;
  // a fresh read after the wait reports the failed stage, not only dispatch.
  const auto evidence = content::EvalJs(contents, "window.welcomeSignupEvidence()",
                                       content::EXECUTE_SCRIPT_NO_USER_GESTURE);
  LOG(ERROR) << "Welcome after state wait: " << evidence;
  EXPECT_EQ(true, selected) << "Welcome diagnostic evidence: " << evidence;
}

class MahoWelcomeProviderRestartBrowserTest : public MahoUrlSchemeBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    base::test::TestFuture<void> ready;
    auto subscription = maho::AddCoreReadyCallback(ready.GetRepeatingCallback());
    if (!maho::GetCore()) {
      ASSERT_TRUE(ready.Wait());
    }
    // Core-ready and completion-policy reconciliation run in the same UI task;
    // TestFuture::Wait cannot return until that task has finished.
    profile_ = maho::GetCoreOwnerProfile();
    ASSERT_TRUE(profile_);
  }

  void PersistCompletedWithoutNativeVault(const std::string& provider,
                                         bool passwords_enabled = true) {
    base::test::TestFuture<int> saved;
    maho::PostCoreTaskAndReplyWithResult(
        FROM_HERE,
        base::BindOnce(
            [](std::string provider, bool passwords_enabled) {
              MahoCore* core = maho::GetCore();
              EXPECT_TRUE(core);
              if (!core) {
                return 0;
              }
              const std::string update =
                  "{\"autofill\":{\"passwordsEnabled\":" +
                  std::string(passwords_enabled ? "true" : "false") +
                  ",\"passwordProvider\":\"" + provider + "\"}}";
              maho_core_update_settings(core, update.c_str());
              const auto take_json = [](char* raw) {
                std::unique_ptr<char, decltype(&maho_string_free)> value(
                    raw, &maho_string_free);
                return base::JSONReader::Read(
                    value ? value.get() : "", base::JSON_PARSE_RFC);
              };
              const auto settings = take_json(maho_core_get_settings(core));
              const auto vault = take_json(maho_vault_status_json(core));
              EXPECT_TRUE(settings && settings->is_dict());
              EXPECT_TRUE(vault && vault->is_dict());
              if (!settings || !settings->is_dict() || !vault ||
                  !vault->is_dict()) {
                return 0;
              }
              const std::string* stored_provider =
                  settings->GetDict().FindStringByDottedPath(
                      "autofill.passwordProvider");
              const std::string* lock_state =
                  vault->GetDict().FindStringByDottedPath("data.lockState");
              EXPECT_EQ(provider, stored_provider ? *stored_provider : "");
              const auto stored_enabled =
                  settings->GetDict().FindBoolByDottedPath(
                      "autofill.passwordsEnabled");
              EXPECT_TRUE(stored_enabled.has_value());
              EXPECT_EQ(passwords_enabled,
                        stored_enabled.value_or(!passwords_enabled));
              EXPECT_EQ("uninitialized", lock_state ? *lock_state : "");
              return maho_core_save_state(core);
            },
            provider, passwords_enabled),
        saved.GetCallback());
    ASSERT_EQ(1, saved.Get());
    profile_->GetPrefs()->SetBoolean(maho::welcome::kWelcomeCompleted, true);
    base::test::TestFuture<void> flushed;
    profile_->GetPrefs()->CommitPendingWrite(flushed.GetCallback());
    ASSERT_TRUE(flushed.Wait());
  }

  void ExpectCompletedWithoutNativeVault(const std::string& provider,
                                        bool passwords_enabled = true) {
    base::test::TestFuture<std::string, std::string> restored;
    maho::PostCoreTaskAndReplyWithResult(
        FROM_HERE,
        base::BindOnce([](bool passwords_enabled) {
          const auto take_json = [](char* raw) {
            std::unique_ptr<char, decltype(&maho_string_free)> value(
                raw, &maho_string_free);
            return base::JSONReader::Read(
                value ? value.get() : "", base::JSON_PARSE_RFC);
          };
          const auto settings = take_json(maho_core_get_settings(maho::GetCore()));
          const auto vault = take_json(maho_vault_status_json(maho::GetCore()));
          EXPECT_TRUE(settings && settings->is_dict());
          if (settings && settings->is_dict()) {
            const auto stored_enabled =
                settings->GetDict().FindBoolByDottedPath(
                    "autofill.passwordsEnabled");
            EXPECT_TRUE(stored_enabled.has_value());
            EXPECT_EQ(passwords_enabled,
                      stored_enabled.value_or(!passwords_enabled));
          }
          const std::string* stored_provider =
              settings && settings->is_dict()
                  ? settings->GetDict().FindStringByDottedPath(
                        "autofill.passwordProvider")
                  : nullptr;
          const std::string* lock_state =
              vault && vault->is_dict()
                  ? vault->GetDict().FindStringByDottedPath("data.lockState")
                  : nullptr;
          return std::make_pair(stored_provider ? *stored_provider : "",
                                lock_state ? *lock_state : "");
        }, passwords_enabled),
        base::BindOnce(
            [](base::OnceCallback<void(std::string, std::string)> callback,
               std::pair<std::string, std::string> result) {
              std::move(callback).Run(std::move(result.first),
                                      std::move(result.second));
            },
            restored.GetCallback()));
    ASSERT_TRUE(restored.Wait());
    EXPECT_EQ(provider, restored.Get<0>());
    EXPECT_EQ("uninitialized", restored.Get<1>());
    EXPECT_TRUE(profile_->GetPrefs()->GetBoolean(maho::welcome::kWelcomeCompleted));
    EXPECT_FALSE(profile_->GetPrefs()->GetBoolean(maho::welcome::kLoginGateActive));
    ASSERT_TRUE(browser());
    EXPECT_TRUE(browser()->GetWindow()->IsVisible());
  }

  void ExpectVaultOperation(
      base::OnceCallback<char*(MahoCore*)> operation,
      const std::string& expected_lock_state) {
    base::test::TestFuture<std::string> completed;
    maho::PostCoreTaskAndReplyWithResult(
        FROM_HERE,
        base::BindOnce(
            [](base::OnceCallback<char*(MahoCore*)> operation) {
              std::unique_ptr<char, decltype(&maho_string_free)> response(
                  std::move(operation).Run(maho::GetCore()), &maho_string_free);
              const auto parsed = base::JSONReader::Read(
                  response ? response.get() : "", base::JSON_PARSE_RFC);
              if (!parsed || !parsed->is_dict() ||
                  !parsed->GetDict().FindBool("ok").value_or(false)) {
                return std::string();
              }
              const std::string* state =
                  parsed->GetDict().FindStringByDottedPath("data.lockState");
              return state ? *state : std::string();
            },
            std::move(operation)),
        completed.GetCallback());
    EXPECT_EQ(expected_lock_state, completed.Get());
  }

  raw_ptr<Profile> profile_ = nullptr;
};

IN_PROC_BROWSER_TEST_F(MahoWelcomeProviderRestartBrowserTest,
                       PRE_DisabledProviderKeepsCompletionWithoutNativeVault) {
  // "disabled" is a legacy alias for maho_native, not a persisted provider.
  PersistCompletedWithoutNativeVault("maho_native", /*passwords_enabled=*/false);
}

IN_PROC_BROWSER_TEST_F(MahoWelcomeProviderRestartBrowserTest,
                       DisabledProviderKeepsCompletionWithoutNativeVault) {
  ExpectCompletedWithoutNativeVault("maho_native", /*passwords_enabled=*/false);
}

IN_PROC_BROWSER_TEST_F(MahoWelcomeProviderRestartBrowserTest,
                       PRE_BitwardenProviderKeepsCompletionWithoutNativeVault) {
  PersistCompletedWithoutNativeVault("bitwarden");
}

IN_PROC_BROWSER_TEST_F(MahoWelcomeProviderRestartBrowserTest,
                       BitwardenProviderKeepsCompletionWithoutNativeVault) {
  ExpectCompletedWithoutNativeVault("bitwarden");
}

IN_PROC_BROWSER_TEST_F(MahoWelcomeProviderRestartBrowserTest,
                       PRE_MissingNativeVaultReopensSetup) {
  PersistCompletedWithoutNativeVault("maho_native");
}

IN_PROC_BROWSER_TEST_F(MahoWelcomeProviderRestartBrowserTest,
                       MissingNativeVaultReopensSetup) {
  ExpectVaultOperation(base::BindOnce(&maho_vault_status_json), "uninitialized");
  EXPECT_FALSE(profile_->GetPrefs()->GetBoolean(maho::welcome::kWelcomeCompleted));
  EXPECT_TRUE(profile_->GetPrefs()->GetBoolean(maho::welcome::kLoginGateActive));
  EXPECT_TRUE(maho::MahoWelcomeWindow::GetHostedWebContents());
}

IN_PROC_BROWSER_TEST_F(MahoWelcomeProviderRestartBrowserTest,
                       PRE_LockedNativeVaultKeepsCompletionAndUnlocks) {
  PersistCompletedWithoutNativeVault("maho_native");
  ExpectVaultOperation(
      base::BindOnce([](MahoCore* core) {
        return maho_vault_initialize_json(
            core,
            R"({"masterPassphrase":"onboarding test master passphrase","recoverySecret":"onboarding independent test recovery secret"})");
      }),
      "unlocked");
  ExpectVaultOperation(base::BindOnce(&maho_vault_lock_json), "locked");
}

IN_PROC_BROWSER_TEST_F(MahoWelcomeProviderRestartBrowserTest,
                       LockedNativeVaultKeepsCompletionAndUnlocks) {
  ExpectVaultOperation(base::BindOnce(&maho_vault_status_json), "locked");
  EXPECT_TRUE(profile_->GetPrefs()->GetBoolean(maho::welcome::kWelcomeCompleted));
  EXPECT_FALSE(profile_->GetPrefs()->GetBoolean(maho::welcome::kLoginGateActive));
  EXPECT_FALSE(maho::MahoWelcomeWindow::GetHostedWebContents());
  // The original passphrase must still unlock persisted key material; startup
  // must not replace it merely because the process starts with a locked Vault.
  ExpectVaultOperation(
      base::BindOnce([](MahoCore* core) {
        return maho_vault_unlock_json(
            core,
            R"({"masterPassphrase":"onboarding test master passphrase"})");
      }),
      "unlocked");
}

// Unlike the provider-repair fixture, these tests exercise normal startup's
// persisted auth/completion decision without the test-only login-gate bypass.
class MahoWelcomeIncompleteRestartBrowserTest
    : public MahoWelcomeProviderRestartBrowserTest {
 protected:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    InProcessBrowserTest::SetUpCommandLine(command_line);
    ASSERT_FALSE(command_line->HasSwitch(kDisableLoginGateSwitch));
  }

  void SetUpOnMainThread() override {
    timeout_ = std::make_unique<base::test::ScopedRunLoopTimeout>(
        FROM_HERE, TestTimeouts::action_max_timeout());
    // Subscribe before the inherited core-ready wait can dispatch startup's
    // separately posted welcome creation. Already-created contents are handled
    // by WaitForLoadStop below, including a commit that preceded this setup.
    welcome_navigation_ = std::make_unique<content::TestNavigationObserver>(
        GURL(kMahoWelcomeURL));
    welcome_navigation_->StartWatchingNewWebContents();
    MahoWelcomeProviderRestartBrowserTest::SetUpOnMainThread();
  }

  void TearDownOnMainThread() override {
    base::test::TestFuture<void> startup_tasks_done;
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, startup_tasks_done.GetCallback());
    ASSERT_TRUE(startup_tasks_done.Wait());

    if (auto* contents = MahoWelcomeWindow::GetHostedWebContents()) {
      auto* window = MahoWelcomeWindow::FromWebContents(contents);
      views::test::WidgetDestroyedWaiter destroyed(window->GetWidget());
      window->Close();
      destroyed.Wait();
    }

    base::test::TestFuture<void> cleanup_done;
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, cleanup_done.GetCallback());
    ASSERT_TRUE(cleanup_done.Wait());
    EXPECT_FALSE(MahoWelcomeWindow::GetHostedWebContents());
    welcome_navigation_.reset();
    InProcessBrowserTest::TearDownOnMainThread();
    timeout_.reset();
  }

  void ExpectVisibleWelcome() {
    auto* contents = MahoWelcomeWindow::GetHostedWebContents();
    if (!contents) {
      welcome_navigation_->Wait();
      ASSERT_TRUE(welcome_navigation_->last_navigation_succeeded());
      contents = MahoWelcomeWindow::GetHostedWebContents();
    }
    ASSERT_TRUE(contents);
    ASSERT_EQ(profile_, contents->GetBrowserContext());
    ASSERT_TRUE(content::WaitForLoadStop(contents));
    EXPECT_EQ(GURL(kMahoWelcomeURL), contents->GetLastCommittedURL());
    ASSERT_TRUE(LastCommittedEntry(contents));
    EXPECT_EQ(content::PAGE_TYPE_NORMAL,
              LastCommittedEntry(contents)->GetPageType());
    auto* window = MahoWelcomeWindow::FromWebContents(contents);
    ASSERT_TRUE(window);
    ASSERT_TRUE(window->GetWidget());
    EXPECT_TRUE(window->GetWidget()->IsVisible());
    EXPECT_EQ(content::Visibility::VISIBLE, contents->GetVisibility());
  }

  void PersistAuthenticatedProfile(bool completed) {
    PrefService* prefs = profile_->GetPrefs();
    // Opaque, synthetic values satisfy only local validity: no encryption,
    // real credentials or remote authentication is involved. Use the refresh
    // path because access expiry currently reads GetIfInt on an Int64 pref.
    prefs->SetString(account_prefs::kRelayAccessTokenEncryptedB64,
                     "synthetic-onboarding-access-token");
    prefs->SetInt64(account_prefs::kRelayAccessTokenExpiresAt, 0);
    prefs->SetString(account_prefs::kRelayRefreshTokenEncryptedB64,
                     "synthetic-onboarding-refresh-token");
    // Fixed future Unix timestamp (2033); fail loudly when it expires.
    prefs->SetInt64(account_prefs::kRelayRefreshTokenExpiresAt, 2000000000);
    ASSERT_TRUE(auth::HasValidRelaySession(prefs));
    if (completed) {
      // The external provider prevents native-Vault repair from masking the
      // completed + valid-session no-bypass control. Saves core and prefs.
      ASSERT_NO_FATAL_FAILURE(PersistCompletedWithoutNativeVault("bitwarden"));
      return;
    }

    prefs->ClearPref(welcome::kWelcomeCompleted);
    ASSERT_FALSE(prefs->GetUserPrefValue(welcome::kWelcomeCompleted));
    base::test::TestFuture<int> saved;
    PostCoreTaskAndReplyWithResult(
        FROM_HERE, base::BindOnce([] { return maho_core_save_state(GetCore()); }),
        saved.GetCallback());
    ASSERT_EQ(1, saved.Get());
    base::test::TestFuture<void> flushed;
    prefs->CommitPendingWrite(flushed.GetCallback());
    ASSERT_TRUE(flushed.Wait());
  }

  void ExpectAuthenticatedIncompleteProfile() {
    PrefService* prefs = profile_->GetPrefs();
    ASSERT_FALSE(prefs->GetUserPrefValue(welcome::kWelcomeCompleted));
    ASSERT_FALSE(prefs->GetBoolean(welcome::kWelcomeCompleted));
    ASSERT_TRUE(auth::HasValidRelaySession(prefs));
  }

 private:
  std::unique_ptr<base::test::ScopedRunLoopTimeout> timeout_;
  std::unique_ptr<content::TestNavigationObserver> welcome_navigation_;
};

IN_PROC_BROWSER_TEST_F(MahoWelcomeIncompleteRestartBrowserTest,
                       PRE_AuthenticatedIncompleteProfileReopensWelcome) {
  ASSERT_NO_FATAL_FAILURE(ExpectVisibleWelcome());
  ASSERT_NO_FATAL_FAILURE(PersistAuthenticatedProfile(/*completed=*/false));
}

IN_PROC_BROWSER_TEST_F(MahoWelcomeIncompleteRestartBrowserTest,
                       AuthenticatedIncompleteProfileReopensWelcome) {
  ASSERT_FALSE(base::CommandLine::ForCurrentProcess()->HasSwitch(
      kDisableLoginGateSwitch));
  ASSERT_NO_FATAL_FAILURE(ExpectAuthenticatedIncompleteProfile());
  // Decisive RED on unchanged production: inspect the real stored startup gate
  // before waiting for a welcome that the session-only policy never creates.
  ASSERT_TRUE(profile_->GetPrefs()->GetBoolean(maho::welcome::kLoginGateActive));
  ASSERT_NO_FATAL_FAILURE(ExpectVisibleWelcome());
}

IN_PROC_BROWSER_TEST_F(MahoWelcomeIncompleteRestartBrowserTest,
                       PRE_AuthenticatedCompletedExternalProfileStaysClosed) {
  ASSERT_NO_FATAL_FAILURE(ExpectVisibleWelcome());
  ASSERT_NO_FATAL_FAILURE(PersistAuthenticatedProfile(/*completed=*/true));
}

IN_PROC_BROWSER_TEST_F(MahoWelcomeIncompleteRestartBrowserTest,
                       AuthenticatedCompletedExternalProfileStaysClosed) {
  ASSERT_FALSE(base::CommandLine::ForCurrentProcess()->HasSwitch(
      kDisableLoginGateSwitch));
  ASSERT_TRUE(auth::HasValidRelaySession(profile_->GetPrefs()));
  ASSERT_NO_FATAL_FAILURE(ExpectCompletedWithoutNativeVault("bitwarden"));
  EXPECT_FALSE(MahoWelcomeWindow::GetHostedWebContents());
}

class MahoWelcomeIncompleteBypassRestartBrowserTest
    : public MahoWelcomeIncompleteRestartBrowserTest {
 protected:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    MahoUrlSchemeBrowserTest::SetUpCommandLine(command_line);
    ASSERT_TRUE(command_line->HasSwitch(kDisableLoginGateSwitch));
  }
};

IN_PROC_BROWSER_TEST_F(MahoWelcomeIncompleteBypassRestartBrowserTest,
                       PRE_AuthenticatedIncompleteProfileHonorsBypass) {
  ASSERT_NO_FATAL_FAILURE(PersistAuthenticatedProfile(/*completed=*/false));
}

IN_PROC_BROWSER_TEST_F(MahoWelcomeIncompleteBypassRestartBrowserTest,
                       AuthenticatedIncompleteProfileHonorsBypass) {
  ASSERT_TRUE(base::CommandLine::ForCurrentProcess()->HasSwitch(
      kDisableLoginGateSwitch));
  ASSERT_NO_FATAL_FAILURE(ExpectAuthenticatedIncompleteProfile());
  EXPECT_FALSE(profile_->GetPrefs()->GetBoolean(welcome::kLoginGateActive));
  EXPECT_FALSE(MahoWelcomeWindow::GetHostedWebContents());
  ASSERT_TRUE(browser());
  EXPECT_TRUE(browser()->GetWindow()->IsVisible());
}

// Substitute only the picker. The handler and its asynchronous disk write are
// real, and each selection consumes exactly one outstanding picker request.
class WelcomeSaveDialog : public ui::SelectFileDialog {
 public:
  WelcomeSaveDialog(Listener* listener,
                    std::unique_ptr<ui::SelectFilePolicy> policy)
      : SelectFileDialog(listener, std::move(policy)) {}

  bool IsRunning(gfx::NativeWindow window) const override {
    return pending_ && window == owner_;
  }
  void ListenerDestroyed() override {
    listener_ = nullptr;
    pending_ = false;
    detached = true;
  }
  void Cancel() {
    ASSERT_TRUE(pending_);
    ASSERT_TRUE(listener_);
    pending_ = false;
    listener_->FileSelectionCanceled();
  }
  void Select(const base::FilePath& path) {
    ASSERT_TRUE(pending_);
    ASSERT_TRUE(listener_);
    pending_ = false;
    listener_->FileSelected(ui::SelectedFileInfo(path), 0);
  }

  base::OnceClosure on_open;
  int opens = 0;
  bool detached = false;

 private:
  ~WelcomeSaveDialog() override = default;
  bool HasMultipleFileTypeChoicesImpl() override { return false; }
  void SelectFileImpl(Type type,
                      const std::u16string& title,
                      const base::FilePath& default_path,
                      const FileTypeInfo* file_types,
                      int file_type_index,
                      const base::FilePath::StringType& default_extension,
                      gfx::NativeWindow owning_window,
                      const GURL* caller) override {
    EXPECT_EQ(SELECT_SAVEAS_FILE, type);
    EXPECT_TRUE(owning_window);
    EXPECT_FALSE(pending_);
    owner_ = owning_window;
    pending_ = true;
    ++opens;
    if (on_open) {
      std::move(on_open).Run();
    }
  }

  gfx::NativeWindow owner_;
  bool pending_ = false;
};

class WelcomeSaveDialogFactory : public ui::SelectFileDialogFactory {
 public:
  ui::SelectFileDialog* Create(
      ui::SelectFileDialog::Listener* listener,
      std::unique_ptr<ui::SelectFilePolicy> policy) override {
    dialog = base::MakeRefCounted<WelcomeSaveDialog>(listener, std::move(policy));
    dialog->on_open = std::move(on_open);
    return dialog.get();
  }
  scoped_refptr<WelcomeSaveDialog> dialog;
  base::OnceClosure on_open;
};

class MahoWelcomeSaveBrowserTest : public MahoUrlSchemeBrowserTest {
 protected:
  using SaveResult = base::test::TestFuture<bool, std::string>;

  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    ASSERT_TRUE(directory_.CreateUniqueTempDir());
    content::WebContents* contents =
        browser()->GetTabStripModel()->GetActiveWebContents();
    if (!contents) {
      const GURL url("about:blank");
      base::test::ScopedRunLoopTimeout timeout(
          FROM_HERE, TestTimeouts::action_max_timeout());
      content::TestNavigationObserver navigation(url);
      navigation.StartWatchingNewWebContents();
      contents = chrome::AddSelectedTabWithURL(
          static_cast<Browser*>(browser()), url, ui::PAGE_TRANSITION_AUTO_TOPLEVEL);
      ASSERT_TRUE(contents);
      navigation.Wait();
      ASSERT_TRUE(navigation.last_navigation_succeeded());
    }
    ASSERT_TRUE(contents);
    ASSERT_TRUE(contents->GetTopLevelNativeWindow());
    auto factory = std::make_unique<WelcomeSaveDialogFactory>();
    factory_ = factory.get();
    ui::SelectFileDialog::SetFactory(std::move(factory));
    handler_ = std::make_unique<MahoWelcomePageHandler>(
        mojo::PendingReceiver<maho_welcome::mojom::PageHandler>(),
        mojo::PendingRemote<maho_welcome::mojom::Page>(),
        browser()->GetProfile(), static_cast<Browser*>(browser()), contents);
  }

  void TearDownOnMainThread() override {
    handler_.reset();
    if (factory_) {
      DrainWrites();
      factory_ = nullptr;
      ui::SelectFileDialog::SetFactory(nullptr);
    }
    InProcessBrowserTest::TearDownOnMainThread();
  }

  void DrainWrites() {
    base::ThreadPoolInstance::Get()->FlushForTesting();
    base::RunLoop().RunUntilIdle();
  }

  void Save(SaveResult& result) {
    handler_->SaveSyncKeyBackup(
        kContent, result.GetCallback<bool, const std::string&>());
  }

  void ExpectWritten(const base::FilePath& path) {
    base::ScopedAllowBlockingForTesting allow_blocking;
    std::string actual;
    ASSERT_TRUE(base::ReadFileToString(path, &actual));
    EXPECT_EQ(kContent, actual);
  }

  static constexpr char kContent[] = "onboarding-backup-test-fixture\n";
  base::ScopedTempDir directory_;
  raw_ptr<WelcomeSaveDialogFactory> factory_ = nullptr;
  std::unique_ptr<MahoWelcomePageHandler> handler_;
};

IN_PROC_BROWSER_TEST_F(MahoWelcomeSaveBrowserTest, SynchronousCancelAllowsRetry) {
  ASSERT_TRUE(handler_);
  factory_->on_open = base::BindOnce(
      [](WelcomeSaveDialogFactory* factory) { factory->dialog->Cancel(); },
      base::Unretained(factory_.get()));
  SaveResult canceled;
  Save(canceled);
  ASSERT_TRUE(canceled.IsReady());
  EXPECT_FALSE(canceled.Get<0>());
  SaveResult retry;
  Save(retry);
  ASSERT_EQ(2, factory_->dialog->opens);
  factory_->dialog->Select(directory_.GetPath().AppendASCII("retry.txt"));
  ASSERT_TRUE(retry.Wait());
  EXPECT_TRUE(retry.Get<0>());
  ExpectWritten(directory_.GetPath().AppendASCII("retry.txt"));
}

IN_PROC_BROWSER_TEST_F(MahoWelcomeSaveBrowserTest, DuplicatePendingSaveRejected) {
  ASSERT_TRUE(handler_);
  SaveResult first;
  Save(first);
  SaveResult duplicate;
  Save(duplicate);
  ASSERT_TRUE(duplicate.IsReady());
  EXPECT_FALSE(duplicate.Get<0>());
  EXPECT_FALSE(duplicate.Get<1>().empty());
  EXPECT_FALSE(first.IsReady());
  ASSERT_EQ(1, factory_->dialog->opens);
  factory_->dialog->Cancel();
  ASSERT_TRUE(first.IsReady());
  EXPECT_FALSE(first.Get<0>());
}

IN_PROC_BROWSER_TEST_F(MahoWelcomeSaveBrowserTest, SelectedFileWaitsForWrite) {
  ASSERT_TRUE(handler_);
  SaveResult result;
  Save(result);
  const auto path = directory_.GetPath().AppendASCII("backup.txt");
  factory_->dialog->Select(path);
  // The worker may run immediately, but its UI reply cannot run on this stack.
  EXPECT_FALSE(result.IsReady());
  SaveResult duplicate;
  Save(duplicate);
  ASSERT_TRUE(duplicate.IsReady());
  EXPECT_FALSE(duplicate.Get<0>());
  EXPECT_FALSE(duplicate.Get<1>().empty());
  EXPECT_EQ(1, factory_->dialog->opens);
  EXPECT_FALSE(result.IsReady());
  ASSERT_TRUE(result.Wait());
  EXPECT_TRUE(result.Get<0>());
  EXPECT_TRUE(result.Get<1>().empty());
  ExpectWritten(path);
}

IN_PROC_BROWSER_TEST_F(MahoWelcomeSaveBrowserTest, WriteFailureAllowsRetry) {
  ASSERT_TRUE(handler_);
  SaveResult failure;
  Save(failure);
  // Opening a directory for writing fails independently of user permissions.
  factory_->dialog->Select(directory_.GetPath());
  ASSERT_TRUE(failure.Wait());
  EXPECT_FALSE(failure.Get<0>());
  EXPECT_FALSE(failure.Get<1>().empty());
  SaveResult retry;
  Save(retry);
  const auto path = directory_.GetPath().AppendASCII("retry.txt");
  factory_->dialog->Select(path);
  ASSERT_TRUE(retry.Wait());
  EXPECT_TRUE(retry.Get<0>());
  ExpectWritten(path);
}

IN_PROC_BROWSER_TEST_F(MahoWelcomeSaveBrowserTest,
                       DestructionDuringSelectDetachesListener) {
  ASSERT_TRUE(handler_);
  bool returned_from_destruction = false;
  factory_->on_open = base::BindLambdaForTesting([&] {
    // Release the factory's extra reference: production must keep the dialog
    // alive itself until SelectFile returns from this nested destruction.
    WelcomeSaveDialog* dialog = factory_->dialog.get();
    factory_->dialog.reset();
    handler_.reset();
    EXPECT_TRUE(dialog->detached);
    returned_from_destruction = true;
  });
  SaveResult result;
  Save(result);
  EXPECT_TRUE(returned_from_destruction);
  EXPECT_FALSE(result.IsReady());
}

IN_PROC_BROWSER_TEST_F(MahoWelcomeSaveBrowserTest,
                       DestructionBeforeWriteReplyDropsCallback) {
  ASSERT_TRUE(handler_);
  SaveResult result;
  Save(result);
  const auto path = directory_.GetPath().AppendASCII("detached.txt");
  factory_->dialog->Select(path);
  handler_.reset();
  EXPECT_TRUE(factory_->dialog->detached);
  DrainWrites();
  EXPECT_FALSE(result.IsReady());
  // Weak replies do not cancel a disk operation already handed to the pool.
  ExpectWritten(path);
}
IN_PROC_BROWSER_TEST_F(MahoUrlSchemeBrowserTest,
                       FreshProfileRegistersProviderOAuthSettings) {
  PrefService* prefs = browser()->GetProfile()->GetPrefs();
  for (const char* key : {
           ai_prefs::kOAuthOpenAIClientId,
           ai_prefs::kOAuthOpenAIRefreshEncryptedB64,
           ai_prefs::kOAuthAnthropicClientId,
           ai_prefs::kOAuthAnthropicRefreshEncryptedB64}) {
    ASSERT_TRUE(prefs->FindPreference(key)) << key;
    EXPECT_TRUE(prefs->GetString(key).empty());
  }
  for (const char* key : {ai_prefs::kOAuthOpenAIExpiresAt,
                          ai_prefs::kOAuthAnthropicExpiresAt}) {
    ASSERT_TRUE(prefs->FindPreference(key)) << key;
    EXPECT_EQ(prefs->GetInt64(key), 0);
  }
}

class MahoWelcomeByokBrowserTest : public MahoUrlSchemeBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    ASSERT_FALSE(MahoWelcomeWindow::GetByokSettingsDialogWebContents());
    auto* contents = browser()->GetTabStripModel()->GetActiveWebContents();
    if (!contents) {
      const GURL url("about:blank");
      base::test::ScopedRunLoopTimeout timeout(
          FROM_HERE, TestTimeouts::action_max_timeout());
      content::TestNavigationObserver navigation(url);
      navigation.StartWatchingNewWebContents();
      contents = chrome::AddSelectedTabWithURL(
          static_cast<Browser*>(browser()), url, ui::PAGE_TRANSITION_AUTO_TOPLEVEL);
      ASSERT_TRUE(contents);
      navigation.Wait();
      ASSERT_TRUE(navigation.last_navigation_succeeded());
    }
    ASSERT_TRUE(contents);
    ASSERT_TRUE(contents->GetTopLevelNativeWindow());
    handler_ = std::make_unique<MahoWelcomePageHandler>(
        mojo::PendingReceiver<maho_welcome::mojom::PageHandler>(),
        mojo::PendingRemote<maho_welcome::mojom::Page>(),
        browser()->GetProfile(), static_cast<Browser*>(browser()), contents);
    base::test::TestFuture<bool> configured;
    handler_->GetByokCredentialConfigured(configured.GetCallback());
    ASSERT_TRUE(configured.IsReady());
    ASSERT_FALSE(configured.Get());
  }

  void TearDownOnMainThread() override {
    if (MahoWelcomeWindow::GetByokSettingsDialogWebContents()) {
      CloseDialog();
    }
    handler_.reset();
    InProcessBrowserTest::TearDownOnMainThread();
  }

  void CloseDialog() {
    auto* contents = MahoWelcomeWindow::GetByokSettingsDialogWebContents();
    ASSERT_TRUE(contents);
    auto* widget = views::Widget::GetWidgetForNativeWindow(
        contents->GetTopLevelNativeWindow());
    ASSERT_TRUE(widget);
    base::test::ScopedRunLoopTimeout timeout(
        FROM_HERE, TestTimeouts::action_max_timeout());
    views::test::WidgetDestroyedWaiter destroyed(widget);
    widget->Close();
    destroyed.Wait();
    EXPECT_FALSE(MahoWelcomeWindow::GetByokSettingsDialogWebContents());
  }

  // These callbacks remain valid through failure-path dialog cleanup.
  base::test::TestFuture<bool> first_;
  base::test::TestFuture<bool> second_;
  std::unique_ptr<MahoWelcomePageHandler> handler_;
};

IN_PROC_BROWSER_TEST_F(MahoWelcomeByokBrowserTest,
                       CssProviderRadioResolvesExactControl) {
  base::test::ScopedRunLoopTimeout timeout(
      FROM_HERE, TestTimeouts::action_max_timeout());
  handler_->OpenByokSettingsDialog(first_.GetCallback());
  auto* contents = MahoWelcomeWindow::GetByokSettingsDialogWebContents();
  ASSERT_TRUE(contents);
  ASSERT_TRUE(content::WaitForLoadStop(contents));
  ASSERT_EQ(true, content::EvalJs(contents, R"JS(
    (async () => {
      const store = window.settingsStore;
      await new Promise((resolve, reject) => {
        const timeout = setTimeout(() => {
          unsubscribe();
          reject(new Error('Settings bootstrap did not finish'));
        }, 10000);
        const check = () => {
          if (!store.getSnapshot().browserVersion) return;
          clearTimeout(timeout);
          unsubscribe();
          resolve();
        };
        const unsubscribe = store.subscribe(check);
        check();
      });
      async function selectPane(key) {
        await new Promise((resolve, reject) => {
          const observer = new MutationObserver(check);
          const timeout = setTimeout(() => {
            observer.disconnect();
            reject(new Error('Settings pane did not mount'));
          }, 10000);
          function check() {
            if (!document.querySelector('[data-pane="' + key + '"]')) return;
            observer.disconnect();
            clearTimeout(timeout);
            resolve();
          }
          observer.observe(document.documentElement, {childList: true, subtree: true});
          store.selectPane(key);
          check();
        });
      }
      await selectPane('general');
      await selectPane('maho-ai');
      return await new Promise((resolve, reject) => {
        const selector =
            'label:has(input[name="ai_provider_mode"]):nth-child(2) input';
        const observer = new MutationObserver(check);
        const timeout = setTimeout(() => {
          observer.disconnect();
          reject(new Error('Provider radio did not render'));
        }, 10000);
        function check() {
          if (!document.querySelector(selector)) return;
          observer.disconnect();
          clearTimeout(timeout);
          resolve(true);
        }
        observer.observe(document.documentElement, {childList: true, subtree: true});
        check();
      });
    })()
  )JS", content::EXECUTE_SCRIPT_NO_USER_GESTURE));

  auto delegate = CreateMailDelegateForTesting({}, {});
  int tab_id = 0;
  for (const auto& tab : delegate->GetTabList()) {
    if (tab.url == contents->GetLastCommittedURL().spec()) {
      ASSERT_EQ(0, tab_id);
      ASSERT_EQ(-1, tab.tab_strip_index);
      tab_id = tab.id;
    }
  }
  ASSERT_NE(0, tab_id);
  MahoMcpBrowserDelegate::LocatorParams locator;
  locator.tab_id = tab_id;
  locator.css =
      R"(label:has(input[name="ai_provider_mode"]):nth-child(2) input)";
  const auto resolved = delegate->ResolveLocator(tab_id, locator, {});
  ASSERT_TRUE(resolved.success) << resolved.error_code << ": " << resolved.hint;

  base::test::TestFuture<ui::AXTreeUpdate> snapshot;
  contents->RequestAXTreeSnapshot(
      base::BindLambdaForTesting(
          [&](ui::AXTreeUpdate& update) { snapshot.SetValue(update); }),
      ui::kAXModeComplete, /*max_nodes=*/12000,
      TestTimeouts::action_timeout(),
      content::WebContents::AXTreeSnapshotPolicy::kAll);
  ASSERT_TRUE(snapshot.Wait());
  ASSERT_NE(snapshot.Get().root_id, resolved.ax_id)
      << "A CSS-selected radio must not resolve to the document root";
  const ui::AXNodeData* resolved_node = nullptr;
  for (const auto& node : snapshot.Get().nodes) {
    if (node.id == resolved.ax_id) {
      resolved_node = &node;
      break;
    }
  }
  ASSERT_TRUE(resolved_node);
  EXPECT_EQ(ax::mojom::Role::kRadioButton, resolved_node->role);
  EXPECT_NE(std::string::npos,
            resolved_node->GetStringAttribute(
                ax::mojom::StringAttribute::kName).find("Use my own API key"));
  ASSERT_TRUE(content::ExecJs(contents, R"JS(
    window.byokInputReady = new Promise((resolve, reject) => {
      const observer = new MutationObserver(check);
      const timeout = setTimeout(() => {
        observer.disconnect();
        reject(new Error('BYOK input did not mount'));
      }, 10000);
      function check() {
        if (!document.querySelector('input[aria-label="OpenAI BYOK Key"]')) return;
        observer.disconnect();
        clearTimeout(timeout);
        resolve(true);
      }
      observer.observe(document.documentElement, {childList: true, subtree: true});
      check();
    });
    void 0;
  )JS", content::EXECUTE_SCRIPT_NO_USER_GESTURE));
  base::test::TestFuture<InputActionOutcome> clicked;
  delegate->ClickVerified(tab_id, resolved.ax_id, clicked.GetCallback());
  ASSERT_TRUE(clicked.Wait());
  EXPECT_TRUE(clicked.Get().dispatched) << clicked.Get().reason;
  EXPECT_EQ(true, clicked.Get().verified) << clicked.Get().reason;
  ASSERT_EQ(true, content::EvalJs(contents, "window.byokInputReady",
                                content::EXECUTE_SCRIPT_NO_USER_GESTURE));
  EXPECT_EQ("openai", browser()->GetProfile()->GetPrefs()->GetString(
                          ai_prefs::kProvider));

  locator.css = R"(input[aria-label="OpenAI BYOK Key"])";
  const auto key_input = delegate->ResolveLocator(tab_id, locator, {});
  ASSERT_TRUE(key_input.success) << key_input.error_code << ": " << key_input.hint;
  ASSERT_TRUE(content::ExecJs(contents, R"JS(
    window.byokTyped = new Promise((resolve, reject) => {
      const input = document.querySelector('input[aria-label="OpenAI BYOK Key"]');
      let trustedPointer = false;
      input.addEventListener('pointerdown', event => {
        trustedPointer = event.isTrusted;
      }, {once: true});
      const timeout = setTimeout(() => {
        input.removeEventListener('input', check);
        reject(new Error('BYOK test key was not entered'));
      }, 10000);
      function check() {
        if (input.value !== 'sk-onboarding-fixture') return;
        clearTimeout(timeout);
        input.removeEventListener('input', check);
        resolve(trustedPointer && document.activeElement === input);
      }
      input.addEventListener('input', check);
      check();
    });
    void 0;
  )JS", content::EXECUTE_SCRIPT_NO_USER_GESTURE));
  base::test::TestFuture<InputActionOutcome> typed;
  delegate->TypeVerified(tab_id, key_input.ax_id, "sk-onboarding-fixture",
                         typed.GetCallback());
  ASSERT_TRUE(typed.Wait());
  EXPECT_TRUE(typed.Get().dispatched) << typed.Get().reason;
  EXPECT_EQ(true, typed.Get().verified) << typed.Get().reason;
  EXPECT_EQ(true, content::EvalJs(contents, "window.byokTyped",
                                content::EXECUTE_SCRIPT_NO_USER_GESTURE));
}

IN_PROC_BROWSER_TEST_F(MahoWelcomeByokBrowserTest,
                       CliClosesOwnedSettingsDialog) {
  handler_->OpenByokSettingsDialog(first_.GetCallback());
  auto* contents = MahoWelcomeWindow::GetByokSettingsDialogWebContents();
  ASSERT_TRUE(contents);
  ASSERT_TRUE(content::WaitForLoadStop(contents));
  auto delegate = CreateMailDelegateForTesting({}, {});
  int tab_id = 0;
  for (const auto& tab : delegate->GetTabList()) {
    if (tab.url == contents->GetLastCommittedURL().spec()) {
      ASSERT_EQ(0, tab_id);
      ASSERT_EQ(-1, tab.tab_strip_index);
      tab_id = tab.id;
    }
  }
  ASSERT_NE(0, tab_id);
  auto* widget = views::Widget::GetWidgetForNativeWindow(
      contents->GetTopLevelNativeWindow());
  ASSERT_TRUE(widget);
  base::test::ScopedRunLoopTimeout timeout(
      FROM_HERE, TestTimeouts::action_max_timeout());
  views::test::WidgetDestroyedWaiter destroyed(widget);
  ASSERT_TRUE(delegate->CloseTab(tab_id));
  destroyed.Wait();
  EXPECT_FALSE(MahoWelcomeWindow::GetByokSettingsDialogWebContents());
  ASSERT_TRUE(first_.IsReady());
  EXPECT_FALSE(first_.Get());
  EXPECT_FALSE(delegate->CloseTab(tab_id));
}

IN_PROC_BROWSER_TEST_F(MahoWelcomeByokBrowserTest,
                       CloseWithoutCredentialsReturnsFalse) {
  ASSERT_TRUE(handler_);
  handler_->OpenByokSettingsDialog(first_.GetCallback());
  ASSERT_TRUE(MahoWelcomeWindow::GetByokSettingsDialogWebContents());
  EXPECT_FALSE(first_.IsReady());
  ASSERT_NO_FATAL_FAILURE(CloseDialog());
  ASSERT_TRUE(first_.IsReady());
  EXPECT_FALSE(first_.Get());
}

IN_PROC_BROWSER_TEST_F(MahoWelcomeByokBrowserTest,
                       DuplicateOpenPreservesOriginalDialog) {
  ASSERT_TRUE(handler_);
  handler_->OpenByokSettingsDialog(first_.GetCallback());
  auto* original = MahoWelcomeWindow::GetByokSettingsDialogWebContents();
  ASSERT_TRUE(original);
  EXPECT_FALSE(first_.IsReady());
  handler_->OpenByokSettingsDialog(second_.GetCallback());
  ASSERT_TRUE(second_.IsReady());
  EXPECT_FALSE(second_.Get());
  EXPECT_EQ(original, MahoWelcomeWindow::GetByokSettingsDialogWebContents());
  EXPECT_FALSE(first_.IsReady());
  ASSERT_NO_FATAL_FAILURE(CloseDialog());
  ASSERT_TRUE(first_.IsReady());
  EXPECT_FALSE(first_.Get());
}

IN_PROC_BROWSER_TEST_F(MahoWelcomeByokBrowserTest,
                       ReopensAfterCancellation) {
  ASSERT_TRUE(handler_);
  handler_->OpenByokSettingsDialog(first_.GetCallback());
  ASSERT_TRUE(MahoWelcomeWindow::GetByokSettingsDialogWebContents());
  ASSERT_NO_FATAL_FAILURE(CloseDialog());
  ASSERT_TRUE(first_.IsReady());
  EXPECT_FALSE(first_.Get());
  handler_->OpenByokSettingsDialog(second_.GetCallback());
  ASSERT_TRUE(MahoWelcomeWindow::GetByokSettingsDialogWebContents());
  EXPECT_FALSE(second_.IsReady());
  ASSERT_NO_FATAL_FAILURE(CloseDialog());
  ASSERT_TRUE(second_.IsReady());
  EXPECT_FALSE(second_.Get());
}
}  // namespace

class MahoSettingsPersistenceBrowserTest : public MahoUrlSchemeBrowserTest {};
class MahoAiPopupAliasTest : public MahoUrlSchemeBrowserTest {};
class MahoCommandAliasCompatibilityTest : public MahoUrlSchemeBrowserTest {};

IN_PROC_BROWSER_TEST_F(MahoSettingsNavigationAliasTest,
                       ReusesExistingAliasTab) {
  const int tab_count = browser()->GetTabStripModel()->count();
  const GURL alias_url(kMahoSettingsPublicURL);
  const GURL actual_url = RequiredActualUrlForAlias(alias_url);

  ASSERT_NE(nullptr, NavigateAndGetEntry(static_cast<Browser*>(browser()), alias_url));
  OpenMahoSettingsPane(static_cast<Browser*>(browser()), std::string());
  WaitForActiveTabLoad(static_cast<Browser*>(browser()));

  EXPECT_EQ(tab_count, browser()->GetTabStripModel()->count());
  ExpectCommittedWebUI(ActiveWebContents(static_cast<Browser*>(browser())), actual_url, alias_url);
}

IN_PROC_BROWSER_TEST_F(MahoSettingsNavigationAliasTest,
                       ReusesExistingLegacyTab) {
  const int tab_count = browser()->GetTabStripModel()->count();
  const GURL legacy_url(kMahoSettingsURL);

  ASSERT_NE(nullptr, NavigateAndGetEntry(static_cast<Browser*>(browser()), legacy_url));
  OpenMahoSettingsPane(static_cast<Browser*>(browser()), std::string());
  WaitForActiveTabLoad(static_cast<Browser*>(browser()));

  EXPECT_EQ(tab_count, browser()->GetTabStripModel()->count());
  ExpectCommittedWebUI(ActiveWebContents(static_cast<Browser*>(browser())), legacy_url, legacy_url);
}

IN_PROC_BROWSER_TEST_F(MahoSettingsNavigationAliasTest,
                       RepeatedOpenAndPaneChangeNoDuplicate) {
  OpenMahoSettingsPane(static_cast<Browser*>(browser()), "appearance");
  WaitForActiveTabLoad(static_cast<Browser*>(browser()));

  const int tab_count_after_first_open = browser()->GetTabStripModel()->count();
  const GURL appearance_alias = WithPane(GURL(kMahoSettingsPublicURL),
                                        "appearance");
  const GURL appearance_actual = RequiredActualUrlForAlias(appearance_alias);
  ExpectCommittedWebUI(ActiveWebContents(static_cast<Browser*>(browser())), appearance_actual,
                       appearance_alias);

  OpenMahoSettingsPane(static_cast<Browser*>(browser()), "notifications");
  WaitForActiveTabLoad(static_cast<Browser*>(browser()));

  const GURL notifications_alias =
      WithPane(GURL(kMahoSettingsPublicURL), "notifications");
  const GURL notifications_actual = RequiredActualUrlForAlias(notifications_alias);
  EXPECT_EQ(tab_count_after_first_open, browser()->GetTabStripModel()->count());
  ExpectCommittedWebUI(ActiveWebContents(static_cast<Browser*>(browser())), notifications_actual,
                       notifications_alias);
}

IN_PROC_BROWSER_TEST_F(MahoSettingsPersistenceBrowserTest,
                       PeekTogglesPersistAcrossReload) {
  OpenMahoSettingsPane(static_cast<Browser*>(browser()), "maho-mini");
  WaitForActiveTabLoad(static_cast<Browser*>(browser()));
  content::WebContents* contents = ActiveWebContents(static_cast<Browser*>(browser()));

  const std::string toggle_and_wait_script = R"js(
    (async () => {
      const labels = [
        'Enable Peek',
        'Open links in Peek',
        'Open popups in Peek',
      ];
      const toggles = await new Promise((resolve, reject) => {
        const findToggles = () => labels.map(label =>
            document.querySelector(`button[aria-label="${label}"]`));
        const verify = () => {
          const candidates = findToggles();
          if (candidates.every(toggle => toggle !== null)) {
            observer.disconnect();
            clearTimeout(timeout);
            resolve(candidates);
          }
        };
        const observer = new MutationObserver(verify);
        const timeout = setTimeout(() => {
          observer.disconnect();
          reject(new Error('Peek toggles did not render'));
        }, 5000);
        observer.observe(document.documentElement, {childList: true, subtree: true});
        verify();
      });
      if (toggles.some(toggle => toggle.getAttribute('aria-checked') !== 'true')) {
        return 'unexpected-initial-state';
      }
      for (const toggle of toggles) {
        await new Promise((resolve, reject) => {
          const timeout = setTimeout(() => {
            observer.disconnect();
            reject(new Error(`toggle did not settle: ${toggle.getAttribute('aria-label')}`));
          }, 5000);
          const observer = new MutationObserver(() => {
            if (toggle.getAttribute('aria-checked') === 'false' &&
                !toggle.hasAttribute('disabled')) {
              clearTimeout(timeout);
              observer.disconnect();
              resolve(undefined);
            }
          });
          observer.observe(toggle, {attributes: true});
          toggle.click();
        });
      }
      return 'ok';
    })()
  )js";
  EXPECT_EQ("ok", content::EvalJs(contents, toggle_and_wait_script));

  PrefService* handler_prefs = browser()->GetProfile()->GetPrefs();
  EXPECT_FALSE(
      handler_prefs->GetBoolean(maho::sidebar_prefs::kPeekEnabled));
  EXPECT_FALSE(handler_prefs->GetBoolean(
      maho::sidebar_prefs::kPeekLinkRoutingEnabled));
  EXPECT_FALSE(handler_prefs->GetBoolean(
      maho::sidebar_prefs::kPeekPopupRoutingEnabled));

  content::TestNavigationObserver reload_observer(contents);
  contents->GetController().Reload(content::ReloadType::NORMAL,
                                   /*check_for_repost=*/true);
  reload_observer.Wait();

  EXPECT_EQ(true, content::EvalJs(contents, R"js(
    (async () => {
      const labels = [
        'Enable Peek',
        'Open links in Peek',
        'Open popups in Peek',
      ];
      return new Promise((resolve, reject) => {
        const verify = () => {
          const toggles = labels.map(label =>
              document.querySelector(`button[aria-label="${label}"]`));
          if (toggles.every(toggle =>
              toggle?.getAttribute('aria-checked') === 'false')) {
            observer.disconnect();
            clearTimeout(timeout);
            resolve(true);
          }
        };
        const observer = new MutationObserver(verify);
        const timeout = setTimeout(() => {
          observer.disconnect();
          reject(new Error('reloaded Peek toggles did not restore'));
        }, 5000);
        observer.observe(document.documentElement, {
          attributes: true,
          childList: true,
          subtree: true,
        });
        verify();
      });
    })()
  )js"));
  EXPECT_FALSE(
      handler_prefs->GetBoolean(maho::sidebar_prefs::kPeekEnabled));
  EXPECT_FALSE(handler_prefs->GetBoolean(
      maho::sidebar_prefs::kPeekLinkRoutingEnabled));
  EXPECT_FALSE(handler_prefs->GetBoolean(
      maho::sidebar_prefs::kPeekPopupRoutingEnabled));
}

IN_PROC_BROWSER_TEST_F(MahoAiPopupAliasTest,
                       FloatingUsesAliasVirtualChromeActual) {
  BrowserWindowCreateParams popup_params(Browser::TYPE_POPUP, browser()->GetProfile(),
                                     /*user_gesture=*/true);
  popup_params.is_trusted_source = true;
  popup_params.omit_from_session_restore = true;
  Browser* popup = static_cast<Browser*>(CreateBrowserWindow(std::move(popup_params)));
  ASSERT_NE(nullptr, popup);
  popup->GetWindow()->Show();

  const GURL alias_url(kMahoAIPublicURL);
  const GURL actual_url = RequiredActualUrlForAlias(alias_url);
  content::WebContents* popup_contents = chrome::AddSelectedTabWithURL(
      popup, alias_url, ui::PAGE_TRANSITION_AUTO_BOOKMARK);
  ASSERT_NE(nullptr, popup_contents);
  ASSERT_TRUE(content::WaitForLoadStop(popup_contents));

  EXPECT_TRUE((popup->GetType() == BrowserWindowInterface::TYPE_POPUP));
  ExpectCommittedWebUI(popup_contents, actual_url, alias_url);
}

IN_PROC_BROWSER_TEST_F(MahoCommandAliasCompatibilityTest,
                       RejectsUnknownMahoHost) {
  const GURL unknown_alias("maho://not-listed/");
  GURL mapped_actual("chrome://maho-settings/");

  EXPECT_FALSE(IsValidMahoUrlAlias(unknown_alias));
  EXPECT_FALSE(MapMahoUrlAliasToActualUrl(unknown_alias, &mapped_actual));
  EXPECT_EQ(GURL("chrome://maho-settings/"), mapped_actual);
}

IN_PROC_BROWSER_TEST_F(MahoCommandAliasCompatibilityTest,
                       EmbeddedAiSidePanelKeepsChromeOrigin) {
  const GURL legacy_ai_url(kMahoAIURL);

  ASSERT_NE(nullptr, NavigateAndGetEntry(static_cast<Browser*>(browser()), legacy_ai_url));

  ExpectCommittedWebUI(ActiveWebContents(static_cast<Browser*>(browser())), legacy_ai_url,
                       legacy_ai_url);
  EXPECT_FALSE(ResolveActualUrlToMahoAlias(GURL(kMahoBoostUntrustedURL),
                                           nullptr));
}

IN_PROC_BROWSER_TEST_F(MahoCommandAliasCompatibilityTest,
                       BoostEditorKeepsChromeUntrustedOrigin) {
  const GURL boost_editor_url(kMahoBoostUntrustedURL);

  ASSERT_NE(nullptr, NavigateAndGetEntry(static_cast<Browser*>(browser()), boost_editor_url));

  content::WebContents* contents = ActiveWebContents(static_cast<Browser*>(browser()));
  content::NavigationEntry* entry = LastCommittedEntry(contents);
  ASSERT_NE(nullptr, entry);
  EXPECT_EQ(boost_editor_url, entry->GetURL());
  EXPECT_EQ(boost_editor_url, entry->GetVirtualURL());
  EXPECT_EQ("chrome-untrusted",
            contents->GetPrimaryMainFrame()->GetLastCommittedOrigin().scheme());
  EXPECT_NE("maho", contents->GetPrimaryMainFrame()
                        ->GetSiteInstance()
                        ->GetSecurityPrincipal()
                        .GetDeprecatedSiteURL()
                        .scheme());
  EXPECT_FALSE(IsValidMahoUrlAlias(GURL("maho://boost/")));
}

IN_PROC_BROWSER_TEST_F(MahoUrlSchemeBrowserTest,
                       BrandedSettingsUsesDocumentTitleAndAliasURL) {
  const GURL alias_url(kMahoSettingsPublicURL);
  const GURL actual_url = RequiredActualUrlForAlias(alias_url);

  ASSERT_NE(nullptr, NavigateAndGetEntry(static_cast<Browser*>(browser()), alias_url));

  content::NavigationEntry* entry =
      LastCommittedEntry(ActiveWebContents(static_cast<Browser*>(browser())));
  ExpectCommittedWebUI(ActiveWebContents(static_cast<Browser*>(browser())), actual_url, alias_url);
  EXPECT_EQ(u"Maho Settings", entry->GetTitle());
  EXPECT_EQ(u"Maho Settings", entry->GetTitleForDisplay());
}

IN_PROC_BROWSER_TEST_F(MahoUrlSchemeBrowserTest,
                       BrandedFallbackTitleUsesAliasWhenTitleCleared) {
  const GURL alias_url(kMahoSettingsPublicURL);
  const GURL actual_url = RequiredActualUrlForAlias(alias_url);
  content::NavigationEntry* entry = NavigateAndGetEntry(static_cast<Browser*>(browser()), alias_url);
  ASSERT_NE(nullptr, entry);

  entry->SetTitle(std::u16string());

  ExpectCommittedWebUI(ActiveWebContents(static_cast<Browser*>(browser())), actual_url, alias_url);
  EXPECT_EQ(u"maho://settings/", entry->GetTitleForDisplay());
}

IN_PROC_BROWSER_TEST_F(MahoUrlSchemeBrowserTest,
                       LegacyFallbackTitleUsesChromeWhenTitleCleared) {
  const GURL legacy_url(kMahoSettingsURL);
  content::NavigationEntry* entry = NavigateAndGetEntry(static_cast<Browser*>(browser()), legacy_url);
  ASSERT_NE(nullptr, entry);

  entry->SetTitle(std::u16string());

  ExpectCommittedWebUI(ActiveWebContents(static_cast<Browser*>(browser())), legacy_url, legacy_url);
  EXPECT_EQ(u"chrome://maho-settings/", entry->GetTitleForDisplay());
}

IN_PROC_BROWSER_TEST_F(MahoUrlSchemeBrowserTest,
                       BrandedReloadPreservesActualAndVirtual) {
  const GURL alias_url(kMahoSettingsPublicURL);
  const GURL actual_url = RequiredActualUrlForAlias(alias_url);

  ASSERT_NE(nullptr, NavigateAndGetEntry(static_cast<Browser*>(browser()), alias_url));

  ReloadActiveTabAndExpectUrls(static_cast<Browser*>(browser()), actual_url, alias_url);
}

IN_PROC_BROWSER_TEST_F(MahoUrlSchemeBrowserTest,
                       LegacyReloadPreservesChrome) {
  const GURL legacy_url(kMahoSettingsURL);

  ASSERT_NE(nullptr, NavigateAndGetEntry(static_cast<Browser*>(browser()), legacy_url));

  ReloadActiveTabAndExpectUrls(static_cast<Browser*>(browser()), legacy_url, legacy_url);
}

IN_PROC_BROWSER_TEST_F(MahoUrlSchemeBrowserTest,
                       BrandedBackForwardPreservesActualAndVirtual) {
  const GURL alias_url(kMahoSettingsPublicURL);
  const GURL actual_url = RequiredActualUrlForAlias(alias_url);

  NavigateAwayAndBackToExpectUrls(static_cast<Browser*>(browser()), actual_url, alias_url);
}

IN_PROC_BROWSER_TEST_F(MahoUrlSchemeBrowserTest,
                       LegacyBackForwardPreservesChrome) {
  const GURL legacy_url(kMahoSettingsURL);

  NavigateAwayAndBackToExpectUrls(static_cast<Browser*>(browser()), legacy_url, legacy_url);
}

}  // namespace maho
