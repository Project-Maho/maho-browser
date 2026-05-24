use maho_types::keyboard::*;

pub struct ShortcutManager {
    bindings: Vec<ShortcutBinding>,
    defaults: Vec<ShortcutBinding>,
}

impl ShortcutManager {
    pub fn new() -> Self {
        let defaults = register_default_bindings();
        let bindings = defaults.clone();
        Self { bindings, defaults }
    }

    pub fn get_all_bindings(&self) -> &[ShortcutBinding] {
        &self.bindings
    }

    pub fn get_bindings_by_category(&self, category: &ShortcutCategory) -> Vec<&ShortcutBinding> {
        self.bindings
            .iter()
            .filter(|b| &b.category == category)
            .collect()
    }

    pub fn set_shortcut(
        &mut self,
        action: &str,
        key_combo: KeyCombo,
    ) -> Result<(), ShortcutConflict> {
        if let Some(conflict) = self.check_conflict(action, &key_combo) {
            return Err(conflict);
        }
        if let Some(binding) = self.bindings.iter_mut().find(|b| b.action == action) {
            binding.key_combo = key_combo;
            binding.is_custom = true;
        }
        Ok(())
    }

    pub fn check_conflict(&self, action: &str, key_combo: &KeyCombo) -> Option<ShortcutConflict> {
        let normalized = key_combo.to_normalized_string();
        self.bindings
            .iter()
            .find(|b| {
                b.action != action && b.enabled && b.key_combo.to_normalized_string() == normalized
            })
            .map(|b| ShortcutConflict {
                key_combo: key_combo.clone(),
                existing_action: b.action.clone(),
                new_action: action.to_string(),
            })
    }

    pub fn reset_shortcut(&mut self, action: &str) {
        if let Some(default_binding) = self.defaults.iter().find(|b| b.action == action) {
            if let Some(binding) = self.bindings.iter_mut().find(|b| b.action == action) {
                binding.key_combo = default_binding.key_combo.clone();
                binding.is_custom = false;
            }
        }
    }

    pub fn reset_all_shortcuts(&mut self) {
        self.bindings = self.defaults.clone();
    }

    pub fn toggle_shortcut(&mut self, action: &str, enabled: bool) {
        if let Some(binding) = self.bindings.iter_mut().find(|b| b.action == action) {
            binding.enabled = enabled;
        }
    }

    pub fn find_binding_by_key_combo(&self, key_combo: &KeyCombo) -> Option<&ShortcutBinding> {
        let normalized = key_combo.to_normalized_string();
        self.bindings
            .iter()
            .find(|b| b.enabled && b.key_combo.to_normalized_string() == normalized)
    }
}

impl Default for ShortcutManager {
    fn default() -> Self {
        Self::new()
    }
}

