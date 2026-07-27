// Copyright 2026 Maho Browser. All rights reserved.

#ifndef HAS_OUT_OF_PROC_TEST_RUNNER
#define HAS_OUT_OF_PROC_TEST_RUNNER
#endif

#include "maho/browser/maho_url_scheme.h"

#include <string>

#include "base/check.h"
#include "base/command_line.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/child_process_security_policy.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/site_instance.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/test_navigation_observer.h"
#include "maho/browser/ui/maho_settings_navigation.h"
#include "maho/components/constants/webui_url_constants.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/page_transition_types.h"
#include "url/gurl.h"

namespace maho {
namespace {

constexpr char kDisableLoginGateSwitch[] = "maho-disable-login-gate";

content::WebContents* ActiveWebContents(Browser* browser) {
  return browser->tab_strip_model()->GetActiveWebContents();
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
  EXPECT_NE("maho", main_frame->GetSiteInstance()->GetSiteURL().scheme());

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
class MahoAiPopupAliasTest : public MahoUrlSchemeBrowserTest {};
class MahoCommandAliasCompatibilityTest : public MahoUrlSchemeBrowserTest {};

IN_PROC_BROWSER_TEST_F(MahoSettingsNavigationAliasTest,
                       ReusesExistingAliasTab) {
  const int tab_count = browser()->tab_strip_model()->count();
  const GURL alias_url(kMahoSettingsPublicURL);
  const GURL actual_url = RequiredActualUrlForAlias(alias_url);

  ASSERT_NE(nullptr, NavigateAndGetEntry(browser(), alias_url));
  OpenMahoSettingsPane(browser(), std::string());
  WaitForActiveTabLoad(browser());

  EXPECT_EQ(tab_count, browser()->tab_strip_model()->count());
  ExpectCommittedWebUI(ActiveWebContents(browser()), actual_url, alias_url);
}

IN_PROC_BROWSER_TEST_F(MahoSettingsNavigationAliasTest,
                       ReusesExistingLegacyTab) {
  const int tab_count = browser()->tab_strip_model()->count();
  const GURL legacy_url(kMahoSettingsURL);

  ASSERT_NE(nullptr, NavigateAndGetEntry(browser(), legacy_url));
  OpenMahoSettingsPane(browser(), std::string());
  WaitForActiveTabLoad(browser());

  EXPECT_EQ(tab_count, browser()->tab_strip_model()->count());
  ExpectCommittedWebUI(ActiveWebContents(browser()), legacy_url, legacy_url);
}

IN_PROC_BROWSER_TEST_F(MahoSettingsNavigationAliasTest,
                       RepeatedOpenAndPaneChangeNoDuplicate) {
  OpenMahoSettingsPane(browser(), "appearance");
  WaitForActiveTabLoad(browser());

  const int tab_count_after_first_open = browser()->tab_strip_model()->count();
  const GURL appearance_alias = WithPane(GURL(kMahoSettingsPublicURL),
                                        "appearance");
  const GURL appearance_actual = RequiredActualUrlForAlias(appearance_alias);
  ExpectCommittedWebUI(ActiveWebContents(browser()), appearance_actual,
                       appearance_alias);

  OpenMahoSettingsPane(browser(), "privacy");
  WaitForActiveTabLoad(browser());

  const GURL privacy_alias = WithPane(GURL(kMahoSettingsPublicURL), "privacy");
  const GURL privacy_actual = RequiredActualUrlForAlias(privacy_alias);
  EXPECT_EQ(tab_count_after_first_open, browser()->tab_strip_model()->count());
  ExpectCommittedWebUI(ActiveWebContents(browser()), privacy_actual,
                       privacy_alias);
}

IN_PROC_BROWSER_TEST_F(MahoAiPopupAliasTest,
                       FloatingUsesAliasVirtualChromeActual) {
  Browser::CreateParams popup_params(Browser::TYPE_POPUP, browser()->profile(),
                                     /*user_gesture=*/true);
  popup_params.trusted_source = true;
  popup_params.omit_from_session_restore = true;
  Browser* popup = Browser::Create(popup_params);
  ASSERT_NE(nullptr, popup);
  popup->window()->Show();

  const GURL alias_url(kMahoAIPublicURL);
  const GURL actual_url = RequiredActualUrlForAlias(alias_url);
  content::WebContents* popup_contents = chrome::AddSelectedTabWithURL(
      popup, alias_url, ui::PAGE_TRANSITION_AUTO_BOOKMARK);
  ASSERT_NE(nullptr, popup_contents);
  ASSERT_TRUE(content::WaitForLoadStop(popup_contents));

  EXPECT_TRUE(popup->is_type_popup());
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

  ASSERT_NE(nullptr, NavigateAndGetEntry(browser(), legacy_ai_url));

  ExpectCommittedWebUI(ActiveWebContents(browser()), legacy_ai_url,
                       legacy_ai_url);
  EXPECT_FALSE(ResolveActualUrlToMahoAlias(GURL(kMahoBoostUntrustedURL),
                                           nullptr));
}

IN_PROC_BROWSER_TEST_F(MahoCommandAliasCompatibilityTest,
                       BoostEditorKeepsChromeUntrustedOrigin) {
  const GURL boost_editor_url(kMahoBoostUntrustedURL);

  ASSERT_NE(nullptr, NavigateAndGetEntry(browser(), boost_editor_url));

  content::WebContents* contents = ActiveWebContents(browser());
  content::NavigationEntry* entry = LastCommittedEntry(contents);
  ASSERT_NE(nullptr, entry);
  EXPECT_EQ(boost_editor_url, entry->GetURL());
  EXPECT_EQ(boost_editor_url, entry->GetVirtualURL());
  EXPECT_EQ("chrome-untrusted",
            contents->GetPrimaryMainFrame()->GetLastCommittedOrigin().scheme());
  EXPECT_NE("maho", contents->GetPrimaryMainFrame()
                        ->GetSiteInstance()
                        ->GetSiteURL()
                        .scheme());
  EXPECT_FALSE(IsValidMahoUrlAlias(GURL("maho://boost/")));
}

IN_PROC_BROWSER_TEST_F(MahoUrlSchemeBrowserTest,
                       BrandedSettingsUsesDocumentTitleAndAliasURL) {
  const GURL alias_url(kMahoSettingsPublicURL);
  const GURL actual_url = RequiredActualUrlForAlias(alias_url);

  ASSERT_NE(nullptr, NavigateAndGetEntry(browser(), alias_url));

  content::NavigationEntry* entry =
      LastCommittedEntry(ActiveWebContents(browser()));
  ExpectCommittedWebUI(ActiveWebContents(browser()), actual_url, alias_url);
  EXPECT_EQ(u"Maho Settings", entry->GetTitle());
  EXPECT_EQ(u"Maho Settings", entry->GetTitleForDisplay());
}

IN_PROC_BROWSER_TEST_F(MahoUrlSchemeBrowserTest,
                       BrandedFallbackTitleUsesAliasWhenTitleCleared) {
  const GURL alias_url(kMahoSettingsPublicURL);
  const GURL actual_url = RequiredActualUrlForAlias(alias_url);
  content::NavigationEntry* entry = NavigateAndGetEntry(browser(), alias_url);
  ASSERT_NE(nullptr, entry);

  entry->SetTitle(std::u16string());

  ExpectCommittedWebUI(ActiveWebContents(browser()), actual_url, alias_url);
  EXPECT_EQ(u"maho://settings/", entry->GetTitleForDisplay());
}

IN_PROC_BROWSER_TEST_F(MahoUrlSchemeBrowserTest,
                       LegacyFallbackTitleUsesChromeWhenTitleCleared) {
  const GURL legacy_url(kMahoSettingsURL);
  content::NavigationEntry* entry = NavigateAndGetEntry(browser(), legacy_url);
  ASSERT_NE(nullptr, entry);

  entry->SetTitle(std::u16string());

  ExpectCommittedWebUI(ActiveWebContents(browser()), legacy_url, legacy_url);
  EXPECT_EQ(u"chrome://maho-settings/", entry->GetTitleForDisplay());
}

IN_PROC_BROWSER_TEST_F(MahoUrlSchemeBrowserTest,
                       BrandedReloadPreservesActualAndVirtual) {
  const GURL alias_url(kMahoSettingsPublicURL);
  const GURL actual_url = RequiredActualUrlForAlias(alias_url);

  ASSERT_NE(nullptr, NavigateAndGetEntry(browser(), alias_url));

  ReloadActiveTabAndExpectUrls(browser(), actual_url, alias_url);
}

IN_PROC_BROWSER_TEST_F(MahoUrlSchemeBrowserTest,
                       LegacyReloadPreservesChrome) {
  const GURL legacy_url(kMahoSettingsURL);

  ASSERT_NE(nullptr, NavigateAndGetEntry(browser(), legacy_url));

  ReloadActiveTabAndExpectUrls(browser(), legacy_url, legacy_url);
}

IN_PROC_BROWSER_TEST_F(MahoUrlSchemeBrowserTest,
                       BrandedBackForwardPreservesActualAndVirtual) {
  const GURL alias_url(kMahoSettingsPublicURL);
  const GURL actual_url = RequiredActualUrlForAlias(alias_url);

  NavigateAwayAndBackToExpectUrls(browser(), actual_url, alias_url);
}

IN_PROC_BROWSER_TEST_F(MahoUrlSchemeBrowserTest,
                       LegacyBackForwardPreservesChrome) {
  const GURL legacy_url(kMahoSettingsURL);

  NavigateAwayAndBackToExpectUrls(browser(), legacy_url, legacy_url);
}

}  // namespace maho
