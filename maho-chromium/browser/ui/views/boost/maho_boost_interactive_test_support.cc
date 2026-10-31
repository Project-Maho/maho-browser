// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/boost/maho_boost_interactive_test_support.h"

#include <utility>

#include "base/cancelable_callback.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/run_loop.h"
#include "base/strings/stringprintf.h"
#include "base/threading/thread_restrictions.h"
#include "base/timer/timer.h"
#include "chrome/browser/ui/browser.h"

// These desktop fixtures use CreateBrowserWindow, whose non-Android factory
// creates Browser instances. Borrowing the concrete pointer does not own it.
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/pref_names.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test_utils.h"
#include "components/viz/common/frame_sinks/copy_output_result.h"
#include "components/prefs/pref_service.h"
#include "maho/browser/maho_core_holder.h"
#include "maho/browser/net/maho_boost_injection_handler.h"
#include "maho/browser/ui/views/boost/maho_boost_window_controller.h"
#include "maho/third_party/maho/maho_bridge.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/accessibility/platform/ax_platform_node_base.h"
#include "ui/gfx/codec/png_codec.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace {

constexpr base::TimeDelta kPollInterval = base::Milliseconds(25);

struct WaitForConditionState {
  base::FunctionRef<bool()> condition;
  base::RunLoop* run_loop;
  bool* satisfied;
};

void PollWaitForCondition(WaitForConditionState* state) {
  if (state->condition()) {
    *state->satisfied = true;
    state->run_loop->Quit();
  }
}

bool WaitForConditionImpl(std::string_view description,
                          base::FunctionRef<bool()> condition,
                          base::TimeDelta timeout_duration) {
  if (condition()) {
    return true;
  }
  bool satisfied = false;
  base::RunLoop run_loop;
  base::OneShotTimer timeout;
  base::RepeatingTimer poll;
  WaitForConditionState state{condition, &run_loop, &satisfied};
  timeout.Start(FROM_HERE, timeout_duration, run_loop.QuitClosure());
  poll.Start(FROM_HERE, kPollInterval,
             base::BindRepeating(&PollWaitForCondition,
                                 base::Unretained(&state)));
  run_loop.Run();
  poll.Stop();
  timeout.Stop();
  if (!satisfied) {
    ADD_FAILURE() << "Timed out after " << timeout_duration << " waiting for "
                  << description;
  }
  return satisfied;
}

}  // namespace

MahoBoostInteractiveUiTest::MahoBoostInteractiveUiTest() = default;
MahoBoostInteractiveUiTest::~MahoBoostInteractiveUiTest() = default;

void MahoBoostInteractiveUiTest::SetUpCommandLine(
    base::CommandLine* command_line) {
  InProcessBrowserTest::SetUpCommandLine(command_line);
  command_line->AppendSwitch("maho-disable-login-gate");
}

void MahoBoostInteractiveUiTest::SetUpOnMainThread() {
  host_resolver()->AddRule("*", "127.0.0.1");
  embedded_test_server()->ServeFilesFromSourceDirectory("chrome/test/data");
  ASSERT_TRUE(embedded_test_server()->Start());
  InProcessBrowserTest::SetUpOnMainThread();

  ASSERT_TRUE(browser());
  ASSERT_TRUE(download_directory_.CreateUniqueTempDir(
      FILE_PATH_LITERAL("maho-boost-download-")));
  browser()->GetProfile()->GetPrefs()->SetFilePath(
      prefs::kDownloadDefaultDirectory, download_directory_.GetPath());
  if (!browser()->GetTabStripModel()->GetActiveWebContents()) {
    chrome::AddTabAt(browser(), GURL("about:blank"), /*index=*/-1,
                     /*foreground=*/true);
  }
}

void MahoBoostInteractiveUiTest::TearDownOnMainThread() {
  maho::MahoBoostWindowController::HideActive();
  if (browser()) {
    EXPECT_TRUE(WaitForCondition(
        "Boost widget host-close cleanup during teardown",
        [this]() { return !GetController().HasWidget(); }));
  }
  if (browser()) {
    browser()->GetTabStripModel()->CloseAllTabs();
  }
  base::RunLoop().RunUntilIdle();
  {
    base::ScopedAllowBlockingForTesting allow_blocking;
    EXPECT_TRUE(download_directory_.Delete())
        << "Boost test download directory was not removed: "
        << download_directory_.GetPath();
  }
  // Do not reset the AX platform node counter here: the harness reads that same
  // counter after teardown to detect leaked Views/Widgets, so clearing it merely
  // hid real leaks and made failures intermittent.
  InProcessBrowserTest::TearDownOnMainThread();
}

