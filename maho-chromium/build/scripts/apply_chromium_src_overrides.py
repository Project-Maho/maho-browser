#!/usr/bin/env python3

from __future__ import annotations

import argparse
import sys
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class Replacement:
    old: str
    new: str
    description: str
    # When True, treat the replacement as already applied if neither old nor
    # new is present in the file.  Use this for removals whose target content
    # may have been cleaned up independently of this script.
    idempotent: bool = False


REPLACEMENTS: dict[str, list[Replacement]] = {
    # ── app-Info.plist ────────────────────────────────────────────────────────
    "chrome/app/app-Info.plist": [
        Replacement(
            old=(
                "\t<key>LSFileQuarantineEnabled</key>\n"
                "\t<true/>\n"
            ),
            new=(
                "\t<key>LSFileQuarantineEnabled</key>\n"
                "\t<false/>\n"
            ),
            description=(
                "Disable LSFileQuarantineEnabled so files written by Maho "
                "and its child processes (notably Bun-extracted native dylibs "
                "from `opencode acp`) are not quarantined and dlopen-blocked"
            ),
        ),
    ],
    # ── location_bar_view.cc ──────────────────────────────────────────────────
    "chrome/browser/ui/views/location_bar/location_bar_view.cc": [
        Replacement(
            old=(
                "    utility_bubble_coordinator_ =\n"
                "        std::make_unique<maho::MahoLocationBarUtilityBubbleCoordinator>();\n"
                "    utility_icon_view_ = AddChildView(\n"
            ),
            new=(
                "    utility_bubble_coordinator_ =\n"
                "        std::make_unique<maho::MahoLocationBarUtilityBubbleCoordinator>();\n"
                "    utility_bubble_coordinator_->AttachToBrowser(browser_);\n"
                "    utility_icon_view_ = AddChildView(\n"
            ),
            description="Call AttachToBrowser(browser_) after coordinator construction to activate tab-switch dismissal",
        ),
    ],
    # ── tab_helpers.cc ────────────────────────────────────────────────────────
    "chrome/browser/ui/tab_helpers.cc": [
        Replacement(
            old=(
                '#include "maho/browser/net/maho_boost_injection_handler.h"\n'
                '#include "maho/browser/net/maho_cosmetic_filters_handler.h"\n'
            ),
            new=(
                '#include "maho/browser/maho_tab_id_helper.h"\n'
                '#include "maho/browser/net/maho_boost_injection_handler.h"\n'
                '#include "maho/browser/net/maho_cosmetic_filters_handler.h"\n'
            ),
            description="Add MahoTabIdHelper include to tab_helpers.cc",
        ),
        Replacement(
            old=(
                "  MahoBoostInjectionHandler::CreateForWebContents(web_contents);\n"
                "  MahoCosmeticFiltersHandler::CreateForWebContents(web_contents);\n"
            ),
            new=(
                "  MahoBoostInjectionHandler::CreateForWebContents(web_contents);\n"
                "  MahoCosmeticFiltersHandler::CreateForWebContents(web_contents);\n"
                "  MahoTabIdHelper::CreateForWebContents(web_contents);\n"
            ),
            description="Attach MahoTabIdHelper in TabHelpers::AttachTabHelpers()",
        ),
    ],
    # ── browser_live_tab_context.cc ───────────────────────────────────────────
    "chrome/browser/ui/browser_live_tab_context.cc": [
        Replacement(
            old='#include "chrome/browser/glic/glic_tab_restore_helper.h"\n',
            new=(
                '#include "chrome/browser/glic/glic_tab_restore_helper.h"\n'
                '#include "maho/browser/maho_tab_id_helper.h"\n'
            ),
            description="Add MahoTabIdHelper include to browser_live_tab_context.cc",
        ),
        Replacement(
            old=(
                "  glic::PopulateGlicExtraData(tab_strip_model_->GetTabAtIndex(index),\n"
                "                              &extra_data);\n"
                "\n"
                "  return extra_data;\n"
            ),
            new=(
                "  glic::PopulateGlicExtraData(tab_strip_model_->GetTabAtIndex(index),\n"
                "                              &extra_data);\n"
                "\n"
                "  content::WebContents* contents =\n"
                "      tab_strip_model_->GetWebContentsAt(index);\n"
                "  if (contents) {\n"
                "    auto* tab_id_helper = MahoTabIdHelper::FromWebContents(contents);\n"
                "    if (tab_id_helper) {\n"
                "      extra_data[MahoTabIdHelper::kExtraDataKey] =\n"
                "          tab_id_helper->stable_tab_id();\n"
                "    }\n"
                "  }\n"
                "\n"
                "  return extra_data;\n"
            ),
            description="Write stable tab ID into session extra_data in GetExtraDataForTab()",
        ),
    ],
    # ── browser_tabrestore.cc ─────────────────────────────────────────────────
    "chrome/browser/ui/browser_tabrestore.cc": [
        Replacement(
            old='#include "chrome/browser/glic/glic_tab_restore_helper.h"\n',
            new=(
                '#include "chrome/browser/glic/glic_tab_restore_helper.h"\n'
                '#include "maho/browser/maho_tab_id_helper.h"\n'
            ),
            description="Add MahoTabIdHelper include to browser_tabrestore.cc",
        ),
        Replacement(
            old="  glic::RestoreGlicStateFromExtraData(web_contents.get(), extra_data);\n",
            new=(
                "  glic::RestoreGlicStateFromExtraData(web_contents.get(), extra_data);\n"
                "\n"
                "  {\n"
                "    auto it = extra_data.find(MahoTabIdHelper::kExtraDataKey);\n"
                "    if (it != extra_data.end() && !it->second.empty()) {\n"
                "      MahoTabIdHelper::CreateForWebContents(web_contents.get());\n"
                "      auto* helper =\n"
                "          MahoTabIdHelper::FromWebContents(web_contents.get());\n"
                "      if (helper) {\n"
                "        helper->SetRestoredTabId(it->second);\n"
                "      }\n"
                "    }\n"
                "  }\n"
            ),
            description="Restore stable tab ID from session extra_data in CreateRestoredTab()",
        ),
    ],
    # ── side_panel_helper.cc ──────────────────────────────────────────────────
    "chrome/browser/ui/views/side_panel/side_panel_helper.cc": [
        Replacement(
            old=(
                "  if (webui_browser::IsWebUIBrowserEnabled()) {\n"
                "    // TODO(webium): Consider supporting additional side panels beyond reading\n"
                "    // list and bookmarks.\n"
                "    return;\n"
                "  }\n"
            ),
            new=(
                "  if (webui_browser::IsWebUIBrowserEnabled()) {\n"
                "    // TODO(webium): Consider supporting additional side panels beyond reading\n"
                "    // list and bookmarks.\n"
                "    maho::RegisterMahoAiSidePanel(browser, window_registry);\n"
                "    return;\n"
                "  }\n"
            ),
            description="Register Maho AI side panel in Webium path",
        ),
    ],
    "chrome/browser/ui/views/side_panel/BUILD.gn": [
        Replacement(
            old=(
                "  allow_circular_includes_from = [\n"
                "    \"//chrome/browser/ui/views/toolbar\",\n"
                "    \"//chrome/browser/ui/customize_chrome\",\n"
                "    \"//chrome/browser/ui/side_panel:side_panel\",\n"
                "    \"//chrome/browser/glic\",\n"
                "  ]\n"
                "}\n"
            ),
            new=(
                "  allow_circular_includes_from = [\n"
                "    \"//chrome/browser/ui/views/toolbar\",\n"
                "    \"//chrome/browser/ui/customize_chrome\",\n"
                "    \"//chrome/browser/ui/side_panel:side_panel\",\n"
                "    \"//chrome/browser/glic\",\n"
                "  ]\n"
                "}\n"
                "\n"
                "source_set(\"interactive_tests\") {\n"
                "  testonly = true\n"
                "\n"
                "  sources = [ \"//maho/browser/ui/views/side_panel/maho_ai_side_panel_interactive_uitest.cc\" ]\n"
                "\n"
                "  defines = [ \"HAS_OUT_OF_PROC_TEST_RUNNER\" ]\n"
                "\n"
                "  deps = [\n"
                "    \":side_panel\",\n"
                "    \"//base\",\n"
                "    \"//base/test:test_support\",\n"
                "    \"//chrome/browser:browser\",\n"
                "    \"//chrome/browser/ui:ui\",\n"
                "    \"//chrome/test:test_support\",\n"
                "    \"//chrome/test:test_support_ui\",\n"
                "    \"//content/public/browser:browser\",\n"
                "    \"//content/test:test_support\",\n"
                "    \"//net:test_support\",\n"
                "    \"//testing/gtest:gtest\",\n"
                "    \"//ui/views:views\",\n"
                "    \"//ui/views:test_support\",\n"
                "  ]\n"
                "}\n"
            ),
            description="Add Maho AI side-panel interactive test target to Chromium side-panel BUILD.gn",
        ),
    ],
    "chrome/browser/ui/webui_browser/webui_browser_ui.cc": [
        Replacement(
            old=(
                "    case SidePanelEntryId::kBookmarks:\n"
                "      return l10n_util::GetStringUTF8(IDS_BOOKMARK_MANAGER_TITLE);\n"
                "    default:\n"
            ),
            new=(
                "    case SidePanelEntryId::kBookmarks:\n"
                "      return l10n_util::GetStringUTF8(IDS_BOOKMARK_MANAGER_TITLE);\n"
                "    case SidePanelEntryId::kMahoAiPanel:\n"
                "      return \"Maho AI\";\n"
                "    default:\n"
            ),
            description="Add Webium side-panel title for Maho AI",
        ),
    ],
    "chrome/browser/ui/BUILD.gn": [
        Replacement(
            old=(
                '      "//maho/browser/ui/views/sidebar/maho_sidebar_state_models.cc",\n'
                '      "//maho/browser/ui/views/sidebar/maho_sidebar_state_models.h",\n'
                '      "//maho/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc",\n'
            ),
            new=(
                '      "//maho/browser/ui/tab_preview/maho_tab_preview_controller.cc",\n'
                '      "//maho/browser/ui/tab_preview/maho_tab_preview_controller.h",\n'
                '      "//maho/browser/ui/views/sidebar/maho_sidebar_state_models.cc",\n'
                '      "//maho/browser/ui/views/sidebar/maho_sidebar_state_models.h",\n'
                '      "//maho/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc",\n'
            ),
            description="Include Maho tab preview controller sources in Chromium browser UI build",
        ),
    ],
    # ── browser_view.cc ───────────────────────────────────────────────────────
    "chrome/browser/ui/views/frame/browser_view.cc": [
        Replacement(
            old=(
                "    LOG(WARNING) << \"[maho-cmdl-trace] BrowserView::ShowMahoCommandOverlayForCurrentTab\"\n"
                "                 << \" url=\" << url.spec()\n"
                "                 << \" scheme=\" << url.scheme()\n"
                "                 << \" is_http_https=\" << (url.SchemeIsHTTPOrHTTPS() ? \"true\" : \"false\")\n"
                "                 << \" current_url_passed=\" << (current_url.empty() ? \"(empty)\" : current_url);\n"
                "  } else {\n"
                "    LOG(WARNING) << \"[maho-cmdl-trace] BrowserView::ShowMahoCommandOverlayForCurrentTab\"\n"
                "                 << \" no_active_contents current_url_passed=(empty)\";\n"
                "  }\n"
                "  LOG(WARNING) << \"[maho-cmdl-trace] BrowserView::ShowMahoCommandOverlayForCurrentTab\"\n"
                "               << \" calling Show mode=kCurrentTab select_initial_text=true\";\n"
            ),
            new=(
                "  } else {\n"
                "  }\n"
            ),
            description="Remove [maho-cmdl-trace] LOG(WARNING) calls from ShowMahoCommandOverlayForCurrentTab",
            idempotent=True,
        ),
    ],
    # ── chrome_content_browser_client.cc ─────────────────────────────────────
    "chrome/browser/chrome_content_browser_client.cc": [
        Replacement(
            old=(
                '#include "chrome/browser/search/search.h"\n'
            ),
            new=(
                '#include "chrome/browser/search/search.h"\n'
                '#include "content/public/browser/browser_url_handler.h"\n'
            ),
            description=(
                "Insert BrowserURLHandler include into "
                "chrome_content_browser_client.cc"
            ),
        ),
        Replacement(
            old=(
                "bool g_disable_advanced_protection_caching_for_tests = false;\n"
            ),
            new=(
                "bool g_disable_advanced_protection_caching_for_tests = false;\n"
                "\n"
                "// Rewrites stray chrome://newtab navigations (those that reach\n"
                "// BrowserURLHandlerCreated after policy and extra_parts handlers have\n"
                "// already had a chance to intercept) to about:blank so that Maho never\n"
                "// commits a chrome://newtab WebUI frame.\n"
                "bool MahoRewriteNewTabToBlank(GURL* url,\n"
                "                              content::BrowserContext* /*context*/) {\n"
                "  if (url->SchemeIs(\"chrome\") && url->host() == \"newtab\") {\n"
                "    *url = GURL(\"about:blank\");\n"
                "    return true;\n"
                "  }\n"
                "  return false;\n"
                "}\n"
            ),
            description=(
                "Insert MahoRewriteNewTabToBlank helper into the existing anonymous "
                "namespace of chrome_content_browser_client.cc"
            ),
        ),
        Replacement(
            old=(
                "  handler->AddHandlerPair(&HandleChromeAboutAndChromeSyncRewrite,\n"
                "                          BrowserURLHandler::null_handler());\n"
            ),
            new=(
                "  // Maho: rewrite stray chrome://newtab to about:blank before the\n"
                "  // upstream NTP rewriter runs, so Maho never commits a NTP WebUI frame.\n"
                "  handler->AddHandlerPair(&MahoRewriteNewTabToBlank,\n"
                "                          BrowserURLHandler::null_handler());\n"
                "  handler->AddHandlerPair(&HandleChromeAboutAndChromeSyncRewrite,\n"
                "                          BrowserURLHandler::null_handler());\n"
            ),
            description=(
                "Register MahoRewriteNewTabToBlank handler before "
                "HandleChromeAboutAndChromeSyncRewrite in BrowserURLHandlerCreated()"
            ),
        ),
    ],
    "chrome/test/BUILD.gn": [
        Replacement(
            old=(
                '      "//maho/browser/ui/views/spaces_overlay:interactive_tests",\n'
            ),
            new=(
                '      "//maho/browser/ui/views/spaces_overlay:interactive_tests",\n'
                '      "//chrome/browser/ui/views/side_panel:interactive_tests",\n'
            ),
            description="Include Maho AI side-panel interactive tests in Chromium interactive_ui_tests deps",
        ),
        Replacement(
            old=(
                '      "//chrome/browser/ui/views/side_panel:interactive_tests",\n'
            ),
            new=(
                '      "//chrome/browser/ui/views/side_panel:interactive_tests",\n'
                '      "//maho/browser/ui/link_preview:interactive_tests",\n'
            ),
            description="Include Maho link-preview interactive tests in Chromium interactive_ui_tests deps",
        ),
        Replacement(
            old=(
                '      "//maho/browser/ui/link_preview:interactive_tests",\n'
            ),
            new=(
                '      "//maho/browser/ui/link_preview:interactive_tests",\n'
                '      "//maho/browser/ui/views/sidebar:interactive_tests",\n'
            ),
            description="Include Maho sidebar interactive tests in Chromium interactive_ui_tests deps",
        ),
    ],
}


def repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def default_chromium_src() -> Path:
    return repo_root().parent / "chromium" / "src"


def apply_replacements(target_file: Path, replacements: list[Replacement]) -> tuple[bool, list[str]]:
    text = target_file.read_text()
    changed = False
    applied: list[str] = []

    for replacement in replacements:
        if replacement.new in text:
            applied.append(f"already: {replacement.description}")
            continue

        if replacement.old not in text:
            if replacement.idempotent:
                applied.append(f"already: {replacement.description}")
                continue
            raise RuntimeError(
                f"Missing expected anchor for {replacement.description} in {target_file}"
            )

        text = text.replace(replacement.old, replacement.new, 1)
        changed = True
        applied.append(f"patched: {replacement.description}")

    if changed:
        target_file.write_text(text)

    return changed, applied


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Apply tracked Maho chromium_src overrides into the local Chromium checkout."
    )
    parser.add_argument(
        "--chromium-src",
        type=Path,
        default=default_chromium_src(),
        help="Path to chromium/src checkout (default: workspace chromium/src)",
    )
    args = parser.parse_args()

    chromium_src = args.chromium_src.resolve()
    if not chromium_src.exists():
        print(f"chromium/src not found: {chromium_src}", file=sys.stderr)
        return 1

    any_changed = False
    for relative_path, replacements in REPLACEMENTS.items():
        target_file = chromium_src / relative_path
        if not target_file.exists():
            print(f"target file not found: {target_file}", file=sys.stderr)
            return 1

        changed, applied = apply_replacements(target_file, replacements)
        any_changed = any_changed or changed
        print(relative_path)
        for line in applied:
            print(f"  - {line}")

    print("result: changed" if any_changed else "result: no-op")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
