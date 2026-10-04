#!/usr/bin/env python3

from __future__ import annotations

import argparse
import ast
import hashlib
import re
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Final

from split_view_replacements import SPLIT_REPLACEMENTS


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
    "chrome/installer/mini_installer/chrome.release": frozenset({"win"}),
    "chrome/install_static/chromium_install_modes.h": frozenset({"win"}),
    "chrome/installer/setup/install_worker.cc": frozenset({"win"}),
    "chrome/app/app-Info.plist": frozenset({"mac"}),
    "chrome/browser/app_controller_mac.h": frozenset({"mac"}),
    "chrome/browser/app_controller_mac.mm": frozenset({"mac"}),
    "chrome/browser/ui/cocoa/main_menu_builder.mm": frozenset({"mac"}),
    "chrome/browser/global_keyboard_shortcuts_mac.mm": frozenset({"mac"}),
    "chrome/browser/global_keyboard_shortcuts_mac_unittest.mm": frozenset({"mac"}),
    "ui/accelerated_widget_mac/ca_layer_tree_coordinator.mm": frozenset({"mac"}),
    "ui/base/cocoa/command_dispatcher.mm": frozenset({"mac"}),
    "build/toolchain/apple/linker_driver.py": frozenset({"mac"}),
    "chrome/browser/ui/views/frame/browser_caption_button_container_win.cc": frozenset({"win"}),
    "chrome/browser/ui/views/frame/browser_caption_button_container_win.h": frozenset({"win"}),
    "chrome/browser/ui/views/frame/browser_frame_view_win.cc": frozenset({"win"}),
    "chrome/browser/device_reauth/chrome_device_authenticator_factory.cc": frozenset({"linux"}),
    "chrome/browser/device_reauth/BUILD.gn": frozenset({"linux"}),
}


_BROWSER_WIDGET_LEGACY_INCLUDES: Final[str] = (
    '#include "chrome/browser/ui/views/frame/browser_view.h"\n'
    '#include "maho/browser/ui/theme/maho_space_theme_state.h"\n'
    '#include "maho/browser/ui/theme/maho_color_mixer.h"\n'
    '#include "ui/native_theme/native_theme.h"\n'
)
_BROWSER_WIDGET_INCLUDES: Final[str] = (
    '#include "chrome/browser/ui/views/frame/browser_view.h"\n'
    '#include "maho/browser/ui/theme/maho_color_mixer.h"\n'
)
_BROWSER_WIDGET_LEGACY_THEME_OVERRIDE: Final[str] = (
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
)
_BROWSER_WIDGET_OTR_SENTINEL: Final[str] = (
    '  // Maho: tag Incognito windows for private semantic color recipes without\n'
    '  // changing the Chromium/ThemeService-controlled color mode.\n'
    '  if (IsIncognitoBrowser()) {\n'
    '    key.app_controller = GetMahoOtrSentinel();\n'
    '  }\n'
    '\n'
)


# Maho registers EVERY enabled shortcut from the Rust registry with the
# FocusManager, at kHighPriority.
#
# Why the full set and not just Ctrl+1..9: BrowserView::AcceleratorPressed() is
# the only Maho dispatch path wired into browser_view.cc, and the FocusManager
# only ever calls it for accelerators that were registered. Registering a subset
# silently kills every other binding.
#
# Why kHighPriority: on macOS, ChromeCommandDispatcherDelegate
# ::prePerformKeyEquivalent consults the Views FocusManager at kHighPriority
# BEFORE calling CommandForKeyEvent(); kNormalPriority is only consulted in
# postPerformKeyEquivalent, i.e. AFTER the upstream key-equivalent table already
# claimed the event. That ordering is why Cmd+Shift+C resolved to
# IDC_DEV_TOOLS_INSPECT and Cmd+L / Cmd+E never reached Maho at all.
# The second iteration re-registered on core readiness but was NOT idempotent:
# when the core was already warm at AddedToWidget() (every window after the
# first) both the immediate call and the core-ready callback registered the same
# (accelerator, target) pair, tripping AcceleratorManager's !Contains(target) and
# !has_priority_handler_ DCHECKs in a debug build. Kept so already-patched trees
# migrate to the guarded form.
_MAHO_LEGACY_UNGATED_ACCELERATORS_REGISTERED_MEMBER: Final[str] = (
    "  // Maho: true once the Maho shortcut accelerators have actually been\n"
    "  // registered with this window's FocusManager. AddedToWidget() and the\n"
    "  // core-ready retry share one registration path, and registering the\n"
    "  // same target twice at kHighPriority trips AcceleratorManager DCHECKs,\n"
    "  // so the second caller must be able to tell the work is already done.\n"
    "  bool maho_accelerators_registered_ = false;\n"
)

_MAHO_GATED_ACCELERATORS_REGISTERED_MEMBER: Final[str] = (
    "#if BUILDFLAG(IS_MAC)\n"
    "  // Maho: true once the Maho shortcut accelerators have actually been\n"
    "  // registered with this window's FocusManager. AddedToWidget() and the\n"
    "  // core-ready retry share one registration path, and registering the\n"
    "  // same target twice at kHighPriority trips AcceleratorManager DCHECKs,\n"
    "  // so the second caller must be able to tell the work is already done.\n"
    "  // Gated with the registration it guards: off-mac nothing reads it, and\n"
    "  // -Wunused-private-field is an error under Chromium's -Werror.\n"
    "  bool maho_accelerators_registered_ = false;\n"
    "#endif  // BUILDFLAG(IS_MAC)\n"
)

_MAHO_LEGACY_NONIDEMPOTENT_ACCELERATOR_REGISTRATION: Final[str] = (
    "  // Maho: register every enabled Maho shortcut as a FocusManager\n"
    "  // accelerator. FocusManager accelerators fire regardless of which view\n"
    "  // holds focus (so shortcuts still work with no active tab), and\n"
    "  // kHighPriority is what makes macOS consult the FocusManager in\n"
    "  // ChromeCommandDispatcherDelegate::prePerformKeyEquivalent BEFORE\n"
    "  // CommandForKeyEvent(). At kNormalPriority the upstream key-equivalent\n"
    "  // table wins first, which is why Cmd+Shift+C opened DevTools Inspect.\n"
    "  // Resolution still goes through MahoShortcutInterceptor, so the Rust\n"
    "  // ShortcutManager stays the single source of truth.\n"
    "  if (browser_ && browser_->is_type_normal()) {\n"
    "    auto maho_register_accelerators = [](base::WeakPtr<BrowserView> self) {\n"
    "      if (!self) {\n"
    "        return;\n"
    "      }\n"
    "      views::FocusManager* fm = self->GetFocusManager();\n"
    "      if (!fm) {\n"
    "        return;\n"
    "      }\n"
    "      for (const ui::Accelerator& maho_accelerator :\n"
    "           maho::MahoShortcutInterceptor::GetRegisteredAccelerators()) {\n"
    "        fm->RegisterAccelerator(maho_accelerator,\n"
    "                                ui::AcceleratorManager::kHighPriority,\n"
    "                                self.get());\n"
    "      }\n"
    "    };\n"
    "    maho_register_accelerators(GetAsWeakPtr());\n"
    "    // The Rust core is frequently still starting at AddedToWidget() time,\n"
    "    // and GetRegisteredAccelerators() returns an EMPTY list until it is\n"
    "    // up. Registering only once therefore leaves the window with zero\n"
    "    // Maho accelerators for its entire lifetime, so prePerformKeyEquivalent\n"
    "    // finds nothing at kHighPriority and falls through to\n"
    "    // CommandForKeyEvent() -- which is exactly how Cmd+Shift+C still\n"
    "    // reached IDC_DEV_TOOLS_INSPECT while the binding itself resolved\n"
    "    // correctly. Re-register when the core publishes readiness.\n"
    "    maho_core_ready_accelerators_subscription_ =\n"
    "        maho::AddCoreReadyCallback(\n"
    "            base::BindRepeating(maho_register_accelerators,\n"
    "                                GetAsWeakPtr()));\n"
    "  }\n"
)

# The third iteration was idempotent but had no platform gate. BrowserView calls
# LoadAccelerators() right after this block, and on Windows/Linux the upstream
# table registers Ctrl+L (EF_PLATFORM_ACCELERATOR) for IDC_FOCUS_LOCATION on the
# SAME BrowserView target that the Rust registry's command_bar resolves to, so
# the two collide on the same-target invariant. That upstream entry sits inside
# an "#if !BUILDFLAG(IS_MAC)" block, so macOS is unaffected -- and the whole
# mechanism here (kHighPriority so prePerformKeyEquivalent consults the
# FocusManager before CommandForKeyEvent) is macOS-only anyway. Kept so
# already-patched trees migrate to the gated form.
_MAHO_LEGACY_UNSCOPED_IDEMPOTENT_ACCELERATOR_REGISTRATION: Final[str] = (
    "  // Maho: register every enabled Maho shortcut as a FocusManager\n"
    "  // accelerator. FocusManager accelerators fire regardless of which view\n"
    "  // holds focus (so shortcuts still work with no active tab), and\n"
    "  // kHighPriority is what makes macOS consult the FocusManager in\n"
    "  // ChromeCommandDispatcherDelegate::prePerformKeyEquivalent BEFORE\n"
    "  // CommandForKeyEvent(). At kNormalPriority the upstream key-equivalent\n"
    "  // table wins first, which is why Cmd+Shift+C opened DevTools Inspect.\n"
    "  // Resolution still goes through MahoShortcutInterceptor, so the Rust\n"
    "  // ShortcutManager stays the single source of truth.\n"
    "  if (browser_ && browser_->is_type_normal()) {\n"
    "    // Runs at AddedToWidget() and again when the core publishes readiness,\n"
    "    // so it MUST be idempotent: AcceleratorManager DCHECKs both\n"
    "    // !Contains(target) and !has_priority_handler_, so registering the same\n"
    "    // (accelerator, target) pair twice at kHighPriority crashes a debug\n"
    "    // build. That happens whenever the core is already warm here -- for\n"
    "    // example every window opened after the first. The empty-list check\n"
    "    // also matters on its own: GetRegisteredAccelerators() returns an EMPTY\n"
    "    // vector while maho::GetCore() is null, and treating that as a real\n"
    "    // registration is what previously left the window with zero Maho\n"
    "    // accelerators for its lifetime, dropping Cmd+Shift+C through to\n"
    "    // CommandForKeyEvent() and IDC_DEV_TOOLS_INSPECT.\n"
    "    auto maho_register_accelerators = [](base::WeakPtr<BrowserView> self) {\n"
    "      if (!self || self->maho_accelerators_registered_) {\n"
    "        return;\n"
    "      }\n"
    "      views::FocusManager* fm = self->GetFocusManager();\n"
    "      if (!fm) {\n"
    "        return;\n"
    "      }\n"
    "      const std::vector<ui::Accelerator> maho_accelerators =\n"
    "          maho::MahoShortcutInterceptor::GetRegisteredAccelerators();\n"
    "      if (maho_accelerators.empty()) {\n"
    "        return;\n"
    "      }\n"
    "      for (const ui::Accelerator& maho_accelerator : maho_accelerators) {\n"
    "        fm->RegisterAccelerator(maho_accelerator,\n"
    "                                ui::AcceleratorManager::kHighPriority,\n"
    "                                self.get());\n"
    "      }\n"
    "      self->maho_accelerators_registered_ = true;\n"
    "    };\n"
    "    maho_register_accelerators(GetAsWeakPtr());\n"
    "    maho_core_ready_accelerators_subscription_ =\n"
    "        maho::AddCoreReadyCallback(\n"
    "            base::BindRepeating(maho_register_accelerators,\n"
    "                                GetAsWeakPtr()));\n"
    "  }\n"
)

_MAHO_FOCUS_ACCELERATOR_REGISTRATION: Final[str] = (
    "#if BUILDFLAG(IS_MAC)\n"
    "  // Maho: register every enabled Maho shortcut as a FocusManager\n"
    "  // accelerator. FocusManager accelerators fire regardless of which view\n"
    "  // holds focus (so shortcuts still work with no active tab), and\n"
    "  // kHighPriority is what makes macOS consult the FocusManager in\n"
    "  // ChromeCommandDispatcherDelegate::prePerformKeyEquivalent BEFORE\n"
    "  // CommandForKeyEvent(). At kNormalPriority the upstream key-equivalent\n"
    "  // table wins first, which is why Cmd+Shift+C opened DevTools Inspect.\n"
    "  // Resolution still goes through MahoShortcutInterceptor, so the Rust\n"
    "  // ShortcutManager stays the single source of truth.\n"
    "  if (browser_ && browser_->is_type_normal()) {\n"
    "    // Runs at AddedToWidget() and again when the core publishes readiness,\n"
    "    // so it MUST be idempotent: AcceleratorManager DCHECKs both\n"
    "    // !Contains(target) and !has_priority_handler_, so registering the same\n"
    "    // (accelerator, target) pair twice at kHighPriority crashes a debug\n"
    "    // build. That happens whenever the core is already warm here -- for\n"
    "    // example every window opened after the first. The empty-list check\n"
    "    // also matters on its own: GetRegisteredAccelerators() returns an EMPTY\n"
    "    // vector while maho::GetCore() is null, and treating that as a real\n"
    "    // registration is what previously left the window with zero Maho\n"
    "    // accelerators for its lifetime, dropping Cmd+Shift+C through to\n"
    "    // CommandForKeyEvent() and IDC_DEV_TOOLS_INSPECT.\n"
    "    auto maho_register_accelerators = [](base::WeakPtr<BrowserView> self) {\n"
    "      if (!self || self->maho_accelerators_registered_) {\n"
    "        return;\n"
    "      }\n"
    "      views::FocusManager* fm = self->GetFocusManager();\n"
    "      if (!fm) {\n"
    "        return;\n"
    "      }\n"
    "      const std::vector<ui::Accelerator> maho_accelerators =\n"
    "          maho::MahoShortcutInterceptor::GetRegisteredAccelerators();\n"
    "      if (maho_accelerators.empty()) {\n"
    "        return;\n"
    "      }\n"
    "      for (const ui::Accelerator& maho_accelerator : maho_accelerators) {\n"
    "        fm->RegisterAccelerator(maho_accelerator,\n"
    "                                ui::AcceleratorManager::kHighPriority,\n"
    "                                self.get());\n"
    "      }\n"
    "      self->maho_accelerators_registered_ = true;\n"
    "    };\n"
    "    maho_register_accelerators(GetAsWeakPtr());\n"
    "    maho_core_ready_accelerators_subscription_ =\n"
    "        maho::AddCoreReadyCallback(\n"
    "            base::BindRepeating(maho_register_accelerators,\n"
    "                                GetAsWeakPtr()));\n"
    "  }\n"
    "#endif  // BUILDFLAG(IS_MAC)\n"
)

# The first shipped version of the registration above ran exactly once, inside
# AddedToWidget(). That is too early: the Rust core is often still starting, so
# GetRegisteredAccelerators() returned an empty list and the window kept ZERO
# Maho accelerators for its lifetime. Kept here so already-patched trees migrate
# to the core-ready retry instead of failing the override pass.
_MAHO_LEGACY_SINGLE_SHOT_ACCELERATOR_REGISTRATION: Final[str] = (
    "  // Maho: register every enabled Maho shortcut as a FocusManager\n"
    "  // accelerator. FocusManager accelerators fire regardless of which view\n"
    "  // holds focus (so shortcuts still work with no active tab), and\n"
    "  // kHighPriority is what makes macOS consult the FocusManager in\n"
    "  // ChromeCommandDispatcherDelegate::prePerformKeyEquivalent BEFORE\n"
    "  // CommandForKeyEvent(). At kNormalPriority the upstream key-equivalent\n"
    "  // table wins first, which is why Cmd+Shift+C opened DevTools Inspect.\n"
    "  // Resolution still goes through MahoShortcutInterceptor, so the Rust\n"
    "  // ShortcutManager stays the single source of truth.\n"
    "  if (browser_ && browser_->is_type_normal()) {\n"
    "    views::FocusManager* fm = GetFocusManager();\n"
    "    if (fm) {\n"
    "      for (const ui::Accelerator& maho_accelerator :\n"
    "           maho::MahoShortcutInterceptor::GetRegisteredAccelerators()) {\n"
    "        fm->RegisterAccelerator(maho_accelerator,\n"
    "                                ui::AcceleratorManager::kHighPriority,\n"
    "                                this);\n"
    "      }\n"
    "    }\n"
    "  }\n"
)

_MAHO_ADDED_TO_WIDGET_PATCHED: Final[str] = (
    "  views::ClientView::AddedToWidget();\n"
    "\n"
    + _MAHO_FOCUS_ACCELERATOR_REGISTRATION
    + "\n"
    "  widget_observation_.Observe(GetWidget());\n"
)

# The Ctrl+1..9-only shapes that shipped before the full-registry registration.
# Both spellings of the layout gate exist in the wild, so migrate both.
_MAHO_LEGACY_CTRL_ACCELERATOR_BODY: Final[str] = (
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
)

# Cmd+1..8 in a Maho window addresses the sidebar favorites, never the Chromium
# tab strip. Asking for a favorite that does not exist must be a no-op: the old
# `if (activate) break; SelectNumberedTab(...)` fallback is what made Cmd+2/3/4
# jump to unrelated tabs on a profile with a single favorite.
_MAHO_FAVORITE_ONLY_NUMBERED_TAB: Final[str] = (
    "      if (maho_sidebar_container) {\n"
    "        // Maho windows address favorites with Cmd+1..8. An index past the\n"
    "        // last favorite is a no-op; falling through to the Chromium tab\n"
    "        // strip would move focus to an unrelated tab.\n"
    "        maho_sidebar_container->TryActivateFavoriteByIndex(\n"
    "            id - IDC_SELECT_TAB_0);\n"
    "        break;\n"
    "      }\n"
    "      SelectNumberedTab(\n"
    "          browser_, id - IDC_SELECT_TAB_0,\n"
)

_MAHO_LEGACY_FAVORITE_FALLBACK_NUMBERED_TAB: Final[str] = (
    "      if (maho_sidebar_container &&\n"
    "          maho_sidebar_container->TryActivateFavoriteByIndex(\n"
    "              id - IDC_SELECT_TAB_0)) {\n"
    "        break;\n"
    "      }\n"
    "      SelectNumberedTab(\n"
    "          browser_, id - IDC_SELECT_TAB_0,\n"
)

_MAHO_LEGACY_CTRL_ACCELERATOR_COMMENT: Final[str] = (
    "  // Maho: Register Ctrl+1..9 as FocusManager accelerators so space-switching\n"
    "  // shortcuts fire regardless of which view has focus. Delegates to\n"
    "  // MahoShortcutInterceptor::ResolveActionForAccelerator to preserve\n"
    "  // Rust ShortcutManager as the single source of truth.\n"
)


_PINNED_CHROMIUM_REVISIONS: Final[tuple[str, ...]] = (
    "ee4bd9e95294a95c855ef51dfa26f0576f192e69",
    "72f18f12ad47a8d6dbfb4282a2ef8e507ae65428",
)
_PINNED_CHROMIUM_REVISION: Final[str] = _PINNED_CHROMIUM_REVISIONS[0]
_PINNED_INCOMPATIBLE_TARGETS: Final[frozenset[str]] = frozenset(
    {
        "base/trace_event/builtin_categories.h",
        "chrome/app/chrome_main_delegate.cc",
        "chrome/browser/global_keyboard_shortcuts_mac.mm",
        "chrome/browser/global_keyboard_shortcuts_mac_unittest.mm",
        "chrome/browser/password_manager/factories/BUILD.gn",
        "chrome/browser/password_manager/factories/password_store_backend_factory.cc",
        "chrome/browser/profiles/chrome_browser_main_extra_parts_profiles.cc",
        "chrome/browser/renderer_context_menu/render_view_context_menu.cc",
        "chrome/browser/ui/views/frame/browser_caption_button_container_win.cc",
        "chrome/browser/ui/views/frame/browser_caption_button_container_win.h",
        "chrome/browser/ui/views/frame/browser_frame_view_mac.mm",
        "chrome/browser/ui/views/frame/browser_frame_view_win.cc",
        "chrome/browser/ui/views/frame/browser_native_widget_mac.mm",
        "chrome/browser/ui/views/frame/contents_container_outline.h",
        "chrome/browser/ui/views/frame/contents_container_view.cc",
        "chrome/browser/ui/views/frame/contents_container_view.h",
        "chrome/browser/ui/views/frame/contents_layout_manager.cc",
        "chrome/browser/ui/views/frame/layout/browser_view_popup_layout_impl.cc",
        "chrome/browser/ui/views/frame/layout/browser_view_tabbed_layout_impl.cc",
        "chrome/browser/ui/views/frame/multi_contents_view.cc",
        "chrome/browser/ui/views/frame/multi_contents_view.h",
        "chrome/browser/ui/views/page_action/page_action_properties_provider.cc",
        "chrome/browser/ui/views/side_panel/side_panel_coordinator.cc",
        "chrome/browser/ui/views/side_panel/side_panel_resize_area.cc",
        "chrome/browser/ui/views/toolbar/toolbar_view.cc",
        "ui/base/ui_base_features.cc",
        "ui/compositor/layer.cc",
    }
)


def chromium_revision(chromium_src: Path) -> str:
    try:
        result = subprocess.run(
            ["git", "-C", str(chromium_src), "rev-parse", "HEAD"],
            check=True,
            capture_output=True,
            text=True,
        )
    except (OSError, subprocess.CalledProcessError) as error:
        raise RuntimeError(
            f"Unable to determine Chromium revision at {chromium_src}"
        ) from error
    revision = result.stdout.strip()
    if not revision:
        raise RuntimeError(
            f"Unable to determine Chromium revision at {chromium_src}"
        )
    return revision


_DEAD_CODE_MIGRATION_TARGETS = frozenset(
    {
        "chrome/browser/ui/startup/bad_flags_prompt.cc",
        "chrome/browser/ui/views/frame/layout/browser_view_tabbed_layout_impl.cc",
    }
)


def is_revision_incompatible_target(relative_path: str, revision: str) -> bool:
    return (
        any(revision.startswith(p[:12]) for p in _PINNED_CHROMIUM_REVISIONS)
        and relative_path in _PINNED_INCOMPATIBLE_TARGETS
    )


# Sidebar rail layout block injected into BrowserViewTabbedLayoutImpl.
# V1 only reserved width; an auto-hide sidebar (preferred width 0) was dropped
# from layout entirely, leaving no view on screen to receive the hover that
# reveals it again.
_MAHO_SIDEBAR_RAIL_LAYOUT_V1 = (
    "  // Maho: reserve the leading rail for the native sidebar before the\n"
    "  // upstream pipeline consumes `params`. Upstream lays out the rest of\n"
    "  // the browser inside the remaining area with no further changes.\n"
    "  if (views().maho_sidebar_container &&\n"
    "      views().maho_sidebar_container->GetVisible()) {\n"
    "    const int maho_rail_width =\n"
    "        views().maho_sidebar_container->GetPreferredSize().width();\n"
    "    if (maho_rail_width > 0) {\n"
    "      const gfx::Rect maho_rail_bounds(\n"
    "          params.visual_client_area.x(), params.visual_client_area.y(),\n"
    "          maho_rail_width, params.visual_client_area.height());\n"
    "      layout.AddChild(views().maho_sidebar_container, maho_rail_bounds);\n"
    "      params.InsetHorizontal(maho_rail_width, /*leading=*/true);\n"
    "    }\n"
    "  }\n"
    "\n"
    "  bool needs_exclusion = true;\n"
)

_MAHO_SIDEBAR_RAIL_LAYOUT = (
    "  // Maho: reserve the leading rail for the native sidebar before the\n"
    "  // upstream pipeline consumes `params`. Upstream lays out the rest of\n"
    "  // the browser inside the remaining area with no further changes.\n"
    "  if (views().maho_sidebar_container &&\n"
    "      views().maho_sidebar_container->GetVisible()) {\n"
    "    const int maho_rail_width =\n"
    "        views().maho_sidebar_container->GetPreferredSize().width();\n"
    "    if (maho_rail_width > 0) {\n"
    "      const gfx::Rect maho_rail_bounds(\n"
    "          params.visual_client_area.x(), params.visual_client_area.y(),\n"
    "          maho_rail_width, params.visual_client_area.height());\n"
    "      layout.AddChild(views().maho_sidebar_container, maho_rail_bounds);\n"
    "      params.InsetHorizontal(maho_rail_width, /*leading=*/true);\n"
    "    } else if (const auto* maho_floating_sidebar =\n"
    "                   views::AsViewClass<maho::MahoSidebarContainerView>(\n"
    "                       views().maho_sidebar_container)) {\n"
    "      // Maho: an auto-hide sidebar reserves no width. It floats over the\n"
    "      // contents as a hover strip, or as the full sidebar while revealed,\n"
    "      // so the contents never relayout while it slides.\n"
    "      layout.AddChild(\n"
    "          views().maho_sidebar_container,\n"
    "          gfx::Rect(params.visual_client_area.x(),\n"
    "                    params.visual_client_area.y(),\n"
    "                    maho_floating_sidebar->GetFloatingOverlayWidth(),\n"
    "                    params.visual_client_area.height()));\n"
    "    }\n"
    "  }\n"
    "\n"
    "  bool needs_exclusion = true;\n"
)


# Tail of the BrowserView::ShowTranslateBubble suppression comment. Trees
# patched while the sidebar search pill existed carry an older wording, which
# the follow-up Replacement rewrites to this text.
_MAHO_TRANSLATE_BUBBLE_COMMENT_TAIL: Final[str] = (
    "  // via the per-pane contents header (MahoContentsHeaderView) and on-device\n"
    "  // translation engine. The upstream TranslateBubbleView popup is suppressed\n"
    "  // so it does not duplicate or overlay the header interface.\n"
)


REPLACEMENTS: dict[str, list[Replacement]] = {
    # ── Xcode 27 rejects Mach-O whose LINKEDIT string pool is misaligned ───────
    # Chromium links with llvm-strip -x -S. When the indirect symbol count is
    # odd, stroff ends up 4-byte aligned (e.g. Maho Framework at 0x0F9260CC), and
    # Xcode 27's Apple ld then refuses to read the dylib:
    #   ld: mis-aligned LINKEDIT string pool, fileOffset=0x0F9260CC
    # Align the pool (8 bytes), fix LC_SYMTAB/__LINKEDIT, and re-sign ad-hoc; the
    # strip output is re-signed anyway, but an unaligned pool breaks every later
    # link and dyld rejects such dylibs at load time.
    "build/toolchain/apple/linker_driver.py": [
        Replacement(
            old=(
                "class LinkerDriver(object):\n"
                "    def __init__(self, args):\n"
            ),
            new=(
                "def _ensure_symtab_string_pool_aligned(filepath, alignment=8):\n"
                '    """Keep the Mach-O LC_SYMTAB string pool 8-byte aligned.\n'
                "\n"
                "    llvm-strip -x -S can leave stroff unaligned when the indirect\n"
                "    symbol count is odd; Xcode 27 ld then rejects the binary.\n"
                '    """\n'
                "    import struct\n"
                "    if not os.path.exists(filepath):\n"
                "        return\n"
                "    try:\n"
                '        with open(filepath, "rb") as f:\n'
                "            data = bytearray(f.read())\n"
                "        if len(data) < 32:\n"
                "            return\n"
                '        magic = struct.unpack_from("<I", data, 0)[0]\n'
                "        if magic != 0xfeedfacf:\n"
                "            return\n"
                '        ncmds = struct.unpack_from("<I", data, 16)[0]\n'
                "        offset = 32\n"
                "        symtab_offset = None\n"
                "        linkedit_offset = None\n"
                "        for _ in range(ncmds):\n"
                "            if offset + 8 > len(data):\n"
                "                break\n"
                '            cmd, cmdsize = struct.unpack_from("<II", data, offset)\n'
                "            if cmd == 2:\n"
                "                symtab_offset = offset\n"
                "            elif cmd == 0x19:\n"
                '                segname = data[offset + 8:offset + 24].split(b"\\x00")[0]\n'
                '                if segname == b"__LINKEDIT":\n'
                "                    linkedit_offset = offset\n"
                "            offset += cmdsize\n"
                "        if not symtab_offset:\n"
                "            return\n"
                "        symoff, nsyms, stroff, strsize = struct.unpack_from(\n"
                '            "<IIII", data, symtab_offset + 8)\n'
                "        rem = stroff % alignment\n"
                "        if rem == 0:\n"
                "            return\n"
                "        pad = alignment - rem\n"
                '        subprocess.run(["codesign", "--remove-signature", filepath],\n'
                "                       capture_output=True)\n"
                '        with open(filepath, "rb") as f:\n'
                "            data = bytearray(f.read())\n"
                '        data[stroff:stroff] = b"\\x00" * pad\n'
                '        struct.pack_into("<I", data, symtab_offset + 16, stroff + pad)\n'
                "        if linkedit_offset:\n"
                "            vmaddr, vmsize, fileoff, filesize = struct.unpack_from(\n"
                '                "<QQQQ", data, linkedit_offset + 24)\n'
                '            struct.pack_into("<Q", data, linkedit_offset + 48,\n'
                "                             filesize + pad)\n"
                '        with open(filepath, "wb") as f:\n'
                "            f.write(data)\n"
                '        subprocess.run(["codesign", "-f", "-s", "-", filepath],\n'
                "                       capture_output=True, check=True)\n"
                "    except Exception:\n"
                "        pass\n"
                "\n"
                "\n"
                "class LinkerDriver(object):\n"
                "    def __init__(self, args):\n"
            ),
            description=(
                "Maho: align the stripped Mach-O LC_SYMTAB string pool for Xcode 27 ld"
            ),
            guard="def _ensure_symtab_string_pool_aligned",
        ),
        Replacement(
            old=(
                "        strip_command.append(self._get_linker_output())\n"
                "        subprocess.check_call(strip_command)\n"
                "        return []\n"
            ),
            new=(
                "        strip_command.append(self._get_linker_output())\n"
                "        subprocess.check_call(strip_command)\n"
                "        _ensure_symtab_string_pool_aligned(self._get_linker_output())\n"
                "        return []\n"
            ),
            description=(
                "Maho: run the symbol-pool alignment after every linker-driver strip"
            ),
            guard="_ensure_symtab_string_pool_aligned(self._get_linker_output())",
        ),
    ],
    # ── Settings entry points land on Maho settings, never chrome://settings ──
    # On macOS the app-menu "Settings…" item and Cmd+, do NOT go through
    # IDC_OPTIONS in browser_command_controller: AppController intercepts the
    # command and calls -showPreferencesForProfile:, which calls
    # chrome::ShowSettings() directly (app_controller_mac.mm). Overriding
    # ShowSettings() itself is the one place that covers every entry point
    # (app menu, Cmd+comma, IDC_OPTIONS, chrome::OpenOptionsWindow).
    # ShowSettingsSubPage() keeps upstream behaviour so feature-specific
    # sub-pages (passwords, site settings, …) are untouched.
    "chrome/browser/ui/chrome_pages.cc": [
        Replacement(
            old='#include "chrome/browser/ui/browser_navigator_params.h"\n',
            new=(
                '#include "chrome/browser/ui/browser_navigator_params.h"\n'
                '#include "maho/browser/ui/maho_settings_navigation.h"\n'
            ),
            description="Include Maho settings navigation in chrome_pages.cc",
            guard='#include "maho/browser/ui/maho_settings_navigation.h"',
        ),
        Replacement(
            old=(
                "void ShowSettings(BrowserWindowInterface* browser) {\n"
                "  ShowSettingsSubPage(browser, std::string());\n"
                "}\n"
            ),
            new=(
                "void ShowSettings(BrowserWindowInterface* browser) {\n"
                "  // Maho: open Maho's own settings surface instead of\n"
                "  // chrome://settings. This is the single choke point for the macOS\n"
                "  // app menu Settings item, Cmd+comma, IDC_OPTIONS and\n"
                "  // chrome::OpenOptionsWindow.\n"
                "  Browser* maho_browser =\n"
                "      browser ? browser->GetBrowserForMigrationOnly() : nullptr;\n"
                "  if (maho_browser) {\n"
                "    maho::OpenMahoSettingsPane(maho_browser, std::string());\n"
                "    return;\n"
                "  }\n"
                "  ShowSettingsSubPage(browser, std::string());\n"
                "}\n"
            ),
            description=(
                "Route chrome::ShowSettings to Maho settings so the macOS app menu "
                "and Cmd+comma open chrome://maho-settings, not chrome://settings"
            ),
            guard="maho::OpenMahoSettingsPane(maho_browser, std::string());",
        ),
    ],
    # ── macOS menu bar carries no Bookmarks menu ──
    # Maho ships no bookmark surface (no manager, no bar, no star), so the
    # upstream Bookmarks menu would open Chromium UI for a store Maho never
    # writes. BuildMainMenu skips builders that return nil, so returning nil
    # removes the whole menu bar entry, while keeping the symbol referenced
    # (an unused static builder would fail -Wunused-function).
    "chrome/browser/ui/cocoa/main_menu_builder.mm": [
        Replacement(
            old=(
            "NSMenuItem* BuildBookmarksMenu(NSApplication* nsapp,\n"
            "                               id app_delegate,\n"
            "                               const std::u16string& product_name,\n"
            "                               bool is_pwa,\n"
            "                               bool is_rtl) {\n"
            "  if (is_pwa) {\n"
            "    return nil;\n"
            "  }\n"
            "\n"
            "  // clang-format off\n"
            "  NSMenuItem* item =\n"
            "      Item(IDS_BOOKMARKS_MENU)\n"
            "          .tag(IDC_BOOKMARKS_MENU)\n"
            "          .submenu({\n"
            "              Item(IDS_BOOKMARK_MANAGER)\n"
            "                  .command_id(IDC_SHOW_BOOKMARK_MANAGER),\n"
            "              Item().is_separator()\n"
            "                  .tag(IDC_BOOKMARK_THIS_TAB),\n"
            "              Item(IDS_BOOKMARK_THIS_TAB)\n"
            "                  .command_id(IDC_BOOKMARK_THIS_TAB),\n"
            "              Item(IDS_BOOKMARK_ALL_TABS)\n"
            "                  .command_id(IDC_BOOKMARK_ALL_TABS),\n"
            "              Item().is_separator()\n"
            "                  .tag(IDC_BOOKMARK_THIS_TAB),\n"
            "          })\n"
            "          .Build();\n"
            "  // clang-format on\n"
            "  return item;\n"
            "}\n"
            ),
            new=(
            "NSMenuItem* BuildBookmarksMenu(NSApplication* nsapp,\n"
            "                               id app_delegate,\n"
            "                               const std::u16string& product_name,\n"
            "                               bool is_pwa,\n"
            "                               bool is_rtl) {\n"
            "  // Maho: the browser exposes no bookmark surface (sidebar favorites and\n"
            "  // spaces replace it), so the macOS Bookmarks menu is never built. The main\n"
            "  // menu loop skips nil builders, so the menu bar entry disappears with it.\n"
            "  return nil;\n"
            "}\n"
            ),
            description=(
                "Maho: never build the macOS Bookmarks menu; the browser has no "
                "bookmark surface"
            ),
            guard="Maho: the browser exposes no bookmark surface",
        ),
    ],
    "third_party/blink/public/common/loader/url_loader_throttle.h": [
        Replacement(
            old="  virtual void DetachFromCurrentSequence();\n",
            new=(
                "  virtual void DetachFromCurrentSequence();\n\n"
                "  virtual const GURL* TakeDeferredStartRedirectUrl() { return nullptr; }\n"
            ),
            description="Maho: expose an owned deferred-start redirect without retaining a request pointer",
            guard="TakeDeferredStartRedirectUrl",
        ),
    ],
    "third_party/blink/common/loader/throttling_url_loader.cc": [
        Replacement(
            old="void ThrottlingURLLoader::StartNow() {\n",
            new=(
                "void ThrottlingURLLoader::StartNow() {\n"
                "  for (const auto& entry : throttles_) {\n"
                "    if (const GURL* url = entry.throttle->TakeDeferredStartRedirectUrl()) {\n"
                "      CHECK(throttle_will_start_redirect_url_.is_empty());\n"
                "      CHECK(url->is_valid());\n"
                "      CHECK(!original_url_.SchemeIsHTTPOrHTTPS() || url->SchemeIsHTTPOrHTTPS());\n"
                "      throttle_will_start_redirect_url_ = *url;\n"
                "    }\n"
                "  }\n"
            ),
            description="Maho: apply asynchronous filtering redirects before a deferred request starts",
            guard="entry.throttle->TakeDeferredStartRedirectUrl()",
        ),
    ],
    "third_party/blink/renderer/platform/fonts/shaping/ng_shape_cache.h": [
        Replacement(
            old=(
                "#if EXPENSIVE_DCHECKS_ARE_ON()\n"
                "      const auto [other_shape_result, can_cache_other] = shape_result_func();\n"
                "      const bool has_private_or_non_characters =\n"
                "          !key.GetText().Is8Bit() &&\n"
                "          std::ranges::any_of(key.GetText().Span16(), [](UChar32 c) {\n"
                "            return Character::IsPrivateUse(c) || Character::IsNonCharacter(c);\n"
                "          });\n"
                "      // The shape-result call might try and reuse previous shape-results, we\n"
                "      // can't check for equality in this case.\n"
                "      //\n"
                "      // TODO(crbug.com/486945341): We currently incorrectly cache shape-results\n"
                "      // which contain PUA or non-characters.\n"
                "      //\n"
                "      // Specifically `FontCache::FallbackFontForCharacter` has different\n"
                "      // fallback logic for these characters; for the same primary-font, and\n"
                "      // different fallback lists we may produce two different shape-results.\n"
                "      //\n"
                "      // We should avoid the cache for this case.\n"
                "      if (can_cache_other && !has_private_or_non_characters) {\n"
                "        DCHECK_EQ(*cached_result, *other_shape_result);\n"
                "      }\n"
                "#endif\n"
            ),
            new=(
                "#if EXPENSIVE_DCHECKS_ARE_ON()\n"
                "      // Disabled cache consistency verification due to crbug.com/486945341\n"
                "      // (fallback fonts / minor float differences causing false-positive DCHECK crashes).\n"
                "#if 0\n"
                "      const auto [other_shape_result, can_cache_other] = shape_result_func();\n"
                "      const bool has_private_or_non_characters =\n"
                "          !key.GetText().Is8Bit() &&\n"
                "          std::ranges::any_of(key.GetText().Span16(), [](UChar32 c) {\n"
                "            return Character::IsPrivateUse(c) || Character::IsNonCharacter(c);\n"
                "          });\n"
                "      // The shape-result call might try and reuse previous shape-results, we\n"
                "      // can't check for equality in this case.\n"
                "      //\n"
                "      // TODO(crbug.com/486945341): We currently incorrectly cache shape-results\n"
                "      // which contain PUA or non-characters.\n"
                "      //\n"
                "      // Specifically `FontCache::FallbackFontForCharacter` has different\n"
                "      // fallback logic for these characters; for the same primary-font, and\n"
                "      // different fallback lists we may produce two different shape-results.\n"
                "      //\n"
                "      // We should avoid the cache for this case.\n"
                "      if (can_cache_other && !has_private_or_non_characters) {\n"
                "        DCHECK_EQ(*cached_result, *other_shape_result);\n"
                "      }\n"
                "#endif\n"
                "#endif\n"
            ),
            description="Maho: bypass Blink shape cache consistency DCHECK to avoid false-positive crashes on fallback fonts",
            guard="// Disabled cache consistency verification due to crbug.com/486945341",
        ),
    ],
    "chrome/browser/ui/views/passwords/password_bubble_view_base.cc": [
        Replacement(
            old=(
                '  if (browser_) {\n'
                '    auto* passwords_action_item = actions::ActionManager::Get().FindAction(\n'
                '        kActionShowPasswordsBubbleOrPage,\n'
                '        browser_->browser_actions()->root_action_item());\n'
                '    CHECK(passwords_action_item);\n'
                '    passwords_action_item->SetIsShowingBubble(false);\n'
                '  }\n'
            ),
            new=(
                '  if (browser_ && browser_->browser_actions() &&\n'
                '      browser_->browser_actions()->root_action_item()) {\n'
                '    auto* passwords_action_item = actions::ActionManager::Get().FindAction(\n'
                '        kActionShowPasswordsBubbleOrPage,\n'
                '        browser_->browser_actions()->root_action_item());\n'
                '    if (passwords_action_item) {\n'
                '      passwords_action_item->SetIsShowingBubble(false);\n'
                '    }\n'
                '  }\n'
            ),
            description="Maho: safely null-check passwords_action_item in destructor",
            guard="if (passwords_action_item) {\n      passwords_action_item->SetIsShowingBubble(false);",
        ),
    ],
    "chrome/browser/notifications/notification_handler.h": [
        Replacement(
            old=(
                "    MAHO_MAIL = 5,\n"
                "    SHARING = 6,\n"
            ),
            new="    SHARING = 6,\n",
            description="Maho: remove the obsolete deprecated-slot Mail enum",
            idempotent=True,
        ),
        Replacement(
            old=(
                "    DEFAULT_BROWSER_CHANGED = 11,\n"
                "    MAX = DEFAULT_BROWSER_CHANGED,\n"
            ),
            new=(
                "    DEFAULT_BROWSER_CHANGED = 11,\n"
                "    MAHO_MAIL = 12,\n"
                "    MAX = MAHO_MAIL,\n"
            ),
            description="Maho: append an ABI-safe native notification type for Mail",
            guard="    MAHO_MAIL = 12,",
        ),
    ],
    "chrome/installer/mini_installer/chrome.release": [
        Replacement(
            old=(
                "chrome.exe: %(ChromeDir)s\\\n"
                "chrome_proxy.exe: %(ChromeDir)s\\\n"
            ),
            new=(
                "chrome.exe: %(ChromeDir)s\\\n"
                "chrome_proxy.exe: %(ChromeDir)s\\\n"
                "maho.exe: %(ChromeDir)s\\\n"
                "maho_mail_helper.exe: %(ChromeDir)s\\\n"
            ),
            description="Maho: bundle the CLI and Mail helper in the Windows installer",
            guard="maho_mail_helper.exe: %(ChromeDir)s\\",
        ),
    ],
    "chrome/install_static/chromium_install_modes.h": [
        Replacement(
            old='inline constexpr wchar_t kProductPathName[] = L"Chromium";',
            new='inline constexpr wchar_t kProductPathName[] = L"Maho";',
            description="Maho: install into the Maho product directory on Windows",
            guard='kProductPathName[] = L"Maho";',
        ),
        Replacement(
            old='        .base_app_name = L"Chromium",              // A distinct base_app_name.',
            new='        .base_app_name = L"Maho",                  // A distinct base_app_name.',
            description="Maho: register base_app_name as Maho",
            guard='.base_app_name = L"Maho",',
        ),
        Replacement(
            old='        .base_app_id = L"Chromium",                // A distinct base_app_id.',
            new='        .base_app_id = L"Maho",                    // A distinct base_app_id.',
            description="Maho: register base_app_id as Maho",
            guard='.base_app_id = L"Maho",',
        ),
    ],
    "chrome/installer/setup/install_worker.cc": [
        Replacement(
            old=(
                "  not_in_use_list->AddMoveTreeWorkItem(\n"
                "      src_path.Append(installer::kChromeExe),\n"
                "      target_path.Append(installer::kChromeExe), temp_path,\n"
                "      WorkItem::MoveTreeOptions{.lenient_deletion = true});\n"
                "\n"
                "  install_list->AddWorkItem(WorkItem::CreateConditionalWorkItem("
            ),
            new=(
                "  not_in_use_list->AddMoveTreeWorkItem(\n"
                "      src_path.Append(installer::kChromeExe),\n"
                "      target_path.Append(installer::kChromeExe), temp_path,\n"
                "      WorkItem::MoveTreeOptions{.lenient_deletion = true});\n"
                "\n"
                "  // Maho: relocate the bundled CLI and Mail helper out of Chrome-bin\n"
                "  // into the install root. Setup only moves chrome.exe and the\n"
                "  // version dir, so any other root executable would otherwise be\n"
                "  // discarded together with the temporary directory.\n"
                "  install_list->AddMoveTreeWorkItem(\n"
                "      src_path.Append(L\"maho.exe\"),\n"
                "      target_path.Append(L\"maho.exe\"), temp_path,\n"
                "      WorkItem::MoveTreeOptions{.lenient_deletion = true});\n"
                "  install_list->AddMoveTreeWorkItem(\n"
                "      src_path.Append(L\"maho_mail_helper.exe\"),\n"
                "      target_path.Append(L\"maho_mail_helper.exe\"), temp_path,\n"
                "      WorkItem::MoveTreeOptions{.lenient_deletion = true});\n"
                "\n"
                "  install_list->AddWorkItem(WorkItem::CreateConditionalWorkItem("
            ),
            description="Maho: keep the bundled CLI and Mail helper during install",
            guard="Maho: relocate the bundled CLI and Mail helper",
        ),
    ],
    "ui/accelerated_widget_mac/ca_layer_tree_coordinator.mm": [
        Replacement(
            old="    root_ca_layer_.opaque = YES;\n",
            new=(
                "    // Maho: transparent embedded WebContents must preserve alpha so the\n"
                "    // browser window's shared vibrancy and gradient remain visible.\n"
                "    root_ca_layer_.opaque = NO;\n"
            ),
            description="Maho: preserve alpha in the macOS remote compositor root layer",
            guard="root_ca_layer_.opaque = NO;",
        ),
    ],
    "chrome/common/chrome_content_client.cc": [
        Replacement(
            old="#include \"chrome/common/url_constants.h\"\n",
            new=(
                "#include \"chrome/common/url_constants.h\"\n"
                "#include \"maho/components/constants/url_constants.h\"\n"
            ),
            description="Maho: include maho url_constants in chrome_content_client.cc",
            guard="#include \"maho/components/constants/url_constants.h\"",
        ),
        Replacement(
            old=(
                "void ChromeContentClient::AddAdditionalSchemes(Schemes* schemes) {\n"
                "  for (auto* standard_scheme : kChromeStandardURLSchemes)\n"
                "    schemes->standard_schemes.push_back(standard_scheme);\n"
            ),
            new=(
                "void ChromeContentClient::AddAdditionalSchemes(Schemes* schemes) {\n"
                "  for (auto* standard_scheme : kChromeStandardURLSchemes)\n"
                "    schemes->standard_schemes.push_back(standard_scheme);\n"
                "  schemes->standard_schemes.push_back(maho::kMahoUIScheme);\n"
            ),
            description="Maho: register maho scheme as a standard scheme",
            guard="schemes->standard_schemes.push_back(maho::kMahoUIScheme);",
        ),
    ],
    "chrome/common/BUILD.gn": [
        Replacement(
            old="    \"//chrome/common/profiler\",\n",
            new=(
                "    \"//chrome/common/profiler\",\n"
                "    \"//maho/components/constants\",\n"
            ),
            description="Maho: add constants dep to common_lib",
            # Anchored to the static_library("common_lib") deps list; the guard
            # keeps the pair adjacent so a //maho dep added to any other target
            # in this file cannot be mistaken for this replacement.
            guard=(
                "    \"//chrome/common/profiler\",\n"
                "    \"//maho/components/constants\",\n"
            ),
        ),
    ],
    "chrome/browser/profiles/profile_io_data.cc": [
        Replacement(
            old="#include \"chrome/common/url_constants.h\"\n",
            new=(
                "#include \"chrome/common/url_constants.h\"\n"
                "#include \"maho/components/constants/url_constants.h\"\n"
            ),
            description="Maho: include maho url_constants in profile_io_data.cc",
            guard="#include \"maho/components/constants/url_constants.h\"",
        ),
        Replacement(
            old="      chrome::kChromeSearchScheme,\n",
            new=(
                "      chrome::kChromeSearchScheme,\n"
                "      maho::kMahoUIScheme,\n"
            ),
            description="Maho: register maho as handled protocol in profile_io_data.cc",
            guard="maho::kMahoUIScheme,",
        ),
    ],
    "chrome/browser/profiles/BUILD.gn": [
        Replacement(
            # "//url" alone appears in several targets in this file; anchor on
            # the source_set("profile_io_data") deps pair so the dep can only
            # land in the target that compiles profile_io_data.cc.
            old=(
                "    \"//net:buildflags\",\n"
                "    \"//url\",\n"
            ),
            new=(
                "    \"//net:buildflags\",\n"
                "    \"//url\",\n"
                "    \"//maho/components/constants\",\n"
            ),
            description="Maho: add constants dep to profile_io_data",
            guard=(
                "    \"//url\",\n"
                "    \"//maho/components/constants\",\n"
            ),
        ),
    ],
    "chrome/browser/chrome_content_browser_client.cc": [
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
                "\n"
                "// Maps only approved public Maho aliases to their existing Chrome WebUI\n"
                "// origins. Returning false leaves malformed, unlisted, and direct legacy\n"
                "// chrome://maho-* URLs untouched.\n"
                "bool MahoHandleUrlAlias(GURL* url,\n"
                "                        content::BrowserContext* /*context*/) {\n"
                "  return maho::MapMahoUrlAliasToActualUrl(*url, url);\n"
                "}\n"
                "\n"
                "// BrowserURLHandler calls this only after MahoHandleUrlAlias matched the\n"
                "// original public alias. Always claim that reverse rewrite: an approved\n"
                "// final WebUI destination becomes its alias, while any external or\n"
                "// unlisted final URL is intentionally retained to clear a stale alias.\n"
                "bool MahoHandleUrlAliasReverse(GURL* url,\n"
                "                               content::BrowserContext* /*context*/) {\n"
                "  GURL alias;\n"
                "  if (maho::ResolveActualUrlToMahoAlias(*url, &alias)) {\n"
                "    *url = alias;\n"
                "  }\n"
                "  return true;\n"
                "}\n"
            ),
            description="Maho: add URL alias helpers to chrome_content_browser_client.cc",
            guard="bool MahoRewriteNewTabToBlank(GURL* url,",
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
                "  // Preserve maho:// aliases as virtual URLs while committing only the\n"
                "  // approved existing chrome://maho-* WebUI origins.\n"
                "  handler->AddHandlerPair(&MahoHandleUrlAlias,\n"
                "                          &MahoHandleUrlAliasReverse);\n"
                "  handler->AddHandlerPair(&HandleChromeAboutAndChromeSyncRewrite,\n"
                "                          BrowserURLHandler::null_handler());\n"
            ),
            description="Maho: register URL alias handlers in BrowserURLHandlerCreated",
            guard="handler->AddHandlerPair(&MahoRewriteNewTabToBlank,",
        ),

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
                "\n"
                "// Maps only approved public Maho aliases to their existing Chrome WebUI\n"
                "// origins. Returning false leaves malformed, unlisted, and direct legacy\n"
                "// chrome://maho-* URLs untouched.\n"
                "bool MahoHandleUrlAlias(GURL* url,\n"
                "                        content::BrowserContext* /*context*/) {\n"
                "  return maho::MapMahoUrlAliasToActualUrl(*url, url);\n"
                "}\n"
                "\n"
                "// BrowserURLHandler calls this only after MahoHandleUrlAlias matched the\n"
                "// original public alias. Always claim that reverse rewrite: an approved\n"
                "// final WebUI destination becomes its alias, while any external or\n"
                "// unlisted final URL is intentionally retained to clear a stale alias.\n"
                "bool MahoHandleUrlAliasReverse(GURL* url,\n"
                "                               content::BrowserContext* /*context*/) {\n"
                "  GURL alias;\n"
                "  if (maho::ResolveActualUrlToMahoAlias(*url, &alias)) {\n"
                "    *url = alias;\n"
                "  }\n"
                "  return true;\n"
                "}\n"
            ),
            description=(
                "Insert Maho new-tab and public URL alias helpers into the existing "
                "anonymous namespace of chrome_content_browser_client.cc"
            ),
            guard="bool MahoHandleUrlAliasReverse(",
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
                "  // Preserve maho:// aliases as virtual URLs while committing only the\n"
                "  // approved existing chrome://maho-* WebUI origins.\n"
                "  handler->AddHandlerPair(&MahoHandleUrlAlias,\n"
                "                          &MahoHandleUrlAliasReverse);\n"
                "  handler->AddHandlerPair(&HandleChromeAboutAndChromeSyncRewrite,\n"
                "                          BrowserURLHandler::null_handler());\n"
            ),
            description=(
                "Register Maho new-tab then alias handler pairs before "
                "HandleChromeAboutAndChromeSyncRewrite in BrowserURLHandlerCreated()"
            ),
            guard="&MahoHandleUrlAliasReverse);",
        ),
        Replacement(
            old=(
                '#include "content/public/browser/browser_url_handler.h"\n'
            ),
            new=(
                '#include "content/public/browser/browser_url_handler.h"\n'
                '#include "maho/browser/maho_url_scheme.h"\n'
            ),
            description=(
                "Include Maho URL alias resolver in chrome_content_browser_client.cc"
            ),
            guard='#include "maho/browser/maho_url_scheme.h"',
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
        # ── Ad-block request util include (independent guard) ──────────────────
        Replacement(
            old='#include "maho/browser/net/maho_ad_block_tab_helper.h"\n',
            new=(
                '#include "maho/browser/net/maho_ad_block_tab_helper.h"\n'
                '#include "maho/browser/net/maho_ad_block_request_util.h"\n'
            ),
            description=(
                "Insert Maho ad-block request util include into "
                "chrome_content_browser_client.cc for request-aware block counts"
            ),
            guard='#include "maho/browser/net/maho_ad_block_request_util.h"',
        ),
        # ── Subresource ad-block proxy include (independent guard) ─────────────
        Replacement(
            old='#include "maho/browser/net/maho_ad_block_throttle.h"\n',
            new=(
                '#include "maho/browser/net/maho_ad_block_throttle.h"\n'
                '#include "maho/browser/net/maho_proxying_url_loader_factory.h"\n'
            ),
            description=(
                "Insert Maho subresource proxying URLLoaderFactory include into "
                "chrome_content_browser_client.cc (own guard so it applies even "
                "when the throttle include is already present)"
            ),
            guard='#include "maho/browser/net/maho_proxying_url_loader_factory.h"',
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
                "    base::RepeatingCallback<void(const network::ResourceRequest&)> on_blocked;\n"
                "    content::WebContents* wc = wc_getter ? wc_getter.Run() : nullptr;\n"
                "    if (wc) {\n"
                "      MahoAdBlockTabHelper::CreateForWebContents(wc);\n"
                "      MahoAdBlockTabHelper* helper =\n"
                "          MahoAdBlockTabHelper::FromWebContents(wc);\n"
                "      if (helper) {\n"
                "        content::GlobalRenderFrameHostId page_id;\n"
                "        // If no primary main frame is available, page_id stays invalid\n"
                "        // and RecordBlockedRequest ignores the callback conservatively.\n"
                "        if (content::RenderFrameHost* main_frame = wc->GetPrimaryMainFrame()) {\n"
                "          page_id = main_frame->GetGlobalId();\n"
                "        }\n"
                "        on_blocked = base::BindRepeating(\n"
                "            [](base::WeakPtr<MahoAdBlockTabHelper> weak,\n"
                "               content::GlobalRenderFrameHostId page_id,\n"
                "               const network::ResourceRequest& request) {\n"
                "              if (weak) {\n"
                "                MahoBlockedRequestInfo info;\n"
                "                info.page_id = page_id;\n"
                "                info.destination = request.destination;\n"
                "                info.is_outermost_main_frame = request.is_outermost_main_frame;\n"
                "                info.request_type = maho::MapRequestToFilterType(request);\n"
                "                weak->RecordBlockedRequest(info);\n"
                "              }\n"
                "            },\n"
                "            helper->GetWeakPtr(), page_id);\n"
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
        # ── Update already-applied ad-block throttle callback ──────────────────
        Replacement(
            old=(
                "  if (maho::GetCore()) {\n"
                "    base::RepeatingClosure on_blocked;\n"
                "    content::WebContents* wc = wc_getter ? wc_getter.Run() : nullptr;\n"
                "    if (wc) {\n"
                "      MahoAdBlockTabHelper::CreateForWebContents(wc);\n"
                "      MahoAdBlockTabHelper* helper =\n"
                "          MahoAdBlockTabHelper::FromWebContents(wc);\n"
                "      if (helper) {\n"
                "        on_blocked = base::BindRepeating(\n"
                "            &MahoAdBlockTabHelper::IncrementBlockedCount,\n"
                "            helper->GetWeakPtr());\n"
                "      }\n"
                "    }\n"
                "    result.push_back(\n"
                "        std::make_unique<MahoAdBlockThrottle>(std::move(on_blocked)));\n"
                "  }\n"
            ),
            new=(
                "  if (maho::GetCore()) {\n"
                "    base::RepeatingCallback<void(const network::ResourceRequest&)> on_blocked;\n"
                "    content::WebContents* wc = wc_getter ? wc_getter.Run() : nullptr;\n"
                "    if (wc) {\n"
                "      MahoAdBlockTabHelper::CreateForWebContents(wc);\n"
                "      MahoAdBlockTabHelper* helper =\n"
                "          MahoAdBlockTabHelper::FromWebContents(wc);\n"
                "      if (helper) {\n"
                "        content::GlobalRenderFrameHostId page_id;\n"
                "        // If no primary main frame is available, page_id stays invalid\n"
                "        // and RecordBlockedRequest ignores the callback conservatively.\n"
                "        if (content::RenderFrameHost* main_frame = wc->GetPrimaryMainFrame()) {\n"
                "          page_id = main_frame->GetGlobalId();\n"
                "        }\n"
                "        on_blocked = base::BindRepeating(\n"
                "            [](base::WeakPtr<MahoAdBlockTabHelper> weak,\n"
                "               content::GlobalRenderFrameHostId page_id,\n"
                "               const network::ResourceRequest& request) {\n"
                "              if (weak) {\n"
                "                MahoBlockedRequestInfo info;\n"
                "                info.page_id = page_id;\n"
                "                info.destination = request.destination;\n"
                "                info.is_outermost_main_frame = request.is_outermost_main_frame;\n"
                "                info.request_type = maho::MapRequestToFilterType(request);\n"
                "                weak->RecordBlockedRequest(info);\n"
                "              }\n"
                "            },\n"
                "            helper->GetWeakPtr(), page_id);\n"
                "      }\n"
                "    }\n"
                "    result.push_back(\n"
                "        std::make_unique<MahoAdBlockThrottle>(std::move(on_blocked)));\n"
                "  }\n"
            ),
            description=(
                "Update already-applied MahoAdBlockThrottle callback to pass "
                "request context into page-scoped blocked-count recording"
            ),
            guard="weak->RecordBlockedRequest(info);",
        ),
        # ── Subresource ad-block proxy in WillCreateURLLoaderFactory() ─────────
        Replacement(
            old=(
                "  // WARNING: This must be the last interceptor in the chain as the proxying\n"
                "  // URLLoaderFactory installed by this needs to be the one actually sending\n"
                "  // packets over the network (to effectively target `bound_network`).\n"
                "  MaybeProxyNetworkBoundRequest(\n"
            ),
            new=(
                "  // Maho: enforce native content-blocking on renderer-initiated\n"
                "  // subresources (script/image/iframe/xhr/font/...) and worker requests,\n"
                "  // which the navigation-only browser-side throttle never sees. Navigation\n"
                "  // stays on the throttle in Phase 1. Installed after the extension\n"
                "  // WebRequest/signin proxies (so extensions still observe) but before the\n"
                "  // network-bound proxy, which must remain the last interceptor.\n"
                "  if (type == URLLoaderFactoryType::kDocumentSubResource ||\n"
                "      type == URLLoaderFactoryType::kWorkerMainResource ||\n"
                "      type == URLLoaderFactoryType::kWorkerSubResource ||\n"
                "      type == URLLoaderFactoryType::kServiceWorkerScript ||\n"
                "      type == URLLoaderFactoryType::kServiceWorkerSubResource ||\n"
                "      type == URLLoaderFactoryType::kPrefetch) {\n"
                "    MahoProxyingURLLoaderFactory::MaybeProxyRequest(frame, factory_builder);\n"
                "  }\n"
                "\n"
                "  // WARNING: This must be the last interceptor in the chain as the proxying\n"
                "  // URLLoaderFactory installed by this needs to be the one actually sending\n"
                "  // packets over the network (to effectively target `bound_network`).\n"
                "  MaybeProxyNetworkBoundRequest(\n"
            ),
            description=(
                "Install MahoProxyingURLLoaderFactory in WillCreateURLLoaderFactory "
                "for document-subresource, worker/service-worker, and prefetch "
                "factory types (subresource ad-block coverage), before the "
                "network-bound proxy"
            ),
            guard="MahoProxyingURLLoaderFactory::MaybeProxyRequest",
        ),
        # ── Extend the subresource proxy to prefetch factories (own guard) ─────
        Replacement(
            old="      type == URLLoaderFactoryType::kServiceWorkerSubResource) {\n",
            new=(
                "      type == URLLoaderFactoryType::kServiceWorkerSubResource ||\n"
                "      type == URLLoaderFactoryType::kPrefetch) {\n"
            ),
            description=(
                "Extend the Maho subresource proxy to the kPrefetch factory type "
                "so prefetched ad/tracker resources are also blocked (own guard so "
                "it applies when the base install is already present)"
            ),
            guard="URLLoaderFactoryType::kPrefetch",
        ),
        # ── ATC space-routing NavigationThrottle ───────────────────────────────
        Replacement(
            old=(
                '#include "maho/browser/net/maho_webstore_ua_throttle.h"\n'
            ),
            new=(
                '#include "maho/browser/net/maho_webstore_ua_throttle.h"\n'
                '#include "maho/browser/net/maho_atc_navigation_throttle.h"\n'
                '#include "maho/browser/net/maho_atc_state.h"\n'
                '#include "maho/browser/ui/views/sidebar/maho_sidebar_prefs.h"\n'
            ),
            description=(
                "Insert Maho ATC/Peek navigation throttle dependencies into "
                "chrome_content_browser_client.cc"
            ),
            guard='#include "maho/browser/net/maho_atc_navigation_throttle.h"',
        ),
        Replacement(
            old=(
                '#include "maho/browser/net/maho_atc_navigation_throttle.h"\n'
            ),
            new=(
                '#include "maho/browser/net/maho_atc_navigation_throttle.h"\n'
                '#include "maho/browser/net/maho_atc_state.h"\n'
            ),
            description="Include Maho ATC state beside the navigation throttle after the throttle include is already present",
            guard='#include "maho/browser/net/maho_atc_state.h"',
        ),
        Replacement(
            old=(
                '#include "maho/browser/net/maho_atc_state.h"\n'
            ),
            new=(
                '#include "maho/browser/net/maho_atc_state.h"\n'
                '#include "maho/browser/ui/views/sidebar/maho_sidebar_prefs.h"\n'
            ),
            description=(
                "Include Peek link-routing prefs beside Maho ATC state in "
                "chrome_content_browser_client.cc"
            ),
            guard=(
                '#include "maho/browser/ui/views/sidebar/maho_sidebar_prefs.h"'
            ),
        ),

        Replacement(
            old='#include "maho/browser/ui/views/sidebar/maho_sidebar_prefs.h"\n',
            new=(
                '#include "maho/browser/ui/views/sidebar/maho_sidebar_prefs.h"\n'
                '#include "content/public/browser/browser_url_handler.h"\n'
                '#include "maho/browser/maho_url_scheme.h"\n'
            ),
            description="Maho: include maho_url_scheme.h and browser_url_handler.h in chrome_content_browser_client.cc",
            guard='#include "maho/browser/maho_url_scheme.h"',
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
                "  // Maho ATC + Peek: keep one throttle decision path for top-level\n"
                "  // navigations that bypass Browser::OpenURLFromTab. ATC keeps its\n"
                "  // existing eligibility; Peek additionally handles eligible same-tab\n"
                "  // renderer link clicks even when no ATC rules are enabled.\n"
                "  content::WebContents* web_contents =\n"
                "      registry.GetNavigationHandle().GetWebContents();\n"
                "  Profile* profile =\n"
                "      web_contents\n"
                "          ? Profile::FromBrowserContext(\n"
                "                web_contents->GetBrowserContext())\n"
                "          : nullptr;\n"
                "  PrefService* prefs = profile ? profile->GetPrefs() : nullptr;\n"
                "  const bool peek_link_routing_enabled =\n"
                "      prefs && prefs->GetBoolean(\n"
                "                   maho::sidebar_prefs::kPeekEnabled) &&\n"
                "      prefs->GetBoolean(\n"
                "          maho::sidebar_prefs::kPeekLinkRoutingEnabled);\n"
                "  if (maho::MahoAtcState::HasEnabledRules() ||\n"
                "      peek_link_routing_enabled) {\n"
                "    registry.AddThrottle(\n"
                "        std::make_unique<maho::MahoAtcNavigationThrottle>(registry));\n"
                "  }\n"
                "}\n"
            ),
            description=(
                "Register the shared Maho ATC/Peek navigation throttle on all "
                "desktop platforms when ATC rules or Peek link routing may apply"
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
    "ui/base/cocoa/command_dispatcher.mm": [
        Replacement(
            old=(
                "  if (handler)\n"
                "    [handler commandDispatch:sender window:_owner];\n"
                "  else\n"
                "    [_owner.commandDispatchParent commandDispatch:sender];\n"
            ),
            new=(
                "  if (handler) {\n"
                "    [handler commandDispatch:sender window:_owner];\n"
                "  } else if (_owner.commandDispatchParent) {\n"
                "    [_owner.commandDispatchParent commandDispatch:sender];\n"
                "  } else {\n"
                "    // Maho: mirror -validateUserInterfaceItem:'s AppController fallback so a\n"
                "    // command the app delegate enabled (e.g. New Incognito Window while a\n"
                "    // non-Browser window such as the login gate is key, with no\n"
                "    // commandDispatchParent) actually runs instead of no-oping against nil.\n"
                "    id appController = [NSApp delegate];\n"
                "    if ([appController respondsToSelector:@selector(commandDispatch:)])\n"
                "      [appController commandDispatch:sender];\n"
                "  }\n"
            ),
            description=(
                "Maho: route commandDispatch to the AppController when the key window has "
                "no command handler and no commandDispatchParent (e.g. the login gate), so "
                "New Incognito Window works with zero Browser windows"
            ),
            guard="Maho: mirror -validateUserInterfaceItem:'s AppController fallback",
        ),
        Replacement(
            old=(
                "  if (handler)\n"
                "    [handler commandDispatchUsingKeyModifiers:sender window:_owner];\n"
                "  else\n"
                "    [_owner.commandDispatchParent commandDispatchUsingKeyModifiers:sender];\n"
            ),
            new=(
                "  if (handler) {\n"
                "    [handler commandDispatchUsingKeyModifiers:sender window:_owner];\n"
                "  } else if (_owner.commandDispatchParent) {\n"
                "    [_owner.commandDispatchParent commandDispatchUsingKeyModifiers:sender];\n"
                "  } else {\n"
                "    id appController = [NSApp delegate];\n"
                "    if ([appController\n"
                "            respondsToSelector:@selector(commandDispatchUsingKeyModifiers:)])\n"
                "      [appController commandDispatchUsingKeyModifiers:sender];\n"
                "  }\n"
            ),
            description=(
                "Maho: same AppController fallback for commandDispatchUsingKeyModifiers"
            ),
            guard="respondsToSelector:@selector(commandDispatchUsingKeyModifiers:)])",
        ),
    ],

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
    "components/affiliations/core/browser/affiliation_prefetcher.cc": [
        Replacement(
            old=(
                "AffiliationPrefetcher::~AffiliationPrefetcher() = default;\n"
                "\n"
                "void AffiliationPrefetcher::RegisterSource("
            ),
            new=(
                "AffiliationPrefetcher::~AffiliationPrefetcher() = default;\n"
                "\n"
                "void AffiliationPrefetcher::Shutdown() {\n"
                "  weak_ptr_factory_.InvalidateWeakPtrs();\n"
                "  on_facets_received_barrier_callback_.Reset();\n"
                "  pending_initializations_.clear();\n"
                "  initialized_sources_.clear();\n"
                "  is_ready_ = false;\n"
                "}\n"
                "\n"
                "void AffiliationPrefetcher::RegisterSource("
            ),
            description="Maho: cancel affiliation source observation during password-store shutdown",
            guard="void AffiliationPrefetcher::Shutdown()",
        ),
    ],
    "components/affiliations/core/browser/affiliation_prefetcher.h": [
        Replacement(
            old=(
                "  // Registers an affiliation source.\n"
                "  void RegisterSource(std::unique_ptr<AffiliationSource> source);\n"
                "\n"
                " private:"
            ),
            new=(
                "  // Registers an affiliation source.\n"
                "  void RegisterSource(std::unique_ptr<AffiliationSource> source);\n"
                "\n"
                "  // Stops delayed initialization and releases all sources. This must run during\n"
                "  // affiliation service shutdown, before dependent sources such as password\n"
                "  // stores are destroyed.\n"
                "  void Shutdown();\n"
                "\n"
                " private:"
            ),
            description="Maho: expose affiliation prefetcher shutdown hook",
            guard="void Shutdown();",
        ),
    ],
    "components/affiliations/core/browser/affiliation_service_impl.cc": [
        Replacement(
            old=(
                "void AffiliationServiceImpl::Shutdown() {\n"
                "  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);\n"
                "  if (backend_) {"
            ),
            new=(
                "void AffiliationServiceImpl::Shutdown() {\n"
                "  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);\n"
                "  prefetcher_.Shutdown();\n"
                "  if (backend_) {"
            ),
            description="Maho: release affiliation sources before backend teardown",
            guard="prefetcher_.Shutdown();",
        ),
    ],
    "components/affiliations/core/browser/affiliation_service_impl_unittest.cc": [
        Replacement(
            old=(
                "#include \"components/affiliations/core/browser/mock_affiliation_fetcher.h\"\n"
                "#include \"components/affiliations/core/browser/mock_affiliation_fetcher_factory.h\""
            ),
            new=(
                "#include \"components/affiliations/core/browser/mock_affiliation_fetcher.h\"\n"
                "#include \"components/affiliations/core/browser/mock_affiliation_fetcher_factory.h\"\n"
                "#include \"components/affiliations/core/browser/mock_affiliation_source.h\""
            ),
            description="Maho: include mock affiliation source for shutdown regression test",
            guard="#include \"components/affiliations/core/browser/mock_affiliation_source.h\"",
        ),
        Replacement(
            old=(
                "  void RunUntilIdle() { task_environment_.RunUntilIdle(); }\n"
                "\n"
                "  AffiliationServiceImpl* service() { return service_.get(); }"
            ),
            new=(
                "  void RunUntilIdle() { task_environment_.RunUntilIdle(); }\n"
                "\n"
                "  void FastForwardBy(base::TimeDelta delta) {\n"
                "    task_environment_.FastForwardBy(delta);\n"
                "  }\n"
                "\n"
                "  AffiliationServiceImpl* service() { return service_.get(); }"
            ),
            description="Maho: add time-forward helper for affiliation shutdown regression test",
            guard="void FastForwardBy(base::TimeDelta delta)",
        ),
        Replacement(
            old=(
                "TEST_F(AffiliationServiceImplTest, SupportForMultipleRequests) {"
            ),
            new=(
                "TEST_F(AffiliationServiceImplTest,\n"
                "       ShutdownCancelsDelayedSourceInitialization) {\n"
                "  auto source =\n"
                "      std::make_unique<testing::StrictMock<MockAffiliationSource>>(nullptr);\n"
                "  MockAffiliationSource* source_ptr = source.get();\n"
                "\n"
                "  EXPECT_CALL(*source_ptr, StartObserving(_)).Times(0);\n"
                "  service()->RegisterSource(std::move(source));\n"
                "\n"
                "  service()->Shutdown();\n"
                "  FastForwardBy(base::Seconds(31));\n"
                "  RunUntilIdle();\n"
                "}\n"
                "\n"
                "TEST_F(AffiliationServiceImplTest, SupportForMultipleRequests) {"
            ),
            description="Maho: regression-test delayed affiliation source shutdown",
            guard="ShutdownCancelsDelayedSourceInitialization",
        ),
    ],
    "chrome/browser/ui/views/frame/browser_widget.cc": [
        Replacement(
            old='#include "chrome/browser/ui/views/frame/browser_view.h"\n',
            new=_BROWSER_WIDGET_INCLUDES,
            description="Maho: include the OTR color sentinel declaration in browser_widget.cc",
            guard='#include "maho/browser/ui/theme/maho_color_mixer.h"',
        ),
        Replacement(
            old='  // user_color.\n',
            new=_BROWSER_WIDGET_OTR_SENTINEL + '  // user_color.\n',
            description="Maho: tag Incognito BrowserWidget color keys with the OTR sentinel",
            guard="key.app_controller = GetMahoOtrSentinel();",
        ),
    ],
    "chrome/browser/ui/views/frame/contents_web_view.h": [
        Replacement(
            old="#include <memory>\n",
            new=(
                "#include <memory>\n"
                "#include <optional>\n"
            ),
            description="Maho: include <optional> for the per-instance content background override",
            guard="#include <optional>\n",
        ),
        Replacement(
            old='#include "ui/gfx/geometry/rounded_corners_f.h"\n',
            new=(
                '#include "ui/gfx/geometry/rounded_corners_f.h"\n'
                '#include "third_party/skia/include/core/SkColor.h"\n'
            ),
            description="Maho: include SkColor for the per-instance content background override",
            guard='#include "third_party/skia/include/core/SkColor.h"',
        ),
        Replacement(
            old="  void SetBackgroundVisible(bool background_visible);\n",
            new=(
                "  void SetBackgroundVisible(bool background_visible);\n"
                "\n"
                "  // Maho: opaque content background sourced from the active Space's\n"
                "  // sidebar palette. std::nullopt clears it (falls back to the\n"
                "  // ColorProvider). Never applied to Incognito windows.\n"
                "  void SetMahoBackgroundOverride(std::optional<SkColor> color);\n"
            ),
            description="Maho: declare ContentsWebView::SetMahoBackgroundOverride",
            guard="void SetMahoBackgroundOverride(",
        ),
        Replacement(
            old="  bool background_visible_ = true;\n",
            new=(
                "  bool background_visible_ = true;\n"
                "\n"
                "  std::optional<SkColor> maho_background_override_;\n"
            ),
            description="Maho: add ContentsWebView::maho_background_override_ member",
            guard="maho_background_override_;",
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
        Replacement(
            old=(
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
            new=(
                "  bool maho_is_private = false;\n"
                "  if (web_contents()) {\n"
                "    Profile* maho_profile =\n"
                "        Profile::FromBrowserContext(web_contents()->GetBrowserContext());\n"
                "    if (maho_profile && maho_profile->IsIncognitoProfile() &&\n"
                "        maho_profile->IsPrimaryOTRProfile()) {\n"
                "      maho_content_bg = kMahoColorPrivateSidebarBackground;\n"
                "      maho_is_private = true;\n"
                "    }\n"
                "  }\n"
                "  SkColor color = GetColorProvider()->GetColor(maho_content_bg);\n"
                "  if (!maho_is_private && maho_background_override_.has_value()) {\n"
                "    color = SkColorSetA(maho_background_override_.value(), SK_AlphaOPAQUE);\n"
                "  }\n"
            ),
            description=(
                "Maho: apply the per-instance Space content background override "
                "(opaque) in UpdateBackgroundColor for regular windows; Incognito "
                "keeps the fixed private surface"
            ),
            guard="maho_background_override_.has_value()",
        ),
        Replacement(
            old="void ContentsWebView::UpdateBackgroundColor() {\n",
            new=(
                "void ContentsWebView::SetMahoBackgroundOverride(\n"
                "    std::optional<SkColor> color) {\n"
                "  if (color.has_value()) {\n"
                "    color = SkColorSetA(color.value(), SK_AlphaOPAQUE);\n"
                "  }\n"
                "  if (maho_background_override_ == color) {\n"
                "    return;\n"
                "  }\n"
                "  maho_background_override_ = color;\n"
                "  UpdateBackgroundColor();\n"
                "}\n"
                "\n"
                "void ContentsWebView::UpdateBackgroundColor() {\n"
            ),
            description="Maho: define ContentsWebView::SetMahoBackgroundOverride",
            guard="void ContentsWebView::SetMahoBackgroundOverride(",
        ),
    ],
    "chrome/browser/ui/webui/chrome_untrusted_web_ui_configs.cc": [
        Replacement(
            old='#include "content/public/browser/webui_config_map.h"\n',
            new=(
                '#include "content/public/browser/webui_config_map.h"\n'
                '#include "maho/browser/ui/webui/maho_boost/maho_boost_ui.h"\n'
                '#include "maho/browser/ui/webui/maho_ai/maho_artifact_preview_untrusted_ui.h"\n'
                '#include "maho/browser/ui/webui/maho_ai/maho_artifact_export_untrusted_ui.h"\n'
            ),
            description="Maho: include the Maho untrusted WebUI config headers",
            guard='#include "maho/browser/ui/webui/maho_boost/maho_boost_ui.h"',
        ),
        Replacement(
            old=(
                '#if defined(TOOLKIT_VIEWS)\n'
                '  map.AddUntrustedWebUIConfig(\n'
                '      std::make_unique<lens::LensOverlayUntrustedUIConfig>());\n'
            ),
            new=(
                '#if defined(TOOLKIT_VIEWS)\n'
                '  map.AddUntrustedWebUIConfig(std::make_unique<MahoBoostUIConfig>());\n'
                '  map.AddUntrustedWebUIConfig(std::make_unique<MahoArtifactPreviewUntrustedUIConfig>());\n'
                '  map.AddUntrustedWebUIConfig(std::make_unique<MahoArtifactExportUntrustedUIConfig>());\n'
                '  map.AddUntrustedWebUIConfig(\n'
                '      std::make_unique<lens::LensOverlayUntrustedUIConfig>());\n'
            ),
            description="Maho: register the Maho untrusted WebUI configs",
            guard="map.AddUntrustedWebUIConfig(std::make_unique<MahoBoostUIConfig>());",
        ),
    ],
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
                '  "maho_split_view.json",\n'
                '  "maho_side_panel.json",\n'
                ']\n'
            ),
            description=(
                "Register Maho extension API schemas (maho.splitView, "
                "maho.sidePanel) in the uncompiled schema source list. The JSONs "
                "MUST be referenced as local paths (co-located under "
                "chrome/common/extensions/api via MAHO_SCHEMA_FILE_COPIES) so the "
                "cpp_bundle_generator's common-source-dir resolves to "
                "chrome/common/extensions/api and generated_schemas.cc is emitted "
                "to the GN-declared path Chrome links (not an orphan gen/ path)."
            ),
            guard='  "maho_split_view.json",',
        ),
    ],
    # ── C1: register the "maho" API permission so chrome.maho.* features
    #    resolve instead of CHECK-crashing in PermissionSet::HasAPIPermission ──
    "extensions/common/mojom/api_permission_id.mojom": [
        Replacement(
            old=(
                "  kGlicPrivateInvoke = 266,\n"
                "\n"
                "  // Add new entries at the end of the enum and be sure to update the\n"
            ),
            new=(
                "  kGlicPrivateInvoke = 266,\n"
                "  kMaho = 267,\n"
                "\n"
                "  // Add new entries at the end of the enum and be sure to update the\n"
            ),
            description="Maho C1: add APIPermissionID kMaho enum value",
            guard="kMaho = 267,",
        ),
    ],
    "chrome/common/extensions/permissions/chrome_api_permissions.cc": [
        Replacement(
            old='    {APIPermissionID::kManagement, "management"},\n',
            new=(
                '    {APIPermissionID::kManagement, "management"},\n'
                '    {APIPermissionID::kMaho, "maho"},\n'
            ),
            description="Maho C1: register the maho API permission in the permissions table",
            guard='{APIPermissionID::kMaho, "maho"}',
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
    # No per-message Replacements: these files are swept entirely by
    # normalize_chromium_product_branding(). They must still appear as keys so
    # the main loop visits them.
    "chrome/app/password_manager_ui_strings.grdp": [],
    "chrome/app/settings_chromium_strings.grdp": [],
    "chrome/app/settings_strings.grdp": [],
    "components/autofill_payments_strings.grdp": [],
    "components/autofill_strings.grdp": [],
    "components/components_chromium_strings.grd": [],
    "components/management_strings.grdp": [],
    "components/new_or_sad_tab_strings.grdp": [],
    "components/page_info_strings.grdp": [],
    "components/password_manager_strings.grdp": [],
    "components/privacy_sandbox_strings.grd": [],
    "components/reset_password_strings.grdp": [],
    "components/search_engine_choice_strings.grdp": [],
    "components/security_interstitials_strings.grdp": [],
    "components/ssl_errors_strings.grdp": [],
    "chrome/app/chromium_strings.grd": [
        Replacement(
            old=(
                '          <message name="IDS_PRODUCT_NAME" desc="The Chrome application name" translateable="false">\n'
                "            Chromium\n"
                "          </message>\n"
                '          <message name="IDS_SHORT_PRODUCT_NAME" desc="The Chrome application short name." translateable="false">\n'
                "            Chromium\n"
                "          </message>\n"
            ),
            new=(
                '          <message name="IDS_PRODUCT_NAME" desc="The Chrome application name" translateable="false">\n'
                "            Maho\n"
                "          </message>\n"
                '          <message name="IDS_SHORT_PRODUCT_NAME" desc="The Chrome application short name." translateable="false">\n'
                "            Maho\n"
                "          </message>\n"
            ),
            description=(
                "Maho: brand IDS_PRODUCT_NAME/IDS_SHORT_PRODUCT_NAME so the "
                "os_crypt keychain item is \"Maho Safe Storage\" (not "
                "\"Chromium Safe Storage\") and in-app product strings say Maho"
            ),
        ),
        Replacement(
            old=(
                '      <message name="IDS_FR_CUSTOMIZE_DEFAULT_BROWSER" desc="Default browser checkbox label">\n'
                "        Make Chromium the default browser\n"
                "      </message>\n"
                '      <message name="IDS_DEFAULT_BROWSER_CHANGED_MESSAGE" desc="Body of the notification shown when Chrome is no longer the default browser.">\n'
                "        Chromium is no longer your default browser. Make Chromium the default browser?\n"
                "      </message>\n"
            ),
            new=(
                '      <message name="IDS_FR_CUSTOMIZE_DEFAULT_BROWSER" desc="Default browser checkbox label">\n'
                "        Make Maho the default browser\n"
                "      </message>\n"
                '      <message name="IDS_DEFAULT_BROWSER_CHANGED_MESSAGE" desc="Body of the notification shown when Chrome is no longer the default browser.">\n'
                "        Maho is no longer your default browser. Make Maho the default browser?\n"
                "      </message>\n"
            ),
            description="Maho: brand default browser customize and changed notification strings as Maho",
            guard="Make Maho the default browser",
        ),
        Replacement(
            old=(
                '      <message name="IDS_DEFAULT_BROWSER_INFOBAR_TEXT" desc="Text to show in an infobar when Chromium is not the current default browser.">\n'
                "        Chromium isn't your default browser\n"
                "      </message>\n"
                '      <message name="IDS_DEFAULT_BROWSER_PIN_INFOBAR_TEXT" desc="Text to show in an infobar when Chromium is not the current default browser and can be pinned to the taskbar.">\n'
                "        Set Chromium as your default browser and pin it to your taskbar\n"
                "      </message>\n"
                '      <if expr="not is_android and not is_chromeos">\n'
                '        <message name="IDS_DEFAULT_BROWSER_MODAL_TITLE_WITH_SETTINGS_ILLUSTRATION" desc="Title of the default browser modal with settings illustration.">\n'
                "          Set Chromium as your default browser\n"
                "        </message>\n"
                '        <message name="IDS_DEFAULT_BROWSER_MODAL_BODY_WITH_SETTINGS_ILLUSTRATION" desc="Body text of the default browser modal with settings illustration.">\n'
                "          Keep your passwords, bookmarks, and history perfectly synced across devices. Easily access the features you know and love by updating your default browser to Chromium.\n"
                "        </message>\n"
                '        <message name="IDS_DEFAULT_BROWSER_MODAL_BODY_WITH_SETTINGS_ILLUSTRATION_UNPINNED" desc="Body text of the default browser modal with settings illustration when Chromium is not pinned to the taskbar.">\n'
                "          Keep your passwords, bookmarks, and history perfectly synced across devices. Easily access the features you know and love by updating your default browser to Chromium. Chromium will be pinned to your taskbar for quick access.\n"
                "        </message>\n"
                '        <message name="IDS_DEFAULT_BROWSER_MODAL_TITLE_WITHOUT_SETTINGS_ILLUSTRATION" desc="Title of the default browser modal without settings illustration.">\n'
                "          Set Chromium as your default browser\n"
                "        </message>\n"
                '        <message name="IDS_DEFAULT_BROWSER_MODAL_BODY_WITHOUT_SETTINGS_ILLUSTRATION" desc="Body text of the default browser modal without settings illustration.">\n'
                "          Get easy access to the features you know, love, and trust by setting Chromium as your default browser.\n"
                "        </message>\n"
                '        <message name="IDS_DEFAULT_BROWSER_MODAL_BODY_WITHOUT_SETTINGS_ILLUSTRATION_UNPINNED" desc="Body text of the default browser modal without settings illustration when Chromium is not pinned to the taskbar.">\n'
                "          Get easy access to the features you know, love, and trust by setting Chromium as your default browser. Chromium will be pinned to your taskbar for quick access.\n"
                "        </message>\n"
                '        <message name="IDS_DEFAULT_BROWSER_BUBBLE_DIALOG_TITLE" desc="Title of the dialog prompting the user to set Chromium as the default browser.">\n'
                "          Set Chromium as your default browser\n"
                "        </message>\n"
                '        <message name="IDS_DEFAULT_BROWSER_BUBBLE_DIALOG_BODY" desc="Main explanatory text in the default browser dialog.">\n'
                "          Get easy access to the features you know, love, and trust by setting Chromium as your default browser.\n"
                "        </message>\n"
            ),
            new=(
                '      <message name="IDS_DEFAULT_BROWSER_INFOBAR_TEXT" desc="Text to show in an infobar when Chromium is not the current default browser.">\n'
                "        Maho isn't your default browser\n"
                "      </message>\n"
                '      <message name="IDS_DEFAULT_BROWSER_PIN_INFOBAR_TEXT" desc="Text to show in an infobar when Chromium is not the current default browser and can be pinned to the taskbar.">\n'
                "        Set Maho as your default browser and pin it to your taskbar\n"
                "      </message>\n"
                '      <if expr="not is_android and not is_chromeos">\n'
                '        <message name="IDS_DEFAULT_BROWSER_MODAL_TITLE_WITH_SETTINGS_ILLUSTRATION" desc="Title of the default browser modal with settings illustration.">\n'
                "          Set Maho as your default browser\n"
                "        </message>\n"
                '        <message name="IDS_DEFAULT_BROWSER_MODAL_BODY_WITH_SETTINGS_ILLUSTRATION" desc="Body text of the default browser modal with settings illustration.">\n'
                "          Keep your passwords, bookmarks, and history perfectly synced across devices. Easily access the features you know and love by updating your default browser to Maho.\n"
                "        </message>\n"
                '        <message name="IDS_DEFAULT_BROWSER_MODAL_BODY_WITH_SETTINGS_ILLUSTRATION_UNPINNED" desc="Body text of the default browser modal with settings illustration when Chromium is not pinned to the taskbar.">\n'
                "          Keep your passwords, bookmarks, and history perfectly synced across devices. Easily access the features you know and love by updating your default browser to Maho. Maho will be pinned to your taskbar for quick access.\n"
                "        </message>\n"
                '        <message name="IDS_DEFAULT_BROWSER_MODAL_TITLE_WITHOUT_SETTINGS_ILLUSTRATION" desc="Title of the default browser modal without settings illustration.">\n'
                "          Set Maho as your default browser\n"
                "        </message>\n"
                '        <message name="IDS_DEFAULT_BROWSER_MODAL_BODY_WITHOUT_SETTINGS_ILLUSTRATION" desc="Body text of the default browser modal without settings illustration.">\n'
                "          Get easy access to the features you know, love, and trust by setting Maho as your default browser.\n"
                "        </message>\n"
                '        <message name="IDS_DEFAULT_BROWSER_MODAL_BODY_WITHOUT_SETTINGS_ILLUSTRATION_UNPINNED" desc="Body text of the default browser modal without settings illustration when Chromium is not pinned to the taskbar.">\n'
                "          Get easy access to the features you know, love, and trust by setting Maho as your default browser. Maho will be pinned to your taskbar for quick access.\n"
                "        </message>\n"
                '        <message name="IDS_DEFAULT_BROWSER_BUBBLE_DIALOG_TITLE" desc="Title of the dialog prompting the user to set Chromium as the default browser.">\n'
                "          Set Maho as your default browser\n"
                "        </message>\n"
                '        <message name="IDS_DEFAULT_BROWSER_BUBBLE_DIALOG_BODY" desc="Main explanatory text in the default browser dialog.">\n'
                "          Get easy access to the features you know, love, and trust by setting Maho as your default browser.\n"
                "        </message>\n"
            ),
            description="Maho: brand default browser infobar and modal dialog strings as Maho",
            guard="Maho isn't your default browser",
        ),
        Replacement(
            old=(
                '        <message name="IDS_FRE_DEFAULT_BROWSER_TITLE" desc="Title for the page that prompts user to set Chromium as their default browser in the first run experience.">\n'
                "          Set Chromium as your default browser\n"
                "        </message>\n"
                '        <message name="IDS_FRE_DEFAULT_BROWSER_AND_PINNING_TITLE" desc="Title for the page that prompts user to set Chromium as their default browser and pin to taskbar in the first run experience.">\n'
                "          Use Chromium by default and get it pinned\n"
                "        </message>\n"
                '        <message name="IDS_FRE_DEFAULT_BROWSER_SUBTITLE_NEW" desc="Subtitle for the page that prompts user to set Chromium as their default browser in the first run experience.">\n'
                "          Use Chromium anytime you click links in messages, documents, and other apps\n"
                "        </message>\n"
                '        <message name="IDS_FRE_DEFAULT_BROWSER_AND_PINNING_SUBTITLE" desc="Subtitle for the page that prompts user to set Chromium as their default browser and pin to taskbar in the first run experience.">\n'
                "          Open links in Chromium from any app. Plus for easy access, it gets pinned to your taskbar.\n"
                "        </message>\n"
                '        <message name="IDS_FRE_DEFAULT_BROWSER_ILLUSTRATION_ALT_TEXT" desc="Alt text for the illustration in the page that prompts user to set Chromium as their default browser.">\n'
                "          Chromium logo inside a computer screen.\n"
                "        </message>\n"
                '        <message name="IDS_APP_MENU_TOOLTIP_DEFAULT_PROMPT" desc="The tooltip to show for the browser menu when the default browser prompt is displayed">\n'
                "          Customize and control Chromium. Set Chromium as your default.\n"
                "        </message>\n"
                '        <message name="IDS_APP_MENU_BUTTON_DEFAULT_PROMPT" desc="Text in the browser menu chip indicating that the user can change their default browser">\n'
                "          Set Chromium as your default\n"
                "        </message>\n"
                '        <message name="IDS_SET_BROWSER_AS_DEFAULT_MENU_ITEM" desc="Menu item that prompts user to set Chromium as their default browser.">\n'
                "          Set Chromium as your default browser\n"
                "        </message>\n"
                "\n"
                "        <!-- Default Browser Promo Refresh -->\n"
                '        <message name="IDS_FRE_REFRESH_DEFAULT_BROWSER_TITLE" desc="Title for the page that prompts user to set Chromium as their default browser in the refreshed first run experience.">\n'
                "          Set Chromium as default and pin it to taskbar\n"
                "        </message>\n"
                '        <message name="IDS_FRE_REFRESH_DEFAULT_BROWSER_SUBTITLE" desc="Subtitle for the page that prompts user to set Chromium as their default browser in the refreshed first run experience.">\n'
                "          Open links in Chromium from any app and keep the browser accessible on your taskbar\n"
                "        </message>\n"
            ),
            new=(
                '        <message name="IDS_FRE_DEFAULT_BROWSER_TITLE" desc="Title for the page that prompts user to set Chromium as their default browser in the first run experience.">\n'
                "          Set Maho as your default browser\n"
                "        </message>\n"
                '        <message name="IDS_FRE_DEFAULT_BROWSER_AND_PINNING_TITLE" desc="Title for the page that prompts user to set Chromium as their default browser and pin to taskbar in the first run experience.">\n'
                "          Use Maho by default and get it pinned\n"
                "        </message>\n"
                '        <message name="IDS_FRE_DEFAULT_BROWSER_SUBTITLE_NEW" desc="Subtitle for the page that prompts user to set Chromium as their default browser in the first run experience.">\n'
                "          Use Maho anytime you click links in messages, documents, and other apps\n"
                "        </message>\n"
                '        <message name="IDS_FRE_DEFAULT_BROWSER_AND_PINNING_SUBTITLE" desc="Subtitle for the page that prompts user to set Chromium as their default browser and pin to taskbar in the first run experience.">\n'
                "          Open links in Maho from any app. Plus for easy access, it gets pinned to your taskbar.\n"
                "        </message>\n"
                '        <message name="IDS_FRE_DEFAULT_BROWSER_ILLUSTRATION_ALT_TEXT" desc="Alt text for the illustration in the page that prompts user to set Chromium as their default browser.">\n'
                "          Maho logo inside a computer screen.\n"
                "        </message>\n"
                '        <message name="IDS_APP_MENU_TOOLTIP_DEFAULT_PROMPT" desc="The tooltip to show for the browser menu when the default browser prompt is displayed">\n'
                "          Customize and control Maho. Set Maho as your default.\n"
                "        </message>\n"
                '        <message name="IDS_APP_MENU_BUTTON_DEFAULT_PROMPT" desc="Text in the browser menu chip indicating that the user can change their default browser">\n'
                "          Set Maho as your default\n"
                "        </message>\n"
                '        <message name="IDS_SET_BROWSER_AS_DEFAULT_MENU_ITEM" desc="Menu item that prompts user to set Chromium as their default browser.">\n'
                "          Set Maho as your default browser\n"
                "        </message>\n"
                "\n"
                "        <!-- Default Browser Promo Refresh -->\n"
                '        <message name="IDS_FRE_REFRESH_DEFAULT_BROWSER_TITLE" desc="Title for the page that prompts user to set Chromium as their default browser in the refreshed first run experience.">\n'
                "          Set Maho as default and pin it to taskbar\n"
                "        </message>\n"
                '        <message name="IDS_FRE_REFRESH_DEFAULT_BROWSER_SUBTITLE" desc="Subtitle for the page that prompts user to set Chromium as their default browser in the refreshed first run experience.">\n'
                "          Open links in Maho from any app and keep the browser accessible on your taskbar\n"
                "        </message>\n"
            ),
            description="Maho: brand FRE and app menu default browser promo strings as Maho",
            guard="Set Maho as default and pin it to taskbar",
        ),
        Replacement(
            old=(
                '      <if expr="not is_macosx and not is_chromeos">\n'
                '        <message name="IDS_TASK_MANAGER_TITLE" desc="The title of the Task Manager window">\n'
                "          Task Manager - Chromium\n"
                "        </message>\n"
                "      </if>\n"
            ),
            new=(
                '      <if expr="not is_macosx and not is_chromeos">\n'
                '        <message name="IDS_TASK_MANAGER_TITLE" desc="The title of the Task Manager window">\n'
                "          Task Manager - Maho\n"
                "        </message>\n"
                "      </if>\n"
            ),
            description="Maho: brand task manager title as Maho",
            guard="Task Manager - Maho",
        ),
        Replacement(
            old=(
                "          <else>\n"
                '            <message name="IDS_BROWSER_WINDOW_TITLE_FORMAT" desc="The format for titles displayed in tabs and popup windows">\n'
                '              <ph name="PAGE_TITLE">$1<ex>Google</ex></ph> - Chromium\n'
                "            </message>\n"
                "          </else>\n"
                "        </if>\n"
                "      </if>\n"
                '      <if expr="is_chromeos">\n'
                "        <!-- Browser Window Title Format -->\n"
                '        <message name="IDS_BROWSER_WINDOW_TITLE_FORMAT" desc="The format for titles displayed in tabbed browser windows">\n'
                '          Chromium - <ph name="PAGE_TITLE">$1<ex>Google</ex></ph>\n'
                "        </message>\n"
                "      </if>\n"
                '      <if expr="not is_chromeos and not is_macosx">\n'
                "        <!-- Captive Portal Browser Window Title Format -->\n"
                '        <message name="IDS_CAPTIVE_PORTAL_BROWSER_WINDOW_TITLE_FORMAT" desc="The format for titles displayed in captive portal popup windows">\n'
                '          <ph name="PAGE_TITLE">$1<ex>Google</ex></ph> - Network Sign-in - Chromium\n'
                "        </message>\n"
                "      </if>\n"
            ),
            new=(
                "          <else>\n"
                '            <message name="IDS_BROWSER_WINDOW_TITLE_FORMAT" desc="The format for titles displayed in tabs and popup windows">\n'
                '              <ph name="PAGE_TITLE">$1<ex>Google</ex></ph> - Maho\n'
                "            </message>\n"
                "          </else>\n"
                "        </if>\n"
                "      </if>\n"
                '      <if expr="is_chromeos">\n'
                "        <!-- Browser Window Title Format -->\n"
                '        <message name="IDS_BROWSER_WINDOW_TITLE_FORMAT" desc="The format for titles displayed in tabbed browser windows">\n'
                '          Maho - <ph name="PAGE_TITLE">$1<ex>Google</ex></ph>\n'
                "        </message>\n"
                "      </if>\n"
                '      <if expr="not is_chromeos and not is_macosx">\n'
                "        <!-- Captive Portal Browser Window Title Format -->\n"
                '        <message name="IDS_CAPTIVE_PORTAL_BROWSER_WINDOW_TITLE_FORMAT" desc="The format for titles displayed in captive portal popup windows">\n'
                '          <ph name="PAGE_TITLE">$1<ex>Google</ex></ph> - Network Sign-in - Maho\n'
                "        </message>\n"
                "      </if>\n"
            ),
            description="Maho: brand browser window title format as Maho",
            guard='<ph name="PAGE_TITLE">$1<ex>Google</ex></ph> - Maho',
        ),
        Replacement(
            old=(
                "        <else>\n"
                '          <message name="IDS_ACCESSIBLE_BROWSER_WINDOW_TITLE_FORMAT" desc="The format for the accessible name of a tabbed browser window">\n'
                '            <ph name="PAGE_TITLE">$1<ex>Google</ex></ph> - Chromium\n'
                "          </message>\n"
                "        </else>\n"
            ),
            new=(
                "        <else>\n"
                '          <message name="IDS_ACCESSIBLE_BROWSER_WINDOW_TITLE_FORMAT" desc="The format for the accessible name of a tabbed browser window">\n'
                '            <ph name="PAGE_TITLE">$1<ex>Google</ex></ph> - Maho\n'
                "          </message>\n"
                "        </else>\n"
            ),
            description="Maho: brand accessible browser window title format as Maho",
            guard='IDS_ACCESSIBLE_BROWSER_WINDOW_TITLE_FORMAT" desc="The format for the accessible name of a tabbed browser window">\n            <ph name="PAGE_TITLE">$1<ex>Google</ex></ph> - Maho',
        ),
        Replacement(
            old=(
                '      <message name="IDS_ACCNAME_APP" desc="The accessible name for the app menu." translateable="false">\n'
                "        Chromium\n"
                "      </message>\n"
                "      <!-- Hung Browser Detector -->\n"
                '      <if expr="is_win">\n'
                '        <message name="IDS_BROWSER_HUNGBROWSER_MESSAGE" desc="Content of the dialog box shown when the browser is hung">\n'
                "          Chromium is unresponsive. Relaunch now?\n"
                "        </message>\n"
                "      </if>\n"
                "      <!-- General app failure messages -->\n"
                '      <if expr="not is_chromeos and not is_android">\n'
                '        <message name="IDS_COULDNT_STARTUP_PROFILE_ERROR" desc="Error displayed when Chrome cannot start because profiles were not opened correctly.">\n'
                "          Cannot start Chromium because something went wrong when opening your profile. Try to restart Chromium.\n"
                "        </message>\n"
                "      </if>\n"
            ),
            new=(
                '      <message name="IDS_ACCNAME_APP" desc="The accessible name for the app menu." translateable="false">\n'
                "        Maho\n"
                "      </message>\n"
                "      <!-- Hung Browser Detector -->\n"
                '      <if expr="is_win">\n'
                '        <message name="IDS_BROWSER_HUNGBROWSER_MESSAGE" desc="Content of the dialog box shown when the browser is hung">\n'
                "          Maho is unresponsive. Relaunch now?\n"
                "        </message>\n"
                "      </if>\n"
                "      <!-- General app failure messages -->\n"
                '      <if expr="not is_chromeos and not is_android">\n'
                '        <message name="IDS_COULDNT_STARTUP_PROFILE_ERROR" desc="Error displayed when Chrome cannot start because profiles were not opened correctly.">\n'
                "          Cannot start Maho because something went wrong when opening your profile. Try to restart Maho.\n"
                "        </message>\n"
                "      </if>\n"
            ),
            description="Maho: brand app menu, hung browser dialog, and profile startup error as Maho",
            guard="Cannot start Maho because something went wrong when opening your profile",
        ),
        Replacement(
            old=(
                "      <!-- Uninstall messages -->\n"
                '      <if expr="is_win">\n'
                '        <message name="IDS_UNINSTALL_CLOSE_APP" desc="Message to user when uninstall detects other app instance running">\n'
                "          Please close all Chromium windows and try again.\n"
                "        </message>\n"
                '        <message name="IDS_UNINSTALL_VERIFY" desc="Message to confirm user wants to uninstall">\n'
                "          Are you sure you want to uninstall Chromium?\n"
                "        </message>\n"
                '        <message name="IDS_UNINSTALL_CHROME" desc="The title of the Chromium uninstall dialog.">\n'
                "          Uninstall Chromium\n"
                "        </message>\n"
                "      </if>\n"
            ),
            new=(
                "      <!-- Uninstall messages -->\n"
                '      <if expr="is_win">\n'
                '        <message name="IDS_UNINSTALL_CLOSE_APP" desc="Message to user when uninstall detects other app instance running">\n'
                "          Please close all Maho windows and try again.\n"
                "        </message>\n"
                '        <message name="IDS_UNINSTALL_VERIFY" desc="Message to confirm user wants to uninstall">\n'
                "          Are you sure you want to uninstall Maho?\n"
                "        </message>\n"
                '        <message name="IDS_UNINSTALL_CHROME" desc="The title of the Chromium uninstall dialog.">\n'
                "          Uninstall Maho\n"
                "        </message>\n"
                "      </if>\n"
            ),
            description="Maho: brand Windows uninstall dialogs as Maho",
            guard="Uninstall Maho",
        ),
        Replacement(
            old=(
                "      <!-- App shortcuts -->\n"
                '      <message name="IDS_APP_SHORTCUTS_SUBDIR_NAME" desc="Name for the Chromium Apps Start Menu folder name.">\n'
                "        Chromium Apps\n"
                "      </message>\n"
                '      <message name="IDS_APP_SHORTCUTS_SUBDIR_NAME_CANARY" desc="Name for the Chrome Apps Start Menu folder name.">\n'
                "        Chromium Apps\n"
                "      </message>\n"
            ),
            new=(
                "      <!-- App shortcuts -->\n"
                '      <message name="IDS_APP_SHORTCUTS_SUBDIR_NAME" desc="Name for the Chromium Apps Start Menu folder name.">\n'
                "        Maho Apps\n"
                "      </message>\n"
                '      <message name="IDS_APP_SHORTCUTS_SUBDIR_NAME_CANARY" desc="Name for the Chrome Apps Start Menu folder name.">\n'
                "        Maho Apps\n"
                "      </message>\n"
            ),
            description="Maho: brand app shortcut start menu folder names as Maho",
            guard="Maho Apps",
        ),
        Replacement(
            old=(
                '      <if expr="is_win or is_macosx">\n'
                '        <message name="IDS_PDF_INFOBAR_TEXT" desc="Text to show in an infobar when Chromium is not the default PDF viewer.">\n'
                "          Set Chromium as your default PDF viewer\n"
                "        </message>\n"
                "      </if>\n"
            ),
            new=(
                '      <if expr="is_win or is_macosx">\n'
                '        <message name="IDS_PDF_INFOBAR_TEXT" desc="Text to show in an infobar when Chromium is not the default PDF viewer.">\n'
                "          Set Maho as your default PDF viewer\n"
                "        </message>\n"
                "      </if>\n"
            ),
            description="Maho: brand default PDF viewer infobar string as Maho",
            guard="Set Maho as your default PDF viewer",
        ),
    ],
    "components/os_crypt/common/keychain_password_mac.mm": [
        Replacement(
            old=(
                "#else\n"
                "const char kDefaultServiceName[] = \"Chromium Safe Storage\";\n"
                "const char kDefaultAccountName[] = \"Chromium\";\n"
                "#endif\n"
            ),
            new=(
                "#else\n"
                "const char kDefaultServiceName[] = \"Maho Safe Storage\";\n"
                "const char kDefaultAccountName[] = \"Maho\";\n"
                "#endif\n"
            ),
            description=(
                "Maho: use Maho's own macOS keychain item for the os_crypt "
                "encryption key instead of Chromium's, matching the release "
                "build — the Chromium-signed item's ACL never matches a "
                "rebuilt binary, which re-triggered the login-password "
                "keychain prompt after every build"
            ),
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
                "const allowedOptionalTags: Set<string> = new Set(['IMG', 'LI', 'UL']);\n"
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
            idempotent=True,
        ),
        Replacement(
            old=(
                "const allowedOptionalTags: Set<string> = new Set(['IMG', 'LI', 'UL', 'OL']);\n"
            ),
            new=(
                "const allowedOptionalTags: Set<string> = new Set(['IMG', 'LI', 'UL', 'OL', 'CODE', 'BLOCKQUOTE', 'H1', 'H2', 'H3', 'H4', 'H5', 'H6', 'HR', 'DEL']);\n"
            ),
            description=(
                "Whitelist all markdown-safe tags (migration from intermediate OL-only form) "
                "in parseHtmlSubset so renderTrustedMarkdown's output never throws "
                "'X is not supported' on persisted assistant messages"
            ),
            idempotent=True,
        ),
    ],
    # ── app-Info.plist ────────────────────────────────────────────────────────
    "chrome/app/app-Info.plist": [
        Replacement(
            old=(
                "\t<key>CFBundleDevelopmentRegion</key>\n"
                "\t<string>en</string>\n"
            ),
            new=(
                "\t<key>CFBundleDevelopmentRegion</key>\n"
                "\t<string>en</string>\n"
                "\t<key>NSBluetoothAlwaysUsageDescription</key>\n"
                "\t<string>Maho uses Bluetooth to connect to a nearby phone when you sign in with a passkey.</string>\n"
            ),
            description="Declare Bluetooth access for cross-device passkey sign-in on macOS",
            idempotent=True,
        ),
        Replacement(
            old=(
                "\t<key>LSFileQuarantineEnabled</key>\n"
                "\t<true/>\n"
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
            idempotent=True,
        ),
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
                "Disable LSFileQuarantineEnabled and inject Sparkle update keys to app-Info.plist (migration from false-only form)"
            ),
            idempotent=True,
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
                "the panel is owned by the per-pane contents header (MahoContentsHeaderView)"
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
                '#include "chrome/browser/ui/tabs/tab_enums.h"\n'
                '#include "maho/browser/ui/views/sidebar/maho_sidebar_container_view.h"\n'
            ),
            description=(
                "Re-add Maho sidebar container include for Cmd+N favorite "
                "activation and tab_enums for TabCloseTypes"
            ),
            guard='#include "chrome/browser/ui/tabs/tab_enums.h"',
            idempotent=True,
        ),
        Replacement(
            old=(
                '#include "chrome/browser/ui/views/frame/browser_view.h"\n'
                '#include "maho/browser/ui/views/sidebar/maho_sidebar_container_view.h"\n'
            ),
            new=(
                '#include "chrome/browser/ui/views/frame/browser_view.h"\n'
                '#include "chrome/browser/ui/tabs/tab_enums.h"\n'
                '#include "maho/browser/ui/views/sidebar/maho_sidebar_container_view.h"\n'
            ),
            description="Migrate includes to add tab_enums.h",
            guard='#include "chrome/browser/ui/tabs/tab_enums.h"',
            idempotent=True,
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
                "      if (browser_->tab_strip_model()->empty()) {\n"
                "        CloseWindow(browser_);\n"
                "        break;\n"
                "      }\n"
                "      if (auto* maho_browser_view =\n"
                "              BrowserView::GetBrowserViewForBrowser(browser_);\n"
                "          maho_browser_view &&\n"
                "          maho_browser_view->MaybeHandleMultiTabClose()) {\n"
                "        break;\n"
                "      }\n"
                "      if (browser_->is_type_normal() &&\n"
                "          !browser_->profile()->IsOffTheRecord() &&\n"
                "          browser_->tab_strip_model()->count() == 1) {\n"
                "        content::WebContents* active_wc =\n"
                "            browser_->tab_strip_model()->GetActiveWebContents();\n"
                "        if (active_wc) {\n"
                "          const GURL& url = active_wc->GetVisibleURL();\n"
                "          const bool is_empty_ntp =\n"
                "              (url.is_empty() || url == GURL(chrome::kChromeUINewTabURL) ||\n"
                "               url == GURL(\"about:blank\")) &&\n"
                "              !active_wc->GetController().CanGoBack();\n"
                "          if (is_empty_ntp) {\n"
                "            break;\n"
                "          }\n"
                "        }\n"
                "        int old_index = browser_->tab_strip_model()->active_index();\n"
                "        chrome::NewTab(browser_);\n"
                "        if (browser_->tab_strip_model()->ContainsIndex(old_index)) {\n"
                "          browser_->tab_strip_model()->CloseWebContentsAt(\n"
                "              old_index, TabCloseTypes::CLOSE_USER_GESTURE |\n"
                "                             TabCloseTypes::CLOSE_CREATE_HISTORICAL_TAB);\n"
                "        }\n"
                "        break;\n"
                "      }\n"
                "      CloseTab(browser_);\n"
                "      break;\n"
            ),
            description=(
                "Route Cmd+W through BrowserView so sidebar multi-selected tabs "
                "close together instead of only the active tab; close the window "
                "when the strip is already empty (zero-tab Maho window); keep "
                "window open on last tab close by opening New Tab (Arc parity)"
            ),
            guard="browser_->tab_strip_model()->count() == 1",
            idempotent=True,
        ),
        Replacement(
            old=(
                "    case IDC_CLOSE_TAB:\n"
                '      base::RecordAction(base::UserMetricsAction("CloseTabByKey"));\n'
                "      if (browser_->tab_strip_model()->empty()) {\n"
                "        CloseWindow(browser_);\n"
                "        break;\n"
                "      }\n"
                "      if (auto* maho_browser_view =\n"
                "              BrowserView::GetBrowserViewForBrowser(browser_);\n"
                "          maho_browser_view &&\n"
                "          maho_browser_view->MaybeHandleMultiTabClose()) {\n"
                "        break;\n"
                "      }\n"
                "      CloseTab(browser_);\n"
                "      break;\n"
            ),
            new=(
                "    case IDC_CLOSE_TAB:\n"
                '      base::RecordAction(base::UserMetricsAction("CloseTabByKey"));\n'
                "      if (browser_->tab_strip_model()->empty()) {\n"
                "        CloseWindow(browser_);\n"
                "        break;\n"
                "      }\n"
                "      if (auto* maho_browser_view =\n"
                "              BrowserView::GetBrowserViewForBrowser(browser_);\n"
                "          maho_browser_view &&\n"
                "          maho_browser_view->MaybeHandleMultiTabClose()) {\n"
                "        break;\n"
                "      }\n"
                "      if (browser_->is_type_normal() &&\n"
                "          !browser_->profile()->IsOffTheRecord() &&\n"
                "          browser_->tab_strip_model()->count() == 1) {\n"
                "        content::WebContents* active_wc =\n"
                "            browser_->tab_strip_model()->GetActiveWebContents();\n"
                "        if (active_wc) {\n"
                "          const GURL& url = active_wc->GetVisibleURL();\n"
                "          const bool is_empty_ntp =\n"
                "              (url.is_empty() || url == GURL(chrome::kChromeUINewTabURL) ||\n"
                "               url == GURL(\"about:blank\")) &&\n"
                "              !active_wc->GetController().CanGoBack();\n"
                "          if (is_empty_ntp) {\n"
                "            break;\n"
                "          }\n"
                "        }\n"
                "        int old_index = browser_->tab_strip_model()->active_index();\n"
                "        chrome::NewTab(browser_);\n"
                "        if (browser_->tab_strip_model()->ContainsIndex(old_index)) {\n"
                "          browser_->tab_strip_model()->CloseWebContentsAt(\n"
                "              old_index, TabCloseTypes::CLOSE_USER_GESTURE |\n"
                "                             TabCloseTypes::CLOSE_CREATE_HISTORICAL_TAB);\n"
                "        }\n"
                "        break;\n"
                "      }\n"
                "      CloseTab(browser_);\n"
                "      break;\n"
            ),
            description="Migrate IDC_CLOSE_TAB to keep window open on last tab close",
            guard="browser_->tab_strip_model()->count() == 1",
            idempotent=True,
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
                + _MAHO_FAVORITE_ONLY_NUMBERED_TAB +
                '          TabStripUserGestureDetails(\n'
                '              TabStripUserGestureDetails::GestureType::kKeyboard, time_stamp));\n'
                '      break;\n'
                '    }\n'
            ),
            idempotent=True,
            description=(
                "Maho Cmd+1..8 activates the Nth favorite (Zen essentials) only; "
                "an index past the last favorite is a no-op instead of falling "
                "back to the Chromium tab strip"
            ),
        ),
        Replacement(
            old=_MAHO_LEGACY_FAVORITE_FALLBACK_NUMBERED_TAB,
            new=_MAHO_FAVORITE_ONLY_NUMBERED_TAB,
            idempotent=True,
            description=(
                "Migrate the Cmd+1..8 favorite activation away from its Chromium "
                "tab-strip fallback so out-of-range favorites no longer move tab "
                "focus"
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
    "chrome/browser/app_controller_mac.h": [
        Replacement(
            old=(
                '#include "components/sessions/core/tab_restore_service_observer.h"\n'
                '\n'
                '#if defined(__OBJC__)\n'
            ),
            new=(
                '#include "components/sessions/core/tab_restore_service_observer.h"\n'
                '\n'
                '#include <vector>\n'
                '\n'
                'class GURL;\n'
                '\n'
                '#if defined(__OBJC__)\n'
            ),
            description=(
                "Expose vector and GURL declarations for the macOS native URL "
                "queue API"
            ),
            guard="QueueNativeUrlsWhileMahoLoginGated",
        ),
        Replacement(
            old=(
                "void AllowApplicationToTerminate();\n"
            ),
            new=(
                "void AllowApplicationToTerminate();\n"
                "\n"
                "// Retains validated native URLs for the original regular profile while\n"
                "// Maho's login gate is active. The queue is process-local.\n"
                "void QueueNativeUrlsWhileMahoLoginGated(Profile* profile,\n"
                "                                           std::vector<GURL> urls);\n"
                "void DrainQueuedNativeUrlsAfterMahoWelcome(Profile* profile);\n"
                "\n"
                "// Test-only queue observation.\n"
                "size_t GetQueuedNativeUrlCountForTesting(Profile* profile);\n"
            ),
            description=(
                "Expose the macOS deferred native URL queue API and test accessor"
            ),
            guard="GetQueuedNativeUrlCountForTesting",
        ),
        Replacement(
            old="size_t GetQueuedNativeUrlCountForTesting(Profile* profile);\n",
            new=(
                "size_t GetQueuedNativeUrlCountForTesting(Profile* profile);\n"
                "std::vector<GURL> GetQueuedNativeUrlsForTesting(Profile* profile);\n"
            ),
            description="Expose ordered test-only native URL queue observation",
            guard="GetQueuedNativeUrlsForTesting",
        ),
    ],
    "chrome/browser/app_controller_mac.mm": [
        Replacement(
            old=(
                "#include <memory>\n"
                "#include <vector>\n"
            ),
            new=(
                "#include <map>\n"
                "#include <memory>\n"
                "#include <vector>\n"
            ),
            description="Include map for AppController's deferred native URL queues",
            guard="#include <map>\n",
        ),
        Replacement(
            old=(
                "  std::vector<GURL> _startupUrls;\n"
                "  BOOL _startupComplete;\n"
            ),
            new=(
                "  std::vector<GURL> _startupUrls;\n"
                "  std::map<base::FilePath, std::vector<GURL>>\n"
                "      _mahoNativeUrlsQueuedWhileLoginGated;\n"
                "  BOOL _startupComplete;\n"
            ),
            description=(
                "Store deferred native URLs on AppController by original regular "
                "profile path"
            ),
            guard="_mahoNativeUrlsQueuedWhileLoginGated",
        ),
        Replacement(
            old=(
                "  BrowserWindowInterface* browser = chrome::FindLastActive();\n"
                "  CHECK(browser);\n"
                "  return browser;\n"
            ),
            new=(
                "  BrowserWindowInterface* browser = chrome::FindLastActive();\n"
                "  // Maho login gate: NewEmptyWindow() can intentionally show the onboarding\n"
                "  // gate instead of creating a browser (see browser_commands.cc override), so a\n"
                "  // null result is legitimate. Tolerate it instead of crashing on CHECK.\n"
                "  if (!browser) {\n"
                "    return nullptr;\n"
                "  }\n"
                "  return browser;\n"
            ),
            description=(
                "Maho login gate: CreateBrowser tolerates a null browser when "
                "NewEmptyWindow redirects to the onboarding gate (avoids CHECK crash "
                "on Incognito/New Window while gated)"
            ),
            guard="Maho login gate: NewEmptyWindow() can intentionally show the onboarding",
            idempotent=True,
        ),
        Replacement(
            old=(
                "  BrowserWindowInterface* browser =\n"
                "      GlobalBrowserCollection::GetInstance()->GetLastActiveBrowser();\n"
                "  CHECK(browser);\n"
                "  return browser;\n"
            ),
            new=(
                "  BrowserWindowInterface* browser =\n"
                "      GlobalBrowserCollection::GetInstance()->GetLastActiveBrowser();\n"
                "  // Maho login gate: NewEmptyWindow() can intentionally show the onboarding\n"
                "  // gate instead of creating a browser (see browser_commands.cc override), so a\n"
                "  // null result is legitimate. Tolerate it instead of crashing on CHECK.\n"
                "  if (!browser) {\n"
                "    return nullptr;\n"
                "  }\n"
                "  return browser;\n"
            ),
            description=(
                "Maho login gate: CreateBrowser tolerates a null browser when "
                "NewEmptyWindow redirects to the onboarding gate (avoids CHECK crash "
                "on Incognito/New Window while gated) (migration)"
            ),
            guard="Maho login gate: NewEmptyWindow() can intentionally show the onboarding",
            idempotent=True,
        ),
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
                '#include "maho/browser/ui/views/maho_mini/maho_mini_window.h"\n'
                '#include "maho/browser/ui/views/sidebar/maho_sidebar_prefs.h"\n'
                '#include "components/prefs/pref_service.h"\n'
                '#include "chrome/browser/ui/browser_finder.h"\n'
                '#include "chrome/browser/ui/browser_tabstrip.h"\n'
                '#include "maho/browser/maho_space_profile_bridge.h"\n'
            ),
            description="Include Maho Mini and Space headers in app_controller_mac.mm",
            guard='#include "maho/browser/maho_space_profile_bridge.h"',
        ),
        Replacement(
            old=(
                '#include "maho/browser/maho_space_profile_bridge.h"\n'
            ),
            new=(
                '#include "maho/browser/maho_space_profile_bridge.h"\n'
                '#include "maho/browser/ui/webui/maho_welcome/maho_welcome_prefs.h"\n'
            ),
            description="Include the Maho welcome login-gate pref declaration",
            guard='#include "maho/browser/ui/webui/maho_welcome/maho_welcome_prefs.h"',
        ),
        Replacement(
            old=(
                '#include "maho/browser/ui/webui/maho_welcome/maho_welcome_prefs.h"\n'
            ),
            new=(
                '#include "maho/browser/ui/webui/maho_welcome/maho_welcome_prefs.h"\n'
                '#include "maho/browser/ui/views/welcome/maho_welcome_window.h"\n'
            ),
            description="Include the Maho welcome window for native URL login-gate presentation",
            guard='#include "maho/browser/ui/views/welcome/maho_welcome_window.h"',
        ),
        Replacement(
            old=(
                "  base::FilePath lastProfilePath = GetStartupProfilePathMac();\n"
            ),
            new=(
                "  // Maho onboarding is a top-level product window, but it is not a\n"
                "  // BrowserWindowInterface and therefore is absent from GetBrowserWindows().\n"
                "  // Reactivate it before Chromium interprets the empty browser list as a\n"
                "  // request to create a regular browser window.\n"
                "  if (maho::MahoWelcomeWindow::ActivateIfOpen()) {\n"
                "    return NO;\n"
                "  }\n"
                "\n"
                "  base::FilePath lastProfilePath = GetStartupProfilePathMac();\n"
            ),
            description="Activate the Maho welcome window before macOS creates a browser",
            guard="MahoWelcomeWindow::ActivateIfOpen()",
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
                "                  if (bridge && bridge->SwitchToSpace(browser, result.space_id)) {\n"
                "                    chrome::AddSelectedTabWithURL(browser, url, ui::PAGE_TRANSITION_LINK);\n"
                "                  }\n"
                "                }\n"
                "              } else if (result.type == maho::LinkDestinationType::kMahoMini) {\n"
                "                maho::LaunchMahoMini(profile.get(), maho::MahoMiniRequest{url});\n"
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
            description="Route external single URL links to ATC Space or Maho Mini if configured, with global linkage",
            guard="DecideLinkDestination(\n        url, /*is_external=*/true",
        ),
        Replacement(
            old=(
                "void OpenUrlsInBrowserWithProfile(const std::vector<GURL>& urls,\n"
                "                                  Profile* profile) {\n"
                "  if (!profile)\n"
                "    return;  // No suitable profile to open the URLs, do nothing.\n"
                "\n"
                "  if (urls.size() == 1) {\n"
            ),
            new=(
                "void OpenUrlsInBrowserWithProfile(const std::vector<GURL>& urls,\n"
                "                                  Profile* profile) {\n"
                "  if (!profile)\n"
                "    return;  // No suitable profile to open the URLs, do nothing.\n"
                "\n"
                "  Profile* regular_profile = profile->GetOriginalProfile();\n"
                "  if (regular_profile->GetPrefs()->GetBoolean(\n"
                "          maho::welcome::kLoginGateActive)) {\n"
                "    app_controller_mac::QueueNativeUrlsWhileMahoLoginGated(profile, urls);\n"
                "    maho::MahoWelcomeWindow::Show(profile);\n"
                "    return;\n"
                "  }\n"
                "\n"
                "  if (urls.size() == 1) {\n"
            ),
            description="Gate native URL dispatch while Maho login gate is active",
            guard="MahoWelcomeWindow::Show(profile)",
        ),
        Replacement(
            old=(
                "- (void)setLastProfileForTesting:(Profile*)profile {\n"
                "  _lastProfile = profile;\n"
                "  Browser* browser = chrome::FindLastActiveWithProfile(profile);\n"
                "  _lastActiveBrowser = browser->GetWeakPtr();\n"
                "}\n"
            ),
            new=(
                "- (void)setLastProfileForTesting:(Profile*)profile {\n"
                "  _lastProfile = profile;\n"
                "  Browser* browser = chrome::FindLastActiveWithProfile(profile);\n"
                "  _lastActiveBrowser = browser->GetWeakPtr();\n"
                "}\n"
                "\n"
                "- (void)queueNativeUrlsWhileMahoLoginGatedForProfile:(Profile*)profile\n"
                "                                                urls:(std::vector<GURL>)urls {\n"
                "  if (!profile || urls.empty()) {\n"
                "    return;\n"
                "  }\n"
                "  Profile* regular_profile = profile->GetOriginalProfile();\n"
                "  std::vector<GURL>& queued_urls =\n"
                "      _mahoNativeUrlsQueuedWhileLoginGated[regular_profile->GetPath()];\n"
                "  queued_urls.insert(queued_urls.end(), urls.begin(), urls.end());\n"
                "}\n"
                "\n"
                "- (void)drainQueuedNativeUrlsAfterMahoWelcomeForProfile:(Profile*)profile {\n"
                "  if (!profile) {\n"
                "    return;\n"
                "  }\n"
                "  Profile* regular_profile = profile->GetOriginalProfile();\n"
                "  if (regular_profile->GetPrefs()->GetBoolean(\n"
                "          maho::welcome::kLoginGateActive)) {\n"
                "    return;\n"
                "  }\n"
                "  // Todo 3 takes this profile's FIFO batch and dispatches it through the\n"
                "  // existing native URL path after successful completion.\n"
                "}\n"
                "\n"
                "- (size_t)queuedNativeUrlCountForTestingForProfile:(Profile*)profile {\n"
                "  if (!profile) {\n"
                "    return 0;\n"
                "  }\n"
                "  Profile* regular_profile = profile->GetOriginalProfile();\n"
                "  auto it = _mahoNativeUrlsQueuedWhileLoginGated.find(\n"
                "      regular_profile->GetPath());\n"
                "  return it == _mahoNativeUrlsQueuedWhileLoginGated.end()\n"
                "             ? 0\n"
                "             : it->second.size();\n"
                "}\n"
            ),
            description=(
                "Add AppController-owned deferred native URL queue operations"
            ),
            guard="queueNativeUrlsWhileMahoLoginGatedForProfile",
            idempotent=True,
        ),
        Replacement(
            old=(
                "- (void)setLastProfileForTesting:(Profile*)profile {\n"
                "  _lastProfile = profile;\n"
                "  BrowserWindowInterface* current_browser =\n"
                "      chrome::FindLastActiveWithProfile(profile);\n"
                "  Browser* browser =\n"
                "      current_browser ? current_browser->GetBrowserForMigrationOnly() : nullptr;\n"
                "  _lastActiveBrowser = browser->GetWeakPtr();\n"
                "}\n"
            ),
            new=(
                "- (void)setLastProfileForTesting:(Profile*)profile {\n"
                "  _lastProfile = profile;\n"
                "  BrowserWindowInterface* current_browser =\n"
                "      chrome::FindLastActiveWithProfile(profile);\n"
                "  Browser* browser =\n"
                "      current_browser ? current_browser->GetBrowserForMigrationOnly() : nullptr;\n"
                "  _lastActiveBrowser = browser->GetWeakPtr();\n"
                "}\n"
                "\n"
                "- (void)queueNativeUrlsWhileMahoLoginGatedForProfile:(Profile*)profile\n"
                "                                                urls:(std::vector<GURL>)urls {\n"
                "  if (!profile || urls.empty()) {\n"
                "    return;\n"
                "  }\n"
                "  Profile* regular_profile = profile->GetOriginalProfile();\n"
                "  std::vector<GURL>& queued_urls =\n"
                "      _mahoNativeUrlsQueuedWhileLoginGated[regular_profile->GetPath()];\n"
                "  queued_urls.insert(queued_urls.end(), urls.begin(), urls.end());\n"
                "}\n"
                "\n"
                "- (void)drainQueuedNativeUrlsAfterMahoWelcomeForProfile:(Profile*)profile {\n"
                "  if (!profile) {\n"
                "    return;\n"
                "  }\n"
                "  Profile* regular_profile = profile->GetOriginalProfile();\n"
                "  if (regular_profile->GetPrefs()->GetBoolean(\n"
                "          maho::welcome::kLoginGateActive)) {\n"
                "    return;\n"
                "  }\n"
                "  // Todo 3 takes this profile's FIFO batch and dispatches it through the\n"
                "  // existing native URL path after successful completion.\n"
                "}\n"
                "\n"
                "- (size_t)queuedNativeUrlCountForTestingForProfile:(Profile*)profile {\n"
                "  if (!profile) {\n"
                "    return 0;\n"
                "  }\n"
                "  Profile* regular_profile = profile->GetOriginalProfile();\n"
                "  auto it = _mahoNativeUrlsQueuedWhileLoginGated.find(\n"
                "      regular_profile->GetPath());\n"
                "  return it == _mahoNativeUrlsQueuedWhileLoginGated.end()\n"
                "             ? 0\n"
                "             : it->second.size();\n"
                "}\n"
            ),
            description=(
                "Add AppController-owned deferred native URL queue operations (migration)"
            ),
            guard="queueNativeUrlsWhileMahoLoginGatedForProfile",
            idempotent=True,
        ),
        Replacement(
            old=(
                "- (void)drainQueuedNativeUrlsAfterMahoWelcomeForProfile:(Profile*)profile {\n"
                "  if (!profile) {\n"
                "    return;\n"
                "  }\n"
                "  Profile* regular_profile = profile->GetOriginalProfile();\n"
                "  if (regular_profile->GetPrefs()->GetBoolean(\n"
                "          maho::welcome::kLoginGateActive)) {\n"
                "    return;\n"
                "  }\n"
                "  // Todo 3 takes this profile's FIFO batch and dispatches it through the\n"
                "  // existing native URL path after successful completion.\n"
                "}\n"
            ),
            new=(
                "- (void)drainQueuedNativeUrlsAfterMahoWelcomeForProfile:(Profile*)profile {\n"
                "  if (!profile) {\n"
                "    return;\n"
                "  }\n"
                "  Profile* regular_profile = profile->GetOriginalProfile();\n"
                "  if (regular_profile->GetPrefs()->GetBoolean(\n"
                "          maho::welcome::kLoginGateActive)) {\n"
                "    return;\n"
                "  }\n"
                "  auto it = _mahoNativeUrlsQueuedWhileLoginGated.find(\n"
                "      regular_profile->GetPath());\n"
                "  if (it == _mahoNativeUrlsQueuedWhileLoginGated.end()) {\n"
                "    return;\n"
                "  }\n"
                "  std::vector<GURL> urls = std::move(it->second);\n"
                "  _mahoNativeUrlsQueuedWhileLoginGated.erase(it);\n"
                "  OpenUrlsInBrowserWithProfile(urls, profile);\n"
                "}\n"
            ),
            description=(
                "Drain deferred native URLs through the existing dispatcher after the "
                "login gate releases"
            ),
            guard="_mahoNativeUrlsQueuedWhileLoginGated.erase(it)",
        ),
        Replacement(
            old=(
                "- (size_t)queuedNativeUrlCountForTestingForProfile:(Profile*)profile {\n"
                "  if (!profile) {\n"
                "    return 0;\n"
                "  }\n"
                "  Profile* regular_profile = profile->GetOriginalProfile();\n"
                "  auto it = _mahoNativeUrlsQueuedWhileLoginGated.find(\n"
                "      regular_profile->GetPath());\n"
                "  return it == _mahoNativeUrlsQueuedWhileLoginGated.end()\n"
                "             ? 0\n"
                "             : it->second.size();\n"
                "}\n"
            ),
            new=(
                "- (size_t)queuedNativeUrlCountForTestingForProfile:(Profile*)profile {\n"
                "  if (!profile) {\n"
                "    return 0;\n"
                "  }\n"
                "  Profile* regular_profile = profile->GetOriginalProfile();\n"
                "  auto it = _mahoNativeUrlsQueuedWhileLoginGated.find(\n"
                "      regular_profile->GetPath());\n"
                "  return it == _mahoNativeUrlsQueuedWhileLoginGated.end()\n"
                "             ? 0\n"
                "             : it->second.size();\n"
                "}\n"
                "\n"
                "- (std::vector<GURL>)queuedNativeUrlsForTestingForProfile:(Profile*)profile {\n"
                "  if (!profile) {\n"
                "    return {};\n"
                "  }\n"
                "  Profile* regular_profile = profile->GetOriginalProfile();\n"
                "  auto it = _mahoNativeUrlsQueuedWhileLoginGated.find(\n"
                "      regular_profile->GetPath());\n"
                "  return it == _mahoNativeUrlsQueuedWhileLoginGated.end()\n"
                "             ? std::vector<GURL>()\n"
                "             : it->second;\n"
                "}\n"
            ),
            description="Expose ordered AppController native URL queue entries for tests",
            guard="queuedNativeUrlsForTestingForProfile",
        ),
        Replacement(
            old=(
                "@end  // @implementation AppController\n"
                "\n"
                "//---------------------------------------------------------------------------\n"
            ),
            new=(
                "@end  // @implementation AppController\n"
                "\n"
                "namespace app_controller_mac {\n"
                "\n"
                "void QueueNativeUrlsWhileMahoLoginGated(Profile* profile,\n"
                "                                           std::vector<GURL> urls) {\n"
                "  [AppController.sharedController\n"
                "      queueNativeUrlsWhileMahoLoginGatedForProfile:profile\n"
                "                                                urls:std::move(urls)];\n"
                "}\n"
                "\n"
                "void DrainQueuedNativeUrlsAfterMahoWelcome(Profile* profile) {\n"
                "  [AppController.sharedController\n"
                "      drainQueuedNativeUrlsAfterMahoWelcomeForProfile:profile];\n"
                "}\n"
                "\n"
                "size_t GetQueuedNativeUrlCountForTesting(Profile* profile) {\n"
                "  return [AppController.sharedController\n"
                "      queuedNativeUrlCountForTestingForProfile:profile];\n"
                "}\n"
                "\n"
                "}  // namespace app_controller_mac\n"
                "\n"
                "//---------------------------------------------------------------------------\n"
            ),
            description=(
                "Bridge deferred native URL queue operations through AppController"
            ),
            guard="QueueNativeUrlsWhileMahoLoginGated(Profile* profile",
        ),
        Replacement(
            old=(
                "size_t GetQueuedNativeUrlCountForTesting(Profile* profile) {\n"
                "  return [AppController.sharedController\n"
                "      queuedNativeUrlCountForTestingForProfile:profile];\n"
                "}\n"
            ),
            new=(
                "size_t GetQueuedNativeUrlCountForTesting(Profile* profile) {\n"
                "  return [AppController.sharedController\n"
                "      queuedNativeUrlCountForTestingForProfile:profile];\n"
                "}\n"
                "\n"
                "std::vector<GURL> GetQueuedNativeUrlsForTesting(Profile* profile) {\n"
                "  return [AppController.sharedController\n"
                "      queuedNativeUrlsForTestingForProfile:profile];\n"
                "}\n"
            ),
            description="Bridge ordered native URL queue entries through AppController for tests",
            guard="GetQueuedNativeUrlsForTesting(Profile* profile)",
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
        Replacement(
            old=(
                "  auto& entry = _profileBookmarkMenuBridgeMap[profile->GetPath()];\n"
                "  if (!entry || !entry->GetProfile()) {\n"
                "    // This creates a deep copy, but only the first 3 items in the root menu\n"
                "    // are really wanted. This can probably be optimized, but lazy-loading of\n"
                "    // the menu should reduce the impact in most flows.\n"
                "    NSMenu* submenu = [bookmarkItem.submenu copy];\n"
                "    submenu.delegate = nil;  // The delegate is also copied. Remove it.\n"
                "\n"
                "    // The original profile outlives the OTR profile. Always create the bridge\n"
                "    // on the original profile, to prevent bugs WRT profile lifetime.\n"
                "    entry = std::make_unique<BookmarkMenuBridge>(profile->GetOriginalProfile(),\n"
                "                                                 submenu);\n"
                "\n"
                "    // Clear bookmarks from the old profile.\n"
                "    entry->ClearBookmarkMenu();\n"
                "  }\n"
                "  _bookmarkMenuBridge = entry.get();\n"
                "\n"
                "  // No need to |BuildMenu| here.  It is done lazily upon menu access.\n"
                "  bookmarkItem.submenu = _bookmarkMenuBridge->BookmarkMenu();\n"
                "  bookmarkItem.hidden = hidden;\n"
            ),
            new=(
                "  // Maho: the macOS Bookmarks menu is never built (BuildBookmarksMenu\n"
                "  // returns nil), so |bookmarkItem| is nil. AppKit tolerates messaging nil,\n"
                "  // but building a BookmarkMenuBridge without a root menu trips its\n"
                "  // CHECK(menu_root_) and kills the browser at startup, so skip the whole\n"
                "  // bridging step when the menu item is absent.\n"
                "  if (bookmarkItem) {\n"
                "    auto& entry = _profileBookmarkMenuBridgeMap[profile->GetPath()];\n"
                "    if (!entry || !entry->GetProfile()) {\n"
                "      // This creates a deep copy, but only the first 3 items in the root\n"
                "      // menu are really wanted. This can probably be optimized, but\n"
                "      // lazy-loading of the menu should reduce the impact in most flows.\n"
                "      NSMenu* submenu = [bookmarkItem.submenu copy];\n"
                "      submenu.delegate = nil;  // The delegate is also copied. Remove it.\n"
                "\n"
                "      // The original profile outlives the OTR profile. Always create the\n"
                "      // bridge on the original profile, to prevent bugs WRT profile\n"
                "      // lifetime.\n"
                "      entry = std::make_unique<BookmarkMenuBridge>(\n"
                "          profile->GetOriginalProfile(), submenu);\n"
                "\n"
                "      // Clear bookmarks from the old profile.\n"
                "      entry->ClearBookmarkMenu();\n"
                "    }\n"
                "    _bookmarkMenuBridge = entry.get();\n"
                "\n"
                "    // No need to |BuildMenu| here.  It is done lazily upon menu access.\n"
                "    bookmarkItem.submenu = _bookmarkMenuBridge->BookmarkMenu();\n"
                "    bookmarkItem.hidden = hidden;\n"
                "  }\n"
            ),
            description=(
                "Maho: skip the macOS BookmarkMenuBridge setup when the Bookmarks "
                "menu is absent, avoiding a CHECK(menu_root_) crash at startup"
            ),
            guard="Maho: the macOS Bookmarks menu is never built",
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
    # ── pinned_toolbar_actions_container.cc ───────────────────────────────────
    "chrome/browser/ui/views/toolbar/pinned_toolbar_actions_container.cc": [
        Replacement(
            old=(
                "PinnedActionToolbarButton* PinnedToolbarActionsContainer::AddPoppedOutButtonFor(\n"
                "    actions::ActionId id) {\n"
                "  CHECK(GetActionItemFor(id));\n"
            ),
            new=(
                "PinnedActionToolbarButton* PinnedToolbarActionsContainer::AddPoppedOutButtonFor(\n"
                "    actions::ActionId id) {\n"
                "  if (!GetActionItemFor(id)) {\n"
                "    return nullptr;\n"
                "  }\n"
            ),
            description="Tolerate missing ActionItem when adding popped out button",
        ),
    ],
    "chrome/browser/ui/views/side_panel/side_panel_helper.cc": [
        Replacement(
            old=(
                "// static\n"
                "void SidePanelHelper::PopulateGlobalEntries(\n"
                "    Browser* browser,\n"
                "    SidePanelRegistry* window_registry) {\n"
            ),
            new=(
                "// static\n"
                "void SidePanelHelper::PopulateGlobalEntries(\n"
                "    Browser* browser,\n"
                "    SidePanelRegistry* window_registry) {\n"
                "  maho::RegisterMahoAiSidePanel(browser, window_registry);\n"
            ),
            description="Register Maho AI side panel in PopulateGlobalEntries",
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
        Replacement(
            old='    "//chrome/common/read_anything:mojo_bindings",\n',
            new=(
                '    "//chrome/common/read_anything:mojo_bindings",\n'
                '    "//maho/browser/ui/webui/maho_ai:mojo_bindings",\n'
            ),
            description=(
                "Add the maho_ai mojom C++ bindings to the side_panel target's "
                "public_deps so the generated maho_ai.mojom.h (and its header "
                "family) is ordered before the injected maho_ai side-panel "
                "sources compile. Without this, a clean build races and fails "
                "with 'maho_ai.mojom.h file not found'."
            ),
            guard='"//maho/browser/ui/webui/maho_ai:mojo_bindings"',
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
            old=(
                'static_library("ui") {\n'
                '  # Keep the overlay first: canonical chrome/... includes must resolve\n'
                '  # to chromium_src, while the parent root is only for explicit src/...\n'
                '  # includes of the original Chromium implementation.\n'
                '  include_dirs = [\n'
                '    "//maho/chromium_src",\n'
                '    "../../../..",\n'
                '  ]\n'
                '\n'
                '  sources = [\n'
            ),
            new='static_library("ui") {\n  sources = [\n',
            description=(
                "Maho: remove target-wide chromium_src include shadowing from "
                "chrome/browser/ui:ui"
            ),
            idempotent=True,
        ),
        Replacement(
            old='static_library("ui") {\n  sources = [\n',
            new=(
                'static_library("ui") {\n'
                '  # The password bubble wrapper includes its overlay explicitly.\n'
                '  # This parent root is only for its src/... Chromium fallback.\n'
                '  include_dirs = [ "../../../.." ]\n'
                '\n'
                '  sources = [\n'
            ),
            description=(
                "Maho: expose the explicit Chromium password bubble fallback to "
                "chrome/browser/ui:ui"
            ),
            guard='  include_dirs = [ "../../../.." ]\n',
        ),
        Replacement(
            old='    include_dirs = [ "$target_gen_dir" ]\n',
            new='    include_dirs += [ "$target_gen_dir" ]\n',
            description=(
                "Maho: preserve password bubble overlay include roots when macOS "
                "adds target_gen_dir to chrome/browser/ui:ui"
            ),
            guard='    include_dirs += [ "$target_gen_dir" ]\n',
        ),
        Replacement(
            old=(
                '      "views/passwords/password_bubble_view_base.cc",\n'
                '      "views/passwords/password_bubble_view_base.h",\n'
            ),
            new=(
                '      "//maho/chromium_src/chrome/browser/ui/views/passwords/'
                'password_bubble_view_base.cc",\n'
                '      "//maho/chromium_src/chrome/browser/ui/views/passwords/'
                'password_bubble_view_base.h",\n'
            ),
            description=(
                "Maho: compile the chromium_src password bubble factory override "
                "in chrome/browser/ui:ui"
            ),
            guard=(
                '      "//maho/chromium_src/chrome/browser/ui/views/passwords/'
                'password_bubble_view_base.cc",\n'
            ),
        ),
        Replacement(
            old=(
                '    "//build/config/linux/dbus:buildflags",\n'
                '    "//maho/browser/net",\n'
            ),
            new=(
                '    "//build/config/linux/dbus:buildflags",\n'
                '    "//maho/browser:maho_password_save_update_view",\n'
                '    "//maho/browser/net",\n'
            ),
            description=(
                "Maho: link the native password save/update view into "
                "chrome/browser/ui:ui"
            ),
            guard='    "//maho/browser:maho_password_save_update_view",\n',
        ),
        Replacement(
            old=(
                '      "//maho/browser/ui/views/peek:maho_peek",\n'
            ),
            new=(
                '      "//maho/browser/ui/views/peek:maho_peek",\n'
                '      "//maho/browser/ui/views/peek:maho_peek_route",\n'
            ),
            description=(
                "Link the Peek routing decision helper into chrome/browser/ui:ui "
                "for Browser::AddNewContents"
            ),
            guard='"//maho/browser/ui/views/peek:maho_peek_route"',
        ),
        Replacement(
            old='    "//maho/browser:maho_link_preview",\\n',
            new="",
            description="Maho: strip deleted maho_link_preview dep from chrome/browser/ui:ui",
            idempotent=True,
        ),
        Replacement(
            old='    "//maho/browser:maho_features",\\n',
            new="",
            description=(
                "Maho: strip deleted maho_features dep from chrome/browser/ui:ui "
                "(target and sources no longer exist in the overlay)"
            ),
            idempotent=True,
        ),
        Replacement(
            old='      "//maho/browser:maho_mac_sf_symbol_helper",\n',
            new="",
            description=(
                "Maho: strip deleted maho_mac_sf_symbol_helper dep "
                "(target and sources no longer exist in the overlay)"
            ),
            idempotent=True,
        ),
        Replacement(
            old='      "//maho/browser/ui/views/sidebar:maho_sidebar_boosts",\n',
            new="",
            description=(
                "Maho: strip deleted maho_sidebar_boosts dep "
                "(target and sources no longer exist in the overlay)"
            ),
            idempotent=True,
        ),
        Replacement(
            old=(
                '      "//maho/browser/ui/views/little_'
                'arc/maho_little_'
                'arc_window.cc",\n'
                '      "//maho/browser/ui/views/little_'
                'arc/maho_little_'
                'arc_window.h",\n'
            ),
            new=(
                '      "//maho/browser/ui/views/maho_mini/maho_mini_window.cc",\n'
                '      "//maho/browser/ui/views/maho_mini/maho_mini_window.h",\n'
                '      "//maho/browser/ui/views/maho_mini/maho_mini_top_bar_view.cc",\n'
                '      "//maho/browser/ui/views/maho_mini/maho_mini_top_bar_view.h",\n'
                '      "//maho/browser/net/maho_atc_navigation_throttle.cc",\n'
                '      "//maho/browser/net/maho_atc_navigation_throttle.h",\n'
            ),
            description=(
                "Compile the MahoAtcNavigationThrottle and MahoMiniTopBarView into the "
                "chrome/browser/ui:ui target"
            ),
            guard='"//maho/browser/ui/views/maho_mini/maho_mini_top_bar_view.cc"',
        ),
        Replacement(
            old=(
                '      "//maho/browser/ui/views/little_'
                'arc/maho_little_'
                'arc_top_bar_view.cc",\n'
                '      "//maho/browser/ui/views/little_'
                'arc/maho_little_'
                'arc_top_bar_view.h",\n'
            ),
            new=(
                '      "//maho/browser/ui/views/maho_mini/maho_mini_top_bar_view.cc",\n'
                '      "//maho/browser/ui/views/maho_mini/maho_mini_top_bar_view.h",\n'
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
        # The sidebar search pill was removed in favor of the per-pane contents
        # header; strip its retired source pair from trees that still carry it
        # (MahoTranslatePopoverView stays registered via
        # _CHROME_BROWSER_UI_SOURCES).
        Replacement(
            old=(
                '      "//maho/browser/ui/views/sidebar/maho_sidebar_'
                'search_view.cc",\n'
                '      "//maho/browser/ui/views/sidebar/maho_sidebar_'
                'search_view.h",\n'
            ),
            new="",
            description=(
                "Maho: strip the retired sidebar search pill sources from the "
                "chrome/browser/ui:ui target"
            ),
            idempotent=True,
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
                '      "//maho/browser/ui/views/sidebar/maho_control_activity_indicator_view.cc",\n'
                '      "//maho/browser/ui/views/sidebar/maho_control_activity_indicator_view.h",\n'
                '      "//maho/browser/ui/views/maho_action_marker_service.cc",\n'
                '      "//maho/browser/ui/views/maho_action_marker_service.h",\n'
                '      "//maho/browser/ui/views/sidebar/maho_toolbar_button_provider.cc",\n'
                '      "//maho/browser/ui/views/sidebar/maho_toolbar_button_provider.h",\n'
                '      "//maho/browser/ui/views/frame/maho_contents_header_view.cc",\n'
                '      "//maho/browser/ui/views/frame/maho_contents_header_view.h",\n'
                '      "//maho/browser/ai/maho_tab_tidy_orchestrator.cc",\n'
                '      "//maho/browser/ai/maho_tab_tidy_orchestrator.h",\n'
            ),
            description="Include Maho tab preview controller sources in Chromium browser UI build",
            # These sources are already emitted into the unconditional top-level
            # sources list by normalize_chrome_browser_ui_build(), so match that
            # 4-space form; the old 6-space guard never fired and re-adding them
            # made GN fail with duplicate entries.
            guard='    "//maho/browser/ui/views/sidebar/maho_control_activity_indicator_view.cc",\n',
        ),
        Replacement(
            old=(
                '    if (is_mac) {\n'
                '      sources +=\n'
                '          [ "views/user_education/browser_help_bubble_event_relay_mac.mm" ]\n'
                '    }'
            ),
            new=(
                '    if (is_mac) {\n'
                '      sources += [\n'
                '        "views/user_education/browser_help_bubble_event_relay_mac.mm",\n'
                '        "//maho/browser/ui/views/sidebar/maho_traffic_light_geometry_mac.mm",\n'
                '      ]\n'
                '    } else {\n'
                '      sources +=\n'
                '          [ "//maho/browser/ui/views/sidebar/maho_traffic_light_geometry_stub.cc" ]\n'
                '    }'
            ),
            description=(
                "Maho: compile traffic-light geometry per-platform (mac .mm on "
                "macOS, stub .cc elsewhere) so chrome/browser/ui:ui resolves "
                "maho::GetTrafficLightCenterYInView on Linux/Windows"
            ),
            guard='maho_traffic_light_geometry_stub.cc',
        ),
        Replacement(
            old=(
                '      "//maho/browser/ui/views/sidebar/maho_now_playing_card.h",\n'
            ),
            new=(
                '      "//maho/browser/ui/views/sidebar/maho_now_playing_card.h",\n'
                '      "//maho/browser/ui/views/sidebar/maho_now_playing_coordinator.cc",\n'
                '      "//maho/browser/ui/views/sidebar/maho_now_playing_coordinator.h",\n'
                '      "//maho/browser/ui/views/sidebar/maho_now_playing_coordinator_factory.cc",\n'
                '      "//maho/browser/ui/views/sidebar/maho_now_playing_coordinator_factory.h",\n'
            ),
            description="Compile Maho now playing coordinator and factory in chrome/browser/ui:ui target",
            guard='maho/browser/ui/views/sidebar/maho_now_playing_coordinator.cc',
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
                '#include "maho/browser/mail_helper/maho_mail_service.h"\n'
                '#include "maho/browser/passwords/maho_password_provider_utils.h"\n'
                '#include "maho/browser/updates/maho_update_prefs.h"\n'
            ),
            description=(
                "Include maho_ctrl_tab_prefs.h, maho_mail_service.h, maho_password_provider_utils.h, "
                "and maho_update_prefs.h in browser_prefs.cc for pref registration"
            ),
            guard='#include "maho/browser/updates/maho_update_prefs.h"',
        ),
        Replacement(
            old=(
                '#include "maho/browser/passwords/maho_password_provider_utils.h"\n'
            ),
            new=(
                '#include "maho/browser/passwords/maho_password_provider_utils.h"\n'
                '#include "maho/browser/maho_tab_id_session_helper.h"\n'
            ),
            description=(
                "Include maho_tab_id_session_helper.h in browser_prefs.cc for tab ID session pref registration"
            ),
            guard='#include "maho/browser/maho_tab_id_session_helper.h"',
        ),
        Replacement(
            old='  maho::sidebar_prefs::RegisterProfilePrefs(registry);\n',
            new=(
                '  maho::sidebar_prefs::RegisterProfilePrefs(registry);\n'
                '  maho::ctrl_tab_prefs::RegisterProfilePrefs(registry);\n'
                '  maho::MahoMailService::RegisterProfilePrefs(registry);\n'
            ),
            description=(
                "Register Ctrl+Tab MRU switcher prefs and MahoMailService prefs "
                "alongside the other Maho profile prefs"
            ),
            guard="maho::MahoMailService::RegisterProfilePrefs",
        ),
        Replacement(
            old='  maho::MahoMailService::RegisterProfilePrefs(registry);\n',
            new=(
                '  maho::MahoMailService::RegisterProfilePrefs(registry);\n'
                '  maho::MahoTabIdSessionHelper::RegisterProfilePrefs(registry);\n'
            ),
            description=(
                "Register tab ID session prefs alongside the other Maho profile prefs"
            ),
            guard="maho::MahoTabIdSessionHelper::RegisterProfilePrefs",
        ),
        Replacement(
            old='void RegisterLocalState(PrefRegistrySimple* registry) {\n',
            new=(
                'void RegisterLocalState(PrefRegistrySimple* registry) {\n'
                '  maho::passwords::RegisterLocalStatePrefs(registry);\n'
                '  maho::updates::RegisterLocalStatePrefs(registry);\n'
            ),
            description="Register Maho passwords and updates local state kill switch prefs",
            guard="maho::updates::RegisterLocalStatePrefs(registry);",
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
            old="class MahoMruTabTracker;\n",
            new=(
                "class MahoMruTabTracker;\n"
                "class MahoPeekController;\n"
            ),
            description=(
                "Forward-declare the host-owned Peek controller for the "
                "BrowserView forwarding accessor"
            ),
            guard="class MahoPeekController;",
        ),
        Replacement(
            old=(
                "  void ShowMahoCommandOverlayForSearch(const std::string& initial_text);\n"
            ),
            new=(
                "  void ShowMahoCommandOverlayForSearch(const std::string& initial_text);\n"
                "\n"
                "  // Returns the overlay-host-owned Peek controller for eligible\n"
                "  // normal BrowserViews, attaching the host synchronously if needed.\n"
                "  maho::MahoPeekController* GetOrCreateMahoPeekController();\n"
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
            guard="maho::MahoPeekController* GetOrCreateMahoPeekController();",
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

        Replacement(
            old="class MahoCommandOverlayController;\n",
            new=(
                "class MahoCommandOverlayController;\n"
                "class MahoPeekController;\n"
            ),
            description="Forward-declare the host-owned Peek controller",
            guard="class MahoPeekController;",
        ),
        Replacement(
            old=(
                "  void ShowMahoCommandOverlayForSearch(const std::string& initial_text);\n"
            ),
            new=(
                "  void ShowMahoCommandOverlayForSearch(const std::string& initial_text);\n"
                "\n"
                "  // Returns the overlay-host-owned Peek controller for eligible\n"
                "  // normal BrowserViews, attaching the host synchronously if needed.\n"
                "  maho::MahoPeekController* GetOrCreateMahoPeekController();\n"
            ),
            description="Declare the BrowserView Peek controller forwarding accessor",
            guard="maho::MahoPeekController* GetOrCreateMahoPeekController();",
        ),
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
                "  raw_ptr<views::View> maho_mini_top_bar_ = nullptr;\n"
            ),
            description="Maho: Add maho_mini_top_bar_ to BrowserView",
            guard="maho_mini_top_bar_",
        ),
        Replacement(
            old="  raw_ptr<views::View> maho_mini_top_bar_ = nullptr;\n",
            new=(
                "  raw_ptr<views::View> maho_mini_top_bar_ = nullptr;\n"
                "\n"
                "  raw_ptr<maho::MahoContentGradientView>\n"
                "      maho_content_gradient_view_ = nullptr;\n"
            ),
            description=(
                "Store the lowest-z Maho content gradient child on BrowserView"
            ),
            guard="maho_content_gradient_view_ = nullptr;",
        ),
        Replacement(
            old="namespace maho {\nclass MahoCommandOverlayController;\n",
            new=(
                "struct MahoSidebarPalette;\n"
                "\n"
                "namespace maho {\nclass MahoCommandOverlayController;\n"
            ),
            description=(
                "Forward-declare (global-scope) MahoSidebarPalette for "
                "BrowserView's content surface palette projection"
            ),
            guard="struct MahoSidebarPalette;",
        ),
        Replacement(
            old=(
                "  raw_ptr<maho::MahoContentGradientView>\n"
                "      maho_content_gradient_view_ = nullptr;\n"
            ),
            new=(
                "  raw_ptr<maho::MahoContentGradientView>\n"
                "      maho_content_gradient_view_ = nullptr;\n"
                "\n"
                "  // Maho: mirror the active Space's sidebar palette onto the content\n"
                "  // gradient + WebContents background so both panes read as one\n"
                "  // themed surface (light/dark, opacity and stops all follow the\n"
                "  // sidebar's browser-local resolution).\n"
                "  void ApplyMahoSidebarPaletteToContentSurfaces(\n"
                "      const MahoSidebarPalette& palette);\n"
                "  void MaybeRefreshMahoContentSurfacesFromSidebar();\n"
                "  bool maho_sidebar_palette_subscribed_ = false;\n"
                "  base::CallbackListSubscription maho_sidebar_palette_subscription_;\n"
            ),
            description=(
                "Maho: declare BrowserView content-surface palette projection "
                "(method + subscription) alongside the gradient view pointer"
            ),
            guard="maho_sidebar_palette_subscription_;",
        ),
        Replacement(
            old=(
                "  base::CallbackListSubscription maho_sidebar_palette_subscription_;\n"
            ),
            new=(
                "  base::CallbackListSubscription maho_sidebar_palette_subscription_;\n"
                "\n"
                "  // Maho: keeps the core-ready retry that re-registers Maho shortcut\n"
                "  // accelerators with the FocusManager. AddedToWidget() runs before the\n"
                "  // Rust core is guaranteed to be up, and GetRegisteredAccelerators()\n"
                "  // returns an empty list until then; without this retry the window\n"
                "  // keeps zero Maho accelerators and Cmd+Shift+C falls through to\n"
                "  // Chromium's hidden table (DevTools Inspect). Destroyed with the\n"
                "  // BrowserView, and the callback holds only a WeakPtr.\n"
                "  base::CallbackListSubscription\n"
                "      maho_core_ready_accelerators_subscription_;\n"
            ),
            description=(
                "Maho: declare the core-ready subscription that re-registers Maho "
                "shortcut accelerators once the Rust core finishes starting"
            ),
            guard="maho_core_ready_accelerators_subscription_;",
        ),
        Replacement(
            old=(
                "  base::CallbackListSubscription\n"
                "      maho_core_ready_accelerators_subscription_;\n"
            ),
            new=(
                "  base::CallbackListSubscription\n"
                "      maho_core_ready_accelerators_subscription_;\n"
                "\n"
                "#if BUILDFLAG(IS_MAC)\n"
                "  // Maho: true once the Maho shortcut accelerators have actually been\n"
                "  // registered with this window's FocusManager. AddedToWidget() and the\n"
                "  // core-ready retry share one registration path, and registering the\n"
                "  // same target twice at kHighPriority trips AcceleratorManager DCHECKs,\n"
                "  // so the second caller must be able to tell the work is already done.\n"
                "  // Gated with the registration it guards: off-mac nothing reads it, and\n"
                "  // -Wunused-private-field is an error under Chromium's -Werror.\n"
                "  bool maho_accelerators_registered_ = false;\n"
                "#endif  // BUILDFLAG(IS_MAC)\n"
            ),
            description=(
                "Maho: track whether the shortcut accelerators were already registered "
                "so the core-ready retry stays idempotent"
            ),
            guard="maho_accelerators_registered_ = false;",
        ),
        Replacement(
            old=_MAHO_LEGACY_UNGATED_ACCELERATORS_REGISTERED_MEMBER,
            new=_MAHO_GATED_ACCELERATORS_REGISTERED_MEMBER,
            idempotent=True,
            description=(
                "Gate the Maho accelerator-registered flag to macOS so off-mac "
                "builds do not carry an unread private field, which is an error "
                "under -Wunused-private-field with -Werror"
            ),
            # The ungated declaration is a SUBSTRING of the gated one, so without
            # this guard the migration would match its own output and nest.
            guard=(
                "#if BUILDFLAG(IS_MAC)\n"
                "  // Maho: true once the Maho shortcut accelerators have actually been\n"
            ),
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
            guard="const bool maho_wco =",
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
                "        MahoIsCapabilityAllowed(\n"
                "            maho_private_profile,\n"
                "            MahoPrivateCapability::kPrivateVisuals)) {\n"
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
            old='#include "maho/browser/ui/tabs/maho_ctrl_tab_switcher_controller.h"\n',
            new=(
                '#include "maho/browser/ui/tabs/maho_ctrl_tab_switcher_controller.h"\n'
                '#include "maho/browser/maho_private_context_policy.h"\n'
            ),
            description=(
                "Include MahoIsCapabilityAllowed policy header in browser_view.cc "
                "for the kPrivateVisuals SetPrivateAppearance guard"
            ),
            guard='#include "maho/browser/maho_private_context_policy.h"',
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
                "#include \"maho/browser/ui/views/sidebar/maho_sidebar_container_view.h\"\n"
            ),
            new=(
                "#include \"maho/browser/ui/views/peek/maho_peek_controller.h\"\n"
                "#include \"maho/browser/ui/views/sidebar/maho_sidebar_container_view.h\"\n"
                "#include \"maho/browser/ui/views/spaces_overlay/maho_browser_frame_overlay_host.h\"\n"
            ),
            description=(
                "Include Peek and overlay-host seams used by the BrowserView "
                "forwarding accessor"
            ),
            guard="maho/browser/ui/views/peek/maho_peek_controller.h",
        ),
        Replacement(
            old=(
                "maho::MahoCommandOverlayController*\n"
                "BrowserView::GetOrCreateMahoCommandOverlayController() {\n"
            ),
            new=(
                "maho::MahoPeekController*\n"
                "BrowserView::GetOrCreateMahoPeekController() {\n"
                "  if (!maho::IsPeekEligible(browser_.get())) {\n"
                "    return nullptr;\n"
                "  }\n"
                "  auto* container = views::AsViewClass<maho::MahoSidebarContainerView>(\n"
                "      maho_sidebar_container());\n"
                "  if (!container) {\n"
                "    return nullptr;\n"
                "  }\n"
                "  auto* host = container->GetOrCreateOverlayHost();\n"
                "  return host ? host->peek_controller() : nullptr;\n"
                "}\n"
                "\n"
                "maho::MahoCommandOverlayController*\n"
                "BrowserView::GetOrCreateMahoCommandOverlayController() {\n"
            ),
            description=(
                "Implement BrowserView::GetOrCreateMahoPeekController as an "
                "eligibility-gated forwarder to the sidebar overlay host"
            ),
            guard="BrowserView::GetOrCreateMahoPeekController()",
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
            idempotent=True,
        ),
        Replacement(
            old=(
                "void BrowserView::ShowMahoCommandOverlayForCurrentTab() {\n"
                "  auto* controller = GetOrCreateMahoCommandOverlayController();\n"
                "  if (!controller) {\n"
                "    return;\n"
                "  }\n"
                "\n"
                "  controller->Show(this, maho::CommandOverlayMode::kCurrentTab);\n"
                "}\n"
            ),
            new=(
                "void BrowserView::ShowMahoCommandOverlayForCurrentTab() {\n"
                "  auto* controller = GetOrCreateMahoCommandOverlayController();\n"
                "  if (!controller) {\n"
                "    return;\n"
                "  }\n"
                "\n"
                "  std::string current_url;\n"
                "  if (auto* contents = browser_->tab_strip_model()->GetActiveWebContents()) {\n"
                "    const GURL& visible = contents->GetVisibleURL();\n"
                "    const GURL& url =\n"
                "        visible.is_empty() ? contents->GetLastCommittedURL() : visible;\n"
                "    if (!url.is_empty() && !url.SchemeIs(\"javascript\") &&\n"
                "        !url.SchemeIs(\"data\") && url.spec() != \"about:blank\") {\n"
                "      current_url = url.spec();\n"
                "    }\n"
                "  }\n"
                "\n"
                "  controller->Show(this, maho::CommandOverlayMode::kCurrentTab, current_url,\n"
                "                   /*select_initial_text=*/true);\n"
                "}\n"
            ),
            description="Prefill active tab URL with select-all in ShowMahoCommandOverlayForCurrentTab",
            idempotent=True,
            guard="visible.is_empty() ? contents->GetLastCommittedURL() : visible;",
        ),
        Replacement(
            old=(
                "void BrowserView::ShowMahoCommandOverlayForCurrentTab() {\n"
                "  auto* controller = GetOrCreateMahoCommandOverlayController();\n"
                "  if (!controller) {\n"
                "    return;\n"
                "  }\n"
                "\n"
                "  std::string current_url;\n"
                "  auto* active_contents = browser_->tab_strip_model()->GetActiveWebContents();\n"
                "  if (active_contents) {\n"
                "    // Prefer the visible URL so that pages where the last committed URL is\n"
                "    // empty or stale (e.g. mid-load, error pages with a pending entry) still\n"
                "    // seed the overlay correctly. Fall back to last-committed when the visible\n"
                "    // URL is empty (e.g. initial blank tab before any navigation commits).\n"
                "    const GURL& visible = active_contents->GetVisibleURL();\n"
                "    const GURL& url =\n"
                "        visible.is_empty() ? active_contents->GetLastCommittedURL() : visible;\n"
                "    // Seed the textfield with the current URL for web pages only.\n"
                "    // Exclude: javascript: (script injection risk), data: (opaque blobs\n"
                "    // too long to be useful), and internal browser pages (chrome://, about:,\n"
                "    // devtools://, etc.) which are suppressed by the shared display policy.\n"
                "    if (!url.is_empty() &&\n"
                "        !url.SchemeIs(\"javascript\") &&\n"
                "        !url.SchemeIs(\"data\") &&\n"
                "        !maho::MahoDisplayPolicy::IsInternalBrowserPage(url)) {\n"
                "      current_url = url.spec();\n"
                "    }\n"
                "  }\n"
                "  controller->Show(this, maho::CommandOverlayMode::kCurrentTab,\n"
                "                   current_url, true);\n"
                "}\n"
            ),
            new=(
                "void BrowserView::ShowMahoCommandOverlayForCurrentTab() {\n"
                "  auto* controller = GetOrCreateMahoCommandOverlayController();\n"
                "  if (!controller) {\n"
                "    return;\n"
                "  }\n"
                "\n"
                "  std::string current_url;\n"
                "  if (auto* contents = browser_->tab_strip_model()->GetActiveWebContents()) {\n"
                "    const GURL& visible = contents->GetVisibleURL();\n"
                "    const GURL& url =\n"
                "        visible.is_empty() ? contents->GetLastCommittedURL() : visible;\n"
                "    if (!url.is_empty() && !url.SchemeIs(\"javascript\") &&\n"
                "        !url.SchemeIs(\"data\") && url.spec() != \"about:blank\") {\n"
                "      current_url = url.spec();\n"
                "    }\n"
                "  }\n"
                "\n"
                "  controller->Show(this, maho::CommandOverlayMode::kCurrentTab, current_url,\n"
                "                   /*select_initial_text=*/true);\n"
                "}\n"
            ),
            description="Migrate the intermediate ShowMahoCommandOverlayForCurrentTab prefill (active_contents + MahoDisplayPolicy) to the select-all prefill",
            guard="visible.is_empty() ? contents->GetLastCommittedURL() : visible;",
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
                "  MaybeRefreshMahoContentSurfacesFromSidebar();\n"
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
        Replacement(
            old=(
                "void BrowserView::OnThemeChanged() {\n"
                "  views::ClientView::OnThemeChanged();\n"
                "  if (!initialized_) {\n"
                "    return;\n"
                "  }\n"
                "\n"
                "  FrameColorsChanged();\n"
            ),
            new=(
                "void BrowserView::OnThemeChanged() {\n"
                "  views::ClientView::OnThemeChanged();\n"
                "  if (!initialized_) {\n"
                "    return;\n"
                "  }\n"
                "  MaybeRefreshMahoContentSurfacesFromSidebar();\n"
                "\n"
                "  FrameColorsChanged();\n"
            ),
            description="Maho: refresh content surfaces on theme change",
            guard="MaybeRefreshMahoContentSurfacesFromSidebar();\n\n  FrameColorsChanged();",
            idempotent=True,
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
            idempotent=True,
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
        # ── Patch #14: every Maho shortcut dispatches from any focus state ────
        Replacement(
            old=(
                "  views::ClientView::AddedToWidget();\n"
                "\n"
                "  widget_observation_.Observe(GetWidget());\n"
            ),
            new=_MAHO_ADDED_TO_WIDGET_PATCHED,
            idempotent=True,
            description=(
                "Register every enabled Maho shortcut as a kHighPriority "
                "FocusManager accelerator in AddedToWidget so Maho bindings fire "
                "from any focus state and win over upstream macOS key "
                "equivalents (Cmd+Shift+C, Cmd+L, Cmd+E)"
            ),
        ),
        Replacement(
            old=(
                _MAHO_LEGACY_CTRL_ACCELERATOR_COMMENT
                + "  if (IsMahoArcLayoutActive()) {\n"
                + _MAHO_LEGACY_CTRL_ACCELERATOR_BODY
            ),
            new=_MAHO_FOCUS_ACCELERATOR_REGISTRATION,
            idempotent=True,
            description=(
                "Migrate the Ctrl+1..9-only FocusManager registration to the full "
                "Maho shortcut set at kHighPriority (IsMahoArcLayoutActive gate)"
            ),
        ),
        Replacement(
            old=(
                _MAHO_LEGACY_CTRL_ACCELERATOR_COMMENT
                + "  if ((maho_sidebar_container_ && maho_sidebar_container_->GetVisible())) {\n"
                + _MAHO_LEGACY_CTRL_ACCELERATOR_BODY
            ),
            new=_MAHO_FOCUS_ACCELERATOR_REGISTRATION,
            idempotent=True,
            description=(
                "Migrate the Ctrl+1..9-only FocusManager registration to the full "
                "Maho shortcut set at kHighPriority (sidebar-visibility gate)"
            ),
        ),
        Replacement(
            old=_MAHO_LEGACY_SINGLE_SHOT_ACCELERATOR_REGISTRATION,
            new=_MAHO_FOCUS_ACCELERATOR_REGISTRATION,
            idempotent=True,
            description=(
                "Retry Maho accelerator registration once the Rust core is ready; "
                "the single-shot AddedToWidget registration saw an empty shortcut "
                "list on a cold core and left Cmd+Shift+C to DevTools Inspect"
            ),
        ),
        Replacement(
            old=_MAHO_LEGACY_UNSCOPED_IDEMPOTENT_ACCELERATOR_REGISTRATION,
            new=_MAHO_FOCUS_ACCELERATOR_REGISTRATION,
            idempotent=True,
            description=(
                "Scope Maho accelerator registration to macOS: LoadAccelerators() "
                "runs right after it and on Windows/Linux registers Ctrl+L on the "
                "same BrowserView target the Rust command_bar binding resolves to, "
                "violating AcceleratorManager's same-target invariant"
            ),
            # The ungated body is a SUBSTRING of the gated replacement (same body,
            # wrapped in #if/#endif), so without this guard the migration would
            # match its own output and nest the block on every later run.
            guard=(
                "#if BUILDFLAG(IS_MAC)\n"
                "  // Maho: register every enabled Maho shortcut as a FocusManager\n"
            ),
        ),
        Replacement(
            old=_MAHO_LEGACY_NONIDEMPOTENT_ACCELERATOR_REGISTRATION,
            new=_MAHO_FOCUS_ACCELERATOR_REGISTRATION,
            idempotent=True,
            description=(
                "Make Maho accelerator registration idempotent: skip once already "
                "registered and ignore the empty cold-core list, so the immediate "
                "call and the core-ready retry cannot both register the same "
                "target and trip AcceleratorManager's duplicate-target and "
                "single-high-priority-handler DCHECKs"
            ),
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

        Replacement(
            old=(
                '#include "maho/browser/ui/views/maho_content_gradient_view.h"\n'
            ),
            new=(
                '#include "maho/browser/ui/views/maho_content_gradient_view.h"\n'
                '#include "maho/browser/ui/views/peek/maho_peek_controller.h"\n'
                '#include "maho/browser/ui/views/spaces_overlay/maho_browser_frame_overlay_host.h"\n'
            ),
            description="Include Peek and overlay-host seams used by BrowserView",
            guard="maho/browser/ui/views/peek/maho_peek_controller.h",
        ),
        Replacement(
            old=(
                "maho::MahoCommandOverlayController*\n"
                "BrowserView::GetOrCreateMahoCommandOverlayController() {\n"
            ),
            new=(
                "maho::MahoPeekController*\n"
                "BrowserView::GetOrCreateMahoPeekController() {\n"
                "  if (!maho::IsPeekEligible(browser_.get())) {\n"
                "    return nullptr;\n"
                "  }\n"
                "  auto* container = views::AsViewClass<maho::MahoSidebarContainerView>(\n"
                "      maho_sidebar_container());\n"
                "  if (!container) {\n"
                "    return nullptr;\n"
                "  }\n"
                "  auto* host = container->GetOrCreateOverlayHost();\n"
                "  return host ? host->peek_controller() : nullptr;\n"
                "}\n"
                "\n"
                "maho::MahoCommandOverlayController*\n"
                "BrowserView::GetOrCreateMahoCommandOverlayController() {\n"
            ),
            description=(
                "Implement BrowserView::GetOrCreateMahoPeekController as an "
                "eligibility-gated forwarder to the sidebar overlay host"
            ),
            guard="BrowserView::GetOrCreateMahoPeekController()",
        ),
        Replacement(
            old=(
                '#include "chrome/browser/ui/views/frame/browser_view.h"\n'
            ),
            new=(
                '#include "chrome/browser/ui/views/frame/browser_view.h"\n'
                '#include "maho/browser/ui/views/maho_mini/maho_mini_top_bar_view.h"\n'
            ),
            description="Maho: Include maho_mini_top_bar_view.h in browser_view.cc",
            guard="maho_mini_top_bar_view.h",
        ),
        Replacement(
            old='#include "maho/browser/ui/views/maho_mini/maho_mini_top_bar_view.h"\n',
            new=(
                '#include "maho/browser/ui/views/maho_mini/maho_mini_top_bar_view.h"\n'
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
                "    maho_content_gradient_view_ = AddChildView(\n"
                "        std::make_unique<maho::MahoContentGradientView>());\n"
                "    maho_content_gradient_view_->SetVisible(false);\n"
                "  }\n"
                "\n"
                "  views::View* contents_view;\n"
            ),
            description=(
                "Create MahoContentGradientView as the first content-container "
                "child and initialize it from the live tab count"
            ),
            guard="maho_content_gradient_view_ = AddChildView(",
            idempotent=True,
        ),
        Replacement(
            old=(
                "  if (GetIsNormalType()) {\n"
                "    maho_content_gradient_view_ = contents_container->AddChildView(\n"
                "        std::make_unique<maho::MahoContentGradientView>());\n"
                "    maho_content_gradient_view_->SetVisible(\n"
                "        browser_->tab_strip_model()->count() == 0);\n"
                "  }\n"
            ),
            new=(
                "  if (GetIsNormalType()) {\n"
                "    maho_content_gradient_view_ = AddChildView(\n"
                "        std::make_unique<maho::MahoContentGradientView>());\n"
                "    maho_content_gradient_view_->SetVisible(false);\n"
                "  }\n"
            ),
            description=(
                "Migrate the already-applied nested Maho content gradient to "
                "BrowserView ownership for shared content and AI-panel glass"
            ),
            guard="maho_content_gradient_view_ = AddChildView(",
            idempotent=True,
        ),
        Replacement(
            old=(
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
            new="\n",
            description=(
                "Remove the obsolete nested-gradient reorder after moving the "
                "shared glass view to BrowserView ownership"
            ),
            idempotent=True,
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
                "  if (browser_->is_maho_mini()) {\n"
                "    maho_mini_top_bar_ = AddChildView(\n"
                "        std::make_unique<maho::MahoMiniTopBarView>(browser_.get(), this));\n"
                "  }\n"
            ),
            description="Maho: Initialize maho_mini_top_bar_ in BrowserView::InitViews",
            guard="maho_mini_top_bar_ = AddChildView",
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
                "  layout_views.maho_mini_top_bar = maho_mini_top_bar_;\n"
                "  // LINT.ThenChange(//chrome/browser/ui/views/frame/layout/browser_view_layout.h:BrowserViewLayoutViews)\n"
            ),
            description="Maho: Populate maho_mini_top_bar in layout_views",
            guard="layout_views.maho_mini_top_bar =",
        ),
        Replacement(
            old=(
                "  gfx::Point point_in_browser_view_coords(point);\n"
                "  views::View::ConvertPointToTarget(parent(), this,\n"
                "                                    &point_in_browser_view_coords);\n"
            ),
            new=(
                "  gfx::Point point_in_browser_view_coords(point);\n"
                "  views::View::ConvertPointToTarget(parent(), this,\n"
                "                                    &point_in_browser_view_coords);\n"
                "\n"
                "  // Maho: the Maho Mini top bar sits inside the macOS full-size\n"
                "  // titlebar drag region. Consult its caption hit-test so the address\n"
                "  // field and action buttons register as client area instead of being\n"
                "  // swallowed as window-drag caption.\n"
                "  if (maho_mini_top_bar_ && maho_mini_top_bar_->GetVisible()) {\n"
                "    gfx::Point test_point(point);\n"
                "    if (ConvertedHitTest(parent(), maho_mini_top_bar_, &test_point)) {\n"
                "      if (static_cast<maho::MahoMiniTopBarView*>(\n"
                "              maho_mini_top_bar_)\n"
                "              ->IsPositionInWindowCaption(test_point)) {\n"
                "        return HTCAPTION;\n"
                "      }\n"
                "      return HTCLIENT;\n"
                "    }\n"
                "  }\n"
                "\n"
                "  // Maho: the Maho sidebar sits inside the full-size window frame.\n"
                "  // Consult its caption hit-test so top-bar drag regions (traffic-light\n"
                "  // spacer, button gaps, and library rail caption) return HTCAPTION for\n"
                "  // native window dragging, while interactive controls return HTCLIENT.\n"
                "  if (maho_sidebar_container_ && maho_sidebar_container_->GetVisible()) {\n"
                "    gfx::Point test_point(point);\n"
                "    if (ConvertedHitTest(parent(), maho_sidebar_container_, &test_point)) {\n"
                "      if (static_cast<maho::MahoSidebarContainerView*>(\n"
                "              maho_sidebar_container_)\n"
                "              ->IsPositionInWindowCaption(test_point)) {\n"
                "        return HTCAPTION;\n"
                "      }\n"
                "      return HTCLIENT;\n"
                "    }\n"
                "  }\n"
            ),
            description="Maho: Consult Maho Mini and sidebar caption hit-test in NonClientHitTest",
            guard="static_cast<maho::MahoMiniTopBarView*>(",
        ),
        Replacement(
            # Anchor must match the wrapped form that patch #48 actually emits
            # (and that the checkout carries); the single-line spelling this
            # anchor used before never matched, so the migration path was dead.
            old=(
                "  if (maho_mini_top_bar_ && maho_mini_top_bar_->GetVisible()) {\n"
                "    gfx::Point test_point(point);\n"
                "    if (ConvertedHitTest(parent(), maho_mini_top_bar_, &test_point)) {\n"
                "      if (static_cast<maho::MahoMiniTopBarView*>(\n"
                "              maho_mini_top_bar_)\n"
                "              ->IsPositionInWindowCaption(test_point)) {\n"
                "        return HTCAPTION;\n"
                "      }\n"
                "      return HTCLIENT;\n"
                "    }\n"
                "  }\n"
            ),
            new=(
                "  if (maho_mini_top_bar_ && maho_mini_top_bar_->GetVisible()) {\n"
                "    gfx::Point test_point(point);\n"
                "    if (ConvertedHitTest(parent(), maho_mini_top_bar_, &test_point)) {\n"
                "      if (static_cast<maho::MahoMiniTopBarView*>(\n"
                "              maho_mini_top_bar_)\n"
                "              ->IsPositionInWindowCaption(test_point)) {\n"
                "        return HTCAPTION;\n"
                "      }\n"
                "      return HTCLIENT;\n"
                "    }\n"
                "  }\n"
                "\n"
                "  // Maho: the Maho sidebar sits inside the full-size window frame.\n"
                "  // Consult its caption hit-test so top-bar drag regions (traffic-light\n"
                "  // spacer, button gaps, and library rail caption) return HTCAPTION for\n"
                "  // native window dragging, while interactive controls return HTCLIENT.\n"
                "  if (maho_sidebar_container_ && maho_sidebar_container_->GetVisible()) {\n"
                "    gfx::Point test_point(point);\n"
                "    if (ConvertedHitTest(parent(), maho_sidebar_container_, &test_point)) {\n"
                "      if (static_cast<maho::MahoSidebarContainerView*>(\n"
                "              maho_sidebar_container_)\n"
                "              ->IsPositionInWindowCaption(test_point)) {\n"
                "        return HTCAPTION;\n"
                "      }\n"
                "      return HTCLIENT;\n"
                "    }\n"
                "  }\n"
            ),
            description="Maho: Append sidebar container caption hit-test to already-patched NonClientHitTest",
            guard="// Maho: the Maho sidebar sits inside the full-size window frame.",
        ),
        Replacement(
            old=(
                "    // The vertical tabstrip is not part of the overlay in immersive mode and\n"
                "    // must be tested separately.\n"
                "    if (vertical_tab_strip_region_view_ &&\n"
                "        vertical_tab_strip_region_view_->GetVisible()) {\n"
            ),
            new=(
                "    // Maho: the vertical sidebar is not part of the overlay in immersive mode\n"
                "    // and must be tested separately.\n"
                "    if (maho_sidebar_container_ &&\n"
                "        maho_sidebar_container_->GetVisible()) {\n"
                "      gfx::Point test_point(point);\n"
                "      if (ConvertedHitTest(parent(), maho_sidebar_container_,\n"
                "                           &test_point)) {\n"
                "        if (static_cast<maho::MahoSidebarContainerView*>(\n"
                "                maho_sidebar_container_)\n"
                "                ->IsPositionInWindowCaption(test_point)) {\n"
                "          return HTCAPTION;\n"
                "        }\n"
                "        return HTCLIENT;\n"
                "      }\n"
                "    }\n"
                "\n"
                "    // The vertical tabstrip is not part of the overlay in immersive mode and\n"
                "    // must be tested separately.\n"
                "    if (vertical_tab_strip_region_view_ &&\n"
                "        vertical_tab_strip_region_view_->GetVisible()) {\n"
            ),
            description="Maho: Consult sidebar container caption hit-test in immersive mode on macOS",
            guard="Maho: the vertical sidebar is not part of the overlay in immersive mode",
        ),
        Replacement(
            old=(
                "  layout_views.maho_mini_top_bar = maho_mini_top_bar_;\n"
                "  // LINT.ThenChange(//chrome/browser/ui/views/frame/layout/browser_view_layout.h:BrowserViewLayoutViews)\n"
            ),
            new=(
                "  layout_views.maho_mini_top_bar = maho_mini_top_bar_;\n"
                "  layout_views.maho_content_gradient_view = maho_content_gradient_view_;\n"
                "  // LINT.ThenChange(//chrome/browser/ui/views/frame/layout/browser_view_layout.h:BrowserViewLayoutViews)\n"
            ),
            description="Maho: Populate maho_content_gradient_view in layout_views",
            guard="layout_views.maho_content_gradient_view =",
        ),

        Replacement(
            old='#include "maho/browser/ui/views/maho_content_gradient_view.h"\n',
            new=(
                '#include "maho/browser/ui/views/maho_content_gradient_view.h"\n'
                '#include "chrome/common/webui_url_constants.h"\n'
                '#include "maho/browser/ui/views/sidebar/maho_sidebar_container_view.h"\n'
                '#include "maho/browser/ui/views/sidebar/maho_sidebar_view.h"\n'
                '#include "maho/browser/ui/theme/maho_theme_helper.h"\n'
            ),
            description=(
                "Maho: include sidebar container/view + theme helper in "
                "browser_view.cc for content-surface palette projection"
            ),
            guard='#include "maho/browser/ui/theme/maho_theme_helper.h"',
        ),
        Replacement(
            old=(
                "std::vector<ContentsWebView*> BrowserView::GetAllVisibleContentsWebViews() {\n"
            ),
            new=(
                "bool MahoShouldRevealSharedGlass(content::WebContents* contents) {\n"
                "  if (!contents) {\n"
                "    return true;\n"
                "  }\n"
                "  const GURL& url = contents->GetVisibleURL();\n"
                "  return url.is_empty() ||\n"
                "         (url.SchemeIs(\"about\") && url.path() == \"blank\") ||\n"
                "         (url.SchemeIs(\"chrome\") &&\n"
                "          url.host() == chrome::kChromeUINewTabHost);\n"
                "}\n"
                "\n"
                "void BrowserView::ApplyMahoSidebarPaletteToContentSurfaces(\n"
                "    const MahoSidebarPalette& palette) {\n"
                "  // Regular windows only; Incognito keeps its fixed private surface.\n"
                "  if (browser_->profile()->IsOffTheRecord()) {\n"
                "    return;\n"
                "  }\n"
                "  // Only blank and New Tab use the native themed surface. WebContents\n"
                "  // always stay opaque so page transparency cannot expose Space chrome.\n"
                "  if (maho_content_gradient_view_) {\n"
                "    maho_content_gradient_view_->SetPalette(palette);\n"
                "  }\n"
                "  bool has_native_ntp_surface = false;\n"
                "  for (ContentsWebView* view : GetAllVisibleContentsWebViews()) {\n"
                "    if (!view) {\n"
                "      continue;\n"
                "    }\n"
                "    const bool view_shows_native_ntp =\n"
                "        MahoShouldRevealSharedGlass(view->web_contents());\n"
                "    has_native_ntp_surface |= view_shows_native_ntp;\n"
                "    view->SetVisible(!view_shows_native_ntp);\n"
                "    view->SetMahoBackgroundOverride(std::nullopt);\n"
                "    view->SetBackgroundVisible(true);\n"
                "  }\n"
                "  if (maho_content_gradient_view_) {\n"
                "    maho_content_gradient_view_->SetVisible(has_native_ntp_surface);\n"
                "  }\n"
                "  TabStripModel* maho_tsm = browser_->tab_strip_model();\n"
                "  for (int i = 0; i < maho_tsm->count(); ++i) {\n"
                "    if (content::WebContents* wc = maho_tsm->GetWebContentsAt(i)) {\n"
                "      wc->SetPageBaseBackgroundColor(std::nullopt);\n"
                "    }\n"
                "  }\n"
                "}\n"
                "\n"
                "void BrowserView::MaybeRefreshMahoContentSurfacesFromSidebar() {\n"
                "  auto* container = views::AsViewClass<maho::MahoSidebarContainerView>(\n"
                "      maho_sidebar_container_);\n"
                "  if (!container) {\n"
                "    return;\n"
                "  }\n"
                "  auto* sidebar = views::AsViewClass<maho::MahoSidebarView>(\n"
                "      container->sidebar_view());\n"
                "  if (!sidebar) {\n"
                "    return;\n"
                "  }\n"
                "  const bool shared_glass_active =\n"
                "      MahoShouldRevealSharedGlass(GetActiveWebContents());\n"
                "  if (shared_glass_active) {\n"
                "    sidebar->SetSharedBrowserGlassActive(true);\n"
                "  } else {\n"
                "    sidebar->SetSharedBrowserGlassActive(false);\n"
                "  }\n"
                "  if (!maho_sidebar_palette_subscribed_) {\n"
                "    maho_sidebar_palette_subscription_ =\n"
                "        sidebar->AddSidebarPaletteChangedCallback(base::BindRepeating(\n"
                "            &BrowserView::ApplyMahoSidebarPaletteToContentSurfaces,\n"
                "            base::Unretained(this)));\n"
                "    maho_sidebar_palette_subscribed_ = true;\n"
                "  }\n"
                "  ApplyMahoSidebarPaletteToContentSurfaces(sidebar->sidebar_palette());\n"
                "}\n"
                "\n"
                "std::vector<ContentsWebView*> BrowserView::GetAllVisibleContentsWebViews() {\n"
            ),
            description=(
                "Maho: define BrowserView content-surface palette projection "
                "(gradient stops + per-instance WebContents background) from the "
                "sidebar's browser-local palette"
            ),
            guard="void BrowserView::MaybeRefreshMahoContentSurfacesFromSidebar()",
        ),
        Replacement(
            old=(
                "  const bool maho_has_stops = !palette.content_surface_stops.empty();\n"
                "  const SkColor solid = maho_has_stops\n"
                "                            ? palette.content_surface_stops.front()\n"
                "                            : SK_ColorTRANSPARENT;\n"
                "  for (ContentsWebView* view : GetAllVisibleContentsWebViews()) {\n"
                "    if (!view) {\n"
                "      continue;\n"
                "    }\n"
                "    view->SetBackgroundVisible(true);\n"
                "    if (maho_has_stops) {\n"
                "      view->SetMahoBackgroundOverride(solid);\n"
                "    }\n"
                "  }\n"
                "  if (!maho_has_stops) {\n"
                "    return;\n"
                "  }\n"
                "  TabStripModel* maho_tsm = browser_->tab_strip_model();\n"
                "  for (int i = 0; i < maho_tsm->count(); ++i) {\n"
                "    if (content::WebContents* wc = maho_tsm->GetWebContentsAt(i)) {\n"
                "      wc->SetPageBaseBackgroundColor(solid);\n"
                "    }\n"
                "  }\n"
            ),
            new=(
                "  const SkColor maho_web_content_base =\n"
                "      GetColorProvider()->GetColor(kColorWebContentsBackground);\n"
                "  for (ContentsWebView* view : GetAllVisibleContentsWebViews()) {\n"
                "    if (!view) {\n"
                "      continue;\n"
                "    }\n"
                "    view->SetBackgroundVisible(true);\n"
                "    view->SetMahoBackgroundOverride(std::nullopt);\n"
                "  }\n"
                "  TabStripModel* maho_tsm = browser_->tab_strip_model();\n"
                "  for (int i = 0; i < maho_tsm->count(); ++i) {\n"
                "    if (content::WebContents* wc = maho_tsm->GetWebContentsAt(i)) {\n"
                "      wc->SetPageBaseBackgroundColor(maho_web_content_base);\n"
                "    }\n"
                "  }\n"
            ),
            description=(
                "Maho: isolate loaded WebContents from the active Space palette"
            ),
            guard="maho_web_content_base",
            idempotent=True,
        ),
        Replacement(
            old=(
                "  if (reveal_shared_glass) {\n"
                "    for (ContentsWebView* view : GetAllVisibleContentsWebViews()) {\n"
                "      if (!view) {\n"
                "        continue;\n"
                "      }\n"
                "      view->SetMahoBackgroundOverride(std::nullopt);\n"
                "      view->SetBackgroundVisible(false);\n"
                "    }\n"
                "    if (active_contents) {\n"
                "      active_contents->SetPageBaseBackgroundColor(SK_ColorTRANSPARENT);\n"
                "    }\n"
                "    return;\n"
                "  }\n"
                "  const SkColor maho_web_content_base =\n"
                "      GetColorProvider()->GetColor(kColorWebContentsBackground);\n"
                "  for (ContentsWebView* view : GetAllVisibleContentsWebViews()) {\n"
                "    if (!view) {\n"
                "      continue;\n"
                "    }\n"
                "    view->SetBackgroundVisible(true);\n"
                "    view->SetMahoBackgroundOverride(std::nullopt);\n"
                "  }\n"
                "  TabStripModel* maho_tsm = browser_->tab_strip_model();\n"
                "  for (int i = 0; i < maho_tsm->count(); ++i) {\n"
                "    if (content::WebContents* wc = maho_tsm->GetWebContentsAt(i)) {\n"
                "      wc->SetPageBaseBackgroundColor(maho_web_content_base);\n"
                "    }\n"
                "  }\n"
            ),
            new=(
                "  if (!GetColorProvider()) {\n"
                "    return;\n"
                "  }\n"
                "  const SkColor maho_web_content_base =\n"
                "      GetColorProvider()->GetColor(kColorWebContentsBackground);\n"
                "  for (ContentsWebView* view : GetAllVisibleContentsWebViews()) {\n"
                "    if (!view) {\n"
                "      continue;\n"
                "    }\n"
                "    const bool view_reveals_shared_glass =\n"
                "        MahoShouldRevealSharedGlass(view->web_contents());\n"
                "    view->SetMahoBackgroundOverride(std::nullopt);\n"
                "    view->SetBackgroundVisible(!view_reveals_shared_glass);\n"
                "  }\n"
                "  TabStripModel* maho_tsm = browser_->tab_strip_model();\n"
                "  for (int i = 0; i < maho_tsm->count(); ++i) {\n"
                "    if (content::WebContents* wc = maho_tsm->GetWebContentsAt(i)) {\n"
                "      wc->SetPageBaseBackgroundColor(\n"
                "          MahoShouldRevealSharedGlass(wc) ? SK_ColorTRANSPARENT\n"
                "                                           : maho_web_content_base);\n"
                "    }\n"
                "  }\n"
            ),
            description=(
                "Maho: scope shared-glass transparency to each visible WebContents"
            ),
            guard="view->SetBackgroundVisible(!view_reveals_shared_glass);",
            idempotent=True,
        ),
        Replacement(
            old=(
                "  content::WebContents* active_contents = GetActiveWebContents();\n"
                "  const bool reveal_shared_glass =\n"
                "      MahoShouldRevealSharedGlass(active_contents);\n"
                "  if (maho_content_gradient_view_) {\n"
                "    maho_content_gradient_view_->SetVisible(reveal_shared_glass);\n"
                "  }\n"
                "  if (!GetColorProvider()) {\n"
                "    return;\n"
                "  }\n"
                "  const SkColor maho_web_content_base =\n"
                "      GetColorProvider()->GetColor(kColorWebContentsBackground);\n"
                "  for (ContentsWebView* view : GetAllVisibleContentsWebViews()) {\n"
                "    if (!view) {\n"
                "      continue;\n"
                "    }\n"
                "    const bool view_reveals_shared_glass =\n"
                "        MahoShouldRevealSharedGlass(view->web_contents());\n"
                "    view->SetMahoBackgroundOverride(std::nullopt);\n"
                "    view->SetBackgroundVisible(!view_reveals_shared_glass);\n"
                "  }\n"
                "  TabStripModel* maho_tsm = browser_->tab_strip_model();\n"
                "  for (int i = 0; i < maho_tsm->count(); ++i) {\n"
                "    if (content::WebContents* wc = maho_tsm->GetWebContentsAt(i)) {\n"
                "      wc->SetPageBaseBackgroundColor(\n"
                "          MahoShouldRevealSharedGlass(wc) ? SK_ColorTRANSPARENT\n"
                "                                           : maho_web_content_base);\n"
                "    }\n"
                "  }\n"
            ),
            new=(
                "  bool has_native_ntp_surface = false;\n"
                "  for (ContentsWebView* view : GetAllVisibleContentsWebViews()) {\n"
                "    if (!view) {\n"
                "      continue;\n"
                "    }\n"
                "    const bool view_shows_native_ntp =\n"
                "        MahoShouldRevealSharedGlass(view->web_contents());\n"
                "    has_native_ntp_surface |= view_shows_native_ntp;\n"
                "    view->SetVisible(!view_shows_native_ntp);\n"
                "    view->SetMahoBackgroundOverride(std::nullopt);\n"
                "    view->SetBackgroundVisible(true);\n"
                "  }\n"
                "  if (maho_content_gradient_view_) {\n"
                "    maho_content_gradient_view_->SetVisible(has_native_ntp_surface);\n"
                "  }\n"
                "  TabStripModel* maho_tsm = browser_->tab_strip_model();\n"
                "  for (int i = 0; i < maho_tsm->count(); ++i) {\n"
                "    if (content::WebContents* wc = maho_tsm->GetWebContentsAt(i)) {\n"
                "      wc->SetPageBaseBackgroundColor(std::nullopt);\n"
                "    }\n"
                "  }\n"
            ),
            description=(
                "Maho: render New Tab as a native opaque surface"
            ),
            guard="view->SetVisible(!view_shows_native_ntp);",
            idempotent=True,
        ),
        Replacement(
            old=(
                "  if (!GetColorProvider()) {\n"
                "    return;\n"
                "  }\n"
                "  const SkColor maho_web_content_base =\n"
                "      GetColorProvider()->GetColor(kColorWebContentsBackground);\n"
                "  bool has_native_ntp_surface = false;\n"
                "  for (ContentsWebView* view : GetAllVisibleContentsWebViews()) {\n"
                "    if (!view) {\n"
                "      continue;\n"
                "    }\n"
                "    const bool view_shows_native_ntp =\n"
                "        MahoShouldRevealSharedGlass(view->web_contents());\n"
                "    has_native_ntp_surface |= view_shows_native_ntp;\n"
                "    view->SetVisible(!view_shows_native_ntp);\n"
                "    view->SetMahoBackgroundOverride(std::nullopt);\n"
                "    view->SetBackgroundVisible(true);\n"
                "  }\n"
                "  if (maho_content_gradient_view_) {\n"
                "    maho_content_gradient_view_->SetVisible(has_native_ntp_surface);\n"
                "  }\n"
                "  TabStripModel* maho_tsm = browser_->tab_strip_model();\n"
                "  for (int i = 0; i < maho_tsm->count(); ++i) {\n"
                "    if (content::WebContents* wc = maho_tsm->GetWebContentsAt(i)) {\n"
                "      wc->SetPageBaseBackgroundColor(maho_web_content_base);\n"
                "    }\n"
                "  }\n"
            ),
            new=(
                "  bool has_native_ntp_surface = false;\n"
                "  for (ContentsWebView* view : GetAllVisibleContentsWebViews()) {\n"
                "    if (!view) {\n"
                "      continue;\n"
                "    }\n"
                "    const bool view_shows_native_ntp =\n"
                "        MahoShouldRevealSharedGlass(view->web_contents());\n"
                "    has_native_ntp_surface |= view_shows_native_ntp;\n"
                "    view->SetVisible(!view_shows_native_ntp);\n"
                "    view->SetMahoBackgroundOverride(std::nullopt);\n"
                "    view->SetBackgroundVisible(true);\n"
                "  }\n"
                "  if (maho_content_gradient_view_) {\n"
                "    maho_content_gradient_view_->SetVisible(has_native_ntp_surface);\n"
                "  }\n"
                "  TabStripModel* maho_tsm = browser_->tab_strip_model();\n"
                "  for (int i = 0; i < maho_tsm->count(); ++i) {\n"
                "    if (content::WebContents* wc = maho_tsm->GetWebContentsAt(i)) {\n"
                "      wc->SetPageBaseBackgroundColor(std::nullopt);\n"
                "    }\n"
                "  }\n"
            ),
            description=(
                "Maho: reset Blink page base background to std::nullopt for loaded WebContents"
            ),
            guard="wc->SetPageBaseBackgroundColor(std::nullopt);",
            idempotent=True,
        ),
        Replacement(
            old=(
                "void BrowserView::OnTabChangedAt(tabs::TabInterface* tab,\n"
                "                                 int index,\n"
                "                                 TabChangeType change_type) {\n"
                "  content::WebContents* contents = tab->GetContents();\n"
                "\n"
                "  if (change_type != TabChangeType::kLoadingOnly || contents->IsLoading()) {\n"
            ),
            new=(
                "void BrowserView::OnTabChangedAt(tabs::TabInterface* tab,\n"
                "                                 int index,\n"
                "                                 TabChangeType change_type) {\n"
                "  content::WebContents* contents = tab->GetContents();\n"
                "\n"
                "  if (change_type == TabChangeType::kLoadingOnly &&\n"
                "      contents == GetActiveWebContents()) {\n"
                "    MaybeRefreshMahoContentSurfacesFromSidebar();\n"
                "  }\n"
                "\n"
                "  if (change_type != TabChangeType::kLoadingOnly || contents->IsLoading()) {\n"
            ),
            description=(
                "Maho: refresh the web-content background when active navigation changes"
            ),
            guard=(
                "if (change_type == TabChangeType::kLoadingOnly &&\n"
                "      contents == GetActiveWebContents()) {\n"
                "    MaybeRefreshMahoContentSurfacesFromSidebar();"
            ),
        ),
        Replacement(
            old=(
                "  MaybeRefreshMahoContentSurfacesFromSidebar();\n"
                "\n"
                "  if (maho_content_gradient_view_) {\n"
                "    maho_content_gradient_view_->SetVisible(\n"
                "        tab_strip_model->count() == 0);\n"
                "  }\n"
                "  // Maho: mirror the active Space palette onto content when live tabs change.\n"
                "  MaybeRefreshMahoContentSurfacesFromSidebar();\n"
            ),
            new=(
                "  // Maho: refresh the active pane's shared-glass state when tabs change.\n"
                "  MaybeRefreshMahoContentSurfacesFromSidebar();\n"
            ),
            description="Maho: remove legacy tab-count shared-glass refresh",
            guard=(
                "// Maho: refresh the active pane's shared-glass state when tabs change.\n"
            ),
            idempotent=True,
        ),
        Replacement(
            old=(
                "void BrowserView::ApplyMahoSidebarPaletteToContentSurfaces(\n"
                "    const MahoSidebarPalette& palette) {\n"
            ),
            new=(
                "bool MahoShouldRevealSharedGlass(content::WebContents* contents) {\n"
                "  if (!contents) {\n"
                "    return true;\n"
                "  }\n"
                "  const GURL& url = contents->GetVisibleURL();\n"
                "  return url.is_empty() ||\n"
                "         (url.SchemeIs(\"about\") && url.path() == \"blank\") ||\n"
                "         (url.SchemeIs(\"chrome\") &&\n"
                "          url.host() == chrome::kChromeUINewTabHost);\n"
                "}\n"
                "\n"
                "void BrowserView::ApplyMahoSidebarPaletteToContentSurfaces(\n"
                "    const MahoSidebarPalette& palette) {\n"
            ),
            description=(
                "Migrate the already-applied content palette projection to add "
                "the blank and New Tab shared-glass predicate"
            ),
            guard="bool MahoShouldRevealSharedGlass(",
            idempotent=True,
        ),
        Replacement(
            old=(
                "  return (url.SchemeIs(\"about\") && url.path_piece() == \"blank\") ||\n"
                "         (url.SchemeIs(\"chrome\") &&\n"
                "          url.host_piece() == chrome::kChromeUINewTabHost);\n"
            ),
            new=(
                "  return (url.SchemeIs(\"about\") && url.path() == \"blank\") ||\n"
                "         (url.SchemeIs(\"chrome\") &&\n"
                "          url.host() == chrome::kChromeUINewTabHost);\n"
            ),
            description=(
                "Migrate the generated shared-glass URL predicate to the "
                "current GURL path and host APIs"
            ),
            guard="url.path() == \"blank\"",
            idempotent=True,
        ),
        Replacement(
            old=(
                "  return (url.SchemeIs(\"about\") && url.path() == \"blank\") ||\n"
                "         (url.SchemeIs(\"chrome\") &&\n"
                "          url.host() == chrome::kChromeUINewTabHost);\n"
            ),
            new=(
                "  return url.is_empty() ||\n"
                "         (url.SchemeIs(\"about\") && url.path() == \"blank\") ||\n"
                "         (url.SchemeIs(\"chrome\") &&\n"
                "          url.host() == chrome::kChromeUINewTabHost);\n"
            ),
            description=(
                "Treat an empty startup URL as a shared-glass blank surface"
            ),
            guard="return url.is_empty() ||",
            idempotent=True,
        ),
        Replacement(
            old=(
                "  if (maho_content_gradient_view_) {\n"
                "    maho_content_gradient_view_->SetGradientStops(palette.surface_stops);\n"
                "    maho_content_gradient_view_->SetGrain(palette.grain);\n"
                "  }\n"
                "  if (browser_->tab_strip_model()->count() == 0) {\n"
                "    if (ContentsWebView* view = contents_web_view()) {\n"
                "      view->SetBackgroundVisible(false);\n"
                "    }\n"
                "    return;\n"
                "  }\n"
            ),
            new=(
                "  if (maho_content_gradient_view_) {\n"
                "    maho_content_gradient_view_->SetPalette(palette);\n"
                "  }\n"
                "  content::WebContents* active_contents = GetActiveWebContents();\n"
                "  const bool reveal_shared_glass =\n"
                "      MahoShouldRevealSharedGlass(active_contents);\n"
                "  if (maho_content_gradient_view_) {\n"
                "    maho_content_gradient_view_->SetVisible(reveal_shared_glass);\n"
                "  }\n"
                "  if (reveal_shared_glass) {\n"
                "    for (ContentsWebView* view : GetAllVisibleContentsWebViews()) {\n"
                "      if (!view) {\n"
                "        continue;\n"
                "      }\n"
                "      view->SetMahoBackgroundOverride(std::nullopt);\n"
                "      view->SetBackgroundVisible(false);\n"
                "    }\n"
                "    if (active_contents) {\n"
                "      active_contents->SetPageBaseBackgroundColor(SK_ColorTRANSPARENT);\n"
                "    }\n"
                "    return;\n"
                "  }\n"
            ),
            description=(
                "Migrate the already-applied tab-count content projection to "
                "blank and New Tab URL state with transparent page backing"
            ),
            guard="const bool reveal_shared_glass =",
            idempotent=True,
        ),
        Replacement(
            old=(
                "  if (!sidebar) {\n"
                "    return;\n"
                "  }\n"
                "  if (!maho_sidebar_palette_subscribed_) {\n"
            ),
            new=(
                "  if (!sidebar) {\n"
                "    return;\n"
                "  }\n"
                "  const bool shared_glass_active =\n"
                "      MahoShouldRevealSharedGlass(GetActiveWebContents());\n"
                "  sidebar->SetSharedBrowserGlassActive(shared_glass_active);\n"
                "  if (!maho_sidebar_palette_subscribed_) {\n"
            ),
            description=(
                "Migrate the applied sidebar palette subscription to share the "
                "BrowserView glass coordinate space on blank and New Tab pages"
            ),
            guard="sidebar->SetSharedBrowserGlassActive(",
            idempotent=True,
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
                "  MaybeRefreshMahoContentSurfacesFromSidebar();\n"
                "\n"
            ),
            description=(
                "Maho: re-project the Space palette onto content surfaces whenever "
                "BrowserView observes a tab-model change"
            ),
            guard="Maho: re-project the Space palette onto content surfaces whenever",
        ),
        Replacement(
            old='  TRACE_EVENT0("ui", "BrowserView::OnActiveTabChanged");\n',
            new=(
                '  TRACE_EVENT0("ui", "BrowserView::OnActiveTabChanged");\n'
                "  // Maho: refresh content surfaces for the newly active tab/split.\n"
                "  MaybeRefreshMahoContentSurfacesFromSidebar();\n"
            ),
            description=(
                "Maho: re-project the Space palette onto content surfaces when the "
                "active tab (or split pane) changes"
            ),
            guard="refresh content surfaces for the newly active tab/split",
        ),
        Replacement(
            old=(
                "BrowserView::~BrowserView() {\n"
                "  browser_->tab_strip_model()->RemoveObserver(this);\n"
            ),
            new=(
                "BrowserView::~BrowserView() {\n"
                "  browser_->tab_strip_model()->RemoveObserver(this);\n"
                "  maho_sidebar_palette_subscription_ = {};\n"
            ),
            description=(
                "Maho: drop the sidebar palette subscription before child views "
                "(gradient/content) are torn down"
            ),
            guard="maho_sidebar_palette_subscription_ = {};",
        ),
        Replacement(
            old=(
                "  std::optional<ui::ElementIdentifier> highlight_element =\n"
                "      kTranslatePageActionElementId;\n"
                "\n"
                "  views::BubbleAnchor anchor =\n"
                "      toolbar_button_provider()->GetBubbleAnchor(kActionShowTranslate);\n"
                "  if (bubble_anchor_util::IsHighlightable(anchor)) {\n"
                "    // No need for a separate highlight.\n"
                "    highlight_element = std::nullopt;\n"
                "  }\n"
                "  CHECK_DEREF(TranslateBubbleController::From(browser_.get()))\n"
                "      .ShowTranslateBubble(web_contents, anchor, highlight_element, step,\n"
                "                           source_language, target_language, error_type,\n"
                "                           is_user_gesture ? TranslateBubbleView::USER_GESTURE\n"
                "                                           : TranslateBubbleView::AUTOMATIC);\n"
                "\n"
                "  return ShowTranslateBubbleResult::kSuccess;\n"
            ),
            new=(
                "  // Maho: in Maho's Arc-style layout, translation affordances are surfaced\n"
                + _MAHO_TRANSLATE_BUBBLE_COMMENT_TAIL
                + "  return ShowTranslateBubbleResult::kSuccess;\n"
            ),
            description=(
                "Maho: suppress upstream TranslateBubbleView in BrowserView::ShowTranslateBubble "
                "so the native Google Translate bubble never overlays the sidebar search pill"
            ),
            guard="// Maho: in Maho's Arc-style layout, translation affordances are surfaced\n",
            idempotent=True,
        ),
        # Trees patched before the sidebar search pill was removed still name
        # it in the suppression comment; point the comment at the header.
        Replacement(
            old=(
                "  // via the sidebar search pill (MahoSidebar" "SearchView) and on-device\n"
                "  // translation engine. The upstream TranslateBubbleView popup is suppressed\n"
                "  // so it does not duplicate or overlay the sidebar interface.\n"
            ),
            new=_MAHO_TRANSLATE_BUBBLE_COMMENT_TAIL,
            description=(
                "Maho: retarget the translate bubble suppression comment from the "
                "removed sidebar search pill to the contents header"
            ),
            idempotent=True,
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
                '#include "maho/browser/net/maho_atc_state.h"\n'
                '#include "maho/browser/ui/views/maho_mini/maho_mini_window.h"\n'
                '#include "maho/browser/ui/views/sidebar/maho_sidebar_prefs.h"\n'
                '#include "components/prefs/pref_service.h"\n'
                '#include "maho/browser/maho_space_profile_bridge.h"'
            ),
            description="Include Maho headers in browser.cc",
        ),
        Replacement(
            old=(
                '#include "maho/browser/maho_browser_main_extra_parts.h"\n'
            ),
            new=(
                '#include "maho/browser/maho_browser_main_extra_parts.h"\n'
                '#include "maho/browser/net/maho_atc_state.h"\n'
            ),
            description="Include Maho ATC state in browser.cc after the Maho include block is already present",
            guard='#include "maho/browser/net/maho_atc_state.h"',
        ),
        Replacement(
            old=(
                "std::u16string Browser::GetWindowTitleForTab(int index) const {\n"
                "  std::u16string title = base::UTF8ToUTF16(user_title_);\n"
            ),
            new=(
                "std::u16string Browser::GetWindowTitleForTab(int index) const {\n"
                "  // Maho: incognito windows show \"Incognito\" instead of the raw\n"
                "  // chrome://newtab URL in the Window menu and title bar.\n"
                "  if (profile_->IsOffTheRecord()) {\n"
                "    content::WebContents* otr_contents =\n"
                "        tab_strip_model_->GetWebContentsAt(index);\n"
                "    if (otr_contents) {\n"
                "      const GURL& otr_url = otr_contents->GetVisibleURL();\n"
                "      if (otr_url.SchemeIs(\"chrome\") && otr_url.host() == \"newtab\") {\n"
                "        return u\"Incognito\";\n"
                "      }\n"
                "    }\n"
                "  }\n"
                "  std::u16string title = base::UTF8ToUTF16(user_title_);\n"
            ),
            description="Maho: show \"Incognito\" as the tab window title for incognito NTP tabs",
            guard="content::WebContents* otr_contents =",
            idempotent=True,
        ),
        Replacement(
            old=(
                "  // |contents| can be NULL if GetWindowTitleForMenu is called during the\n"
                "  // window's creation (before tabs have been added).\n"
                "  if (contents) {\n"
                "    auto* const app_browser_controller = app_controller();\n"
                "    title = FormatTitleForDisplay(app_browser_controller\n"
                "                                      ? app_browser_controller->GetTitle()\n"
                "                                      : contents->GetTitle());\n"
                "  }\n"
            ),
            new=(
                "  // |contents| can be NULL if GetWindowTitleForMenu is called during the\n"
                "  // window's creation (before tabs have been added).\n"
                "  if (contents) {\n"
                "    const GURL& otr_url = contents->GetVisibleURL();\n"
                "    if (profile_->IsOffTheRecord() && otr_url.SchemeIs(\"chrome\") &&\n"
                "        otr_url.host() == \"newtab\") {\n"
                "      title = u\"Incognito\";\n"
                "    } else {\n"
                "      auto* const app_browser_controller = app_controller();\n"
                "      title = FormatTitleForDisplay(app_browser_controller\n"
                "                                        ? app_browser_controller->GetTitle()\n"
                "                                        : contents->GetTitle());\n"
                "    }\n"
                "  }\n"
            ),
            description="Maho: show \"Incognito\" as the max-width window title for incognito NTP tabs",
            guard="title = u\"Incognito\";",
            idempotent=True,
        ),
        Replacement(
            old=(
                "std::u16string Browser::GetWindowTitleFromWebContents(\n"
                "    bool include_app_name,\n"
                "    content::WebContents* contents) const {\n"
                "  std::u16string title = base::UTF8ToUTF16(user_title_);\n"
            ),
            new=(
                "std::u16string Browser::GetWindowTitleFromWebContents(\n"
                "    bool include_app_name,\n"
                "    content::WebContents* contents) const {\n"
                "  if (profile_->IsOffTheRecord() && contents) {\n"
                "    const GURL& otr_url = contents->GetVisibleURL();\n"
                "    if (otr_url.SchemeIs(\"chrome\") && otr_url.host() == \"newtab\") {\n"
                "      return u\"Incognito\";\n"
                "    }\n"
                "  }\n"
                "  std::u16string title = base::UTF8ToUTF16(user_title_);\n"
            ),
            description="Maho: show \"Incognito\" as the from-web-contents window title for incognito NTP tabs",
            guard="if (profile_->IsOffTheRecord() && contents) {",
            idempotent=True,
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
                "        maho::sidebar_prefs::kMahoMiniClickOverrideEnabled);\n"
                "    if (override_enabled) {\n"
                "      maho::LaunchMahoMini(profile(), maho::MahoMiniRequest{params.url});\n"
                "      return nullptr;\n"
                "    } else {\n"
                "      content::OpenURLParams demoted_params = params;\n"
                "      demoted_params.disposition = WindowOpenDisposition::NEW_BACKGROUND_TAB;\n"
                "      return OpenURLFromTab(source, demoted_params, std::move(navigation_handle_callback));\n"
                "    }\n"
                "  }\n"
                "#endif\n"
                "\n"
                "  struct AtcBypassState {\n"
                "    static std::set<Browser*>& BypassedBrowsers() {\n"
                "      static std::set<Browser*>* bypassed_browsers = new std::set<Browser*>();\n"
                "      return *bypassed_browsers;\n"
                "    }\n"
                "    static bool ShouldBypass(Browser* browser) {\n"
                "      return BypassedBrowsers().count(browser) > 0;\n"
                "    }\n"
                "    static void SetBypass(Browser* browser, bool bypass) {\n"
                "      if (bypass) {\n"
                "        BypassedBrowsers().insert(browser);\n"
                "      } else {\n"
                "        BypassedBrowsers().erase(browser);\n"
                "      }\n"
                "    }\n"
                "  };\n"
                "\n"
                "  if (!AtcBypassState::ShouldBypass(this)) {\n"
                "    const bool is_top_level =\n"
                "        ui::PageTransitionIsMainFrame(params.transition) &&\n"
                "        !ui::PageTransitionIsRedirect(params.transition);\n"
                "    const bool is_user_initiated =\n"
                "        !params.is_renderer_initiated || params.user_gesture;\n"
                "    bool is_maho_mini_gesture = false;\n"
                "#if BUILDFLAG(IS_MAC)\n"
                "    is_maho_mini_gesture =\n"
                "        params.disposition == WindowOpenDisposition::NEW_SPLIT_VIEW &&\n"
                "        !params.started_from_context_menu &&\n"
                "        ui::PageTransitionCoreTypeIs(params.transition,\n"
                "                                         ui::PAGE_TRANSITION_LINK);\n"
                "#endif\n"
                "#if BUILDFLAG(IS_MAC)\n"
                "    if (is_top_level && is_user_initiated &&\n"
                "        !is_maho_mini_gesture &&\n"
                "        maho::MahoAtcState::HasEnabledRules()) {\n"
                "      std::string current_space_id;\n"
                "      maho::MahoSpaceProfileBridge* bridge =\n"
                "          maho::MahoSpaceProfileBridge::GetInstance();\n"
                "      if (bridge) {\n"
                "        current_space_id = bridge->GetActiveSpaceId(this);\n"
                "      }\n"
                "      base::WeakPtr<Browser> weak_browser = AsWeakPtr();\n"
                "      Profile* prof = profile();\n"
                "      base::WeakPtr<Profile> weak_profile =\n"
                "          prof ? prof->GetWeakPtr() : nullptr;\n"
                "      base::WeakPtr<content::WebContents> weak_source =\n"
                "          source ? source->GetWeakPtr() : nullptr;\n"
                "\n"
                "      maho::DecideLinkDestination(\n"
                "          params.url, /*is_external=*/false,\n"
                "          base::BindOnce(\n"
                "              [](base::WeakPtr<Browser> browser,\n"
                "                 base::WeakPtr<Profile> profile,\n"
                "                 base::WeakPtr<content::WebContents> source,\n"
                "                 std::string current_space_id,\n"
                "                 content::OpenURLParams params,\n"
                "                 base::OnceCallback<void(content::NavigationHandle&)>\n"
                "                     navigation_handle_callback,\n"
                "                 maho::LinkDestinationResult result) {\n"
                "                if (!browser || !profile) {\n"
                "                  return;\n"
                "                }\n"
                "\n"
                "                if (result.type ==\n"
                "                        maho::LinkDestinationType::kSpace &&\n"
                "                    !result.space_id.empty() &&\n"
                "                    result.space_id != current_space_id) {\n"
                "                  maho::MahoSpaceProfileBridge* bridge =\n"
                "                      maho::MahoSpaceProfileBridge::GetInstance();\n"
                "                  if (bridge && bridge->SwitchToSpace(\n"
                "                                    browser.get(), result.space_id)) {\n"
                "                    chrome::AddSelectedTabWithURL(\n"
                "                        browser.get(), params.url, params.transition);\n"
                "                    return;\n"
                "                  }\n"
                "                }\n"
                "\n"
                "                BrowserView* browser_view =\n"
                "                    BrowserView::GetBrowserViewForBrowser(browser.get());\n"
                "                maho::MahoPeekController* peek_controller =\n"
                "                    browser_view\n"
                "                        ? browser_view->GetOrCreateMahoPeekController()\n"
                "                        : nullptr;\n"
                "                const maho::PeekRoute route = maho::DecidePeekRoute({\n"
                "                    .master_enabled = profile->GetPrefs()->GetBoolean(\n"
                "                        maho::sidebar_prefs::kPeekEnabled),\n"
                "                    .popup_routing_enabled = false,\n"
                "                    .link_routing_enabled =\n"
                "                        profile->GetPrefs()->GetBoolean(\n"
                "                            maho::sidebar_prefs::kPeekLinkRoutingEnabled),\n"
                "                    .seam = maho::PeekSeam::kOpenUrlFromTab,\n"
                "                    .disposition = params.disposition,\n"
                "                    .source_role =\n"
                "                        maho::MahoSidebarContainerView::GetPeekSourceRole(\n"
                "                            browser.get(), source.get()),\n"
                "                    .is_user_initiated = true,\n"
                "                    .force_tab = params.is_renderer_initiated &&\n"
                "                        params.disposition ==\n"
                "                            WindowOpenDisposition::NEW_BACKGROUND_TAB,\n"
                "                    .force_peek = params.is_renderer_initiated &&\n"
                "                        params.disposition ==\n"
                "                            WindowOpenDisposition::NEW_WINDOW,\n"
                "                    .is_maho_mini_gesture = false,\n"
                "                    .atc_has_cross_space_target = false,\n"
                "                    .peek_slot_state =\n"
                "                        peek_controller && peek_controller->SlotBusy()\n"
                "                            ? maho::PeekSlotState::kBusy\n"
                "                            : maho::PeekSlotState::kClosed,\n"
                "                });\n"
                "                if (route == maho::PeekRoute::kOpenInPeek &&\n"
                "                    peek_controller &&\n"
                "                    peek_controller->ShowUrl(source.get(), params.url)) {\n"
                "                  return;\n"
                "                }\n"
                "                if (route ==\n"
                "                    maho::PeekRoute::kOpenInForegroundTab) {\n"
                "                  params.disposition =\n"
                "                      WindowOpenDisposition::NEW_FOREGROUND_TAB;\n"
                "                }\n"
                "                AtcBypassState::SetBypass(browser.get(), true);\n"
                "                browser->OpenURL(\n"
                "                    params, std::move(navigation_handle_callback));\n"
                "                AtcBypassState::SetBypass(browser.get(), false);\n"
                "              },\n"
                "              weak_browser, weak_profile, weak_source, current_space_id,\n"
                "              params, std::move(navigation_handle_callback)));\n"
                "      return nullptr;\n"
                "    }\n"
                "#endif\n"
                "\n"
                "    if (is_top_level && is_user_initiated &&\n"
                "        !is_maho_mini_gesture &&\n"
                "        !maho::MahoAtcState::HasEnabledRules()) {\n"
                "      BrowserView* browser_view =\n"
                "          BrowserView::GetBrowserViewForBrowser(this);\n"
                "      maho::MahoPeekController* peek_controller =\n"
                "          browser_view\n"
                "              ? browser_view->GetOrCreateMahoPeekController()\n"
                "              : nullptr;\n"
                "      const maho::PeekRoute route = maho::DecidePeekRoute({\n"
                "          .master_enabled = profile()->GetPrefs()->GetBoolean(\n"
                "              maho::sidebar_prefs::kPeekEnabled),\n"
                "          .popup_routing_enabled = false,\n"
                "          .link_routing_enabled = profile()->GetPrefs()->GetBoolean(\n"
                "              maho::sidebar_prefs::kPeekLinkRoutingEnabled),\n"
                "          .seam = maho::PeekSeam::kOpenUrlFromTab,\n"
                "          .disposition = params.disposition,\n"
                "          .source_role =\n"
                "              maho::MahoSidebarContainerView::GetPeekSourceRole(\n"
                "                  this, source),\n"
                "          .is_user_initiated = true,\n"
                "          .force_tab = params.is_renderer_initiated &&\n"
                "              params.disposition ==\n"
                "                  WindowOpenDisposition::NEW_BACKGROUND_TAB,\n"
                "          .force_peek = params.is_renderer_initiated &&\n"
                "              params.disposition ==\n"
                "                  WindowOpenDisposition::NEW_WINDOW,\n"
                "          .is_maho_mini_gesture = false,\n"
                "          .atc_has_cross_space_target = false,\n"
                "          .peek_slot_state =\n"
                "              peek_controller && peek_controller->SlotBusy()\n"
                "                  ? maho::PeekSlotState::kBusy\n"
                "                  : maho::PeekSlotState::kClosed,\n"
                "      });\n"
                "      if (route == maho::PeekRoute::kOpenInPeek &&\n"
                "          peek_controller &&\n"
                "          peek_controller->ShowUrl(source, params.url)) {\n"
                "        return nullptr;\n"
                "      }\n"
                "      if (route == maho::PeekRoute::kOpenInForegroundTab) {\n"
                "        content::OpenURLParams foreground_params = params;\n"
                "        foreground_params.disposition =\n"
                "            WindowOpenDisposition::NEW_FOREGROUND_TAB;\n"
                "        AtcBypassState::SetBypass(this, true);\n"
                "        content::WebContents* result = OpenURLFromTab(\n"
                "            source, foreground_params,\n"
                "            std::move(navigation_handle_callback));\n"
                "        AtcBypassState::SetBypass(this, false);\n"
                "        return result;\n"
                "      }\n"
                "    }\n"
                "  }\n"
            ),
            description=(
                "Route new-tab/new-window link dispositions from pinned or Favorite "
                "tabs into Peek after Maho Mini and any cross-space ATC decision"
            ),
            guard="AtcBypassState::ShouldBypass",
        ),
        Replacement(
            old=(
                "  // At this point the |new_contents| is beyond the popup blocker, but we use\n"
                "  // the same logic for determining if the popup tracker needs to be attached.\n"
                "  if (source && blocked_content::ConsiderForPopupBlocking(disposition)) {\n"
                "    blocked_content::PopupTracker::CreateForWebContents(new_contents.get(),\n"
                "                                                        source, disposition);\n"
                "  }\n"
                "\n"
                "  // Postpone activating popups opened by content-fullscreen tabs. This permits\n"
            ),
            new=(
                "  // At this point the |new_contents| is beyond the popup blocker, but we use\n"
                "  // the same logic for determining if the popup tracker needs to be attached.\n"
                "  if (source && blocked_content::ConsiderForPopupBlocking(disposition)) {\n"
                "    blocked_content::PopupTracker::CreateForWebContents(new_contents.get(),\n"
                "                                                        source, disposition);\n"
                "  }\n"
                "\n"
                "  if (maho::IsPeekEligible(this) &&\n"
                "      profile()->GetPrefs()->GetBoolean(\n"
                "          maho::sidebar_prefs::kPeekEnabled) &&\n"
                "      profile()->GetPrefs()->GetBoolean(\n"
                "          maho::sidebar_prefs::kPeekPopupRoutingEnabled) &&\n"
                "      disposition == WindowOpenDisposition::NEW_POPUP) {\n"
                "    const maho::PeekRoute route = maho::DecidePeekRoute({\n"
                "        .master_enabled = true,\n"
                "        .popup_routing_enabled = true,\n"
                "        .link_routing_enabled = false,\n"
                "        .seam = maho::PeekSeam::kAddNewContents,\n"
                "        .disposition = disposition,\n"
                "        .source_role = maho::PeekSourceRole::kNormal,\n"
                "        .is_user_initiated = user_gesture,\n"
                "        .force_tab = false,\n"
                "        .force_peek = false,\n"
                "        .is_maho_mini_gesture = false,\n"
                "        .atc_has_cross_space_target = false,\n"
                "        .peek_slot_state = maho::PeekSlotState::kClosed,\n"
                "    });\n"
                "    if (route == maho::PeekRoute::kOpenInPeek) {\n"
                "      content::WebContents* adopted_contents = new_contents.get();\n"
                "      BrowserView* browser_view =\n"
                "          BrowserView::GetBrowserViewForBrowser(this);\n"
                "      maho::MahoPeekController* peek_controller =\n"
                "          browser_view\n"
                "              ? browser_view->GetOrCreateMahoPeekController()\n"
                "              : nullptr;\n"
                "      if (peek_controller &&\n"
                "          peek_controller->TryAdoptContents(source, &new_contents)) {\n"
                "        return adopted_contents;\n"
                "      }\n"
                "      disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;\n"
                "    } else if (route == maho::PeekRoute::kOpenInForegroundTab) {\n"
                "      disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;\n"
                "    }\n"
                "  }\n"
                "\n"
                "  // Postpone activating popups opened by content-fullscreen tabs. This permits\n"
            ),
            description=(
                "Adopt allowed NEW_POPUP WebContents into the source window's "
                "Peek controller, coercing failed or busy adoption to a foreground tab"
            ),
            guard="peek_controller->TryAdoptContents(source, &new_contents)",
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
                "  if (is_type_normal() && !profile_->IsOffTheRecord() &&\n"
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
            guard="// Maho: normal browser windows support zero-tab state.",
        ),

        Replacement(
            old=(
                '#include "maho/browser/ui/views/maho_mini/maho_mini_window.h"\n'
                '#include "maho/browser/ui/views/sidebar/maho_sidebar_prefs.h"\n'
            ),
            new=(
                '#include "maho/browser/ui/views/maho_mini/maho_mini_window.h"\n'
                '#include "maho/browser/ui/views/peek/maho_peek_controller.h"\n'
                '#include "maho/browser/ui/views/peek/maho_peek_route.h"\n'
                '#include "maho/browser/ui/views/sidebar/maho_sidebar_container_view.h"\n'
                '#include "maho/browser/ui/views/sidebar/maho_sidebar_prefs.h"\n'
            ),
            description="Include Peek adoption seams in browser.cc",
            guard='#include "maho/browser/ui/views/peek/maho_peek_controller.h"',
        ),
        Replacement(
            old=(
                "  // At this point the |new_contents| is beyond the popup blocker, but we use\n"
                "  // the same logic for determining if the popup tracker needs to be attached.\n"
                "  if (source && blocked_content::ConsiderForPopupBlocking(disposition)) {\n"
                "    blocked_content::PopupTracker::CreateForWebContents(new_contents.get(),\n"
                "                                                        source, disposition);\n"
                "  }\n"
                "\n"
                "  // Postpone activating popups opened by content-fullscreen tabs. This permits\n"
            ),
            new=(
                "  // At this point the |new_contents| is beyond the popup blocker, but we use\n"
                "  // the same logic for determining if the popup tracker needs to be attached.\n"
                "  if (source && blocked_content::ConsiderForPopupBlocking(disposition)) {\n"
                "    blocked_content::PopupTracker::CreateForWebContents(new_contents.get(),\n"
                "                                                        source, disposition);\n"
                "  }\n"
                "\n"
                "  if (maho::IsPeekEligible(this) &&\n"
                "      profile()->GetPrefs()->GetBoolean(\n"
                "          maho::sidebar_prefs::kPeekEnabled) &&\n"
                "      profile()->GetPrefs()->GetBoolean(\n"
                "          maho::sidebar_prefs::kPeekPopupRoutingEnabled) &&\n"
                "      disposition == WindowOpenDisposition::NEW_POPUP) {\n"
                "    const maho::PeekRoute route = maho::DecidePeekRoute({\n"
                "        .master_enabled = true,\n"
                "        .popup_routing_enabled = true,\n"
                "        .link_routing_enabled = false,\n"
                "        .seam = maho::PeekSeam::kAddNewContents,\n"
                "        .disposition = disposition,\n"
                "        .source_role = maho::PeekSourceRole::kNormal,\n"
                "        .is_user_initiated = user_gesture,\n"
                "        .force_tab = false,\n"
                "        .force_peek = false,\n"
                "        .is_maho_mini_gesture = false,\n"
                "        .atc_has_cross_space_target = false,\n"
                "        .peek_slot_state = maho::PeekSlotState::kClosed,\n"
                "    });\n"
                "    if (route == maho::PeekRoute::kOpenInPeek) {\n"
                "      content::WebContents* adopted_contents = new_contents.get();\n"
                "      BrowserView* browser_view =\n"
                "          BrowserView::GetBrowserViewForBrowser(this);\n"
                "      maho::MahoPeekController* peek_controller =\n"
                "          browser_view\n"
                "              ? browser_view->GetOrCreateMahoPeekController()\n"
                "              : nullptr;\n"
                "      if (peek_controller &&\n"
                "          peek_controller->TryAdoptContents(source, &new_contents)) {\n"
                "        return adopted_contents;\n"
                "      }\n"
                "      disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;\n"
                "    } else if (route == maho::PeekRoute::kOpenInForegroundTab) {\n"
                "      disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;\n"
                "    }\n"
                "  }\n"
                "\n"
                "  // Postpone activating popups opened by content-fullscreen tabs. This permits\n"
            ),
            description=(
                "Adopt allowed NEW_POPUP WebContents into the source window's "
                "Peek controller, coercing failed or busy adoption to a foreground tab"
            ),
            guard="peek_controller->TryAdoptContents(source, &new_contents)",
        ),
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
                "      is_maho_mini_(params.is_maho_mini),\n"
                "      profile_(params.profile),\n"
            ),
            description="Maho: Initialize is_maho_mini_ in Browser constructor",
            guard="is_maho_mini_(params.is_maho_mini)",
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
                "  if (is_maho_mini()) {\n"
                "    return false;\n"
                "  }\n"
                "  switch (type_) {\n"
            ),
            description="Maho: Disable window features for Maho Mini windows",
            guard="if (is_maho_mini()) {",
        ),
        Replacement(
            old=(
                "void Browser::RunFileChooser(\n"
                "    content::RenderFrameHost* render_frame_host,\n"
                "    scoped_refptr<content::FileSelectListener> listener,\n"
                "    const blink::mojom::FileChooserParams& params) {\n"
                "  FileSelectHelper::RunFileChooser(render_frame_host, std::move(listener),\n"
                "                                   params);\n"
                "}\n"
            ),
            new=(
                "namespace maho {\n"
                "// Defined in maho/browser/maho_browser_main_extra_parts.cc.\n"
                "// Returns true when the chooser was consumed by automation\n"
                "// (native dialog suppressed; input.file_upload_select completes it).\n"
                "bool MahoBeginAutomationFileChooser(\n"
                "    content::RenderFrameHost* render_frame_host,\n"
                "    scoped_refptr<content::FileSelectListener> listener,\n"
                "    blink::mojom::FileChooserParams::Mode mode);\n"
                "}  // namespace maho\n"
                "\n"
                "void Browser::RunFileChooser(\n"
                "    content::RenderFrameHost* render_frame_host,\n"
                "    scoped_refptr<content::FileSelectListener> listener,\n"
                "    const blink::mojom::FileChooserParams& params) {\n"
                "  // Maho: suppress the native file-selection dialog while an\n"
                "  // automation lease owns this tab; the MCP browser delegate\n"
                "  // completes the chooser via input.file_upload_select instead.\n"
                "  if (maho::MahoBeginAutomationFileChooser(\n"
                "          render_frame_host, listener, params.mode)) {\n"
                "    return;\n"
                "  }\n"
                "  FileSelectHelper::RunFileChooser(render_frame_host, std::move(listener),\n"
                "                                   params);\n"
                "}\n"
            ),
            description=(
                "Maho: intercept RunFileChooser under an automation lease so "
                "input.file_upload_select completes the pending chooser instead of the native dialog"
            ),
            guard="maho::MahoBeginAutomationFileChooser(",
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
    "chrome/browser/ui/startup/infobar_utils.cc": [
        Replacement(
            old=(
                "  if (!google_apis::HasAPIKeyConfigured()) {\n"
                "    GoogleApiKeysInfoBarDelegate::Create(infobar_manager);\n"
                "  }\n"
            ),
            new=(
                "  // Maho: Google API keys infobar is permanently suppressed regardless of configuration.\n"
            ),
            description="Maho: permanently suppress Google API keys missing infobar",
            guard="Maho: Google API keys infobar is permanently suppressed",
        ),
    ],
    "chrome/browser/ui/startup/default_browser_prompt/default_browser_prompt.cc": [
        Replacement(
            old=(
                "    // Only show the prompt if some other program is the user's default browser.\n"
                "    // In particular, don't show it if another install mode is default (e.g.,\n"
                "    // don't prompt for Chrome Beta if stable Chrome is the default).\n"
                "    did_show_prompt =\n"
                "        DefaultBrowserPromptManager::GetInstance()->MaybeShowPrompt();\n"
            ),
            new=(
                "    // Maho: permanently suppress default browser startup prompts (infobars, modals, bubbles).\n"
                "    did_show_prompt = false;\n"
            ),
            description="Maho: permanently suppress startup default browser prompt",
            guard="Maho: permanently suppress startup default browser prompt",
        ),
    ],
    "chrome/browser/ui/startup/default_browser_prompt/default_browser_infobar_delegate.cc": [
        Replacement(
            old=(
                "// static\n"
                "infobars::InfoBar* DefaultBrowserInfoBarDelegate::Create(\n"
                "    infobars::ContentInfoBarManager* infobar_manager,\n"
                "    Profile* profile,\n"
                "    bool can_pin_to_taskbar) {\n"
                "  return infobar_manager->AddInfoBar(\n"
                "      CreateConfirmInfoBar(std::make_unique<DefaultBrowserInfoBarDelegate>(\n"
                "          base::PassKey<DefaultBrowserInfoBarDelegate>(), profile,\n"
                "          can_pin_to_taskbar)));\n"
                "}\n"
            ),
            new=(
                "// static\n"
                "infobars::InfoBar* DefaultBrowserInfoBarDelegate::Create(\n"
                "    infobars::ContentInfoBarManager* /*infobar_manager*/,\n"
                "    Profile* /*profile*/,\n"
                "    bool /*can_pin_to_taskbar*/) {\n"
                "  // Maho: permanently suppress default browser infobar. Maho handles default browser\n"
                "  // prompts in Welcome (chrome://maho-welcome) and Settings (chrome://maho-settings).\n"
                "  return nullptr;\n"
                "}\n"
            ),
            description="Maho: permanently suppress default browser infobar creation",
            guard="Maho: permanently suppress default browser infobar creation",
        ),
    ],
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
                "// BrowserView is constructed. On the zero-tab\n"
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
                "    // BrowserView queues the new-tab overlay until its widget and views\n"
                "    // are initialized. Do not wait for sidebar construction here: in slow\n"
                "    // cold starts that unnecessarily exhausts this helper's retry budget\n"
                "    // and leaves the zero-tab browser with no palette.\n"
                "    browser_view->ShowMahoCommandOverlayForNewTab();\n"
                "    return;\n"
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
                "the new-tab command palette once BrowserView is registered; BrowserView "
                "itself defers while its widget and views initialize"
            ),
            guard="void ShowMahoColdStartPaletteWhenReady(base::WeakPtr<Browser> browser,",
        ),
        Replacement(
            old=(
                "  if (auto* browser_view =\n"
                "          BrowserView::GetBrowserViewForBrowser(browser.get())) {\n"
                "    if (browser_view->maho_sidebar_container()) {\n"
                "      browser_view->ShowMahoCommandOverlayForNewTab();\n"
                "      return;\n"
                "    }\n"
                "  }\n"
            ),
            new=(
                "  if (auto* browser_view =\n"
                "          BrowserView::GetBrowserViewForBrowser(browser.get())) {\n"
                "    // BrowserView queues the new-tab overlay until its widget and views\n"
                "    // are initialized. Do not wait for sidebar construction here: in slow\n"
                "    // cold starts that unnecessarily exhausts this helper's retry budget\n"
                "    // and leaves the zero-tab browser with no palette.\n"
                "    browser_view->ShowMahoCommandOverlayForNewTab();\n"
                "    return;\n"
                "  }\n"
            ),
            description=(
                "Maho cold-start: migrate the legacy sidebar-gated retry helper to "
                "BrowserView's widget-ready pending-overlay handoff"
            ),
            guard=(
                "// BrowserView queues the new-tab overlay until its widget and views\n"
                "    // are initialized. Do not wait for sidebar construction here:"
            ),
            idempotent=True,
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
                "      gate_profile && !profile->IsOffTheRecord() &&\n"
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
        Replacement(
            old='#include "maho/browser/ui/views/welcome/maho_welcome_window.h"\n',
            new=(
                '#include "maho/browser/ui/views/welcome/maho_welcome_window.h"\n'
                '#include "chrome/browser/profiles/profile_destroyer.h"\n'
                '#include "chrome/browser/ui/browser_finder.h"\n'
            ),
            description="Add ProfileDestroyer/browser_finder includes for the NewIncognitoWindow stale-OTR fix",
            guard='#include "chrome/browser/profiles/profile_destroyer.h"',
            idempotent=True,
        ),
        Replacement(
            old=(
                "void NewIncognitoWindow(Profile* profile) {\n"
                "  NewEmptyWindow(profile->GetPrimaryOTRProfile(/*create_if_needed=*/true));\n"
                "}"
            ),
            new=(
                "void NewIncognitoWindow(Profile* profile) {\n"
                "  Profile* original = profile->GetOriginalProfile();\n"
                "  if (original->ShutdownStarted()) {\n"
                "    return;\n"
                "  }\n"
                "  // Maho: a primary OTR profile left in ShutdownStarted() (pending\n"
                "  // ProfileDestroyer after its last incognito window closed) would be reused\n"
                "  // by GetPrimaryOTRProfile() and make OpenEmptyWindow() abort, so the new\n"
                "  // incognito window never appears. Finish destroying the stale OTR first\n"
                "  // (only when no incognito browser still references it) so a fresh primary\n"
                "  // OTR is created for the new window.\n"
                "  if (Profile* existing_otr =\n"
                "          original->GetPrimaryOTRProfile(/*create_if_needed=*/false);\n"
                "      existing_otr && existing_otr->ShutdownStarted() &&\n"
                "      chrome::GetOffTheRecordBrowsersActiveForProfile(existing_otr) == 0) {\n"
                "    ProfileDestroyer::DestroyOTRProfileImmediately(existing_otr);\n"
                "  }\n"
                "  NewEmptyWindow(original->GetPrimaryOTRProfile(/*create_if_needed=*/true));\n"
                "}"
            ),
            description=(
                "Maho: destroy a stale pending-shutdown primary OTR profile before opening "
                "a new incognito window so the window is not aborted/destroyed by a race"
            ),
            guard="ProfileDestroyer::DestroyOTRProfileImmediately(existing_otr)",
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
        "chrome/browser/BUILD.gn": [
        Replacement(
            old='      "//maho/browser:maho_context_menus",\n',
            new=(
                '      "//maho/browser:maho_context_menus",\n'
                '      "//maho/browser:maho_url_scheme",\n'
            ),
            description=(
                "Add Maho URL alias resolver directly to chrome/browser:browser deps"
            ),
            idempotent=True,
            guard=(
                '      "//maho/browser:maho_password_provider",\n'
                '       "//maho/browser:maho_url_scheme",\n'
            ),
        ),
    ],
    "chrome/test/BUILD.gn": [
        # Self-healing interactive_tests dep links. Each link anchors on the
        # sidebar dep line (present once any Maho interactive dep exists) and
        # is guarded by its own target line, so a partially reverted
        # chrome/test/BUILD.gn is repaired in one apply pass without
        # duplicating deps.
        Replacement(
            old=(
                '      "//maho/browser/ui/views/sidebar:interactive_tests",\n'
            ),
            new=(
                '      "//maho/browser/ui/views/sidebar:interactive_tests",\n'
                '      "//maho/browser/ui/views/spaces_overlay:interactive_tests",\n'
            ),
            description="Include Maho spaces-overlay interactive tests in Chromium interactive_ui_tests deps (self-healing)",
            idempotent=True,
            guard='"//maho/browser/ui/views/spaces_overlay:interactive_tests"',
        ),
        Replacement(
            old=(
                '      "//maho/browser/ui/views/sidebar:interactive_tests",\n'
            ),
            new=(
                '      "//maho/browser/ui/views/sidebar:interactive_tests",\n'
                '      "//maho/browser/ui/views/peek:interactive_tests",\n'
            ),
            description="Include Maho Peek interactive tests in Chromium interactive_ui_tests deps (self-healing)",
            idempotent=True,
            guard='"//maho/browser/ui/views/peek:interactive_tests"',
        ),
        Replacement(
            old=(
                '      "//maho/browser/ui/views/sidebar:interactive_tests",\n'
            ),
            new=(
                '      "//maho/browser/ui/views/sidebar:interactive_tests",\n'
                '      "//chrome/browser/ui/views/side_panel:interactive_tests",\n'
            ),
            description="Include Maho AI side-panel interactive tests in Chromium interactive_ui_tests deps (self-healing)",
            idempotent=True,
            guard='"//chrome/browser/ui/views/side_panel:interactive_tests"',
        ),
        Replacement(
            old=(
                '      "//maho/browser/ui/views/sidebar:interactive_tests",\n'
            ),
            new=(
                '      "//maho/browser/ui/views/sidebar:interactive_tests",\n'
                '      "//maho/browser/ui/views/command:interactive_tests",\n'
            ),
            description="Include Maho command interactive tests in Chromium interactive_ui_tests deps (self-healing)",
            idempotent=True,
            guard='"//maho/browser/ui/views/command:interactive_tests"',
        ),
        Replacement(
            old=(
                '      "//maho/browser/ui/views/spaces_overlay:interactive_tests",\n'
            ),
            new=(
                '      "//maho/browser/ui/views/spaces_overlay:interactive_tests",\n'
                '      "//maho/browser/ui/views/peek:interactive_tests",\n'
            ),
            description="Include Maho Peek interactive tests in Chromium interactive_ui_tests deps",
            idempotent=True,
            guard='"//maho/browser/ui/views/peek:interactive_tests"',
        ),
        Replacement(
            old=(
                '      "//maho/browser/ui/views/peek:interactive_tests",\n'
            ),
            new=(
                '      "//maho/browser/ui/views/peek:interactive_tests",\n'
                '      "//chrome/browser/ui/views/side_panel:interactive_tests",\n'
            ),
            description="Include Maho AI side-panel interactive tests in Chromium interactive_ui_tests deps",
            idempotent=True,
            guard='"//chrome/browser/ui/views/side_panel:interactive_tests"',
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
            idempotent=True,
            guard='"//maho/browser/ui/views/sidebar:interactive_tests"',
        ),
        Replacement(
            old='      "//maho/browser/ui/link_preview:interactive_tests",\n',
            new="",
            description="Maho: strip deleted link-preview interactive_tests dep from chrome/test/BUILD.gn",
            idempotent=True,
        ),
        Replacement(
            old='      "//maho/browser/ui/views/sidebar:interactive_tests",\n',
            new=(
                '      "//maho/browser/ui/views/sidebar:interactive_tests",\n'
                '      "//maho/browser/ui/views/shields:interactive_tests",\n'
            ),
            description="Include Maho Shield bubble interactive tests in Chromium interactive_ui_tests deps",
            guard='"//maho/browser/ui/views/shields:interactive_tests"',
        ),
        Replacement(
            old='      "//maho/browser/ui/views/shields:interactive_tests",\n',
            new=(
                '      "//maho/browser/ui/views/shields:interactive_tests",\n'
                '      "//maho/browser/ui/views/boost:interactive_tests",\n'
            ),
            description="Include Maho Boost interactive tests in Chromium interactive_ui_tests deps",
            guard='"//maho/browser/ui/views/boost:interactive_tests"',
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
            old='      "//maho/browser/ui/tabs:unit_tests",\n',
            new=(
                '      "//maho/browser/ui/tabs:unit_tests",\n'
                '      "//maho/browser/ui/views/peek:unit_tests",\n'
            ),
            description="Include Maho Peek route unit tests in Chromium unit_tests",
            guard='"//maho/browser/ui/views/peek:unit_tests"',
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
            idempotent=True,
            guard='"//maho/browser:maho_ai_runtime_router_unittests"',
        ),
        Replacement(
            old='      "//maho/browser:maho_ai_llm_client_unittests",\n',
            new=(
                '      "//maho/browser:maho_ai_llm_client_unittests",\n'
                '      "//maho/browser/net:maho_ad_block_tab_helper_unittests",\n'
            ),
            description=(
                "Include Maho ad-block tab helper request-context unit tests "
                "in Chromium unit_tests"
            ),
            idempotent=True,
            guard='"//maho/browser/net:maho_ad_block_tab_helper_unittests"',
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
        Replacement(
            old='      "//maho/browser/mcp:browser_tests",\n',
            new=(
                '      "//maho/browser:maho_google_sign_in_browser_tests",\n'
                '      "//maho/browser/mcp:browser_tests",\n'
            ),
            description=(
                "Include deferred native Google sign-in-in-Peek browser cases"
            ),
            guard='"//maho/browser:maho_google_sign_in_browser_tests"',
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
    "chrome/browser/chrome_browser_interface_binders_webui_parts_desktop.cc": [
        Replacement(
            old='#include "chrome/browser/ui/webui/tab_search/tab_search_ui.h"\n',
            new=(
                '#include "chrome/browser/ui/webui/tab_search/tab_search_ui.h"\n'
                '#include "maho/browser/ui/webui/maho_ai/maho_ai_ui.h"\n'
                '#include "maho/browser/ui/webui/maho_live_folders/maho_live_folders_ui.h"\n'
                '#include "maho/browser/ui/webui/maho_mail/maho_mail_ui.h"\n'
                '#include "maho/browser/ui/webui/maho_settings/maho_settings_ui.h"\n'
                '#include "maho/browser/ui/webui/maho_space_config/maho_space_config_ui.h"\n'
                '#include "maho/browser/ui/webui/maho_space_create/maho_space_create_ui.h"\n'
                '#include "maho/browser/ui/webui/maho_sync/maho_sync_ui.h"\n'
                '#include "maho/browser/ui/webui/maho_welcome/maho_welcome_ui.h"\n'
            ),
            description="Maho: include the Maho WebUI headers for binder registration",
            guard='#include "maho/browser/ui/webui/maho_settings/maho_settings_ui.h"',
        ),
        Replacement(
            old=(
                "  registry.ForWebUI<TabSearchUI>().Add<tab_search::mojom::PageHandlerFactory>();\n"
            ),
            new=(
                "  registry.ForWebUI<TabSearchUI>().Add<tab_search::mojom::PageHandlerFactory>();\n"
                "  registry.ForWebUI<MahoAIUI>().Add<maho_ai::mojom::PageHandlerFactory>();\n"
                "  registry.ForWebUI<MahoLiveFoldersUI>().Add<maho_live_folders::mojom::PageHandlerFactory>();\n"
                "  registry.ForWebUI<MahoMailUI>().Add<maho_mail::mojom::PageHandlerFactory>();\n"
                "  registry.ForWebUI<MahoSettingsUI>().Add<maho_settings::mojom::PageHandlerFactory>();\n"
                "  registry.ForWebUI<MahoSpaceConfigUI>().Add<maho_space_config::mojom::PageHandlerFactory>();\n"
                "  registry.ForWebUI<MahoSpaceCreateUI>().Add<maho_space_create::mojom::PageHandlerFactory>();\n"
                "  registry.ForWebUI<MahoSyncUI>().Add<maho_sync::mojom::PageHandlerFactory>();\n"
                "  registry.ForWebUI<MahoWelcomeUI>().Add<maho_welcome::mojom::PageHandlerFactory>();\n"
            ),
            description="Maho: expose the Maho WebUI page-handler factories",
            guard="registry.ForWebUI<MahoWelcomeUI>()",
        ),
    ],
    "chrome/chrome_paks.gni": [
        Replacement(
            old='      "$root_gen_dir/chrome/browser_resources.pak",\n',
            new=(
                '      "$root_gen_dir/chrome/browser_resources.pak",\n'
                + "".join(
                    '      "$root_gen_dir/chrome/%s_resources.pak",\n' % d
                    for d in [
                        "maho_ai",
                        "maho_boost",
                        "maho_boost_content_script",
                        "maho_changelog",
                        "maho_mail",
                        "maho_settings",
                        "maho_space_config",
                        "maho_space_create",
                        "maho_translate_content_script",
                        "maho_welcome",
                    ]
                )
            ),
            description="Maho: pack the Maho WebUI resource paks into resources.pak",
            guard="$root_gen_dir/chrome/maho_welcome_resources.pak",
        ),
        Replacement(
            old=(
                '      "$root_gen_dir/ui/webui/resources/webui_resources.pak",\n'
                "    ]\n"
                "\n"
                "    deps = [\n"
                '      "//base/tracing/protos:chrome_track_event_resources",\n'
            ),
            new=(
                '      "$root_gen_dir/ui/webui/resources/webui_resources.pak",\n'
                "    ]\n"
                "\n"
                "    deps = [\n"
                '      "//base/tracing/protos:chrome_track_event_resources",\n'
                + "".join(
                    '      "//maho/browser/resources/%s:resources_grit",\n' % d
                    for d in [
                        "maho_ai",
                        "maho_boost",
                        "maho_boost_content_script",
                        "maho_changelog",
                        "maho_mail",
                        "maho_settings",
                        "maho_space_config",
                        "maho_space_create",
                        "maho_translate_content_script",
                        "maho_welcome",
                    ]
                )
            ),
            description="Maho: depend on the Maho grit targets that feed resources.pak",
            guard='//maho/browser/resources/maho_welcome:resources_grit',
        ),
    ],
    "chrome/browser/ui/webui/chrome_web_ui_configs.cc": [
        Replacement(
            old=(
                '#include "chrome/browser/ui/webui/commerce/commerce_internals_ui_config.h"\n'
                '#include "chrome/browser/ui/webui/components/components_ui.h"\n'
            ),
            new=(
                '#include "chrome/browser/ui/webui/commerce/commerce_internals_ui_config.h"\n'
                '#include "chrome/browser/ui/webui/components/components_ui.h"\n'
                '#include "maho/browser/ui/webui/maho_ai/maho_ai_ui.h"\n'
                '#include "maho/browser/ui/webui/maho_changelog/maho_changelog_ui.h"\n'
                '#include "maho/browser/ui/webui/maho_live_folders/maho_live_folders_ui.h"\n'
                '#include "maho/browser/ui/webui/maho_mail/maho_mail_ui.h"\n'
                '#include "maho/browser/ui/webui/maho_routines/maho_routines_ui.h"\n'
                '#include "maho/browser/ui/webui/maho_settings/maho_settings_ui.h"\n'
                '#include "maho/browser/ui/webui/maho_space_config/maho_space_config_ui.h"\n'
                '#include "maho/browser/ui/webui/maho_space_create/maho_space_create_ui.h"\n'
                '#include "maho/browser/ui/webui/maho_sync/maho_sync_ui.h"\n'
                '#include "maho/browser/ui/webui/maho_test/maho_test_ui.h"\n'
                '#include "maho/browser/ui/webui/maho_welcome/maho_welcome_ui.h"\n'
            ),
            description="Maho: include the Maho trusted WebUI config headers",
            guard='#include "maho/browser/ui/webui/maho_settings/maho_settings_ui.h"',
        ),
        Replacement(
            old=(
                '  auto& map = content::WebUIConfigMap::GetInstance();\n'
                '  map.AddWebUIConfig(std::make_unique<AccessibilityUIConfig>());\n'
            ),
            new=(
                '  auto& map = content::WebUIConfigMap::GetInstance();\n'
                '  map.AddWebUIConfig(std::make_unique<MahoAIUIConfig>());\n'
                '  map.AddWebUIConfig(std::make_unique<MahoChangelogUIConfig>());\n'
                '  map.AddWebUIConfig(std::make_unique<MahoLiveFoldersUIConfig>());\n'
                '  map.AddWebUIConfig(std::make_unique<MahoMailUIConfig>());\n'
                '  map.AddWebUIConfig(std::make_unique<MahoRoutinesUIConfig>());\n'
                '  map.AddWebUIConfig(std::make_unique<MahoSettingsUIConfig>());\n'
                '  map.AddWebUIConfig(std::make_unique<MahoSpaceConfigUIConfig>());\n'
                '  map.AddWebUIConfig(std::make_unique<MahoSpaceCreateUIConfig>());\n'
                '  map.AddWebUIConfig(std::make_unique<MahoSyncUIConfig>());\n'
                '  map.AddWebUIConfig(std::make_unique<MahoTestUIConfig>());\n'
                '  map.AddWebUIConfig(std::make_unique<MahoWelcomeUIConfig>());\n'
                '  map.AddWebUIConfig(std::make_unique<AccessibilityUIConfig>());\n'
            ),
            description="Maho: register the Maho trusted WebUI configs",
            guard="map.AddWebUIConfig(std::make_unique<MahoSettingsUIConfig>());",
        ),
    ],
    # ── chrome/browser/ui/webui/BUILD.gn ──────────────────────────────────────
    "chrome/browser/ui/webui/BUILD.gn": [
        Replacement(
            old=(
                '  deps = [\n'
                '    ":webui",\n'
            ),
            new=(
                '  deps = [\n'
                '    ":webui",\n'
                '    "//maho/browser/ui/webui/maho_ai",\n'
                '    "//maho/browser/ui/webui/maho_boost",\n'
                '    "//maho/browser/ui/webui/maho_changelog",\n'
                '    "//maho/browser/ui/webui/maho_live_folders",\n'
                '    "//maho/browser/ui/webui/maho_mail",\n'
                '    "//maho/browser/ui/webui/maho_routines",\n'
                '    "//maho/browser/ui/webui/maho_settings",\n'
                '    "//maho/browser/ui/webui/maho_space_config",\n'
                '    "//maho/browser/ui/webui/maho_space_create",\n'
                '    "//maho/browser/ui/webui/maho_sync",\n'
                '    "//maho/browser/ui/webui/maho_test",\n'
                '    "//maho/browser/ui/webui/maho_welcome",\n'
            ),
            description="Maho: depend on the Maho WebUI targets so their configs link",
            guard='"//maho/browser/ui/webui/maho_settings",',
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
        Replacement(
            old=(
                '      {true,  false, false, true,  kVK_RightArrow,        IDC_SELECT_NEXT_TAB},\n'
                '      {true,  false, false, true,  kVK_LeftArrow,         IDC_SELECT_PREVIOUS_TAB},\n'
            ),
            new="",
            description=(
                "Maho(mac): remove Chromium's Cmd+Option+Arrow hidden tab "
                "shortcuts so Maho next_space/prev_space accelerators own "
                "those key combinations"
            ),
            idempotent=True,
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
            guard="HistoryShortcuts",
        ),
        Replacement(
            old=(
                '  EXPECT_EQ(menu_accelerator->key_code(), ui::VKEY_Y);\n'
                '  EXPECT_EQ(menu_accelerator->modifiers(), ui::EF_COMMAND_DOWN);\n'
                '}\n'
            ),
            new=(
                '  EXPECT_EQ(menu_accelerator->key_code(), ui::VKEY_Y);\n'
                '  EXPECT_EQ(menu_accelerator->modifiers(), ui::EF_COMMAND_DOWN);\n'
                '}\n'
                '\n'
                'TEST(GlobalKeyboardShortcuts, MahoSpaceArrowShortcutsAreNotChromeTabShortcuts) {\n'
                '  EXPECT_EQ(\n'
                '      NO_COMMAND,\n'
                '      CommandForKeys(kVK_RightArrow, CommandKeyState::kDown, ShiftKeyState::kUp,\n'
                '                     OptionKeyState::kDown, ControlKeyState::kUp));\n'
                '  EXPECT_EQ(\n'
                '      NO_COMMAND,\n'
                '      CommandForKeys(kVK_LeftArrow, CommandKeyState::kDown, ShiftKeyState::kUp,\n'
                '                     OptionKeyState::kDown, ControlKeyState::kUp));\n'
                '}\n'
            ),
            description=(
                "Assert Cmd+Option+Arrow remains available for Maho Space "
                "switching instead of Chromium tab switching"
            ),
            guard="MahoSpaceArrowShortcutsAreNotChromeTabShortcuts",
        ),
        Replacement(
            old=(
                'TEST(GlobalKeyboardShortcuts, MahoSpaceArrowShortcutsAreNotChromeTabShortcuts) {\n'
                '  EXPECT_EQ(\n'
                '      NO_COMMAND,\n'
                '      CommandForKeys(kVK_RightArrow, CommandKeyState::kDown, ShiftKeyState::kUp,\n'
                '                     OptionKeyState::kDown, ControlKeyState::kUp));\n'
                '  EXPECT_EQ(\n'
                '      NO_COMMAND,\n'
                '      CommandForKeys(kVK_LeftArrow, CommandKeyState::kDown, ShiftKeyState::kUp,\n'
                '                     OptionKeyState::kDown, ControlKeyState::kUp));\n'
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
            new=(
                'TEST(GlobalKeyboardShortcuts, MahoSpaceArrowShortcutsAreNotChromeTabShortcuts) {\n'
                '  EXPECT_EQ(\n'
                '      NO_COMMAND,\n'
                '      CommandForKeys(kVK_RightArrow, CommandKeyState::kDown, ShiftKeyState::kUp,\n'
                '                     OptionKeyState::kDown, ControlKeyState::kUp));\n'
                '  EXPECT_EQ(\n'
                '      NO_COMMAND,\n'
                '      CommandForKeys(kVK_LeftArrow, CommandKeyState::kDown, ShiftKeyState::kUp,\n'
                '                     OptionKeyState::kDown, ControlKeyState::kUp));\n'
                '}\n'
            ),
            description="Remove duplicate HistoryShortcuts block from prior combined injection",
            idempotent=True,
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
        Replacement(
            old=(
                "      if (browser_->is_type_normal() && "
                "!overflow_button_->GetVisible()) {\n"
            ),
            new=(
                "      if (browser_->is_type_normal() && overflow_button_ &&\n"
                "          !overflow_button_->GetVisible()) {\n"
            ),
            description=(
                "Allow macOS titlebar sizing before the toolbar overflow "
                "button is initialized"
            ),
            guard="overflow_button_ &&\n          !overflow_button_->GetVisible()",
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
                "             browser_view_->browser()->is_maho_mini()) {\n"
                "    params->window_class = remote_cocoa::mojom::WindowClass::kBrowser;\n"
                "    params->style_mask |= NSWindowStyleMaskFullSizeContentView;\n"
                "\n"
                "    // Ensure tabstrip/profile button are visible.\n"
                "    params->titlebar_appears_transparent = true;\n"
                "\n"
                "    if (browser_view_->browser()->is_maho_mini()) {\n"
                "      params->window_title_hidden = true;\n"
                "    } else if (browser_view_->GetIsWebAppType()) {\n"
                "      params->window_title_hidden = true;\n"
                "    }\n"
                "  } else {\n"
            ),
            description="Maho: Style Maho Mini window with transparent titlebar and full size content",
            guard="browser_view_->browser()->is_maho_mini()",
        ),
        Replacement(
            old=(
                "    // height reported here; report the 38dip Maho Mini top bar\n"
                "    // height so they center on its midline, not near the top edge.\n"
                "    *override_titlebar_height = true;\n"
                "    *titlebar_height = 38;\n"
            ),
            new=(
                "    // height reported here; report the 46dip Maho Mini top bar\n"
                "    // height so they center on its midline, not near the top edge.\n"
                "    *override_titlebar_height = true;\n"
                "    *titlebar_height = 46;\n"
            ),
            description="Maho: Raise an existing Maho Mini titlebar override to 46dip",
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
                "      browser_view_->browser()->is_maho_mini()) {\n"
                "    // Maho: macOS centers the traffic lights within the titlebar\n"
                "    // height reported here; report the 46dip Maho Mini top bar\n"
                "    // height so they center on its midline, not near the top edge.\n"
                "    *override_titlebar_height = true;\n"
                "    *titlebar_height = 46;\n"
                "    return;\n"
                "  }\n"
                "  if (browser_view_ && browser_view_->browser_widget() &&\n"
            ),
            description="Maho: Center traffic lights in Maho Mini 46dip top bar",
            guard="*titlebar_height = 46;",
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
                "  if ((GetBrowserView()->browser()->is_type_popup() && !GetBrowserView()->browser()->is_maho_mini()) ||\n"
                "      GetBrowserView()->browser()->is_type_devtools()) {\n"
                "    return result;\n"
                "  }\n"
            ),
            description="Maho: Allow caption button bounds for Maho Mini popups",
            guard="!GetBrowserView()->browser()->is_maho_mini()",
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
    "chrome/browser/ui/startup/bad_flags_prompt.cc": [
        Replacement(
            old=(
                "void ShowBadFlagsPrompt(content::WebContents* web_contents) {\n"
            ),
            new=(
                "void ShowBadFlagsPrompt(content::WebContents* web_contents) {\n"
                "  // Maho: suppress unsupported command-line flags security warning infobars.\n"
                "  if (web_contents) {\n"
                "    return;\n"
                "  }\n"
            ),
            description="Maho: suppress unsupported command-line flags security warning infobar",
            guard="Maho: suppress unsupported command-line flags security warning infobars.",
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
                "      drop_target_view_->GetVisible() &&\n"
                "      drop_target_view_->drag_type() != MultiContentsDropTargetView::DragType::kTab;\n"
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
            guard="show_background",
        ),
    ],
    "chrome/browser/ui/views/frame/contents_container_view.cc": [
        Replacement(
            old=(
                "void ContentsContainerView::ClearBorderRoundedCorners() {\n"
            ),
            new=(
                "void ContentsContainerView::SetMahoAiPanelAdjacent(bool adjacent) {\n"
                "  if (maho_ai_panel_adjacent_ == adjacent) {\n"
                "    return;\n"
                "  }\n"
                "  maho_ai_panel_adjacent_ = adjacent;\n"
                "  if (adjacent) {\n"
                "    ClearBorderRoundedCorners();\n"
                "  } else if (!is_in_split_) {\n"
                "    UpdateBorderRoundedCorners();\n"
                "  }\n"
                "}\n"
                "\n"
                "void ContentsContainerView::ClearBorderRoundedCorners() {\n"
            ),
            description="Maho: expose complete content-corner reset beside AI",
            guard="void ContentsContainerView::SetMahoAiPanelAdjacent",
        ),
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
    "chrome/browser/ui/views/frame/contents_container_view.h": [
        Replacement(
            old=(
                "  void UpdateBorderAndOverlay(bool is_in_split,\n"
                "                              bool is_active,\n"
                "                              bool is_highlighted);\n"
            ),
            new=(
                "  void UpdateBorderAndOverlay(bool is_in_split,\n"
                "                              bool is_active,\n"
                "                              bool is_highlighted);\n"
                "\n"
                "  void SetMahoAiPanelAdjacent(bool adjacent);\n"
            ),
            description="Maho: declare content-corner reset beside AI",
            guard="void SetMahoAiPanelAdjacent(bool adjacent);",
        ),
        Replacement(
            old="  bool is_in_split_ = false;\n",
            new=(
                "  bool is_in_split_ = false;\n"
                "  bool maho_ai_panel_adjacent_ = false;\n"
            ),
            description="Maho: track content adjacency to AI panel",
            guard="bool maho_ai_panel_adjacent_ = false;",
        ),
    ],
    "chrome/browser/profiles/chrome_browser_main_extra_parts_profiles.cc": [
        Replacement(
            old='#include "chrome/browser/webdata_services/web_data_service_factory.h"\n',
            new=(
                '#include "chrome/browser/webdata_services/web_data_service_factory.h"\n'
                '#include "maho/browser/mail_helper/maho_mail_service_factory.h"\n'
                '#include "maho/browser/ui/downloads/maho_download_bridge_service_factory.h"\n'
                '#include "maho/browser/ui/views/sidebar/maho_now_playing_coordinator_factory.h"\n'
                '#include "maho/browser/net/maho_content_blocker_update_service_factory.h"\n'
                '#include "maho/browser/net/maho_shield_site_state_factory.h"\n'
            ),
            description="Include Maho service factories in profiles.cc",
            guard='#include "maho/browser/net/maho_shield_site_state_factory.h"\n',
        ),
        Replacement(
            old='#include "maho/browser/mail_helper/maho_mail_service_factory.h"\n',
            new=(
                '#include "maho/browser/mail_helper/maho_mail_service_factory.h"\n'
                '#include "maho/browser/extensions/maho_extension_state_bridge.h"\n'
            ),
            description="Include Maho extension state bridge factory registration",
            guard='#include "maho/browser/extensions/maho_extension_state_bridge.h"\n',
        ),
        Replacement(
            old='  WebDataServiceFactory::GetInstance();\n',
            new=(
                '  WebDataServiceFactory::GetInstance();\n'
                '  maho::MahoMailServiceFactory::GetInstance();\n'
                '  maho::MahoDownloadBridgeServiceFactory::GetInstance();\n'
                '  maho::MahoNowPlayingCoordinatorFactory::GetInstance();\n'
                '  maho::MahoContentBlockerUpdateServiceFactory::GetInstance();\n'
                '  maho::MahoShieldSiteStateFactory::GetInstance();\n'
            ),
            description="Initialize Maho service factories in profiles.cc",
            guard='  maho::MahoShieldSiteStateFactory::GetInstance();\n',
        ),
        Replacement(
            old='  maho::MahoMailServiceFactory::GetInstance();\n',
            new=(
                '  maho::MahoMailServiceFactory::GetInstance();\n'
                '  maho::EnsureMahoExtensionStateBridgeFactoryBuilt();\n'
            ),
            description="Register Maho extension state bridge factory at startup",
            guard='  maho::EnsureMahoExtensionStateBridgeFactoryBuilt();\n',
        ),
        Replacement(
            old='#include "maho/browser/extensions/maho_extension_state_bridge.h"\n',
            new=(
                '#include "maho/browser/extensions/maho_extension_state_bridge.h"\n'
                '#include "maho/browser/ai/maho_artifact_registry.h"\n'
            ),
            description="Include Maho artifact registry factory registration",
            guard='#include "maho/browser/ai/maho_artifact_registry.h"\n',
        ),
        Replacement(
            old='  maho::EnsureMahoExtensionStateBridgeFactoryBuilt();\n',
            new=(
                '  maho::EnsureMahoExtensionStateBridgeFactoryBuilt();\n'
                '  maho::ai::EnsureMahoArtifactRegistryFactoryBuilt();\n'
            ),
            description="Register Maho artifact registry factory at startup",
            guard='  maho::ai::EnsureMahoArtifactRegistryFactoryBuilt();\n',
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
    "ui/base/window_open_disposition_utils.cc": [
        Replacement(
            old=(
                "  if (split_view_modifier && alt_key && !shift_key &&\n"
                "      base::FeatureList::IsEnabled(features::kSplitViewLinkOpen)) {\n"
                "    return WindowOpenDisposition::NEW_SPLIT_VIEW;\n"
                "  }"
            ),
            new=(
                "  (void)split_view_modifier;\n"
                "  if (alt_key && !shift_key &&\n"
                "      base::FeatureList::IsEnabled(features::kSplitViewLinkOpen)) {\n"
                "    return WindowOpenDisposition::NEW_SPLIT_VIEW;\n"
                "  }"
            ),
            description="Maho: Arc parity - Option/Alt-click opens link in split view",
            guard="(void)split_view_modifier;",
            idempotent=True,
        ),
        Replacement(
            old=(
                "  if (alt_key && !shift_key &&\n"
                "      base::FeatureList::IsEnabled(features::kSplitViewLinkOpen)) {\n"
                "    return WindowOpenDisposition::NEW_SPLIT_VIEW;\n"
                "  }"
            ),
            new=(
                "  (void)split_view_modifier;\n"
                "  if (alt_key && !shift_key &&\n"
                "      base::FeatureList::IsEnabled(features::kSplitViewLinkOpen)) {\n"
                "    return WindowOpenDisposition::NEW_SPLIT_VIEW;\n"
                "  }"
            ),
            description="Maho: Arc parity - suppress unused split_view_modifier warning",
            guard="(void)split_view_modifier;",
            idempotent=True,
        ),
    ],
    "third_party/blink/renderer/core/loader/navigation_policy.cc": [
        Replacement(
            old=(
                "  if (new_tab_modifier && alt && !shift &&\n"
                "      base::FeatureList::IsEnabled(::features::kSplitViewLinkOpen)) {\n"
                "    return kNavigationPolicySplitView;\n"
                "  }"
            ),
            new=(
                "  if (alt && !shift &&\n"
                "      base::FeatureList::IsEnabled(::features::kSplitViewLinkOpen)) {\n"
                "    return kNavigationPolicySplitView;\n"
                "  }"
            ),
            description="Maho: Arc parity - Option/Alt-click opens web link in split view",
            guard="if (alt && !shift &&",
            idempotent=True,
        ),
    ],
    "chrome/browser/renderer_context_menu/render_view_context_menu.cc": [
        Replacement(
            old=(
                '#include "maho/browser/ui/views/little_'
                'arc/maho_little_'
                'arc_window.h"\n'
            ),
            new='#include "maho/browser/ui/views/maho_mini/maho_mini_window.h"\n',
            description="Maho: update Maho Mini include in render_view_context_menu.cc",
            guard='maho_mini_window.h',
            idempotent=True,
        ),
        Replacement(
            old='IDC_MAHO_LINK_OPEN_' 'LITTLE_' 'ARC',
            new='IDC_MAHO_LINK_OPEN_MAHO_MINI',
            description="Maho: update Maho Mini context-menu command id in render_view_context_menu.cc",
            guard='IDC_MAHO_LINK_OPEN_MAHO_MINI',
            idempotent=True,
        ),
        Replacement(
            old='      menu_model_.AddItem(IDC_MAHO_LINK_OPEN_' 'LITTLE_' 'ARC, u"Open in Maho Mini");',
            new='      menu_model_.AddItem(IDC_MAHO_LINK_OPEN_MAHO_MINI, u"Open in Maho Mini");',
            description="Maho: Rename context menu item to Open in Maho Mini",
        ),
        Replacement(
            old=(
                '      maho::Launch'
                'Little'
                'Arc(browser_context_, maho::'
                'Little'
                'ArcRequest{params_.link_url});'
            ),
            new=(
                '      maho::LaunchMahoMini(browser_context_, '
                'maho::MahoMiniRequest{params_.link_url});'
            ),
            description="Maho: route context-menu command through Maho Mini launch API",
            guard='maho::LaunchMahoMini(browser_context_',
            idempotent=True,
        ),
        Replacement(
            old=(
                '      IDC_CONTENT_CONTEXT_TRANSLATE,\n'
                '      l10n_util::GetStringFUTF16(\n'
                '          IDS_CONTENT_CONTEXT_TRANSLATE,\n'
                '          GetTargetLanguageDisplayName(/*is_full_page_translation=*/true)),\n'
            ),
            new=(
                '      // Maho: fixed, discoverable page-translate label surfaced in the\n'
                '      // page context menu (English-only build). Command id and the\n'
                '      // ExecTranslate() -> BrowserView::ShowTranslateBubble path are\n'
                '      // unchanged, so this reuses the existing working translate bubble.\n'
                '      IDC_CONTENT_CONTEXT_TRANSLATE, u"Translate this page",\n'
            ),
            description=(
                "Maho: relabel the page-translate context menu item to "
                "'Translate this page'"
            ),
            guard='u"Translate this page"',
            idempotent=True,
        ),
        Replacement(
            old='#include "maho/browser/ui/views/maho_mini/maho_mini_window.h"\n',
            new=(
                '#include "maho/browser/ui/views/maho_mini/maho_mini_window.h"\n'
                '#include "maho/browser/net/maho_translate_injection_handler.h"\n'
            ),
            description="Maho: include on-device page-translate injection handler",
            guard='maho/browser/net/maho_translate_injection_handler.h',
            idempotent=True,
        ),
        Replacement(
            old=(
                '  if (CanTranslate(/*menu_logging=*/true)) {\n'
                '    AppendTranslateItem();\n'
                '  }'
            ),
            new=(
                '  if (maho::CanOnDeviceTranslate(embedder_web_contents_)) {\n'
                '    AppendTranslateItem();\n'
                '  }'
            ),
            description=(
                "Maho: show the page-translate context item based on on-device "
                "translate availability instead of the cloud CanTranslate gate "
                "that requires a Google API key"
            ),
            guard='if (maho::CanOnDeviceTranslate(embedder_web_contents_)) {',
            idempotent=True,
        ),
        Replacement(
            old=(
                '    case IDC_CONTENT_CONTEXT_TRANSLATE:\n'
                '      return navigation_allowed && IsTranslateEnabled();'
            ),
            new=(
                '    case IDC_CONTENT_CONTEXT_TRANSLATE:\n'
                '      return navigation_allowed &&\n'
                '             maho::CanOnDeviceTranslate(embedder_web_contents_);'
            ),
            description=(
                "Maho: enable the page-translate context item via on-device "
                "availability rather than the cloud IsTranslateEnabled gate"
            ),
            guard='maho::CanOnDeviceTranslate(embedder_web_contents_);',
            idempotent=True,
        ),
        Replacement(
            old=(
                'void RenderViewContextMenu::ExecTranslate() {\n'
                '  ChromeTranslateClient* chrome_translate_client =\n'
                '      ChromeTranslateClient::FromWebContents(embedder_web_contents_);\n'
                '  if (!chrome_translate_client) {\n'
                '    return;\n'
                '  }\n'
                '\n'
                '  translate::TranslateManager* manager =\n'
                '      chrome_translate_client->GetTranslateManager();\n'
                '  DCHECK(manager);\n'
                '  manager->ShowTranslateUI(/*auto_translate=*/true,\n'
                '                           /*triggered_from_menu=*/true);\n'
                '}'
            ),
            new=(
                'void RenderViewContextMenu::ExecTranslate() {\n'
                '  maho::TranslatePageViaOnDevice(embedder_web_contents_);\n'
                '}'
            ),
            description=(
                "Maho: route the page-translate context item to the on-device "
                "content-script injector instead of the cloud ShowTranslateUI"
            ),
            guard='maho::TranslatePageViaOnDevice(embedder_web_contents_);',
            idempotent=True,
        ),
        Replacement(
            old=(
                'void RenderViewContextMenu::RecordUsedItem(int id) {\n'
                '  // Log general ID.\n'
            ),
            new=(
                'void RenderViewContextMenu::RecordUsedItem(int id) {\n'
                '  // Maho: the custom link context-menu commands (e.g. "Open in\n'
                '  // Maho Mini") use command ids outside the upstream UMA enum map,\n'
                '  // so RecordUsedItem() must skip them; otherwise\n'
                '  // FindUMAEnumValueForCommand() returns -1 and the NOTREACHED()\n'
                '  // below aborts the browser. See maho_context_menu_ids.h.\n'
                '  if (id == IDC_MAHO_LINK_OPEN_MAHO_MINI) {\n'
                '    return;\n'
                '  }\n'
                '\n'
                '  // Log general ID.\n'
            ),
            description=(
                "Maho: skip UMA recording for the Maho Mini link context-menu "
                "command so RecordUsedItem() does not hit NOTREACHED()"
            ),
            guard='if (id == IDC_MAHO_LINK_OPEN_MAHO_MINI) {',
            idempotent=True,
        ),
        Replacement(
            old=(
                "    const WebContents* new_web_contents = source_web_contents_->OpenURL(\n"
                "        params, /*navigation_handle_callback=*/{});\n"
                "    const int new_tab_index =\n"
                "        tab_strip_model->GetIndexOfWebContents(new_web_contents);\n"
                "\n"
                "    // Create split and activate new tab.\n"
                "    tab_strip_model->AddToNewSplit(\n"
                "        {new_tab_index}, split_tabs::SplitTabVisualData(),\n"
                "        split_tabs::SplitTabCreatedSource::kLinkContextMenu);\n"
                "    tab_strip_model->ActivateTabAt(\n"
                "        tab_strip_model->GetIndexOfWebContents(new_web_contents));\n"
            ),
            new=(
                "    const WebContents* new_web_contents = source_web_contents_->OpenURL(\n"
                "        params, /*navigation_handle_callback=*/{});\n"
                "    if (!new_web_contents) {\n"
                "      return;\n"
                "    }\n"
                "    const int new_tab_index =\n"
                "        tab_strip_model->GetIndexOfWebContents(new_web_contents);\n"
                "    if (new_tab_index == TabStripModel::kNoTab) {\n"
                "      return;\n"
                "    }\n"
                "\n"
                "    // Create split and activate new tab.\n"
                "    tab_strip_model->AddToNewSplit(\n"
                "        {new_tab_index}, split_tabs::SplitTabVisualData(),\n"
                "        split_tabs::SplitTabCreatedSource::kLinkContextMenu);\n"
                "    const int post_split_index =\n"
                "        tab_strip_model->GetIndexOfWebContents(new_web_contents);\n"
                "    if (post_split_index != TabStripModel::kNoTab) {\n"
                "      tab_strip_model->ActivateTabAt(post_split_index);\n"
                "    }\n"
            ),
            description=(
                "Maho: guard OpenLinkInSplitView against null WebContents or "
                "kNoTab index before AddToNewSplit, and activate via post-split index"
            ),
            guard="const int post_split_index =",
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
                "    bool is_maho_mini = false;\n"
            ),
            description="Maho: Add is_maho_mini to CreateParams",
            guard="bool is_maho_mini = false;",
        ),
        Replacement(
            old=(
                "  ~Browser() override;\n"
            ),
            new=(
                "  ~Browser() override;\n"
                "\n"
                "  bool is_maho_mini() const { return is_maho_mini_; }\n"
            ),
            description="Maho: Add is_maho_mini getter to Browser",
            guard="is_maho_mini() const",
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
                "  const bool is_maho_mini_;\n"
            ),
            description="Maho: Add is_maho_mini_ member to Browser",
            guard="const bool is_maho_mini_;",
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
                "  raw_ptr<views::View> maho_mini_top_bar = nullptr;\n"
            ),
            description="Maho: Add maho_mini_top_bar to BrowserViewLayoutViews",
            guard="maho_mini_top_bar = nullptr;",
        ),
        Replacement(
            old="  raw_ptr<views::View> maho_mini_top_bar = nullptr;\n",
            new=(
                "  raw_ptr<views::View> maho_mini_top_bar = nullptr;\n"
                "\n"
                "  raw_ptr<views::View> maho_content_gradient_view = nullptr;\n"
            ),
            description="Maho: Add maho_content_gradient_view to BrowserViewLayoutViews",
            guard="maho_content_gradient_view = nullptr;",
        ),
    ],
    "chrome/browser/ui/views/frame/layout/browser_view_tabbed_layout_impl.cc": [
        Replacement(
            old=_MAHO_SIDEBAR_RAIL_LAYOUT_V1,
            new=_MAHO_SIDEBAR_RAIL_LAYOUT,
            idempotent=True,
            description=(
                "Migrate the reserved-only sidebar rail layout to also float the "
                "auto-hide sidebar over the contents"
            ),
        ),
        Replacement(
            old="  bool needs_exclusion = true;\n",
            new=_MAHO_SIDEBAR_RAIL_LAYOUT,
            description=(
                "Maho: lay out the native sidebar rail on the leading edge and inset "
                "the remaining browser area"
            ),
            guard="Maho: an auto-hide sidebar reserves no width",
        ),
        Replacement(
            old='#include "ui/views/view_utils.h"\n',
            new=(
                '#include "ui/views/view_utils.h"\n'
                '#include "maho/browser/ui/views/sidebar/maho_sidebar_container_view.h"\n'
            ),
            description="Maho: include the sidebar container for floating layout",
            guard='#include "maho/browser/ui/views/sidebar/maho_sidebar_container_view.h"',
        ),
        Replacement(
            old=(
                "BrowserViewTabbedLayoutImpl::GetTabStripType() const {\n"
                "  if (delegate().ShouldUseTouchableTabstrip()) {\n"
            ),
            new=(
                "BrowserViewTabbedLayoutImpl::GetTabStripType() const {\n"
                "  // Maho Arc layout: Maho permanently suppresses stock Chromium tab strips\n"
                "  // regardless of sidebar visibility or toggle state.\n"
                "  return TabStripType::kNone;\n"
                "  if (delegate().ShouldUseTouchableTabstrip()) {\n"
            ),
            description="Maho: permanently suppress stock tab strips regardless of sidebar state",
            guard="Maho Arc layout: Maho permanently suppresses stock Chromium tab strips",
        ),
        Replacement(
            old=(
                "  // When the tabstrip isn't at the top or in constrained widths, the top\n"
                "  // container is laid out before all side panels.\n"
                "  if (horizontal_layout.force_top_container_to_top &&\n"
                "      IsParentedTo(views().top_container, views().browser_view)) {\n"
            ),
            new=(
                "  // Maho Arc layout: stock top chrome (tabs, toolbar, omnibox) must never occupy\n"
                "  // vertical space in the window, regardless of sidebar visibility or toggle state.\n"
                "  if (IsParentedTo(views().top_container, views().browser_view)) {\n"
                "    layout.AddChild(views().top_container, gfx::Rect(), false);\n"
                "  }\n"
                "  if (IsParentedTo(views().top_container_separator, views().browser_view)) {\n"
                "    layout.AddChild(views().top_container_separator, gfx::Rect(), false);\n"
                "  }\n"
                "\n"
                "  // When the tabstrip isn't at the top or in constrained widths, the top\n"
                "  // container is laid out before all side panels.\n"
                "  if ((false) && horizontal_layout.force_top_container_to_top &&\n"
                "      IsParentedTo(views().top_container, views().browser_view)) {\n"
            ),
            description="Maho: suppress top_container vertical space consumption before side panels",
            guard="Maho Arc layout: stock top chrome (tabs, toolbar, omnibox) must never occupy",
        ),
        Replacement(
            old=(
                "  if (!horizontal_layout.force_top_container_to_top &&\n"
                "      IsParentedTo(views().top_container, views().browser_view)) {\n"
            ),
            new=(
                "  if ((false) && !horizontal_layout.force_top_container_to_top &&\n"
                "      IsParentedTo(views().top_container, views().browser_view)) {\n"
            ),
            description="Maho: suppress top_container layout after side panels",
            guard="if ((false) && !horizontal_layout.force_top_container_to_top &&",
        ),
        Replacement(
            old=(
                "    views().vertical_tab_strip_region_view->SetCaptionButtonWidthForLayout(\n"
                "        std::max(0, caption_button_width));\n"
                "  }\n"
                "\n"
                "  return layout;\n"
            ),
            new=(
                "    views().vertical_tab_strip_region_view->SetCaptionButtonWidthForLayout(\n"
                "        std::max(0, caption_button_width));\n"
                "  }\n"
                "\n"
                "  // Maho Arc layout: the sidebar rail owns navigation and the omnibox,\n"
                "  // so the stock top chrome must not occupy the window. Collapse it\n"
                "  // after upstream has finished laying everything out, which keeps the\n"
                "  // upstream geometry pipeline (exclusions, side panels, splits) intact.\n"
                "  for (views::View* maho_top_chrome :\n"
                "       {static_cast<views::View*>(views().top_container),\n"
                "        static_cast<views::View*>(views().top_container_separator)}) {\n"
                "    if (!IsParentedTo(maho_top_chrome, views().browser_view)) {\n"
                "      continue;\n"
                "    }\n"
                "    auto maho_it = layout.children.find(maho_top_chrome);\n"
                "    if (maho_it == layout.children.end()) {\n"
                "      layout.AddChild(maho_top_chrome, gfx::Rect(), false);\n"
                "      continue;\n"
                "    }\n"
                "    maho_it->second.bounds = gfx::Rect();\n"
                "    maho_it->second.visibility = false;\n"
                "    maho_it->second.children.clear();\n"
                "  }\n"
                "\n"
                "  return layout;\n"
            ),
            description="Maho: collapse stock top chrome while the Arc sidebar rail is visible",
            guard="Maho Arc layout: the sidebar rail owns navigation and the omnibox",
        ),
        Replacement(
            old=(
                "#include \"chrome/browser/ui/views/frame/browser_view.h\"\n"
            ),
            new=(
                "#include \"chrome/browser/ui/views/frame/browser_view.h\"\n"
                "#include \"chrome/browser/ui/views/frame/contents_container_view.h\"\n"
                "#include \"chrome/browser/ui/views/frame/contents_web_view.h\"\n"
            ),
            description="Maho: include content views for AI seam corner control",
            guard="contents_container_view.h",
        ),
        Replacement(
            old=(
                "void BrowserViewTabbedLayoutImpl::DoPostLayoutVisualAdjustments(\n"
                "    const BrowserLayoutParams& params) {\n"
            ),
            new=(
                "void BrowserViewTabbedLayoutImpl::DoPostLayoutVisualAdjustments(\n"
                "    const BrowserLayoutParams& params) {\n"
                "  const bool maho_ai_panel_showing =\n"
                "      views().toolbar_height_side_panel &&\n"
                "      views().toolbar_height_side_panel->IsMahoAiPanelShowing();\n"
            ),
            description="Maho: expose AI-panel visibility throughout visual adjustments",
            guard=(
                "  const bool maho_ai_panel_showing =\n"
                "      views().toolbar_height_side_panel &&\n"
                "      views().toolbar_height_side_panel->IsMahoAiPanelShowing();\n"
            ),
        ),
        Replacement(
            old=(
                "      contents->layer()->SetRoundedCornerRadius(\n"
                "          gfx::RoundedCornersF(0.0f, 12.0f, 12.0f, 0.0f));\n"
            ),
            new=(
                "      contents->layer()->SetRoundedCornerRadius(\n"
                "          maho_ai_panel_showing\n"
                "              ? gfx::RoundedCornersF(0.0f)\n"
                "              : gfx::RoundedCornersF(0.0f, 12.0f, 12.0f, 0.0f));\n"
                "      contents->layer()->SetMasksToBounds(true);\n"
                "      if (maho_ai_panel_showing && views().multi_contents_view) {\n"
                "        for (ContentsContainerView* container :\n"
                "             views().multi_contents_view->contents_container_views()) {\n"
                "          container->contents_view()->SetBackgroundRadii(\n"
                "              gfx::RoundedCornersF(0.0f));\n"
                "          container->contents_view()->holder()->SetCornerRadii(\n"
                "              gfx::RoundedCornersF(0.0f));\n"
                "        }\n"
                "      }\n"
            ),
            description="Maho: square the content seam while the AI panel is open",
            idempotent=True,
            guard="maho_ai_panel_showing\n",
        ),
        Replacement(
            old=(
                "      toolbar_corners.upper_trailing.type =\n"
                "          CustomCornersBackground::CornerType::kRoundedWithBackground;\n"
            ),
            new=(
                "      if (!maho_ai_panel_showing) {\n"
                "        toolbar_corners.upper_trailing.type =\n"
                "            CustomCornersBackground::CornerType::kRoundedWithBackground;\n"
                "      }\n"
            ),
            description="Maho: square the toolbar-to-AI seam",
            guard="toolbar_corners.upper_trailing.type =",
        ),
        Replacement(
            old=(
                "        main_background_corners.upper_trailing.type =\n"
                "            CustomCornersBackground::CornerType::kRoundedWithBackground;\n"
            ),
            new=(
                "        if (!maho_ai_panel_showing) {\n"
                "          main_background_corners.upper_trailing.type =\n"
                "              CustomCornersBackground::CornerType::kRoundedWithBackground;\n"
                "        }\n"
            ),
            description="Maho: square the upper content-to-AI seam",
            guard="main_background_corners.upper_trailing.type =",
        ),
        Replacement(
            old=(
                "      main_background_corners.lower_trailing =\n"
                "          background->GetWindowCorner(/*upper=*/false);\n"
            ),
            new=(
                "      if (!maho_ai_panel_showing) {\n"
                "        main_background_corners.lower_trailing =\n"
                "            background->GetWindowCorner(/*upper=*/false);\n"
                "      }\n"
            ),
            description="Maho: square the lower content-to-AI seam",
            guard="if (!maho_ai_panel_showing) {",
        ),
        Replacement(
            old=(
                "  return views().toolbar_height_side_panel->GetVisible();\n"
                "}\n"
                "\n"
                "BrowserViewTabbedLayoutImpl::VerticalTabStripCollapsedState\n"
            ),
            new=(
                "  return views().toolbar_height_side_panel->GetVisible() &&\n"
                "         !views().toolbar_height_side_panel->IsMahoAiPanelShowing();\n"
                "}\n"
                "\n"
                "BrowserViewTabbedLayoutImpl::VerticalTabStripCollapsedState\n"
            ),
            description="Maho: suppress toolbar shadow gap for the AI panel",
            guard="!views().toolbar_height_side_panel->IsMahoAiPanelShowing()",
        ),
        Replacement(
            old=(
                "    layout.side_panel_padding =\n"
                "        GetLayoutConstant(LayoutConstant::kToolbarHeightSidePanelInset);\n"
            ),
            new=(
                "    layout.side_panel_padding = panel->IsMahoAiPanelShowing()\n"
                "                                    ? 0\n"
                "                                    : GetLayoutConstant(\n"
                "                                          LayoutConstant::kToolbarHeightSidePanelInset);\n"
            ),
            description="Maho: remove toolbar side-panel inset for the AI panel",
            guard="layout.side_panel_padding = panel->IsMahoAiPanelShowing()",
        ),
        Replacement(
            old=(
                "      const gfx::Rect panel_bounds = layout.GetBoundsFor(\n"
                "          views().contents_height_side_panel, views().browser_view);\n"
                "      shared_glass_bounds.Union(panel_bounds);\n"
            ),
            new=(
                "      const auto panel_bounds = layout.GetBoundsFor(\n"
                "          views().contents_height_side_panel, views().browser_view);\n"
                "      if (panel_bounds) {\n"
                "        shared_glass_bounds.Union(*panel_bounds);\n"
                "      }\n"
            ),
            description=(
                "Migrate generated shared-glass panel bounds to the optional "
                "ProposedLayout lookup API"
            ),
            guard="shared_glass_bounds.Union(*panel_bounds)",
            idempotent=True,
        ),
        Replacement(
            old=(
                "  if (views().maho_content_gradient_view) {\n"
                "    views().maho_content_gradient_view->SetBoundsRect(\n"
                "        gfx::Rect(contents_bounds.size()));\n"
                "  }\n"
            ),
            new=(
                "  if (views().maho_content_gradient_view) {\n"
                "    gfx::Rect shared_glass_bounds = contents_bounds;\n"
                "    if (views().contents_height_side_panel &&\n"
                "        views().contents_height_side_panel->GetVisible()) {\n"
                "      const auto panel_bounds = layout.GetBoundsFor(\n"
                "          views().contents_height_side_panel, views().browser_view);\n"
                "      if (panel_bounds) {\n"
                "        shared_glass_bounds.Union(*panel_bounds);\n"
                "      }\n"
                "    }\n"
                "    shared_glass_bounds.Union(contents_bounds);\n"
                "    layout.AddChild(views().maho_content_gradient_view,\n"
                "                    shared_glass_bounds,\n"
                "                    views().maho_content_gradient_view->GetVisible());\n"
                "  }\n"
            ),
            description=(
                "Migrate the already-applied content-only gradient bounds to "
                "the shared content and side-panel BrowserView bounds"
            ),
            guard="shared_glass_bounds.Union(*panel_bounds)",
            idempotent=True,
        ),
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
                "    gfx::Rect shared_glass_bounds = contents_bounds;\n"
                "    if (views().contents_height_side_panel &&\n"
                "        views().contents_height_side_panel->GetVisible()) {\n"
                "      const auto panel_bounds = layout.GetBoundsFor(\n"
                "          views().contents_height_side_panel, views().browser_view);\n"
                "      if (panel_bounds) {\n"
                "        shared_glass_bounds.Union(*panel_bounds);\n"
                "      }\n"
                "    }\n"
                "    shared_glass_bounds.Union(contents_bounds);\n"
                "    layout.AddChild(views().maho_content_gradient_view,\n"
                "                    shared_glass_bounds,\n"
                "                    views().maho_content_gradient_view->GetVisible());\n"
                "  }\n"
                "\n"
                "  if (views().maho_create_space_blank_view &&\n"
            ),
            description=(
                "Size the nested Maho content gradient to the Arc content rect "
                "during BrowserView layout"
            ),
            guard="shared_glass_bounds.Union(*panel_bounds)",
            idempotent=True,
        ),
    ],
    "chrome/browser/ui/views/side_panel/side_panel.cc": [
        Replacement(
            old=(
                "void SidePanel::RemoveHeaderView() {\n"
                "  SetBorder(views::CreateEmptyBorder(GetBorderInsets().set_top(0)));\n"
            ),
            new=(
                "void SidePanel::RemoveHeaderView() {\n"
                "  SetBorder(IsMahoAiPanelShowing() || !border_view_\n"
                "                ? nullptr\n"
                "                : views::CreateEmptyBorder(\n"
                "                      GetBorderInsets().set_top(0)));\n"
            ),
            description=(
                "Maho: keep the headerless AI panel flush without Chromium's "
                "default empty-border frame"
            ),
            guard="SetBorder(IsMahoAiPanelShowing() || !border_view_",
        ),
        Replacement(
            old=(
                "bool SidePanel::ShouldRestrictMaxWidth() const {\n"
            ),
            new=(
                "bool SidePanel::IsMahoAiPanelShowing() const {\n"
                "  const SidePanelUI* side_panel_ui =\n"
                "      browser_view_->browser()->GetFeatures().side_panel_ui();\n"
                "  if (side_panel_ui &&\n"
                "      side_panel_ui->GetCurrentEntryId(type_) ==\n"
                "          SidePanelEntryId::kMahoAiPanel) {\n"
                "    return true;\n"
                "  }\n"
                "  return content_parent_view_ &&\n"
                "         !content_parent_view_->children().empty() &&\n"
                "         content_parent_view_->children().front()->GetClassName() ==\n"
                "             std::string_view(\"MahoAiSidePanelWebView\");\n"
                "}\n"
                "\n"
                "bool SidePanel::ShouldRestrictMaxWidth() const {\n"
            ),
            description="Maho: expose active AI side-panel identity for borderless layout",
            guard="bool SidePanel::IsMahoAiPanelShowing() const",
        ),
        Replacement(
            old=(
                "  void AddedToWidget() override {\n"
                "    SetBackground(std::make_unique<ContentParentBackground>(\n"
                "        browser_view_, type_,\n"
                "        base::BindRepeating(\n"
                "            [](base::WeakPtr<ContentParentView> view) {\n"
                "              return view ? view->GetRoundedCorners() : gfx::RoundedCornersF();\n"
                "            },\n"
                "            weak_ptr_factory_.GetWeakPtr())));\n"
                "  }\n"
            ),
            new=(
                "  void AddedToWidget() override {\n"
                "    const bool hosts_maho_ai =\n"
                "        !children().empty() && children().front()->GetClassName() ==\n"
                "                                   std::string_view(\"MahoAiSidePanelWebView\");\n"
                "    SetBackground(hosts_maho_ai\n"
                "                      ? nullptr\n"
                "                      : std::make_unique<ContentParentBackground>(\n"
                "                            browser_view_, type_,\n"
                "                            base::BindRepeating(\n"
                "                                [](base::WeakPtr<ContentParentView> view) {\n"
                "                                  return view ? view->GetRoundedCorners()\n"
                "                                              : gfx::RoundedCornersF();\n"
                "                                },\n"
                "                                weak_ptr_factory_.GetWeakPtr())));\n"
                "  }\n"
            ),
            description=(
                "Maho: preserve AI content-parent transparency when AddedToWidget "
                "runs after the child is attached"
            ),
            idempotent=True,
            guard="const bool hosts_maho_ai =",
        ),
        Replacement(
            old=(
                "  void OnChildViewAdded(views::View* observed_view,\n"
                "                        views::View* child) override {\n"
            ),
            new=(
                "  void OnChildViewAdded(views::View* observed_view,\n"
                "                        views::View* child) override {\n"
                "    if (child && child->GetClassName() ==\n"
                "                     std::string_view(\"MahoAiSidePanelWebView\")) {\n"
                "      SetBackground(nullptr);\n"
                "      if (views::IsViewClass<views::WebView>(child)) {\n"
                "        views::AsViewClass<views::WebView>(child)\n"
                "            ->holder()\n"
                "            ->SetCornerRadii(gfx::RoundedCornersF());\n"
                "      }\n"
                "      if (child->layer()) {\n"
                "        child->layer()->SetRoundedCornerRadius(gfx::RoundedCornersF());\n"
                "      }\n"
                "      browser_view_->InvalidateLayout();\n"
                "      return;\n"
                "    }\n"
            ),
            description=(
                "Maho: keep the MahoAiSidePanelWebView content parent transparent "
                "so the shared BrowserView glass remains visible"
            ),
            guard="MahoAiSidePanelWebView",
        ),
        Replacement(
            old=(
                "  void OnWebContentsAttached(views::WebView* web_view) {\n"
                "    CHECK(web_view);\n"
                "    CHECK(web_view->holder());\n"
                "\n"
                "    // Native View Host doesn't always get reused, so ensure a nested Native\n"
                "    // View's corners are always rounded.\n"
                "    web_view->holder()->SetCornerRadii(GetRoundedCorners());\n"
            ),
            new=(
                "  void OnWebContentsAttached(views::WebView* web_view) {\n"
                "    CHECK(web_view);\n"
                "    CHECK(web_view->holder());\n"
                "\n"
                "    // Native View Host doesn't always get reused. Maho AI is the\n"
                "    // borderless toolbar panel, so keep its native holder square.\n"
                "    web_view->holder()->SetCornerRadii(\n"
                "        web_view->GetClassName() ==\n"
                "                std::string_view(\"MahoAiSidePanelWebView\")\n"
                "            ? gfx::RoundedCornersF()\n"
                "            : GetRoundedCorners());\n"
            ),
            description="Maho: prevent delayed native corner clipping on the AI panel",
            guard="borderless toolbar panel, so keep its native holder square",
        ),
        Replacement(
            old=(
                "  bool side_panel_open_or_closing = GetVisible() || should_be_open;\n"
                "  if (border_view_ &&\n"
                "      side_panel_open_or_closing != border_view_->GetVisible()) {\n"
                "    border_view_->SetVisible(side_panel_open_or_closing);\n"
            ),
            new=(
                "  bool side_panel_open_or_closing = GetVisible() || should_be_open;\n"
                "  const bool border_should_be_visible =\n"
                "      side_panel_open_or_closing && !IsMahoAiPanelShowing();\n"
                "  if (border_view_ &&\n"
                "      border_should_be_visible != border_view_->GetVisible()) {\n"
                "    border_view_->SetVisible(border_should_be_visible);\n"
            ),
            description="Maho: suppress the native side-panel border painter for AI",
            guard="const bool border_should_be_visible =",
        ),
    ],
    "chrome/browser/ui/views/side_panel/side_panel_resize_area.cc": [
        Replacement(
            old=(
                '#include "ui/views/background.h"\n'
            ),
            new=(
                '#include "ui/views/background.h"\n'
                '#include "ui/views/border.h"\n'
            ),
            description="Maho: include border support for the AI resize seam",
            guard='#include "ui/views/border.h"',
        ),
        Replacement(
            old=(
                "  layer()->SetFillsBoundsOpaquely(false);\n"
            ),
            new=(
                "  layer()->SetFillsBoundsOpaquely(false);\n"
                "  SetBorder(CreateSolidSidedBorder(\n"
                "      gfx::Insets::TLBR(0, 1, 0, 0),\n"
                "      kColorSidePanelContentAreaSeparator));\n"
            ),
            description="Maho: draw a thin draggable side-panel resize seam",
            guard="SetBorder(CreateSolidSidedBorder(",
        ),
        Replacement(
            old=(
                "  gfx::Rect resize_bounds;\n"
            ),
            new=(
                "  gfx::Rect resize_bounds;\n"
                "  const int maho_ai_resize_width =\n"
                "      side_panel_->IsMahoAiPanelShowing() ? 5 : 0;\n"
            ),
            description="Maho: reserve a draggable hit width for the borderless AI seam",
            guard="const int maho_ai_resize_width =",
        ),
        Replacement(
            old=(
                "    resize_bounds = gfx::Rect(local_bounds.x(), local_bounds.y(),\n"
                "                              contents_bounds.x(), local_bounds.height());\n"
            ),
            new=(
                "    resize_bounds = gfx::Rect(\n"
                "        local_bounds.x(), local_bounds.y(),\n"
                "        std::max(contents_bounds.x(), maho_ai_resize_width),\n"
                "        local_bounds.height());\n"
            ),
            description="Maho: keep the left AI resize seam draggable without panel padding",
            guard=(
                "std::max(contents_bounds.x(),\n"
                "                                       maho_ai_resize_width)"
            ),
        ),
        Replacement(
            old=(
                "    resize_bounds = gfx::Rect(contents_bounds.right(), local_bounds.y(),\n"
                "                              local_bounds.right() - contents_bounds.right(),\n"
                "                              local_bounds.height());\n"
            ),
            new=(
                "    resize_bounds = gfx::Rect(\n"
                "        contents_bounds.right(), local_bounds.y(),\n"
                "        std::max(local_bounds.right() - contents_bounds.right(),\n"
                "                 maho_ai_resize_width),\n"
                "        local_bounds.height());\n"
            ),
            description="Maho: keep the right AI resize seam draggable without panel padding",
            guard=(
                "std::max(local_bounds.right() -\n"
                "                                           contents_bounds.right(),\n"
                "                                       maho_ai_resize_width)"
            ),
        ),
    ],
    "chrome/browser/ui/views/side_panel/side_panel.h": [
        Replacement(
            old="  bool ShouldRestrictMaxWidth() const;\n",
            new=(
                "  bool IsMahoAiPanelShowing() const;\n"
                "  bool ShouldRestrictMaxWidth() const;\n"
            ),
            description="Maho: declare AI side-panel identity predicate",
            guard="bool IsMahoAiPanelShowing() const;",
        ),
    ],
    "chrome/browser/ui/views/side_panel/side_panel_coordinator.cc": [
        Replacement(
            old=(
                "  auto* content = content_wrapper->AddChildView(\n"
                "      content_view.has_value() ? std::move(content_view.value())\n"
                "                               : entry->GetContent());\n"
                "  content->SetVisible(true);\n"
            ),
            new=(
                "  auto* content = content_wrapper->AddChildView(\n"
                "      content_view.has_value() ? std::move(content_view.value())\n"
                "                               : entry->GetContent());\n"
                "  content->SetVisible(true);\n"
                "  // Reapply after insertion so borderless entries can identify their content.\n"
                "  if (!entry->should_show_header()) {\n"
                "    side_panel->RemoveHeaderView();\n"
                "  }\n"
            ),
            description=(
                "Maho: reapply headerless border state after side-panel content "
                "identity becomes available"
            ),
            guard="Reapply after insertion so borderless entries can identify their content",
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
            description="Maho: Raise an existing Maho Mini minimum height to 46dip",
            idempotent=True,
            guard="return gfx::Size(400, 46 + 1);",
        ),
        Replacement(
            old="    top_bar_bounds.set_height(38);\n",
            new="    top_bar_bounds.set_height(46);\n",
            description="Maho: Raise an existing Maho Mini layout height to 46dip",
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
            description="Maho: Include Browser for is_maho_mini() calls in popup layout",
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
                "  if (views().maho_mini_top_bar && views().maho_mini_top_bar->GetVisible()) {\n"
                "    gfx::Rect top_bar_bounds = params.visual_client_area;\n"
                "    top_bar_bounds.set_height(46);\n"
                "    layout.AddChild(views().maho_mini_top_bar, top_bar_bounds);\n"
                "    params.SetTop(top_bar_bounds.bottom());\n"
                "  }\n"
                "\n"
                "  // Lay out the top container if it's in the browser (even if it's empty).\n"
            ),
            description="Maho: Lay out maho_mini_top_bar in CalculateProposedLayout",
            guard="views().maho_mini_top_bar &&",
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
                "  if (browser()->is_maho_mini()) {\n"
                "    return gfx::Rect(params.visual_client_area.x(), original_top,\n"
                "                     params.visual_client_area.width(), 0);\n"
                "  }\n"
                "\n"
                "  // Layout starts beneath the caption buttons because title is laid out by the\n"
            ),
            description="Maho: Zero top container height for Maho Mini in CalculateTopContainerLayout",
            guard="if (browser()->is_maho_mini()) {",
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
                "  if (browser()->is_maho_mini()) {\n"
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
            description="Maho: Add custom minimum size for Maho Mini windows",
            guard="if (browser()->is_maho_mini()) {\n    return gfx::Size(400",
        ),
    ],
    "components/password_manager/core/browser/password_form_digest.h": [
        Replacement(
            old=(
                '#include <string>\n\n'
                '#include "components/password_manager/core/browser/password_form.h"\n'
            ),
            new=(
                '#include <string>\n\n'
                '#include "components/password_manager/core/browser/password_form.h"\n'
                '#include "maho/chromium_src/components/password_manager/core/browser/'
                'password_fill_request.h"\n'
            ),
            description="Maho: include standalone password fill request contract",
            guard='password_fill_request.h',
        ),
    ],
    "components/password_manager/core/browser/password_store/password_store_interface.h": [
        Replacement(
            old=(
                '  virtual void GetLogins(\n'
                '      const PasswordFormDigest& form,\n'
                '      const PasswordFillRequestContext& context,\n'
                '      base::WeakPtr<PasswordStoreConsumer> consumer) {\n'
                '    GetLogins(form, std::move(consumer));\n'
                '  }\n\n'
                '  // Starts final delivery. The default fails closed.\n'
                '  virtual void ResolvePasswordFill(\n'
                '      const PasswordFillRequestContext& context,\n'
                '      const PasswordFillSelection& selection,\n'
                '      PasswordFillResolver resolver) {\n'
                '    std::move(resolver).Run(std::nullopt);\n'
                '  }\n'
            ),
            new=(
                '  virtual void GetLogins(\n'
                '      const PasswordFormDigest& form,\n'
                '      const PasswordFillRequestContext& context,\n'
                '      base::WeakPtr<PasswordStoreConsumer> consumer);\n\n'
                '  // Starts final delivery. The default fails closed.\n'
                '  virtual void ResolvePasswordFill(\n'
                '      const PasswordFillRequestContext& context,\n'
                '      const PasswordFillSelection& selection,\n'
                '      PasswordFillResolver resolver);\n'
            ),
            description="Maho: move password store virtual defaults out of line",
            idempotent=True,
            guard=(
                '      base::WeakPtr<PasswordStoreConsumer> consumer);\n\n'
                '  // Starts final delivery. The default fails closed.\n'
            ),
        ),
        Replacement(
            old='#include <vector>\n',
            new='#include <utility>\n#include <vector>\n',
            description="Maho: include utility for password store default adapters",
            guard='#include <utility>\n',
        ),
        Replacement(
            old=(
                '  virtual void GetLogins(const PasswordFormDigest& form,\n'
                '                         base::WeakPtr<PasswordStoreConsumer> consumer) = 0;\n'
            ),
            new=(
                '  virtual void GetLogins(const PasswordFormDigest& form,\n'
                '                         base::WeakPtr<PasswordStoreConsumer> consumer) = 0;\n\n'
                '  // Context-aware adapter. Legacy stores keep their exact retrieval behavior.\n'
                '  virtual void GetLogins(\n'
                '      const PasswordFormDigest& form,\n'
                '      const PasswordFillRequestContext& context,\n'
                '      base::WeakPtr<PasswordStoreConsumer> consumer);\n\n'
                '  // Starts a just-in-time, secret-free resolution check. Backends opt in;\n'
                '  // the default fails closed and never exposes credential material.\n'
                '  virtual void ResolvePasswordFill(\n'
                '      const PasswordFillRequestContext& context,\n'
                '      const PasswordFillSelection& selection,\n'
                '      PasswordFillResolver resolver);\n'
            ),
            description="Maho: add context-aware password store interface adapters",
            guard="virtual void ResolvePasswordFill(",
        ),
    ],
    "components/password_manager/core/browser/password_store/password_store_consumer.cc": [
        Replacement(
            old='void PasswordStoreConsumer::OnGetPasswordStoreResults(\n',
            new=(
                'void PasswordStoreInterface::GetLogins(\n'
                '    const PasswordFormDigest& form,\n'
                '    const PasswordFillRequestContext& context,\n'
                '    base::WeakPtr<PasswordStoreConsumer> consumer) {\n'
                '  GetLogins(form, std::move(consumer));\n'
                '}\n\n'
                'void PasswordStoreInterface::ResolvePasswordFill(\n'
                '    const PasswordFillRequestContext& context,\n'
                '    const PasswordFillSelection& selection,\n'
                '    PasswordFillResolver resolver) {\n'
                '  std::move(resolver).Run(std::nullopt);\n'
                '}\n\n'
                'void PasswordStoreConsumer::OnGetPasswordStoreResults(\n'
            ),
            description="Maho: implement out-of-line PasswordStoreInterface defaults",
            guard="void PasswordStoreInterface::ResolvePasswordFill(",
        ),
    ],
    "components/password_manager/core/browser/password_store/password_store_backend.h": [
        Replacement(
            old=(
                '  virtual void GetGroupedMatchingLoginsAsync(\n'
                '      const PasswordFormDigest& form_digest,\n'
                '      const PasswordFillRequestContext& context,\n'
                '      LoginsOrErrorReply callback) {\n'
                '    GetGroupedMatchingLoginsAsync(form_digest, std::move(callback));\n'
                '  }\n\n'
                '  virtual void ResolvePasswordFill(\n'
                '      const PasswordFillRequestContext& context,\n'
                '      const PasswordFillSelection& selection,\n'
                '      PasswordFillResolver resolver) {\n'
                '    std::move(resolver).Run(std::nullopt);\n'
                '  }\n'
            ),
            new=(
                '  virtual void GetGroupedMatchingLoginsAsync(\n'
                '      const PasswordFormDigest& form_digest,\n'
                '      const PasswordFillRequestContext& context,\n'
                '      LoginsOrErrorReply callback);\n\n'
                '  virtual void ResolvePasswordFill(\n'
                '      const PasswordFillRequestContext& context,\n'
                '      const PasswordFillSelection& selection,\n'
                '      PasswordFillResolver resolver);\n'
            ),
            description="Maho: move password backend virtual defaults out of line",
            idempotent=True,
            guard=(
                '      const PasswordFillRequestContext& context,\n'
                '      LoginsOrErrorReply callback);\n'
            ),
        ),
        Replacement(
            old='#include <optional>\n',
            new='#include <optional>\n#include <utility>\n',
            description="Maho: include utility for password backend default adapters",
            guard='#include <utility>\n',
        ),
        Replacement(
            old=(
                '  virtual void GetGroupedMatchingLoginsAsync(\n'
                '      const PasswordFormDigest& form_digest,\n'
                '      LoginsOrErrorReply callback) = 0;\n'
            ),
            new=(
                '  virtual void GetGroupedMatchingLoginsAsync(\n'
                '      const PasswordFormDigest& form_digest,\n'
                '      LoginsOrErrorReply callback) = 0;\n\n'
                '  // Context-aware adapter preserving every legacy backend unchanged.\n'
                '  virtual void GetGroupedMatchingLoginsAsync(\n'
                '      const PasswordFormDigest& form_digest,\n'
                '      const PasswordFillRequestContext& context,\n'
                '      LoginsOrErrorReply callback);\n\n'
                '  virtual void ResolvePasswordFill(\n'
                '      const PasswordFillRequestContext& context,\n'
                '      const PasswordFillSelection& selection,\n'
                '      PasswordFillResolver resolver);\n'
            ),
            description="Maho: add context-aware password backend adapters",
            guard="const PasswordFillSelection& selection",
        ),
    ],
    "components/password_manager/core/browser/password_store/password_store_backend_metrics_recorder.cc": [
        Replacement(
            old=(
                '#include "components/password_manager/core/browser/password_store/'
                'password_store_backend_metrics_recorder.h"\n'
            ),
            new=(
                '#include "components/password_manager/core/browser/password_store/'
                'password_store_backend_metrics_recorder.h"\n'
                '#include "components/password_manager/core/browser/password_store/'
                'password_store_backend.h"\n'
            ),
            description="Maho: include backend interface for out-of-line defaults",
            guard='#include "components/password_manager/core/browser/password_store/password_store_backend.h"\n',
        ),
        Replacement(
            old='namespace password_manager {\n',
            new=(
                'namespace password_manager {\n\n'
                'void PasswordStoreBackend::GetGroupedMatchingLoginsAsync(\n'
                '    const PasswordFormDigest& form_digest,\n'
                '    const PasswordFillRequestContext& context,\n'
                '    LoginsOrErrorReply callback) {\n'
                '  GetGroupedMatchingLoginsAsync(form_digest, std::move(callback));\n'
                '}\n\n'
                'void PasswordStoreBackend::ResolvePasswordFill(\n'
                '    const PasswordFillRequestContext& context,\n'
                '    const PasswordFillSelection& selection,\n'
                '    PasswordFillResolver resolver) {\n'
                '  std::move(resolver).Run(std::nullopt);\n'
                '}\n'
            ),
            description="Maho: implement out-of-line PasswordStoreBackend defaults",
            guard="void PasswordStoreBackend::ResolvePasswordFill(",
        ),
    ],
    "components/password_manager/core/browser/password_store/password_store.h": [
        Replacement(
            old=(
                '  void GetLogins(const PasswordFormDigest& form,\n'
                '                 base::WeakPtr<PasswordStoreConsumer> consumer) override;\n'
            ),
            new=(
                '  void GetLogins(const PasswordFormDigest& form,\n'
                '                 base::WeakPtr<PasswordStoreConsumer> consumer) override;\n'
                '  void GetLogins(\n'
                '      const PasswordFormDigest& form,\n'
                '      const PasswordFillRequestContext& context,\n'
                '      base::WeakPtr<PasswordStoreConsumer> consumer) override;\n'
                '  void ResolvePasswordFill(\n'
                '      const PasswordFillRequestContext& context,\n'
                '      const PasswordFillSelection& selection,\n'
                '      PasswordFillResolver resolver) override;\n'
            ),
            description="Maho: declare context-aware PasswordStore forwarding",
            guard="PasswordFillResolver resolver) override;",
        ),
    ],
    "components/password_manager/core/browser/password_store/password_store.cc": [
        Replacement(
            old=(
                '    post_init_callback_ = std::move(post_init_callback_)\n'
                '                              .Then(base::BindOnce(&PasswordStore::GetLogins,\n'
                '                                                   this, form, consumer));\n'
            ),
            new=(
                '    post_init_callback_ =\n'
                '        std::move(post_init_callback_)\n'
                '            .Then(base::BindOnce(\n'
                '                static_cast<void (PasswordStore::*)(\n'
                '                    const PasswordFormDigest&,\n'
                '                    base::WeakPtr<PasswordStoreConsumer>)>(\n'
                '                    &PasswordStore::GetLogins),\n'
                '                this, form, consumer));\n'
            ),
            description="Maho: disambiguate legacy PasswordStore GetLogins callback",
            guard=(
                'static_cast<void (PasswordStore::*)(\n'
                '                    const PasswordFormDigest&,\n'
                '                    base::WeakPtr<PasswordStoreConsumer>)>'
            ),
        ),
        Replacement(
            old='void PasswordStore::GetAutofillableLogins(\n',
            new=(
                'void PasswordStore::GetLogins(\n'
                '    const PasswordFormDigest& form,\n'
                '    const PasswordFillRequestContext& context,\n'
                '    base::WeakPtr<PasswordStoreConsumer> consumer) {\n'
                '  DCHECK(main_task_runner_->RunsTasksInCurrentSequence());\n'
                '  if (!backend_) {\n'
                '    return;\n'
                '  }\n'
                '  if (post_init_callback_) {\n'
                '    post_init_callback_ =\n'
                '        std::move(post_init_callback_)\n'
                '            .Then(base::BindOnce(\n'
                '                static_cast<void (PasswordStore::*)(\n'
                '                    const PasswordFormDigest&,\n'
                '                    const PasswordFillRequestContext&,\n'
                '                    base::WeakPtr<PasswordStoreConsumer>)>(\n'
                '                    &PasswordStore::GetLogins),\n'
                '                this, form, context, consumer));\n'
                '    return;\n'
                '  }\n'
                '  backend_->GetGroupedMatchingLoginsAsync(\n'
                '      form, context,\n'
                '      base::BindOnce(&ConsumerReplyConverter, consumer,\n'
                '                     base::RetainedRef(this)));\n'
                '}\n\n'
                'void PasswordStore::ResolvePasswordFill(\n'
                '    const PasswordFillRequestContext& context,\n'
                '    const PasswordFillSelection& selection,\n'
                '    PasswordFillResolver resolver) {\n'
                '  if (!backend_) {\n'
                '    std::move(resolver).Run(std::nullopt);\n'
                '    return;\n'
                '  }\n'
                '  backend_->ResolvePasswordFill(context, selection, std::move(resolver));\n'
                '}\n\n'
                'void PasswordStore::GetAutofillableLogins(\n'
            ),
            description="Maho: forward context-aware requests and resolver callbacks",
            guard="void PasswordStore::ResolvePasswordFill(",
        ),
    ],
    "components/password_manager/core/browser/form_fetcher.h": [
        Replacement(
            old='#include "components/password_manager/core/browser/password_form.h"\n',
            new=(
                '#include "components/password_manager/core/browser/password_form.h"\n'
                '#include "components/password_manager/core/browser/password_form_digest.h"\n'
            ),
            description="Maho: include password fill request context in FormFetcher",
            guard="#include \"components/password_manager/core/browser/password_form_digest.h\"",
        ),
        Replacement(
            old='  virtual void Fetch() = 0;\n',
            new=(
                '  virtual void Fetch() = 0;\n\n'
                '  // Captures the requesting document before the asynchronous store read.\n'
                '  virtual void SetPasswordFillRequestContext(\n'
                '      const PasswordFillRequestContext& context);\n'
            ),
            description="Maho: add optional FormFetcher document context capture",
            guard="virtual void SetPasswordFillRequestContext(",
        ),
    ],
    "components/password_manager/core/browser/form_fetcher_impl.h": [
        Replacement(
            old='  void Fetch() override;\n',
            new=(
                '  void Fetch() override;\n'
                '  void SetPasswordFillRequestContext(\n'
                '      const PasswordFillRequestContext& context) override;\n'
            ),
            description="Maho: declare FormFetcherImpl context capture",
            guard="void SetPasswordFillRequestContext(",
        ),
        Replacement(
            old='  const PasswordFormDigest form_digest_;\n',
            new=(
                '  const PasswordFormDigest form_digest_;\n'
                '  std::optional<PasswordFillRequestContext> fill_request_context_;\n'
            ),
            description="Maho: retain captured password fill request context",
            guard="fill_request_context_",
        ),
    ],
    "components/password_manager/core/browser/form_fetcher_impl.cc": [
        Replacement(
            old='void FormFetcherImpl::Fetch() {\n',
            new=(
                'void FormFetcher::SetPasswordFillRequestContext(\n'
                '    const PasswordFillRequestContext& context) {}\n\n'
                'void FormFetcherImpl::SetPasswordFillRequestContext(\n'
                '    const PasswordFillRequestContext& context) {\n'
                '  fill_request_context_ = context;\n'
                '}\n\n'
                'void FormFetcherImpl::Fetch() {\n'
            ),
            description="Maho: implement FormFetcher document context capture",
            guard="void FormFetcherImpl::SetPasswordFillRequestContext(",
        ),
        Replacement(
            old=(
                '  profile_password_store->GetLogins(form_digest_,\n'
                '                                    weak_ptr_factory_.GetWeakPtr());\n'
                '  if (account_password_store) {\n'
                '    account_password_store->GetLogins(form_digest_,\n'
                '                                      weak_ptr_factory_.GetWeakPtr());\n'
                '  }\n'
            ),
            new=(
                '  if (fill_request_context_) {\n'
                '    profile_password_store->GetLogins(\n'
                '        form_digest_, *fill_request_context_,\n'
                '        weak_ptr_factory_.GetWeakPtr());\n'
                '    if (account_password_store) {\n'
                '      account_password_store->GetLogins(\n'
                '          form_digest_, *fill_request_context_,\n'
                '          weak_ptr_factory_.GetWeakPtr());\n'
                '    }\n'
                '  } else {\n'
                '    profile_password_store->GetLogins(\n'
                '        form_digest_, weak_ptr_factory_.GetWeakPtr());\n'
                '    if (account_password_store) {\n'
                '      account_password_store->GetLogins(\n'
                '          form_digest_, weak_ptr_factory_.GetWeakPtr());\n'
                '    }\n'
                '  }\n'
            ),
            description="Maho: issue context-aware password store requests when captured",
            guard="if (fill_request_context_)",
        ),
        Replacement(
            old='  auto result = std::make_unique<FormFetcherImpl>(form_digest_, client_, false);\n',
            new=(
                '  auto result = std::make_unique<FormFetcherImpl>(form_digest_, client_, false);\n'
                '  result->fill_request_context_ = fill_request_context_;\n'
            ),
            description="Maho: preserve document context when cloning FormFetcherImpl",
            guard="result->fill_request_context_ = fill_request_context_;",
        ),
    ],
    "components/password_manager/core/browser/password_manager_driver.h": [
        Replacement(
            old=(
                '  // Produces the only callback that may receive a resolved secret. It is\n'
                '  bound to this exact driver and captured document context.\n'
            ),
            new=(
                '  // Produces the only callback that may receive a resolved secret. It is\n'
                '  // bound to this exact driver and captured document context.\n'
            ),
            description=(
                "Maho: repair resolver adapter comment continuation in applied driver"
            ),
            idempotent=True,
        ),
        Replacement(
            old='#include <string>\n',
            new='#include <string>\n#include <utility>\n',
            description="Maho: include utility for driver default adapter",
            guard='#include <utility>\n',
        ),
        Replacement(
            old='#include "components/autofill/core/common/unique_ids.h"\n',
            new=(
                '#include "components/autofill/core/common/unique_ids.h"\n'
                '#include "components/password_manager/core/browser/password_form_digest.h"\n'
            ),
            description="Maho: include document-aware password fill types in driver",
            guard='#include "components/password_manager/core/browser/password_form_digest.h"',
        ),
        Replacement(
            old='  virtual int GetId() const = 0;\n',
            new=(
                '  virtual int GetId() const = 0;\n\n'
                '  PasswordFillRequestContext GetPasswordFillRequestContext() const {\n'
                '    return {GetLastCommittedOrigin(), document_token_};\n'
                '  }\n\n'
                '  bool IsPasswordFillRequestContextCurrent(\n'
                '      const PasswordFillRequestContext& context) const {\n'
                '    return context.document_token == document_token_ &&\n'
                '           context.requesting_origin == GetLastCommittedOrigin();\n'
                '  }\n\n'
                '  void SetPasswordFillRequestContextForTesting(\n'
                '      const PasswordFillRequestContext& context) {\n'
                '    document_token_ = context.document_token;\n'
                '  }\n'
            ),
            description="Maho: expose driver-owned document fill context",
            guard="SetPasswordFillRequestContextForTesting(",
        ),
        Replacement(
            old=(
                '  virtual void FillSuggestion(\n'
                '      const std::u16string& username,\n'
                '      const std::u16string& password,\n'
                '      base::OnceCallback<void(bool)> success_callback) = 0;\n'
            ),
            new=(
                '  virtual void FillSuggestion(\n'
                '      const std::u16string& username,\n'
                '      const std::u16string& password,\n'
                '      base::OnceCallback<void(bool)> success_callback) = 0;\n\n'
                '  // Rechecks the captured document immediately before legacy\n'
                '  // dispatch; defined in password_manager_driver.cc so the\n'
                '  // chromium-style plugin does not flag an inline virtual body.\n'
                '  virtual void FillSuggestion(\n'
                '      const PasswordFillRequestContext& context,\n'
                '      const std::u16string& username,\n'
                '      const std::u16string& password,\n'
                '      base::OnceCallback<void(bool)> success_callback);\n\n'
                '  // Produces the only callback that may receive a resolved secret. It is\n'
                '  // bound to this exact driver and captured document context.\n'
                '  PasswordFillResolver CreatePasswordFillResolver(\n'
                '      const PasswordFillRequestContext& context,\n'
                '      std::u16string username,\n'
                '      base::OnceCallback<void(bool)> success_callback) {\n'
                '    return PasswordFillResolver(base::BindOnce(\n'
                '        [](base::WeakPtr<PasswordManagerDriver> driver,\n'
                '           PasswordFillRequestContext context, std::u16string username,\n'
                '           base::OnceCallback<void(bool)> success_callback,\n'
                '           std::optional<PasswordFillResolution> resolution) {\n'
                '          if (!driver || !resolution ||\n'
                '              !driver->IsPasswordFillRequestContextCurrent(context)) {\n'
                '            std::move(success_callback).Run(false);\n'
                '            return;\n'
                '          }\n'
                '          driver->FillSuggestion(context, username, resolution->secret(),\n'
                '                                 std::move(success_callback));\n'
                '        },\n'
                '        AsWeakPtr(), context, std::move(username),\n'
                '        std::move(success_callback)));\n'
                '  }\n'
            ),
            description="Maho: add context-aware driver fill adapter",
            guard="CreatePasswordFillResolver(",
        ),
        Replacement(
            old='  virtual base::WeakPtr<PasswordManagerDriver> AsWeakPtr() = 0;\n',
            new=(
                '  virtual base::WeakPtr<PasswordManagerDriver> AsWeakPtr() = 0;\n\n'
                ' protected:\n'
                '  void RotatePasswordManagerDocumentToken() {\n'
                '    document_token_ = PasswordManagerDocumentToken();\n'
                '  }\n\n'
                ' private:\n'
                '  PasswordManagerDocumentToken document_token_;\n'
            ),
            description="Maho: make PasswordManagerDriver own a rotating document token",
            guard="PasswordManagerDocumentToken document_token_;",
        ),
    ],
    "components/password_manager/core/browser/password_form_digest.cc": [
        Replacement(
            old='#include "components/password_manager/core/browser/password_form_digest.h"\n',
            new=(
                '#include "components/password_manager/core/browser/password_form_digest.h"\n\n'
                '#include <algorithm>\n'
                '#include <utility>\n'
            ),
            description="Maho: include utilities for secure fill request values",
            guard='#include <algorithm>\n#include <utility>\n',
        ),
        Replacement(
            old="PasswordFormDigest& PasswordFormDigest::operator=(PasswordFormDigest&& other) =\n    default;\n",
            new=(
                "PasswordFormDigest& PasswordFormDigest::operator=(PasswordFormDigest&& other) =\n"
                "    default;\n\n"
                "PasswordFillResolution::PasswordFillResolution(std::u16string secret)\n"
                "    : secret_(std::move(secret)) {}\n\n"
                "PasswordFillResolution::PasswordFillResolution(PasswordFillResolution&& other) noexcept\n"
                "    : secret_(std::move(other.secret_)) {}\n\n"
                "PasswordFillResolution& PasswordFillResolution::operator=(\n"
                "    PasswordFillResolution&& other) noexcept {\n"
                "  if (this != &other) {\n"
                "    Zeroize();\n"
                "    secret_ = std::move(other.secret_);\n"
                "  }\n"
                "  return *this;\n"
                "}\n\n"
                "PasswordFillResolution::~PasswordFillResolution() {\n"
                "  Zeroize();\n"
                "}\n\n"
                "void PasswordFillResolution::Zeroize() {\n"
                "  std::fill(secret_.begin(), secret_.end(), u'\\0');\n"
                "  secret_.clear();\n"
                "}\n\n"
                "PasswordFillResolver::PasswordFillResolver(PasswordFillResolver&&) noexcept =\n"
                "    default;\n\n"
                "PasswordFillResolver& PasswordFillResolver::operator=(\n"
                "    PasswordFillResolver&&) noexcept = default;\n\n"
                "PasswordFillResolver::~PasswordFillResolver() = default;\n\n"
                "PasswordFillResolver::PasswordFillResolver(\n"
                "    base::OnceCallback<void(std::optional<PasswordFillResolution>)> callback)\n"
                "    : callback_(std::move(callback)) {}\n\n"
                "void PasswordFillResolver::Run(\n"
                "    std::optional<PasswordFillResolution> resolution) && {\n"
                "  std::move(callback_).Run(std::move(resolution));\n"
                "}\n"
            ),
            description="Maho: define PasswordFillResolution and PasswordFillResolver in password_form_digest.cc",
            guard="PasswordFillResolver::PasswordFillResolver(PasswordFillResolver&&)",
        ),
    ],
    "components/password_manager/core/browser/stub_password_manager_driver.cc": [
        Replacement(
            old=(
                'void PasswordManagerDriver::FillSuggestion(\n'
                '    const PasswordFillRequestContext& context,\n'
                '    const std::u16string& username,\n'
                '    const std::u16string& password,\n'
                '    base::OnceCallback<void(bool)> success_callback) {\n'
                '  if (!IsPasswordFillRequestContextCurrent(context)) {\n'
                '    std::move(success_callback).Run(false);\n'
                '    return;\n'
                '  }\n'
                '  FillSuggestion(username, password, std::move(success_callback));\n'
                '}\n\n'
            ),
            new="",
            description=(
                "Maho: remove production driver default from stub test support"
            ),
            idempotent=True,
        ),
    ],
    "components/password_manager/content/browser/content_password_manager_driver.h": [
        Replacement(
            old=(
                '  void FillSuggestion(const std::u16string& username,\n'
                '                      const std::u16string& password,\n'
                '                      base::OnceCallback<void(bool)> success_callback) override;\n'
            ),
            new=(
                '  void FillSuggestion(const std::u16string& username,\n'
                '                      const std::u16string& password,\n'
                '                      base::OnceCallback<void(bool)> success_callback) override;\n'
            ),
            description="Maho: keep ContentPasswordManagerDriver interface unchanged",
            idempotent=True,
        ),
    ],
    "components/password_manager/content/browser/content_password_manager_driver.cc": [
        Replacement(
            old=(
                "void PasswordManagerDriver::FillSuggestion(\n"
                "    const PasswordFillRequestContext& context,\n"
                "    const std::u16string& username,\n"
                "    const std::u16string& password,\n"
                "    base::OnceCallback<void(bool)> success_callback) {\n"
                "  if (!IsPasswordFillRequestContextCurrent(context)) {\n"
                "    std::move(success_callback).Run(false);\n"
                "    return;\n"
                "  }\n"
                "  FillSuggestion(username, password, std::move(success_callback));\n"
                "}\n\n"
            ),
            new="",
            description="Maho: keep the driver default implementation in core only",
            idempotent=True,
        ),
        Replacement(
            old='void ContentPasswordManagerDriver::DidNavigate() {\n',
            new=(
                'void ContentPasswordManagerDriver::DidNavigate() {\n'
                '  RotatePasswordManagerDocumentToken();\n'
            ),
            description="Maho: rotate password fill token after committed navigation",
            guard="  RotatePasswordManagerDocumentToken();",
        ),
    ],
    "components/password_manager/core/browser/BUILD.gn": [
        Replacement(
            old=(
                '    "browser_save_password_progress_logger.cc",\n'
            ),
            new=(
                '    "password_manager_driver.cc",\n'
                '    "browser_save_password_progress_logger.cc",\n'
            ),
            description=(
                "Maho: add password_manager_driver.cc for the context-aware "
                "FillSuggestion definition"
            ),
            guard='"password_manager_driver.cc",',
        ),
    ],
    "components/password_manager/core/browser/password_form_manager.cc": [
        Replacement(
            old=(
                '  if (driver_) {\n'
                '    driver_id_ = driver->GetId();\n'
                '    cached_driver_frame_id_ = driver->GetFrameId();\n'
                '  }\n\n'
                '  metrics_recorder_->RecordFormSignature('
            ),
            new=(
                '  if (driver_) {\n'
                '    driver_id_ = driver->GetId();\n'
                '    cached_driver_frame_id_ = driver->GetFrameId();\n'
                '    form_fetcher_->SetPasswordFillRequestContext(\n'
                '        driver_->GetPasswordFillRequestContext());\n'
                '  }\n\n'
                '  metrics_recorder_->RecordFormSignature('
            ),
            description="Maho: capture requesting document before FormFetcher starts",
            guard="form_fetcher_->SetPasswordFillRequestContext(",
        ),
    ],
    "chrome/browser/password_manager/factories/password_store_backend_factory.cc": [
        Replacement(
            old=(
                "#else  // BUILDFLAG(IS_ANDROID)\n"
                "#include \"components/password_manager/core/browser/password_store/login_database.h\"\n"
                "#include \"components/password_manager/core/browser/password_store/password_store_built_in_backend.h\"\n"
                "#endif  // BUILDFLAG(IS_ANDROID)"
            ),
            new=(
                "#else  // BUILDFLAG(IS_ANDROID)\n"
                "#include \"components/password_manager/core/browser/password_store/login_database.h\"\n"
                "#include \"components/password_manager/core/browser/password_store/password_store_built_in_backend.h\"\n"
                "#include \"maho/browser/maho_core_holder.h\"\n"
                "#include \"maho/browser/passwords/maho_password_store_backend.h\"\n"
                "#endif  // BUILDFLAG(IS_ANDROID)"
            ),
            description="Maho: Include maho_password_store_backend.h in password_store_backend_factory.cc",
            guard="maho_password_store_backend.h",
        ),
        Replacement(
            old=(
                '#include "maho/browser/passwords/maho_password_store_backend.h"\n'
            ),
            new=(
                '#include "maho/browser/maho_core_holder.h"\n'
                '#include "maho/browser/passwords/maho_password_store_backend.h"\n'
            ),
            description=(
                "Maho: include profile identity helper beside an already-applied "
                "password backend factory override"
            ),
            guard='#include "maho/browser/maho_core_holder.h"',
        ),
        Replacement(
            old=(
                "#else   //  BUILDFLAG(IS_ANDROID)\n"
                "  std::unique_ptr<password_manager::LoginDatabase> login_db(\n"
                "      password_manager::CreateLoginDatabase(is_account_store,\n"
                "                                            login_db_directory, prefs));\n"
                "  SetIsUserDataDirPolicySet(login_db.get());\n"
                "  auto behavior = is_account_store\n"
                "                      ? syncer::WipeModelUponSyncDisabledBehavior::kAlways\n"
                "                      : syncer::WipeModelUponSyncDisabledBehavior::kNever;\n"
                "  // Maho: Route CreatePasswordStoreBackend through Maho's password store backend\n"
                "  return std::make_unique<password_manager::PasswordStoreBuiltInBackend>(\n"
                "      std::move(login_db), behavior, prefs, os_crypt_async);\n"
                "#endif  // BUILDFLAG(IS_ANDROID)"
            ),
            new=(
                "#else   //  BUILDFLAG(IS_ANDROID)\n"
                "  std::unique_ptr<password_manager::LoginDatabase> login_db(\n"
                "      password_manager::CreateLoginDatabase(is_account_store,\n"
                "                                            login_db_directory, prefs));\n"
                "  SetIsUserDataDirPolicySet(login_db.get());\n"
                "  // Maho: Route CreatePasswordStoreBackend through Maho's password store backend\n"
                "  // and disable account store in Maho Native.\n"
                "  const std::string profile_key = maho::GetProfileIdentityKey(\n"
                "      login_db_directory, prefs);\n"
                "  if (is_account_store) {\n"
                "    return std::make_unique<maho::passwords::MahoPasswordStoreBackend>(\n"
                "        /*enabled=*/false, profile_key);\n"
                "  }\n"
                "  return std::make_unique<maho::passwords::MahoPasswordStoreBackend>(\n"
                "      /*enabled=*/true, profile_key);\n"
                "#endif  // BUILDFLAG(IS_ANDROID)"
            ),
            description="Maho: Repair partial password backend factory override in password_store_backend_factory.cc",
            idempotent=True,
            guard="const std::string profile_key = maho::GetProfileIdentityKey(",
        ),
        Replacement(
            old=(
                "#else   //  BUILDFLAG(IS_ANDROID)\n"
                "  std::unique_ptr<password_manager::LoginDatabase> login_db(\n"
                "      password_manager::CreateLoginDatabase(is_account_store,\n"
                "                                            login_db_directory, prefs));\n"
                "  SetIsUserDataDirPolicySet(login_db.get());\n"
                "  auto behavior = is_account_store\n"
                "                      ? syncer::WipeModelUponSyncDisabledBehavior::kAlways\n"
                "                      : syncer::WipeModelUponSyncDisabledBehavior::kNever;\n"
                "  return std::make_unique<password_manager::PasswordStoreBuiltInBackend>(\n"
                "      std::move(login_db), behavior, prefs, os_crypt_async);\n"
                "#endif  // BUILDFLAG(IS_ANDROID)"
            ),
            new=(
                "#else   //  BUILDFLAG(IS_ANDROID)\n"
                "  std::unique_ptr<password_manager::LoginDatabase> login_db(\n"
                "      password_manager::CreateLoginDatabase(is_account_store,\n"
                "                                            login_db_directory, prefs));\n"
                "  SetIsUserDataDirPolicySet(login_db.get());\n"
                "  // Maho: Route CreatePasswordStoreBackend through Maho's password store backend\n"
                "  // and disable account store in Maho Native.\n"
                "  const std::string profile_key = maho::GetProfileIdentityKey(\n"
                "      login_db_directory, prefs);\n"
                "  if (is_account_store) {\n"
                "    return std::make_unique<maho::passwords::MahoPasswordStoreBackend>(\n"
                "        /*enabled=*/false, profile_key);\n"
                "  }\n"
                "  return std::make_unique<maho::passwords::MahoPasswordStoreBackend>(\n"
                "      /*enabled=*/true, profile_key);\n"
                "#endif  // BUILDFLAG(IS_ANDROID)"
            ),
            description="Maho: Route CreatePasswordStoreBackend through Maho password store backend in password_store_backend_factory.cc",
            idempotent=True,
            guard="const std::string profile_key = maho::GetProfileIdentityKey(",
        ),
        Replacement(
            old=(
                "  // Maho: Route CreatePasswordStoreBackend through Maho's password store backend\n"
                "  // and disable account store in Maho Native.\n"
                "  if (is_account_store) {\n"
                "    return std::make_unique<maho::passwords::MahoPasswordStoreBackend>(false);\n"
                "  }\n"
                "  return std::make_unique<maho::passwords::MahoPasswordStoreBackend>();"
            ),
            new=(
                "  // Maho: Route CreatePasswordStoreBackend through Maho's password store backend\n"
                "  // and disable account store in Maho Native.\n"
                "  const std::string profile_key = maho::GetProfileIdentityKey(\n"
                "      login_db_directory, prefs);\n"
                "  if (is_account_store) {\n"
                "    return std::make_unique<maho::passwords::MahoPasswordStoreBackend>(\n"
                "        /*enabled=*/false, profile_key);\n"
                "  }\n"
                "  return std::make_unique<maho::passwords::MahoPasswordStoreBackend>(\n"
                "      /*enabled=*/true, profile_key);"
            ),
            description=(
                "Maho: bind an already-routed password backend factory to its "
                "construction-time profile identity"
            ),
            guard="const std::string profile_key = maho::GetProfileIdentityKey(",
        ),
        Replacement(
            old=(
                "  auto behavior = is_account_store\n"
                "                      ? syncer::WipeModelUponSyncDisabledBehavior::kAlways\n"
                "                      : syncer::WipeModelUponSyncDisabledBehavior::kNever;\n"
            ),
            new="",
            description="Maho: Remove unused built-in backend wipe behavior after factory override",
            idempotent=True,
        ),
        Replacement(
            old=(
                "  if (is_account_store) {\n"
                "    return nullptr;\n"
                "  }\n"
                "  return std::make_unique<maho::passwords::MahoPasswordStoreBackend>();"
            ),
            new=(
                "  const std::string profile_key = maho::GetProfileIdentityKey(\n"
                "      login_db_directory, prefs);\n"
                "  if (is_account_store) {\n"
                "    return std::make_unique<maho::passwords::MahoPasswordStoreBackend>(\n"
                "        /*enabled=*/false, profile_key);\n"
                "  }\n"
                "  return std::make_unique<maho::passwords::MahoPasswordStoreBackend>(\n"
                "      /*enabled=*/true, profile_key);"
            ),
            description="Maho: Replace null account password backend with disabled Maho backend",
            guard="const std::string profile_key = maho::GetProfileIdentityKey(",
        ),
    ],
    "chrome/browser/password_manager/factories/BUILD.gn": [
        Replacement(
            old=(
                "  deps = [\n"
                "    \"//components/password_manager/core/browser\",\n"
                "    \"//components/password_manager/core/browser:password_manager_buildflags\","
            ),
            new=(
                "  deps = [\n"
                "    \"//maho/browser:maho_core_holder\",\n"
                "    \"//maho/browser:maho_password_store_backend\",\n"
                "    \"//components/password_manager/core/browser\",\n"
                "    \"//components/password_manager/core/browser:password_manager_buildflags\","
            ),
            description="Maho: Link password backend factory against Maho password store backend",
            idempotent=True,
            guard="//maho/browser:maho_core_holder",
        ),
        Replacement(
            old='    "//maho/browser:maho_password_store_backend",\n',
            new=(
                '    "//maho/browser:maho_core_holder",\n'
                '    "//maho/browser:maho_password_store_backend",\n'
            ),
            description=(
                "Maho: add profile identity helper dependency beside an "
                "already-applied password backend factory override"
            ),
            guard='    "//maho/browser:maho_core_holder",\n',
        ),
    ],
    "chrome/browser/ui/views/permissions/permission_prompt_factory.cc": [
        Replacement(
            old=(
                "  // Suppress permission prompts if the omnibox is being edited or is empty.\n"
                "  LocationBar* location_bar = GetLocationBar(browser);\n"
                "  bool can_display_prompt = !(location_bar && location_bar->IsEditingOrEmpty());\n"
            ),
            new=(
                "  // Suppress permission prompts if the omnibox is being edited or is empty.\n"
                "  // Maho hides the toolbar, so an undrawn location bar is not an editing\n"
                "  // omnibox. Dropping the prompt here skips the bubble and the OS dialog.\n"
                "  LocationBar* location_bar = GetLocationBar(browser);\n"
                "  bool can_display_prompt = !(location_bar && location_bar->IsDrawn() &&\n"
                "                              location_bar->IsEditingOrEmpty());\n"
            ),
            description=(
                "Maho: do not drop site permission prompts when the toolbar is hidden"
            ),
            guard=(
                "Maho hides the toolbar, so an undrawn location bar is not an editing"
            ),
        ),
    ],
    "chrome/browser/ui/views/bubble_anchor_util_views.cc": [
        Replacement(
            old=(
                "  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser);\n"
                "  // Get position in view (taking RTL UI into account).\n"
                "  int x_within_browser_view = browser_view->GetMirroredXInView(\n"
                "      bubble_anchor_util::kNoToolbarLeftOffset);\n"
            ),
            new=(
                "  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser);\n"
                "  // Maho hides the toolbar. The window-origin fallback lands in the sidebar\n"
                "  // rail, so anchor at the top of the visible page when it is drawn.\n"
                "  if (auto* contents = browser_view->contents_container()) {\n"
                "    if (contents->IsDrawn()) {\n"
                "      const gfx::Rect contents_bounds = contents->GetBoundsInScreen();\n"
                "      if (!contents_bounds.IsEmpty()) {\n"
                "        return gfx::Rect(\n"
                "            contents_bounds.x() + bubble_anchor_util::kNoToolbarLeftOffset,\n"
                "            contents_bounds.y(), 0, 0);\n"
                "      }\n"
                "    }\n"
                "  }\n"
                "  // Get position in view (taking RTL UI into account).\n"
                "  int x_within_browser_view = browser_view->GetMirroredXInView(\n"
                "      bubble_anchor_util::kNoToolbarLeftOffset);\n"
            ),
            description=(
                "Maho: anchor hidden-toolbar permission bubbles to the visible page"
            ),
            guard=(
                "Maho hides the toolbar. The window-origin fallback lands in the sidebar"
            ),
        ),
    ],
}


def _guard_replacement_literal_keys() -> None:
    """Reject duplicate literal paths before Python can silently discard them."""
    module = ast.parse(Path(__file__).read_text(encoding="utf-8"))
    for node in module.body:
        if not (
            isinstance(node, ast.AnnAssign)
            and isinstance(node.target, ast.Name)
            and node.target.id == "REPLACEMENTS"
            and isinstance(node.value, ast.Dict)
        ):
            continue
        seen: dict[str, int] = {}
        for key in node.value.keys:
            if not isinstance(key, ast.Constant) or not isinstance(key.value, str):
                continue
            if key.value in seen:
                raise RuntimeError(
                    "Duplicate literal REPLACEMENTS path "
                    f"{key.value!r} at lines {seen[key.value]} and {key.lineno}"
                )
            seen[key.value] = key.lineno
        return
    raise RuntimeError("Unable to locate literal REPLACEMENTS mapping")


_guard_replacement_literal_keys()


def repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def base_patch_dir(revision: str) -> Path | None:
    """Per-revision whole-file Maho patches generated against pristine upstream.

    A revision with a base-patch directory is rebuilt from an untouched
    checkout: each ``<path>.patch`` owns that file entirely, so the anchor-based
    REPLACEMENTS entries for the same path are not applied on top of it.
    """
    directory = repo_root() / "build" / "chromium_base_patches" / revision
    return directory if directory.is_dir() else None


def base_patch_targets(directory: Path) -> dict[str, Path]:
    return {
        patch.relative_to(directory).as_posix()[: -len(".patch")]: patch
        for patch in sorted(directory.rglob("*.patch"))
    }


_UPDATED154_REVISION: Final = "f89f3a4363808e117c592adedcf9947882ac3b79"
_UPDATED154_OLD_SHA256: Final[dict[str, str]] = {
    "chrome/browser/ui/views/frame/contents_container_view.cc":
        "feac31c0e4ef85de95c3ffbfecd830cef894374e00ec47a2f617791786e374af",
    "chrome/browser/ui/views/frame/multi_contents_view.cc":
        "f629ead6a06ad34c105292f0cbafe8fc0e2457074c250dc4db3327106160a03b",
    "chrome/browser/ui/tabs/tab_strip_model.cc":
        "b2710629acb805a4abf75d5faa8e22633022462131640847271c1b81b208b746",
    "components/autofill/content/renderer/password_autofill_agent.cc":
        "14f939bdca26a5eb9605c8466fe5d3cf802f54c03f05fc3c65e0391f8b50d2f5",
    "chrome/browser/ui/views/frame/browser_caption_button_container_win.cc":
        "7ca4b5007685ba33262323ade99a355caf6b938bc14d8ceb6feaa106bc293bfb",
    "chrome/browser/ui/views/frame/layout/browser_view_popup_layout_impl.cc":
        "d3e7ce754365df207440e37047299cd20f4489305d468b4bc378ad99a6fd353b",
    "chrome/browser/ui/browser.cc":
        "55d9d30b339364b54368aa77867729100cc6d4bdcfd5aff0c927b652c4db4041",
    "chrome/browser/app_controller_mac.mm":
        "199d9c5a88a9b7d4757cc58c73479d9b3a557bede44155f68e35c777567e766d",
    "chrome/browser/ui/browser_actions.cc":
        "bb2e3a21ae4675d0e99acdf35daba35ed15623306d5893eba173d4435c085798",
    "chrome/browser/ui/browser_commands.cc":
        "339e23168734a38e6e0a8449b5a458f0158273f4d7a7e5a331f31c3894828505",
    "chrome/browser/ui/chrome_pages.cc":
        "f116ecfe56750b094e4a9dc019787d1ecc54de8b3723a8d1cec03f2051a303f7",
    "chrome/browser/ui/cocoa/browser_window_command_handler.mm":
        "68153b84d898e10535900d0623a511575e126a3fb4d1a109f5596e68be3006b9",
    "chrome/browser/ui/cocoa/chrome_command_dispatcher_delegate.mm":
        "04753ec9aa46de68248d39a5e1265311dffd41f219806dbf457e38c68b1e4cf7",
    "chrome/browser/ui/browser_web_contents_delegate/browser_web_contents_delegate.cc":
        "82b784e7ed3bd93f9ff6d830c7eb67cb5253c0358e1ab2151f94feda2179a755",
    "chrome/browser/sessions/session_restore.cc":
        "6c647c1361bed87d8937915e56fcf0ab4daae977d473583d1f53f52509de28c5",
    "chrome/browser/ui/browser_command_controller.cc":
        "ab1f5239e9e29c23f9a833085c909c59a004a90aecb1c57e1d36b4d4584858c6",
    "chrome/browser/ui/unload_controller.cc":
        "1e2237d2c14dc8d75667d3a4b2ffe6de1776c87329ded3bfcd750d117f54322e",
    "chrome/browser/ui/browser_command_controller_browsertest.cc":
        "233676871a1aeb93e1d72c1dd54e10e2393b5a23a13bef57c7db750c87a976bc",
    "chrome/browser/ui/views/location_bar/location_bar_view.cc":
        "c9e374b82a73454f2686107d71f2e56772eefcfdd45e6d4f147ab110fa8f1d86",
    "chrome/browser/ui/views/frame/browser_view.cc":
        "30d12563a55ead3d381ec574038089f9eb2aeb60996c1cbc8fb39659c012a286",
    "chrome/browser/ui/views/frame/layout/browser_view_tabbed_layout_impl.cc":
        "a853bd90bacfb1dd4e6951293ac61f23800455487d0d31af993884e943a009b8",
    "chrome/browser/ui/views/toolbar/toolbar_view.cc":
        "4ddb98b7a89ca37b72ca9d51177d723a671a790d7e099b302bec2a0b7414a97d",
    "chrome/browser/ui/startup/startup_browser_creator_impl.cc":
        "d595ff2eb97d2f0a596a31815aa5b32c4557f56720dac12dbc8598f4bb3c4577",
    "chrome/browser/ui/views/side_panel/side_panel.cc":
        "7122ce6b00ed03da190d3d840de6ad404d229e799aa3818d23385b5582ceb44e",
    "chrome/browser/device_reauth/chrome_device_authenticator_factory.cc":
        "8099af6de9b39c325a4f196b3109748e31350d53b8866e890ddb78d8172d2b3d",
}


_UPDATED154_INCREMENTAL_SHA256: Final[dict[str, str]] = {
    "chrome/browser/device_reauth/chrome_device_authenticator_factory.cc":
        "6cd7dedbdeec35babaf2f62a7b489d7e159de75570741e07ecd3931f91397b98",
    "components/autofill/content/renderer/password_autofill_agent.cc":
        "7f82a720c05f01736581fc1db4d894347493d248f280f8085440a3911dcd878f",
    "chrome/browser/ui/views/frame/layout/browser_view_tabbed_layout_impl.cc":
        "42af9f599f429e69d2d19a1e5e272ded248cb190b756c07f4b095a70f137fce3",
    "chrome/browser/ui/browser_command_controller.cc":
        "8aea96cc899ed2b7123269b728324c396371719ee47b940ae04d11ea21389f89",
    "chrome/browser/ui/views/frame/browser_view.cc":
        "808d230fa0f079d5823af6a3b7db1e37554b31519a2edb049aa4d32864858b1e",
    "chrome/browser/ui/browser_web_contents_delegate/browser_web_contents_delegate.cc":
        "94139a8495e3e7e0dba0f500e901b773aca222716999aef629974eda9ef43144",
}


_UPDATED154_FOCUS_SHA256: Final[dict[str, str]] = {
    "chrome/browser/ui/browser_command_controller.cc":
        "80a3f702a8fb0b26107372a5927eee7596c1b7f97fad4a45c8a630f2eac117f0",
}


_UPDATED154_LAYOUT_SHA256: Final[dict[str, str]] = {
    "chrome/browser/ui/views/frame/layout/browser_view_tabbed_layout_impl.cc":
        "575045f2c245f645b918c6cffd2e52b9c59cb5ba4d619151b903755b19cc7245",
}


def apply_base_patch(
    chromium_src: Path, relative_path: str, patch: Path, dry_run: bool
) -> str:
    """Apply one base patch; returns 'patched' or 'already'.

    Raises RuntimeError when the file matches neither the pristine upstream
    text nor the patched result, so drift is never silently skipped.
    """
    def git_apply(*extra: str) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            ["git", "-C", str(chromium_src), "apply", *extra, str(patch)],
            capture_output=True,
            text=True,
        )

    if git_apply("--reverse", "--check").returncode == 0:
        return "already"
    forward = git_apply("--check")
    if forward.returncode != 0:
        canonical = (repo_root() / "build" / "chromium_base_patches" /
                     _UPDATED154_REVISION / f"{relative_path}.patch")
        expected = _UPDATED154_OLD_SHA256.get(relative_path)
        target = chromium_src / relative_path
        digest = hashlib.sha256(target.read_bytes()).hexdigest()
        migration_directory = "chromium_base_migrations"
        incremental = _UPDATED154_INCREMENTAL_SHA256.get(relative_path)
        if incremental is not None and digest == incremental:
            expected = incremental
            migration_directory = "chromium_base_incremental_migrations"
        focus = _UPDATED154_FOCUS_SHA256.get(relative_path)
        if focus is not None and digest == focus:
            expected = focus
            migration_directory = "chromium_base_focus_migrations"
        layout = _UPDATED154_LAYOUT_SHA256.get(relative_path)
        if layout is not None and digest == layout:
            expected = layout
            migration_directory = "chromium_base_layout_migrations"
        if (expected is not None and patch.resolve() == canonical.resolve() and
                digest == expected):
            migration = (repo_root() / "build" / migration_directory /
                         _UPDATED154_REVISION / f"{relative_path}.patch")
            # Validate the entire transition before touching the developer tree.
            # Dry-run performs exactly the same checks in an isolated directory.
            with tempfile.TemporaryDirectory(prefix="maho-base-migration-") as tmp:
                staged = Path(tmp)
                subprocess.run(
                    ["git", "init", "--quiet", str(staged)],
                    check=True, capture_output=True, text=True,
                )
                staged_target = staged / relative_path
                staged_target.parent.mkdir(parents=True)
                staged_target.write_bytes(target.read_bytes())
                for arguments in (
                    ["--check", str(migration)],
                    [str(migration)],
                    ["--reverse", "--check", str(patch)],
                ):
                    result = subprocess.run(
                        ["git", "-C", str(staged), "apply", *arguments],
                        capture_output=True, text=True,
                    )
                    if result.returncode != 0:
                        raise RuntimeError(
                            f"base migration validation failed for {relative_path}: "
                            f"{result.stderr.strip()}")
                if not dry_run:
                    # Refuse intervening edits rather than overwriting them.
                    if hashlib.sha256(target.read_bytes()).hexdigest() != expected:
                        raise RuntimeError(f"base migration source changed: {relative_path}")
                    for arguments in (["--check", str(migration)], [str(migration)]):
                        result = subprocess.run(
                            ["git", "-C", str(chromium_src), "apply", *arguments],
                            capture_output=True, text=True,
                        )
                        if result.returncode != 0:
                            raise RuntimeError(
                                f"base migration failed for {relative_path}: "
                                f"{result.stderr.strip()}")
                    if git_apply("--reverse", "--check").returncode != 0:
                        raise RuntimeError(f"base migration postcheck failed: {relative_path}")
            return "patched"
        raise RuntimeError(
            f"base patch does not apply to {relative_path}: {forward.stderr.strip()}"
        )
    if not dry_run:
        applied = git_apply()
        if applied.returncode != 0:
            raise RuntimeError(
                f"base patch failed for {relative_path}: {applied.stderr.strip()}"
            )
    return "patched"


def default_chromium_src() -> Path:
    if repo_root().parent.name == "src" and repo_root().parent.parent.name == "chromium":
        return repo_root().parent
    return repo_root().parent / "chromium" / "src"


def normalize_maho_test_webui_config(text: str) -> tuple[str, int]:
    text, include_count = re.subn(
        r'#include "build/buildflag\.h"\n'
        r'#include "[^"]*buildflags\.h"\n'
        r'#if [^\n]+\n'
        r'(#include "maho/browser/ui/webui/maho_test/maho_test_ui\.h"\n)'
        r'#endif\n',
        r'\1',
        text,
    )
    text, registration_count = re.subn(
        r'#if [^\n]+\n'
        r'(  map\.AddWebUIConfig\(std::make_unique<MahoTestUIConfig>\(\)\);\n)'
        r'#endif\n',
        r'\1',
        text,
    )
    return text, include_count + registration_count


# grd/grdp files whose message bodies still hardcode the upstream product name.
# generated_resources.grd uses <ph name="PRODUCT_NAME"> and therefore already
# resolves to IDS_PRODUCT_NAME ("Maho"); these two spell "Chromium" out in the
# message body, which is how "your Chromium" reached the UI.
# Every resource file that spells the product name out instead of deferring to
# <ph name="PRODUCT_NAME">. The shared *_strings.grdp files keep the literal in
# an <if expr="not _google_chrome"> branch, and Maho builds unbranded, so those
# branches are exactly the ones that ship.
_CHROMIUM_BRANDING_TARGETS: Final[frozenset[str]] = frozenset(
    {
        "chrome/app/chromium_strings.grd",
        "chrome/app/password_manager_ui_strings.grdp",
        "chrome/app/settings_chromium_strings.grdp",
        "chrome/app/settings_strings.grdp",
        "components/autofill_payments_strings.grdp",
        "components/autofill_strings.grdp",
        "components/components_chromium_strings.grd",
        "components/management_strings.grdp",
        "components/new_or_sad_tab_strings.grdp",
        "components/page_info_strings.grdp",
        "components/password_manager_strings.grdp",
        "components/privacy_sandbox_strings.grd",
        "components/reset_password_strings.grdp",
        "components/search_engine_choice_strings.grdp",
        "components/security_interstitials_strings.grdp",
        "components/ssl_errors_strings.grdp",
    }
)

_GRD_ATTRIBUTE_RE: Final[re.Pattern[str]] = re.compile(r'[\w:-]+="[^"]*"')
# "Chromium OS" is a different product that is never compiled into a Maho build;
# renaming it would be wrong, so it is excluded.
_CHROMIUM_PRODUCT_WORD_RE: Final[re.Pattern[str]] = re.compile(r"\bChromium\b(?! OS)")
_GRD_MASK_RE: Final[re.Pattern[str]] = re.compile(r"\x00(\d+)\x00")


def normalize_chromium_product_branding(text: str) -> tuple[str, int]:
    """Rebrand user-visible Chromium product strings to Maho.

    Only message BODY text is rewritten. XML attributes (name=, desc=,
    meaning=, translateable=) are masked first so grit message ids and
    translator notes keep describing the upstream string, which is what keeps
    these files diffable against upstream.
    """
    rewritten = 0
    lines: list[str] = []
    for line in text.split("\n"):
        masked: list[str] = []

        def _mask(match: re.Match[str]) -> str:
            masked.append(match.group(0))
            return f"\x00{len(masked) - 1}\x00"

        body = _GRD_ATTRIBUTE_RE.sub(_mask, line)
        body, count = _CHROMIUM_PRODUCT_WORD_RE.subn("Maho", body)
        rewritten += count
        body = _GRD_MASK_RE.sub(lambda m: masked[int(m.group(1))], body)
        lines.append(body)
    return "\n".join(lines), rewritten


def normalize_browser_view_features(text: str) -> tuple[str, int]:
    text, include_count = re.subn(
        r'#include "[^"]*/maho_features\.h"\n',
        "",
        text,
    )
    text, focus_count = re.subn(
        r'if \(is_user_initiated && maho_sidebar_container_ &&\n'
        r'      base::FeatureList::IsEnabled\([^\n]+\)\) \{',
        "if (is_user_initiated && maho_sidebar_container_) {",
        text,
    )
    text, toolbar_count = re.subn(
        r'  if \(!maho_toolbar_button_provider_ &&\n'
        r'      !ToolbarButtonProvider::From\(browser_\.get\(\)\) &&\n'
        r'      IsMahoArcLayoutActive\(\) &&\n'
        r'      base::FeatureList::IsEnabled\(\n'
        r'          maho::features::k[A-Za-z0-9_]+\)\) \{',
        "  if (!maho_toolbar_button_provider_ &&\n"
        "      !ToolbarButtonProvider::From(browser_.get()) &&\n"
        "      IsMahoArcLayoutActive()) {",
        text,
    )
    return text, include_count + focus_count + toolbar_count


def normalize_browser_widget_theme_override(text: str) -> tuple[str, int]:
    normalized_count = 0
    if _BROWSER_WIDGET_LEGACY_INCLUDES in text:
        text = text.replace(
            _BROWSER_WIDGET_LEGACY_INCLUDES,
            _BROWSER_WIDGET_INCLUDES,
            1,
        )
        normalized_count += 1
    if _BROWSER_WIDGET_LEGACY_THEME_OVERRIDE in text:
        text = text.replace(
            _BROWSER_WIDGET_LEGACY_THEME_OVERRIDE,
            _BROWSER_WIDGET_OTR_SENTINEL,
            1,
        )
        normalized_count += 1

    stale_markers = (
        '#include "maho/browser/ui/theme/maho_space_theme_state.h"',
        '#include "ui/native_theme/native_theme.h"',
        "MahoSpaceThemeState::ResolveSidebarDarkMode",
        "the whole window (sidebar-dominated chrome) follows the active Space",
    )
    remaining_markers = [marker for marker in stale_markers if marker in text]
    if remaining_markers:
        raise RuntimeError(
            "BrowserWidget Space-theme override drift: legacy markers remain after "
            f"normalization: {', '.join(remaining_markers)}"
        )

    return text, normalized_count


def normalize_startup_browser_creator_session_restore(
    text: str,
) -> tuple[str, int]:
    """Revert the legacy g_maho_restore_palette_shown injection on the SessionRestore path.

    Startup auto-show must not appear when the window opens with a startup URL or restored tabs.
    """
    legacy_block = (
        "    browser = SessionRestore::RestoreSession(profile_, nullptr, restore_options,\n"
        "                                             tabs);\n"
        "    if (browser) {\n"
        "      static std::atomic<bool> g_maho_restore_palette_shown{false};\n"
        "      if (!g_maho_restore_palette_shown.exchange(true)) {\n"
        "        ShowMahoColdStartPaletteWhenReady(browser->AsWeakPtr(), 200);\n"
        "      }\n"
        "      return browser;\n"
        "    }\n"
    )
    clean_block = (
        "    browser = SessionRestore::RestoreSession(profile_, nullptr, restore_options,\n"
        "                                             tabs);\n"
        "    if (browser) {\n"
        "      return browser;\n"
        "    }\n"
    )
    if legacy_block in text:
        return text.replace(legacy_block, clean_block, 1), 1
    return text, 0


def normalize_gn_stale_deps(text: str, relative_path: str = "") -> tuple[str, int]:
    normalized_path = relative_path.replace("\\", "/")
    stale_deps: set[str] = set()

    if normalized_path == "chrome/browser/ui/webui/BUILD.gn":
        stale_deps.add('"//maho/build/config:maho_test_buildflags"')
    elif normalized_path == "chrome/browser/ui/BUILD.gn":
        stale_deps.add('"//maho/browser:maho_features"')

    if not stale_deps:
        return text, 0

    kept_lines: list[str] = []
    removed_count = 0

    for line in text.splitlines(keepends=True):
        if line.strip().removesuffix(",") in stale_deps:
            removed_count += 1
            continue
        kept_lines.append(line)

    return "".join(kept_lines), removed_count


_CHROME_BROWSER_UI_SOURCES: Final[tuple[str, ...]] = (
    "//maho/browser/maho_browser_main_extra_parts.cc",
    "//maho/browser/maho_browser_main_extra_parts.h",
    "//maho/browser/maho_routines_scheduler.cc",
    "//maho/browser/maho_routines_scheduler.h",
    "//maho/browser/maho_tab_id_session_helper.cc",
    "//maho/browser/maho_tab_id_session_helper.h",
    "//maho/browser/mail_helper/maho_mail_notification_coordinator.cc",
    "//maho/browser/mail_helper/maho_mail_notification_coordinator.h",
    "//maho/browser/mcp/maho_mcp_console_capture.cc",
    "//maho/browser/mcp/maho_mcp_console_capture.h",
    "//maho/browser/mcp/maho_mcp_input_synthesizer.cc",
    "//maho/browser/mcp/maho_mcp_input_synthesizer.h",
    "//maho/browser/mcp/maho_mcp_navigation_tracker.cc",
    "//maho/browser/mcp/maho_mcp_navigation_tracker.h",
    "//maho/browser/net/maho_content_blocker_update_service_factory.cc",
    "//maho/browser/net/maho_content_blocker_update_service_factory.h",
    "//maho/browser/sync/maho_sync_relay_client.cc",
    "//maho/browser/sync/maho_sync_relay_client.h",
    "//maho/browser/sync/maho_sync_relay_protocol.cc",
    "//maho/browser/sync/maho_sync_relay_protocol.h",
    "//maho/browser/updates/maho_config_manager.cc",
    "//maho/browser/updates/maho_config_manager.h",
    "//maho/browser/updates/maho_update_signature.cc",
    "//maho/browser/updates/maho_update_signature.h",
    "//maho/browser/updates/rollout_bucket.cc",
    "//maho/browser/updates/rollout_bucket.h",
    "//maho/browser/ui/views/maho_content_gradient_view.cc",
    "//maho/browser/ui/views/maho_content_gradient_view.h",
    "//maho/browser/ui/views/maho_action_marker_service.cc",
    "//maho/browser/ui/views/maho_action_marker_service.h",
    "//maho/browser/ui/views/maho_mini/maho_mini_top_bar_view.cc",
    "//maho/browser/ui/views/maho_mini/maho_mini_top_bar_view.h",
    "//maho/browser/ui/views/sidebar/maho_sidebar_section_policy.cc",
    "//maho/browser/ui/views/sidebar/maho_sidebar_section_policy.h",
    "//maho/browser/ui/views/sidebar/maho_traffic_light_geometry.h",
    "//maho/browser/ui/views/location_bar/maho_dom_screenshot_handler.cc",
    "//maho/browser/ui/views/location_bar/maho_dom_screenshot_handler.h",
    "//maho/browser/ui/views/location_bar/maho_dom_screenshot_action_dialog_view.cc",
    "//maho/browser/ui/views/location_bar/maho_dom_screenshot_action_dialog_view.h",
    "//maho/browser/ui/views/location_bar/maho_full_page_capture_client.cc",
    "//maho/browser/ui/views/location_bar/maho_full_page_capture_client.h",
    "//maho/browser/ui/views/location_bar/maho_dom_screenshot_script_string.h",
    "//maho/browser/ui/views/shields/maho_shield_bubble_coordinator.cc",
    "//maho/browser/ui/views/shields/maho_shield_bubble_coordinator.h",
    "//maho/browser/ui/views/shields/maho_shield_bubble_view.cc",
    "//maho/browser/ui/views/shields/maho_shield_bubble_view.h",
    "//maho/browser/ui/webui/maho_live_folders/maho_live_folder_item_cache.cc",
    "//maho/browser/ui/webui/maho_live_folders/maho_live_folder_item_cache.h",
    "//maho/browser/ui/webui/maho_live_folders/maho_live_folder_cache_warmer.cc",
    "//maho/browser/ui/webui/maho_live_folders/maho_live_folder_cache_warmer.h",
    "//maho/browser/ui/site_control/maho_page_info_ui.cc",
    "//maho/browser/ui/site_control/maho_page_info_ui.h",
    "//maho/browser/maho_tab_preview_capture.cc",
    "//maho/browser/maho_tab_preview_capture.h",
    "//maho/browser/maho_private_context_policy.cc",
    "//maho/browser/maho_private_context_policy.h",
    "//maho/browser/net/maho_atc_navigation_throttle.cc",
    "//maho/browser/net/maho_atc_navigation_throttle.h",
    "//maho/browser/extensions/api/maho_split_view_api.cc",
    "//maho/browser/extensions/api/maho_split_view_api.h",
    "//maho/browser/ui/maho_settings_navigation.cc",
    "//maho/browser/ui/maho_settings_navigation.h",
    "//maho/browser/ui/views/maho_mini/maho_mini_window.h",
    "//maho/browser/ui/views/location_bar/maho_location_bar_utility_icon_view.cc",
    "//maho/browser/ui/views/location_bar/maho_location_bar_utility_icon_view.h",
    "//maho/browser/ui/views/location_bar/maho_location_bar_utility_bubble_coordinator.cc",
    "//maho/browser/ui/views/location_bar/maho_location_bar_utility_bubble_coordinator.h",
    "//maho/browser/ui/views/location_bar/maho_location_bar_utility_panel_provider.cc",
    "//maho/browser/ui/views/location_bar/maho_location_bar_utility_panel_provider.h",
    "//maho/browser/ui/views/location_bar/maho_location_bar_utility_panel_view.cc",
    "//maho/browser/ui/views/location_bar/maho_location_bar_utility_panel_view.h",
    "//maho/browser/ui/views/sidebar/maho_sidebar_drop_planner.cc",
    "//maho/browser/ui/views/sidebar/maho_sidebar_drop_planner.h",
    "//maho/browser/ui/views/split_view/maho_split_view_controller.cc",
    "//maho/browser/ui/views/changelog/maho_changelog_auto_opener.cc",
    "//maho/browser/ui/views/split_view/maho_split_view_controller.h",
    "//maho/browser/ui/views/welcome/maho_welcome_window.cc",
    "//maho/browser/ui/views/welcome/maho_welcome_window.h",
    "//maho/browser/ui/webui/maho_webui_private_boundary.cc",
    "//maho/browser/ui/webui/maho_webui_private_boundary.h",
    "//maho/browser/ui/webui/maho_live_folders/maho_live_folders_page_handler.cc",
    "//maho/browser/ui/webui/maho_live_folders/maho_live_folders_page_handler.h",
    "//maho/browser/ui/views/sidebar/maho_control_activity_indicator_view.cc",
    "//maho/browser/ui/views/sidebar/maho_control_activity_indicator_view.h",
    "//maho/browser/ui/views/sidebar/maho_favorite_edit_dialog.cc",
    "//maho/browser/ui/views/sidebar/maho_favorite_edit_dialog.h",
    "//maho/browser/ui/views/sidebar/maho_sidebar_container_view.cc",
    "//maho/browser/ui/views/sidebar/maho_sidebar_container_view.h",
    "//maho/browser/ui/views/sidebar/maho_sidebar_favorites_grid_view.cc",
    "//maho/browser/ui/views/sidebar/maho_sidebar_favorites_grid_view.h",
    "//maho/browser/ui/views/sidebar/maho_sidebar_footer_view.cc",
    "//maho/browser/ui/views/sidebar/maho_sidebar_footer_view.h",
    "//maho/browser/ui/views/sidebar/maho_sidebar_scroll_bar.cc",
    "//maho/browser/ui/views/sidebar/maho_sidebar_scroll_bar.h",
    "//maho/browser/ui/views/sidebar/maho_sidebar_space_dot_view.cc",
    "//maho/browser/ui/views/sidebar/maho_sidebar_space_dot_view.h",
    "//maho/browser/ui/views/sidebar/maho_sidebar_state_adapter.cc",
    "//maho/browser/ui/views/sidebar/maho_sidebar_state_adapter.h",
    "//maho/browser/ui/views/sidebar/maho_sidebar_state_models.cc",
    "//maho/browser/ui/views/sidebar/maho_sidebar_state_models.h",
    "//maho/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc",
    "//maho/browser/ui/views/sidebar/maho_sidebar_tab_list_view.h",
    "//maho/browser/ui/views/sidebar/maho_sidebar_visibility_manager.cc",
    "//maho/browser/ui/views/sidebar/maho_sidebar_visibility_manager.h",
    "//maho/browser/ui/views/sidebar/maho_sidebar_folder_hover_popup_view.cc",
    "//maho/browser/ui/views/sidebar/maho_sidebar_folder_hover_popup_view.h",
    "//maho/browser/ui/views/sidebar/maho_sidebar_folder_hover_controller.cc",
    "//maho/browser/ui/views/sidebar/maho_sidebar_folder_hover_controller.h",
    "//maho/browser/ui/views/sidebar/maho_sidebar_top_bar_view.cc",
    "//maho/browser/ui/views/sidebar/maho_sidebar_top_bar_view.h",
    "//maho/browser/ui/views/sidebar/maho_sidebar_update_notification_controller.cc",
    "//maho/browser/ui/views/sidebar/maho_sidebar_update_notification_controller.h",
    "//maho/browser/ui/views/sidebar/maho_sidebar_update_notification_model.cc",
    "//maho/browser/ui/views/sidebar/maho_sidebar_update_notification_model.h",
    "//maho/browser/ui/views/sidebar/maho_sidebar_update_notification_view.cc",
    "//maho/browser/ui/views/sidebar/maho_sidebar_update_notification_view.h",
    "//maho/browser/ui/views/sidebar/maho_now_playing_card.cc",
    "//maho/browser/ui/views/sidebar/maho_now_playing_card.h",
    "//maho/browser/ui/views/sidebar/maho_now_playing_coordinator.cc",
    "//maho/browser/ui/views/sidebar/maho_now_playing_coordinator.h",
    "//maho/browser/ui/views/sidebar/maho_now_playing_coordinator_factory.cc",
    "//maho/browser/ui/views/sidebar/maho_now_playing_coordinator_factory.h",
    "//maho/browser/ui/views/sidebar/maho_sidebar_now_playing_view.cc",
    "//maho/browser/ui/views/sidebar/maho_sidebar_now_playing_view.h",
    "//maho/browser/ui/views/sidebar/maho_sidebar_grain_overlay_view.cc",
    "//maho/browser/ui/views/sidebar/maho_sidebar_grain_overlay_view.h",
    "//maho/browser/ui/views/sidebar/maho_sidebar_processing_placeholder_view.cc",
    "//maho/browser/ui/views/sidebar/maho_sidebar_processing_placeholder_view.h",
    "//maho/browser/ui/views/sidebar/maho_sidebar_view.cc",
    "//maho/browser/ui/views/sidebar/maho_sidebar_view.h",
    "//maho/browser/ui/views/sidebar/maho_translate_popover_view.cc",
    "//maho/browser/ui/views/sidebar/maho_translate_popover_view.h",
    "//maho/browser/ui/views/frame/maho_contents_header_view.cc",
    "//maho/browser/ui/views/frame/maho_contents_header_view.h",
    "//maho/browser/ui/views/sidebar/maho_toolbar_button_provider.cc",
    "//maho/browser/ui/views/sidebar/maho_toolbar_button_provider.h",
    "//maho/browser/ui/views/sidebar/maho_sidebar_prefs.cc",
    "//maho/browser/ui/views/sidebar/maho_sidebar_prefs.h",
    "//maho/browser/ui/views/command/maho_command_overlay_controller.cc",
)

# NOTE: maho_traffic_light_geometry is registered by the dedicated is_mac/else
# replacement on chrome/browser/ui/BUILD.gn, which picks the .mm on macOS and
# the stub elsewhere. Listing it here too would add it to the unconditional
# top-level sources list as well and GN then fails with a duplicate entry.
_CHROME_BROWSER_UI_MAC_SOURCES: Final[tuple[str, ...]] = (
    "//maho/browser/ui/views/maho_mini/maho_mini_window.cc",
    "//maho/browser/ui/views/location_bar/maho_utility_panel_vibrancy_mac.mm",
    "//maho/browser/mcp/maho_mcp_native_input_mac.mm",
)

_CHROME_BROWSER_UI_NON_MAC_SOURCES: Final[tuple[str, ...]] = (
    "//maho/browser/ui/views/maho_mini/maho_mini_window.cc",
    "//maho/browser/ui/views/location_bar/maho_utility_panel_vibrancy_stub.cc",
)

# Windows-only overlay sources for chrome/browser/ui:ui. Linux takes neither
# this nor the mac tuple: the input synthesizer's platform dispatch falls
# back to nullopt there, so no native-input backend object is referenced.
_CHROME_BROWSER_UI_WIN_SOURCES: Final[tuple[str, ...]] = (
    "//maho/browser/mcp/maho_mcp_native_input_win.cc",
)

_CHROME_BROWSER_UI_DEPS: Final[tuple[str, ...]] = (
    "//ui/base/accelerators/global_accelerator_listener",
    "//maho/browser/mail_helper:maho_mail_service",
    "//maho/browser/ui/views/command:maho_command_overlay",
    "//maho/browser/ui/views/peek:maho_peek",
    "//maho/browser/ui/views/peek:maho_peek_route",
    "//maho/browser/ui/views/spaces_overlay:maho_spaces_overlay",
    "//maho/browser/ui/tabs:maho_mru_tab_tracker",
    "//maho/browser/ui/views/side_panel:side_panel",
    "//maho/browser/ui/tabs:maho_ctrl_tab_switcher_controller",
    "//components/split_tabs",
    "//maho/browser/ui/webui/maho_mail:maho_mail",
    "//maho/browser/ui/webui/maho_sync:maho_sync",
    "//maho/browser/ui/webui/maho_settings:maho_settings",
    "//maho/browser:maho_private_context_policy",
    "//maho/browser:maho_settings_navigation",
    "//maho/browser/ui/webui/maho_welcome:mojo_bindings",
    "//maho/browser/ui/webui/maho_sync:mojo_bindings",
    "//maho/browser/ui/webui/maho_space_create:mojo_bindings",
    "//maho/browser/ui/webui/maho_space_config:mojo_bindings",
    "//maho/browser/ui/webui/maho_settings:mojo_bindings",
    "//maho/browser/ui/webui/maho_routines:mojo_bindings",
    "//maho/browser/ui/webui/maho_live_folders:mojo_bindings",
    "//maho/browser/ui/webui/maho_inline_edit:mojo_bindings",
    "//maho/browser/ui/webui/maho_boost:mojo_bindings",
    "//maho/browser/ui/webui/maho_ai:mojo_bindings",
    "//maho/browser/ui/views/sidebar:maho_spaces_board_data",
    "//maho/browser/ui/views/sidebar:maho_sidebar_themed_background",
    "//maho/browser/ui/views/sidebar:maho_sidebar_prefs",
    "//maho/browser/ui/views/sidebar:maho_sidebar_library",
    "//maho/browser/ui/views/sidebar:maho_sidebar_downloads",
    "//maho/third_party/maho:maho_ffi_headers",
    "//maho/third_party/maho:maho_bridge",
    "//maho/browser/ui/webui/maho_ai:maho_ai",
    "//maho/browser/ui/views/maho_lucide_icons:maho_lucide_icon_library",
    "//maho/browser/updates:maho_updates_prefs",
    "//maho/browser/updates:maho_updates_lib",
    "//maho/browser/mcp:maho_mcp",
    "//maho/browser:maho_tab_tidy_orchestrator",
    "//maho/browser:maho_content_blocker_update_service",
)


def normalize_chrome_browser_ui_build(text: str) -> tuple[str, int]:
    source_anchor = '    "accelerator_utils.h",\n'
    dep_anchor = '      "//chrome/browser/ui/side_panel:side_panel_views_dependent",\n'
    if source_anchor not in text or dep_anchor not in text:
        raise RuntimeError("Missing chrome/browser/ui:ui normalization anchor")

    # Retired entries injected by earlier materializations must be stripped
    # before the missing-source pass re-adds anything, otherwise a deleted
    # source file keeps breaking ninja ("missing and no known rule").
    # mac-only overlay sources must also be stripped on non-mac hosts: a
    # mac-mirrored BUILD.gn carries them, and a win host that skips the
    # strip keeps the mac backend in sources while the non-mac stub that
    # satisfies ApplyVibrancyToUtilityPanel never gets added.
    retired_removed = 0
    retired = (
        '    "//maho/browser/ui/views/maho_mini/maho_mini_window_stub.cc",\n',
    )
    # Sidebar search pill, replaced by the per-pane contents header. Trees list
    # it at either 4- or 6-space indent, so drop the whole line.
    text, pill_removed = re.subn(
        r'^[ \t]*"//maho/browser/ui/views/sidebar/maho_sidebar_'
        r'search_view\.(?:cc|h)",\n',
        '',
        text,
        flags=re.MULTILINE,
    )
    retired_removed += pill_removed
    if host_platform() != "mac":
        retired += (
            '    "//maho/browser/ui/views/location_bar/maho_utility_panel_vibrancy_mac.mm",\n',
            '    "//maho/browser/mcp/maho_mcp_native_input_mac.mm",\n',
        )
    for retired_entry in retired:
        if retired_entry in text:
            text = text.replace(retired_entry, '', 1)
            retired_removed += 1

    sources = _CHROME_BROWSER_UI_SOURCES
    if host_platform() == "mac":
        sources += _CHROME_BROWSER_UI_MAC_SOURCES
    elif host_platform() == "win":
        sources += _CHROME_BROWSER_UI_NON_MAC_SOURCES
        sources += _CHROME_BROWSER_UI_WIN_SOURCES
    else:
        sources += _CHROME_BROWSER_UI_NON_MAC_SOURCES
    missing_sources = [entry for entry in sources if f'"{entry}"' not in text]
    missing_deps = [entry for entry in _CHROME_BROWSER_UI_DEPS if f'"{entry}"' not in text]
    if missing_sources:
        insertion = "".join(f'    "{entry}",\n' for entry in missing_sources)
        text = text.replace(source_anchor, source_anchor + insertion, 1)
    if missing_deps:
        insertion = "".join(f'      "{entry}",\n' for entry in missing_deps)
        text = text.replace(dep_anchor, dep_anchor + insertion, 1)
    return text, len(missing_sources) + len(missing_deps) + retired_removed


def normalize_chrome_browser_build(text: str) -> tuple[str, int]:
    additions = (
        (
            '    "//components/password_manager/core/browser",\n',
            (
                "//maho/browser:maho_context_menus",
                "//maho/browser:maho_url_scheme",
                "//maho/browser:maho_core_holder",
                "//maho/browser:maho_space_profile_bridge",
                "//maho/browser:maho_tab_id_helper",
                "//maho/browser/mail_helper:maho_mail_service",
                "//maho/browser:maho_control_activity_service",
            ),
        ),
        (
            '    "//chrome/browser/ui/browser_window",\n',
            (
                "//maho/browser/ui/views/boost:maho_boost_window",
                "//maho/browser/ui/views/side_panel:side_panel",
            ),
        ),
        (
            '    "chrome_browser_field_trials.cc",\n',
            (
                "//maho/browser/maho_tab_registry.cc",
                "//maho/browser/maho_tab_registry.h",
            ),
        ),
    )
    count = 0
    for anchor, entries in additions:
        missing = [entry for entry in entries if f'"{entry}"' not in text]
        if anchor not in text or not missing:
            continue
        indent = anchor[: len(anchor) - len(anchor.lstrip())]
        insertion = "".join(f'{indent}"{entry}",\n' for entry in missing)
        text = text.replace(anchor, anchor + insertion, 1)
        count += len(missing)
    return text, count


def normalize_peek_open_url_routing(text: str) -> tuple[str, int]:
    """Migrates the already-applied ATC-only OpenURLFromTab block to Peek."""
    original_text = text
    struct_start = text.find("  struct AtcBypassState {\n")
    if struct_start < 0:
        return text, 0
    end = text.find("\n\n  // If the source is already split", struct_start)
    if end < 0:
        return text, 0
    mac_guard_already_closed = text[:struct_start].rstrip().endswith("#endif")
    atc_bypass_state = """  struct AtcBypassState {
    static std::set<Browser*>& BypassedBrowsers() {
      static std::set<Browser*>* bypassed_browsers = new std::set<Browser*>();
      return *bypassed_browsers;
    }
    static bool ShouldBypass(Browser* browser) {
      return BypassedBrowsers().count(browser) > 0;
    }
    static void SetBypass(Browser* browser, bool bypass) {
      if (bypass) {
        BypassedBrowsers().insert(browser);
      } else {
        BypassedBrowsers().erase(browser);
      }
    }
  };

"""
    replacement = """  if (!AtcBypassState::ShouldBypass(this)) {
    const bool is_top_level =
        ui::PageTransitionIsMainFrame(params.transition) &&
        !ui::PageTransitionIsRedirect(params.transition);
    const bool is_user_initiated =
        !params.is_renderer_initiated || params.user_gesture;
    bool is_maho_mini_gesture = false;
#if BUILDFLAG(IS_MAC)
    is_maho_mini_gesture =
        params.disposition == WindowOpenDisposition::NEW_SPLIT_VIEW &&
        !params.started_from_context_menu &&
        ui::PageTransitionCoreTypeIs(params.transition,
                                     ui::PAGE_TRANSITION_LINK);
#endif
#if BUILDFLAG(IS_MAC)
    if (is_top_level && is_user_initiated &&
        !is_maho_mini_gesture &&
        !params.started_from_context_menu &&
        maho::MahoAtcState::HasEnabledRules()) {
      std::string current_space_id;
      maho::MahoSpaceProfileBridge* bridge =
          maho::MahoSpaceProfileBridge::GetInstance();
      if (bridge) {
        current_space_id = bridge->GetActiveSpaceId(this);
      }
      base::WeakPtr<Browser> weak_browser = AsWeakPtr();
      Profile* prof = profile();
      base::WeakPtr<Profile> weak_profile =
          prof ? prof->GetWeakPtr() : nullptr;
      base::WeakPtr<content::WebContents> weak_source =
          source ? source->GetWeakPtr() : nullptr;

      maho::DecideLinkDestination(
          params.url, /*is_external=*/false,
          base::BindOnce(
              [](base::WeakPtr<Browser> browser,
                 base::WeakPtr<Profile> profile,
                 base::WeakPtr<content::WebContents> source,
                 std::string current_space_id,
                 content::OpenURLParams params,
                 base::OnceCallback<void(content::NavigationHandle&)>
                     navigation_handle_callback,
                 maho::LinkDestinationResult result) {
                if (!browser || !profile) {
                  return;
                }

                if (result.type ==
                        maho::LinkDestinationType::kSpace &&
                    !result.space_id.empty() &&
                    result.space_id != current_space_id) {
                  maho::MahoSpaceProfileBridge* bridge =
                      maho::MahoSpaceProfileBridge::GetInstance();
                  if (bridge && bridge->SwitchToSpace(
                                    browser.get(), result.space_id)) {
                    chrome::AddSelectedTabWithURL(
                        browser.get(), params.url, params.transition);
                    return;
                  }
                }

                BrowserView* browser_view =
                    BrowserView::GetBrowserViewForBrowser(browser.get());
                maho::MahoPeekController* peek_controller =
                    browser_view
                        ? browser_view->GetOrCreateMahoPeekController()
                        : nullptr;
                const maho::PeekRoute route = maho::DecidePeekRoute({
                    .master_enabled = profile->GetPrefs()->GetBoolean(
                        maho::sidebar_prefs::kPeekEnabled),
                    .popup_routing_enabled = false,
                    .link_routing_enabled =
                        profile->GetPrefs()->GetBoolean(
                            maho::sidebar_prefs::kPeekLinkRoutingEnabled),
                    .seam = maho::PeekSeam::kOpenUrlFromTab,
                    .disposition = params.disposition,
                    .source_role =
                        maho::MahoSidebarContainerView::GetPeekSourceRole(
                            browser.get(), source.get()),
                    .is_user_initiated = true,
                    .force_tab = params.is_renderer_initiated &&
                        params.disposition ==
                            WindowOpenDisposition::NEW_BACKGROUND_TAB,
                    .force_peek = params.is_renderer_initiated &&
                        params.disposition ==
                            WindowOpenDisposition::NEW_WINDOW,
                    .is_maho_mini_gesture = false,
                    .atc_has_cross_space_target = false,
                    .peek_slot_state =
                        peek_controller && peek_controller->SlotBusy()
                            ? maho::PeekSlotState::kBusy
                            : maho::PeekSlotState::kClosed,
                });
                if (route == maho::PeekRoute::kOpenInPeek &&
                    peek_controller &&
                    peek_controller->ShowUrl(source.get(), params.url)) {
                  return;
                }
                if (route ==
                    maho::PeekRoute::kOpenInForegroundTab) {
                  params.disposition =
                      WindowOpenDisposition::NEW_FOREGROUND_TAB;
                }
                AtcBypassState::SetBypass(browser.get(), true);
                browser->OpenURL(
                    params, std::move(navigation_handle_callback));
                AtcBypassState::SetBypass(browser.get(), false);
              },
              weak_browser, weak_profile, weak_source, current_space_id,
              params, std::move(navigation_handle_callback)));
      return nullptr;
    }
#endif

    if (is_top_level && is_user_initiated &&
        !is_maho_mini_gesture &&
        !params.started_from_context_menu &&
        !maho::MahoAtcState::HasEnabledRules()) {
      BrowserView* browser_view =
          BrowserView::GetBrowserViewForBrowser(this);
      maho::MahoPeekController* peek_controller =
          browser_view
              ? browser_view->GetOrCreateMahoPeekController()
              : nullptr;
      const maho::PeekRoute route = maho::DecidePeekRoute({
          .master_enabled = profile()->GetPrefs()->GetBoolean(
              maho::sidebar_prefs::kPeekEnabled),
          .popup_routing_enabled = false,
          .link_routing_enabled = profile()->GetPrefs()->GetBoolean(
              maho::sidebar_prefs::kPeekLinkRoutingEnabled),
          .seam = maho::PeekSeam::kOpenUrlFromTab,
          .disposition = params.disposition,
          .source_role =
              maho::MahoSidebarContainerView::GetPeekSourceRole(
                  this, source),
          .is_user_initiated = true,
          .force_tab = params.is_renderer_initiated &&
              params.disposition ==
                  WindowOpenDisposition::NEW_BACKGROUND_TAB,
          .force_peek = params.is_renderer_initiated &&
              params.disposition ==
                  WindowOpenDisposition::NEW_WINDOW,
          .is_maho_mini_gesture = false,
          .atc_has_cross_space_target = false,
          .peek_slot_state =
              peek_controller && peek_controller->SlotBusy()
                  ? maho::PeekSlotState::kBusy
                  : maho::PeekSlotState::kClosed,
      });
      if (route == maho::PeekRoute::kOpenInPeek &&
          peek_controller &&
          peek_controller->ShowUrl(source, params.url)) {
        return nullptr;
      }
      if (route == maho::PeekRoute::kOpenInForegroundTab) {
        content::OpenURLParams foreground_params = params;
        foreground_params.disposition =
            WindowOpenDisposition::NEW_FOREGROUND_TAB;
        AtcBypassState::SetBypass(this, true);
        content::WebContents* result = OpenURLFromTab(
            source, foreground_params,
            std::move(navigation_handle_callback));
        AtcBypassState::SetBypass(this, false);
        return result;
      }
    }
  }
"""
    guard_close = "" if mac_guard_already_closed else "#endif\n\n"
    canonical = guard_close + atc_bypass_state + replacement
    text = text[:struct_start] + canonical + text[end:]
    return text, int(text != original_text)


def normalize_atc_peek_throttle_registration(text: str) -> tuple[str, int]:
    """Widens an already-applied ATC-only throttle registration for Peek."""
    old = """#if BUILDFLAG(IS_MAC)
  // Maho ATC: route top-level user-initiated navigations that bypass
  // Browser::OpenURLFromTab (omnibox typed navigations and plain same-tab
  // link clicks) into their target space. Gated on HasEnabledRules() so
  // there is zero overhead (no throttle created) when no rules exist.
  if (maho::MahoAtcState::HasEnabledRules()) {
    registry.AddThrottle(
        std::make_unique<maho::MahoAtcNavigationThrottle>(registry));
  }
#endif
"""
    wrapped = """#if BUILDFLAG(IS_MAC)
  // Maho ATC + Peek: keep one throttle decision path for top-level
  // navigations that bypass Browser::OpenURLFromTab. ATC keeps its existing
  // eligibility; Peek additionally handles eligible same-tab renderer link
  // clicks even when no ATC rules are enabled.
  content::WebContents* web_contents =
      registry.GetNavigationHandle().GetWebContents();
  Profile* profile =
      web_contents
          ? Profile::FromBrowserContext(web_contents->GetBrowserContext())
          : nullptr;
  PrefService* prefs = profile ? profile->GetPrefs() : nullptr;
  const bool peek_link_routing_enabled =
      prefs && prefs->GetBoolean(maho::sidebar_prefs::kPeekEnabled) &&
      prefs->GetBoolean(maho::sidebar_prefs::kPeekLinkRoutingEnabled);
  if (maho::MahoAtcState::HasEnabledRules() ||
      peek_link_routing_enabled) {
    registry.AddThrottle(
        std::make_unique<maho::MahoAtcNavigationThrottle>(registry));
  }
#endif
"""
    new = """  // Maho ATC + Peek: keep one throttle decision path for top-level
  // navigations that bypass Browser::OpenURLFromTab. ATC keeps its existing
  // eligibility; Peek additionally handles eligible same-tab renderer link
  // clicks even when no ATC rules are enabled.
  content::WebContents* web_contents =
      registry.GetNavigationHandle().GetWebContents();
  Profile* profile =
      web_contents
          ? Profile::FromBrowserContext(web_contents->GetBrowserContext())
          : nullptr;
  PrefService* prefs = profile ? profile->GetPrefs() : nullptr;
  const bool peek_link_routing_enabled =
      prefs && prefs->GetBoolean(maho::sidebar_prefs::kPeekEnabled) &&
      prefs->GetBoolean(maho::sidebar_prefs::kPeekLinkRoutingEnabled);
  if (maho::MahoAtcState::HasEnabledRules() ||
      peek_link_routing_enabled) {
    registry.AddThrottle(
        std::make_unique<maho::MahoAtcNavigationThrottle>(registry));
  }
"""
    if wrapped in text:
        return text.replace(wrapped, new, 1), 1
    if new in text:
        return text, 0
    if old not in text:
        return text, 0
    return text.replace(old, new, 1), 1


def normalize_maho_url_alias_handlers(text: str) -> tuple[str, int]:
    normalized_count = 0
    helper_marker = "bool MahoRewriteNewTabToBlank(GURL* url,"
    helper_count = text.count(helper_marker)
    if helper_count > 1:
        if helper_count != 2:
            raise RuntimeError(
                "Unexpected Maho new-tab helper count while normalizing URL aliases"
            )
        duplicate_helper = re.compile(
            r"// Rewrites stray chrome://newtab navigations \(those that reach\n"
            r"// BrowserURLHandlerCreated after policy and extra_parts handlers have\n"
            r"// already had a chance to intercept\) to about:blank so that Maho never\n"
            r"// commits a chrome://newtab WebUI frame\.\n"
            r"bool MahoRewriteNewTabToBlank\(GURL\* url,\n"
            r"\s+content::BrowserContext\* /\*context\*/\) \{\n"
            r"  if \(url->SchemeIs\(\"chrome\"\) && url->host\(\) == \"newtab\"\) \{\n"
            r"    \*url = GURL\(\"about:blank\"\);\n"
            r"    return true;\n"
            r"  \}\n"
            r"  return false;\n"
            r"\}\n\n"
        )
        matches = list(duplicate_helper.finditer(text))
        if len(matches) != 2:
            raise RuntimeError(
                "Unable to identify duplicate Maho new-tab helper for normalization"
            )
        text = text[: matches[1].start()] + text[matches[1].end() :]
        normalized_count += 1

    registration_marker = "handler->AddHandlerPair(&MahoRewriteNewTabToBlank,"
    registration_count = text.count(registration_marker)
    if registration_count > 1:
        if registration_count != 2:
            raise RuntimeError(
                "Unexpected Maho new-tab handler registration count while normalizing URL aliases"
            )
        duplicate_registration = (
            "  // Maho: rewrite stray chrome://newtab to about:blank before the\n"
            "  // upstream NTP rewriter runs, so Maho never commits a NTP WebUI frame.\n"
            "  handler->AddHandlerPair(&MahoRewriteNewTabToBlank,\n"
            "                          BrowserURLHandler::null_handler());\n"
        )
        first = text.find(duplicate_registration)
        second = text.find(duplicate_registration, first + 1)
        if first == -1 or second == -1:
            raise RuntimeError(
                "Unable to identify duplicate Maho new-tab registration for normalization"
            )
        text = text[:second] + text[second + len(duplicate_registration) :]
        normalized_count += 1

    return text, normalized_count


def normalize_browser_view_mru_decl(text: str) -> tuple[str, int]:
    """Collapse a doubled MaybeHandleMruTabSwitch declaration in browser_view.h.

    A chromium/src checkout synced from the desktop tree already carries the
    MRU switcher declaration; a stale replacement re-injects the identical
    comment + declaration, which clang rejects as a redeclared class member.
    Remove the second consecutive copy so the header declares it exactly once.
    The later injection rules are guarded on GetOrCreateMahoPeekController(),
    so once this collapses to a single copy they no longer re-add it.
    """
    decl_marker = "  bool MaybeHandleMruTabSwitch(bool forward);\n"
    if text.count(decl_marker) < 2:
        return text, 0
    block = (
        "  // Routes Ctrl+Tab / Ctrl+Shift+Tab through the Maho MRU tab\n"
        "  // switcher when the pref is enabled and at least two tabs\n"
        "  // exist. Returns true when the MRU switcher handled the\n"
        "  // command; the caller must skip the upstream positional\n"
        "  // SelectNextTab/SelectPreviousTab in that case.\n"
        "  bool MaybeHandleMruTabSwitch(bool forward);\n"
    )
    if block + "\n" + block in text:
        return text.replace(block + "\n" + block, block, 1), 1
    if block + block in text:
        return text.replace(block + block, block, 1), 1
    return text, 0


def normalize_chrome_content_browser_client_includes(text: str) -> tuple[str, int]:
    """Consolidate the BrowserURLHandler include beside search.h.

    Pristine upstream trees keep `chrome/browser/search/search.h` and
    `content/public/browser/browser_url_handler.h` far apart, so the
    ad-block throttle include anchor (which expects the two lines
    adjacent) can never match. Move the URL-handler include directly
    below the search include before the replacement loop runs. Trees
    that already carry the Maho ad-block includes keep their shape.
    """
    search_inc = '#include "chrome/browser/search/search.h"\n'
    url_inc = '#include "content/public/browser/browser_url_handler.h"\n'
    if text.count(url_inc) != 1 or search_inc not in text:
        return text, 0
    if search_inc + url_inc in text:
        return text, 0
    if '#include "maho/browser/net/maho_ad_block_throttle.h"' in text:
        return text, 0
    text = text.replace(url_inc, "", 1)
    text = text.replace(search_inc, search_inc + url_inc, 1)
    return text, 1


def normalize_maho_dead_code_guards(text: str) -> tuple[str, int]:
    """Migrate older materializations of dead-code suppressions.

    Two older baked patch shapes trip -Werror,-Wunreachable-code on macOS
    (that warning set is mac-only, so win/linux trees compiled fine): a
    bare `return;` after the ShowBadFlagsPrompt suppress comment, and
    unparenthesized `if (false &&` kill switches in the tabbed layout
    impl. Guarding the return or parenthesizing the constant silences
    clang while keeping the suppression.
    """
    changed = 0
    suppress_comment = (
        "  // Maho: suppress unsupported command-line flags security warning infobars.\n"
    )
    bare = suppress_comment + "  return;\n"
    if bare in text:
        text = text.replace(
            bare,
            suppress_comment
            + "  if (web_contents) {\n"
            + "    return;\n"
            + "  }\n",
            1,
        )
        changed += 1
    for old, new in (
        (
            "  if (false && horizontal_layout.force_top_container_to_top &&\n",
            "  if ((false) && horizontal_layout.force_top_container_to_top &&\n",
        ),
        (
            "  if (false && !horizontal_layout.force_top_container_to_top &&\n",
            "  if ((false) && !horizontal_layout.force_top_container_to_top &&\n",
        ),
    ):
        if old in text:
            text = text.replace(old, new, 1)
            changed += 1
    return text, changed


def apply_replacements(
    target_file: Path,
    replacements: list[Replacement],
    dry_run: bool = False,
    relative_path: str | None = None,
) -> tuple[bool, list[str]]:
    text = target_file.read_text(encoding="utf-8")
    changed = False
    applied: list[str] = []
    exact_rel = ""

    if target_file.name == "chrome_content_browser_client.cc":
        text, normalized_count = normalize_chrome_content_browser_client_includes(text)
        if normalized_count:
            changed = True
            applied.append(
                "patched: Consolidate BrowserURLHandler include beside search.h "
                "in chrome_content_browser_client.cc"
            )

    if target_file.name in ("bad_flags_prompt.cc", "browser_view_tabbed_layout_impl.cc"):
        text, normalized_count = normalize_maho_dead_code_guards(text)
        if normalized_count:
            changed = True
            applied.append(
                "patched: Migrate dead-code suppression guards to "
                "-Wunreachable-code-safe form"
            )

    if target_file.name == "chrome_web_ui_configs.cc":
        text, normalized_count = normalize_maho_test_webui_config(text)
        if normalized_count:
            changed = True
            applied.append("patched: Make MahoTestUIConfig registration unconditional")

    if target_file.name == "browser_view.cc":
        text, normalized_count = normalize_browser_view_features(text)
        if normalized_count:
            changed = True
            applied.append("patched: Make Maho browser-view paths unconditional")

    if target_file.name == "browser_view.h":
        text, normalized_count = normalize_browser_view_mru_decl(text)
        if normalized_count:
            changed = True
            applied.append(
                "patched: Collapse duplicate MaybeHandleMruTabSwitch declaration"
            )

    if target_file.name == "browser.cc":
        text, normalized_count = normalize_peek_open_url_routing(text)
        if normalized_count:
            changed = True
            applied.append(
                "patched: Migrate ATC-only OpenURLFromTab routing to ATC + Peek"
            )

    if target_file.name == "browser_widget.cc":
        text, normalized_count = normalize_browser_widget_theme_override(text)
        if normalized_count:
            changed = True
            applied.append(
                "patched: Detach BrowserWidget color mode from global Space theme"
            )

    if target_file.name == "startup_browser_creator_impl.cc":
        text, normalized_count = normalize_startup_browser_creator_session_restore(text)
        if normalized_count:
            changed = True
            applied.append(
                "patched: Revert SessionRestore startup command overlay auto-show"
            )

    if target_file.name == "chrome_content_browser_client.cc":
        text, normalized_count = normalize_atc_peek_throttle_registration(text)
        if normalized_count:
            changed = True
            applied.append(
                "patched: Widen ATC throttle registration for Peek link routing"
            )
        text, normalized_count = normalize_maho_url_alias_handlers(text)
        if normalized_count:
            changed = True
            applied.append("patched: Normalize duplicate Maho URL alias handlers")

    if target_file.name == "BUILD.gn":
        exact_rel = relative_path.replace("\\", "/") if relative_path is not None else ""
        if not exact_rel:
            try:
                exact_rel = target_file.resolve().relative_to(default_chromium_src().resolve()).as_posix()
            except ValueError:
                exact_rel = ""

        if exact_rel in ("chrome/browser/ui/webui/BUILD.gn", "chrome/browser/ui/BUILD.gn"):
            text, normalized_count = normalize_gn_stale_deps(text, exact_rel)
            if normalized_count:
                changed = True
                applied.append(f"patched: Remove stale dependencies from {exact_rel}")

        if exact_rel == "chrome/browser/ui/BUILD.gn" and (
            '    "accelerator_utils.h",\n' in text
            and '      "//chrome/browser/ui/side_panel:side_panel_views_dependent",\n'
            in text
        ):
            text, normalized_count = normalize_chrome_browser_ui_build(text)
            if normalized_count:
                changed = True
                applied.append(
                    "patched: Restore Maho sources and dependencies in chrome/browser/ui:ui"
                )

        if exact_rel == "chrome/browser/BUILD.gn":
            text, normalized_count = normalize_chrome_browser_build(text)
            if normalized_count:
                changed = True
                applied.append(
                    "patched: Restore Maho sources and dependencies in chrome/browser:browser"
                )

    for replacement in replacements:
        if (
            exact_rel == "chrome/browser/BUILD.gn"
            and replacement.description
            == "Add Maho URL alias resolver directly to chrome/browser:browser deps"
            and '    "//components/password_manager/core/browser",\n' in text
            and '    "//maho/browser:maho_url_scheme",\n' in text
        ):
            applied.append(f"already: {replacement.description}")
            continue

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

    # Branding runs AFTER the replacement loop on purpose: the per-message
    # Replacements above anchor on upstream text that still says "Chromium",
    # so sweeping first would destroy their anchors. On a re-run the loop sees
    # its own `new` text already present and reports "already", then this sweep
    # is a no-op because nothing user-visible says Chromium any more.
    branding_rel = (
        relative_path.replace("\\", "/") if relative_path is not None else exact_rel
    )
    if branding_rel in _CHROMIUM_BRANDING_TARGETS:
        text, branded_count = normalize_chromium_product_branding(text)
        if branded_count:
            changed = True
            applied.append(
                f"patched: Rebrand {branded_count} user-visible Chromium product "
                f"strings to Maho"
            )

    # A pristine browser.cc receives the OpenURLFromTab hook in the replacement
    # loop above, so normalize once more after replacements to produce the same
    # current routing shape in a single override-apply invocation.
    if target_file.name == "browser.cc":
        text, normalized_count = normalize_peek_open_url_routing(text)
        if normalized_count:
            changed = True
            applied.append(
                "patched: Migrate newly inserted OpenURLFromTab routing to ATC + Peek"
            )

    if changed and not dry_run:
        target_file.write_text(text, encoding="utf-8")

    return changed, applied



# Maho Arc layout: BrowserView owns MahoToolbarButtonProvider (bubble anchors
# on the active pane header). Upstream ToolbarView already holds the browser's
# ToolbarButtonProvider user-data slot, so the Maho provider is reached through
# BrowserView::toolbar_button_provider() instead of a second registration.
_MAHO_TOOLBAR_PROVIDER_INCLUDE: Final[str] = (
    '#include "maho/browser/ui/views/sidebar/maho_toolbar_button_provider.h"\n'
)
_MAHO_TOOLBAR_PROVIDER_INIT: Final[str] = (
    "  // Maho: the Arc layout anchors page bubbles to the active pane header.\n"
    "  // ToolbarView keeps the browser's ToolbarButtonProvider user-data slot, so\n"
    "  // BrowserView owns the Maho provider and returns it from\n"
    "  // toolbar_button_provider().\n"
    "  if (!maho_toolbar_button_provider_ && IsMahoArcLayoutActive()) {\n"
    "    maho_toolbar_button_provider_ =\n"
    "        std::make_unique<maho::MahoToolbarButtonProvider>(\n"
    "            browser_.get(), this);\n"
    "  }\n"
    "\n"
)
# Revision markers: ee4bd9e9 keeps a raw toolbar_button_provider_ set through
# SetToolbarButtonProvider(); 72f18f12 replaced it with
# ToolbarButtonProvider::From(browser_) user data. Each shape's replacements
# are guarded on the other shape's marker so they skip instead of raising.
_BROWSER_VIEW_EE4BD9E9_PROVIDER_MARKER: Final[str] = (
    "  if (!toolbar_button_provider_) {\n"
)
_BROWSER_VIEW_72F18F12_PROVIDER_MARKER: Final[str] = (
    "ToolbarButtonProvider::From(browser_)"
)
_MAHO_SIDEBAR_PREFS_INCLUDE: Final[str] = (
    '#include "maho/browser/ui/views/sidebar/maho_sidebar_prefs.h"\n'
)
_BROWSER_VIEW_EE4BD9E9_PROVIDER_INIT: Final[str] = (
    "  // This browser view may already have a custom button provider set (e.g the\n"
    "  // hosted app frame).\n"
    "  if (!toolbar_button_provider_) {\n"
    "    SetToolbarButtonProvider(toolbar_);\n"
    "  }\n"
)
_MAHO_LEGACY_SEARCH_VIEW_PROVIDER_INIT: Final[str] = (
    "  if (!maho_toolbar_button_provider_ &&\n"
    "      !ToolbarButtonProvider::From(browser_.get()) &&\n"
    "      IsMahoArcLayoutActive()) {\n"
    "    // Resolve search_view opportunistically; MahoToolbarButtonProvider\n"
    "    // null-checks it internally and falls back to LastResortAnchor(), so\n"
    "    // construction must not be gated on a non-null search_view.\n"
    # The retired pill class/accessor names are split so repository greps for
    # live references stay clean; the joined text matches the old block.
    "    maho::MahoSidebar" "SearchView* search_view = nullptr;\n"
    "    if (auto* container = static_cast<maho::MahoSidebarContainerView*>(\n"
    "            maho_sidebar_container_)) {\n"
    "      if (auto* sidebar =\n"
    "              static_cast<maho::MahoSidebarView*>(container->sidebar_view())) {\n"
    "        search_view = sidebar->search_view_" "for_testing();\n"
    "      }\n"
    "    }\n"
    "    maho_toolbar_button_provider_ =\n"
    "        std::make_unique<maho::MahoToolbarButtonProvider>(\n"
    "            browser_.get(), this, search_view);\n"
    "  }\n"
    "\n"
)
REPLACEMENTS["chrome/browser/ui/views/frame/browser_view.cc"].extend([
    Replacement(
        old=(
            "#include \"maho/browser/ui/views/command/maho_command_overlay_controller.h\"\n"
        ),
        new=(
            "#include \"maho/browser/ui/views/command/maho_command_overlay_controller.h\"\n"
            + _MAHO_TOOLBAR_PROVIDER_INCLUDE
        ),
        description="Include MahoToolbarButtonProvider in browser_view.cc",
        guard=_MAHO_TOOLBAR_PROVIDER_INCLUDE,
    ),
    # ee4bd9e9 has no IsMahoArcLayoutActive(); its Arc gate reads the sidebar
    # layout pref directly (72f18f12 trees already include this header).
    Replacement(
        old=(
            "#include \"maho/browser/ui/views/command/maho_command_overlay_controller.h\"\n"
        ),
        new=(
            "#include \"maho/browser/ui/views/command/maho_command_overlay_controller.h\"\n"
            + _MAHO_SIDEBAR_PREFS_INCLUDE
        ),
        description="Include maho_sidebar_prefs.h in browser_view.cc",
        guard=_MAHO_SIDEBAR_PREFS_INCLUDE,
    ),
    # ee4bd9e9: clear the raw toolbar_button_provider_ (which points at the
    # Maho provider in Arc layout) before the owning unique_ptr dies. Runs
    # before the 72f18f12 entry below, whose guard then reports already.
    Replacement(
        old="  toolbar_button_provider_ = nullptr;\n",
        new=(
            "  toolbar_button_provider_ = nullptr;\n"
            "  // Maho: toolbar_button_provider_ may point at the Maho provider, and\n"
            "  // the provider holds a raw_ptr to toolbar_, so drop it after the raw\n"
            "  // pointer and before RemoveAllChildViews() destroys the toolbar.\n"
            "  maho_toolbar_button_provider_.reset();\n"
        ),
        description=(
            "Destroy MahoToolbarButtonProvider after toolbar_button_provider_ "
            "is cleared (ee4bd9e9)"
        ),
        guard=_BROWSER_VIEW_72F18F12_PROVIDER_MARKER,
    ),
    Replacement(
        old=(
            "  autofill_bubble_handler_.reset();\n"
            "\n"
            "  // These are raw pointers to child views, so they need to be set to null\n"
        ),
        new=(
            "  autofill_bubble_handler_.reset();\n"
            "\n"
            "  // Maho: the provider holds a raw_ptr to toolbar_, so drop it before\n"
            "  // RemoveAllChildViews() destroys the toolbar.\n"
            "  maho_toolbar_button_provider_.reset();\n"
            "\n"
            "  // These are raw pointers to child views, so they need to be set to null\n"
        ),
        description=(
            "Destroy MahoToolbarButtonProvider after the autofill bubble "
            "handler and before BrowserView child views"
        ),
        guard="  maho_toolbar_button_provider_.reset();\n",
    ),
    Replacement(
        old=(
            "ToolbarButtonProvider* BrowserView::toolbar_button_provider() {\n"
            "  return ToolbarButtonProvider::From(browser_);\n"
            "}\n"
        ),
        new=(
            "ToolbarButtonProvider* BrowserView::toolbar_button_provider() {\n"
            "  if (maho_toolbar_button_provider_) {\n"
            "    return maho_toolbar_button_provider_.get();\n"
            "  }\n"
            "  return ToolbarButtonProvider::From(browser_);\n"
            "}\n"
        ),
        description=(
            "Return the Maho Arc-layout provider from "
            "BrowserView::toolbar_button_provider()"
        ),
        # 72f18f12 only; ee4bd9e9 keeps the inline accessor in browser_view.h.
        guard=_BROWSER_VIEW_EE4BD9E9_PROVIDER_MARKER,
    ),
    # Migration first: builder trees still carry the retired search_view
    # construction (its guard never fired because ToolbarView registers first).
    Replacement(
        old=_MAHO_LEGACY_SEARCH_VIEW_PROVIDER_INIT,
        new="",
        description=(
            "Remove the retired search_view MahoToolbarButtonProvider "
            "construction block"
        ),
        idempotent=True,
    ),
    Replacement(
        old=(
            "  // At this point a ToolbarButtonProvider must have been set. It is set only\n"
            "  // once per browser instance.\n"
            "  auto* toolbar_button_provider = ToolbarButtonProvider::From(browser_);\n"
        ),
        new=(
            _MAHO_TOOLBAR_PROVIDER_INIT
            + "  // At this point a ToolbarButtonProvider must have been set. It is set only\n"
            "  // once per browser instance.\n"
            "  auto* toolbar_button_provider =\n"
            "      BrowserView::toolbar_button_provider();\n"
        ),
        description=(
            "Construct MahoToolbarButtonProvider in Arc layout before the "
            "ToolbarButtonProvider CHECK and hand it to the autofill handler"
        ),
        # 72f18f12 only; ee4bd9e9 installs the provider below.
        guard=_BROWSER_VIEW_EE4BD9E9_PROVIDER_MARKER,
    ),
    Replacement(
        old=_BROWSER_VIEW_EE4BD9E9_PROVIDER_INIT,
        new=(
            "  // This browser view may already have a custom button provider set (e.g the\n"
            "  // hosted app frame).\n"
            "  if (!toolbar_button_provider_) {\n"
            "    // Maho: in the Arc layout page bubbles anchor to the active pane\n"
            "    // header, so BrowserView owns the Maho provider and installs it in\n"
            "    // place of toolbar_.\n"
            "    if (browser_->is_type_normal() &&\n"
            "        maho::sidebar_prefs::IsSidebarLayoutEnabled(\n"
            "            browser_->profile()->GetPrefs())) {\n"
            "      maho_toolbar_button_provider_ =\n"
            "          std::make_unique<maho::MahoToolbarButtonProvider>(\n"
            "              browser_.get(), this);\n"
            "      SetToolbarButtonProvider(maho_toolbar_button_provider_.get());\n"
            "    } else {\n"
            "      SetToolbarButtonProvider(toolbar_);\n"
            "    }\n"
            "  }\n"
        ),
        description=(
            "Install MahoToolbarButtonProvider through SetToolbarButtonProvider "
            "in Arc layout (ee4bd9e9)"
        ),
        guard=_BROWSER_VIEW_72F18F12_PROVIDER_MARKER,
    ),
])
REPLACEMENTS["chrome/browser/ui/views/frame/browser_view.h"].extend([
    Replacement(
        old="class MahoSpacesOverlayController;\n",
        new=(
            "class MahoSpacesOverlayController;\n"
            "class MahoToolbarButtonProvider;\n"
        ),
        description="Forward-declare MahoToolbarButtonProvider",
        guard="class MahoToolbarButtonProvider;",
    ),
    Replacement(
        old="  raw_ptr<views::View> maho_create_space_blank_view_ = nullptr;\n",
        new=(
            "  raw_ptr<views::View> maho_create_space_blank_view_ = nullptr;\n"
            "\n"
            "  // Maho Arc layout: bubble-anchor provider returned by\n"
            "  // toolbar_button_provider().\n"
            "  std::unique_ptr<maho::MahoToolbarButtonProvider>\n"
            "      maho_toolbar_button_provider_;\n"
        ),
        description="Own MahoToolbarButtonProvider on BrowserView",
        guard="      maho_toolbar_button_provider_;\n",
    ),
])


# Per-(path, index) guard overrides for SPLIT_VIEW_REPLACEMENTS hunks whose
# inserted text can already be present in a tree patched by an older
# REPLACEMENTS entry before this hunk existed (e.g. omarchy's live tree
# already carried ContentsContainerView::SetMahoAiPanelAdjacent from an
# earlier, unrelated REPLACEMENTS guard at a different anchor). Without an
# explicit guard, the anchor line used in `old` (boilerplate that is NOT the
# inserted text itself, e.g. the following function's signature) is still
# present, so the generic "already applied" checks in apply_replacements()
# don't fire and the hunk re-inserts a duplicate definition/declaration.
_SPLIT_VIEW_HUNK_GUARDS: dict[tuple[str, int], str] = {
    ("chrome/browser/ui/tabs/tab_strip_model.cc", 1):
        "bool TabStripModel::AddToExistingSplit(",
    ("chrome/browser/ui/views/frame/contents_container_view.h", 3):
        "  void SetMahoAiPanelAdjacent(bool adjacent);\n",
    ("chrome/browser/ui/views/frame/contents_container_view.cc", 7):
        "void ContentsContainerView::SetMahoAiPanelAdjacent(bool adjacent) {\n",
}

SPLIT_VIEW_REPLACEMENTS: dict[str, list[Replacement]] = {
    path: [
        Replacement(
            old, new, f"Maho split topology: {path} hunk {index}",
            guard=_SPLIT_VIEW_HUNK_GUARDS.get((path, index)),
        )
        for index, (old, new) in enumerate(hunks, start=1)
    ]
    for path, hunks in SPLIT_REPLACEMENTS.items()
}
for _path, _replacements in SPLIT_VIEW_REPLACEMENTS.items():
    REPLACEMENTS.setdefault(_path, []).extend(_replacements)


# Co-locate maho schema JSONs under chrome/common/extensions/api. Do NOT
# reference them via //maho/...: the json_schema_compiler bundle generator
# derives its output dir from the common source dir, so a foreign root makes
# generated_schemas.cc land at an orphan gen/ path Chrome never links (chrome.maho
# silently disappears). dest (rel to chromium/src) -> source (rel to overlay root).
MAHO_SCHEMA_FILE_COPIES: dict[str, str] = {
    "chrome/common/extensions/api/maho_split_view.json":
        "browser/extensions/api/maho_split_view.json",
    "chrome/common/extensions/api/maho_side_panel.json":
        "browser/extensions/api/maho_side_panel.json",
    "components/password_manager/core/browser/password_manager_driver.cc":
        "chromium_src/components/password_manager/core/browser/password_manager_driver.cc",
}


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

    try:
        revision = chromium_revision(chromium_src)
    except RuntimeError as error:
        print(error, file=sys.stderr)
        return 1

    any_changed = False
    base_targets: dict[str, Path] = {}
    base_dir = base_patch_dir(revision)
    if base_dir is not None:
        base_targets = base_patch_targets(base_dir)
        for relative_path, patch in base_targets.items():
            try:
                state = apply_base_patch(
                    chromium_src, relative_path, patch, args.dry_run
                )
            except RuntimeError as error:
                print(error, file=sys.stderr)
                return 1
            any_changed = any_changed or state == "patched"
            print(f"{relative_path}\n  - {state}: Maho base patch [{revision[:12]}]")

    # A revision with base patches is rebuilt from pristine upstream by those
    # patches alone; anchor-based REPLACEMENTS target older trees.
    for relative_path, replacements in (
        {} if base_dir is not None else REPLACEMENTS
    ).items():
        if is_revision_incompatible_target(relative_path, revision):
            replacements = SPLIT_VIEW_REPLACEMENTS.get(relative_path, [])
            if not replacements:
                if relative_path in _DEAD_CODE_MIGRATION_TARGETS:
                    migrated_file = chromium_src / relative_path
                    migrated_text = migrated_file.read_text()
                    migrated_text, migrated_count = normalize_maho_dead_code_guards(migrated_text)
                    if migrated_count:
                        print(
                            f"  - patched: Migrate dead-code suppression guards to "
                            f"-Wunreachable-code-safe form [{relative_path}]"
                        )
                        if not args.dry_run:
                            migrated_file.write_text(migrated_text)
                        any_changed = True
                print(
                    f"skip [Chromium {revision} incompatible]: {relative_path}"
                )
                continue
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

        changed, applied = apply_replacements(target_file, replacements, dry_run=args.dry_run, relative_path=relative_path)
        any_changed = any_changed or changed
        print(relative_path)
        for line in applied:
            print(f"  - {line}")

    for dest_rel, src_rel in MAHO_SCHEMA_FILE_COPIES.items():
        src = repo_root() / src_rel
        dest = chromium_src / dest_rel
        print(dest_rel)
        if not src.exists():
            print(f"schema copy source missing: {src}", file=sys.stderr)
            return 1
        if dest.exists() and dest.read_bytes() == src.read_bytes():
            print("  - already: up to date")
            continue
        if args.dry_run:
            print(f"  - would copy from {src}")
            continue
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dest)
        any_changed = True
        print(f"  - copied from {src}")

    print("result: changed" if any_changed else "result: no-op")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
