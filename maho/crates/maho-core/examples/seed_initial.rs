use std::path::PathBuf;

use maho_core::maho_core::MahoCore;
use maho_types::common::Url;
use maho_types::events::core_update::CoreUpdate;
use maho_types::events::shell_event::ShellEvent;
use maho_types::identifiers::{ProfileId, SpaceId, TabId};
use maho_types::space::SpaceColor;

fn dispatch_create_tab(core: &mut MahoCore, space_id: &SpaceId, url: &str) -> Option<TabId> {
    let updates = core.handle_event(ShellEvent::CreateTab {
        space_id: space_id.clone(),
        url: Some(Url::new(url)),
        parent_id: None,
        tab_id: None,
        window_id: None,
        is_private: false,
    });
    for u in updates {
        if let CoreUpdate::TabCreated { tab } = u {
            return Some(tab.id);
        }
    }
    None
}

fn dispatch_create_space(
    core: &mut MahoCore,
    name: &str,
    profile_id: &ProfileId,
) -> Option<SpaceId> {
    let color = SpaceColor {
        hue: 0.6,
        saturation: 0.8,
        brightness: 0.9,
        grain: 0.0,
    };
    let updates = core.handle_event(ShellEvent::CreateSpace {
        name: name.to_string(),
        color,
        profile_id: profile_id.clone(),
    });
    for u in updates {
        if let CoreUpdate::SpaceCreated { space } = u {
            return Some(space.id);
        }
    }
    None
}

fn main() {
    let path: PathBuf = std::env::args()
        .nth(1)
        .expect("usage: seed_initial <lmdb_dir>")
        .into();
    std::fs::create_dir_all(&path).expect("mkdir lmdb_dir");
    println!("Seeding LMDB at: {}", path.display());

    let mut core = MahoCore::new().with_lmdb_storage(&path);
    let _ = core.load_state();

    let profile_id: ProfileId = core
        .get_active_profile_id()
        .cloned()
        .or_else(|| core.list_profiles().first().map(|p| p.id.clone()))
        .unwrap_or_else(|| {
            core.create_profile_persisted("Default".to_string())
                .unwrap()
                .id
        });
    println!("Using profile: {:?}", profile_id);

    let active_space_id = core.get_active_space_id();
    println!("Active space: {:?}", active_space_id);

    let fav_urls = [
        "https://github.com/",
        "https://gmail.com/",
        "https://calendar.google.com/",
    ];
    let mut fav_ids: Vec<TabId> = Vec::new();
    for u in &fav_urls {
        if let Some(id) = dispatch_create_tab(&mut core, &active_space_id, u) {
            fav_ids.push(id);
        }
    }
    for tid in &fav_ids {
        core.handle_event(ShellEvent::FavoriteTab {
            tab_id: tid.clone(),
        });
    }
    println!("Favorites added: {}", fav_ids.len());

    let pinned_urls = [
        "https://www.notion.so/",
        "https://www.figma.com/",
        "https://slack.com/",
    ];
    let mut pinned_ids: Vec<TabId> = Vec::new();
    for u in &pinned_urls {
        if let Some(id) = dispatch_create_tab(&mut core, &active_space_id, u) {
            pinned_ids.push(id);
        }
    }
    for tid in &pinned_ids {
        core.handle_event(ShellEvent::PinTab {
            tab_id: tid.clone(),
        });
    }
    println!("Pinned tabs added: {}", pinned_ids.len());

    let normal_urls = [
        "https://stackoverflow.com/",
        "https://dev.to/",
        "https://reddit.com/",
    ];
    let mut normal_ids: Vec<TabId> = Vec::new();
    for u in &normal_urls {
        if let Some(id) = dispatch_create_tab(&mut core, &active_space_id, u) {
            normal_ids.push(id);
        }
    }
    println!("Normal tabs added: {}", normal_ids.len());

    // Suspend all seeded tabs so they start as suspended/ghost tabs
    for tid in fav_ids
        .iter()
        .chain(pinned_ids.iter())
        .chain(normal_ids.iter())
    {
        core.handle_event(ShellEvent::SuspendTab {
            tab_id: tid.clone(),
        });
    }

    for name in &["Reading", "Tools", "Projects"] {
        core.handle_event(ShellEvent::CreateFolder {
            space_id: active_space_id.clone(),
            name: name.to_string(),
            is_pinned: true,
            parent_folder_id: None,
            provider_type: None,
            config_json: None,
        });
    }
    println!("Pinned folders created: 3");

    for name in &["Work", "Personal", "Research"] {
        core.handle_event(ShellEvent::CreateFolder {
            space_id: active_space_id.clone(),
            name: name.to_string(),
            is_pinned: false,
            parent_folder_id: None,
            provider_type: None,
            config_json: None,
        });
    }
    println!("Normal folders created: 3");

    let s1 = dispatch_create_space(&mut core, "Workspace", &profile_id);
    let s2 = dispatch_create_space(&mut core, "Personal", &profile_id);
    println!("Empty spaces created: {:?} / {:?}", s1, s2);

    core.save_state().expect("save_state failed");
    println!("\nSeed complete. save_state succeeded.");
}
