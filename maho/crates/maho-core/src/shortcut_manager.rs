use maho_types::keyboard::*;

pub struct ShortcutManager {
    bindings: Vec<ShortcutBinding>,
    defaults: Vec<ShortcutBinding>,
}

fn is_reserved_combo(key_combo: &KeyCombo) -> bool {
    let key = key_combo.key.to_lowercase();
    let has_meta = key_combo.modifiers.contains(&KeyModifier::Meta);
    let has_ctrl = key_combo.modifiers.contains(&KeyModifier::Ctrl);
    let has_alt = key_combo.modifiers.contains(&KeyModifier::Alt);
    let has_shift = key_combo.modifiers.contains(&KeyModifier::Shift);

    // Platform-specific primary modifier
    let is_primary = if cfg!(target_os = "macos") {
        has_meta
    } else {
        has_ctrl
    };

    if is_primary && !has_alt && !has_shift {
        if key == "q" || key == "h" || key == "m" {
            return true;
        }
    }
    false
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

    fn get_now_secs() -> u64 {
        std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap_or_default()
            .as_secs()
    }

    pub fn set_shortcut(
        &mut self,
        action: &str,
        key_combo: KeyCombo,
    ) -> Result<(), SetShortcutError> {
        if is_reserved_combo(&key_combo) {
            return Err(SetShortcutError::Reserved);
        }
        if let Some(conflict) = self.check_conflict(action, &key_combo) {
            return Err(SetShortcutError::Conflict(conflict));
        }
        if let Some(binding) = self.bindings.iter_mut().find(|b| b.action == action) {
            binding.key_combo = key_combo;
            binding.is_custom = true;
            binding.updated_at = Self::get_now_secs();
        }
        Ok(())
    }

    pub fn check_conflict(&self, action: &str, key_combo: &KeyCombo) -> Option<ShortcutConflict> {
        if is_reserved_combo(key_combo) {
            return Some(ShortcutConflict {
                key_combo: key_combo.clone(),
                existing_action: "reserved".to_string(),
                new_action: action.to_string(),
            });
        }
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
        if let Some(binding) = self.bindings.iter_mut().find(|b| b.action == action) {
            binding.key_combo = binding.default_key_combo.clone();
            binding.is_custom = false;
            binding.updated_at = Self::get_now_secs();
        }
    }

    pub fn reset_all_shortcuts(&mut self) {
        let now = Self::get_now_secs();
        for b in &mut self.bindings {
            if b.is_custom {
                b.key_combo = b.default_key_combo.clone();
                b.is_custom = false;
                b.updated_at = now;
            }
        }
    }

    pub fn toggle_shortcut(&mut self, action: &str, enabled: bool) {
        if let Some(binding) = self.bindings.iter_mut().find(|b| b.action == action) {
            binding.enabled = enabled;
            binding.updated_at = Self::get_now_secs();
        }
    }

    pub fn merge_bindings(&mut self, incoming: Vec<ShortcutBinding>) {
        for incoming_b in incoming {
            if let Some(local_b) = self
                .bindings
                .iter_mut()
                .find(|b| b.action == incoming_b.action)
            {
                if incoming_b.updated_at > local_b.updated_at {
                    local_b.key_combo = incoming_b.key_combo;
                    local_b.is_custom = incoming_b.is_custom;
                    local_b.enabled = incoming_b.enabled;
                    local_b.updated_at = incoming_b.updated_at;
                }
            }
        }
    }

    pub fn restore_shortcut_state(
        &mut self,
        action: &str,
        key_combo: KeyCombo,
        enabled: bool,
        updated_at: u64,
    ) {
        if let Some(binding) = self.bindings.iter_mut().find(|b| b.action == action) {
            binding.key_combo = key_combo;
            binding.is_custom = true;
            binding.enabled = enabled;
            binding.updated_at = updated_at;
        }
    }

    pub fn get_default_binding(&self, action: &str) -> Option<&ShortcutBinding> {
        self.defaults.iter().find(|b| b.action == action)
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
        "command_bar",
        "Command Bar",
        nav.clone(),
        KeyCombo::new("l", vec![primary_modifier()]),
    ));
    // Windows/Linux address-bar alias (Chromium, Arc for Windows): Alt+D opens
    // the same palette as Ctrl+L. Without a registry binding Alt+D fell through
    // to upstream IDC_FOCUS_LOCATION, which focuses the hidden omnibox. macOS
    // has no Alt+D convention (Option+D types a character), so it is not bound
    // there.
    if !cfg!(target_os = "macos") {
        bindings.push(ShortcutBinding::new(
            "command_bar_alt",
            "Command Bar (Alternate)",
            nav.clone(),
            KeyCombo::new("d", vec![KeyModifier::Alt]),
        ));
    }

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
        "mru_tab_switch_next",
        "Next Tab (MRU)",
        tabs.clone(),
        KeyCombo::new("tab", vec![KeyModifier::Ctrl]),
    ));
    bindings.push(ShortcutBinding::new(
        "mru_tab_switch_prev",
        "Previous Tab (MRU)",
        tabs.clone(),
        KeyCombo::new("tab", vec![KeyModifier::Ctrl, KeyModifier::Shift]),
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
        KeyCombo::new("arrowright", vec![primary_modifier(), KeyModifier::Alt]),
    ));
    bindings.push(ShortcutBinding::new(
        "prev_space",
        "Previous Space",
        spaces.clone(),
        KeyCombo::new("arrowleft", vec![primary_modifier(), KeyModifier::Alt]),
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
    bindings.push(ShortcutBinding::new(
        "toggle_spaces_overlay",
        "Spaces Overlay",
        spaces.clone(),
        KeyCombo::new("s", vec![primary_modifier(), KeyModifier::Alt]),
    ));
    let space_num_modifier = if cfg!(target_os = "macos") {
        vec![KeyModifier::Ctrl]
    } else {
        vec![KeyModifier::Ctrl, KeyModifier::Alt]
    };
    for i in 1..=9 {
        bindings.push(ShortcutBinding::new(
            &format!("select_space_{}", i),
            &format!("Select Space {}", i),
            spaces.clone(),
            KeyCombo::new(&i.to_string(), space_num_modifier.clone()),
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
        "maho_mini",
        "Maho Mini",
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
        // Key must use the canonical codec spelling: VKeyToString(OEM_PLUS)
        // is "+", so "=" would never match a resolved key event.
        KeyCombo::new("+", vec![KeyModifier::Ctrl, KeyModifier::Shift]),
    ));
    bindings.push(ShortcutBinding::new(
        "add_split_view_arc",
        "Add Split View (Arc)",
        window.clone(),
        KeyCombo::new("+", vec![primary_modifier(), KeyModifier::Alt]),
    ));
    bindings.push(ShortcutBinding::new(
        "increase_split_view",
        "Increase Split View",
        window.clone(),
        KeyCombo::new("arrowright", vec![primary_modifier(), KeyModifier::Shift]),
    ));
    bindings.push(ShortcutBinding::new(
        "decrease_split_view",
        "Decrease Split View",
        window.clone(),
        KeyCombo::new("arrowleft", vec![primary_modifier(), KeyModifier::Shift]),
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
        "swap_split_view",
        "Swap Split View",
        window.clone(),
        KeyCombo::new("\\", vec![KeyModifier::Ctrl, KeyModifier::Shift]),
    ));
    bindings.push(ShortcutBinding::new(
        "toggle_split_view",
        "Toggle Split View",
        window.clone(),
        KeyCombo::new("enter", vec![KeyModifier::Ctrl, KeyModifier::Shift]),
    ));
    bindings.push(ShortcutBinding::new(
        "toggle_split_orientation",
        "Toggle Split Orientation",
        window.clone(),
        KeyCombo::new(
            "s",
            vec![primary_modifier(), KeyModifier::Shift, KeyModifier::Alt],
        ),
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
    // Arc parity: Cmd+Shift+2 (Ctrl+Shift+2 off macOS) starts the selected-area
    // screenshot flow, same as the utility panel's Screenshot button.
    bindings.push(ShortcutBinding::new(
        "capture_screenshot",
        "Capture Screenshot",
        view.clone(),
        KeyCombo::new("2", vec![primary_modifier(), KeyModifier::Shift]),
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

#[cfg(test)]
mod contract_tests {
    use super::*;
    use std::collections::HashSet;

    // Key strings the C++ codec (StringToVKey/VKeyToString in
    // maho-chromium/browser/ui/views/command/maho_shortcut_interceptor.cc)
    // can map. A binding outside this alphabet cannot register as a
    // FocusManager accelerator and cannot be resolved from a key event.
    // The C++ side enforces the same contract against the live registry in
    // maho_shortcut_contract_unittest.cc; maho-chromium/docs/shortcut-registry-contract.md
    // documents the full triple-sync contract.
    const CODEC_NAMED_KEYS: &[&str] = &[
        "enter",
        "escape",
        "tab",
        "space",
        "backspace",
        "delete",
        "arrowup",
        "arrowdown",
        "arrowleft",
        "arrowright",
        "home",
        "end",
        "pageup",
        "pagedown",
    ];
    const CODEC_SINGLE_CHARS: &str = "abcdefghijklmnopqrstuvwxyz0123456789,./[]-=+\\";

    fn is_codec_key(key: &str) -> bool {
        if key.len() == 1 {
            return CODEC_SINGLE_CHARS.contains(key);
        }
        if let Some(num) = key.strip_prefix('f') {
            if let Ok(n) = num.parse::<u8>() {
                return (1..=12).contains(&n);
            }
        }
        CODEC_NAMED_KEYS.contains(&key)
    }

    #[test]
    fn all_binding_keys_are_in_cpp_codec_alphabet() {
        let manager = ShortcutManager::new();
        assert!(manager.bindings.len() > 20, "default registry looks empty");
        for binding in &manager.bindings {
            assert!(
                is_codec_key(&binding.key_combo.key),
                "key {:?} (action {:?}) is outside the C++ codec alphabet; \
                 extend StringToVKey/VKeyToString or change the key spelling",
                binding.key_combo.key,
                binding.action,
            );
        }
    }

    #[test]
    fn no_duplicate_normalized_combos() {
        let manager = ShortcutManager::new();
        let mut seen: HashSet<String> = HashSet::new();
        for binding in &manager.bindings {
            assert!(
                seen.insert(binding.key_combo.to_normalized_string()),
                "action {:?} duplicates an existing normalized combo; the \
                 earlier binding in registration order wins and this one is \
                 unreachable",
                binding.action,
            );
        }
    }

    #[test]
    fn binding_actions_are_unique_and_nonempty() {
        let manager = ShortcutManager::new();
        let mut seen: HashSet<&str> = HashSet::new();
        for binding in &manager.bindings {
            assert!(!binding.action.is_empty(), "binding with empty action");
            assert!(
                seen.insert(binding.action.as_str()),
                "duplicate action id {:?}",
                binding.action,
            );
        }
    }

    #[test]
    fn spaces_overlay_shortcut_is_registered_and_collision_free() {
        let manager = ShortcutManager::new();
        let binding = manager
            .bindings
            .iter()
            .find(|b| b.action == "toggle_spaces_overlay")
            .expect("toggle_spaces_overlay must be registered");
        assert_eq!(binding.key_combo.key, "s");
        assert!(binding.key_combo.modifiers.contains(&KeyModifier::Alt));
        assert!(
            binding.key_combo.modifiers.contains(&if cfg!(target_os = "macos") {
                KeyModifier::Meta
            } else {
                KeyModifier::Ctrl
            })
        );
        assert!(
            !binding.key_combo.modifiers.contains(&KeyModifier::Shift),
            "toggle_spaces_overlay must not contain Shift"
        );
        assert_eq!(binding.category, ShortcutCategory::Spaces);

        // Explicit collision check: no duplicate full modifier combo across bindings
        let norm = binding.key_combo.to_normalized_string();
        let duplicates: Vec<&ShortcutBinding> = manager
            .bindings
            .iter()
            .filter(|b| b.key_combo.to_normalized_string() == norm)
            .collect();
        assert_eq!(
            duplicates.len(),
            1,
            "found duplicate full modifier combo for toggle_spaces_overlay: {duplicates:?}"
        );
    }
}