maho::MahoBoostWindowController& MahoBoostInteractiveUiTest::GetController() {
  return maho::MahoBoostWindowController::GetForBrowser(static_cast<Browser*>(browser()),
                                                         browser()->GetProfile());
}

content::WebContents* MahoBoostInteractiveUiTest::GetTargetWebContents() {
  return browser()->GetTabStripModel()->GetActiveWebContents();
}

content::WebContents* MahoBoostInteractiveUiTest::GetEditorWebContents() {
  return GetController().GetEditorWebContentsForTesting();
}

void MahoBoostInteractiveUiTest::NavigateTo(const std::string& path) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(),
                                            embedded_test_server()->GetURL(path)));
}

void MahoBoostInteractiveUiTest::ShowBoostEditor() {
  ASSERT_TRUE(GetTargetWebContents());
  GetController().ShowForActiveDomain(GetTargetWebContents());
}

void MahoBoostInteractiveUiTest::HideBoostEditor() {
  GetController().Hide();
}

bool MahoBoostInteractiveUiTest::WaitUntilShowing() {
  return WaitForCondition("Boost widget to become visible",
                          [this]() { return GetController().IsShowing(); });
}

bool MahoBoostInteractiveUiTest::WaitUntilHidden() {
  return WaitForCondition("Boost widget to close",
                          [this]() { return !GetController().IsShowing(); });
}

bool MahoBoostInteractiveUiTest::WaitUntilDestroyed() {
  return WaitForCondition("Boost host cleanup and widget destruction",
                          [this]() { return !GetController().HasWidget(); });
}

content::WebContents* MahoBoostInteractiveUiTest::OpenEditorAndWait() {
  ShowBoostEditor();
  if (!WaitUntilShowing()) {
    return nullptr;
  }
  return WaitForEditorLoad();
}

content::WebContents* MahoBoostInteractiveUiTest::WaitForEditorLoad() {
  content::WebContents* editor_web_contents = nullptr;
  if (!WaitForCondition("Boost editor WebContents and document load",
                        [this, &editor_web_contents]() {
                           editor_web_contents = GetEditorWebContents();
                           return editor_web_contents &&
                                  editor_web_contents
                                      ->IsDocumentOnLoadCompletedInPrimaryMainFrame();
                         })) {
    return nullptr;
  }
  if (!WaitForReactReady(editor_web_contents)) {
    return nullptr;
  }
  return editor_web_contents;
}

bool MahoBoostInteractiveUiTest::WaitForReactReady(
    content::WebContents* editor_web_contents) {
  if (!editor_web_contents) {
    ADD_FAILURE() << "Cannot wait for React: Boost editor WebContents is null";
    return false;
  }
  return WaitForCondition("React Boost controls to mount in chrome-untrusted "
                           "maho-boost-editor",
                           [editor_web_contents]() {
                             const auto result = content::EvalJs(
                                 editor_web_contents,
                                 "Boolean(document.getElementById('zen-boost-code'))");
                             return result.is_ok() && result.ExtractBool();
                           });
}

bool MahoBoostInteractiveUiTest::SwitchToCodeMode(
    content::WebContents* editor_web_contents) {
  if (!editor_web_contents ||
      !content::ExecJs(editor_web_contents,
                       "document.getElementById('zen-boost-code')?.click()")) {
    ADD_FAILURE() << "Could not click #zen-boost-code in the Boost WebUI";
    return false;
  }
  return WaitForCondition("Boost widget to resize to Code mode",
                          [this]() {
                            const auto* widget = GetController().GetWidgetForTesting();
                            return widget &&
                                   widget->GetWindowBoundsInScreen().width() ==
                                       maho::kCodeModeWidth;
                          });
}

bool MahoBoostInteractiveUiTest::WaitForCodeMirrorMount(
    content::WebContents* editor_web_contents) {
  if (!editor_web_contents) {
    ADD_FAILURE() << "Cannot wait for CodeMirror: Boost editor WebContents is null";
    return false;
  }
  return WaitForCondition("CodeMirror test seam to mount",
                          [editor_web_contents]() {
                            const auto result = content::EvalJs(
                                editor_web_contents,
                                "Boolean(window.__mahoBoostTest?.mounted())");
                            return result.is_ok() && result.ExtractBool();
                          });
}

