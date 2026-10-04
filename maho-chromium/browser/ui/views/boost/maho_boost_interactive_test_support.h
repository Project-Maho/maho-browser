// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_BOOST_MAHO_BOOST_INTERACTIVE_TEST_SUPPORT_H_
#define MAHO_BROWSER_UI_VIEWS_BOOST_MAHO_BOOST_INTERACTIVE_TEST_SUPPORT_H_

#include <optional>
#include <string>
#include <string_view>

#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/callback.h"
#include "base/functional/function_ref.h"
#include "base/time/time.h"
#include "base/values.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "maho/browser/ui/views/boost/maho_boost_window_controller.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/gfx/geometry/size.h"

class Browser;

namespace content {
class WebContents;
}

class MahoBoostInteractiveUiTest : public InProcessBrowserTest {
 public:
  MahoBoostInteractiveUiTest();
  MahoBoostInteractiveUiTest(const MahoBoostInteractiveUiTest&) = delete;
  MahoBoostInteractiveUiTest& operator=(const MahoBoostInteractiveUiTest&) =
      delete;
  ~MahoBoostInteractiveUiTest() override;

  void SetUpCommandLine(base::CommandLine* command_line) override;
  void SetUpOnMainThread() override;
  void TearDownOnMainThread() override;

 protected:
  static constexpr base::TimeDelta kWaitTimeout = base::Seconds(10);

  maho::MahoBoostWindowController& GetController();
  content::WebContents* GetTargetWebContents();
  content::WebContents* GetEditorWebContents();

  void NavigateTo(const std::string& path);
  void ShowBoostEditor();
  void HideBoostEditor();
  bool WaitUntilShowing();
  bool WaitUntilHidden();
  bool WaitUntilDestroyed();
  content::WebContents* OpenEditorAndWait();
  content::WebContents* WaitForEditorLoad();
  bool WaitForReactReady(content::WebContents* editor_web_contents);
  bool SwitchToCodeMode(content::WebContents* editor_web_contents);
  bool WaitForCodeMirrorMount(content::WebContents* editor_web_contents);

  bool WaitForTargetStyle(std::string_view expected_fragment);
  bool WaitForTargetVisibility(std::string_view selector, bool visible);
  bool WaitForCoreBoostCss(std::string_view expected_fragment);

  bool WriteCodeMirrorCss(content::WebContents* editor_web_contents,
                          std::string_view css);
  std::optional<std::string> ReadCodeMirrorCss(
      content::WebContents* editor_web_contents);
  bool FlushCodeMirror(content::WebContents* editor_web_contents);

  std::optional<base::Value> EvaluateInIsolatedWorld(
      content::WebContents* target_web_contents,
      std::string_view script);
  bool CaptureEditorScreenshot(content::WebContents* editor_web_contents,
                               const gfx::Size& output_size,
                               const base::FilePath& output_path);

  const base::FilePath& download_directory() const;

  bool WaitForCondition(std::string_view description,
                        base::FunctionRef<bool()> condition);
  bool WaitForCondition(
      std::string_view description,
      const base::RepeatingCallback<bool()>& condition);

 private:
  base::ScopedTempDir download_directory_;
};

#endif  // MAHO_BROWSER_UI_VIEWS_BOOST_MAHO_BOOST_INTERACTIVE_TEST_SUPPORT_H_
