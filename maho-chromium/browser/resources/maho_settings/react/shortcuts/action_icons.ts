import React from 'react';
import {
  Search,
  Pin,
  History,
  Sparkles,
  Download,
  ExternalLink,
  Code,
  Check,
  Copy,
  Trash2,
} from '@icons/lucide';
import {ShortcutActionId} from './types.js';

export const ACTION_ICONS: Record<ShortcutActionId, React.ComponentType<any>> = {
  // Navigation
  "command_bar": Search,
  "command_bar_alt": Search,
  "focus_url_bar": Search,
  "copy_url": Copy,
  "open_history": History,
  "open_downloads": Download,
  "new_space": Sparkles,
  "go_back": Check,
  "go_forward": Check,
  "reload_tab": Check,
  "hard_reload": Check,
  "stop_loading": Check,

  // Tabs
  "new_tab": Sparkles,
  "close_tab": Trash2,
  "restore_tab": History,
  "pin_tab": Pin,
  "duplicate_tab": Copy,
  "next_tab": Check,
  "prev_tab": Check,
  "select_tab_1": Sparkles,
  "select_tab_2": Sparkles,
  "select_tab_3": Sparkles,
  "select_tab_4": Sparkles,
  "select_tab_5": Sparkles,
  "select_tab_6": Sparkles,
  "select_tab_7": Sparkles,
  "select_tab_8": Sparkles,
  "select_tab_last": Sparkles,

  // Spaces
  "toggle_sidebar": ExternalLink,
  "ai_panel": Sparkles,
  "find_in_page": Search,
  "zoom_in": ExternalLink,
  "zoom_out": ExternalLink,
  "reset_zoom": ExternalLink,
  "view_source": Code,
  "print_page": ExternalLink,
  "settings": ExternalLink,
  "maho_mini": Sparkles,
  "next_space": ExternalLink,
  "prev_space": ExternalLink,
  "select_space_1": Sparkles,
  "select_space_2": Sparkles,
  "select_space_3": Sparkles,
  "select_space_4": Sparkles,
  "select_space_5": Sparkles,
  "select_space_6": Sparkles,
  "select_space_7": Sparkles,
  "select_space_8": Sparkles,
  "select_space_last": Sparkles,

  // Split-view
  "add_split_view": ExternalLink,
  "add_split_view_arc": ExternalLink,
  "increase_split_view": ExternalLink,
  "decrease_split_view": ExternalLink,
  "remove_split_view": ExternalLink,
  "next_split_view": ExternalLink,
  "prev_split_view": ExternalLink,
  "swap_split_view": ExternalLink,
};

export function getIconForAction(action: ShortcutActionId): React.ComponentType<any> {
  return ACTION_ICONS[action] || Sparkles;
}
