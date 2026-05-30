use maho_core::maho_core::MahoCore;
use maho_core::extension_bridge::InstalledExtension;
use maho_types::events::core_update::CoreUpdate;
use std::rc::Rc;
use std::cell::RefCell;

#[test]
fn set_installed_extensions_replaces_map() {
    let mut core = MahoCore::new();
    assert!(core.get_installed_extensions().is_empty());

    let extensions = vec![
        InstalledExtension {
            id: "ext1".to_string(),
            name: "Extension One".to_string(),
            version: "1.0.0".to_string(),
            description: String::new(),
            enabled: true,
        },
        InstalledExtension {
            id: "ext2".to_string(),
            name: "Extension Two".to_string(),
            version: "2.1.3".to_string(),
            description: String::new(),
            enabled: false,
        },
    ];

    core.set_installed_extensions(extensions.clone());
    let installed = core.get_installed_extensions();
    assert_eq!(installed.len(), 2);

    let ext1 = installed.iter().find(|e| e.id == "ext1").unwrap();
    assert_eq!(ext1.name, "Extension One");
    assert!(ext1.enabled);

    let ext2 = installed.iter().find(|e| e.id == "ext2").unwrap();
    assert_eq!(ext2.name, "Extension Two");
    assert!(!ext2.enabled);
}

#[test]
fn clear_installed_extensions_empties_map() {
    let mut core = MahoCore::new();
    let extensions = vec![
        InstalledExtension {
            id: "ext1".to_string(),
            name: "Extension One".to_string(),
            version: "1.0.0".to_string(),
            description: String::new(),
            enabled: true,
        },
    ];

    core.set_installed_extensions(extensions);
    assert_eq!(core.get_installed_extensions().len(), 1);

    core.clear_installed_extensions();
    assert!(core.get_installed_extensions().is_empty());
}

#[test]
fn set_then_toggle_keeps_id_and_flips_enabled() {
    let mut core = MahoCore::new();
    let extensions = vec![
        InstalledExtension {
            id: "ext1".to_string(),
            name: "Extension One".to_string(),
            version: "1.0.0".to_string(),
            description: String::new(),
            enabled: true,
        },
    ];

    core.set_installed_extensions(extensions);
    let enabled = core.toggle_extension("ext1");
    assert_eq!(enabled, Some(false));

    let installed = core.get_installed_extensions();
    let ext1 = installed.iter().find(|e| e.id == "ext1").unwrap();
    assert!(!ext1.enabled);
}

#[test]
fn register_extension_emits_update() {
    let mut core = MahoCore::new();
    let updates = Rc::new(RefCell::new(Vec::new()));

    let updates_clone = updates.clone();
    core.on_update(Box::new(move |update| {
        updates_clone.borrow_mut().push(update);
    }));

    core.register_extension(InstalledExtension {
        id: "ext1".to_string(),
        name: "Extension One".to_string(),
        version: "1.0.0".to_string(),
        description: String::new(),
        enabled: true,
    });

    let received = updates.borrow();
    assert_eq!(received.len(), 1);
    assert!(matches!(received[0], CoreUpdate::InstalledExtensionsUpdated));
}
