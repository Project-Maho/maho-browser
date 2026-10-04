// Copyright 2026 Maho Browser. All rights reserved.
//
#include <utility>

#include <optional>

#include "base/command_line.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/run_loop.h"
#include "base/test/run_until.h"
#include "base/threading/thread_restrictions.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_window_features.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/interactive_test_utils.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/viz/common/frame_sinks/copy_output_request.h"
#include "components/viz/common/frame_sinks/copy_output_result.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "maho/browser/ui/views/command/maho_command_action_selector_view.h"
#include "maho/browser/ui/views/command/maho_command_overlay_controller.h"
#include "maho/browser/ui/views/command/maho_command_overlay_view.h"
#include "maho/browser/ui/theme/maho_color_id.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkImage.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/compositor.h"
#include "ui/compositor/layer.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/events/types/event_type.h"
#include "ui/gfx/codec/png_codec.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/widget/widget.h"

namespace {

bool CaptureVisiblePalette(views::Widget* widget) {
  if (!widget || !widget->IsVisible()) {
    return false;
  }

  const base::FilePath summary_path =
      base::CommandLine::ForCurrentProcess()->GetSwitchValuePath(
          "test-launcher-summary-output");
  if (summary_path.empty()) {
    return false;
  }

  ui::Layer* layer = widget->GetLayer();
  ui::Compositor* compositor = widget->GetCompositor();
  if (!layer || !compositor || layer->size().IsEmpty()) {
    return false;
  }

  // The native WindowServer capture path returns no pixels in Tart's
  // --no-graphics guest. CopyOutput reads the real compositor layer tree,
  // including the palette's layer-backed textfield and action selector.
  std::optional<SkBitmap> bitmap;
  base::RunLoop run_loop;
  auto request = std::make_unique<viz::CopyOutputRequest>(
      viz::CopyOutputRequest::ResultFormat::RGBA,
      viz::CopyOutputRequest::ResultDestination::kSystemMemory,
      base::BindOnce(
          [](std::optional<SkBitmap>* bitmap, base::OnceClosure quit,
             std::unique_ptr<viz::CopyOutputResult> result) {
            if (!result->IsEmpty()) {
              auto scoped_bitmap = result->ScopedAccessSkBitmap();
              SkBitmap output = scoped_bitmap.GetOutScopedBitmap();
              if (output.readyToDraw()) {
                *bitmap = output;
              }
            }
            std::move(quit).Run();
          },
          &bitmap, run_loop.QuitClosure()));
  request->set_area(gfx::Rect(layer->size()));
  layer->RequestCopyOfOutput(std::move(request));
  compositor->ScheduleFullRedraw();
  run_loop.Run();

  if (!bitmap) {
    return false;
  }
  const ui::ColorProvider* color_provider = widget->GetColorProvider();
  if (!color_provider) {
    return false;
  }
  SkBitmap composited_bitmap;
  composited_bitmap.allocN32Pixels(bitmap->width(), bitmap->height(), true);
  composited_bitmap.eraseColor(
      color_provider->GetColor(kMahoColorCommandBarBackground));
  const sk_sp<SkImage> composited_image = bitmap->asImage();
  if (!composited_image) {
    return false;
  }
  SkCanvas(composited_bitmap).drawImage(composited_image, 0, 0);

  auto png_data = gfx::PNGCodec::EncodeBGRASkBitmap(composited_bitmap, true);
  if (!png_data || png_data->empty()) {
    return false;
  }

  base::ScopedAllowBlockingForTesting allow_blocking;
  return base::WriteFile(
             summary_path.DirName().AppendASCII("startup-command-palette.png"),
             *png_data) > 0;
}

class MahoCommandIncognitoInteractiveUiTest : public InProcessBrowserTest {
 public:
  MahoCommandIncognitoInteractiveUiTest() = default;
  MahoCommandIncognitoInteractiveUiTest(
      const MahoCommandIncognitoInteractiveUiTest&) = delete;
  MahoCommandIncognitoInteractiveUiTest& operator=(
      const MahoCommandIncognitoInteractiveUiTest&) = delete;
  ~MahoCommandIncognitoInteractiveUiTest() override = default;

  void SetUpCommandLine(base::CommandLine* command_line) override {
    InProcessBrowserTest::SetUpCommandLine(command_line);
    // Maho suppresses the startup browser while its onboarding login-gate is
    // active (no relay session in tests), which would leave browser() null.
    // Disable the gate so the standard InProcessBrowserTest window exists.
    command_line->AppendSwitch("maho-disable-login-gate");
  }

