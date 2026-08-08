//! `seed` subcommand: writes deterministic normal positive rows into a
//! profile's Maho SQLite and LMDB stores through public APIs only.

use maho_storage::lmdb::LmdbStorage;
use maho_storage::sqlite::{AutofillAddressParams, SqliteStorage};

use super::Args;

const CREATED_AT: &str = "2024-01-01T00:00:00Z";
const NORMAL_TAB_ID: &str = "tab-normal-v1";

fn e(context: &'static str) -> impl Fn(maho_storage::StorageError) -> String {
    move |err| format!("{context}: {err}")
}

fn sqlite_path(profile_root: &str) -> std::path::PathBuf {
    std::path::Path::new(profile_root)
        .join("MahoCore")
        .join("maho.db")
}

fn lmdb_dir(profile_root: &str) -> std::path::PathBuf {
    std::path::Path::new(profile_root)
        .join("MahoCore")
        .join("maho-core.lmdb")
}

fn seed_sqlite(sqlite: &SqliteStorage) -> Result<u64, String> {
    sqlite
        .add_history_entry("https://normal.test/a", "Normal Alpha")
        .map_err(e("history"))?;
    sqlite
        .save_bookmark_folder("folder-normal-v1", "Normal Folder", None, CREATED_AT)
        .map_err(e("bookmark_folder"))?;
    sqlite
        .save_bookmark(
            "bm-normal-v1",
            "https://normal.test/b",
            "Normal Bookmark",
            Some("folder-normal-v1"),
            CREATED_AT,
        )
        .map_err(e("bookmark"))?;
    sqlite
        .set_setting("maho.normal.seed", "normal-setting-value")
        .map_err(e("setting"))?;
    sqlite
        .save_note(
            "note-normal-v1",
            None,
            Some("https://normal.test/a"),
            "Normal note body",
            CREATED_AT,
            CREATED_AT,
        )
        .map_err(e("note"))?;
    sqlite
        .save_download(
            "dl-normal-v1",
            "regular.bin",
            "https://normal.test/regular.bin",
            24,
            24,
            "complete",
            Some("/tmp/regular.bin"),
            CREATED_AT,
            None,
            None,
            None,
        )
        .map_err(e("download"))?;
    sqlite
        .save_search_engine(
            "se-normal-v1",
            "Normal Search",
            "https://normal.test/s?q={searchTerms}",
            Some("n"),
            None,
            true,
        )
        .map_err(e("search_engine"))?;
    sqlite
        .update_default_search_engine("se-normal-v1")
        .map_err(e("default_search_engine"))?;
    sqlite
        .increment_usage("bm-normal-v1", "bookmark")
        .map_err(e("usage"))?;
    sqlite
        .save_atc_rule(
            "atc-normal-v1",
            "Normal Rule",
            None,
            Some("https://normal.test/*"),
            Some(24),
            Some(20),
            true,
            None,
        )
        .map_err(e("atc"))?;
    sqlite
        .save_permission("https://normal.test", "geolocation", "allow")
        .map_err(e("permission"))?;
    sqlite
        .save_zoom("https://normal.test", 1.1)
        .map_err(e("zoom"))?;
    sqlite.save_search("normal query").map_err(e("search"))?;
    sqlite
        .save_password(
            "pw-normal-v1",
            "normal.test",
            "normal-user",
            CREATED_AT,
            Some("normal-secret"),
        )
        .map_err(e("password"))?;
    sqlite
        .save_autofill_address(AutofillAddressParams {
            id: "addr-normal-v1",
            name: "Normal Person",
            street: "1 Normal St",
            city: "Normalville",
            state: "NA",
            zip: "00000",
            country: "US",
            phone: None,
            email: None,
            address_line2: None,
        })
        .map_err(e("autofill_address"))?;
    sqlite
        .save_profile("profile-normal-v1", "{\"name\":\"Normal\"}")
        .map_err(e("profile"))?;
    sqlite
        .create_conversation("conv-normal-v1", Some("Normal Chat"), None, None)
        .map_err(e("conversation"))?;
    sqlite.set_last_hlc_ts(1).map_err(e("hlc"))?;

    Ok(19)
}

fn seed_lmdb(lmdb: &LmdbStorage) -> Result<u64, String> {
    let entries: &[(&str, &[u8])] = &[
        ("tabs", br#"[{"id":"tab-normal-v1","scroll_position":0}]"#),
        ("spaces", br#"[{"id":"space-normal-v1","name":"Normal"}]"#),
        ("active_space_id", br#""space-normal-v1""#),
        ("profiles", br#"[{"id":"profile-normal-v1"}]"#),
        ("active_profile_id", br#""profile-normal-v1""#),
        ("account", br#"{"status":"signed_out"}"#),
        ("schema_version", b"4"),
        ("split_view_configs", b"{}"),
    ];
    for (key, value) in entries {
        lmdb.put(key, value).map_err(e(key))?;
    }
    let preview_key = format!("preview:{NORMAL_TAB_ID}");
    lmdb.put(&preview_key, b"NORMAL_PREVIEW_BYTES")
        .map_err(e("preview"))?;
    Ok(entries.len() as u64 + 1)
}

pub fn run(args: &Args) -> Result<(), String> {
    let profile_root = args.require("profile-root")?;
    let receipt = args.require("receipt")?;

    let sqlite_file = sqlite_path(profile_root);
    if let Some(parent) = sqlite_file.parent() {
        std::fs::create_dir_all(parent).map_err(|e| format!("create MahoCore dir: {e}"))?;
    }
    let sqlite = SqliteStorage::open(sqlite_file.to_str().ok_or("non-utf8 sqlite path")?)
        .map_err(|e| format!("open sqlite: {e}"))?;
    let sqlite_rows = seed_sqlite(&sqlite)?;

    let lmdb = LmdbStorage::open(&lmdb_dir(profile_root)).map_err(|e| format!("open lmdb: {e}"))?;
    let lmdb_keys = seed_lmdb(&lmdb)?;

    super::write_receipt(
        receipt,
        &serde_json::json!({
            "status": "seeded",
            "profile_root": profile_root,
            "sqlite_rows": sqlite_rows,
            "lmdb_keys": lmdb_keys,
            "default_search_engines": 1,
        }),
    )?;
    Ok(())
}
