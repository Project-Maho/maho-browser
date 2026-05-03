#!/usr/bin/env python3
"""Selectors and known accessibility labels for Chromium Maho E2E."""

MAHO_APP_NAME = 'Maho'
DEFAULT_APP_PATH = '/Users/indo/code/project/maho-workspace/chromium/src/out/Release/Maho.app'
DEFAULT_INITIAL_URLS = (
    'https://example.com',
    'https://news.ycombinator.com',
)

SCENARIO_NAMES = (
    'launch_clean_profile',
    'sidebar_populates_real_tabs',
    'switch_tab_from_sidebar',
    'open_spaces_surface_via_footer_action',
    'create_space_entry_opens_surface',
    'create_space_real_flow',
    'sidebar_dnd_favorite_tile_reorder',
    'sidebar_dnd_tab_to_pinned_section',
    'sidebar_dnd_tab_to_folder',
    'sidebar_dnd_tab_reorder_within_regular_tabs',
    'sidebar_dnd_folder_reorder',
    'sidebar_dnd_tab_out_of_folder',
    'phase2_seeded_favorites_snapshot_assert',
    'phase2_seeded_folder_snapshot_assert',
    'phase2_pinned_section_support_contract_assert',
    'phase2_folder_support_contract_assert',
    'phase2_tab_reorder_support_contract_assert',
    'phase2_sidebar_tree_json_contract_assert',
)

KNOWN_BUTTONS = (
    'Back',
    'Forward',
    'Reload',
    'Maho AI',
    'Open spaces',
    'Spaces',
    'Switch space',
    'Create space',
)

STARTUP_INTERRUPTION_BUTTONS = (
    'Close',
    'Ignore',
    'Don’t Restore',
    "Don't Restore",
    'Reopen',
    'Restore',
)

STARTUP_INTERRUPTION_TEXTS = (
    'Restore previous pages?',
    'Chromium didn’t shut down correctly.',
    "Chromium didn't shut down correctly.",
    'Restore pages?',
)

KNOWN_TAB_TITLES = (
    'Example Domain',
    'Hacker News',
    'news.ycombinator.com',
    'Maho Spaces',
    'Manage Spaces',
    '+ New Tab',
)

KNOWN_TEXTS = (
    'Maho Test Page',
    'Snapshot',
    'Favorites',
    'Pinned tabs',
    'Normal tabs',
    'Folders',
    'Phase 2 Coverage',
    'Pinned favorites',
    'Tabs',
    'No tabs open',
    'Air Traffic Control',
    'SUPPORTED',
    'Sidebar tree JSON',
    'expanded=',
    'children=',
    'Favorites present',
    'Pinned tabs present',
    'Normal tabs present',
    'Spaces visible',
    'Manage Spaces',
    'New Space',
    'Space name',
    'Enter a name',
    'Create Space',
    'Cancel',
    'Configure Selected',
    'Current',
    'Switch',
    'Active',
    'GitHub',
    'Research',
    'Work',
    'folder',
    'collapsed',
    'expand',
    'favorite',
)