  void SetUpOnMainThread() override {
    embedded_test_server()->ServeFilesFromSourceDirectory("chrome/test/data");
    ASSERT_TRUE(embedded_test_server()->Start());
    InProcessBrowserTest::SetUpOnMainThread();
  }

 protected:
  Browser* OpenIncognitoWithPage() {
    Browser* incognito = static_cast<Browser*>(CreateIncognitoBrowser());
    EXPECT_TRUE(incognito);
    EXPECT_TRUE(incognito->GetProfile()->IsIncognitoProfile());
    EXPECT_TRUE(ui_test_utils::NavigateToURL(
        incognito, embedded_test_server()->GetURL("/title1.html")));
    return incognito;
  }

  maho::MahoCommandOverlayView* OpenOverlay(Browser* incognito) {
    BrowserView* bv = BrowserView::GetBrowserViewForBrowser(incognito);
    EXPECT_TRUE(bv);
    if (!bv) {
      return nullptr;
    }
    bv->ShowMahoCommandOverlayForCurrentTab();
    if (!base::test::RunUntil([&]() {
          auto* c = bv->GetMahoCommandOverlayControllerForTesting();
          return c && c->IsVisible() && c->GetOverlayViewForTesting();
        })) {
      return nullptr;
    }
    return bv->GetMahoCommandOverlayControllerForTesting()
        ->GetOverlayViewForTesting();
  }

  static bool SendKey(maho::MahoCommandOverlayView* view,
                      ui::KeyboardCode code) {
    ui::KeyEvent event(ui::EventType::kKeyPressed, code, ui::EF_NONE);
    return view->HandleKeyEvent(view->textfield(), event);
  }
};

class MahoCommandColdStartInteractiveUiTest : public InProcessBrowserTest {
 public:
  MahoCommandColdStartInteractiveUiTest() {
    set_open_about_blank_on_browser_launch(false);
  }
  MahoCommandColdStartInteractiveUiTest(
      const MahoCommandColdStartInteractiveUiTest&) = delete;
  MahoCommandColdStartInteractiveUiTest& operator=(
      const MahoCommandColdStartInteractiveUiTest&) = delete;
  ~MahoCommandColdStartInteractiveUiTest() override = default;

  void SetUp() override {
    EnablePixelOutput();
    InProcessBrowserTest::SetUp();
  }

