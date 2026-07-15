use maho_storage::lmdb::LmdbStorage;
use tempfile::tempdir;

fn keys_of(entries: &[(Vec<u8>, Vec<u8>)]) -> Vec<String> {
    entries
        .iter()
        .map(|(k, _)| String::from_utf8(k.clone()).expect("utf-8 key"))
        .collect()
}

#[test]
fn snapshot_entries_is_complete_sorted_and_read_only() {
    let dir = tempdir().expect("temp dir");
    let storage = LmdbStorage::open(dir.path()).expect("open lmdb");

    // Insert out of byte-sorted order to prove the enumerator sorts.
    storage.put("spaces", b"[]").expect("put spaces");
    storage
        .put("active_space_id", b"\"space-1\"")
        .expect("put active");
    storage.put("tabs", b"[]").expect("put tabs");
    storage.put("schema_version", b"4").expect("put schema");

    let entries = storage.snapshot_entries().expect("snapshot");

    // Completeness: every inserted key is present exactly once.
    let keys = keys_of(&entries);
    assert_eq!(entries.len(), 4, "expected exactly the four seeded keys");
    for expected in ["active_space_id", "schema_version", "spaces", "tabs"] {
        assert_eq!(
            keys.iter().filter(|k| k.as_str() == expected).count(),
            1,
            "key {expected} must appear exactly once"
        );
    }

    // Byte-sorted ordering.
    let mut sorted = keys.clone();
    sorted.sort();
    assert_eq!(keys, sorted, "keys must be byte-sorted");
    assert_eq!(
        keys,
        vec!["active_space_id", "schema_version", "spaces", "tabs"]
    );

    // Values match the logical bytes stored.
    for (k, v) in &entries {
        let via_get = storage
            .get(std::str::from_utf8(k).unwrap())
            .expect("get")
            .expect("value present");
        assert_eq!(&via_get, v, "snapshot value must equal get() value");
    }

    // Read-only: repeated snapshots are identical and the store is unchanged.
    let entries_again = storage.snapshot_entries().expect("snapshot again");
    assert_eq!(entries, entries_again, "snapshot must be idempotent");
    assert_eq!(
        storage.get("tabs").expect("get tabs"),
        Some(b"[]".to_vec()),
        "enumeration must not mutate any entry"
    );
    let after = storage.snapshot_entries().expect("snapshot after get");
    assert_eq!(keys_of(&after).len(), 4, "no key was created or removed");
}

#[test]
fn snapshot_tabs_exposes_scroll_inside_tabs_value() {
    let dir = tempdir().expect("temp dir");
    let storage = LmdbStorage::open(dir.path()).expect("open lmdb");

    let tabs_json = serde_json::json!([
        {"id": "tab-a", "scroll_position": 0},
        {"id": "tab-b", "scroll_position": 640}
    ]);
    let tabs_bytes = serde_json::to_vec(&tabs_json).expect("encode tabs");
    storage.put("tabs", &tabs_bytes).expect("put tabs");

    let entries = storage.snapshot_entries().expect("snapshot");

    // There is no standalone scroll key: scroll lives only inside `tabs`.
    let keys = keys_of(&entries);
    assert!(
        !keys.iter().any(|k| k.contains("scroll")),
        "no standalone scroll key may exist: {keys:?}"
    );

    let tabs_entry = entries
        .iter()
        .find(|(k, _)| k == b"tabs")
        .expect("tabs key present");
    let decoded: serde_json::Value =
        serde_json::from_slice(&tabs_entry.1).expect("decode tabs json");
    let arr = decoded.as_array().expect("tabs is an array");
    assert_eq!(arr.len(), 2);

    let scrolls: Vec<i64> = arr
        .iter()
        .map(|t| t["scroll_position"].as_i64().expect("scroll_position i64"))
        .collect();
    assert_eq!(
        scrolls,
        vec![0, 640],
        "per-tab scroll decoded from tabs json"
    );
}
