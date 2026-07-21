#!/usr/bin/env python3

from __future__ import annotations

import argparse
import sys
from dataclasses import dataclass
from pathlib import Path


# ---------------------------------------------------------------------------
# Platform detection
# ---------------------------------------------------------------------------

def host_platform() -> str:
    """Return the canonical host platform string: 'mac', 'win', or 'linux'."""
    if sys.platform == "darwin":
        return "mac"
    if sys.platform == "win32":
        return "win"
    return "linux"


@dataclass(frozen=True)
class Replacement:
    old: str
    new: str
    description: str
    # When True, treat the replacement as already applied if neither old nor
    # new is present in the file.  Use this for removals whose target content
    # may have been cleaned up independently of this script.
    idempotent: bool = False
    # If set, skip this replacement (treat as already applied) when this
    # string is found anywhere in the file.  Use this when the injected
    # content may already be present at a different location than the anchor
    # used in `old`, which would otherwise cause a duplicate insertion.
    guard: str | None = None


# ---------------------------------------------------------------------------
# Per-file platform filter
# ---------------------------------------------------------------------------

# Maps a relative path (matching a key in REPLACEMENTS) to the set of
# platforms on which that file exists in the Chromium checkout.  A path
# absent from this map is treated as enabled on all platforms.
FILE_PLATFORMS: dict[str, frozenset[str]] = {
    "chrome/app/app-Info.plist": frozenset({"mac"}),
    "chrome/browser/app_controller_mac.mm": frozenset({"mac"}),
    "chrome/browser/global_keyboard_shortcuts_mac.mm": frozenset({"mac"}),
    "chrome/browser/global_keyboard_shortcuts_mac_unittest.mm": frozenset({"mac"}),
    "chrome/browser/ui/views/frame/browser_caption_button_container_win.cc": frozenset({"win"}),
    "chrome/browser/ui/views/frame/browser_caption_button_container_win.h": frozenset({"win"}),
    "chrome/browser/ui/views/frame/browser_frame_view_win.cc": frozenset({"win"}),
    "chrome/browser/device_reauth/chrome_device_authenticator_factory.cc": frozenset({"linux"}),
    "chrome/browser/device_reauth/BUILD.gn": frozenset({"linux"}),
}


