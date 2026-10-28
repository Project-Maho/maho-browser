//! `export` tests for the GAP-5 data export surface. Select with
//! `cargo test -p maho-core --lib export`.

use maho_types::vault::{CredentialOrigin, VaultItemKind, VaultItemPublicMetadata};
use zeroize::Zeroizing;

use crate::import_export_manager::ImportExportManager;
use crate::maho_core::MahoCore;
use crate::vault_manager::VaultLoginInput;

use super::{DataExportKind, EXPORT_ERROR_VAULT_LOCKED};

const MASTER: &[u8] = b"correct horse battery staple";
const RECOVERY: &[u8] = b"recovery-emergency-phrase-deterministic";

fn storage_core(dir: &tempfile::TempDir) -> MahoCore {
    crate::install_test_sqlcipher_key();
    let path = dir.path().join("export.sqlite");
    MahoCore::new().with_storage(path.to_str().expect("utf8"))
}

fn login(origin: &str, title: &str, username: &str, password: &str) -> VaultLoginInput {
    VaultLoginInput {
        metadata: VaultItemPublicMetadata {
            favorite: false,
            trashed_at: None,
            has_notes: false,
            title: title.to_string(),
            origins: vec![CredentialOrigin::try_from(origin).expect("origin")],
            username_hint: String::new(),
            item_kind: VaultItemKind::Login,
            totp: None,
            passkey: None,
        },
        username: username.to_string(),
        password: Zeroizing::new(password.to_string()),
        form_details: None,
    }
}

#[test]
fn export_bookmarks_html_round_trips_folder_tree_through_netscape_importer() {
    let mut core = MahoCore::new();
    let work = core.create_bookmark_folder("Work & Co".to_string(), None);
    let docs = core.create_bookmark_folder("Docs".to_string(), Some(work.id.clone()));
    core.add_imported_bookmark("https://example.com/?a=1&b=<2>", "Root \"quoted\"", None);
    core.add_imported_bookmark("https://work.example/", "Work <home>", Some(&work.id));
    core.add_imported_bookmark("https://docs.example/", "Docs", Some(&docs.id));

    let html = core.export_bookmarks_html();
    assert!(html.contains("&amp;b=&lt;2&gt;"), "url must be escaped: {html}");
    assert!(html.contains("Work &lt;home&gt;"));
    assert!(html.contains("<H3>Work &amp; Co</H3>"));
    assert_eq!(html.matches("<H3>Work &amp; Co</H3>").count(), 1);

    let mut parsed = ImportExportManager::parse_netscape_bookmarks(&html);
    parsed.sort_by(|a, b| a.url.cmp(&b.url));
    assert_eq!(parsed.len(), 3, "{parsed:?}");
    let by_url = |url: &str| parsed.iter().find(|b| b.url == url).expect(url).clone();
    let root = by_url("https://example.com/?a=1&b=<2>");
    assert_eq!(root.title, "Root \"quoted\"");
    assert!(root.folder_path.is_empty());
    let work_bm = by_url("https://work.example/");
    assert_eq!(work_bm.title, "Work <home>");
    assert_eq!(work_bm.folder_path, vec!["Work & Co".to_string()]);
    let docs_bm = by_url("https://docs.example/");
    assert_eq!(
        docs_bm.folder_path,
        vec!["Work & Co".to_string(), "Docs".to_string()]
    );
}

#[test]
fn export_bookmarks_groups_items_sharing_a_folder_under_one_heading() {
    let manager = ImportExportManager::new();
    let item = |title: &str, folder: Option<&str>| crate::import_export_manager::ImportItem {
        title: title.to_string(),
        url: format!("https://{title}.example/"),
        folder: folder.map(String::from),
        date_added: None,
    };
    let html = manager.export_bookmarks(&[
        item("a", Some("F")),
        item("b", None),
        item("c", Some("F")),
    ]);
    assert_eq!(html.matches("<H3>F</H3>").count(), 1, "{html}");
    let parsed = ImportExportManager::parse_netscape_bookmarks(&html);
    let in_f: Vec<_> = parsed
        .iter()
        .filter(|b| b.folder_path == vec!["F".to_string()])
        .map(|b| b.title.as_str())
        .collect();
    assert_eq!(in_f, vec!["a", "c"]);
}

#[test]
fn export_history_csv_reads_storage_and_quotes_fields() {
    let dir = tempfile::tempdir().unwrap();
    let core = storage_core(&dir);
    let storage = core.storage_ref().unwrap();
    storage
        .add_history_entry("https://a.example/", "Title, with \"comma\"")
        .unwrap();
    storage.add_history_entry("https://b.example/", "B").unwrap();
    let csv = core.export_history_csv().expect("csv");
    let mut lines = csv.lines();
    assert_eq!(lines.next(), Some("URL,Title,Visit Count,Last Visited"));
    assert!(csv.contains("\"https://a.example/\",\"Title, with \"\"comma\"\"\",\"1\""));
    assert!(csv.contains("\"https://b.example/\",\"B\""));
    assert_eq!(csv.lines().count(), 3);
}

#[test]
fn export_history_csv_without_storage_is_an_error() {
    assert!(MahoCore::new().export_history_csv().is_err());
}

