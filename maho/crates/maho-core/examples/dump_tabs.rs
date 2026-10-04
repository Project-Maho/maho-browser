use maho_core::maho_core::MahoCore;
use std::path::PathBuf;

fn main() {
    let path: PathBuf = std::env::args()
        .nth(1)
        .expect("usage: dump_tabs <lmdb_dir>")
        .into();
    let mut core = MahoCore::new().with_lmdb_storage(&path);
    let _ = core.load_state();

    let json = core.get_tab_view_models();
    println!("Tab count: {}", json.len());
    for tab in &json {
        let pinned = if tab.is_pinned { "PIN" } else { "   " };
        let fav = if tab.is_favorite { "FAV" } else { "   " };
        let state = tab.lifecycle_state.as_str();
        println!(
            "  [{}][{}][{:9}] {} - {}",
            pinned, fav, state, tab.url, tab.title
        );
    }
}