REPLACEMENTS: dict[str, list[Replacement]] = {
    "chrome/browser/ui/views/status_bubble_views.h": [
        Replacement(
            old="static const int kTotalVerticalPadding = 7;",
            new="static const int kTotalVerticalPadding = 14;",
            description="Maho: widen vertical padding between the status-bar text and the bubble edge",
            guard="static const int kTotalVerticalPadding = 14;",
        ),
    ],
    # ── status_bubble_views.cc: enlarge the bottom-left hover-URL text and
    #    inset the bubble with a small margin ─────────────────────────────────
    "chrome/browser/ui/views/status_bubble_views.cc": [
        Replacement(
            old="constexpr int kBubbleCornerRadius = 4;",
            new="constexpr int kBubbleCornerRadius = 8;",
            description="Maho: round the hover-URL status bubble corners more",
            guard="constexpr int kBubbleCornerRadius = 8;",
        ),
        Replacement(
            old=(
                "    rad[index] = {radius, radius};\n"
                "  };\n"
                "\n"
                "  // Top Edges"
            ),
            new=(
                "    rad[index] = {radius, radius};\n"
                "  };\n"
                "\n"
                "  // Maho: the bubble is inset from the window edge, so round all\n"
                "  // four corners for a floating pill look.\n"
                "  round_corner(gfx::RRectF::Corner::kUpperLeft);\n"
                "  round_corner(gfx::RRectF::Corner::kUpperRight);\n"
                "  round_corner(gfx::RRectF::Corner::kLowerLeft);\n"
                "  round_corner(gfx::RRectF::Corner::kLowerRight);\n"
                "\n"
                "  // Top Edges"
            ),
            description="Maho: round all four corners of the inset hover-URL bubble",
            guard="round all\n  // four corners for a floating pill look.",
        ),
        Replacement(
            old='#include "base/memory/raw_ptr.h"\n',
            new=(
                '#include "base/memory/raw_ptr.h"\n'
                '#include "base/no_destructor.h"\n'
            ),
            description="Maho: include base/no_destructor.h for the enlarged status-bar font",
            guard='#include "base/no_destructor.h"',
        ),
        Replacement(
            old=(
                "const gfx::FontList& GetFont() {\n"
                "  return views::TypographyProvider::Get().GetFont(views::style::CONTEXT_LABEL,\n"
                "                                                  views::style::STYLE_PRIMARY);\n"
                "}"
            ),
            new=(
                "const gfx::FontList& GetFont() {\n"
                "  // Maho: enlarge the hover-URL status-bar text by 2pt.\n"
                "  static const base::NoDestructor<gfx::FontList> kMahoStatusFont(\n"
                "      views::TypographyProvider::Get()\n"
                "          .GetFont(views::style::CONTEXT_LABEL, views::style::STYLE_PRIMARY)\n"
                "          .DeriveWithSizeDelta(3));\n"
                "  return *kMahoStatusFont;\n"
                "}"
            ),
            description="Maho: enlarge the status-bar (hover URL) text by 3pt",
            guard="kMahoStatusFont",
        ),
        Replacement(
            old=(
                "  std::unique_ptr<views::Label> text = std::make_unique<views::Label>();\n"
            ),
            new=(
                "  std::unique_ptr<views::Label> text = std::make_unique<views::Label>();\n"
                "  text->SetFontList(GetFont());\n"
            ),
            description="Maho: apply the enlarged font to the status-bar label so the rendered text grows",
            guard="text->SetFontList(GetFont());",
        ),
        Replacement(
            old="constexpr int kTextHorizPadding = 5;",
            new="constexpr int kTextHorizPadding = 10;",
            description="Maho: widen horizontal padding between the status-bar text and the bubble edge",
            guard="constexpr int kTextHorizPadding = 10;",
        ),
    ],
    "chrome/browser/device_reauth/chrome_device_authenticator_factory.cc": [
        Replacement(
            old=(
                "#else\n"
                "  static_assert(false);\n"
                "#endif"
            ),
            new=(
                "#elif BUILDFLAG(IS_LINUX)\n"
                "  // Maho Linux: upstream Chromium ships no Linux-desktop DeviceAuthenticator.\n"
                "  // Return null so reauth-gated operations fail closed; every caller in\n"
                "  // maho_settings_page_handler.cc checks for null and declines gracefully.\n"
                "  std::unique_ptr<DeviceAuthenticator> device_authenticator = nullptr;\n"
                "#else\n"
                "  static_assert(false);\n"
                "#endif"
            ),
            description="Maho Linux: fail-closed DeviceAuthenticator for Linux desktop",
            guard="std::unique_ptr<DeviceAuthenticator> device_authenticator = nullptr;",
        ),
    ],
    "chrome/browser/device_reauth/BUILD.gn": [
        Replacement(
            old=(
                "  if (is_android || is_mac || is_win || is_chromeos) {\n"
                "    public += [ \"chrome_device_authenticator_factory.h\" ]"
            ),
            new=(
                "  if (is_android || is_mac || is_win || is_chromeos || is_linux) {\n"
                "    public += [ \"chrome_device_authenticator_factory.h\" ]"
            ),
            description="Maho Linux: expose device authenticator factory header on Linux",
        ),
        Replacement(
            old=(
                "  if (is_android || is_mac || is_win || is_chromeos) {\n"
                "    sources += [ \"chrome_device_authenticator_factory.cc\" ]"
            ),
            new=(
                "  if (is_android || is_mac || is_win || is_chromeos || is_linux) {\n"
                "    sources += [ \"chrome_device_authenticator_factory.cc\" ]"
            ),
            description="Maho Linux: compile device authenticator factory source on Linux",
        ),
    ],
    "chrome/browser/ui/views/frame/browser_widget.cc": [
        Replacement(
            old=(
                '#include "chrome/browser/ui/views/frame/browser_view.h"\n'
            ),
            new=(
                '#include "chrome/browser/ui/views/frame/browser_view.h"\n'
                '#include "maho/browser/ui/theme/maho_space_theme_state.h"\n'
                '#include "maho/browser/ui/theme/maho_color_mixer.h"\n'
                '#include "ui/native_theme/native_theme.h"\n'
            ),
            description="Maho: include maho_space_theme_state.h and native_theme.h in browser_widget.cc",
            guard='#include "maho/browser/ui/theme/maho_space_theme_state.h"',
        ),
        Replacement(
            old=(
                '  // user_color.\n'
            ),
            new=(
                '  // Maho: the whole window (sidebar-dominated chrome) follows the active Space\n'
                '  // theme. Auto / no theme → OS system appearance. Incognito stays forced-dark.\n'
                '  if (!IsIncognitoBrowser()) {\n'
                '    const bool os_dark =\n'
                '        ui::NativeTheme::GetInstanceForNativeUi()->preferred_color_scheme() ==\n'
                '        ui::NativeTheme::PreferredColorScheme::kDark;\n'
                '    key.color_mode = MahoSpaceThemeState::ResolveSidebarDarkMode(os_dark)\n'
                '                         ? ui::ColorProviderKey::ColorMode::kDark\n'
                '                         : ui::ColorProviderKey::ColorMode::kLight;\n'
                '  } else {\n'
                '    key.app_controller = GetMahoOtrSentinel();\n'
                '  }\n'
                '\n'
                '  // user_color.\n'
            ),
            description="Maho: force window color_mode to follow space theme or OS dark mode",
            guard="MahoSpaceThemeState::ResolveSidebarDarkMode",
        ),
    ],
    "chrome/browser/ui/views/frame/contents_web_view.cc": [
        Replacement(
            old='#include "chrome/browser/ui/color/chrome_color_id.h"\n',
            new=(
                '#include "chrome/browser/ui/color/chrome_color_id.h"\n'
                '#include "chrome/browser/profiles/profile.h"\n'
                '#include "maho/browser/ui/theme/maho_color_id.h"\n'
            ),
            description=(
                "Maho R-12: include Profile + Maho private color ids in "
                "contents_web_view.cc for the exact-primary-Incognito content "
                "background projection"
            ),
            guard='#include "maho/browser/ui/theme/maho_color_id.h"',
        ),
        Replacement(
            old=(
                "  const SkColor color = GetColorProvider()->GetColor(\n"
                "      is_letterboxing() ? kColorWebContentsBackgroundLetterboxing\n"
                "                        : kColorWebContentsBackground);\n"
            ),
            new=(
                "  ui::ColorId maho_content_bg =\n"
                "      is_letterboxing() ? kColorWebContentsBackgroundLetterboxing\n"
                "                        : kColorWebContentsBackground;\n"
                "  if (web_contents()) {\n"
                "    Profile* maho_profile =\n"
                "        Profile::FromBrowserContext(web_contents()->GetBrowserContext());\n"
                "    if (maho_profile && maho_profile->IsIncognitoProfile() &&\n"
                "        maho_profile->IsPrimaryOTRProfile()) {\n"
                "      maho_content_bg = kMahoColorPrivateSidebarBackground;\n"
                "    }\n"
                "  }\n"
                "  const SkColor color = GetColorProvider()->GetColor(maho_content_bg);\n"
            ),
            description=(
                "Maho R-12: paint the exact-primary-Incognito content background "
                "with the fixed private semantic surface before renderer-ready so "
                "there is no regular-color flash; regular/Guest unchanged"
            ),
            guard="maho_content_bg",
        ),
    ],
    "chrome/browser/ui/webui/chrome_untrusted_web_ui_configs.cc": [],
    # ── settings_ui.cc: allow chrome://settings to be framed by the Maho
    #    settings "Chromium settings" embed pane ────────────────────────────
    "chrome/browser/ui/webui/settings/settings_ui.cc": [
        Replacement(
            old=(
                "          chrome::kChromeUISettingsHost);\n"
                "  html_source->OverrideContentSecurityPolicy(\n"
                "      network::mojom::CSPDirectiveName::WorkerSrc,\n"
            ),
            new=(
                "          chrome::kChromeUISettingsHost);\n"
                "  html_source->DisableDenyXFrameOptions();\n"
                '  html_source->AddFrameAncestor(GURL("chrome://maho-settings/"));\n'
                "  html_source->OverrideContentSecurityPolicy(\n"
                "      network::mojom::CSPDirectiveName::WorkerSrc,\n"
            ),
            description=(
                "Allow chrome://settings to be embedded in an <iframe> by the "
                "chrome://maho-settings 'Chromium settings' pane: disable the "
                "default X-Frame-Options: DENY and add chrome://maho-settings/ as "
                "a permitted frame-ancestor. Without this the embed renders blank "
                "because WebUI data sources default to frame-ancestors 'none'. "
                "GURL and the CSP header are already included upstream, so no "
                "extra include is required."
            ),
            guard='html_source->AddFrameAncestor(GURL("chrome://maho-settings/"));',
        ),
    ],
    # ── Maho extension APIs (chrome.maho.splitView / chrome.maho.sidePanel) ─────
    "chrome/common/extensions/api/api_sources.gni": [
        Replacement(
            old=(
                'uncompiled_sources_ = [\n'
                '  "action.json",\n'
                '  "browsing_data.json",\n'
                '  "commands.json",\n'
                '  "extension.json",\n'
                '  "top_sites.json",\n'
                ']\n'
            ),
            new=(
                'uncompiled_sources_ = [\n'
                '  "action.json",\n'
                '  "browsing_data.json",\n'
                '  "commands.json",\n'
                '  "extension.json",\n'
                '  "top_sites.json",\n'
                '  "//maho/browser/extensions/api/maho_split_view.json",\n'
                '  "//maho/browser/extensions/api/maho_side_panel.json",\n'
                ']\n'
            ),
            description=(
                "Register Maho extension API schemas (maho.splitView, "
                "maho.sidePanel) in the uncompiled schema source list so the "
                "JSON strings and function registration are generated"
            ),
            guard='"//maho/browser/extensions/api/maho_split_view.json"',
        ),
    ],
    "chrome/common/extensions/api/_api_features.override.json": [
        Replacement(
            old=(
                '{\n'
                '  // Entries go here.\n'
                '}\n'
            ),
            new=(
                '{\n'
                '  "maho.splitView": {\n'
                '    "dependencies": ["permission:maho"],\n'
                '    "contexts": ["privileged_extension"]\n'
                '  },\n'
                '  "maho.sidePanel": {\n'
                '    "dependencies": ["permission:maho"],\n'
                '    "contexts": ["privileged_extension"]\n'
                '  }\n'
                '}\n'
            ),
            description=(
                "Declare maho.splitView and maho.sidePanel API features gated "
                "on the 'maho' permission"
            ),
            guard='"maho.splitView"',
        ),
    ],
    "chrome/common/extensions/api/_permission_features.override.json": [
        Replacement(
            old=(
                '{\n'
                '  // Entries go here.\n'
                '}\n'
            ),
            new=(
                '{\n'
                '  "maho": {\n'
                '    "channel": "stable",\n'
                '    "extension_types": ["extension"]\n'
                '  }\n'
                '}\n'
            ),
            description="Declare the 'maho' extension permission",
            guard='"maho":',
        ),
    ],
    "extensions/browser/extension_function_histogram_value.h": [
        Replacement(
            old=(
                "GLICPRIVATE_INVOKE = 1963,\n"
                "  // Last entry: Add new entries above, then run:\n"
            ),
            new=(
                "GLICPRIVATE_INVOKE = 1963,\n"
                "  MAHO_SPLITVIEW_CREATE = 1964,\n"
                "  MAHO_SPLITVIEW_CLOSE = 1965,\n"
                "  MAHO_SPLITVIEW_QUERY = 1966,\n"
                "  MAHO_SIDEPANEL_SETLAYOUT = 1967,\n"
                "  // Last entry: Add new entries above, then run:\n"
            ),
            description=(
                "Add UMA histogram enum entries for the Maho split-view and "
                "side-panel extension functions"
            ),
            guard="MAHO_SPLITVIEW_CREATE = 1964,",
        ),
    ],
    # ── generated_resources.grd ────────────────────────────────────────────────
    "chrome/app/generated_resources.grd": [

        Replacement(
            old=(
                '      <message name="IDS_LINK_COPIED_TOAST_BODY" desc="Text on a toast notification that is shown when a link is successfully copied.">\n'
                '        Link copied\n'
                '      </message>\n'
            ),
            new=(
                '      <message name="IDS_LINK_COPIED_TOAST_BODY" desc="Text on a toast notification that is shown when a link is successfully copied.">\n'
                '        Link copied\n'
                '      </message>\n'
                '      <message name="IDS_MAHO_TOAST_LINK_COPIED" desc="Maho: Text shown when a link is successfully copied to the clipboard.">\n'
                '        Link copied\n'
                '      </message>\n'
                '      <message name="IDS_MAHO_TOAST_EXPORTED" desc="Maho: Text shown when an export operation succeeds.">\n'
                '        Exported\n'
                '      </message>\n'
                '      <message name="IDS_MAHO_TOAST_CLOSED_TABS" desc="Maho: Text shown when one or more tabs are closed. [ICU Syntax]">\n'
                '        {NUM_TABS, plural, =1 {Closed 1 tab} other {Closed # tabs}}\n'
                '      </message>\n'
                '      <message name="IDS_MAHO_EXPORT_THEME_FAILED_TITLE" desc="Maho: Title for theme export failure dialog.">\n'
                '        Export Failed\n'
                '      </message>\n'
                '      <message name="IDS_MAHO_EXPORT_THEME_FAILED_MESSAGE" desc="Maho: Message for theme export failure dialog.">\n'
                '        Could not export space theme to the selected file.\n'
                '      </message>\n'
            ),
            description="Add Maho transient notification string resources",
            guard='"IDS_MAHO_TOAST_LINK_COPIED"',
        ),
        Replacement(
            old=(
                '      <message name="IDS_MAHO_EXPORT_THEME_FAILED_MESSAGE" desc="Maho: Message for theme export failure dialog.">\n'
                '        Could not export space theme to the selected file.\n'
                '      </message>\n'
            ),
            new=(
                '      <message name="IDS_MAHO_EXPORT_THEME_FAILED_MESSAGE" desc="Maho: Message for theme export failure dialog.">\n'
                '        Could not export space theme to the selected file.\n'
                '      </message>\n'
                '      <message name="IDS_MAHO_CTRL_TAB_MORE_AFFORDANCE_COUNT" desc="Maho: Label shown in the Ctrl+Tab overlay for the +N more affordance card. [ICU Syntax]">\n'
                '        {NUM_MORE, plural, =1 {+1 more} other {+# more}}\n'
                '      </message>\n'
                '      <message name="IDS_MAHO_CTRL_TAB_MORE_AFFORDANCE_HINT" desc="Maho: Hint text under the +N more affordance in the Ctrl+Tab overlay describing what activating it does.">\n'
                '        Search all\n'
                '      </message>\n'
            ),
            description=(
                "Add Ctrl+Tab MRU switcher string resources for the +N more "
                "affordance count and hint"
            ),
            guard='"IDS_MAHO_CTRL_TAB_MORE_AFFORDANCE_COUNT"',
        ),
    ],
    # ── parse_html_subset.ts ──────────────────────────────────────────────────
    "ui/webui/resources/js/parse_html_subset.ts": [
        Replacement(
            old=(
                "const allowedOptionalTags: Set<string> = new Set(['IMG', 'LI', 'UL', 'OL']);\n"
            ),
            new=(
                "const allowedOptionalTags: Set<string> = new Set(['IMG', 'LI', 'UL', 'OL', 'CODE', 'BLOCKQUOTE', 'H1', 'H2', 'H3', 'H4', 'H5', 'H6', 'HR', 'DEL']);\n"
            ),
            description=(
                "Whitelist all markdown-safe tags (code, blockquote, h1-h6, hr, del) "
                "in parseHtmlSubset so renderTrustedMarkdown's output never throws "
                "'X is not supported' on persisted assistant messages — fixes blank "
                "panel triggered by any markdown element beyond the upstream default whitelist"
            ),
        ),
    ],
    # ── app-Info.plist ────────────────────────────────────────────────────────
    "chrome/app/app-Info.plist": [
        Replacement(
            old=(
                "\t<key>LSFileQuarantineEnabled</key>\n"
                "\t<false/>\n"
            ),
            new=(
                "\t<key>LSFileQuarantineEnabled</key>\n"
                "\t<false/>\n"
                "\t<key>SUFeedURL</key>\n"
                "\t<string>https://github.com/Project-Maho/release/releases/latest/download/appcast.xml</string>\n"
                "\t<key>SUPublicEDKey</key>\n"
                "\t<string>iwdrdct4loEJl8WbHsfhhHMNcUygZ54N6uIveroNTbw=</string>\n"
                "\t<key>SUEnableAutomaticChecks</key>\n"
                "\t<false/>\n"
                "\t<key>SUScheduledCheckInterval</key>\n"
                "\t<integer>86400</integer>\n"
            ),
            description=(
                "Disable LSFileQuarantineEnabled and inject Sparkle update keys to app-Info.plist"
            ),
        ),
        Replacement(
            old=(
                "\t<key>LSMinimumSystemVersion</key>\n"
                "\t<string>${CHROMIUM_MIN_SYSTEM_VERSION}</string>\n"
            ),
            new=(
                "\t<key>LSMinimumSystemVersion</key>\n"
                "\t<string>14.0</string>\n"
            ),
            description="Raise minimum macOS version to Sonoma (14.0)",
        ),
    ],
    # ── location_bar_view.cc ──────────────────────────────────────────────────
    "chrome/browser/ui/views/location_bar/location_bar_view.cc": [
        Replacement(
            old=(
                "  // Maho: construct coordinator and icon for the address-bar utility panel.\n"
                "  if (!is_popup_mode_) {\n"
                "    utility_bubble_coordinator_ =\n"
                "        std::make_unique<maho::MahoLocationBarUtilityBubbleCoordinator>();\n"
                "    utility_icon_view_ = AddChildView(\n"
                "        std::make_unique<maho::MahoLocationBarUtilityIconView>(\n"
                "            base::BindRepeating(\n"
                "                &LocationBarView::ShowUtilityPanel,\n"
                "                base::Unretained(this))));\n"
                "  }\n"
            ),
            new=(
                "  // Maho: the address-bar utility panel is sidebar-only; the standard-layout\n"
                "  // location-bar icon injection is intentionally disabled.\n"
            ),
            description=(
                "Disable the standard-layout location-bar utility icon injection; "
                "the panel is sidebar-only (MahoSidebarSearchView owns the coordinator)"
            ),
            guard="location-bar icon injection is intentionally disabled",
            idempotent=True,
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
        Replacement(
            old='#include "maho/browser/maho_tab_id_helper.h"\n',
            new=(
                '#include "maho/browser/maho_tab_id_helper.h"\n'
                '#include "maho/browser/net/maho_webstore_rebrand_handler.h"\n'
            ),
            description="Add MahoWebStoreRebrandHandler include to tab_helpers.cc",
            guard='#include "maho/browser/net/maho_webstore_rebrand_handler.h"',
        ),
        Replacement(
            old="  MahoTabIdHelper::CreateForWebContents(web_contents);\n",
            new=(
                "  MahoTabIdHelper::CreateForWebContents(web_contents);\n"
                "  MahoWebStoreRebrandHandler::CreateForWebContents(web_contents);\n"
            ),
            description=(
                "Attach MahoWebStoreRebrandHandler in TabHelpers::AttachTabHelpers()"
            ),
            guard="MahoWebStoreRebrandHandler::CreateForWebContents(web_contents);",
        ),
        Replacement(
            old='#include "maho/browser/ui/link_preview/maho_link_preview_manager.h"\n',
            new="",
            description="Maho: strip deleted link-preview manager include from tab_helpers.cc",
            idempotent=True,
        ),
        Replacement(
            old="  MahoLinkPreviewManager::CreateForWebContents(web_contents);\n",
            new="",
            description="Maho: strip deleted link-preview manager attach from tab_helpers.cc",
            idempotent=True,
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
    # ── session_startup_pref.cc ───────────────────────────────────────────────
    "chrome/browser/prefs/session_startup_pref.cc": [
        Replacement(
            old="  return SessionStartupPref::DEFAULT;\n",
            new="  return SessionStartupPref::LAST;\n",
            description=(
                "Make Maho default to restoring last session (continue where you "
                "left off) instead of opening NTP. Bypasses kSetDefaultToContinueSession "
                "feature flag and Chromium pref tracking by changing the registered "
                "default value itself. User explicit override via Settings still wins."
            ),
        ),
    ],
    "chrome/browser/ui/browser_command_controller.cc": [
        Replacement(
            old='#include "base/strings/utf_string_conversions.h"\n',
            new=(
                '#include "base/strings/utf_string_conversions.h"\n'
                '#include "base/task/single_thread_task_runner.h"\n'
            ),
            description=(
                "Include SingleThreadTaskRunner for deferred Maho new-tab "
                "command palette retry"
            ),
            guard='#include "base/task/single_thread_task_runner.h"',
        ),
        Replacement(
            old=(
                '    case IDC_SELECT_NEXT_TAB:\n'
                '      base::RecordAction(base::UserMetricsAction("Accel_SelectNextTab"));\n'
                '      SelectNextTab(\n'
                '          browser_,\n'
                '          TabStripUserGestureDetails(\n'
                '              TabStripUserGestureDetails::GestureType::kKeyboard, time_stamp));\n'
                '      break;\n'
                '    case IDC_SELECT_PREVIOUS_TAB:\n'
                '      base::RecordAction(base::UserMetricsAction("Accel_SelectPreviousTab"));\n'
                '      SelectPreviousTab(\n'
                '          browser_,\n'
                '          TabStripUserGestureDetails(\n'
                '              TabStripUserGestureDetails::GestureType::kKeyboard, time_stamp));\n'
                '      break;\n'
            ),
            new=(
                '    case IDC_SELECT_NEXT_TAB: {\n'
                '      base::RecordAction(base::UserMetricsAction("Accel_SelectNextTab"));\n'
                '      auto* maho_browser_view =\n'
                '          BrowserView::GetBrowserViewForBrowser(browser_);\n'
                '      if (maho_browser_view &&\n'
                '          maho_browser_view->MaybeHandleMruTabSwitch(/*forward=*/true)) {\n'
                '        break;\n'
                '      }\n'
                '      SelectNextTab(\n'
                '          browser_,\n'
                '          TabStripUserGestureDetails(\n'
                '              TabStripUserGestureDetails::GestureType::kKeyboard, time_stamp));\n'
                '      break;\n'
                '    }\n'
                '    case IDC_SELECT_PREVIOUS_TAB: {\n'
                '      base::RecordAction(base::UserMetricsAction("Accel_SelectPreviousTab"));\n'
                '      auto* maho_browser_view =\n'
                '          BrowserView::GetBrowserViewForBrowser(browser_);\n'
                '      if (maho_browser_view &&\n'
                '          maho_browser_view->MaybeHandleMruTabSwitch(/*forward=*/false)) {\n'
                '        break;\n'
                '      }\n'
                '      SelectPreviousTab(\n'
                '          browser_,\n'
                '          TabStripUserGestureDetails(\n'
                '              TabStripUserGestureDetails::GestureType::kKeyboard, time_stamp));\n'
                '      break;\n'
                '    }\n'
            ),
            description=(
                "Route Ctrl+Tab / Ctrl+Shift+Tab through Maho MRU tab switcher "
                "before Chromium's positional SelectNextTab/SelectPreviousTab. "
                "BrowserView::MaybeHandleMruTabSwitch returns true when the "
                "MRU switcher handled the command (pref enabled + at least "
                "two tabs); otherwise fall through to the upstream positional "
                "behavior."
            ),
            guard="MaybeHandleMruTabSwitch(/*forward=*/true)",
        ),
        Replacement(
            old=(
                '#include "chrome/browser/ui/views/frame/browser_view.h"\n'
                '#include "maho/browser/ui/views/sidebar/maho_sidebar_container_view.h"\n'
            ),
            new='#include "chrome/browser/ui/views/frame/browser_view.h"\n',
            description=(
                "Remove unused Maho sidebar include after routing new-tab "
                "commands through BrowserView"
            ),
        ),
        Replacement(
            old='#include "chrome/browser/ui/views/frame/browser_view.h"\n',
            new=(
                '#include "chrome/browser/ui/views/frame/browser_view.h"\n'
                '#include "maho/browser/ui/views/sidebar/maho_sidebar_container_view.h"\n'
            ),
            description=(
                "Re-add Maho sidebar container include for Cmd+N favorite "
                "activation"
            ),
            guard='#include "maho/browser/ui/views/sidebar/maho_sidebar_container_view.h"',
        ),
        Replacement(
            old=(
                "    case IDC_CLOSE_TAB:\n"
                '      base::RecordAction(base::UserMetricsAction("CloseTabByKey"));\n'
                "      CloseTab(browser_);\n"
                "      break;\n"
            ),
            new=(
                "    case IDC_CLOSE_TAB:\n"
                '      base::RecordAction(base::UserMetricsAction("CloseTabByKey"));\n'
                "      if (auto* maho_browser_view =\n"
                "              BrowserView::GetBrowserViewForBrowser(browser_);\n"
                "          maho_browser_view &&\n"
                "          maho_browser_view->MaybeHandleMultiTabClose()) {\n"
                "        break;\n"
                "      }\n"
                "      CloseTab(browser_);\n"
                "      break;\n"
            ),
            description=(
                "Route Cmd+W through BrowserView so sidebar multi-selected tabs "
                "close together instead of only the active tab"
            ),
            guard="maho_browser_view->MaybeHandleMultiTabClose()",
        ),
        Replacement(
            old=(
                '    case IDC_SELECT_TAB_0:\n'
                '    case IDC_SELECT_TAB_1:\n'
                '    case IDC_SELECT_TAB_2:\n'
                '    case IDC_SELECT_TAB_3:\n'
                '    case IDC_SELECT_TAB_4:\n'
                '    case IDC_SELECT_TAB_5:\n'
                '    case IDC_SELECT_TAB_6:\n'
                '    case IDC_SELECT_TAB_7:\n'
                '      base::RecordAction(base::UserMetricsAction("Accel_SelectNumberedTab"));\n'
                '      SelectNumberedTab(\n'
                '          browser_, id - IDC_SELECT_TAB_0,\n'
                '          TabStripUserGestureDetails(\n'
                '              TabStripUserGestureDetails::GestureType::kKeyboard, time_stamp));\n'
                '      break;\n'
            ),
            new=(
                '    case IDC_SELECT_TAB_0:\n'
                '    case IDC_SELECT_TAB_1:\n'
                '    case IDC_SELECT_TAB_2:\n'
                '    case IDC_SELECT_TAB_3:\n'
                '    case IDC_SELECT_TAB_4:\n'
                '    case IDC_SELECT_TAB_5:\n'
                '    case IDC_SELECT_TAB_6:\n'
                '    case IDC_SELECT_TAB_7: {\n'
                '      base::RecordAction(base::UserMetricsAction("Accel_SelectNumberedTab"));\n'
                '      auto* maho_browser_view =\n'
                '          BrowserView::GetBrowserViewForBrowser(browser_);\n'
                '      auto* maho_sidebar_container =\n'
                '          maho_browser_view\n'
                '              ? static_cast<maho::MahoSidebarContainerView*>(\n'
                '                    maho_browser_view->maho_sidebar_container())\n'
                '              : nullptr;\n'
                '      if (maho_sidebar_container &&\n'
                '          maho_sidebar_container->TryActivateFavoriteByIndex(\n'
                '              id - IDC_SELECT_TAB_0)) {\n'
                '        break;\n'
                '      }\n'
                '      SelectNumberedTab(\n'
                '          browser_, id - IDC_SELECT_TAB_0,\n'
                '          TabStripUserGestureDetails(\n'
                '              TabStripUserGestureDetails::GestureType::kKeyboard, time_stamp));\n'
                '      break;\n'
                '    }\n'
            ),
            description=(
                "Maho Cmd+1..8 activates Nth favorite (Zen essentials) with "
                "fallback to Chrome tab strip when no favorite exists"
            ),
        ),
        Replacement(
            old='constexpr char kShortcutTracePrefix[] = "[maho-shortcut-trace]";\n\n',
            new=(
                'constexpr char kShortcutTracePrefix[] = "[maho-shortcut-trace]";\n'
                "\n"
                "void ShowMahoCommandOverlayForNewTabWhenReady(base::WeakPtr<Browser> browser,\n"
                "                                              int remaining_attempts) {\n"
                "  if (!browser) {\n"
                "    return;\n"
                "  }\n"
                "\n"
                "  if (!browser->is_type_normal()) {\n"
                "    NewTab(browser.get());\n"
                "    return;\n"
                "  }\n"
                "\n"
                "  if (auto* browser_view = BrowserView::GetBrowserViewForBrowser(browser.get())) {\n"
                "    if (!browser_view->maho_sidebar_container()) {\n"
                "      NewTab(browser.get());\n"
                "      return;\n"
                "    }\n"
                "    browser_view->ShowMahoCommandOverlayForNewTab();\n"
                "    return;\n"
                "  }\n"
                "\n"
                "  if (remaining_attempts <= 0) {\n"
                "    NewTab(browser.get());\n"
                "    return;\n"
                "  }\n"
                "\n"
                "  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(\n"
                "      FROM_HERE,\n"
                "      base::BindOnce(&ShowMahoCommandOverlayForNewTabWhenReady, browser,\n"
                "                     remaining_attempts - 1),\n"
                "      base::Milliseconds(25));\n"
                "}\n"
                "\n"
                "bool ShouldDeferStartupCommandUntilTabSelected(int id) {\n"
                "  switch (id) {\n"
                "    case IDC_SELECT_NEXT_TAB:\n"
                "    case IDC_SELECT_PREVIOUS_TAB:\n"
                "    case IDC_SELECT_TAB_0:\n"
                "    case IDC_SELECT_TAB_1:\n"
                "    case IDC_SELECT_TAB_2:\n"
                "    case IDC_SELECT_TAB_3:\n"
                "    case IDC_SELECT_TAB_4:\n"
                "    case IDC_SELECT_TAB_5:\n"
                "    case IDC_SELECT_TAB_6:\n"
                "    case IDC_SELECT_TAB_7:\n"
                "    case IDC_SELECT_LAST_TAB:\n"
                "    case IDC_FOCUS_LOCATION:\n"
                "    case IDC_FOCUS_SEARCH:\n"
                "    case IDC_SHOW_HISTORY:\n"
                "    case IDC_SHOW_DOWNLOADS:\n"
                "    case IDC_SHOW_BOOKMARK_MANAGER:\n"
                "    case IDC_MAHO_COMMAND_BAR:\n"
                "    case IDC_MAHO_AI_PANEL:\n"
                "      return true;\n"
                "    default:\n"
                "      return false;\n"
                "  }\n"
                "}\n"
                "\n"
                "void ExecuteStartupCommandWhenTabSelected(base::WeakPtr<Browser> browser,\n"
                "                                          int id,\n"
                "                                          WindowOpenDisposition disposition,\n"
                "                                          base::TimeTicks time_stamp,\n"
                "                                          int remaining_attempts) {\n"
                "  if (!browser) {\n"
                "    return;\n"
                "  }\n"
                "\n"
                "  if (browser->tab_strip_model()->active_index() != TabStripModel::kNoTab) {\n"
                "    browser->command_controller()->ExecuteCommandWithDisposition(\n"
                "        id, disposition, time_stamp);\n"
                "    return;\n"
                "  }\n"
                "\n"
                "  if (remaining_attempts <= 0) {\n"
                "    return;\n"
                "  }\n"
                "\n"
                "  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(\n"
                "      FROM_HERE,\n"
                "      base::BindOnce(&ExecuteStartupCommandWhenTabSelected, browser, id,\n"
                "                     disposition, time_stamp, remaining_attempts - 1),\n"
                "      base::Milliseconds(25));\n"
                "}\n"
                "\n"
            ),
            description=(
                "Add deferred startup shortcut retry while BrowserView or the "
                "startup tab is not ready yet"
            ),
            guard="ShowMahoCommandOverlayForNewTabWhenReady",
        ),
        Replacement(
            old=(
                "    case IDC_NEW_TAB: {\n"
                "      if (auto* browser_view = BrowserView::GetBrowserViewForBrowser(browser_)) {\n"
                "        if (auto* sidebar_container =\n"
                "                views::AsViewClass<maho::MahoSidebarContainerView>(\n"
                "                    browser_view->maho_sidebar_container())) {\n"
                "          sidebar_container->ShowCommandOverlayForNewTab();\n"
                "          break;\n"
                "        }\n"
                "      }\n"
                "      NewTab(browser_);\n"
                "      break;\n"
                "    }\n"
            ),
            new=(
                "    case IDC_NEW_TAB: {\n"
                "      if (auto* browser_view = BrowserView::GetBrowserViewForBrowser(browser_)) {\n"
                "        if (browser_->is_type_normal() &&\n"
                "            browser_view->maho_sidebar_container()) {\n"
                "          browser_view->ShowMahoCommandOverlayForNewTab();\n"
                "        } else {\n"
                "          NewTab(browser_);\n"
                "        }\n"
                "        break;\n"
                "      }\n"
                "      ShowMahoCommandOverlayForNewTabWhenReady(browser_->AsWeakPtr(), 200);\n"
                "      break;\n"
                "    }\n"
            ),
            description=(
                "Route IDC_NEW_TAB through BrowserView so startup Cmd+T can "
                "use the deferred Maho command palette path"
            ),
        ),
        Replacement(
            old=(
                "  if (browser_->tab_strip_model()->active_index() == TabStripModel::kNoTab) {\n"
                "    return true;\n"
                "  }\n"
            ),
            new=(
                "  if (browser_->tab_strip_model()->active_index() == TabStripModel::kNoTab) {\n"
                "    if (id == IDC_NEW_TAB && browser_->is_type_normal()) {\n"
                "      ShowMahoCommandOverlayForNewTabWhenReady(browser_->AsWeakPtr(), 200);\n"
                "    } else if (ShouldDeferStartupCommandUntilTabSelected(id)) {\n"
                "      ExecuteStartupCommandWhenTabSelected(browser_->AsWeakPtr(), id,\n"
                "                                           disposition, time_stamp, 200);\n"
                "    }\n"
                "    return true;\n"
                "  }\n"
            ),
            description=(
                "Schedule startup-safe shortcuts even when they arrive before "
                "any tab is selected"
            ),
            guard="ShouldDeferStartupCommandUntilTabSelected(id)",
        ),
    ],
    "chrome/browser/app_controller_mac.mm": [
        Replacement(
            old=(
                "// Open the urls in the last used browser from a regular profile.\n"
                "void OpenUrlsInBrowserWithProfile(const std::vector<GURL>& urls,\n"
                "                                  Profile* profile);"
            ),
            new=(
                "}  // namespace\n"
                "\n"
                "// Open the urls in the last used browser from a regular profile.\n"
                "void OpenUrlsInBrowserWithProfile(const std::vector<GURL>& urls,\n"
                "                                  Profile* profile);\n"
                "\n"
                "namespace {"
            ),
            description="Declare OpenUrlsInBrowserWithProfile in global namespace to allow linkage from tests",
            guard="}  // namespace\n\n// Open the urls in the last used browser from a regular profile.\nvoid OpenUrlsInBrowserWithProfile",
        ),
        Replacement(
            old=(
                '#import "chrome/browser/app_controller_mac.h"\n'
            ),
            new=(
                '#import "chrome/browser/app_controller_mac.h"\n'
                '#include "maho/browser/ui/views/little_arc/maho_little_arc_window.h"\n'
                '#include "maho/browser/ui/views/sidebar/maho_sidebar_prefs.h"\n'
                '#include "components/prefs/pref_service.h"\n'
                '#include "chrome/browser/ui/browser_finder.h"\n'
                '#include "chrome/browser/ui/browser_tabstrip.h"\n'
                '#include "maho/browser/maho_space_profile_bridge.h"\n'
            ),
            description="Include Little Arc and Space headers in app_controller_mac.mm",
            guard='#include "maho/browser/maho_space_profile_bridge.h"',
        ),
        Replacement(
            old=(
                "void OpenUrlsInBrowserWithProfile(const std::vector<GURL>& urls,\n"
                "                                  Profile* profile) {\n"
                "  if (!profile)\n"
                "    return;  // No suitable profile to open the URLs, do nothing."
            ),
            new=(
                "void OpenUrlsInBrowserWithProfileNormal(const std::vector<GURL>& urls,\n"
                "                                        Profile* profile);\n"
                "\n"
                "}  // namespace\n"
                "\n"
                "void OpenUrlsInBrowserWithProfile(const std::vector<GURL>& urls,\n"
                "                                  Profile* profile) {\n"
                "  if (!profile)\n"
                "    return;  // No suitable profile to open the URLs, do nothing.\n"
                "\n"
                "  if (urls.size() == 1) {\n"
                "    GURL url = urls[0];\n"
                "    base::WeakPtr<Profile> weak_profile = profile->GetWeakPtr();\n"
                "    maho::DecideLinkDestination(\n"
                "        url, /*is_external=*/true,\n"
                "        base::BindOnce(\n"
                "            [](base::WeakPtr<Profile> profile, GURL url, maho::LinkDestinationResult result) {\n"
                "              if (!profile) return;\n"
                "              if (result.type == maho::LinkDestinationType::kSpace && !result.space_id.empty()) {\n"
                "                Browser* browser = chrome::FindTabbedBrowser(profile.get(), false);\n"
                "                if (!browser) {\n"
                "                  browser = Browser::Create(Browser::CreateParams(profile.get(), true));\n"
                "                  if (browser && browser->window()) {\n"
                "                    browser->window()->Show();\n"
                "                  }\n"
                "                }\n"
                "                if (browser) {\n"
                "                  maho::MahoSpaceProfileBridge* bridge = maho::MahoSpaceProfileBridge::GetInstance();\n"
                "                  if (bridge) {\n"
                "                    bridge->SwitchToSpace(browser, result.space_id);\n"
                "                  }\n"
                "                  chrome::AddSelectedTabWithURL(browser, url, ui::PAGE_TRANSITION_LINK);\n"
                "                }\n"
                "              } else if (result.type == maho::LinkDestinationType::kLittleArc) {\n"
                "                maho::LaunchLittleArc(profile.get(), maho::LittleArcRequest{url});\n"
                "              } else {\n"
                "                std::vector<GURL> urls_fallback = {url};\n"
                "                OpenUrlsInBrowserWithProfileNormal(urls_fallback, profile.get());\n"
                "              }\n"
                "            },\n"
                "            weak_profile, url));\n"
                "    return;\n"
                "  }\n"
                "  OpenUrlsInBrowserWithProfileNormal(urls, profile);\n"
                "}\n"
                "\n"
                "namespace {\n"
                "\n"
                "void OpenUrlsInBrowserWithProfileNormal(const std::vector<GURL>& urls,\n"
                "                                        Profile* profile) {\n"
                "  if (!profile)\n"
                "    return;  // No suitable profile to open the URLs, do nothing."
            ),
            description="Route external single URL links to ATC Space or Little Arc if configured, with global linkage",
            guard="DecideLinkDestination(url, /*is_external=*/true",
        ),
        Replacement(
            old=(
                "    case IDC_NEW_TAB:\n"
                "      // Create a new tab in an existing browser window (which we activate) if\n"
                "      // possible.\n"
                "      if (Browser* browser = ActivateBrowser(profile)) {\n"
                "        chrome::ExecuteCommand(browser, IDC_NEW_TAB);\n"
                "        break;\n"
                "      }\n"
                "      [[fallthrough]];  // To create new window.\n"
                "    case IDC_NEW_WINDOW:\n"
                "      CreateBrowser(profile->GetOriginalProfile());\n"
                "      break;\n"
            ),
            new=(
                "    case IDC_NEW_TAB:\n"
                "      if (Browser* browser = ActivateBrowser(profile)) {\n"
                "        chrome::ExecuteCommand(browser, IDC_NEW_TAB);\n"
                "        break;\n"
                "      }\n"
                "      if (BrowserWindowInterface* browser_window =\n"
                "              CreateBrowser(profile->GetOriginalProfile())) {\n"
                "        if (Browser* browser =\n"
                "                browser_window->GetBrowserForMigrationOnly()) {\n"
                "          chrome::ExecuteCommand(browser, IDC_NEW_TAB);\n"
                "        }\n"
                "      }\n"
                "      break;\n"
                "    case IDC_NEW_WINDOW:\n"
                "      CreateBrowser(profile->GetOriginalProfile());\n"
                "      break;\n"
            ),
            description=(
                "After no-key-window Cmd+T creates the first browser, dispatch "
                "IDC_NEW_TAB into that browser so Maho opens the command palette"
            ),
        ),
    ],
    "chrome/browser/ui/browser_command_controller_unittest.cc": [
        Replacement(
            old=(
                "TEST_F(BrowserCommandControllerTest,\n"
                "       SavePageDisabledByAllowFileSelectionDialogsPolicy) {\n"
                "  chrome::BrowserCommandController command_controller(browser());\n"
                "  const CommandUpdater* command_updater = &command_controller;\n"
                "\n"
                "  EXPECT_TRUE(command_updater->IsCommandEnabled(IDC_SAVE_PAGE));\n"
                "  g_browser_process->local_state()->SetBoolean(\n"
                "      prefs::kAllowFileSelectionDialogs, false);\n"
                "  EXPECT_FALSE(command_updater->IsCommandEnabled(IDC_SAVE_PAGE));\n"
                "}\n"
            ),
            new=(
                "TEST_F(BrowserCommandControllerTest,\n"
                "       SavePageDisabledByAllowFileSelectionDialogsPolicy) {\n"
                "  chrome::BrowserCommandController command_controller(browser());\n"
                "  const CommandUpdater* command_updater = &command_controller;\n"
                "\n"
                "  EXPECT_TRUE(command_updater->IsCommandEnabled(IDC_SAVE_PAGE));\n"
                "  g_browser_process->local_state()->SetBoolean(\n"
                "      prefs::kAllowFileSelectionDialogs, false);\n"
                "  EXPECT_FALSE(command_updater->IsCommandEnabled(IDC_SAVE_PAGE));\n"
                "}\n"
                "\n"
                "TEST_F(BrowserCommandControllerTest, SelectTabCommandWaitsForStartupTab) {\n"
                "  chrome::BrowserCommandController command_controller(browser());\n"
                "  browser()->tab_strip_model()->CloseAllTabs();\n"
                "  ASSERT_EQ(TabStripModel::kNoTab, browser()->tab_strip_model()->active_index());\n"
                "\n"
                "  EXPECT_TRUE(command_controller.ExecuteCommand(IDC_SELECT_TAB_0));\n"
                "  AddTab(browser(), GURL(\"https://example.com/first\"));\n"
                "  AddTab(browser(), GURL(\"https://example.com/second\"));\n"
                "  browser()->tab_strip_model()->ActivateTabAt(1);\n"
                "  ASSERT_EQ(1, browser()->tab_strip_model()->active_index());\n"
                "\n"
                "  task_environment()->RunUntilIdle();\n"
                "  EXPECT_EQ(0, browser()->tab_strip_model()->active_index());\n"
                "}\n"
            ),
            description=(
                "Add regression coverage for startup tab-selection shortcut "
                "deferral"
            ),
            guard="SelectTabCommandWaitsForStartupTab",
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
                '    "//maho/browser/ui/views/side_panel/maho_extension_side_panel_coordinator.cc",\n'
                '    "//maho/browser/ui/views/side_panel/maho_extension_side_panel_coordinator.h",\n'
            ),
            new=(
                '    "//maho/browser/ui/views/side_panel/maho_extension_side_panel_coordinator.cc",\n'
                '    "//maho/browser/ui/views/side_panel/maho_extension_side_panel_coordinator.h",\n'
                '    "//maho/browser/extensions/api/maho_side_panel_api.cc",\n'
                '    "//maho/browser/extensions/api/maho_side_panel_api.h",\n'
            ),
            description=(
                "Compile the maho.sidePanel ExtensionFunction into the "
                "side_panel target, co-located with its coordinator"
            ),
            guard='"//maho/browser/extensions/api/maho_side_panel_api.cc"',
        ),
        # Self-healing deduplication: collapse any previously double-injected
        # source_set("interactive_tests") blocks down to one.  Two passes are
        # needed because apply_replacements uses replace(..., 1): the first
        # entry fires when three identical contiguous blocks exist (3→2) and
        # the second fires when two identical contiguous blocks exist (2→1).
        # Both entries are idempotent so they silently no-op once clean.
        Replacement(
            old=(
                "\nsource_set(\"interactive_tests\") {\n"
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
                "\nsource_set(\"interactive_tests\") {\n"
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
            new=(
                "\nsource_set(\"interactive_tests\") {\n"
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
            description="Dedup side-panel interactive_tests blocks (pass 1 of 2: 3→2)",
            idempotent=True,
        ),
        Replacement(
            old=(
                "\nsource_set(\"interactive_tests\") {\n"
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
                "\nsource_set(\"interactive_tests\") {\n"
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
            new=(
                "\nsource_set(\"interactive_tests\") {\n"
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
            description="Dedup side-panel interactive_tests blocks (pass 2 of 2: 2→1)",
            idempotent=True,
        ),
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
            guard="  sources = [ \"//maho/browser/ui/views/side_panel/maho_ai_side_panel_interactive_uitest.cc\" ]\n",
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
            old='    "//maho/browser:maho_link_preview",\n',
            new="",
            description="Maho: strip deleted maho_link_preview dep from chrome/browser/ui:ui",
            idempotent=True,
        ),
        Replacement(
            old=(
                '      "//maho/browser/ui/views/little_arc/maho_little_arc_window.cc",\n'
                '      "//maho/browser/ui/views/little_arc/maho_little_arc_window.h",\n'
            ),
            new=(
                '      "//maho/browser/ui/views/little_arc/maho_little_arc_window.cc",\n'
                '      "//maho/browser/ui/views/little_arc/maho_little_arc_window.h",\n'
                '      "//maho/browser/ui/views/little_arc/maho_little_arc_top_bar_view.cc",\n'
                '      "//maho/browser/ui/views/little_arc/maho_little_arc_top_bar_view.h",\n'
                '      "//maho/browser/net/maho_atc_navigation_throttle.cc",\n'
                '      "//maho/browser/net/maho_atc_navigation_throttle.h",\n'
            ),
            description=(
                "Compile the MahoAtcNavigationThrottle and MahoLittleArcTopBarView into the "
                "chrome/browser/ui:ui target"
            ),
            guard='"//maho/browser/ui/views/little_arc/maho_little_arc_top_bar_view.cc"',
        ),
        Replacement(
            old=(
                '      "//maho/browser/ui/views/little_arc/maho_little_arc_top_bar_view.cc",\n'
                '      "//maho/browser/ui/views/little_arc/maho_little_arc_top_bar_view.h",\n'
            ),
            new=(
                '      "//maho/browser/ui/views/little_arc/maho_little_arc_top_bar_view.cc",\n'
                '      "//maho/browser/ui/views/little_arc/maho_little_arc_top_bar_view.h",\n'
                '      "//maho/browser/ui/views/maho_content_gradient_view.cc",\n'
                '      "//maho/browser/ui/views/maho_content_gradient_view.h",\n'
            ),
            description=(
                "Compile MahoContentGradientView into the chrome/browser/ui:ui "
                "target used by BrowserView"
            ),
            guard='"//maho/browser/ui/views/maho_content_gradient_view.cc"',
        ),
        Replacement(
            old=(
                '      "//maho/browser/ui/views/split_view/maho_split_view_controller.cc",\n'
                '      "//maho/browser/ui/views/split_view/maho_split_view_controller.h",\n'
            ),
            new=(
                '      "//maho/browser/ui/views/split_view/maho_split_view_controller.cc",\n'
                '      "//maho/browser/ui/views/split_view/maho_split_view_controller.h",\n'
                '      "//maho/browser/extensions/api/maho_split_view_api.cc",\n'
                '      "//maho/browser/extensions/api/maho_split_view_api.h",\n'
            ),
            description=(
                "Compile the maho.splitView ExtensionFunctions into the "
                "chrome/browser/ui:ui target, co-located with "
                "MahoSplitViewController"
            ),
            guard='"//maho/browser/extensions/api/maho_split_view_api.cc"',
        ),
        Replacement(
            old=(
                '      "//maho/browser/ui/views/sidebar/maho_sidebar_state_adapter.h",\n'
                '      "//maho/browser/ui/tab_preview/maho_tab_preview_controller.cc",\n'
                '      "//maho/browser/ui/tab_preview/maho_tab_preview_controller.h",\n'
                '      "//maho/browser/ui/views/sidebar/maho_sidebar_state_models.cc",\n'
            ),
            new=(
                '      "//maho/browser/ui/views/sidebar/maho_sidebar_state_adapter.h",\n'
                '      "//maho/browser/ui/views/sidebar/maho_sidebar_state_models.cc",\n'
            ),
            description="Remove duplicate maho_tab_preview_controller.cc/.h pair that appears after maho_sidebar_state_adapter.h in BUILD.gn",
            idempotent=True,
        ),
        Replacement(
            old=(
                '      "//maho/browser/ui/views/sidebar/maho_sidebar_state_models.cc",\n'
                '      "//maho/browser/ui/views/sidebar/maho_sidebar_state_models.h",\n'
                '      "//maho/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc",\n'
            ),
            new=(
                '      "//maho/browser/ui/tab_preview/maho_tab_preview_controller.cc",\n'
                '      "//maho/browser/ui/tab_preview/maho_tab_preview_controller.h",\n'
                '      "//maho/browser/maho_tab_preview_capture.cc",\n'
                '      "//maho/browser/maho_tab_preview_capture.h",\n'
                '      "//maho/browser/maho_tab_id_session_helper.cc",\n'
                '      "//maho/browser/maho_tab_id_session_helper.h",\n'
                '      "//maho/browser/ui/views/sidebar/maho_sidebar_state_models.cc",\n'
                '      "//maho/browser/ui/views/sidebar/maho_sidebar_state_models.h",\n'
                '      "//maho/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc",\n'
                '      "//maho/browser/ui/views/sidebar/maho_sidebar_folder_hover_popup_view.cc",\n'
                '      "//maho/browser/ui/views/sidebar/maho_sidebar_folder_hover_popup_view.h",\n'
                '      "//maho/browser/ui/views/sidebar/maho_sidebar_folder_hover_controller.cc",\n'
                '      "//maho/browser/ui/views/sidebar/maho_sidebar_folder_hover_controller.h",\n'
                '      "//maho/browser/ui/views/sidebar/maho_sidebar_update_notification_controller.cc",\n'
                '      "//maho/browser/ui/views/sidebar/maho_sidebar_update_notification_controller.h",\n'
                '      "//maho/browser/ui/views/sidebar/maho_sidebar_update_notification_model.cc",\n'
                '      "//maho/browser/ui/views/sidebar/maho_sidebar_update_notification_model.h",\n'
                '      "//maho/browser/ui/views/sidebar/maho_sidebar_update_notification_view.cc",\n'
                '      "//maho/browser/ui/views/sidebar/maho_sidebar_update_notification_view.h",\n'
                '      "//maho/browser/ui/views/sidebar/maho_sidebar_now_playing_view.cc",\n'
                '      "//maho/browser/ui/views/sidebar/maho_sidebar_now_playing_view.h",\n'
                '      "//maho/browser/ui/views/sidebar/maho_now_playing_card.cc",\n'
                '      "//maho/browser/ui/views/sidebar/maho_now_playing_card.h",\n'
                '      "//maho/browser/ui/views/sidebar/maho_sidebar_spaces_view.cc",\n'
                '      "//maho/browser/ui/views/sidebar/maho_sidebar_spaces_view.h",\n'
                '      "//maho/browser/ui/views/sidebar/maho_sidebar_visibility_manager.cc",\n'
                '      "//maho/browser/ui/views/sidebar/maho_sidebar_visibility_manager.h",\n'
                '      "//maho/browser/ui/views/sidebar/maho_toolbar_button_provider.cc",\n'
                '      "//maho/browser/ui/views/sidebar/maho_toolbar_button_provider.h",\n'
                '      "//maho/browser/ui/views/sidebar/maho_traffic_light_geometry_mac.mm",\n'
                '      "//maho/browser/ai/maho_tab_tidy_orchestrator.cc",\n'
                '      "//maho/browser/ai/maho_tab_tidy_orchestrator.h",\n'
            ),
            description="Include Maho tab preview controller sources in Chromium browser UI build",
            guard='      "//maho/browser/ui/views/sidebar/maho_sidebar_folder_hover_popup_view.cc",\n',
        ),
        Replacement(
            old='      "//maho/browser/ui/views/spaces_overlay:maho_spaces_overlay",\n',
            new=(
                '      "//maho/browser/ui/views/spaces_overlay:maho_spaces_overlay",\n'
                '      "//maho/browser/ui/tabs:maho_mru_tab_tracker",\n'
            ),
            description=(
                "Add MahoMruTabTracker source_set dep to chrome/browser/ui:ui "
                "so browser_view.cc can include maho/browser/ui/tabs/maho_mru_tab_tracker.h"
            ),
            guard='"//maho/browser/ui/tabs:maho_mru_tab_tracker"',
        ),
        Replacement(
            old='      "//maho/browser/ui/tabs:maho_mru_tab_tracker",\n',
            new=(
                '      "//maho/browser/ui/tabs:maho_mru_tab_tracker",\n'
                '      "//maho/browser/ui/tabs:maho_ctrl_tab_switcher_controller",\n'
            ),
            description=(
                "Add MahoCtrlTabSwitcherController source_set dep to "
                "chrome/browser/ui:ui so browser_view.cc can include the "
                "controller header and manage its lifetime"
            ),
            guard='"//maho/browser/ui/tabs:maho_ctrl_tab_switcher_controller"',
        ),
    ],
    "chrome/browser/prefs/browser_prefs.cc": [
        Replacement(
            old='#include "maho/browser/ui/views/sidebar/maho_sidebar_prefs.h"\n',
            new=(
                '#include "maho/browser/ui/views/sidebar/maho_sidebar_prefs.h"\n'
                '#include "maho/browser/ui/tabs/maho_ctrl_tab_prefs.h"\n'
            ),
            description=(
                "Include maho_ctrl_tab_prefs.h in browser_prefs.cc for pref "
                "registration of Ctrl+Tab MRU switcher settings"
            ),
            guard='#include "maho/browser/ui/tabs/maho_ctrl_tab_prefs.h"',
        ),
        Replacement(
            old='  maho::sidebar_prefs::RegisterProfilePrefs(registry);\n',
            new=(
                '  maho::sidebar_prefs::RegisterProfilePrefs(registry);\n'
                '  maho::ctrl_tab_prefs::RegisterProfilePrefs(registry);\n'
            ),
            description=(
                "Register Ctrl+Tab MRU switcher prefs (mru_order, scope, "
                "max_visible) alongside the other Maho profile prefs"
            ),
            guard="maho::ctrl_tab_prefs::RegisterProfilePrefs",
        ),
    ],
    "chrome/browser/ui/views/frame/browser_view.h": [
        Replacement(
            old=(
                "  // Copy the accelerator table from the app resources into something we can\n"
                "  // use.\n"
                "  void LoadAccelerators();\n"
            ),
            new=(
                "  // Copy the accelerator table from the app resources into something we can\n"
                "  // use.\n"
                "  void LoadAccelerators();\n"
                "\n"
                "  void MaybeShowPendingMahoCommandOverlayForNewTab();\n"
            ),
            description=(
                "Declare deferred Maho new-tab command palette opener for "
                "startup Cmd+T before BrowserView widget initialization"
            ),
            guard="MaybeShowPendingMahoCommandOverlayForNewTab",
        ),
        Replacement(
            old="class MahoCommandOverlayController;\n",
            new=(
                "class MahoCommandOverlayController;\n"
                "class MahoCtrlTabSwitcherController;\n"
                "class MahoMruTabTracker;\n"
            ),
            description=(
                "Forward-declare MahoMruTabTracker and "
                "MahoCtrlTabSwitcherController so BrowserView can own both "
                "for the Ctrl+Tab MRU switcher feature"
            ),
            guard="class MahoCtrlTabSwitcherController;",
        ),
        Replacement(
            old=(
                "  void ShowMahoCommandOverlayForSearch(const std::string& initial_text);\n"
            ),
            new=(
                "  void ShowMahoCommandOverlayForSearch(const std::string& initial_text);\n"
                "\n"
                "  // Routes Ctrl+Tab / Ctrl+Shift+Tab through the Maho MRU tab\n"
                "  // switcher when the pref is enabled and at least two tabs\n"
                "  // exist. Returns true when the MRU switcher handled the\n"
                "  // command; the caller must skip the upstream positional\n"
                "  // SelectNextTab/SelectPreviousTab in that case.\n"
                "  bool MaybeHandleMruTabSwitch(bool forward);\n"
            ),
            description=(
                "Declare BrowserView::MaybeHandleMruTabSwitch for the Ctrl+Tab "
                "MRU tab switcher hook"
            ),
            guard="bool MaybeHandleMruTabSwitch(bool forward);",
        ),
        Replacement(
            old="  bool MaybeHandleMruTabSwitch(bool forward);\n",
            new=(
                "  bool MaybeHandleMruTabSwitch(bool forward);\n"
                "\n"
                "  // Closes all sidebar multi-selected tabs when two or more are\n"
                "  // selected. Returns true when it handled the close so the\n"
                "  // caller skips the upstream single-tab CloseTab.\n"
                "  bool MaybeHandleMultiTabClose();\n"
            ),
            description=(
                "Declare BrowserView::MaybeHandleMultiTabClose for Cmd+W "
                "sidebar multi-select close"
            ),
            guard="bool MaybeHandleMultiTabClose();",
        ),
        Replacement(
            old=(
                "  std::unique_ptr<maho::MahoCommandOverlayController>\n"
                "      maho_command_overlay_controller_;\n"
                "  std::unique_ptr<maho::MahoSpacesOverlayController>\n"
                "      maho_spaces_overlay_controller_;\n"
            ),
            new=(
                "  std::unique_ptr<maho::MahoCommandOverlayController>\n"
                "      maho_command_overlay_controller_;\n"
                "  std::unique_ptr<maho::MahoSpacesOverlayController>\n"
                "      maho_spaces_overlay_controller_;\n"
                "  bool pending_maho_command_overlay_new_tab_ = false;\n"
            ),
            description=(
                "Track one pending Maho new-tab command palette request while "
                "BrowserView is still attaching to its widget"
            ),
            guard="pending_maho_command_overlay_new_tab_",
        ),
        Replacement(
            old=(
                "  std::unique_ptr<maho::MahoCommandOverlayController>\n"
                "      maho_command_overlay_controller_;\n"
                "  std::unique_ptr<maho::MahoSpacesOverlayController>\n"
                "      maho_spaces_overlay_controller_;\n"
                "  bool pending_maho_command_overlay_new_tab_ = false;\n"
            ),
            new=(
                "  std::unique_ptr<maho::MahoCommandOverlayController>\n"
                "      maho_command_overlay_controller_;\n"
                "  std::unique_ptr<maho::MahoSpacesOverlayController>\n"
                "      maho_spaces_overlay_controller_;\n"
                "  bool pending_maho_command_overlay_new_tab_ = false;\n"
                "  std::unique_ptr<maho::MahoMruTabTracker> maho_mru_tab_tracker_;\n"
            ),
            description=(
                "Own MahoMruTabTracker on BrowserView for the Ctrl+Tab MRU "
                "switcher feature"
            ),
            guard="maho_mru_tab_tracker_",
        ),
        Replacement(
            old=(
                "  bool pending_maho_command_overlay_new_tab_ = false;\n"
                "  std::unique_ptr<maho::MahoMruTabTracker> maho_mru_tab_tracker_;\n"
            ),
            new=(
                "  bool pending_maho_command_overlay_new_tab_ = false;\n"
                "  std::unique_ptr<maho::MahoMruTabTracker> maho_mru_tab_tracker_;\n"
                "  std::unique_ptr<maho::MahoCtrlTabSwitcherController>\n"
                "      maho_ctrl_tab_switcher_controller_;\n"
            ),
            description=(
                "Own MahoCtrlTabSwitcherController on BrowserView (Phase 3 "
                "wiring; the controller drives the overlay state machine and "
                "commits tab activation on Ctrl release)"
            ),
            guard="maho_ctrl_tab_switcher_controller_",
        ),
    ],
    # ── browser_view.cc ───────────────────────────────────────────────────────
    "chrome/browser/ui/views/frame/browser_view.cc": [
        Replacement(
            old=(
                "bool BrowserView::AppUsesWindowControlsOverlay() const {\n"
                "  return browser()->app_controller() &&\n"
                "         browser()->app_controller()->AppUsesWindowControlsOverlay();\n"
                "}\n"
            ),
            new=(
                "bool BrowserView::AppUsesWindowControlsOverlay() const {\n"
                "#if BUILDFLAG(IS_WIN)\n"
                "  // Maho: the Arc main window uses Window Controls Overlay on Windows\n"
                "  // to overlay the native caption buttons on the frameless,\n"
                "  // edge-to-edge sidebar + contents layout.\n"
                "  if (IsMahoArcLayoutActive()) {\n"
                "    return true;\n"
                "  }\n"
                "#endif\n"
                "  return browser()->app_controller() &&\n"
                "         browser()->app_controller()->AppUsesWindowControlsOverlay();\n"
                "}\n"
            ),
            description=(
                "Maho(win): enable Window Controls Overlay for the Arc main window so "
                "the native caption buttons overlay the frameless edge-to-edge layout"
            ),
            guard="// Maho: the Arc main window uses Window Controls Overlay on Windows",
        ),
        Replacement(
            old=(
                "  bool enabled = should_show_window_controls_overlay_toggle_ &&\n"
                "                 browser()->app_controller() &&\n"
                "                 browser()->app_controller()->IsWindowControlsOverlayEnabled();\n"
            ),
            new=(
                "#if BUILDFLAG(IS_WIN)\n"
                "  const bool maho_wco = IsMahoArcLayoutActive();\n"
                "#else\n"
                "  const bool maho_wco = false;\n"
                "#endif\n"
                "  bool enabled = maho_wco ||\n"
                "                 (should_show_window_controls_overlay_toggle_ &&\n"
                "                  browser()->app_controller() &&\n"
                "                  browser()->app_controller()->IsWindowControlsOverlayEnabled());\n"
            ),
            description=(
                "Maho(win): allow Window Controls Overlay to be enabled for the Arc "
                "main window, not only PWA app windows"
            ),
            guard="const bool maho_wco = IsMahoArcLayoutActive();",
        ),
        Replacement(
            old=(
                "void BrowserView::ShowAppMenu() {\n"
                "  auto* control = ToolbarButtonProvider::From(browser_)->GetAppMenuControl();\n"
                "  if (!control) {\n"
                "    return;\n"
                "  }\n"
                "\n"
                "  // Keep the top-of-window views revealed as long as the app menu is visible.\n"
                "  std::unique_ptr<ImmersiveRevealedLock> revealed_lock =\n"
                "      ImmersiveModeController::From(browser())->GetRevealedLock(\n"
                "          ImmersiveModeController::ANIMATE_REVEAL_NO);\n"
                "\n"
                "  control->ShowMenu();\n"
                "}\n"
            ),
            new=(
                "void BrowserView::ShowAppMenu() {\n"
                "#if BUILDFLAG(IS_WIN)\n"
                "  if (IsMahoArcLayoutActive()) {\n"
                "    if (auto* maho_sidebar_container =\n"
                "            views::AsViewClass<maho::MahoSidebarContainerView>(\n"
                "                maho_sidebar_container_);\n"
                "        maho_sidebar_container && maho_sidebar_container->ShowAppMenu()) {\n"
                "      return;\n"
                "    }\n"
                "  }\n"
                "#endif\n"
                "  auto* control = ToolbarButtonProvider::From(browser_)->GetAppMenuControl();\n"
                "  if (!control) {\n"
                "    return;\n"
                "  }\n"
                "\n"
                "  // Keep the top-of-window views revealed as long as the app menu is visible.\n"
                "  std::unique_ptr<ImmersiveRevealedLock> revealed_lock =\n"
                "      ImmersiveModeController::From(browser())->GetRevealedLock(\n"
                "          ImmersiveModeController::ANIMATE_REVEAL_NO);\n"
                "\n"
                "  control->ShowMenu();\n"
                "}\n"
            ),
            description=(
                "Maho(win): route BrowserView::ShowAppMenu through the visible "
                "sidebar ellipsis AppMenu in Arc layout, with upstream fallback"
            ),
            guard="maho_sidebar_container->ShowAppMenu()",
        ),
        Replacement(
            old='#include "maho/browser/ui/views/command/maho_command_overlay_controller.h"\n',
            new=(
                '#include "maho/browser/ui/views/command/maho_command_overlay_controller.h"\n'
                '#include "maho/browser/ui/tabs/maho_mru_tab_tracker.h"\n'
                '#include "maho/browser/maho_tab_id_session_helper.h"\n'
                '#include "maho/browser/maho_tab_preview_capture.h"\n'
                '#include "maho/third_party/maho/maho_ffi.h"\n'
                '#include "maho/browser/maho_core_holder.h"\n'
            ),
            description=(
                "Include MahoMruTabTracker and MahoCore FFI headers in browser_view.cc"
            ),
            guard='#include "maho/third_party/maho/maho_ffi.h"',
        ),
        Replacement(
            old='#include "maho/browser/ui/views/command/maho_command_overlay_controller.h"\n',
            new=(
                '#include "maho/browser/ui/views/command/maho_command_overlay_controller.h"\n'
                '#include "maho/browser/ui/views/sidebar/maho_sidebar_container_view.h"\n'
                '#include "maho/browser/ui/views/sidebar/maho_sidebar_view.h"\n'
                '#include "maho/browser/ui/views/sidebar/maho_sidebar_tab_list_view.h"\n'
            ),
            description=(
                "Include Maho sidebar view + tab list view in browser_view.cc "
                "for MaybeHandleMultiTabClose"
            ),
            guard='#include "maho/browser/ui/views/sidebar/maho_sidebar_tab_list_view.h"',
        ),
        Replacement(
            old='#include "maho/browser/ui/views/sidebar/maho_sidebar_tab_list_view.h"\n',
            new=(
                '#include "maho/browser/ui/views/sidebar/maho_sidebar_tab_list_view.h"\n'
                '#include "maho/browser/ui/views/maho_lucide_icons/vector_icons.h"\n'
            ),
            description=(
                "Maho R-12: include the generated Lucide vector icons header in "
                "browser_view.cc for the private Glasses identity"
            ),
            guard='#include "maho/browser/ui/views/maho_lucide_icons/vector_icons.h"',
        ),
        Replacement(
            old=(
                "  if (GetIsNormalType()) {\n"
                "    maho_sidebar_container_ = AddChildView(\n"
                "        std::make_unique<maho::MahoSidebarContainerView>(browser_.get()));\n"
            ),
            new=(
                "  if (GetIsNormalType()) {\n"
                "    maho_sidebar_container_ = AddChildView(\n"
                "        std::make_unique<maho::MahoSidebarContainerView>(browser_.get()));\n"
                "    // Maho R-12: exact primary Incognito injects the decorative\n"
                "    // Glasses identity and fixed private semantic colors into the\n"
                "    // already-built sidebar. Regular/Guest/other-OTR are untouched.\n"
                "    if (Profile* maho_private_profile = browser_->profile();\n"
                "        maho_private_profile &&\n"
                "        maho_private_profile->IsIncognitoProfile() &&\n"
                "        maho_private_profile->IsPrimaryOTRProfile()) {\n"
                "      if (auto* maho_private_container =\n"
                "              views::AsViewClass<maho::MahoSidebarContainerView>(\n"
                "                  maho_sidebar_container_)) {\n"
                "        maho_private_container->SetPrivateAppearance(\n"
                "            ui::ImageModel::FromVectorIcon(\n"
                "                maho_lucide_icons::kGlassesIcon,\n"
                "                kMahoColorPrivateIdentityIcon, /*icon_size=*/20),\n"
                "            kMahoColorPrivateSidebarBackground,\n"
                "            kMahoColorPrivateSidebarText);\n"
                "      }\n"
                "    }\n"
            ),
            description=(
                "Maho R-12: call MahoSidebarContainerView::SetPrivateAppearance "
                "with the generated Glasses icon + fixed private color ids for "
                "exact primary Incognito windows"
            ),
            guard="maho_private_container->SetPrivateAppearance(",
        ),
        Replacement(
            old='#include "maho/browser/ui/tabs/maho_mru_tab_tracker.h"\n',
            new=(
                '#include "maho/browser/ui/tabs/maho_mru_tab_tracker.h"\n'
                '#include "maho/browser/ui/tabs/maho_ctrl_tab_switcher_controller.h"\n'
            ),
            description=(
                "Include MahoCtrlTabSwitcherController header in browser_view.cc"
            ),
            guard='#include "maho/browser/ui/tabs/maho_ctrl_tab_switcher_controller.h"',
        ),
        Replacement(
            old=(
                "    maho_create_space_blank_view_ =\n"
                "        AddChildView(std::make_unique<MahoCreateSpaceBlankView>());\n"
                "    maho_create_space_blank_view_->SetVisible(false);\n"
                "  }\n"
            ),
            new=(
                "    maho_create_space_blank_view_ =\n"
                "        AddChildView(std::make_unique<MahoCreateSpaceBlankView>());\n"
                "    maho_create_space_blank_view_->SetVisible(false);\n"
                "\n"
                "    if (auto* strip = browser_->tab_strip_model()) {\n"
                "      maho_mru_tab_tracker_ =\n"
                "          std::make_unique<maho::MahoMruTabTracker>(strip);\n"
                "    }\n"
                "  }\n"
            ),
            description=(
                "Initialize MahoMruTabTracker in BrowserView constructor for "
                "normal browser windows; the tracker observes the TabStripModel "
                "for MRU ordering from window creation onward"
            ),
            guard="maho_mru_tab_tracker_ =",
        ),
        Replacement(
            old=(
                "    if (auto* strip = browser_->tab_strip_model()) {\n"
                "      maho_mru_tab_tracker_ =\n"
                "          std::make_unique<maho::MahoMruTabTracker>(strip);\n"
                "    }\n"
                "  }\n"
            ),
            new=(
                "    if (auto* strip = browser_->tab_strip_model()) {\n"
                "      maho_mru_tab_tracker_ =\n"
                "          std::make_unique<maho::MahoMruTabTracker>(strip);\n"
                "      maho_ctrl_tab_switcher_controller_ =\n"
                "          std::make_unique<maho::MahoCtrlTabSwitcherController>(\n"
                "              this, maho_mru_tab_tracker_.get());\n"
                "    }\n"
                "    if (browser_->profile()) {\n"
                "      maho::MahoTabIdSessionHelper::GetForProfile(browser_->profile());\n"
                "      maho::MahoTabPreviewCapture::GetInstance();\n"
                "    }\n"
                "  }\n"
            ),
            description=(
                "Initialize MahoCtrlTabSwitcherController alongside the tracker "
                "in the BrowserView constructor; controller lifetime is tied "
                "to the browser window"
            ),
            guard="maho_ctrl_tab_switcher_controller_ =",
        ),
        Replacement(
            old=(
                "maho::MahoCommandOverlayController*\n"
                "BrowserView::GetOrCreateMahoCommandOverlayController() {\n"
            ),
            new=(
                "bool BrowserView::MaybeHandleMruTabSwitch(bool forward) {\n"
                "  if (!maho_ctrl_tab_switcher_controller_) {\n"
                "    return false;\n"
                "  }\n"
                "  return maho_ctrl_tab_switcher_controller_->HandleAdvance(forward);\n"
                "}\n"
                "\n"
                "maho::MahoCommandOverlayController*\n"
                "BrowserView::GetOrCreateMahoCommandOverlayController() {\n"
            ),
            description=(
                "Implement BrowserView::MaybeHandleMruTabSwitch delegating to "
                "MahoCtrlTabSwitcherController::HandleAdvance which owns the "
                "full state machine (200ms fast-path, overlay show/close, "
                "cursor advance, Ctrl-release commit)"
            ),
            guard="bool BrowserView::MaybeHandleMruTabSwitch",
        ),
        Replacement(
            old=(
                "maho::MahoCommandOverlayController*\n"
                "BrowserView::GetOrCreateMahoCommandOverlayController() {\n"
            ),
            new=(
                "bool BrowserView::MaybeHandleMultiTabClose() {\n"
                "  auto* container = views::AsViewClass<maho::MahoSidebarContainerView>(\n"
                "      maho_sidebar_container());\n"
                "  if (!container) {\n"
                "    return false;\n"
                "  }\n"
                "  auto* sidebar = views::AsViewClass<maho::MahoSidebarView>(\n"
                "      container->sidebar_view());\n"
                "  if (!sidebar || !sidebar->tab_list_view() ||\n"
                "      !sidebar->tab_list_view()->HasMultiSelection()) {\n"
                "    return false;\n"
                "  }\n"
                "  sidebar->tab_list_view()->CloseSelected();\n"
                "  return true;\n"
                "}\n"
                "\n"
                "maho::MahoCommandOverlayController*\n"
                "BrowserView::GetOrCreateMahoCommandOverlayController() {\n"
            ),
            description=(
                "Implement BrowserView::MaybeHandleMultiTabClose so Cmd+W closes "
                "all sidebar multi-selected tabs together"
            ),
            guard="bool BrowserView::MaybeHandleMultiTabClose()",
        ),
        Replacement(
            old=(
                "bool BrowserView::MaybeHandleMruTabSwitch(bool forward) {\n"
                "  // Phase 1 stub: if the tracker has at least two live tabs,\n"
                "  // activate MRU[1] (the previously active tab) directly. This\n"
                "  // gives us basic Ctrl+Tab MRU behavior end-to-end while the\n"
                "  // overlay controller and state machine land in later phases.\n"
                "  //\n"
                "  // TODO(maho): route this through MahoCtrlTabSwitcherController\n"
                "  // once Phase 3 lands (200ms delay + preview overlay + Ctrl\n"
                "  // keyup commit).\n"
                "  if (!maho_mru_tab_tracker_) {\n"
                "    return false;\n"
                "  }\n"
                "  std::vector<content::WebContents*> mru =\n"
                "      maho_mru_tab_tracker_->GetMruList();\n"
                "  if (mru.size() < 2) {\n"
                "    return false;\n"
                "  }\n"
                "  content::WebContents* target = forward ? mru[1] : mru.back();\n"
                "  TabStripModel* strip = browser_->tab_strip_model();\n"
                "  if (!strip) {\n"
                "    return false;\n"
                "  }\n"
                "  const int index = strip->GetIndexOfWebContents(target);\n"
                "  if (index == TabStripModel::kNoTab) {\n"
                "    return false;\n"
                "  }\n"
                "  strip->ActivateTabAt(\n"
                "      index,\n"
                "      TabStripUserGestureDetails(\n"
                "          TabStripUserGestureDetails::GestureType::kKeyboard));\n"
                "  return true;\n"
                "}\n"
            ),
            new=(
                "bool BrowserView::MaybeHandleMruTabSwitch(bool forward) {\n"
                "  if (!maho_ctrl_tab_switcher_controller_) {\n"
                "    return false;\n"
                "  }\n"
                "  return maho_ctrl_tab_switcher_controller_->HandleAdvance(forward);\n"
                "}\n"
            ),
            description=(
                "Migrate MaybeHandleMruTabSwitch body from Phase 1 stub to "
                "Phase 3 controller delegation (existing chromium/src trees "
                "still have the stub applied)"
            ),
            idempotent=True,
        ),
        Replacement(
            old=(
                "  void OnThemeChanged() override {\n"
                "    views::View::OnThemeChanged();\n"
                "    const ui::ColorProvider* color_provider = GetColorProvider();\n"
                "    if (color_provider && layer()) {\n"
                "      layer()->SetColor(color_provider->GetColor(kMahoColorContentBackground));\n"
                "    }\n"
                "    SchedulePaint();\n"
                "  }\n"
            ),
            new=(
                "  void OnThemeChanged() override {\n"
                "    views::View::OnThemeChanged();\n"
                "    SchedulePaint();\n"
                "  }\n"
            ),
            description=(
                "Migrate old MahoCreateSpaceBlankView::OnThemeChanged() body "
                "(unsafe layer()->SetColor path) to the new safe 3-line version "
                "that only calls SchedulePaint()"
            ),
            idempotent=True,
        ),
        Replacement(
            old='#include "maho/browser/ui/theme/maho_color_id.h"\n',
            new=(
                '#include "maho/browser/ui/theme/maho_color_id.h"\n'
                '#include "cc/paint/paint_flags.h"\n'
                '#include "cc/paint/paint_shader.h"\n'
            ),
            description="Include cc paint headers in browser_view.cc for space theme blank view custom gradient",
        ),
        Replacement(
            old=(
                "\n"
                "class MahoCreateSpaceBlankView : public views::View {\n"
                " public:\n"
                "  MahoCreateSpaceBlankView() {\n"
                "    SetPaintToLayer();\n"
                "    layer()->SetName(\"MahoCreateSpaceBlankView\");\n"
                "    layer()->SetFillsBoundsOpaquely(true);\n"
                "  }\n"
                "  ~MahoCreateSpaceBlankView() override = default;\n"
                "\n"
                "  void OnThemeChanged() override {\n"
                "    views::View::OnThemeChanged();\n"
                "    const ui::ColorProvider* color_provider = GetColorProvider();\n"
                "    if (color_provider && layer()) {\n"
                "      layer()->SetColor(color_provider->GetColor(kMahoColorContentBackground));\n"
                "    }\n"
                "    SchedulePaint();\n"
                "  }\n"
                "\n"
                "  void OnPaint(gfx::Canvas* canvas) override {\n"
                "    const gfx::Rect bounds = GetContentsBounds();\n"
                "    if (bounds.IsEmpty()) {\n"
                "      return;\n"
                "    }\n"
                "\n"
                "    const ui::ColorProvider* color_provider = GetColorProvider();\n"
                "    if (!color_provider) {\n"
                "      return;\n"
                "    }\n"
                "\n"
                "    const SkColor color_top = color_provider->GetColor(kMahoColorContentGradientTop);\n"
                "    const SkColor color_bottom = color_provider->GetColor(kMahoColorContentGradientBottom);\n"
                "\n"
                "    const SkPoint points[2] = {\n"
                "        {0, 0},\n"
                "        {0, static_cast<float>(bounds.height())}};\n"
                "    const SkColor4f colors[2] = {\n"
                "        SkColor4f::FromColor(color_top),\n"
                "        SkColor4f::FromColor(color_bottom)};\n"
                "\n"
                "    cc::PaintFlags flags;\n"
                "    flags.setAntiAlias(true);\n"
                "    flags.setStyle(cc::PaintFlags::kFill_Style);\n"
                "    flags.setShader(cc::PaintShader::MakeLinearGradient(\n"
                "        points, colors, nullptr, 2, SkTileMode::kClamp));\n"
                "    canvas->DrawRect(bounds, flags);\n"
                "  }\n"
                "};\n"
            ),
            new="",
            description=(
                "Remove legacy MahoCreateSpaceBlankView block (no METADATA_HEADER) "
                "that was injected by an earlier version of this script; the current "
                "version injects the block with METADATA_HEADER instead"
            ),
            idempotent=True,
        ),
        Replacement(
            old=(
                "using web_modal::WebContentsModalDialogHost;\n"
                "\n"
                "namespace {\n"
            ),
            new=(
                "using web_modal::WebContentsModalDialogHost;\n"
                "\n"
                "namespace {\n"
                "\n"
                "class MahoCreateSpaceBlankView : public views::View {\n"
                "  METADATA_HEADER(MahoCreateSpaceBlankView, views::View)\n"
                " public:\n"
                "  MahoCreateSpaceBlankView() {\n"
                "    SetPaintToLayer();\n"
                "    layer()->SetName(\"MahoCreateSpaceBlankView\");\n"
                "    layer()->SetFillsBoundsOpaquely(true);\n"
                "  }\n"
                "  ~MahoCreateSpaceBlankView() override = default;\n"
                "\n"
                "  void OnThemeChanged() override {\n"
                "    views::View::OnThemeChanged();\n"
                "    SchedulePaint();\n"
                "  }\n"
                "\n"
                "  void OnPaint(gfx::Canvas* canvas) override {\n"
                "    const gfx::Rect bounds = GetContentsBounds();\n"
                "    if (bounds.IsEmpty()) {\n"
                "      return;\n"
                "    }\n"
                "\n"
                "    const ui::ColorProvider* color_provider = GetColorProvider();\n"
                "    if (!color_provider) {\n"
                "      return;\n"
                "    }\n"
                "\n"
                "    const SkColor color_top = color_provider->GetColor(kMahoColorContentGradientTop);\n"
                "    const SkColor color_bottom = color_provider->GetColor(kMahoColorContentGradientBottom);\n"
                "\n"
                "    const SkPoint points[2] = {\n"
                "        {0, 0},\n"
                "        {0, static_cast<float>(bounds.height())}};\n"
                "    const SkColor4f colors[2] = {\n"
                "        SkColor4f::FromColor(color_top),\n"
                "        SkColor4f::FromColor(color_bottom)};\n"
                "\n"
                "    cc::PaintFlags flags;\n"
                "    flags.setAntiAlias(true);\n"
                "    flags.setStyle(cc::PaintFlags::kFill_Style);\n"
                "    flags.setShader(cc::PaintShader::MakeLinearGradient(\n"
                "        points, colors, nullptr, 2, SkTileMode::kClamp));\n"
                "    canvas->DrawRect(bounds, flags);\n"
                "  }\n"
                "};\n"
                "\n"
                "BEGIN_METADATA(MahoCreateSpaceBlankView)\n"
                "END_METADATA\n"
            ),
            description="Add MahoCreateSpaceBlankView subclass for gradient background rendering",
            guard="  METADATA_HEADER(MahoCreateSpaceBlankView, views::View)\n",
        ),
        Replacement(
            old=(
                "    canvas->DrawRect(bounds, flags);\n"
                "  }\n"
                "};\n"
            ),
            new=(
                "    canvas->DrawRect(bounds, flags);\n"
                "  }\n"
                "};\n"
                "\n"
                "BEGIN_METADATA(MahoCreateSpaceBlankView)\n"
                "END_METADATA\n"
            ),
            description="Add BEGIN_METADATA/END_METADATA for MahoCreateSpaceBlankView (required by METADATA_HEADER)",
            guard="BEGIN_METADATA(MahoCreateSpaceBlankView)\n",
        ),
        Replacement(
            old=(
                "    maho_create_space_blank_view_ =\n"
                "        AddChildView(std::make_unique<views::View>());\n"
                "    maho_create_space_blank_view_->SetPaintToLayer();\n"
                "    maho_create_space_blank_view_->layer()->SetName(\n"
                "        \"MahoCreateSpaceBlankView\");\n"
                "    maho_create_space_blank_view_->layer()->SetFillsBoundsOpaquely(true);\n"
                "    maho_create_space_blank_view_->SetBackground(\n"
                "        views::CreateSolidBackground(kMahoColorContentBackground));\n"
                "    maho_create_space_blank_view_->SetVisible(false);\n"
            ),
            new=(
                "    maho_create_space_blank_view_ =\n"
                "        AddChildView(std::make_unique<MahoCreateSpaceBlankView>());\n"
                "    maho_create_space_blank_view_->SetVisible(false);\n"
            ),
            description="Use MahoCreateSpaceBlankView for blank view instead of generic views::View",
        ),
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
        Replacement(
            old=(
                "void BrowserView::ShowMahoCommandOverlayForNewTab() {\n"
                "  auto* controller = GetOrCreateMahoCommandOverlayController();\n"
                "  if (!controller) {\n"
                "    return;\n"
                "  }\n"
                "\n"
                "  controller->Show(this, maho::CommandOverlayMode::kNewTab);\n"
                "}\n"
            ),
            new=(
                "void BrowserView::ShowMahoCommandOverlayForNewTab() {\n"
                "  if (!GetIsNormalType()) {\n"
                "    chrome::NewTab(browser_);\n"
                "    return;\n"
                "  }\n"
                "\n"
                "  if (!initialized_ || !GetWidget()) {\n"
                "    pending_maho_command_overlay_new_tab_ = true;\n"
                "    if (GetWidget()) {\n"
                "      base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(\n"
                "          FROM_HERE,\n"
                "          base::BindOnce(\n"
                "              &BrowserView::MaybeShowPendingMahoCommandOverlayForNewTab,\n"
                "              weak_ptr_factory_.GetWeakPtr()));\n"
                "    }\n"
                "    return;\n"
                "  }\n"
                "\n"
                "  auto* controller = GetOrCreateMahoCommandOverlayController();\n"
                "  if (!controller) {\n"
                "    return;\n"
                "  }\n"
                "\n"
                "  controller->Show(this, maho::CommandOverlayMode::kNewTab);\n"
                "}\n"
                "\n"
                "void BrowserView::MaybeShowPendingMahoCommandOverlayForNewTab() {\n"
                "  if (!pending_maho_command_overlay_new_tab_ || !initialized_ ||\n"
                "      !GetWidget()) {\n"
                "    return;\n"
                "  }\n"
                "  pending_maho_command_overlay_new_tab_ = false;\n"
                "  ShowMahoCommandOverlayForNewTab();\n"
                "}\n"
            ),
            description=(
                "Defer Cmd+T command palette opening until BrowserView has a "
                "widget, fixing immediate-startup shortcut drops"
            ),
            guard="MaybeShowPendingMahoCommandOverlayForNewTab",
        ),
        Replacement(
            old=(
                "  dialog_anchor_ = std::make_unique<user_education::ViewSubregionAnchor>(\n"
                "      kBrowserDialogAnchorElementId, *this);\n"
                "\n"
                "  initialized_ = true;\n"
                "}\n"
            ),
            new=(
                "  dialog_anchor_ = std::make_unique<user_education::ViewSubregionAnchor>(\n"
                "      kBrowserDialogAnchorElementId, *this);\n"
                "\n"
                "  initialized_ = true;\n"
                "  if (pending_maho_command_overlay_new_tab_) {\n"
                "    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(\n"
                "        FROM_HERE,\n"
                "        base::BindOnce(\n"
                "            &BrowserView::MaybeShowPendingMahoCommandOverlayForNewTab,\n"
                "            weak_ptr_factory_.GetWeakPtr()));\n"
                "  }\n"
                "}\n"
            ),
            description=(
                "Flush pending Maho new-tab command palette request after "
                "BrowserView finishes widget initialization"
            ),
            guard="pending_maho_command_overlay_new_tab_)",
        ),
        # ── key-hook cleanup: remove stale AddPreTargetHandler/RemovePreTargetHandler
        # and own the OnKeyEvent Maho shortcut interception ────────────────────────
        Replacement(
            old=(
                "  views::ClientView::AddedToWidget();\n"
                "  GetWidget()->AddPreTargetHandler(this);\n"
            ),
            new=(
                "  views::ClientView::AddedToWidget();\n"
            ),
            description=(
                "Remove stale GetWidget()->AddPreTargetHandler(this) from "
                "AddedToWidget (API removed from views::Widget)"
            ),
            idempotent=True,
        ),
        Replacement(
            old=(
                "void BrowserView::RemovedFromWidget() {\n"
                "  CHECK(GetFocusManager());\n"
                "  focus_manager_observation_.Reset();\n"
                "  if (GetWidget()) {\n"
                "    GetWidget()->RemovePreTargetHandler(this);\n"
                "  }\n"
                "}\n"
            ),
            new=(
                "void BrowserView::RemovedFromWidget() {\n"
                "  CHECK(GetFocusManager());\n"
                "  focus_manager_observation_.Reset();\n"
                "}\n"
            ),
            description=(
                "Remove stale RemovePreTargetHandler from RemovedFromWidget "
                "(API removed from views::Widget)"
            ),
            idempotent=True,
        ),
        Replacement(
            old="  if (event->type() == ui::ET_KEY_PRESSED) {\n",
            new="  if (event->type() == ui::EventType::kKeyPressed) {\n",
            description=(
                "Update stale ui::ET_KEY_PRESSED to ui::EventType::kKeyPressed "
                "in BrowserView::OnKeyEvent"
            ),
            idempotent=True,
        ),
        Replacement(
            old=(
                "void BrowserView::OnKeyEvent(ui::KeyEvent* event) {\n"
                "  views::ClientView::OnKeyEvent(event);\n"
                "}\n"
            ),
            new=(
                "void BrowserView::OnKeyEvent(ui::KeyEvent* event) {\n"
                "  if (event->type() == ui::EventType::kKeyPressed) {\n"
                "    std::string action_id =\n"
                "        maho::MahoShortcutInterceptor::ResolveActionForKeyEvent(*event);\n"
                "    if (!action_id.empty()) {\n"
                "      maho::ExecuteCommandAction(browser(), action_id);\n"
                "      event->SetHandled();\n"
                "      return;\n"
                "    }\n"
                "  }\n"
                "  views::ClientView::OnKeyEvent(event);\n"
                "}\n"
            ),
            description="Add Maho shortcut interceptor to BrowserView::OnKeyEvent",
            guard="maho::MahoShortcutInterceptor::ResolveActionForKeyEvent",
        ),
        # ── [Maho fix] WebView fast_resize race defensive cleanup ────────────
        # See chromium_src overlay browser_view.cc section 13 for the
        # full rationale.  TL;DR: upstream BrowserView::ToolbarSizeChanged()
        # has an explicit comment about a "gray rect because the clip wasn't
        # updated" race.  Maho hits intermittent variants of this race at
        # startup / session-restore — visible as the Settings tab being
        # vertically clipped (gray below content) and the AI side panel
        # overflowing its container horizontally.  Both share the same root
        # cause: a views::WebView is left with stale fast_resize state so
        # its underlying WebContents stays rendered at a previous bounds.
        Replacement(
            old='#include "ui/views/view_class_properties.h"\n',
            new=(
                '#include "ui/views/view_class_properties.h"\n'
                '#include "ui/views/view_utils.h"\n'
            ),
            description=(
                "Include ui/views/view_utils.h so BrowserView::"
                "OnWidgetVisibilityChanged can use views::AsViewClass<"
                "views::WebView>() to walk the View tree and reset stale "
                "fast_resize state. Part of the Maho fast_resize race fix."
            ),
            guard='#include "ui/views/view_utils.h"',
        ),
        Replacement(
            old=(
                "void BrowserView::OnWidgetVisibilityChanged(views::Widget* widget,\n"
                "                                            bool visible) {\n"
                "  UpdateLoadingAnimations(visible);\n"
                "}\n"
            ),
            new=(
                "void BrowserView::OnWidgetVisibilityChanged(views::Widget* widget,\n"
                "                                            bool visible) {\n"
                "  UpdateLoadingAnimations(visible);\n"
                "\n"
                "  // [Maho fix] Defensive cleanup for the upstream \"gray rect\"\n"
                "  // / content-overflow race documented in ToolbarSizeChanged().\n"
                "  // A views::WebView can be left with stale fast_resize state\n"
                "  // after animation, infobar, or session-restore transitions,\n"
                "  // leaving the underlying WebContents rendered at its previous\n"
                "  // bounds while the View clips to the new bounds — producing\n"
                "  // a visible gray rect (when the View grew) or content\n"
                "  // overflowing its container (when the View shrank). On every\n"
                "  // visibility-true transition, walk the View tree, force\n"
                "  // fast_resize off on every views::WebView descendant, and\n"
                "  // request a layout so the WebContents tracks the View bounds.\n"
                "  if (visible) {\n"
                "    std::vector<views::View*> stack;\n"
                "    stack.push_back(this);\n"
                "    while (!stack.empty()) {\n"
                "      views::View* node = stack.back();\n"
                "      stack.pop_back();\n"
                "      if (auto* web_view =\n"
                "              views::AsViewClass<views::WebView>(node)) {\n"
                "        web_view->SetFastResize(false);\n"
                "        web_view->InvalidateLayout();\n"
                "      }\n"
                "      for (views::View* child : node->children()) {\n"
                "        stack.push_back(child);\n"
                "      }\n"
                "    }\n"
                "    if (contents_container_) {\n"
                "      contents_container_->DeprecatedLayoutImmediately();\n"
                "    }\n"
                "  }\n"
                "}\n"
            ),
            description=(
                "Defensive cleanup against upstream gray-rect / WebContents "
                "bounds-stuck race. On every visibility-true transition, walk "
                "the View tree, clear fast_resize on every views::WebView, "
                "invalidate their layout, and force an immediate layout of "
                "contents_container_. Covers both the tab WebContents path "
                "(Settings tab being clipped) and the side panel WebUI path "
                "(AI panel overflowing its container)."
            ),
            guard="[Maho fix] Defensive cleanup",
        ),
        # ── Patch #14: Ctrl+1..9 space switching from any focus state ─────────
        Replacement(
            old=(
                "  views::ClientView::AddedToWidget();\n"
                "\n"
                "  widget_observation_.Observe(GetWidget());\n"
            ),
            new=(
                "  views::ClientView::AddedToWidget();\n"
                "\n"
                "  // Maho: Register Ctrl+1..9 as FocusManager accelerators so space-switching\n"
                "  // shortcuts fire regardless of which view has focus. Delegates to\n"
                "  // MahoShortcutInterceptor::ResolveActionForAccelerator to preserve\n"
                "  // Rust ShortcutManager as the single source of truth.\n"
                "  if (IsMahoArcLayoutActive()) {\n"
                "    views::FocusManager* fm = GetFocusManager();\n"
                "    if (fm) {\n"
                "      for (int i = 0; i < 9; ++i) {\n"
                "        fm->RegisterAccelerator(\n"
                "            ui::Accelerator(static_cast<ui::KeyboardCode>(ui::VKEY_1 + i),\n"
                "                            ui::EF_CONTROL_DOWN),\n"
                "            ui::AcceleratorManager::kNormalPriority, this);\n"
                "      }\n"
                "    }\n"
                "  }\n"
                "\n"
                "  widget_observation_.Observe(GetWidget());\n"
            ),
            description=(
                "Register Ctrl+1..9 as FocusManager accelerators in AddedToWidget "
                "so Maho space-switching shortcuts fire regardless of which view "
                "holds focus (fixes shortcuts being dead when the WebContents is "
                "not the first responder, e.g. fresh window, omnibox focus)"
            ),
            guard="Maho: Register Ctrl+1..9 as FocusManager accelerators",
        ),
        Replacement(
            old=(
                "bool BrowserView::AcceleratorPressed(const ui::Accelerator& accelerator) {\n"
                "  int command_id;\n"
            ),
            new=(
                "bool BrowserView::AcceleratorPressed(const ui::Accelerator& accelerator) {\n"
                "  // Maho: Try to dispatch as a Maho shortcut first. This works from any\n"
                "  // focus state (unlike MaybeHandleKeyEvent which needs WebContents focus).\n"
                "  std::string maho_action =\n"
                "      maho::MahoShortcutInterceptor::ResolveActionForAccelerator(accelerator);\n"
                "  if (!maho_action.empty()) {\n"
                "    maho::ExecuteCommandAction(browser_.get(), maho_action);\n"
                "    return true;\n"
                "  }\n"
                "\n"
                "  int command_id;\n"
            ),
            description=(
                "Prepend Maho shortcut dispatch to BrowserView::AcceleratorPressed; "
                "non-Maho accelerators fall through to the existing "
                "FindCommandIdForAccelerator path unchanged"
            ),
            guard="Maho: Try to dispatch as a Maho shortcut first",
        ),
        Replacement(
            old=(
                "BrowserView::~BrowserView() {\n"
                "  // Remove the layout manager to avoid dangling. This needs to be earlier than\n"
                "  // other cleanups that destroy views referenced in the layout manager.\n"
                "  SetLayoutManager(nullptr);\n"
            ),
            new=(
                "BrowserView::~BrowserView() {\n"
                "  // Remove the layout manager to avoid dangling. This needs to be earlier than\n"
                "  // other cleanups that destroy views referenced in the layout manager.\n"
                "  SetLayoutManager(nullptr);\n"
                "\n"
                "  if (auto* core = maho::GetCore()) {\n"
                "    maho_core_window_closed(core, std::to_string(browser_->session_id().id()).c_str());\n"
                "  }\n"
            ),
            description="Call maho_core_window_closed in BrowserView destructor",
            guard="maho_core_window_closed(core",
        ),
    ],
    # ── browser.cc ───────────────────────────────────────────────────────────
    "chrome/browser/ui/browser.cc": [
        Replacement(
            old=(
                '#include "chrome/browser/ui/browser.h"'
            ),
            new=(
                '#include "chrome/browser/ui/browser.h"\n'
                '#include "maho/browser/maho_browser_main_extra_parts.h"\n'
                '#include "maho/browser/ui/views/little_arc/maho_little_arc_window.h"\n'
                '#include "maho/browser/ui/views/sidebar/maho_sidebar_prefs.h"\n'
                '#include "components/prefs/pref_service.h"\n'
                '#include "maho/browser/maho_space_profile_bridge.h"'
            ),
            description="Include Maho headers in browser.cc",
        ),
        Replacement(
            old=(
                "  if (is_type_devtools()) {\n"
                "    DevToolsWindow* window = DevToolsWindow::AsDevToolsWindow(source);\n"
                "    DCHECK(window);\n"
                "    return window->OpenURLFromTab(source, params,\n"
                "                                  std::move(navigation_handle_callback));\n"
                "  }\n"
            ),
            new=(
                "  if (is_type_devtools()) {\n"
                "    DevToolsWindow* window = DevToolsWindow::AsDevToolsWindow(source);\n"
                "    DCHECK(window);\n"
                "    return window->OpenURLFromTab(source, params,\n"
                "                                  std::move(navigation_handle_callback));\n"
                "  }\n"
                "\n"
                "#if BUILDFLAG(IS_MAC)\n"
                "  if (params.disposition == WindowOpenDisposition::NEW_SPLIT_VIEW &&\n"
                "      !params.started_from_context_menu &&\n"
                "      ui::PageTransitionCoreTypeIs(params.transition, ui::PAGE_TRANSITION_LINK)) {\n"
                "    bool override_enabled = profile()->GetPrefs()->GetBoolean(\n"
                "        maho::sidebar_prefs::kLittleArcClickOverrideEnabled);\n"
                "    if (override_enabled) {\n"
                "      maho::LaunchLittleArc(profile(), maho::LittleArcRequest{params.url});\n"
                "      return nullptr;\n"
                "    } else {\n"
                "      content::OpenURLParams demoted_params = params;\n"
                "      demoted_params.disposition = WindowOpenDisposition::NEW_BACKGROUND_TAB;\n"
                "      return OpenURLFromTab(source, demoted_params, std::move(navigation_handle_callback));\n"
                "    }\n"
                "  }\n"
                "\n"
                "  struct AtcBypassState {\n"
                "    static bool ShouldBypass(Browser* browser) {\n"
                "      static std::set<Browser*>* bypassed_browsers = new std::set<Browser*>();\n"
                "      return bypassed_browsers->count(browser) > 0;\n"
                "    }\n"
                "    static void SetBypass(Browser* browser, bool bypass) {\n"
                "      static std::set<Browser*>* bypassed_browsers = new std::set<Browser*>();\n"
                "      if (bypass) {\n"
                "        bypassed_browsers->insert(browser);\n"
                "      } else {\n"
                "        bypassed_browsers->erase(browser);\n"
                "      }\n"
                "    }\n"
                "  };\n"
                "\n"
                "  if (!AtcBypassState::ShouldBypass(this) && maho::MahoAtcState::HasEnabledRules()) {\n"
                "    bool is_top_level = ui::PageTransitionIsMainFrame(params.transition) &&\n"
                "                        !ui::PageTransitionIsRedirect(params.transition);\n"
                "    bool is_user_initiated = !params.is_renderer_initiated || params.user_gesture;\n"
                "    bool is_little_arc_gesture = (params.disposition == WindowOpenDisposition::NEW_SPLIT_VIEW &&\n"
                "                                  !params.started_from_context_menu &&\n"
                "                                  ui::PageTransitionCoreTypeIs(params.transition, ui::PAGE_TRANSITION_LINK));\n"
                "    if (is_top_level && is_user_initiated && !is_little_arc_gesture) {\n"
                "      std::string current_space_id;\n"
                "      maho::MahoSpaceProfileBridge* bridge = maho::MahoSpaceProfileBridge::GetInstance();\n"
                "      if (bridge) {\n"
                "        current_space_id = bridge->GetActiveSpaceId(this);\n"
                "      }\n"
                "      base::WeakPtr<Browser> weak_browser = AsWeakPtr();\n"
                "      Profile* prof = profile();\n"
                "      base::WeakPtr<Profile> weak_profile = prof ? prof->GetWeakPtr() : nullptr;\n"
                "      base::WeakPtr<content::WebContents> weak_source = source ? source->GetWeakPtr() : nullptr;\n"
                "      \n"
                "      maho::DecideLinkDestination(\n"
                "          params.url, /*is_external=*/false,\n"
                "          base::BindOnce(\n"
                "              [](base::WeakPtr<Browser> browser,\n"
                "                 base::WeakPtr<Profile> profile,\n"
                "                 base::WeakPtr<content::WebContents> source,\n"
                "                 std::string current_space_id,\n"
                "                 content::OpenURLParams params,\n"
                "                 base::OnceCallback<void(content::NavigationHandle&)> navigation_handle_callback,\n"
                "                 maho::LinkDestinationResult result) {\n"
                "                if (!browser || !profile) return;\n"
                "                \n"
                "                if (result.type == maho::LinkDestinationType::kSpace &&\n"
                "                    !result.space_id.empty() &&\n"
                "                    result.space_id != current_space_id) {\n"
                "                  maho::MahoSpaceProfileBridge* bridge = maho::MahoSpaceProfileBridge::GetInstance();\n"
                "                  if (bridge) {\n"
                "                    bridge->SwitchToSpace(browser.get(), result.space_id);\n"
                "                    // R-9: AddSelectedTabWithURL creates a new tab in the target space.\n"
                "                    // The callback is intentionally not propagated across space boundaries as a new tab is created.\n"
                "                    chrome::AddSelectedTabWithURL(browser.get(), params.url, params.transition);\n"
                "                  }\n"
                "                } else {\n"
                "                  AtcBypassState::SetBypass(browser.get(), true);\n"
                "                  browser->OpenURL(params, std::move(navigation_handle_callback));\n"
                "                  AtcBypassState::SetBypass(browser.get(), false);\n"
                "                }\n"
                "              },\n"
                "              weak_browser, weak_profile, weak_source, current_space_id, params,\n"
                "              std::move(navigation_handle_callback)));\n"
                "      return nullptr;\n"
                "    }\n"
                "  }\n"
                "#endif\n"
            ),
            description="Intercept in-browser top-level user-initiated navigations and route to space if ATC matches",
            guard="AtcBypassState::ShouldBypass",
        ),
        Replacement(
            old=(
                "  // BrowserWindowFeatures need to be initialized before browser window\n"
                "  // creation, so that the features can be used in creating components\n"
                "  // in browser window.\n"
                "  features_ = std::make_unique<BrowserWindowFeatures>();"
            ),
            new=(
                "  if (auto* maho_parts = MahoBrowserMainExtraParts::GetInstance()) {\n"
                "    maho_parts->MaybeInitializeForBrowser(this);\n"
                "  }\n\n"
                "  // BrowserWindowFeatures need to be initialized before browser window\n"
                "  // creation, so that the features can be used in creating components\n"
                "  // in browser window.\n"
                "  features_ = std::make_unique<BrowserWindowFeatures>();"
            ),
            description="Call MaybeInitializeForBrowser before features Init",
        ),
        Replacement(
            old=(
                "void Browser::TabStripEmpty() {\n"
                "  // Note: even though the tab strip is empty, the call to Close() may not\n"
                "  // result in closing this Browser. This can happen in the case of closing\n"
                "  // the last Browser with ongoing downloads.\n"
                "  window_->Close();\n"
                "}"
            ),
            new=(
                "void Browser::TabStripEmpty() {\n"
                "  // Maho: normal browser windows support zero-tab state.  Only close the\n"
                "  // window when we are already in an explicit close flow (i.e.\n"
                "  // UnloadController has already set is_attempting_to_close_browser_ via\n"
                "  // TryToCloseWindow / GetBrowserClosingStatus).  For every other browser\n"
                "  // type the original behaviour is preserved.\n"
                "  if (is_type_normal() &&\n"
                "      !unload_controller_.is_attempting_to_close_browser()) {\n"
                "    return;\n"
                "  }\n"
                "  // Note: even though the tab strip is empty, the call to Close() may not\n"
                "  // result in closing this Browser. This can happen in the case of closing\n"
                "  // the last Browser with ongoing downloads.\n"
                "  window_->Close();\n"
                "}"
            ),
            description=(
                "Maho zero-tab: do not close a normal browser window when the last tab is "
                "removed unless an explicit close flow is already in progress "
                "(is_attempting_to_close_browser_ == true)"
            ),
        ),
    ],
    # ── unload_controller.cc ─────────────────────────────────────────────────
    "chrome/browser/ui/unload_controller.cc": [
        Replacement(
            old=(
                "void UnloadController::TabStripEmpty() {\n"
                "  // Set is_attempting_to_close_browser_ here, so that extensions, etc, do not\n"
                "  // attempt to add tabs to the browser before it closes.\n"
                "  is_attempting_to_close_browser_ = true;\n"
                "}"
            ),
            new=(
                "void UnloadController::TabStripEmpty() {\n"
                "  // Maho zero-tab: for normal browser windows the tab strip becoming empty\n"
                "  // does not mean the window is closing — the window survives with zero tabs\n"
                "  // so new tabs can be inserted again.  Skip marking the browser as\n"
                "  // attempting-to-close so that extensions and other callers can still open\n"
                "  // new tabs.  The flag is already set by TryToCloseWindow /\n"
                "  // GetBrowserClosingStatus when the user actually closes the window, so\n"
                "  // unload events are still fired correctly in that path.\n"
                "  if (browser_->is_type_normal() && !is_attempting_to_close_browser_) {\n"
                "    return;\n"
                "  }\n"
                "  // Set is_attempting_to_close_browser_ here, so that extensions, etc, do not\n"
                "  // attempt to add tabs to the browser before it closes.\n"
                "  is_attempting_to_close_browser_ = true;\n"
                "}"
            ),
            description=(
                "Maho zero-tab: do not set is_attempting_to_close_browser_ in TabStripEmpty() "
                "for a normal browser that is not already in an explicit close flow, so the "
                "empty window stays open and accepts new tabs"
            ),
        ),
    ],
    # ── startup_browser_creator_impl.cc ──────────────────────────────────────
    "chrome/browser/ui/startup/startup_browser_creator_impl.cc": [
        Replacement(
            old="#include <iterator>\n",
            new=(
                "#include <atomic>\n"
                "#include <iterator>\n"
            ),
            description=(
                "Maho cold-start: include <atomic> for the process-global once-guard "
                "that auto-opens the command palette on a zero-tab launch"
            ),
            guard="#include <atomic>",
        ),
        Replacement(
            old='#include "chrome/browser/ui/browser_window.h"\n',
            new=(
                '#include "chrome/browser/ui/browser_window.h"\n'
                '#include "chrome/browser/ui/views/frame/browser_view.h"\n'
            ),
            description=(
                "Maho cold-start: include BrowserView so the zero-tab branch can reach "
                "ShowMahoCommandOverlayForNewTab() via GetBrowserViewForBrowser()"
            ),
            guard='#include "chrome/browser/ui/views/frame/browser_view.h"',
        ),
        Replacement(
            old="  bool prefs_tabs_originally_empty = prefs_tabs.empty();\n",
            new="",
            description=(
                "Maho zero-tab: remove the now-unused prefs_tabs_originally_empty tracking "
                "variable whose only consumer (the NTP-append block) was deleted by the "
                "zero-tab patch, suppressing the -Wunused-variable build error"
            ),
            idempotent=True,
        ),
        Replacement(
            old=(
                "    // Potentially add the New Tab Page.\n"
                "    // Note that URLs from preferences are explicitly meant to override showing\n"
                "    // the NTP.\n"
                "    if (prefs_tabs_originally_empty) {\n"
                "      AppendTabs(provider.GetNewTabPageTabs(*command_line_, profile_), &tabs);\n"
                "    }\n"
            ),
            new=(
                "    // Maho zero-tab: suppress the default NTP tab that upstream appends when\n"
                "    // the prefs tab list is empty.  Leaving |tabs| empty here lets the caller\n"
                "    // (RestoreOrCreateBrowser) open a zero-tab window instead of navigating\n"
                "    // to chrome://newtab.\n"
            ),
            description=(
                "Maho zero-tab: in DetermineStartupTabs(), do not append GetNewTabPageTabs() "
                "when prefs_tabs_originally_empty so the tab list stays empty and the "
                "zero-tab RestoreOrCreateBrowser branch can fire"
            ),
        ),
        Replacement(
            old=(
                "  base::AutoReset<bool> synchronous_launch_resetter(\n"
                "      &StartupBrowserCreator::in_synchronous_profile_launch_, true);\n"
                "\n"
                "  // OpenTabsInBrowser requires at least one tab be passed. As a fallback to\n"
                "  // prevent a crash, use the NTP if |tabs| is empty. This could happen if\n"
                "  // we expected a session restore to happen but it did not occur/succeed.\n"
                "  browser = OpenTabsInBrowser(\n"
                "      browser, process_startup,\n"
                "      (tabs.empty()\n"
                "           ? StartupTabs({StartupTab(chrome::ChromeUINewTabURLAsGURL())})\n"
                "           : tabs),\n"
                "      (behavior == BrowserOpenBehavior::USE_EXISTING_AND_OVERWRITE_ACTIVE_TAB\n"
                "           ? (TabOverWrite::kYes)\n"
                "           : (TabOverWrite::kNo)));"
            ),
            new=(
                "  base::AutoReset<bool> synchronous_launch_resetter(\n"
                "      &StartupBrowserCreator::in_synchronous_profile_launch_, true);\n"
                "\n"
                "  // Maho zero-tab: when the tab list is empty and we are targeting a normal\n"
                "  // browser window, create/show the browser without synthesising an NTP tab.\n"
                "  // OpenTabsInBrowser DCHECKs on an empty tab list, so we must not call it\n"
                "  // in this path.\n"
                "  if (tabs.empty()) {\n"
                "    // Ensure we have a browser to show.  Reuse the existing one if\n"
                "    // USE_EXISTING selected one above, otherwise create a fresh normal window.\n"
                "    // But skip this if login gate is active.\n"
                "    bool gate_active = false;\n"
                "    if (profile_ && profile_->GetPrefs()) {\n"
                "      gate_active = profile_->GetPrefs()->GetBoolean(\"maho.auth.login_gate_active\");\n"
                "    }\n"
                "    if (!gate_active) {\n"
                "      if (!browser || !browser->is_type_normal()) {\n"
                "        CHECK(profile_);\n"
                "        if (Browser::GetCreationStatusForProfile(profile_) ==\n"
                "            Browser::CreationStatus::kOk) {\n"
                "          Browser::CreateParams params =\n"
                "              Browser::CreateParams(profile_, false);\n"
                "          params.creation_source =\n"
                "              Browser::CreationSource::kStartupCreator;\n"
                "          browser = Browser::Create(params);\n"
                "        }\n"
                "      }\n"
                "      if (browser) {\n"
                "        browser->window()->Show();\n"
                "      }\n"
                "    }\n"
                "  } else {\n"
                "  // OpenTabsInBrowser requires at least one tab be passed. As a fallback to\n"
                "  // prevent a crash, use the NTP if |tabs| is empty. This could happen if\n"
                "  // we expected a session restore to happen but it did not occur/succeed.\n"
                "  browser = OpenTabsInBrowser(\n"
                "      browser, process_startup,\n"
                "      tabs,\n"
                "      (behavior == BrowserOpenBehavior::USE_EXISTING_AND_OVERWRITE_ACTIVE_TAB\n"
                "           ? (TabOverWrite::kYes)\n"
                "           : (TabOverWrite::kNo)));\n"
                "  }"
            ),
            description=(
                "Maho zero-tab: in RestoreOrCreateBrowser, when tabs is empty create/show a "
                "normal browser window directly instead of synthesising an NTP tab via "
                "OpenTabsInBrowser (which DCHECKs on empty input)"
            ),
            guard=(
                "  // Maho zero-tab: when the tab list is empty and we are targeting a normal\n"
                "  // browser window, create/show the browser without synthesising an NTP tab."
            ),
        ),
        Replacement(
            old=(
                "      if (browser) {\n"
                "        browser->window()->Show();\n"
                "      }\n"
                "    }\n"
                "  } else {\n"
            ),
            new=(
                "      if (browser) {\n"
                "        browser->window()->Show();\n"
                "        // Maho cold-start: auto-open the command palette exactly once on the\n"
                "        // zero-tab (cold empty) startup path. This empty-|tabs| branch is the\n"
                "        // ONLY cold empty-start path (Ctrl+N windows and\n"
                "        // session-restore-with-tabs do not reach it), so the process-global\n"
                "        // once-guard is belt-and-suspenders. ShowMahoCommandOverlayForNewTab()\n"
                "        // self-defers until the BrowserView widget is ready and shows recent\n"
                "        // tabs on the empty query; pressing Esc merely Dismiss()es the overlay\n"
                "        // (no tab created), leaving the zero-tab themed gradient window.\n"
                "        static std::atomic<bool> g_maho_cold_start_palette_shown{false};\n"
                "        if (!g_maho_cold_start_palette_shown.exchange(true)) {\n"
                "          if (auto* maho_browser_view =\n"
                "                  BrowserView::GetBrowserViewForBrowser(browser)) {\n"
                "            maho_browser_view->ShowMahoCommandOverlayForNewTab();\n"
                "          }\n"
                "        }\n"
                "      }\n"
                "    }\n"
                "  } else {\n"
            ),
            description=(
                "Maho cold-start: auto-open the command palette exactly once on the "
                "zero-tab empty-|tabs| startup path (Esc creates no tab)"
            ),
            guard="g_maho_cold_start_palette_shown",
        ),
        Replacement(
            old=(
                "  if (!browser->tab_strip_model()->GetActiveWebContents() &&\n"
                "      !process_headless_commands) {\n"
                "    // TODO(sky): this is a work around for 110909. Figure out why it's needed.\n"
                "    if (!browser->tab_strip_model()->count()) {\n"
                "      chrome::AddTabAt(browser, GURL(), -1, true);\n"
                "    } else {\n"
                "      browser->tab_strip_model()->ActivateTabAt(0);\n"
                "    }\n"
                "  }"
            ),
            new=(
                "  if (!browser->tab_strip_model()->GetActiveWebContents() &&\n"
                "      !process_headless_commands) {\n"
                "    // TODO(sky): this is a work around for 110909. Figure out why it's needed.\n"
                "    if (!browser->tab_strip_model()->count()) {\n"
                "      // Maho zero-tab: normal browser windows are allowed to start with zero\n"
                "      // tabs; do not synthesise a blank/NTP fallback tab for them.\n"
                "      if (!browser->is_type_normal()) {\n"
                "        chrome::AddTabAt(browser, GURL(), -1, true);\n"
                "      }\n"
                "    } else {\n"
                "      browser->tab_strip_model()->ActivateTabAt(0);\n"
                "    }\n"
                "  }"
            ),
            description=(
                "Maho zero-tab: suppress the chrome::AddTabAt() empty-tab fallback in "
                "OpenTabsInBrowser() for normal browser windows so they can start with "
                "zero tabs"
            ),
        ),
        Replacement(
            old=(
                "    return {StartupTabs({StartupTab(chrome::ChromeUINewTabURLAsGURL())}),\n"
                "            launch_result};\n"
                "  }"
            ),
            new=(
                "    if (is_post_crash_launch) {\n"
                "      // Maho zero-tab: crash recovery should NOT inject a synthetic NTP.\n"
                "      return {StartupTabs(), launch_result};\n"
                "    }\n"
                "    return {StartupTabs({StartupTab(chrome::ChromeUINewTabURLAsGURL())}),\n"
                "            launch_result};\n"
                "  }"
            ),
            description="Maho zero-tab: crash recovery should NOT inject a synthetic NTP",
            guard="Maho zero-tab: crash recovery should NOT inject",
        ),
        Replacement(
            old=(
                "  // Maho welcome: on first run, open onboarding as a chromeless popup.\n"
                "  if (tabs.empty()) {\n"
                "    PrefService* prefs = profile_->GetPrefs();\n"
                "    if (prefs && !prefs->GetBoolean(\"maho.welcome.completed\")) {\n"
                "      maho::MahoWelcomeWindow::Show(profile_);\n"
                "      // No Browser is created — welcome lives in a frameless views::Widget.\n"
                "      // Returning nullptr is acceptable: callers (MaybeShowSharedTabGroup*)\n"
                "      // null-check their argument.\n"
                "      return nullptr;\n"
                "    }\n"
                "  }\n"
            ),
            new=(
                "  // Maho login gate: while active (no valid relay session) suppress ALL\n"
                "  // startup browser creation AND session restore so onboarding shows alone.\n"
                "  // FinishOnboarding restores-or-creates the browser afterwards. Placed\n"
                "  // before the SYNCHRONOUS_RESTORE block and DeleteStaleSessionData() so the\n"
                "  // on-disk session survives for the deferred restore.\n"
                "  if (PrefService* gate_prefs = profile_->GetPrefs();\n"
                "      gate_prefs && gate_prefs->GetBoolean(\"maho.auth.login_gate_active\")) {\n"
                "    maho::MahoWelcomeWindow::Show(profile_);\n"
                "    return nullptr;\n"
                "  }\n"
            ),
            description=(
                "Maho login gate: suppress startup browser creation AND session restore "
                "while login_gate_active, so the returning-user restore path no longer "
                "shows a browser window behind the onboarding gate"
            ),
            guard="Maho login gate: while active (no valid relay session)",
            idempotent=True,
        ),
        Replacement(
            old='#include "base/functional/bind.h"\n',
            new=(
                '#include "base/functional/bind.h"\n'
                '#include "base/task/single_thread_task_runner.h"\n'
                '#include "base/time/time.h"\n'
            ),
            description=(
                "Maho cold-start: include SingleThreadTaskRunner + TimeDelta so the "
                "zero-tab command-palette trigger can retry on the UI task runner "
                "until the BrowserView widget is ready"
            ),
            guard='#include "base/task/single_thread_task_runner.h"',
        ),
        Replacement(
            old="void UrlsToTabs(const std::vector<GURL>& urls, StartupTabs* tabs) {\n",
            new=(
                "// Maho cold-start: retry opening the new-tab command palette until the\n"
                "// BrowserView and its sidebar container are constructed. On the zero-tab\n"
                "// startup path BrowserView::GetBrowserViewForBrowser() can briefly return\n"
                "// null (or the sidebar container is not built yet) right after\n"
                "// window()->Show(), so a single attempt is dropped and no palette appears.\n"
                "// Give up without creating a tab once the attempt budget is exhausted so\n"
                "// the zero-tab themed gradient window remains.\n"
                "void ShowMahoColdStartPaletteWhenReady(base::WeakPtr<Browser> browser,\n"
                "                                       int remaining_attempts) {\n"
                "  if (!browser) {\n"
                "    return;\n"
                "  }\n"
                "  if (auto* browser_view =\n"
                "          BrowserView::GetBrowserViewForBrowser(browser.get())) {\n"
                "    if (browser_view->maho_sidebar_container()) {\n"
                "      browser_view->ShowMahoCommandOverlayForNewTab();\n"
                "      return;\n"
                "    }\n"
                "  }\n"
                "  if (remaining_attempts <= 0) {\n"
                "    return;\n"
                "  }\n"
                "  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(\n"
                "      FROM_HERE,\n"
                "      base::BindOnce(&ShowMahoColdStartPaletteWhenReady, browser,\n"
                "                     remaining_attempts - 1),\n"
                "      base::Milliseconds(25));\n"
                "}\n"
                "\n"
                "void UrlsToTabs(const std::vector<GURL>& urls, StartupTabs* tabs) {\n"
            ),
            description=(
                "Maho cold-start: add a bounded UI-task-runner retry helper that opens "
                "the new-tab command palette once the BrowserView + sidebar container "
                "are ready (mirrors the browser_command_controller anon-namespace "
                "retry, which is unreachable from this TU)"
            ),
            guard="void ShowMahoColdStartPaletteWhenReady(base::WeakPtr<Browser> browser,",
        ),
        Replacement(
            old=(
                "        static std::atomic<bool> g_maho_cold_start_palette_shown{false};\n"
                "        if (!g_maho_cold_start_palette_shown.exchange(true)) {\n"
                "          if (auto* maho_browser_view =\n"
                "                  BrowserView::GetBrowserViewForBrowser(browser)) {\n"
                "            maho_browser_view->ShowMahoCommandOverlayForNewTab();\n"
                "          }\n"
                "        }\n"
            ),
            new=(
                "        static std::atomic<bool> g_maho_cold_start_palette_shown{false};\n"
                "        if (!g_maho_cold_start_palette_shown.exchange(true)) {\n"
                "          // Maho cold-start: BrowserView (and its sidebar container) may not\n"
                "          // be registered yet at window()->Show() time, so a single\n"
                "          // GetBrowserViewForBrowser() attempt can be lost. Retry on the UI\n"
                "          // task runner until the view is ready, then open the palette.\n"
                "          ShowMahoColdStartPaletteWhenReady(browser->AsWeakPtr(), 200);\n"
                "        }\n"
            ),
            description=(
                "Maho cold-start: route the once-guarded zero-tab palette trigger "
                "through the bounded widget-ready retry helper so it is not dropped "
                "when the BrowserView is not yet registered"
            ),
            guard="ShowMahoColdStartPaletteWhenReady(browser->AsWeakPtr()",
        ),
        Replacement(
            old=(
                "    browser = SessionRestore::RestoreSession(profile_, nullptr, restore_options,\n"
                "                                             tabs);\n"
                "    if (browser) {\n"
                "      return browser;\n"
                "    }\n"
            ),
            new=(
                "    browser = SessionRestore::RestoreSession(profile_, nullptr, restore_options,\n"
                "                                             tabs);\n"
                "    if (browser) {\n"
                "      static std::atomic<bool> g_maho_restore_palette_shown{false};\n"
                "      if (!g_maho_restore_palette_shown.exchange(true)) {\n"
                "        ShowMahoColdStartPaletteWhenReady(browser->AsWeakPtr(), 200);\n"
                "      }\n"
                "      return browser;\n"
                "    }\n"
            ),
            description=(
                "Maho cold-start: also auto-open the command palette once on the "
                "session-restore startup path so returning users with restored tabs "
                "get the palette at launch too (empty-|tabs| path handles cold empty "
                "start; these two paths are mutually exclusive so it fires once)"
            ),
            guard="g_maho_restore_palette_shown",
        ),
    ],
    # ── browser_commands.cc ──────────────────────────────────────────────────
    "chrome/browser/ui/browser_commands.cc": [
        Replacement(
            old='#include "chrome/browser/ui/browser.h"\n',
            new=(
                '#include "chrome/browser/ui/browser.h"\n'
                '#include "maho/browser/ui/views/welcome/maho_welcome_window.h"\n'
            ),
            description="Add MahoWelcomeWindow include to browser_commands.cc for login-gate guard",
            idempotent=True,
        ),
        Replacement(
            old=(
                "void NewEmptyWindow(Profile* profile, bool should_trigger_session_restore) {\n"
                "  bool off_the_record = profile->IsOffTheRecord();\n"
            ),
            new=(
                "void NewEmptyWindow(Profile* profile, bool should_trigger_session_restore) {\n"
                "  // Maho login gate: while active, route New Window / dock-reopen to the\n"
                "  // onboarding gate instead of opening a browser behind it. Covers Cmd+N,\n"
                "  // the New Window menu, and the macOS dock-reopen path (which funnels\n"
                "  // through NewEmptyWindow via CreateBrowser in app_controller_mac.mm).\n"
                "  if (Profile* gate_profile =\n"
                "          profile ? profile->GetOriginalProfile() : nullptr;\n"
                "      gate_profile &&\n"
                "      gate_profile->GetPrefs()->GetBoolean(\"maho.auth.login_gate_active\")) {\n"
                "    maho::MahoWelcomeWindow::Show(gate_profile);\n"
                "    return;\n"
                "  }\n"
                "  bool off_the_record = profile->IsOffTheRecord();\n"
            ),
            description=(
                "Maho login gate: suppress New Window / dock-reopen while login_gate_active, "
                "redirecting to the onboarding gate"
            ),
            guard="Maho login gate: while active, route New Window",
            idempotent=True,
        ),
    ],
    # ── session_restore.cc ───────────────────────────────────────────────────
    "chrome/browser/sessions/session_restore.cc": [
        Replacement(
            old='#include "chrome/browser/sessions/session_restore.h"\n',
            new=(
                '#include "chrome/browser/sessions/session_restore.h"\n'
                '#include "maho/browser/maho_tab_id_helper.h"\n'
            ),
            description="Add MahoTabIdHelper include to session_restore.cc",
        ),
        Replacement(
            old=(
                "    RecordAppLaunchForTab(browser, tab, selected_index);\n"
                "\n"
                "    WebContents* web_contents;\n"
                "    if (disposition == WindowOpenDisposition::CURRENT_TAB) {"
            ),
            new=(
                "    // Maho zero-tab: skip chrome://newtab and about:blank restored tabs so\n"
                "    // legacy sessions do not resurrect these synthetic URLs. Real user tabs\n"
                "    // (youtube.com, etc.) restore normally.\n"
                "    {\n"
                "      const auto& navs = tab.navigations;\n"
                "      if (!navs.empty()) {\n"
                "        const int idx = std::clamp(tab.current_navigation_index, 0,\n"
                "                                   static_cast<int>(navs.size()) - 1);\n"
                "        const GURL& url = navs[idx].virtual_url();\n"
                "        if (url.spec() == \"about:blank\" ||\n"
                "            (url.SchemeIs(\"chrome\") && url.host() == \"newtab\")) {\n"
                "          return nullptr;\n"
                "        }\n"
                "      }\n"
                "    }\n"
                "    // Push the stable id ONLY after the zero-tab filter's early return, so a\n"
                "    // filtered tab never leaks an unconsumed id into the global FIFO queue\n"
                "    // that the next restored WebContents would mis-pop.\n"
                "    {\n"
                "      auto it = tab.extra_data.find(MahoTabIdHelper::kExtraDataKey);\n"
                "      if (it != tab.extra_data.end() && !it->second.empty()) {\n"
                "        MahoTabIdHelper::SetPendingRestoredTabId(it->second);\n"
                "      }\n"
                "    }\n"
                "    RecordAppLaunchForTab(browser, tab, selected_index);\n"
                "\n"
                "    WebContents* web_contents;\n"
                "    if (disposition == WindowOpenDisposition::CURRENT_TAB) {"
            ),
            description="Set pending restored tab ID before foreign tab restoration",
            guard=(
                "        if (url.spec() == \"about:blank\" ||\n"
                "            (url.SchemeIs(\"chrome\") && url.host() == \"newtab\")) {\n"
                "          return nullptr;"
            ),
        ),
        Replacement(
            old=(
                "    RecordAppLaunchForTab(browser, tab, selected_index);\n"
                "\n"
                "    // Associate sessionStorage (if any) to the restored tab."
            ),
            new=(
                "    // Maho zero-tab: skip chrome://newtab and about:blank restored tabs so\n"
                "    // legacy sessions do not resurrect these synthetic URLs. Real user tabs\n"
                "    // (youtube.com, etc.) restore normally. If the filtered tab was the\n"
                "    // selected tab we still Show() the browser (with no active tab) so the\n"
                "    // post-loop DCHECK(did_show_browser) at session_restore.cc:~840 holds;\n"
                "    // Maho's Browser::TabStripEmpty patch keeps the empty window alive.\n"
                "    {\n"
                "      const auto& navs = tab.navigations;\n"
                "      if (!navs.empty()) {\n"
                "        const int idx = std::clamp(tab.current_navigation_index, 0,\n"
                "                                   static_cast<int>(navs.size()) - 1);\n"
                "        const GURL& url = navs[idx].virtual_url();\n"
                "        if (url.spec() == \"about:blank\" ||\n"
                "            (url.SchemeIs(\"chrome\") && url.host() == \"newtab\")) {\n"
                "          if (is_selected_tab && browser != browser_ &&\n"
                "              browser && browser->window()) {\n"
                "            browser->window()->Show();\n"
                "            browser->set_is_session_restore(false);\n"
                "            did_show_browser = true;\n"
                "          }\n"
                "          return;\n"
                "        }\n"
                "      }\n"
                "    }\n"
                "    // Push the stable id ONLY after the zero-tab filter's early return, so a\n"
                "    // filtered tab never leaks an unconsumed id into the global FIFO queue\n"
                "    // that the next restored WebContents would mis-pop.\n"
                "    {\n"
                "      auto it = tab.extra_data.find(MahoTabIdHelper::kExtraDataKey);\n"
                "      if (it != tab.extra_data.end() && !it->second.empty()) {\n"
                "        MahoTabIdHelper::SetPendingRestoredTabId(it->second);\n"
                "      }\n"
                "    }\n"
                "    RecordAppLaunchForTab(browser, tab, selected_index);\n"
                "\n"
                "    // Associate sessionStorage (if any) to the restored tab."
            ),
            description="Set pending restored tab ID before session tab restoration",
            guard=(
                "          if (is_selected_tab && browser != browser_ &&\n"
                "              browser && browser->window()) {"
            ),
        ),
        Replacement(
            old=(
                "      if (startup_tabs_.empty() ||\n"
                "          (startup_tabs_.size() == 1 && whats_new::IsEnabled() &&\n"
                "           startup_tabs_[0].url == whats_new::GetWebUIStartupURL())) {\n"
                "        // No tab browsers were created and no URLs were supplied on the command\n"
                "        // line, or only the What's New page is specified at startup and may or\n"
                "        // may not add a tab. Open the new tab page.\n"
                "        startup_tabs_.emplace_back(chrome::ChromeUINewTabURLAsGURL());\n"
                "      }\n"
                "      AppendURLsToBrowser(browser, startup_tabs_);"
            ),
            new=(
                "      // Maho zero-tab: do not synthesise an NTP tab when the session restore\n"
                "      // produces no tabbed browser.  If |startup_tabs_| already contains real\n"
                "      // URLs (e.g. command-line URLs) append them; otherwise leave the window\n"
                "      // open with zero tabs so the Maho empty-window UX can take over.\n"
                "      startup_tabs_.erase(\n"
                "          std::remove_if(startup_tabs_.begin(), startup_tabs_.end(),\n"
                "              [](const StartupTab& t) {\n"
                "                return t.url.spec() == \"about:blank\" ||\n"
                "                       (t.url.SchemeIs(\"chrome\") && t.url.host() == \"newtab\");\n"
                "              }),\n"
                "          startup_tabs_.end());\n"
                "      if (!startup_tabs_.empty()) {\n"
                "        AppendURLsToBrowser(browser, startup_tabs_);\n"
                "      }"
            ),
            description=(
                "Maho zero-tab: in SessionRestore::FinishedTabCreation(), suppress the NTP "
                "synthesis that fires when startup_tabs_ is empty (or only What's New) so "
                "that a returning-user session restore with no restored tabs starts with a "
                "zero-tab window instead of an unwanted chrome://newtab"
            ),
            guard="startup_tabs_.erase(",
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
            guard='#include "content/public/browser/browser_url_handler.h"',
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
            guard="bool MahoRewriteNewTabToBlank(",
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
            guard="&MahoRewriteNewTabToBlank,",
        ),
        # ── Ad-block throttle includes ─────────────────────────────────────────
        Replacement(
            old=(
                '#include "chrome/browser/search/search.h"\n'
                '#include "content/public/browser/browser_url_handler.h"\n'
            ),
            new=(
                '#include "chrome/browser/search/search.h"\n'
                '#include "content/public/browser/browser_url_handler.h"\n'
                '#include "maho/browser/maho_core_holder.h"\n'
                '#include "maho/browser/net/maho_ad_block_tab_helper.h"\n'
                '#include "maho/browser/net/maho_ad_block_throttle.h"\n'
                '#include "maho/browser/net/maho_webstore_ua_throttle.h"\n'
            ),
            description=(
                "Insert Maho ad-block throttle and WebStore UA throttle includes "
                "into chrome_content_browser_client.cc (after browser_url_handler.h)"
            ),
            guard='#include "maho/browser/net/maho_ad_block_throttle.h"',
        ),
        # ── Ad-block throttle registration in CreateURLLoaderThrottles() ───────
        Replacement(
            old="  return result;\n",
            new=(
                "  // Maho: register ad-block and WebStore UA throttles.\n"
                "  // MahoAdBlockThrottle::WillStartRequest runs on the UI thread\n"
                "  // (DCHECK_CURRENTLY_ON(BrowserThread::UI)); the on_blocked callback\n"
                "  // therefore also fires on the UI thread. Using GetWeakPtr() rather\n"
                "  // than base::Unretained() ensures the callback is a no-op if the\n"
                "  // tab closes between throttle construction and the first network\n"
                "  // request, closing the UAF window identified in audit finding H1.\n"
                "  if (maho::GetCore()) {\n"
                "    base::RepeatingClosure on_blocked;\n"
                "    content::WebContents* wc = wc_getter ? wc_getter.Run() : nullptr;\n"
                "    if (wc) {\n"
                "      MahoAdBlockTabHelper::CreateForWebContents(wc);\n"
                "      MahoAdBlockTabHelper* helper =\n"
                "          MahoAdBlockTabHelper::FromWebContents(wc);\n"
                "      if (helper) {\n"
                "        on_blocked = base::BindRepeating(\n"
                "            [](base::WeakPtr<MahoAdBlockTabHelper> weak) {\n"
                "              if (weak) {\n"
                "                weak->IncrementBlockedCount();\n"
                "              }\n"
                "            },\n"
                "            helper->GetWeakPtr());\n"
                "      }\n"
                "    }\n"
                "    result.push_back(\n"
                "        std::make_unique<MahoAdBlockThrottle>(std::move(on_blocked)));\n"
                "  }\n"
                "  result.push_back(std::make_unique<MahoWebStoreUAThrottle>());\n"
                "  return result;\n"
            ),
            description=(
                "Register MahoAdBlockThrottle and MahoWebStoreUAThrottle in "
                "CreateURLLoaderThrottles(); uses helper->GetWeakPtr() instead of "
                "base::Unretained(helper) to eliminate UAF window (audit H1 fix)"
            ),
            guard="maho::GetCore()",
        ),
        # ── ATC space-routing NavigationThrottle ───────────────────────────────
        Replacement(
            old=(
                '#include "maho/browser/net/maho_webstore_ua_throttle.h"\n'
            ),
            new=(
                '#include "maho/browser/net/maho_webstore_ua_throttle.h"\n'
                '#include "maho/browser/net/maho_atc_navigation_throttle.h"\n'
                '#include "maho/browser/ui/views/sidebar/maho_sidebar_prefs.h"\n'
            ),
            description=(
                "Insert MahoAtcNavigationThrottle and MahoAtcState includes into "
                "chrome_content_browser_client.cc"
            ),
            guard='#include "maho/browser/net/maho_atc_navigation_throttle.h"',
        ),
        Replacement(
            old=(
                "void ChromeContentBrowserClient::CreateThrottlesForNavigation(\n"
                "    content::NavigationThrottleRegistry& registry) {\n"
                "  CreateAndAddChromeThrottlesForNavigation(registry);\n"
                "}\n"
            ),
            new=(
                "void ChromeContentBrowserClient::CreateThrottlesForNavigation(\n"
                "    content::NavigationThrottleRegistry& registry) {\n"
                "  CreateAndAddChromeThrottlesForNavigation(registry);\n"
                "\n"
                "#if BUILDFLAG(IS_MAC)\n"
                "  // Maho ATC: route top-level user-initiated navigations that bypass\n"
                "  // Browser::OpenURLFromTab (omnibox typed navigations and plain same-tab\n"
                "  // link clicks) into their target space. Gated on HasEnabledRules() so\n"
                "  // there is zero overhead (no throttle created) when no rules exist.\n"
                "  if (maho::MahoAtcState::HasEnabledRules()) {\n"
                "    registry.AddThrottle(\n"
                "        std::make_unique<maho::MahoAtcNavigationThrottle>(registry));\n"
                "  }\n"
                "#endif\n"
                "}\n"
            ),
            description=(
                "Register MahoAtcNavigationThrottle in CreateThrottlesForNavigation "
                "(mac-only, gated on MahoAtcState::HasEnabledRules)"
            ),
            guard="maho::MahoAtcNavigationThrottle",
        ),
        Replacement(
            old=(
                '  } else {\n'
                '    // WebUI and regular pages follow the browser theme color mode, provided by\n'
                '    // the color provider.\n'
                '    preferred_color_scheme =\n'
                '        web_contents->GetColorMode() == ui::ColorProviderKey::ColorMode::kLight\n'
                '            ? blink::mojom::PreferredColorScheme::kLight\n'
                '            : blink::mojom::PreferredColorScheme::kDark;\n'
                '  }\n'
                '  // Update the preferred root scrollbar color based on the lightness level of\n'
                '  // the toolbar\'s color.\n'
                '  preferred_root_scrollbar_color_scheme =\n'
                '      color_utils::IsDark(\n'
                '          web_contents->GetColorProvider().GetColor(kColorToolbar))\n'
                '          ? blink::mojom::PreferredColorScheme::kDark\n'
                '          : blink::mojom::PreferredColorScheme::kLight;'
            ),
            new=(
                '  } else {\n'
                '    // Maho: web page rendering follows Browser theme, not Space theme.\n'
                '    Profile* maho_profile = Profile::FromBrowserContext(web_contents->GetBrowserContext());\n'
                '    auto* theme_service = ThemeServiceFactory::GetForProfile(maho_profile);\n'
                '    const auto scheme = theme_service ? theme_service->GetBrowserColorScheme()\n'
                '                                      : ThemeService::BrowserColorScheme::kSystem;\n'
                '    bool prefers_dark = false;\n'
                '    if (scheme == ThemeService::BrowserColorScheme::kSystem) {\n'
                '      prefers_dark = ui::NativeTheme::GetInstanceForNativeUi()->preferred_color_scheme() ==\n'
                '                     ui::NativeTheme::PreferredColorScheme::kDark;\n'
                '    } else {\n'
                '      prefers_dark = (scheme == ThemeService::BrowserColorScheme::kDark);\n'
                '    }\n'
                '    preferred_color_scheme = prefers_dark ? blink::mojom::PreferredColorScheme::kDark\n'
                '                                          : blink::mojom::PreferredColorScheme::kLight;\n'
                '  }\n'
                '  // Maho: scrollbar matches the page color scheme.\n'
                '  preferred_root_scrollbar_color_scheme = preferred_color_scheme;'
            ),
            description="Maho: derive preferred_color_scheme and preferred_root_scrollbar_color_scheme from Browser theme instead of web_contents->GetColorMode()",
            guard="Maho: web page rendering follows Browser theme",
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
                '      "//maho/browser/ui/views/sidebar:interactive_tests",\n'
            ),
            description="Include Maho sidebar interactive tests in Chromium interactive_ui_tests deps",
        ),
        Replacement(
            old='      "//maho/browser/ui/link_preview:interactive_tests",\n',
            new="",
            description="Maho: strip deleted link-preview interactive_tests dep from chrome/test/BUILD.gn",
            idempotent=True,
        ),
        Replacement(
            old='      "//maho/browser/ui/views/spaces_overlay:unit_tests",\n',
            new=(
                '      "//maho/browser/ui/views/spaces_overlay:unit_tests",\n'
                '      "//maho/browser/ui/tabs:unit_tests",\n'
            ),
            description=(
                "Include MahoMruTabTracker unit tests in Chromium unit_tests"
            ),
            guard='"//maho/browser/ui/tabs:unit_tests"',
        ),
        Replacement(
            old='      "//maho/browser:maho_location_bar_views_unittests",\n',
            new=(
                '      "//maho/browser:maho_location_bar_views_unittests",\n'
                '      "//maho/browser:maho_incognito_unittests",\n'
            ),
            description=(
                "Include Maho private context policy unit tests in Chromium unit_tests"
            ),
            guard='"//maho/browser:maho_incognito_unittests"',
        ),
        Replacement(
            old='      "//maho/browser:maho_incognito_unittests",\n',
            new=(
                '      "//maho/browser:maho_incognito_unittests",\n'
                '      "//maho/browser:maho_ai_runtime_router_unittests",\n'
                '      "//maho/browser:maho_ai_llm_client_unittests",\n'
            ),
            description=(
                "Maho R-9: include the AI runtime-router and LLM-client OTR-denial "
                "unit tests (task-9-ai-unit) in Chromium unit_tests"
            ),
            guard='"//maho/browser:maho_ai_runtime_router_unittests"',
        ),
        Replacement(
            old='      "//chrome/browser/ui/views/toolbar:browser_tests",\n',
            new=(
                '      "//maho/browser/mcp:browser_tests",\n'
                '      "//chrome/browser/ui/views/toolbar:browser_tests",\n'
            ),
            description=(
                "Include Maho MCP incognito browser tests in Chromium browser_tests"
            ),
            guard='"//maho/browser/mcp:browser_tests"',
        ),
    ],
    "chrome/browser/ui/views/page_action/page_action_properties_provider.cc": [
        Replacement(
            old=(
                "    {\n"
                "        kActionShowTranslate,\n"
                "        {\n"
                "            .histogram_name = \"Translate\",\n"
                "            .type = PageActionIconType::kTranslate,\n"
                "            .element_identifier = kTranslatePageActionElementId,\n"
                "        },\n"
                "    },\n"
            ),
            new=(
                "    {\n"
                "        kActionShowTranslate,\n"
                "        {\n"
                "            .histogram_name = \"Translate\",\n"
                "            .type = PageActionIconType::kTranslate,\n"
                "            // Maho: kTranslatePageActionElementId is owned by the Maho\n"
                "            // sidebar's secondary_label_ in Arc layout. Suppressing\n"
                "            // identifier assignment here keeps ElementTracker lookups\n"
                "            // (BrowserView::ShowTranslateBubble's highlight_element)\n"
                "            // unambiguous and prevents the DCHECK_EQ(1U, ...) crash in\n"
                "            // GetUniqueElement when both views are visible.\n"
                "        },\n"
                "    },\n"
            ),
            description=(
                "Drop kTranslatePageActionElementId from the upstream translate "
                "PageActionView's properties so Maho's sidebar secondary_label_ "
                "becomes the sole holder; required for §2J of the toolbar provider plan."
            ),
            guard=(
                "// Maho: kTranslatePageActionElementId is owned by the Maho\n"
            ),
        ),
    ],
    # ── chrome_web_ui_configs.cc ──────────────────────────────────────────────
    "chrome/browser/ui/webui/chrome_web_ui_configs.cc": [
        Replacement(
            old='#include "maho/browser/ui/webui/maho_settings/maho_settings_ui.h"\n',
            new=(
                '#include "maho/browser/ui/webui/maho_mail/maho_mail_ui.h"\n'
                '#include "maho/browser/ui/webui/maho_routines/maho_routines_ui.h"\n'
                '#include "maho/browser/ui/webui/maho_settings/maho_settings_ui.h"\n'
            ),
            description="Add maho_mail_ui.h and maho_routines_ui.h includes before maho_settings_ui.h",
            guard='#include "maho/browser/ui/webui/maho_mail/maho_mail_ui.h"',
        ),
        Replacement(
            old='  map.AddWebUIConfig(std::make_unique<MahoSettingsUIConfig>());\n',
            new=(
                '  map.AddWebUIConfig(std::make_unique<MahoMailUIConfig>());\n'
                '  map.AddWebUIConfig(std::make_unique<MahoRoutinesUIConfig>());\n'
                '  map.AddWebUIConfig(std::make_unique<MahoSettingsUIConfig>());\n'
            ),
            description="Register MahoMailUIConfig and MahoRoutinesUIConfig before MahoSettingsUIConfig",
            guard='MahoMailUIConfig',
        ),
        Replacement(
            old='#include "maho/browser/ui/webui/maho_test/maho_test_ui.h"\n',
            new=(
                '#include "build/buildflag.h"\n'
                '#include "maho/build/config/maho_test_buildflags.h"\n'
                '#if BUILDFLAG(ENABLE_MAHO_TEST_WEBUI)\n'
                '#include "maho/browser/ui/webui/maho_test/maho_test_ui.h"\n'
                '#endif\n'
            ),
            description="Conditionally include maho_test_ui.h using buildflag",
            guard='#include "maho/build/config/maho_test_buildflags.h"',
        ),
        Replacement(
            old='  map.AddWebUIConfig(std::make_unique<MahoTestUIConfig>());\n',
            new=(
                '#if BUILDFLAG(ENABLE_MAHO_TEST_WEBUI)\n'
                '  map.AddWebUIConfig(std::make_unique<MahoTestUIConfig>());\n'
                '#endif\n'
            ),
            description="Conditionally register MahoTestUIConfig using buildflag",
            guard='#if BUILDFLAG(ENABLE_MAHO_TEST_WEBUI)\n  map.AddWebUIConfig',
        ),
    ],
    # ── chrome/browser/ui/webui/BUILD.gn ──────────────────────────────────────
    "chrome/browser/ui/webui/BUILD.gn": [
        Replacement(
            old='    "//maho/browser/ui/webui/maho_settings",\n',
            new=(
                '    "//maho/browser/ui/webui/maho_mail",\n'
                '    "//maho/browser/ui/webui/maho_routines",\n'
                '    "//maho/browser/ui/webui/maho_settings",\n'
            ),
            description="Add maho_mail and maho_routines dep before maho_settings in webui BUILD.gn",
            guard='"//maho/browser/ui/webui/maho_mail"',
        ),
    ],
    "chrome/browser/global_keyboard_shortcuts_mac.mm": [
        Replacement(
            old=(
                '      {true,  false, false, false, kVK_ANSI_E,            IDC_MAHO_AI_PANEL},\n'
            ),
            new=(
                '      {true,  false, false, false, kVK_ANSI_E,            IDC_MAHO_AI_PANEL},\n'
                '      {true,  true,  false, false, kVK_ANSI_Y,            IDC_SHOW_HISTORY},\n'
            ),
            description="Add CMD+SHIFT+Y mapping for IDC_SHOW_HISTORY in hidden shortcuts",
        ),
    ],
    "chrome/browser/global_keyboard_shortcuts_mac_unittest.mm": [
        Replacement(
            old=(
                '#include "chrome/browser/global_keyboard_shortcuts_mac.h"\n'
            ),
            new=(
                '#include "chrome/browser/global_keyboard_shortcuts_mac.h"\n'
                '#include "chrome/browser/ui/cocoa/accelerators_cocoa.h"\n'
                '#include "ui/base/accelerators/accelerator.h"\n'
            ),
            description="Include Accelerator and AcceleratorsCocoa headers in keyboard shortcut tests",
        ),
        Replacement(
            old=(
                '  EXPECT_EQ(\n'
                '      IDC_SELECT_TAB_0,\n'
                '      CommandForKeys(kVK_ANSI_1, CommandKeyState::kDown, ShiftKeyState::kUp,\n'
                '                     OptionKeyState::kUp, ControlKeyState::kUp));\n'
                '}\n'
            ),
            new=(
                '  EXPECT_EQ(\n'
                '      IDC_SELECT_TAB_0,\n'
                '      CommandForKeys(kVK_ANSI_1, CommandKeyState::kDown, ShiftKeyState::kUp,\n'
                '                     OptionKeyState::kUp, ControlKeyState::kUp));\n'
                '}\n'
                '\n'
                'TEST(GlobalKeyboardShortcuts, HistoryShortcuts) {\n'
                '  // Test Command+Shift+Y resolves to IDC_SHOW_HISTORY (registered as a non-menu shortcut)\n'
                '  EXPECT_EQ(\n'
                '      IDC_SHOW_HISTORY,\n'
                '      CommandForKeys(kVK_ANSI_Y, CommandKeyState::kDown, ShiftKeyState::kDown,\n'
                '                     OptionKeyState::kUp, ControlKeyState::kUp));\n'
                '\n'
                '  // Test Command+Y is mapped to IDC_SHOW_HISTORY in AcceleratorsCocoa\n'
                '  const ui::Accelerator* menu_accelerator =\n'
                '      AcceleratorsCocoa::GetInstance()->GetAcceleratorForCommand(\n'
                '          IDC_SHOW_HISTORY);\n'
                '  ASSERT_TRUE(menu_accelerator);\n'
                '  EXPECT_EQ(menu_accelerator->key_code(), ui::VKEY_Y);\n'
                '  EXPECT_EQ(menu_accelerator->modifiers(), ui::EF_COMMAND_DOWN);\n'
                '}\n'
            ),
            description="Assert CMD+SHIFT+Y and CMD+Y both resolve to IDC_SHOW_HISTORY",
        ),
    ],
    "base/trace_event/builtin_categories.h": [
        Replacement(
            old='    perfetto::Category("media").SetTags("video"),\n',
            new=(
                '    perfetto::Category("maho").SetDescription(\n'
                '      "Traces for Maho-specific events (sidebar, ai, spaces, tab preview)."),\n'
                '    perfetto::Category("media").SetTags("video"),\n'
            ),
            description="Register 'maho' Perfetto category for TRACE_EVENT calls in maho code",
            guard='perfetto::Category("maho")',
        ),
    ],
    "chrome/browser/ui/views/toolbar/toolbar_view.cc": [
        Replacement(
            old=(
                "  if (media_button) {\n"
                "    media_button_ = AddChildView(std::move(media_button));\n"
                "  }"
            ),
            new=(
                "  if (media_button) {\n"
                "    // Suppressed for Maho sidebar-bottom media controls\n"
                "  }"
            ),
            description="Suppress upstream MediaToolbarButtonView addition",
        ),
    ],
    "ui/compositor/layer.cc": [
        Replacement(
            old=(
                "void Layer::SetFillsBoundsOpaquely(bool fills_bounds_opaquely) {\n"
                "  CHECK_NE(type_, LayerType::LAYER_SOLID_COLOR);\n"
                "  SetFillsBoundsOpaquelyWithReason(fills_bounds_opaquely,\n"
                "                                   PropertyChangeReason::NOT_FROM_ANIMATION);\n"
                "}\n"
            ),
            new=(
                "void Layer::SetFillsBoundsOpaquely(bool fills_bounds_opaquely) {\n"
                "  // Maho: solid-color layers derive opacity from their color's alpha,\n"
                "  // so this setter is a no-op for them; upstream CHECK_NEs and aborts.\n"
                "  // kGlassFrame makes LayerBasedSolidBackground force LAYER_SOLID_COLOR\n"
                "  // on views that also call SetFillsBoundsOpaquely, so no-op instead.\n"
                "  if (type_ == LayerType::LAYER_SOLID_COLOR) {\n"
                "    return;\n"
                "  }\n"
                "  SetFillsBoundsOpaquelyWithReason(fills_bounds_opaquely,\n"
                "                                   PropertyChangeReason::NOT_FROM_ANIMATION);\n"
                "}\n"
            ),
            description=(
                "Maho: make Layer::SetFillsBoundsOpaquely a no-op for "
                "LAYER_SOLID_COLOR instead of CHECK-crashing (kGlassFrame exposes "
                "solid-color-backed views that call it)"
            ),
            guard="solid-color layers derive opacity from their color's alpha",
        ),
    ],
    "chrome/browser/ui/views/frame/browser_native_widget_mac.mm": [
        Replacement(
            old=(
                "      effect_view.material = NSVisualEffectMaterialUnderWindowBackground;\n"
            ),
            new=(
                "      // Maho: use a high-transmittance material so the window\n"
                "      // vibrancy clearly reveals the desktop behind it. The\n"
                "      // default UnderWindowBackground is nearly opaque frosted.\n"
                "      effect_view.material = NSVisualEffectMaterialHUDWindow;\n"
            ),
            description=(
                "Maho: switch the kGlassFrame NSVisualEffectView material to "
                "HUDWindow for a much more see-through desktop blur"
            ),
            guard="NSVisualEffectMaterialHUDWindow",
        ),
    ],
    "chrome/browser/ui/views/frame/browser_frame_view_mac.mm": [
        Replacement(
            old="#include \"maho/browser/ui/views/sidebar/maho_sidebar_container_view.h\"\n",
            new=(
                "#include \"maho/browser/ui/theme/maho_color_id.h\"\n"
                "#include \"maho/browser/ui/views/sidebar/maho_sidebar_container_view.h\"\n"
            ),
            description=(
                "Maho: include maho_color_id.h so OnPaint can read the "
                "Space-tinted kMahoColorWindowBackground"
            ),
            guard="maho/browser/ui/theme/maho_color_id.h",
        ),
        Replacement(
            old="#include \"ui/base/ui_base_features.h\"\n",
            new=(
                "#include \"ui/base/ui_base_features.h\"\n"
                "#include \"ui/color/color_provider.h\"\n"
            ),
            description=(
                "Maho: include color_provider.h so OnPaint can resolve "
                "kMahoColorWindowBackground via the ColorProvider"
            ),
            guard="ui/color/color_provider.h",
        ),
        Replacement(
            old=(
                "  SkColor frame_color = GetFrameColor(BrowserFrameActiveState::kUseCurrent);\n"
                "  if (features::IsGlassFrameEnabled()) {\n"
                "    const SkAlpha frame_alpha = color_utils::IsDark(frame_color)\n"
                "                                    ? kBrowserFrameAlphaDark\n"
                "                                    : kBrowserFrameAlphaLight;\n"
                "    canvas->DrawColor(SkColorSetA(frame_color, frame_alpha));\n"
                "  } else {\n"
                "    canvas->DrawColor(frame_color);\n"
            ),
            new=(
                "  SkColor frame_color = GetFrameColor(BrowserFrameActiveState::kUseCurrent);\n"
                "  const ui::ColorProvider* const color_provider = GetColorProvider();\n"
                "  if (features::IsGlassFrameEnabled()) {\n"
                "    // Maho: under kGlassFrame the window is translucent and the\n"
                "    // NSVisualEffectView desktop blur shows through. Paint only a light\n"
                "    // Space-tinted wash (kMahoColorWindowBackground already carries the\n"
                "    // active theme hue) so the frame gaps around the sidebar/content\n"
                "    // pick up the theme color while the blur stays visible. A fully\n"
                "    // opaque fill (the upstream ~71% wash) would hide it.\n"
                "    if (color_provider) {\n"
                "      // Subtle alpha mirrors the sidebar's translucent glass base\n"
                "      // (kSidebarMinAlpha) so the frame gaps and the sidebar read as one\n"
                "      // continuous tinted-glass surface at zero theme opacity.\n"
                "      constexpr SkAlpha kGlassFrameTintAlpha = 24;\n"
                "      const SkColor window_bg =\n"
                "          color_provider->GetColor(kMahoColorWindowBackground);\n"
                "      canvas->DrawColor(SkColorSetA(window_bg, kGlassFrameTintAlpha));\n"
                "    }\n"
                "  } else {\n"
                "    // Maho: fill the frame with the Space-tinted window background so\n"
                "    // the rounded-corner gaps around the sidebar/content match the\n"
                "    // active theme instead of Chromium's default gray\n"
                "    // (kColorFrameActive).\n"
                "    if (color_provider) {\n"
                "      frame_color = color_provider->GetColor(kMahoColorWindowBackground);\n"
                "    }\n"
                "    canvas->DrawColor(frame_color);\n"
            ),
            description=(
                "Maho: tint the window frame background with the active Space "
                "theme in both glass and non-glass modes, replacing the gray wash"
            ),
            guard="kGlassFrameTintAlpha",
        ),
    ],
    "chrome/browser/ui/views/frame/browser_caption_button_container_win.h": [
        Replacement(
            old="#include \"base/memory/raw_ptr.h\"\n",
            new=(
                "#include <memory>\n"
                "\n"
                "#include \"base/memory/raw_ptr.h\"\n"
            ),
            description="Maho(win): include memory for Arc caption event monitor ownership",
            guard="#include <memory>\n",
        ),
        Replacement(
            old="#include \"base/scoped_observation.h\"\n",
            new=(
                "#include \"base/scoped_observation.h\"\n"
                "#include \"base/timer/timer.h\"\n"
            ),
            description="Maho(win): include timer support for delayed Arc caption hide",
            guard="#include \"base/timer/timer.h\"",
        ),
        Replacement(
            old="#include \"ui/base/pointer/touch_ui_controller.h\"\n",
            new=(
                "#include \"ui/base/pointer/touch_ui_controller.h\"\n"
                "#include \"ui/events/event_observer.h\"\n"
            ),
            description="Maho(win): include event observer for Arc caption hover trigger",
            guard="#include \"ui/events/event_observer.h\"",
        ),
        Replacement(
            old=(
                "class BrowserFrameViewWin;\n"
                "class WindowsCaptionButton;\n"
            ),
            new=(
                "class BrowserFrameViewWin;\n"
                "class WindowsCaptionButton;\n"
                "\n"
                "namespace views {\n"
                "class EventMonitor;\n"
                "}  // namespace views\n"
            ),
            description="Maho(win): forward declare EventMonitor for Arc caption hover tracking",
            guard="class EventMonitor;",
        ),
        Replacement(
            old=(
                "class BrowserCaptionButtonContainer : public views::View,\n"
                "                                      public views::WidgetObserver {\n"
            ),
            new=(
                "class BrowserCaptionButtonContainer : public views::View,\n"
                "                                      public views::WidgetObserver,\n"
                "                                      public ui::EventObserver {\n"
            ),
            description="Maho(win): observe window mouse events for Arc caption reveal",
            guard="public ui::EventObserver",
        ),
        Replacement(
            old=(
                "  void OnWidgetBoundsChanged(views::Widget* widget,\n"
                "                             const gfx::Rect& new_bounds) override;\n"
            ),
            new=(
                "  void OnWidgetBoundsChanged(views::Widget* widget,\n"
                "                             const gfx::Rect& new_bounds) override;\n"
                "  void OnWidgetShowStateChanged(views::Widget* widget) override;\n"
                "  void OnWidgetDestroying(views::Widget* widget) override;\n"
                "\n"
                "  // ui::EventObserver:\n"
                "  void OnEvent(const ui::Event& event) override;\n"
            ),
            description="Maho(win): reset Arc caption reveal on widget state changes",
            guard="void OnWidgetShowStateChanged(views::Widget* widget) override;",
        ),
        Replacement(
            old=(
                "  void UpdateButtonToolTipsForWindowControlsOverlay();\n"
                "\n"
                "  const raw_ptr<BrowserFrameViewWin> frame_view_;\n"
            ),
            new=(
                "  void UpdateButtonToolTipsForWindowControlsOverlay();\n"
                "  bool IsMahoArcCaptionMode() const;\n"
                "  bool IsMahoArcCaptionTriggerPoint(const gfx::Point& screen_point) const;\n"
                "  bool IsMahoArcCaptionKeepAlivePoint(const gfx::Point& screen_point) const;\n"
                "  int GetMahoArcCaptionPreferredWidth() const;\n"
                "  void RevealMahoArcCaptionButtons();\n"
                "  void PollMahoArcCaptionHide();\n"
                "  void MaybeRevealMahoArcCaption();\n"
                "  void ScheduleHideMahoArcCaptionButtons();\n"
                "  void HideMahoArcCaptionButtons();\n"
                "  void ApplyMahoArcCaptionButtonVisibility();\n"
                "  void UpdateMahoArcCaptionEventMonitor();\n"
                "\n"
                "  const raw_ptr<BrowserFrameViewWin> frame_view_;\n"
            ),
            description="Maho(win): declare Arc caption reveal helpers",
            guard="IsMahoArcCaptionTriggerPoint",
        ),
        Replacement(
            old=(
                "  base::ScopedObservation<views::Widget, views::WidgetObserver>\n"
                "      widget_observation_{this};\n"
            ),
            new=(
                "  bool maho_arc_caption_revealed_ = false;\n"
                "  base::OneShotTimer maho_arc_caption_hide_timer_;\n"
                "  std::unique_ptr<views::EventMonitor> maho_arc_caption_event_monitor_;\n"
                "\n"
                "  base::ScopedObservation<views::Widget, views::WidgetObserver>\n"
                "      widget_observation_{this};\n"
            ),
            description="Maho(win): store Arc caption reveal state, timer, and monitor",
            guard="maho_arc_caption_event_monitor_",
        ),
    ],
    "chrome/browser/ui/views/frame/browser_caption_button_container_win.cc": [
        Replacement(
            old=(
                "void BrowserCaptionButtonContainer::OnWindowControlsOverlayEnabledChanged() {\n"
                "  if (frame_view_->GetBrowserView()->IsWindowControlsOverlayEnabled()) {\n"
            ),
            new=(
                "void BrowserCaptionButtonContainer::OnWindowControlsOverlayEnabledChanged() {\n"
                "  if (frame_view_->GetBrowserView()->IsWindowControlsOverlayEnabled() ||\n"
                "      frame_view_->GetBrowserView()->IsMahoArcLayoutActive()) {\n"
            ),
            description=(
                "Maho(win): layer-back the caption container in Arc mode (WCO flag is "
                "not enabled for the main window) so revealed buttons paint over the "
                "edge-to-edge client area"
            ),
            guard=(
                "OnWindowControlsOverlayEnabledChanged() {\n"
                "  if (frame_view_->GetBrowserView()->IsWindowControlsOverlayEnabled() ||"
            ),
        ),
        Replacement(
            old=(
                "  UpdateButtons();\n"
                "\n"
                "  if (frame_view_->GetBrowserView()->IsWindowControlsOverlayEnabled()) {\n"
                "    SetBackground(\n"
            ),
            new=(
                "  UpdateButtons();\n"
                "\n"
                "  if (frame_view_->GetBrowserView()->IsWindowControlsOverlayEnabled() ||\n"
                "      frame_view_->GetBrowserView()->IsMahoArcLayoutActive()) {\n"
                "    SetBackground(\n"
            ),
            description=(
                "Maho(win): layer-back the caption container from AddedToWidget in Arc "
                "mode so the hover-revealed buttons composite above the client area"
            ),
            guard=(
                "  UpdateButtons();\n"
                "\n"
                "  if (frame_view_->GetBrowserView()->IsWindowControlsOverlayEnabled() ||\n"
                "      frame_view_->GetBrowserView()->IsMahoArcLayoutActive()) {\n"
                "    SetBackground(\n"
            ),
        ),
        Replacement(
            old="#include <memory>\n",
            new=(
                "#include <algorithm>\n"
                "#include <memory>\n"
                "#include <optional>\n"
                "#include <set>\n"
            ),
            description="Maho(win): include containers/utilities for Arc caption reveal",
            guard="#include <set>\n",
        ),
        Replacement(
            old="#include \"chrome/browser/ui/views/frame/windows_caption_button.h\"\n",
            new=(
                "#include \"chrome/browser/ui/views/frame/windows_caption_button.h\"\n"
                "#include \"base/time/time.h\"\n"
                "#include \"ui/events/event.h\"\n"
                "#include \"ui/gfx/scoped_animation_duration_scale_mode.h\"\n"
                "#include \"ui/views/event_monitor.h\"\n"
                "#include \"ui/display/screen.h\"\n"
                "#include \"ui/display/display.h\"\n"
            ),
            description="Maho(win): include event/timer APIs for Arc caption reveal",
            guard="#include \"ui/views/event_monitor.h\"",
        ),
        Replacement(
            old=(
                "namespace {\n"
                "\n"
                "std::unique_ptr<WindowsCaptionButton> CreateCaptionButton(\n"
            ),
            new=(
                "namespace {\n"
                "\n"
                "constexpr int kMahoArcCaptionRevealTriggerWidth = 96;\n"
                "constexpr int kMahoArcCaptionRevealTriggerHeight = 8;\n"
                "constexpr base::TimeDelta kMahoArcCaptionHideDelay =\n"
                "    base::Milliseconds(500);\n"
                "\n"
                "std::unique_ptr<WindowsCaptionButton> CreateCaptionButton(\n"
            ),
            description="Maho(win): define Arc caption reveal trigger and hide delay",
            guard="kMahoArcCaptionRevealTriggerWidth",
        ),
        Replacement(
            old=(
                "  DCHECK(HitTestPoint(point))\n"
                "      << \"should only be called with a point inside this view's bounds\";\n"
                "  // BrowserView covers the frame view when Window Controls Overlay is enabled.\n"
            ),
            new=(
                "  DCHECK(HitTestPoint(point))\n"
                "      << \"should only be called with a point inside this view's bounds\";\n"
                "  if (IsMahoArcCaptionMode() && !maho_arc_caption_revealed_) {\n"
                "    return HTNOWHERE;\n"
                "  }\n"
                "  // BrowserView covers the frame view when Window Controls Overlay is enabled.\n"
            ),
            description="Maho(win): hidden Arc caption container does not block hit tests",
            guard="!maho_arc_caption_revealed_",
        ),
        Replacement(
            old=(
                "  if (frame_view_->GetBrowserView()->IsWindowControlsOverlayEnabled() &&\n"
                "      (HitTestCaptionButton(minimize_button_, point) ||\n"
                "       HitTestCaptionButton(maximize_button_, point) ||\n"
                "       HitTestCaptionButton(restore_button_, point) ||\n"
                "       HitTestCaptionButton(close_button_, point))) {\n"
                "    return HTCLIENT;\n"
                "  }\n"
            ),
            new=(
                "  if (!frame_view_->GetBrowserView()->IsMahoArcLayoutActive() &&\n"
                "      frame_view_->GetBrowserView()->IsWindowControlsOverlayEnabled() &&\n"
                "      (HitTestCaptionButton(minimize_button_, point) ||\n"
                "       HitTestCaptionButton(maximize_button_, point) ||\n"
                "       HitTestCaptionButton(restore_button_, point) ||\n"
                "       HitTestCaptionButton(close_button_, point))) {\n"
                "    return HTCLIENT;\n"
                "  }\n"
            ),
            description="Maho(win): let revealed Arc caption buttons return native HT codes for Snap Layout",
            guard="!frame_view_->GetBrowserView()->IsMahoArcLayoutActive() &&",
        ),
        Replacement(
            old=(
                "  UpdateButtonToolTipsForWindowControlsOverlay();\n"
                "}\n"
                "\n"
                "void BrowserCaptionButtonContainer::OnThemeChanged() {\n"
            ),
            new=(
                "  UpdateButtonToolTipsForWindowControlsOverlay();\n"
                "  UpdateMahoArcCaptionEventMonitor();\n"
                "  ApplyMahoArcCaptionButtonVisibility();\n"
                "}\n"
                "\n"
                "void BrowserCaptionButtonContainer::OnThemeChanged() {\n"
            ),
            description="Maho(win): refresh Arc caption monitor when WCO changes",
            guard="UpdateMahoArcCaptionEventMonitor();\n  ApplyMahoArcCaptionButtonVisibility();",
        ),
        Replacement(
            old=(
                "  DCHECK(!widget_observation_.IsObserving());\n"
                "  widget_observation_.Observe(widget);\n"
                "\n"
                "  UpdateButtons();\n"
            ),
            new=(
                "  DCHECK(!widget_observation_.IsObserving());\n"
                "  widget_observation_.Observe(widget);\n"
                "  UpdateMahoArcCaptionEventMonitor();\n"
                "\n"
                "  UpdateButtons();\n"
            ),
            description="Maho(win): start Arc caption event monitor with widget lifecycle",
            guard="  UpdateMahoArcCaptionEventMonitor();\n\n  UpdateButtons();",
        ),
        Replacement(
            old=(
                "void BrowserCaptionButtonContainer::RemovedFromWidget() {\n"
                "  DCHECK(widget_observation_.IsObserving());\n"
                "  widget_observation_.Reset();\n"
                "}\n"
            ),
            new=(
                "void BrowserCaptionButtonContainer::RemovedFromWidget() {\n"
                "  maho_arc_caption_event_monitor_.reset();\n"
                "  maho_arc_caption_hide_timer_.Stop();\n"
                "  maho_arc_caption_revealed_ = false;\n"
                "  DCHECK(widget_observation_.IsObserving());\n"
                "  widget_observation_.Reset();\n"
                "}\n"
            ),
            description="Maho(win): stop Arc caption monitor and timer when removed",
            guard="maho_arc_caption_event_monitor_.reset();\n  maho_arc_caption_hide_timer_.Stop();",
        ),
        Replacement(
            old=(
                "void BrowserCaptionButtonContainer::OnWidgetBoundsChanged(\n"
                "    views::Widget* widget,\n"
                "    const gfx::Rect& new_bounds) {\n"
                "  UpdateButtons();\n"
                "}\n"
            ),
            new=(
                "void BrowserCaptionButtonContainer::OnWidgetBoundsChanged(\n"
                "    views::Widget* widget,\n"
                "    const gfx::Rect& new_bounds) {\n"
                "  maho_arc_caption_hide_timer_.Stop();\n"
                "  maho_arc_caption_revealed_ = false;\n"
                "  UpdateButtons();\n"
                "}\n"
                "\n"
                "void BrowserCaptionButtonContainer::OnWidgetShowStateChanged(\n"
                "    views::Widget* widget) {\n"
                "  maho_arc_caption_hide_timer_.Stop();\n"
                "  maho_arc_caption_revealed_ = false;\n"
                "  UpdateMahoArcCaptionEventMonitor();\n"
                "  UpdateButtons();\n"
                "}\n"
                "\n"
                "void BrowserCaptionButtonContainer::OnWidgetDestroying(\n"
                "    views::Widget* widget) {\n"
                "  maho_arc_caption_event_monitor_.reset();\n"
                "  maho_arc_caption_hide_timer_.Stop();\n"
                "}\n"
                "\n"
                "void BrowserCaptionButtonContainer::OnEvent(const ui::Event& event) {\n"
                "  if (!IsMahoArcCaptionMode() || !maho_arc_caption_event_monitor_) {\n"
                "    return;\n"
                "  }\n"
                "  const gfx::Point screen_point =\n"
                "      maho_arc_caption_event_monitor_->GetLastMouseLocation();\n"
                "  if (maho_arc_caption_revealed_) {\n"
                "    return;\n"
                "  }\n"
                "  if (IsMahoArcCaptionTriggerPoint(screen_point)) {\n"
                "    if (!maho_arc_caption_hide_timer_.IsRunning()) {\n"
                "      maho_arc_caption_hide_timer_.Start(\n"
                "          FROM_HERE, base::Milliseconds(250), this,\n"
                "          &BrowserCaptionButtonContainer::MaybeRevealMahoArcCaption);\n"
                "    }\n"
                "  } else {\n"
                "    maho_arc_caption_hide_timer_.Stop();\n"
                "  }\n"
                "}\n"
            ),
            description="Maho(win): implement Arc caption event/state lifecycle",
            guard="void BrowserCaptionButtonContainer::OnWidgetShowStateChanged(",
        ),
        Replacement(
            old=(
                "  if (!ShouldBrowserCustomDrawTitlebar(frame_view_->GetBrowserView())) {\n"
                "    minimize_button_->SetVisible(false);\n"
                "    maximize_button_->SetVisible(false);\n"
                "    restore_button_->SetVisible(false);\n"
                "    close_button_->SetVisible(false);\n"
                "    return;\n"
                "  }\n"
            ),
            new=(
                "  if (!ShouldBrowserCustomDrawTitlebar(frame_view_->GetBrowserView())) {\n"
                "    minimize_button_->SetVisible(false);\n"
                "    maximize_button_->SetVisible(false);\n"
                "    restore_button_->SetVisible(false);\n"
                "    close_button_->SetVisible(false);\n"
                "    ApplyMahoArcCaptionButtonVisibility();\n"
                "    return;\n"
                "  }\n"
            ),
            description="Maho(win): keep Arc caption hidden even when titlebar state hides buttons",
            guard="ApplyMahoArcCaptionButtonVisibility();\n    return;",
        ),
        Replacement(
            old=(
                "  restore_button_->SetEnabled(!is_touch);\n"
                "  maximize_button_->SetEnabled(!is_touch || !is_maximized);\n"
                "}\n"
                "\n"
                "void BrowserCaptionButtonContainer::\n"
            ),
            new=(
                "  restore_button_->SetEnabled(!is_touch);\n"
                "  maximize_button_->SetEnabled(!is_touch || !is_maximized);\n"
                "  ApplyMahoArcCaptionButtonVisibility();\n"
                "}\n"
                "\n"
                "void BrowserCaptionButtonContainer::\n"
            ),
            description="Maho(win): apply Arc caption reveal visibility after normal button state",
            guard="maximize_button_->SetEnabled(!is_touch || !is_maximized);\n  ApplyMahoArcCaptionButtonVisibility();",
        ),
        Replacement(
            old=(
                "void BrowserCaptionButtonContainer::\n"
                "    UpdateButtonToolTipsForWindowControlsOverlay() {\n"
                "  if (frame_view_->GetBrowserView()->IsWindowControlsOverlayEnabled()) {\n"
            ),
            new=(
                "bool BrowserCaptionButtonContainer::IsMahoArcCaptionMode() const {\n"
                "  return frame_view_->GetBrowserView()->IsMahoArcLayoutActive();\n"
                "}\n"
                "\n"
                "bool BrowserCaptionButtonContainer::IsMahoArcCaptionTriggerPoint(\n"
                "    const gfx::Point& screen_point) const {\n"
                "  views::Widget* widget = GetWidget();\n"
                "  if (!widget || widget->IsFullscreen()) {\n"
                "    return false;\n"
                "  }\n"
                "  // Maho: reveal when the pointer touches the full-width strip along\n"
                "  // the very top edge of the window (the hidden handle bar hotzone).\n"
                "  // Clamp to the display work area so the hotzone stays reachable when\n"
                "  // maximized (a maximized window's top border overhangs off-screen).\n"
                "  gfx::Rect trigger_bounds = widget->GetWindowBoundsInScreen();\n"
                "  trigger_bounds.Intersect(\n"
                "      display::Screen::Get()\n"
                "          ->GetDisplayNearestWindow(widget->GetNativeWindow())\n"
                "          .work_area());\n"
                "  if (trigger_bounds.width() <= 0 || trigger_bounds.height() <= 0) {\n"
                "    return false;\n"
                "  }\n"
                "  trigger_bounds.set_height(std::min(kMahoArcCaptionRevealTriggerHeight,\n"
                "                                      trigger_bounds.height()));\n"
                "  return trigger_bounds.Contains(screen_point);\n"
                "}\n"
                "\n"
                "bool BrowserCaptionButtonContainer::IsMahoArcCaptionKeepAlivePoint(\n"
                "    const gfx::Point& screen_point) const {\n"
                "  return GetBoundsInScreen().Contains(screen_point) ||\n"
                "         IsMahoArcCaptionTriggerPoint(screen_point);\n"
                "}\n"
                "\n"
                "int BrowserCaptionButtonContainer::GetMahoArcCaptionPreferredWidth()\n"
                "    const {\n"
                "  int width = 0;\n"
                "  if (frame_view_->GetBrowserView()->CanMinimize()) {\n"
                "    width += minimize_button_->GetPreferredSize().width();\n"
                "  }\n"
                "  if (frame_view_->GetBrowserView()->CanMaximize()) {\n"
                "    width += (frame_view_->IsMaximized() ? restore_button_\n"
                "                                       : maximize_button_)\n"
                "                 ->GetPreferredSize()\n"
                "                 .width();\n"
                "  }\n"
                "  width += close_button_->GetPreferredSize().width();\n"
                "  return std::max(width, kMahoArcCaptionRevealTriggerWidth);\n"
                "}\n"
                "\n"
                "void BrowserCaptionButtonContainer::MaybeRevealMahoArcCaption() {\n"
                "  if (maho_arc_caption_revealed_) {\n"
                "    return;\n"
                "  }\n"
                "  const gfx::Point cursor =\n"
                "      display::Screen::Get()->GetCursorScreenPoint();\n"
                "  if (IsMahoArcCaptionTriggerPoint(cursor)) {\n"
                "    RevealMahoArcCaptionButtons();\n"
                "  }\n"
                "}\n"
                "\n"
                "void BrowserCaptionButtonContainer::RevealMahoArcCaptionButtons() {\n"
                "  if (!IsMahoArcCaptionMode() ||\n"
                "      frame_view_->browser_widget()->IsFullscreen()) {\n"
                "    return;\n"
                "  }\n"
                "  if (!maho_arc_caption_hide_timer_.IsRunning()) {\n"
                "    maho_arc_caption_hide_timer_.Start(\n"
                "        FROM_HERE, base::Milliseconds(250), this,\n"
                "        &BrowserCaptionButtonContainer::PollMahoArcCaptionHide);\n"
                "  }\n"
                "  if (maho_arc_caption_revealed_) {\n"
                "    return;\n"
                "  }\n"
                "  maho_arc_caption_revealed_ = true;\n"
                "  UpdateButtons();\n"
                "  frame_view_->InvalidateLayout();\n"
                "  frame_view_->DeprecatedLayoutImmediately();\n"
                "}\n"
                "\n"
                "void BrowserCaptionButtonContainer::PollMahoArcCaptionHide() {\n"
                "  if (!maho_arc_caption_revealed_) {\n"
                "    return;\n"
                "  }\n"
                "  const gfx::Point cursor =\n"
                "      display::Screen::Get()->GetCursorScreenPoint();\n"
                "  if (!frame_view_->browser_widget()->IsFullscreen() &&\n"
                "      IsMahoArcCaptionKeepAlivePoint(cursor)) {\n"
                "    maho_arc_caption_hide_timer_.Start(\n"
                "        FROM_HERE, base::Milliseconds(250), this,\n"
                "        &BrowserCaptionButtonContainer::PollMahoArcCaptionHide);\n"
                "    return;\n"
                "  }\n"
                "  HideMahoArcCaptionButtons();\n"
                "}\n"
                "\n"
                "void BrowserCaptionButtonContainer::ScheduleHideMahoArcCaptionButtons() {\n"
                "  if (!maho_arc_caption_revealed_) {\n"
                "    return;\n"
                "  }\n"
                "  if (gfx::ScopedAnimationDurationScaleMode::is_zero()) {\n"
                "    HideMahoArcCaptionButtons();\n"
                "    return;\n"
                "  }\n"
                "  maho_arc_caption_hide_timer_.Start(\n"
                "      FROM_HERE, kMahoArcCaptionHideDelay, this,\n"
                "      &BrowserCaptionButtonContainer::HideMahoArcCaptionButtons);\n"
                "}\n"
                "\n"
                "void BrowserCaptionButtonContainer::HideMahoArcCaptionButtons() {\n"
                "  if (!maho_arc_caption_revealed_) {\n"
                "    return;\n"
                "  }\n"
                "  maho_arc_caption_hide_timer_.Stop();\n"
                "  maho_arc_caption_revealed_ = false;\n"
                "  UpdateButtons();\n"
                "  frame_view_->InvalidateLayout();\n"
                "  frame_view_->DeprecatedLayoutImmediately();\n"
                "}\n"
                "\n"
                "void BrowserCaptionButtonContainer::\n"
                "    ApplyMahoArcCaptionButtonVisibility() {\n"
                "  if (!IsMahoArcCaptionMode()) {\n"
                "    SetCanProcessEventsWithinSubtree(true);\n"
                "    SetPreferredSize(std::nullopt);\n"
                "    return;\n"
                "  }\n"
                "\n"
                "  const bool revealed = maho_arc_caption_revealed_ &&\n"
                "                        !frame_view_->browser_widget()->IsFullscreen();\n"
                "  if (!revealed) {\n"
                "    minimize_button_->SetVisible(false);\n"
                "    maximize_button_->SetVisible(false);\n"
                "    restore_button_->SetVisible(false);\n"
                "    close_button_->SetVisible(false);\n"
                "  }\n"
                "  SetCanProcessEventsWithinSubtree(revealed);\n"
                "  SetPreferredSize(gfx::Size(\n"
                "      revealed ? GetMahoArcCaptionPreferredWidth()\n"
                "               : 0,\n"
                "      0));\n"
                "}\n"
                "\n"
                "void BrowserCaptionButtonContainer::UpdateMahoArcCaptionEventMonitor() {\n"
                "  views::Widget* const widget = GetWidget();\n"
                "  if (!widget || !IsMahoArcCaptionMode()) {\n"
                "    maho_arc_caption_event_monitor_.reset();\n"
                "    return;\n"
                "  }\n"
                "  if (!maho_arc_caption_event_monitor_) {\n"
                "    maho_arc_caption_event_monitor_ = views::EventMonitor::CreateWindowMonitor(\n"
                "        this, widget->GetNativeWindow(),\n"
                "        {ui::EventType::kMouseMoved, ui::EventType::kMouseExited});\n"
                "  }\n"
                "}\n"
                "\n"
                "void BrowserCaptionButtonContainer::\n"
                "    UpdateButtonToolTipsForWindowControlsOverlay() {\n"
                "  if (frame_view_->GetBrowserView()->IsWindowControlsOverlayEnabled()) {\n"
            ),
            description="Maho(win): add Arc caption reveal helpers before WCO tooltip updater",
            guard="void BrowserCaptionButtonContainer::RevealMahoArcCaptionButtons()",
        ),
    ],
    "chrome/browser/ui/views/frame/browser_frame_view_win.cc": [
        Replacement(
            old=(
                "int BrowserFrameViewWin::GetTopInset(bool restored) const {\n"
                "  if (GetBrowserView()->GetTabStripVisible()) {\n"
            ),
            new=(
                "int BrowserFrameViewWin::GetTopInset(bool restored) const {\n"
                "  // Maho(win): the Arc edge-to-edge layout reserves no titlebar strip;\n"
                "  // the sidebar + contents fill to the very top of the window and the\n"
                "  // caption handle bar is revealed on hover as a layer-backed overlay.\n"
                "  if (GetBrowserView()->IsMahoArcLayoutActive()) {\n"
                "    return 0;\n"
                "  }\n"
                "  if (GetBrowserView()->GetTabStripVisible()) {\n"
            ),
            description=(
                "Maho(win): zero the top inset in Arc mode so content fills to the "
                "window top and no persistent handle bar strip is reserved"
            ),
            guard="if (GetBrowserView()->IsMahoArcLayoutActive()) {\n    return 0;",
        ),
        Replacement(
            old=(
                "void BrowserFrameViewWin::PaintTitlebar(gfx::Canvas* canvas) const {\n"
                "  TRACE_EVENT0(\"views.frame\", \"BrowserFrameViewWin::PaintTitlebar\");\n"
            ),
            new=(
                "void BrowserFrameViewWin::PaintTitlebar(gfx::Canvas* canvas) const {\n"
                "  TRACE_EVENT0(\"views.frame\", \"BrowserFrameViewWin::PaintTitlebar\");\n"
                "  // Maho(win): do not paint the titlebar strip in the Arc edge-to-edge\n"
                "  // layout. The handle bar is hidden by default and revealed on hover\n"
                "  // purely through the layer-backed caption button container.\n"
                "  if (GetBrowserView()->IsMahoArcLayoutActive()) {\n"
                "    return;\n"
                "  }\n"
            ),
            description=(
                "Maho(win): suppress the always-visible titlebar/handle-bar strip in "
                "Arc mode; reveal-on-hover is handled by the caption container"
            ),
            guard=(
                "// Maho(win): do not paint the titlebar strip in the Arc edge-to-edge"
            ),
        ),
    ],
    "chrome/app/chrome_main_delegate.cc": [
        Replacement(
            old=(
                "std::optional<int> ChromeMainDelegate::BasicStartupComplete() {\n"
                "#if BUILDFLAG(IS_CHROMEOS)\n"
                "  ash::BootTimesRecorder::Get()->SaveChromeMainStats();\n"
                "#endif\n"
            ),
            new=(
                "std::optional<int> ChromeMainDelegate::BasicStartupComplete() {\n"
                "#if BUILDFLAG(IS_CHROMEOS)\n"
                "  ash::BootTimesRecorder::Get()->SaveChromeMainStats();\n"
                "#endif\n"
                "\n"
                "  // Maho: enable kGlassFrame by default on macOS.\n"
                "  // Must run before base::FeatureList::InitializeInstance to take\n"
                "  // effect. Users can still opt out with --disable-features=GlassFrame.\n"
                "#if BUILDFLAG(IS_MAC)\n"
                "  {\n"
                "    auto* cmd = base::CommandLine::ForCurrentProcess();\n"
                "    const std::string disabled =\n"
                "        cmd->GetSwitchValueASCII(\"disable-features\");\n"
                "    if (disabled.find(\"GlassFrame\") == std::string::npos) {\n"
                "      const std::string existing =\n"
                "          cmd->GetSwitchValueASCII(\"enable-features\");\n"
                "      cmd->AppendSwitchASCII(\n"
                "          \"enable-features\",\n"
                "          existing.empty() ? std::string(\"GlassFrame\")\n"
                "                           : existing + \",GlassFrame\");\n"
                "    }\n"
                "  }\n"
                "#endif\n"
            ),
            description=(
                "Maho: enable kGlassFrame by default on macOS for NSWindow "
                "transparency + NSVisualEffectView sidebar vibrancy"
            ),
            guard="Maho: enable kGlassFrame by default on macOS",
        ),
    ],
    "chrome/browser/ui/views/frame/multi_contents_view.h": [
        Replacement(
            old="  static constexpr int kSplitViewContentInset = 8;\n",
            new="  static constexpr int kSplitViewContentInset = 0;\n",
            description=(
                "Maho: zero split-view content inset so split panes sit flush to "
                "the window edges like a single pane (no gray content frame)"
            ),
            guard="static constexpr int kSplitViewContentInset = 0;",
        ),
    ],
    "chrome/browser/ui/views/frame/multi_contents_view.cc": [
        Replacement(
            old=(
                "  const bool show_background =\n"
                "      drop_target_view_->GetVisible() || IsInSplitView();\n"
            ),
            new=(
                "  const bool show_background =\n"
                "      drop_target_view_->GetVisible();\n"
            ),
            description=(
                "Maho: do not paint the gray kColorToolbar MultiContentsBackgroundView "
                "behind split panes (only during drag-drop); it was the visible gray "
                "rounded frame around split content"
            ),
            guard="  const bool show_background =\n      drop_target_view_->GetVisible();\n",
        ),
    ],
    "chrome/browser/ui/views/frame/contents_container_view.cc": [
        Replacement(
            old="    container_outline_->UpdateState(is_active, is_highlighted);\n",
            new="    container_outline_->SetVisible(false);\n",
            description=(
                "Maho: hide ContentsContainerOutline in split. Its OnPaint draws a "
                "rounded-rect stroke (kCornerRadius=8); with kThickness=0 Skia still "
                "renders a 1px hairline whose rounded bottom corners showed as faint "
                "notches. Hiding the view removes them entirely."
            ),
        ),
        Replacement(
            old="constexpr float kContentCornerRadius = 6;\n",
            new="constexpr float kContentCornerRadius = 0;\n",
            description=(
                "Maho: square split-view content corners to match the flush "
                "single-pane look"
            ),
            guard="constexpr float kContentCornerRadius = 0;",
        ),
        Replacement(
            old="constexpr int kSplitViewContentPadding = 4;\n",
            new="constexpr int kSplitViewContentPadding = 0;\n",
            description="Maho: remove split-view content padding for flush panes",
            guard="constexpr int kSplitViewContentPadding = 0;",
        ),
        Replacement(
            old="    mini_toolbar_->UpdateState(is_active, is_highlighted);\n",
            new="    mini_toolbar_->SetVisible(false);\n",
            description=(
                "Maho: hide the split-view mini toolbar (inactive-pane favicon/"
                "domain/close pill) for a clean borderless split"
            ),
        ),
    ],
    "chrome/browser/profiles/chrome_browser_main_extra_parts_profiles.cc": [
        Replacement(
            old='#include "chrome/browser/webdata_services/web_data_service_factory.h"\n',
            new=(
                '#include "chrome/browser/webdata_services/web_data_service_factory.h"\n'
                '#include "maho/browser/mail_helper/maho_mail_service_factory.h"\n'
                '#include "maho/browser/ui/downloads/maho_download_bridge_service_factory.h"\n'
            ),
            description="Include Maho service factories in profiles.cc",
        ),
        Replacement(
            old='  WebDataServiceFactory::GetInstance();\n',
            new=(
                '  WebDataServiceFactory::GetInstance();\n'
                '  maho::MahoMailServiceFactory::GetInstance();\n'
                '  maho::MahoDownloadBridgeServiceFactory::GetInstance();\n'
            ),
            description="Initialize Maho service factories in profiles.cc",
        ),
    ],
    "chrome/browser/ui/views/frame/contents_container_outline.h": [
        Replacement(
            old="  static constexpr int kThickness = 1;\n",
            new="  static constexpr int kThickness = 0;\n",
            description=(
                "Maho: zero content outline thickness so the per-pane empty "
                "border collapses and split content is fully flush (outline is "
                "already transparent via the color mixer)"
            ),
            guard="static constexpr int kThickness = 0;",
        ),
    ],
    "ui/base/ui_base_features.cc": [
        Replacement(
            old="BASE_FEATURE(kSplitViewLinkOpen, base::FEATURE_DISABLED_BY_DEFAULT);",
            new="BASE_FEATURE(kSplitViewLinkOpen, base::FEATURE_ENABLED_BY_DEFAULT);",
            description="Maho: Enable split view link open by default",
        ),
    ],
    "chrome/browser/renderer_context_menu/render_view_context_menu.cc": [
        Replacement(
            old='      menu_model_.AddItem(IDC_MAHO_LINK_OPEN_LITTLE_ARC, u"Open in Little Arc");',
            new='      menu_model_.AddItem(IDC_MAHO_LINK_OPEN_LITTLE_ARC, u"Open in Maho Mini");',
            description="Maho: Rename context menu item to Open in Maho Mini",
        ),
    ],
    "chrome/browser/ui/browser.h": [
        Replacement(
            old=(
                "    // Document Picture in Picture options, specific to TYPE_PICTURE_IN_PICTURE.\n"
                "    std::optional<blink::mojom::PictureInPictureWindowOptions> pip_options;\n"
            ),
            new=(
                "    // Document Picture in Picture options, specific to TYPE_PICTURE_IN_PICTURE.\n"
                "    std::optional<blink::mojom::PictureInPictureWindowOptions> pip_options;\n"
                "\n"
                "    bool is_little_arc = false;\n"
            ),
            description="Maho: Add is_little_arc to CreateParams",
            guard="bool is_little_arc = false;",
        ),
        Replacement(
            old=(
                "  ~Browser() override;\n"
            ),
            new=(
                "  ~Browser() override;\n"
                "\n"
                "  bool is_little_arc() const { return is_little_arc_; }\n"
            ),
            description="Maho: Add is_little_arc getter to Browser",
            guard="is_little_arc() const",
        ),
        Replacement(
            old=(
                "  // This Browser's type.\n"
                "  const Type type_;\n"
            ),
            new=(
                "  // This Browser's type.\n"
                "  const Type type_;\n"
                "\n"
                "  const bool is_little_arc_;\n"
            ),
            description="Maho: Add is_little_arc_ member to Browser",
            guard="const bool is_little_arc_;",
        ),
    ],
    "chrome/browser/ui/browser.cc": [
        Replacement(
            old=(
                "Browser::Browser(const CreateParams& params)\n"
                "    : create_params_(params),\n"
                "      type_(params.type),\n"
                "      profile_(params.profile),\n"
            ),
            new=(
                "Browser::Browser(const CreateParams& params)\n"
                "    : create_params_(params),\n"
                "      type_(params.type),\n"
                "      is_little_arc_(params.is_little_arc),\n"
                "      profile_(params.profile),\n"
            ),
            description="Maho: Initialize is_little_arc_ in Browser constructor",
            guard="is_little_arc_(params.is_little_arc)",
        ),
        Replacement(
            old=(
                "bool Browser::SupportsWindowFeatureImpl(WindowFeature feature,\n"
                "                                        bool check_can_support) const {\n"
                "  switch (type_) {\n"
            ),
            new=(
                "bool Browser::SupportsWindowFeatureImpl(WindowFeature feature,\n"
                "                                        bool check_can_support) const {\n"
                "  if (is_little_arc()) {\n"
                "    return false;\n"
                "  }\n"
                "  switch (type_) {\n"
            ),
            description="Maho: Disable window features for Little Arc windows",
            guard="if (is_little_arc()) {",
        ),
    ],
    "chrome/browser/ui/views/frame/browser_native_widget_mac.mm": [
        Replacement(
            old=(
                "  } else if (browser_view_->GetIsNormalType() ||\n"
                "             browser_view_->GetIsWebAppType()) {\n"
                "    params->window_class = remote_cocoa::mojom::WindowClass::kBrowser;\n"
                "    params->style_mask |= NSWindowStyleMaskFullSizeContentView;\n"
                "\n"
                "    // Ensure tabstrip/profile button are visible.\n"
                "    params->titlebar_appears_transparent = true;\n"
                "\n"
                "    // Hosted apps draw their own window title.\n"
                "    if (browser_view_->GetIsWebAppType()) {\n"
                "      params->window_title_hidden = true;\n"
                "    }\n"
                "  } else {\n"
            ),
            new=(
                "  } else if (browser_view_->GetIsNormalType() ||\n"
                "             browser_view_->GetIsWebAppType() ||\n"
                "             browser_view_->browser()->is_little_arc()) {\n"
                "    params->window_class = remote_cocoa::mojom::WindowClass::kBrowser;\n"
                "    params->style_mask |= NSWindowStyleMaskFullSizeContentView;\n"
                "\n"
                "    // Ensure tabstrip/profile button are visible.\n"
                "    params->titlebar_appears_transparent = true;\n"
                "\n"
                "    if (browser_view_->browser()->is_little_arc()) {\n"
                "      params->window_title_hidden = true;\n"
                "    } else if (browser_view_->GetIsWebAppType()) {\n"
                "      params->window_title_hidden = true;\n"
                "    }\n"
                "  } else {\n"
            ),
            description="Maho: Style Little Arc window with transparent titlebar and full size content",
            guard="browser_view_->browser()->is_little_arc()",
        ),
        Replacement(
            old=(
                "    // height reported here; report the 38dip Little Arc top bar\n"
                "    // height so they center on its midline, not near the top edge.\n"
                "    *override_titlebar_height = true;\n"
                "    *titlebar_height = 38;\n"
            ),
            new=(
                "    // height reported here; report the 46dip Little Arc top bar\n"
                "    // height so they center on its midline, not near the top edge.\n"
                "    *override_titlebar_height = true;\n"
                "    *titlebar_height = 46;\n"
            ),
            description="Maho: Raise an existing Little Arc titlebar override to 46dip",
            idempotent=True,
            guard="*titlebar_height = 46;",
        ),
        Replacement(
            old=(
                "void BrowserNativeWidgetMac::GetWindowFrameTitlebarHeight(\n"
                "    bool* override_titlebar_height,\n"
                "    float* titlebar_height) {\n"
                "  if (browser_view_ && browser_view_->browser_widget() &&\n"
            ),
            new=(
                "void BrowserNativeWidgetMac::GetWindowFrameTitlebarHeight(\n"
                "    bool* override_titlebar_height,\n"
                "    float* titlebar_height) {\n"
                "  if (browser_view_ && browser_view_->browser() &&\n"
                "      browser_view_->browser()->is_little_arc()) {\n"
                "    // Maho: macOS centers the traffic lights within the titlebar\n"
                "    // height reported here; report the 46dip Little Arc top bar\n"
                "    // height so they center on its midline, not near the top edge.\n"
                "    *override_titlebar_height = true;\n"
                "    *titlebar_height = 46;\n"
                "    return;\n"
                "  }\n"
                "  if (browser_view_ && browser_view_->browser_widget() &&\n"
            ),
            description="Maho: Center traffic lights in Little Arc 46dip top bar",
            guard="*titlebar_height = 46;",
        ),
    ],
    "chrome/browser/ui/views/frame/browser_frame_view_mac.mm": [
        Replacement(
            old=(
                "  // In popups, the titlebar is system-drawn and the caption buttons aren't part\n"
                "  // of the client area.\n"
                "  if (GetBrowserView()->browser()->is_type_popup() ||\n"
                "      GetBrowserView()->browser()->is_type_devtools()) {\n"
                "    return result;\n"
                "  }\n"
            ),
            new=(
                "  // In popups, the titlebar is system-drawn and the caption buttons aren't part\n"
                "  // of the client area.\n"
                "  if ((GetBrowserView()->browser()->is_type_popup() && !GetBrowserView()->browser()->is_little_arc()) ||\n"
                "      GetBrowserView()->browser()->is_type_devtools()) {\n"
                "    return result;\n"
                "  }\n"
            ),
            description="Maho: Allow caption button bounds for Little Arc popups",
            guard="!GetBrowserView()->browser()->is_little_arc()",
        ),
    ],
    "chrome/browser/ui/views/frame/browser_view.h": [
        Replacement(
            old="class MahoCommandOverlayController;\n",
            new=(
                "class MahoCommandOverlayController;\n"
                "class MahoContentGradientView;\n"
            ),
            description=(
                "Forward-declare MahoContentGradientView for BrowserView's "
                "zero-tab content background"
            ),
            guard="class MahoContentGradientView;",
        ),
        Replacement(
            old=(
                "  // Maho: left sidebar host container.\n"
                "  raw_ptr<views::View> maho_sidebar_container_ = nullptr;\n"
            ),
            new=(
                "  // Maho: left sidebar host container.\n"
                "  raw_ptr<views::View> maho_sidebar_container_ = nullptr;\n"
                "\n"
                "  raw_ptr<views::View> maho_little_arc_top_bar_ = nullptr;\n"
            ),
            description="Maho: Add maho_little_arc_top_bar_ to BrowserView",
            guard="maho_little_arc_top_bar_",
        ),
        Replacement(
            old="  raw_ptr<views::View> maho_little_arc_top_bar_ = nullptr;\n",
            new=(
                "  raw_ptr<views::View> maho_little_arc_top_bar_ = nullptr;\n"
                "\n"
                "  raw_ptr<maho::MahoContentGradientView>\n"
                "      maho_content_gradient_view_ = nullptr;\n"
            ),
            description=(
                "Store the lowest-z Maho content gradient child on BrowserView"
            ),
            guard="maho_content_gradient_view_ = nullptr;",
        ),
    ],
    "chrome/browser/ui/views/frame/browser_view.cc": [
        Replacement(
            old=(
                '#include "chrome/browser/ui/views/frame/browser_view.h"\n'
            ),
            new=(
                '#include "chrome/browser/ui/views/frame/browser_view.h"\n'
                '#include "maho/browser/ui/views/little_arc/maho_little_arc_top_bar_view.h"\n'
            ),
            description="Maho: Include maho_little_arc_top_bar_view.h in browser_view.cc",
            guard="maho_little_arc_top_bar_view.h",
        ),
        Replacement(
            old='#include "maho/browser/ui/views/little_arc/maho_little_arc_top_bar_view.h"\n',
            new=(
                '#include "maho/browser/ui/views/little_arc/maho_little_arc_top_bar_view.h"\n'
                '#include "maho/browser/ui/views/maho_content_gradient_view.h"\n'
            ),
            description="Maho: Include maho_content_gradient_view.h in browser_view.cc",
            guard="maho_content_gradient_view.h",
        ),
        Replacement(
            old=(
                "  auto contents_container = std::make_unique<views::View>();\n"
                "\n"
                "  views::View* contents_view;\n"
            ),
            new=(
                "  auto contents_container = std::make_unique<views::View>();\n"
                "\n"
                "  if (GetIsNormalType()) {\n"
                "    maho_content_gradient_view_ = contents_container->AddChildView(\n"
                "        std::make_unique<maho::MahoContentGradientView>());\n"
                "    maho_content_gradient_view_->SetVisible(\n"
                "        browser_->tab_strip_model()->count() == 0);\n"
                "  }\n"
                "\n"
                "  views::View* contents_view;\n"
            ),
            description=(
                "Create MahoContentGradientView as the first content-container "
                "child and initialize it from the live tab count"
            ),
            guard="maho_content_gradient_view_ = contents_container->AddChildView(",
        ),
        Replacement(
            old=(
                "  contents_container_ = AddChildView(std::move(contents_container));\n"
                "  set_contents_view(contents_container_);\n"
            ),
            new=(
                "  contents_container_ = AddChildView(std::move(contents_container));\n"
                "  set_contents_view(contents_container_);\n"
                "\n"
                "  if (browser_->is_little_arc()) {\n"
                "    maho_little_arc_top_bar_ = AddChildView(\n"
                "        std::make_unique<maho::MahoLittleArcTopBarView>(browser_.get(), this));\n"
                "  }\n"
            ),
            description="Maho: Initialize maho_little_arc_top_bar_ in BrowserView::InitViews",
            guard="maho_little_arc_top_bar_ = AddChildView",
        ),
        Replacement(
            old=(
                "void BrowserView::OnTabStripModelChanged(\n"
                "    TabStripModel* tab_strip_model,\n"
                "    const TabStripModelChange& change,\n"
                "    const TabStripSelectionChange& selection) {\n"
            ),
            new=(
                "void BrowserView::OnTabStripModelChanged(\n"
                "    TabStripModel* tab_strip_model,\n"
                "    const TabStripModelChange& change,\n"
                "    const TabStripSelectionChange& selection) {\n"
                "  if (maho_content_gradient_view_) {\n"
                "    maho_content_gradient_view_->SetVisible(\n"
                "        tab_strip_model->count() == 0);\n"
                "  }\n"
                "\n"
            ),
            description=(
                "Toggle the Maho content gradient whenever BrowserView observes "
                "a live-tab count change"
            ),
            guard="tab_strip_model->count() == 0",
        ),
        Replacement(
            old=(
                "BrowserView::~BrowserView() {\n"
                "  // Remove the layout manager to avoid dangling. This needs to be earlier than\n"
                "  // other cleanups that destroy views referenced in the layout manager.\n"
                "  SetLayoutManager(nullptr);\n"
            ),
            new=(
                "BrowserView::~BrowserView() {\n"
                "  browser_->tab_strip_model()->RemoveObserver(this);\n"
                "\n"
                "  // Remove the layout manager to avoid dangling. This needs to be earlier than\n"
                "  // other cleanups that destroy views referenced in the layout manager.\n"
                "  SetLayoutManager(nullptr);\n"
            ),
            description=(
                "Remove BrowserView's TabStripModel observation before gradient "
                "and content children are destroyed"
            ),
            guard="browser_->tab_strip_model()->RemoveObserver(this);",
        ),
        Replacement(
            old=(
                "  window_scrim_view_ = nullptr;\n"
                "  contents_container_ = nullptr;\n"
            ),
            new=(
                "  window_scrim_view_ = nullptr;\n"
                "  maho_content_gradient_view_ = nullptr;\n"
                "  contents_container_ = nullptr;\n"
            ),
            description=(
                "Clear BrowserView's raw Maho content gradient pointer before "
                "removing the child hierarchy"
            ),
            guard="  maho_content_gradient_view_ = nullptr;\n  contents_container_ = nullptr;",
        ),
        Replacement(
            old=(
                "  layout_views.maho_sidebar_container = maho_sidebar_container_;\n"
                "  layout_views.maho_create_space_blank_view = maho_create_space_blank_view_;\n"
                "  // LINT.ThenChange(//chrome/browser/ui/views/frame/layout/browser_view_layout.h:BrowserViewLayoutViews)\n"
            ),
            new=(
                "  layout_views.maho_sidebar_container = maho_sidebar_container_;\n"
                "  layout_views.maho_create_space_blank_view = maho_create_space_blank_view_;\n"
                "  layout_views.maho_little_arc_top_bar = maho_little_arc_top_bar_;\n"
                "  // LINT.ThenChange(//chrome/browser/ui/views/frame/layout/browser_view_layout.h:BrowserViewLayoutViews)\n"
            ),
            description="Maho: Populate maho_little_arc_top_bar in layout_views",
            guard="layout_views.maho_little_arc_top_bar =",
        ),
        Replacement(
            old=(
                "  layout_views.maho_little_arc_top_bar = maho_little_arc_top_bar_;\n"
                "  // LINT.ThenChange(//chrome/browser/ui/views/frame/layout/browser_view_layout.h:BrowserViewLayoutViews)\n"
            ),
            new=(
                "  layout_views.maho_little_arc_top_bar = maho_little_arc_top_bar_;\n"
                "  layout_views.maho_content_gradient_view = maho_content_gradient_view_;\n"
                "  // LINT.ThenChange(//chrome/browser/ui/views/frame/layout/browser_view_layout.h:BrowserViewLayoutViews)\n"
            ),
            description="Maho: Populate maho_content_gradient_view in layout_views",
            guard="layout_views.maho_content_gradient_view =",
        ),
        Replacement(
            old=(
                "  multi_contents_view_ =\n"
                "      contents_container->AddChildView(std::move(multi_contents_view));\n"
                "  contents_view = multi_contents_view_;\n"
            ),
            new=(
                "  multi_contents_view_ =\n"
                "      contents_container->AddChildView(std::move(multi_contents_view));\n"
                "  contents_view = multi_contents_view_;\n"
                "\n"
                "  // Maho: raise the zero-tab content gradient above the opaque\n"
                "  // MultiContentsView so the empty (cold-start) content area shows the\n"
                "  // theme gradient instead of the gray empty-web-contents background.\n"
                "  // The gradient is only SetVisible(true) while the tab strip is empty,\n"
                "  // so once a real tab exists it is hidden and never occludes web content.\n"
                "  if (maho_content_gradient_view_) {\n"
                "    contents_container->ReorderChildView(\n"
                "        maho_content_gradient_view_, contents_container->children().size());\n"
                "  }\n"
            ),
            description=(
                "Maho: reorder the zero-tab content gradient above the opaque "
                "MultiContentsView so the empty content area paints the theme "
                "gradient instead of Chromium's gray empty-web-contents background"
            ),
            guard="Maho: raise the zero-tab content gradient above the opaque",
        ),
    ],
    "chrome/browser/ui/views/frame/layout/browser_view_layout.h": [
        Replacement(
            old=(
                "  // Maho: BrowserView-owned sidebar host container.\n"
                "  raw_ptr<views::View> maho_sidebar_container = nullptr;\n"
                "\n"
                "  raw_ptr<views::View> maho_create_space_blank_view = nullptr;\n"
            ),
            new=(
                "  // Maho: BrowserView-owned sidebar host container.\n"
                "  raw_ptr<views::View> maho_sidebar_container = nullptr;\n"
                "\n"
                "  raw_ptr<views::View> maho_create_space_blank_view = nullptr;\n"
                "\n"
                "  raw_ptr<views::View> maho_little_arc_top_bar = nullptr;\n"
            ),
            description="Maho: Add maho_little_arc_top_bar to BrowserViewLayoutViews",
            guard="maho_little_arc_top_bar = nullptr;",
        ),
        Replacement(
            old="  raw_ptr<views::View> maho_little_arc_top_bar = nullptr;\n",
            new=(
                "  raw_ptr<views::View> maho_little_arc_top_bar = nullptr;\n"
                "\n"
                "  raw_ptr<views::View> maho_content_gradient_view = nullptr;\n"
            ),
            description="Maho: Add maho_content_gradient_view to BrowserViewLayoutViews",
            guard="maho_content_gradient_view = nullptr;",
        ),
    ],
    "chrome/browser/ui/views/frame/layout/browser_view_tabbed_layout_impl.cc": [
        Replacement(
            old=(
                "  layout.AddChild(views().contents_container, contents_bounds);\n"
                "\n"
                "  if (views().maho_create_space_blank_view &&\n"
            ),
            new=(
                "  layout.AddChild(views().contents_container, contents_bounds);\n"
                "\n"
                "  if (views().maho_content_gradient_view) {\n"
                "    views().maho_content_gradient_view->SetBoundsRect(\n"
                "        gfx::Rect(contents_bounds.size()));\n"
                "  }\n"
                "\n"
                "  if (views().maho_create_space_blank_view &&\n"
            ),
            description=(
                "Size the nested Maho content gradient to the Arc content rect "
                "during BrowserView layout"
            ),
            guard="views().maho_content_gradient_view->SetBoundsRect(",
        ),
    ],
    # ── contents_layout_manager.cc: robustly size the zero-tab content gradient ──
    # ContentsLayoutManager (the layout manager that owns the top-level
    # contents_container's children) only emits child_layouts for the contents
    # view, lens overlay, and AI-highlight view. LayoutManagerBase::ApplyLayout
    # only sizes children present in child_layouts, so the injected
    # MahoContentGradientView keeps its default 0x0 bounds and its OnPaint
    # early-returns on GetContentsBounds().IsEmpty() -> the opaque gray empty
    # MultiContentsView shows through. Size any extra child (the gradient) to
    # the full contents bounds here, in the correct layout scope, so it paints
    # every relayout. contents_container is the ONLY view using
    # ContentsLayoutManager (split-view ContentsContainerView uses
    # DelegatingLayoutManager), so the gradient is the sole extra child touched.
    "chrome/browser/ui/views/frame/contents_layout_manager.cc": [
        Replacement(
            old=(
                "  // The AI highlight view bounds are the same as the contents view.\n"
                "  CHECK(context_highlight_view_);\n"
                "  layouts.child_layouts.emplace_back(context_highlight_view_.get(),\n"
                "                                     context_highlight_view_->GetVisible(),\n"
                "                                     contents_rect, optional_size_bound);\n"
                "\n"
                "  layouts.host_size = gfx::Size(width, height);\n"
            ),
            new=(
                "  // The AI highlight view bounds are the same as the contents view.\n"
                "  CHECK(context_highlight_view_);\n"
                "  layouts.child_layouts.emplace_back(context_highlight_view_.get(),\n"
                "                                     context_highlight_view_->GetVisible(),\n"
                "                                     contents_rect, optional_size_bound);\n"
                "\n"
                "  // Maho: size any extra content-area child (the zero-tab theme gradient\n"
                "  // view MahoContentGradientView, injected by BrowserView into\n"
                "  // contents_container) to fill the full contents bounds so it paints the\n"
                "  // theme gradient over the opaque gray empty MultiContentsView. The\n"
                "  // child's own visibility is honored, so once a real tab exists (the\n"
                "  // gradient is SetVisible(false)) it is laid out hidden and never\n"
                "  // occludes live web content. contents_container is the only view that\n"
                "  // uses ContentsLayoutManager, so the gradient is the sole extra child\n"
                "  // this loop ever touches.\n"
                "  for (views::View* maho_extra_child : host_view()->children()) {\n"
                "    if (maho_extra_child == contents_view_ ||\n"
                "        maho_extra_child == lens_overlay_view_ ||\n"
                "        maho_extra_child == context_highlight_view_) {\n"
                "      continue;\n"
                "    }\n"
                "    layouts.child_layouts.emplace_back(maho_extra_child,\n"
                "                                       maho_extra_child->GetVisible(),\n"
                "                                       contents_rect, optional_size_bound);\n"
                "  }\n"
                "\n"
                "  layouts.host_size = gfx::Size(width, height);\n"
            ),
            description=(
                "Maho: size the injected zero-tab content gradient child to the "
                "full contents bounds inside ContentsLayoutManager (the correct "
                "layout scope), so the theme gradient paints over the gray empty "
                "MultiContentsView on cold start instead of keeping default 0x0 "
                "bounds. Honors the child's visibility so it is hidden when a real "
                "tab exists."
            ),
            guard="for (views::View* maho_extra_child : host_view()->children()) {",
        ),
    ],
    "chrome/browser/ui/views/frame/layout/browser_view_popup_layout_impl.cc": [
        Replacement(
            old="    return gfx::Size(400, 38 + 1);\n",
            new="    return gfx::Size(400, 46 + 1);\n",
            description="Maho: Raise an existing Little Arc minimum height to 46dip",
            idempotent=True,
            guard="return gfx::Size(400, 46 + 1);",
        ),
        Replacement(
            old="    top_bar_bounds.set_height(38);\n",
            new="    top_bar_bounds.set_height(46);\n",
            description="Maho: Raise an existing Little Arc layout height to 46dip",
            idempotent=True,
            guard="top_bar_bounds.set_height(46);",
        ),
        Replacement(
            old=(
                '#include "chrome/browser/ui/views/frame/layout/browser_view_popup_layout_impl.h"\n'
            ),
            new=(
                '#include "chrome/browser/ui/views/frame/layout/browser_view_popup_layout_impl.h"\n'
                "\n"
                '#include "chrome/browser/ui/browser.h"\n'
            ),
            description="Maho: Include Browser for is_little_arc() calls in popup layout",
            guard='#include "chrome/browser/ui/browser.h"',
        ),
        Replacement(
            old=(
                "  BrowserLayoutParams params = browser_params;\n"
                "\n"
                "  // Lay out the top container if it's in the browser (even if it's empty).\n"
            ),
            new=(
                "  BrowserLayoutParams params = browser_params;\n"
                "\n"
                "  if (views().maho_little_arc_top_bar && views().maho_little_arc_top_bar->GetVisible()) {\n"
                "    gfx::Rect top_bar_bounds = params.visual_client_area;\n"
                "    top_bar_bounds.set_height(46);\n"
                "    layout.AddChild(views().maho_little_arc_top_bar, top_bar_bounds);\n"
                "    params.SetTop(top_bar_bounds.bottom());\n"
                "  }\n"
                "\n"
                "  // Lay out the top container if it's in the browser (even if it's empty).\n"
            ),
            description="Maho: Lay out maho_little_arc_top_bar in CalculateProposedLayout",
            guard="views().maho_little_arc_top_bar &&",
        ),
        Replacement(
            old=(
                "gfx::Rect BrowserViewPopupLayoutImpl::CalculateTopContainerLayout(\n"
                "    ProposedLayout& layout,\n"
                "    BrowserLayoutParams params,\n"
                "    bool needs_exclusion) const {\n"
                "  const int original_top = params.visual_client_area.y();\n"
                "\n"
                "  // Layout starts beneath the caption buttons because title is laid out by the\n"
            ),
            new=(
                "gfx::Rect BrowserViewPopupLayoutImpl::CalculateTopContainerLayout(\n"
                "    ProposedLayout& layout,\n"
                "    BrowserLayoutParams params,\n"
                "    bool needs_exclusion) const {\n"
                "  const int original_top = params.visual_client_area.y();\n"
                "\n"
                "  if (browser()->is_little_arc()) {\n"
                "    return gfx::Rect(params.visual_client_area.x(), original_top,\n"
                "                     params.visual_client_area.width(), 0);\n"
                "  }\n"
                "\n"
                "  // Layout starts beneath the caption buttons because title is laid out by the\n"
            ),
            description="Maho: Zero top container height for Little Arc in CalculateTopContainerLayout",
            guard="if (browser()->is_little_arc()) {",
        ),
        Replacement(
            old=(
                "  constexpr gfx::Size kMinContentsSize(1, 1);\n"
                "\n"
                "  return gfx::Size(std::max({kMinContentsSize.width(), caption_size.width(),\n"
                "                             toolbar_size.width()}),\n"
                "                   kMinContentsSize.height() + caption_size.height() +\n"
                "                       toolbar_size.height() + separator_height);\n"
            ),
            new=(
                "  if (browser()->is_little_arc()) {\n"
                "    return gfx::Size(400, 46 + 1);\n"
                "  }\n"
                "\n"
                "  constexpr gfx::Size kMinContentsSize(1, 1);\n"
                "\n"
                "  return gfx::Size(std::max({kMinContentsSize.width(), caption_size.width(),\n"
                "                             toolbar_size.width()}),\n"
                "                   kMinContentsSize.height() + caption_size.height() +\n"
                "                       toolbar_size.height() + separator_height);\n"
            ),
            description="Maho: Add custom minimum size for Little Arc windows",
            guard="if (browser()->is_little_arc()) {\n    return gfx::Size(400",
        ),
    ],
}


def repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def default_chromium_src() -> Path:
    return repo_root().parent / "chromium" / "src"


def apply_replacements(target_file: Path, replacements: list[Replacement], dry_run: bool = False) -> tuple[bool, list[str]]:
    text = target_file.read_text()
    changed = False
    applied: list[str] = []

    for replacement in replacements:
        if replacement.new and replacement.new in text and (
            replacement.old not in text or replacement.old in replacement.new
        ):
            applied.append(f"already: {replacement.description}")
            continue

        if replacement.guard is not None and replacement.guard in text:
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

    if changed and not dry_run:
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
    parser.add_argument(
        "--platform",
        choices=("mac", "win", "linux"),
        default=host_platform(),
        help="Target platform for per-file filtering (default: auto-detected host)",
    )
    parser.add_argument(
        "--list-targets",
        action="store_true",
        help="Print the sorted, NUL-separated targets with 'chromium/src/' prefix.",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Simulate the overrides without writing to files.",
    )
    args = parser.parse_args()

    if args.list_targets:
        targets_to_print = sorted([
            f"chromium/src/{k}" for k in REPLACEMENTS.keys()
            if k != "chrome/browser/ui/views/frame/multi_contents_resize_area.cc"
        ])
        sys.stdout.write("".join(f"{t}\0" for t in targets_to_print))
        return 0

    chromium_src = args.chromium_src.resolve()
    if not chromium_src.exists():
        print(f"chromium/src not found: {chromium_src}", file=sys.stderr)
        return 1

    any_changed = False
    for relative_path, replacements in REPLACEMENTS.items():
        enabled_platforms = FILE_PLATFORMS.get(relative_path)
        if enabled_platforms is not None and args.platform not in enabled_platforms:
            print(
                f"skip [{args.platform}]: {relative_path} "
                f"(enabled on: {', '.join(sorted(enabled_platforms))})"
            )
            continue

        target_file = chromium_src / relative_path
        if not target_file.exists():
            print(f"target file not found: {target_file}", file=sys.stderr)
            return 1

        changed, applied = apply_replacements(target_file, replacements, dry_run=args.dry_run)
        any_changed = any_changed or changed
        print(relative_path)
        for line in applied:
            print(f"  - {line}")

    print("result: changed" if any_changed else "result: no-op")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