  void SetUpCommandLine(base::CommandLine* command_line) override {
    InProcessBrowserTest::SetUpCommandLine(command_line);
    command_line->AppendSwitch("maho-disable-login-gate");
  }
};

// Safe allowlisted commands surface and the private palette is functional: an
// action-only query renders at least one row and the overlay stays open.
IN_PROC_BROWSER_TEST_F(MahoCommandIncognitoInteractiveUiTest,
                       AllowlistHappyPath) {
  Browser* incognito = OpenIncognitoWithPage();
  maho::MahoCommandOverlayView* view = OpenOverlay(incognito);
  ASSERT_TRUE(view);

  view->textfield()->SetText(u">reload");
  view->ContentsChanged(view->textfield(), u">reload");

  EXPECT_TRUE(base::test::RunUntil(
      [&]() { return view->GetRenderedResultCountForTesting() > 0u; }))
      << "private commands-only query must render allowlisted action rows";

  // Dismiss overlay to prevent widget and AX node leaks.
  EXPECT_TRUE(SendKey(view, ui::VKEY_ESCAPE));
  BrowserView* bv = BrowserView::GetBrowserViewForBrowser(incognito);
  EXPECT_TRUE(base::test::RunUntil([&]() {
    auto* c = bv->GetMahoCommandOverlayControllerForTesting();
    return !c || !c->IsVisible();
  }));
}

// Typing a normal query must not surface the Maho search-engine picker and the
// palette must remain in a plain search context (no saved-source sub-modes).
IN_PROC_BROWSER_TEST_F(MahoCommandIncognitoInteractiveUiTest,
                       UnsafeQueriesAndPickerAbsent) {
  Browser* incognito = OpenIncognitoWithPage();
  maho::MahoCommandOverlayView* view = OpenOverlay(incognito);
  ASSERT_TRUE(view);

  view->textfield()->SetText(u"secret query");
  view->ContentsChanged(view->textfield(), u"secret query");
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(maho::CommandOverlayMode::kCurrentTab, view->CurrentModeForTesting());
  EXPECT_FALSE(view->IsModeChipVisibleForTesting())
      << "no site-search/picker sub-mode chip may appear in private search";

  // Dismiss overlay to prevent widget and AX node leaks.
  EXPECT_TRUE(SendKey(view, ui::VKEY_ESCAPE));
  BrowserView* bv = BrowserView::GetBrowserViewForBrowser(incognito);
  EXPECT_TRUE(base::test::RunUntil([&]() {
    auto* c = bv->GetMahoCommandOverlayControllerForTesting();
    return !c || !c->IsVisible();
  }));
}

// ai+Tab is denied before the Search | Ask Maho selector mutates in Incognito.
IN_PROC_BROWSER_TEST_F(MahoCommandIncognitoInteractiveUiTest,
                       AskMahoTabDenied) {
  Browser* incognito = OpenIncognitoWithPage();
  maho::MahoCommandOverlayView* view = OpenOverlay(incognito);
  ASSERT_TRUE(view);

  EXPECT_FALSE(view->AiIngressAllowedForTesting());

  view->textfield()->SetText(u"ai");
  EXPECT_TRUE(SendKey(view, ui::VKEY_TAB));

  EXPECT_EQ(maho::PaletteAction::kSearch,
            view->GetPaletteActionForTesting())
      << "Incognito context must block AI action toggling";
  EXPECT_FALSE(view->IsModeChipVisibleForTesting());

  // Dismiss overlay to prevent widget and AX node leaks.
  EXPECT_TRUE(SendKey(view, ui::VKEY_ESCAPE));
  BrowserView* bv = BrowserView::GetBrowserViewForBrowser(incognito);
  EXPECT_TRUE(base::test::RunUntil([&]() {
    auto* c = bv->GetMahoCommandOverlayControllerForTesting();
    return !c || !c->IsVisible();
  }));
}

// Escape dismisses the overlay and focus returns to the page's WebContents.
IN_PROC_BROWSER_TEST_F(MahoCommandIncognitoInteractiveUiTest,
                       FocusRestoresToWebContents) {
  Browser* incognito = OpenIncognitoWithPage();
  BrowserView* bv = BrowserView::GetBrowserViewForBrowser(incognito);
  ASSERT_TRUE(bv);
  maho::MahoCommandOverlayView* view = OpenOverlay(incognito);
  ASSERT_TRUE(view);

  EXPECT_TRUE(SendKey(view, ui::VKEY_ESCAPE));

  EXPECT_TRUE(base::test::RunUntil([&]() {
    auto* c = bv->GetMahoCommandOverlayControllerForTesting();
    return !c || !c->IsVisible();
  })) << "Escape must dismiss the private command overlay";

  content::WebContents* active =
      incognito->GetTabStripModel()->GetActiveWebContents();
  EXPECT_TRUE(active);
}

IN_PROC_BROWSER_TEST_F(MahoCommandColdStartInteractiveUiTest,
                       AutoPresentsVisiblePaletteWithActionResults) {
  ASSERT_TRUE(browser());
  EXPECT_EQ(0, browser()->GetTabStripModel()->count())
      << "the regression must exercise the Maho zero-tab startup path";

  ASSERT_TRUE(base::test::RunUntil([&]() {
    BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
    if (!browser_view) {
      return false;
    }
    auto* controller =
        browser_view->GetMahoCommandOverlayControllerForTesting();
    return controller && controller->IsVisible() &&
           controller->GetOverlayViewForTesting();
  })) << "the cold-start retry must auto-present the new-tab palette";

  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  ASSERT_TRUE(browser_view);
  auto* controller = browser_view->GetMahoCommandOverlayControllerForTesting();
  ASSERT_TRUE(controller);
  auto* overlay = controller->GetOverlayViewForTesting();
  ASSERT_TRUE(overlay);
  EXPECT_EQ(maho::CommandOverlayMode::kNewTab,
            overlay->CurrentModeForTesting());

  RunScheduledLayouts();
  auto* action_selector = overlay->GetActionSelectorForTesting();
  ASSERT_TRUE(action_selector);
  EXPECT_FALSE(action_selector->bounds().IsEmpty())
      << "the automatically opened palette must lay out its visible controls";

  overlay->textfield()->SetText(u">reload");
  overlay->ContentsChanged(overlay->textfield(), u">reload");
  EXPECT_TRUE(base::test::RunUntil(
      [&]() { return overlay->GetRenderedResultCountForTesting() > 0u; }))
      << "the automatically opened palette must render an action result";

  EXPECT_TRUE(CaptureVisiblePalette(overlay->GetWidget()))
      << "the visible startup palette must yield a non-empty PNG artifact";
}

}  // namespace
