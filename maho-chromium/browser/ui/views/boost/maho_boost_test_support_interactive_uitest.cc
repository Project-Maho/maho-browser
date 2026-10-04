// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/boost/maho_boost_interactive_test_support.h"

#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "testing/gtest/include/gtest/gtest-spi.h"

namespace {

class MahoBoostTestSupportTest : public MahoBoostInteractiveUiTest {};

IN_PROC_BROWSER_TEST_F(MahoBoostTestSupportTest, SmokeEachFixtureSeam) {
  NavigateTo("/title1.html");

  content::WebContents* editor_web_contents = OpenEditorAndWait();
  ASSERT_TRUE(editor_web_contents);
  ASSERT_TRUE(SwitchToCodeMode(editor_web_contents));
  ASSERT_TRUE(WaitForCodeMirrorMount(editor_web_contents));

  constexpr char kCss[] = "body { outline: 1px solid rgb(1, 2, 3); }";
  ASSERT_TRUE(WriteCodeMirrorCss(editor_web_contents, kCss));
  ASSERT_TRUE(FlushCodeMirror(editor_web_contents));
  const std::optional<std::string> css =
      ReadCodeMirrorCss(editor_web_contents);
  ASSERT_TRUE(css);
  EXPECT_EQ(kCss, *css);
  ASSERT_TRUE(WaitForCoreBoostCss("outline"));
  ASSERT_TRUE(WaitForTargetStyle("outline"));

  const std::optional<base::Value> isolated_world_result =
      EvaluateInIsolatedWorld(GetTargetWebContents(), "document.title");
  ASSERT_TRUE(isolated_world_result);
  EXPECT_TRUE(isolated_world_result->is_string());

  EXPECT_TRUE(CaptureEditorScreenshot(
      editor_web_contents, gfx::Size(maho::kCodeModeWidth, maho::kCodeModeHeight),
      download_directory().AppendASCII("boost-code.png")));
}

IN_PROC_BROWSER_TEST_F(
    MahoBoostTestSupportTest,
    MissingControlStaleWebContentsWorldAndTimeoutAreActionable) {
  testing::TestPartResultArray failures;
  {
    testing::ScopedFakeTestPartResultReporter reporter(
        testing::ScopedFakeTestPartResultReporter::INTERCEPT_ONLY_CURRENT_THREAD,
        &failures);
    EXPECT_FALSE(WaitForReactReady(nullptr));
    EXPECT_FALSE(WaitForCodeMirrorMount(nullptr));
    EXPECT_FALSE(WriteCodeMirrorCss(nullptr, "body {}"));
    EXPECT_FALSE(FlushCodeMirror(nullptr));
    EXPECT_FALSE(EvaluateInIsolatedWorld(nullptr, "document.title"));
    EXPECT_FALSE(CaptureEditorScreenshot(
        nullptr, gfx::Size(maho::kBoostModeWidth, maho::kBoostModeHeight),
        download_directory().AppendASCII("missing-editor.png")));
  }
  EXPECT_EQ(6, failures.size());

  NavigateTo("/title1.html");
  content::WebContents* target_web_contents = GetTargetWebContents();
  ASSERT_TRUE(target_web_contents);
  const std::optional<base::Value> result = EvaluateInIsolatedWorld(
      target_web_contents, "document.documentElement.dataset.fixture = 'ready'");
  ASSERT_TRUE(result);
}

}  // namespace
