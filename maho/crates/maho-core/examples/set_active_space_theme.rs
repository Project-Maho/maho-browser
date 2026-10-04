use maho_core::maho_core::MahoCore;
use maho_types::space::{ColorHarmony, SpaceConfigUpdate, SpaceTheme, ThemeColor};
use std::path::PathBuf;

fn main() {
    let base: PathBuf = std::env::args()
        .nth(1)
        .unwrap_or_else(|| {
            let home = std::env::var("HOME").expect("HOME not set");
            format!("{home}/Library/Application Support/Maho/MahoCore")
        })
        .into();

    let lmdb_path = base.join("state");
    let sqlite_path = base.join("maho.db");
    assert!(
        lmdb_path.join("data.mdb").exists(),
        "LMDB data.mdb not found at {}",
        lmdb_path.display()
    );

    let mut core = MahoCore::new()
        .with_storage(sqlite_path.to_str().expect("sqlite path utf8"))
        .with_lmdb_storage(&lmdb_path);
    core.load_state().expect("load_state failed");

    let active_space_id = core.get_active_space_id();
    println!("Active space: {active_space_id:?}");

    let theme = SpaceTheme::Gradient {
        colors: vec![
            ThemeColor {
                hue: 0.92,
                saturation: 0.75,
                brightness: 0.95,
                is_custom: false,
                is_primary: true,
                c_raw: None,
                position: None,
                lightness: None,
                algorithm: None,
            },
            ThemeColor {
                hue: 0.07,
                saturation: 0.85,
                brightness: 1.0,
                is_custom: false,
                is_primary: false,
                c_raw: None,
                position: None,
                lightness: None,
                algorithm: None,
            },
        ],
        harmony: ColorHarmony::Analogous,
        scheme: None,
        opacity: 0.8,
        texture: 0.0,
        schema_version: 0,
    };
    assert!(theme.validate(), "theme failed validate()");
    println!(
        "Theme JSON: {}",
        serde_json::to_string(&theme).expect("serialize theme")
    );

    core.update_space_config(SpaceConfigUpdate {
        space_id: active_space_id.clone(),
        name: None,
        color: None,
        theme: Some(theme),
        icon: None,
        profile_id: None,
    });

    core.save_state().expect("save_state failed");
    println!("Applied pink-orange gradient theme to active space and saved.");
}
