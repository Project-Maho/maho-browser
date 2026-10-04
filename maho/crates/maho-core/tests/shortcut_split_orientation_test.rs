use maho_core::shortcut_manager::ShortcutManager;
use maho_types::keyboard::KeyModifier;

#[test]
fn split_orientation_shortcut_matches_platform_contract_and_is_unique() {
    let manager = ShortcutManager::new();
    let binding = manager
        .get_all_bindings()
        .iter()
        .find(|binding| binding.action == "toggle_split_orientation")
        .expect("split orientation shortcut must be registered");

    assert_eq!(binding.key_combo.key.to_lowercase(), "s");
    assert!(binding.key_combo.modifiers.contains(&KeyModifier::Alt));
    assert!(binding.key_combo.modifiers.contains(&KeyModifier::Shift));

    if cfg!(target_os = "macos") {
        assert!(binding.key_combo.modifiers.contains(&KeyModifier::Meta));
        assert!(!binding.key_combo.modifiers.contains(&KeyModifier::Ctrl));
    } else {
        assert!(binding.key_combo.modifiers.contains(&KeyModifier::Ctrl));
        assert!(!binding.key_combo.modifiers.contains(&KeyModifier::Meta));
    }

    let normalized = binding.key_combo.to_normalized_string();
    let conflicts = manager
        .get_all_bindings()
        .iter()
        .filter(|candidate| {
            candidate.enabled && candidate.key_combo.to_normalized_string() == normalized
        })
        .count();
    assert_eq!(conflicts, 1, "default split orientation shortcut conflicts");
}
