use maho_core::maho_core::MahoCore;
use maho_types::common::Url;
use maho_types::events::core_update::CoreUpdate;
use maho_types::events::shell_event::ShellEvent;
use maho_types::identifiers::{ProfileId, SpaceId, TabId};
use maho_types::space::SpaceColor;
use std::path::PathBuf;

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
        .expect("usage: seed_look_and_feel <lmdb_dir>")
        .into();
    // Clean up directory if it exists to ensure clean seed
    if path.exists() {
        let _ = std::fs::remove_dir_all(&path);
    }
    std::fs::create_dir_all(&path).expect("mkdir lmdb_dir");
    println!(
        "Seeding LMDB and SQLite for visual QA at: {}",
        path.display()
    );

    let lmdb_path = path.join("state");
    let sqlite_path = path.join("maho.db");
    std::fs::create_dir_all(&lmdb_path).expect("mkdir state");

    let mut core = MahoCore::new()
        .with_storage(sqlite_path.to_str().unwrap())
        .with_lmdb_storage(&lmdb_path);
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

    // Seed 9 favorites
    let fav_urls = [
        "https://github.com/",
        "https://gmail.com/",
        "https://calendar.google.com/",
        "https://youtube.com/",
        "https://google.com/",
        "https://wikipedia.org/",
        "https://reddit.com/",
        "https://twitter.com/",
        "https://amazon.com/",
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

    // Pinned tabs
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

    // Normal tabs (for populated state)
    let normal_urls = [
        "https://stackoverflow.com/",
        "https://dev.to/",
        "https://news.ycombinator.com/",
    ];
    let mut normal_ids: Vec<TabId> = Vec::new();
    for u in &normal_urls {
        if let Some(id) = dispatch_create_tab(&mut core, &active_space_id, u) {
            normal_ids.push(id);
        }
    }
    println!("Normal tabs added: {}", normal_ids.len());

    // Suspend all seeded tabs
    for tid in fav_ids
        .iter()
        .chain(pinned_ids.iter())
        .chain(normal_ids.iter())
    {
        core.handle_event(ShellEvent::SuspendTab {
            tab_id: tid.clone(),
        });
    }

    // Create extra spaces for space switcher parity
    let s1 = dispatch_create_space(&mut core, "Work", &profile_id);
    let s2 = dispatch_create_space(&mut core, "Entertainment", &profile_id);
    println!("Spaces created: {:?} / {:?}", s1, s2);

    core.save_state().expect("save_state failed");
    println!("\nSeed complete. save_state succeeded.");
}