fn register_default_bindings() -> Vec<ShortcutBinding> {
    fn primary_modifier() -> KeyModifier {
        if cfg!(target_os = "macos") {
            KeyModifier::Meta
        } else {
            KeyModifier::Ctrl
        }
    }

    let mut bindings = Vec::new();

    // === Navigation ===
    let nav = ShortcutCategory::Navigation;
    bindings.push(ShortcutBinding::new(
        "navigate_back",
        "Back",
        nav.clone(),
        KeyCombo::new("[", vec![primary_modifier()]),
    ));
    bindings.push(ShortcutBinding::new(
        "navigate_forward",
        "Forward",
        nav.clone(),
        KeyCombo::new("]", vec![primary_modifier()]),
    ));
    bindings.push(ShortcutBinding::new(
        "reload_tab",
        "Reload",
        nav.clone(),
        KeyCombo::new("r", vec![primary_modifier()]),
    ));
    bindings.push(ShortcutBinding::new(
        "hard_reload",
        "Hard Reload",
        nav.clone(),
        KeyCombo::new("r", vec![primary_modifier(), KeyModifier::Shift]),
    ));
    bindings.push(ShortcutBinding::new(
        "stop_loading",
        "Stop Loading",
        nav.clone(),
        KeyCombo::new(".", vec![primary_modifier()]),
    ));
    bindings.push(ShortcutBinding::new(
        "focus_url_bar",
        "Focus Address Bar",
        nav.clone(),
        KeyCombo::new("l", vec![primary_modifier()]),
    ));

    // === Tabs (Arc-style: ⌘T = New Tab, ⌘D = Pin) ===
    let tabs = ShortcutCategory::Tabs;
    bindings.push(ShortcutBinding::new(
        "new_tab",
        "New Tab",
        tabs.clone(),
        KeyCombo::new("t", vec![primary_modifier()]),
    ));
    bindings.push(ShortcutBinding::new(
        "close_tab",
        "Close Tab",
        tabs.clone(),
        KeyCombo::new("w", vec![primary_modifier()]),
    ));
    bindings.push(ShortcutBinding::new(
        "restore_tab",
        "Reopen Closed Tab",
        tabs.clone(),
        KeyCombo::new("t", vec![primary_modifier(), KeyModifier::Shift]),
    ));
    bindings.push(ShortcutBinding::new(
        "pin_tab",
        "Pin/Unpin Tab",
        tabs.clone(),
        KeyCombo::new("d", vec![primary_modifier()]),
    ));
    bindings.push(ShortcutBinding::new(
        "duplicate_tab",
        "Duplicate Tab",
        tabs.clone(),
        KeyCombo::new("d", vec![primary_modifier(), KeyModifier::Shift]),
    ));
    bindings.push(ShortcutBinding::new(
        "next_tab",
        "Next Tab",
        tabs.clone(),
        KeyCombo::new("arrowdown", vec![primary_modifier(), KeyModifier::Alt]),
    ));
    bindings.push(ShortcutBinding::new(
        "prev_tab",
        "Previous Tab",
        tabs.clone(),
        KeyCombo::new("arrowup", vec![primary_modifier(), KeyModifier::Alt]),
    ));
    bindings.push(ShortcutBinding::new(
        "clear_unpinned_tabs",
        "Clear Unpinned Tabs",
        tabs.clone(),
        KeyCombo::new("k", vec![primary_modifier(), KeyModifier::Shift]),
    ));
    bindings.push(ShortcutBinding::new(
        "copy_url",
        "Copy URL",
        tabs.clone(),
        KeyCombo::new("c", vec![primary_modifier(), KeyModifier::Shift]),
    ));
    bindings.push(ShortcutBinding::new(
        "copy_url_markdown",
        "Copy URL as Markdown",
        tabs.clone(),
        KeyCombo::new(
            "c",
            vec![primary_modifier(), KeyModifier::Shift, KeyModifier::Alt],
        ),
    ));
    for i in 1..=9 {
        bindings.push(ShortcutBinding::new(
            &format!("select_tab_{}", i),
            &format!("Select Tab {}", i),
            tabs.clone(),
            KeyCombo::new(&i.to_string(), vec![primary_modifier()]),
        ));
    }

    // === Spaces (Arc-style: ⌃1-9 = Space selection) ===
    let spaces = ShortcutCategory::Spaces;
    bindings.push(ShortcutBinding::new(
        "next_space",
        "Next Space",
        spaces.clone(),
        KeyCombo::new("Right", vec![primary_modifier(), KeyModifier::Alt]),
    ));
    bindings.push(ShortcutBinding::new(
        "prev_space",
        "Previous Space",
        spaces.clone(),
        KeyCombo::new("Left", vec![primary_modifier(), KeyModifier::Alt]),
    ));
    bindings.push(ShortcutBinding::new(
        "new_space",
        "New Space",
        spaces.clone(),
        KeyCombo::new(
            "n",
            vec![primary_modifier(), KeyModifier::Shift, KeyModifier::Alt],
        ),
    ));
    for i in 1..=9 {
        bindings.push(ShortcutBinding::new(
            &format!("select_space_{}", i),
            &format!("Select Space {}", i),
            spaces.clone(),
            KeyCombo::new(&i.to_string(), vec![KeyModifier::Ctrl]),
        ));
    }

    // === Window ===
    let window = ShortcutCategory::Window;
    bindings.push(ShortcutBinding::new(
        "toggle_sidebar",
        "Toggle Sidebar",
        window.clone(),
        KeyCombo::new("s", vec![primary_modifier()]),
    ));
    bindings.push(ShortcutBinding::new(
        "ai_panel",
        "AI Panel",
        window.clone(),
        KeyCombo::new("e", vec![primary_modifier()]),
    ));

    bindings.push(ShortcutBinding::new(
        "find_in_page",
        "Find in Page",
        window.clone(),
        KeyCombo::new("f", vec![primary_modifier()]),
    ));
    if cfg!(target_os = "macos") {
        bindings.push(ShortcutBinding::new(
            "fullscreen",
            "Fullscreen",
            window.clone(),
            KeyCombo::new("f", vec![KeyModifier::Ctrl, KeyModifier::Meta]),
        ));
    } else {
        bindings.push(ShortcutBinding::new(
            "fullscreen",
            "Fullscreen",
            window.clone(),
            KeyCombo::new("f11", vec![]),
        ));
    }
    bindings.push(ShortcutBinding::new(
        "minimize",
        "Minimize",
        window.clone(),
        KeyCombo::new("m", vec![primary_modifier()]),
    ));
    bindings.push(ShortcutBinding::new(
        "new_window",
        "New Window",
        window.clone(),
        KeyCombo::new("n", vec![primary_modifier()]),
    ));
    bindings.push(ShortcutBinding::new(
        "little_arc",
        "Little Arc",
        window.clone(),
        KeyCombo::new("n", vec![primary_modifier(), KeyModifier::Alt]),
    ));
    bindings.push(ShortcutBinding::new(
        "close_window",
        "Close Window",
        window.clone(),
        KeyCombo::new("w", vec![primary_modifier(), KeyModifier::Shift]),
    ));
    bindings.push(ShortcutBinding::new(
        "add_split_view",
        "Add Split View",
        window.clone(),
        KeyCombo::new("=", vec![KeyModifier::Ctrl, KeyModifier::Shift]),
    ));
    bindings.push(ShortcutBinding::new(
        "remove_split_view",
        "Remove Split View",
        window.clone(),
        KeyCombo::new("-", vec![KeyModifier::Ctrl, KeyModifier::Shift]),
    ));
    bindings.push(ShortcutBinding::new(
        "next_split_view",
        "Next Split View",
        window.clone(),
        KeyCombo::new("]", vec![KeyModifier::Ctrl, KeyModifier::Shift]),
    ));
    bindings.push(ShortcutBinding::new(
        "prev_split_view",
        "Previous Split View",
        window.clone(),
        KeyCombo::new("[", vec![KeyModifier::Ctrl, KeyModifier::Shift]),
    ));
    bindings.push(ShortcutBinding::new(
        "show_archive",
        "Show Archive",
        window.clone(),
        KeyCombo::new("a", vec![primary_modifier(), KeyModifier::Shift]),
    ));
    bindings.push(ShortcutBinding::new(
        "open_downloads",
        "Show Downloads",
        window.clone(),
        KeyCombo::new("j", vec![primary_modifier()]),
    ));
    bindings.push(ShortcutBinding::new(
        "settings",
        "Settings",
        window.clone(),
        KeyCombo::new(",", vec![primary_modifier()]),
    ));
    bindings.push(ShortcutBinding::new(
        "open_history",
        "History",
        window.clone(),
        KeyCombo::new("y", vec![primary_modifier()]),
    ));
    bindings.push(ShortcutBinding::new(
        "open_bookmarks",
        "Bookmarks",
        window.clone(),
        KeyCombo::new(
            "b",
            vec![primary_modifier(), KeyModifier::Shift, KeyModifier::Alt],
        ),
    ));
    bindings.push(ShortcutBinding::new(
        "new_note",
        "New Note",
        window.clone(),
        KeyCombo::new("n", vec![KeyModifier::Ctrl]),
    ));

    // === View ===
    let view = ShortcutCategory::View;
    bindings.push(ShortcutBinding::new(
        "zoom_in",
        "Zoom In",
        view.clone(),
        KeyCombo::new("+", vec![primary_modifier()]),
    ));
    bindings.push(ShortcutBinding::new(
        "zoom_out",
        "Zoom Out",
        view.clone(),
        KeyCombo::new("-", vec![primary_modifier()]),
    ));
    bindings.push(ShortcutBinding::new(
        "reset_zoom",
        "Reset Zoom",
        view.clone(),
        KeyCombo::new("0", vec![primary_modifier()]),
    ));

    // === Developer ===
    let dev = ShortcutCategory::Developer;
    bindings.push(ShortcutBinding::new(
        "toggle_dev_tools",
        "Developer Tools",
        dev.clone(),
        KeyCombo::new("i", vec![primary_modifier(), KeyModifier::Alt]),
    ));
    bindings.push(ShortcutBinding::new(
        "view_source",
        "View Source",
        dev.clone(),
        KeyCombo::new("u", vec![primary_modifier(), KeyModifier::Alt]),
    ));
    bindings.push(ShortcutBinding::new(
        "js_console",
        "JavaScript Console",
        dev.clone(),
        KeyCombo::new("j", vec![primary_modifier(), KeyModifier::Alt]),
    ));

    // === Custom (Maho-specific features) ===
    bindings.push(ShortcutBinding::new(
        "open_boost_editor",
        "Boost Editor",
        ShortcutCategory::Custom,
        KeyCombo::new("b", vec![primary_modifier(), KeyModifier::Alt]),
    ));
    bindings.push(ShortcutBinding::new(
        "open_atc_rules",
        "ATC Rules",
        ShortcutCategory::Custom,
        KeyCombo::new("a", vec![primary_modifier(), KeyModifier::Alt]),
    ));

    bindings
}
