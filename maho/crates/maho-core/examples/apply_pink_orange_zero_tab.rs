// One-shot: apply the pink-orange gradient theme (opacity 0.8) to a zero-tab
// space in the DEFAULT profile and make that space active, so a relaunch lands
// on a zero-tab space whose empty content area shows the pink-orange gradient.
//
// MUST run only while the Maho browser using this profile is CLOSED (LMDB is
// single-writer). Usage:
//   cargo run --example apply_pink_orange_zero_tab -p maho-core [<MahoCore dir>]
// Default dir: ~/Library/Application Support/Maho/MahoCore

use maho_core::maho_core::MahoCore;
use maho_types::space::{ColorHarmony, SpaceConfigUpdate, SpaceTheme, ThemeColor};
use std::path::PathBuf;

fn pink_orange() -> SpaceTheme {
    SpaceTheme::Gradient {
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
    }
}

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

    let active_before = core.get_active_space_id();
    println!("Active space (before): {active_before:?}");

    // Snapshot space metadata (clone to drop the space_manager borrow).
    let space_meta: Vec<_> = core
        .space_manager()
        .get_all_spaces()
        .iter()
        .map(|s| (s.id.clone(), s.name.clone(), s.theme.is_some()))
        .collect();

    println!("Spaces:");
    let mut zero_tab_target: Option<maho_types::identifiers::SpaceId> = None;
    for (id, name, has_theme) in &space_meta {
        let tab_count = core.get_space_tabs(id).len();
        println!("  - {name:?} id={id:?} tabs={tab_count} has_theme={has_theme}");
        if tab_count == 0 && zero_tab_target.is_none() {
            zero_tab_target = Some(id.clone());
        }
    }

    let theme = pink_orange();
    assert!(theme.validate(), "theme failed validate()");

    let target = match zero_tab_target.clone() {
        Some(id) => {
            println!("Chosen zero-tab target space: {id:?}");
            id
        }
        None => {
            println!(
                "WARNING: no zero-tab space found; falling back to active space (content gradient will NOT be visible while tabs exist)."
            );
            active_before.clone()
        }
    };

    core.update_space_config(SpaceConfigUpdate {
        space_id: target.clone(),
        name: None,
        color: None,
        theme: Some(theme),
        icon: None,
        profile_id: None,
    });

    // Make the themed zero-tab space active so a relaunch lands on it.
    core.activate_space(&target);

    core.save_state().expect("save_state failed");
    println!("Active space (after): {:?}", core.get_active_space_id());
    println!("Applied pink-orange gradient (opacity 0.8) to {target:?} and set it active. Saved.");
}