bool MahoBoostInteractiveUiTest::WaitForTargetStyle(
    std::string_view expected_fragment) {
  content::WebContents* target_web_contents = GetTargetWebContents();
  if (!target_web_contents) {
    ADD_FAILURE() << "Cannot wait for target style: active tab is null";
    return false;
  }
  return WaitForCondition(
      base::StringPrintf("#maho-boost-style to contain '%s'",
                         expected_fragment.data()),
      [target_web_contents, expected_fragment]() {
        const auto result = content::EvalJs(
            target_web_contents,
            "document.getElementById('maho-boost-style')?.textContent ?? ''");
        return result.is_ok() &&
               result.ExtractString().find(expected_fragment) != std::string::npos;
      });
}

bool MahoBoostInteractiveUiTest::WaitForTargetVisibility(
    std::string_view selector,
    bool visible) {
  content::WebContents* target_web_contents = GetTargetWebContents();
  if (!target_web_contents) {
    ADD_FAILURE() << "Cannot wait for target visibility: active tab is null";
    return false;
  }
  const std::string script = content::JsReplace(
      "(() => { const element = document.querySelector($1); return Boolean(element) && "
      "getComputedStyle(element).display !== 'none' && "
      "getComputedStyle(element).visibility !== 'hidden'; })()",
      std::string(selector));
  return WaitForCondition(
      base::StringPrintf("target selector '%s' visibility to become %s",
                         selector.data(), visible ? "visible" : "hidden"),
      [target_web_contents, script, visible]() {
        const auto result = content::EvalJs(target_web_contents, script);
        return result.is_ok() && result.ExtractBool() == visible;
      });
}

bool MahoBoostInteractiveUiTest::WaitForCoreBoostCss(
    std::string_view expected_fragment) {
  content::WebContents* target_web_contents = GetTargetWebContents();
  if (!target_web_contents) {
    ADD_FAILURE() << "Cannot poll core Boost state: active tab is null";
    return false;
  }
  const std::string expected(expected_fragment);
  return WaitForCondition(
      base::StringPrintf("core Boost CSS for '%s'", expected.c_str()),
      [target_web_contents, expected]() {
        MahoCore* core = maho::GetCore();
        if (!core) {
          return false;
        }
        return maho::core::GetBoostInjectionCss(
                   core, target_web_contents->GetLastCommittedURL().spec().c_str())
                   .find(expected) != std::string::npos;
      });
}

bool MahoBoostInteractiveUiTest::WriteCodeMirrorCss(
    content::WebContents* editor_web_contents,
    std::string_view css) {
  if (!editor_web_contents) {
    ADD_FAILURE() << "Cannot write CodeMirror CSS: Boost editor WebContents is null";
    return false;
  }
  const auto result = content::EvalJs(
      editor_web_contents,
      content::JsReplace("window.__mahoBoostTest?.writeCss($1) ?? false",
                         std::string(css)));
  if (!result.is_ok() || !result.ExtractBool()) {
    ADD_FAILURE() << "CodeMirror test seam rejected writeCss: "
                  << (result.is_ok() ? "returned false" : result.ExtractError());
    return false;
  }
  return true;
}

std::optional<std::string> MahoBoostInteractiveUiTest::ReadCodeMirrorCss(
    content::WebContents* editor_web_contents) {
  if (!editor_web_contents) {
    ADD_FAILURE() << "Cannot read CodeMirror CSS: Boost editor WebContents is null";
    return std::nullopt;
  }
  const auto result = content::EvalJs(
      editor_web_contents, "window.__mahoBoostTest?.readCss() ?? ''");
  if (!result.is_ok()) {
    ADD_FAILURE() << "CodeMirror test seam did not return CSS: "
                  << result.ExtractError();
    return std::nullopt;
  }
  return result.ExtractString();
}