#[test]
fn export_passwords_csv_uses_chrome_columns_when_unlocked() {
    let dir = tempfile::tempdir().unwrap();
    let mut core = storage_core(&dir);
    core.initialize_vault(MASTER, RECOVERY).expect("init vault");
    core.vault_add_login(login("https://site.example", "Site", "alice", "p,a\"ss"))
        .expect("add login");
    let csv = core.export_passwords_csv().expect("csv");
    let mut lines = csv.lines();
    assert_eq!(lines.next(), Some("name,url,username,password,note"));
    let row = lines.next().expect("row");
    assert!(row.starts_with("\"Site\",\"https://site.example"), "{row}");
    assert!(row.contains("\"alice\",\"p,a\"\"ss\",\"\""), "{row}");
}

#[test]
fn export_passwords_csv_with_locked_vault_returns_vault_locked_and_writes_no_file() {
    let dir = tempfile::tempdir().unwrap();
    let mut core = storage_core(&dir);
    core.initialize_vault(MASTER, RECOVERY).expect("init vault");
    core.vault_add_login(login("https://site.example", "Site", "alice", "secret"))
        .expect("add login");
    core.lock_vault().expect("lock");

    assert_eq!(
        core.export_passwords_csv().unwrap_err(),
        EXPORT_ERROR_VAULT_LOCKED
    );
    let target = dir.path().join("passwords.csv");
    assert_eq!(
        core.write_data_export(DataExportKind::Passwords, &target)
            .unwrap_err(),
        EXPORT_ERROR_VAULT_LOCKED
    );
    assert!(!target.exists(), "no file may be written for a locked vault");
}

#[test]
fn export_password_passphrase_gate_verifies_without_changing_lock_state() {
    let dir = tempfile::tempdir().unwrap();
    let mut core = storage_core(&dir);
    core.initialize_vault(MASTER, RECOVERY).expect("init vault");
    assert_eq!(
        core.authorize_password_export_with_passphrase(None),
        Err(super::EXPORT_ERROR_PASSPHRASE_REQUIRED)
    );
    assert_eq!(
        core.authorize_password_export_with_passphrase(Some(b"wrong")),
        Err(super::EXPORT_ERROR_INVALID_PASSPHRASE)
    );
    assert_eq!(
        core.authorize_password_export_with_passphrase(Some(MASTER)),
        Ok(())
    );
    assert!(core.export_passwords_csv().is_ok(), "vault must stay unlocked");
    core.lock_vault().expect("lock");
    assert_eq!(
        core.authorize_password_export_with_passphrase(Some(MASTER)),
        Err(EXPORT_ERROR_VAULT_LOCKED)
    );
}

#[test]
fn export_conversations_json_includes_every_turn() {
    let dir = tempfile::tempdir().unwrap();
    let mut core = storage_core(&dir);
    assert!(core.create_conversation_persisted("c1", Some("Thread"), None, None));
    for i in 0..105 {
        assert!(core.save_conversation_message_persisted("c1", "user", &format!("m{i}"), None));
    }
    let json: serde_json::Value =
        serde_json::from_str(&core.export_conversations_json().expect("json")).unwrap();
    let conversations = json["conversations"].as_array().unwrap();
    assert_eq!(conversations.len(), 1);
    assert_eq!(conversations[0]["conversation"]["id"], "c1");
    let messages = conversations[0]["messages"].as_array().unwrap();
    assert_eq!(messages.len(), 105);
    assert_eq!(messages[0]["content"], "m0");
}

#[test]
fn export_spaces_json_lists_spaces() {
    let mut core = MahoCore::new();
    let profile = core.get_active_profile_id().cloned().unwrap_or_default();
    core.create_space(
        "Research",
        maho_types::space::SpaceColor {
            hue: 210.0,
            saturation: 0.6,
            brightness: 0.8,
            grain: 0.0,
        },
        profile,
    );
    let json: serde_json::Value = serde_json::from_str(&core.export_spaces_json()).unwrap();
    let names: Vec<&str> = json["spaces"]
        .as_array()
        .unwrap()
        .iter()
        .filter_map(|s| s["name"].as_str())
        .collect();
    assert!(names.contains(&"Research"), "{json}");
    assert!(json["favorites"].is_array());
}

#[test]
fn export_full_backup_json_round_trips_bookmarks_through_import_from_json() {
    let mut core = MahoCore::new();
    let folder = core.create_bookmark_folder("Reading".to_string(), None);
    core.add_imported_bookmark("https://a.example/", "A", Some(&folder.id));
    core.add_imported_bookmark("https://b.example/", "B", None);
    let backup = core.export_full_backup_json();
    let value: serde_json::Value = serde_json::from_str(&backup).unwrap();
    assert_eq!(value["version"], 1);
    assert!(value["settings"].is_object());
    assert!(value["spaces"]["spaces"].is_array());
    let mut importer = ImportExportManager::new();
    let result = importer.import_from_json(&backup).expect("import");
    assert_eq!(result.items_imported, 2);
    let reading = value["bookmarks"]
        .as_array()
        .unwrap()
        .iter()
        .find(|b| b["url"] == "https://a.example/")
        .unwrap();
    assert_eq!(reading["folder"], "Reading");
}

#[test]
fn export_kind_parse_rejects_unknown_kinds() {
    assert_eq!(DataExportKind::parse("bookmarks"), Some(DataExportKind::Bookmarks));
    assert_eq!(DataExportKind::parse("full_backup"), Some(DataExportKind::FullBackup));
    assert_eq!(DataExportKind::parse("cookies"), None);
}