bool MahoBoostInteractiveUiTest::FlushCodeMirror(
    content::WebContents* editor_web_contents) {
  if (!editor_web_contents) {
    ADD_FAILURE() << "Cannot flush CodeMirror: Boost editor WebContents is null";
    return false;
  }
  const auto result = content::EvalJs(
      editor_web_contents,
      "window.__mahoBoostTest?.flush().then(snapshot => snapshot.mounted) ?? false");
  if (!result.is_ok() || !result.ExtractBool()) {
    ADD_FAILURE() << "CodeMirror test seam did not flush: "
                  << (result.is_ok() ? "returned false" : result.ExtractError());
    return false;
  }
  return true;
}

std::optional<base::Value> MahoBoostInteractiveUiTest::EvaluateInIsolatedWorld(
    content::WebContents* target_web_contents,
    std::string_view script) {
  if (!target_web_contents) {
    ADD_FAILURE() << "Cannot evaluate isolated world: target WebContents is null";
    return std::nullopt;
  }
  MahoBoostInjectionHandler::CreateForWebContents(target_web_contents);
  auto* handler = MahoBoostInjectionHandler::FromWebContents(target_web_contents);
  if (!handler) {
    ADD_FAILURE() << "Boost injection handler was not created for target tab";
    return std::nullopt;
  }

  std::optional<base::Value> result;
  base::RunLoop run_loop;
  base::OneShotTimer timeout;
  timeout.Start(FROM_HERE, kWaitTimeout, run_loop.QuitClosure());
  base::CancelableOnceCallback<void(base::Value)> evaluation_callback(
      base::BindOnce(
          [](std::optional<base::Value>* output, base::RunLoop* loop,
             base::Value value) {
            *output = std::move(value);
            loop->Quit();
          },
          &result, &run_loop));
  handler->ExecuteInIsolatedWorldForTesting(
      std::string(script), evaluation_callback.callback());
  run_loop.Run();
  timeout.Stop();
  evaluation_callback.Cancel();
  if (!result) {
    ADD_FAILURE() << "Timed out after " << kWaitTimeout
                  << " waiting for Boost isolated-world evaluation";
  }
  return result;
}

bool MahoBoostInteractiveUiTest::CaptureEditorScreenshot(
    content::WebContents* editor_web_contents,
    const gfx::Size& output_size,
    const base::FilePath& output_path) {
  if (!editor_web_contents || output_size.IsEmpty()) {
    ADD_FAILURE() << "Screenshot requires a live editor WebContents and size";
    return false;
  }
  content::RenderWidgetHostView* view = editor_web_contents->GetRenderWidgetHostView();
  if (!view) {
    ADD_FAILURE() << "Boost editor has no RenderWidgetHostView for screenshot";
    return false;
  }

  std::optional<SkBitmap> bitmap;
  base::RunLoop run_loop;
  view->CopyFromSurface(
      gfx::Rect(), output_size, kWaitTimeout,
      base::BindOnce(
          [](std::optional<SkBitmap>* output, base::RunLoop* loop,
             const content::CopyFromSurfaceResult& result) {
            if (result.has_value()) {
              *output = result.value().bitmap;
            }
            loop->Quit();
          },
          &bitmap, &run_loop));
  run_loop.Run();
  if (!bitmap || bitmap->drawsNothing() ||
      bitmap->width() != output_size.width() ||
      bitmap->height() != output_size.height()) {
    ADD_FAILURE() << "Boost screenshot was missing or had unexpected dimensions";
    return false;
  }
  const auto png = gfx::PNGCodec::EncodeBGRASkBitmap(
      *bitmap, /*discard_transparency=*/false);
  if (!png) {
    ADD_FAILURE() << "Could not write Boost screenshot: " << output_path;
    return false;
  }
  {
    base::ScopedAllowBlockingForTesting allow_blocking;
    if (!base::WriteFile(output_path, *png)) {
      ADD_FAILURE() << "Could not write Boost screenshot: " << output_path;
      return false;
    }
  }
  return true;
}

const base::FilePath& MahoBoostInteractiveUiTest::download_directory() const {
  return download_directory_.GetPath();
}

bool MahoBoostInteractiveUiTest::WaitForCondition(
    std::string_view description,
    base::FunctionRef<bool()> condition) {
  return WaitForConditionImpl(description, condition, kWaitTimeout);
}

bool MahoBoostInteractiveUiTest::WaitForCondition(
    std::string_view description,
    const base::RepeatingCallback<bool()>& condition) {
  return WaitForConditionImpl(
      description, [&condition]() { return condition.Run(); }, kWaitTimeout);
}
