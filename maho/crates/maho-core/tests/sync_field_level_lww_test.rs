//! Regression tests locking the field-level Last-Writer-Wins (LWW) contract of
//! the Maho sync layer (see plan §8 A2.3 / OI-3).
//!
//! The field-level merge engine lives in `sync_manager::apply_remote_entity`
//! (roughly lines 205-283). These tests prove the "correct by construction"
//! guarantee: a flat / partial write from one device (e.g. a mobile client that
//! only knows about a subset of fields) can NOT silently clobber fields authored
//! by another device (e.g. a desktop client that populated `folders`,
//! `root_order`, or a tab `role`).
//!
//! Public entry points exercised (identical to the existing sync harness in
//! `sync_integration_test.rs` / `sync_all_entities_test.rs`):
//!   - `MahoCore::new().with_storage(":memory:")`
//!   - `MahoCore::start_sync` / `MahoCore::set_sync_status`
//!   - `MahoCore::next_hlc_ts`
//!   - `MahoCore::apply_sync_remote_entities` (returns the *merged* entities)
//!   - `sync_models::build_fields_hlc` (builds per-field HLC maps so the
//!     field-level merge path is actually taken)

use maho_core::maho_core::MahoCore;
use maho_core::sync_models::{build_fields_hlc, SyncEntity, SyncEntityType, SyncStatus};
use maho_types::space::Space;
use maho_types::tab::{Tab, TabRole};

/// Build a synced, in-memory core using the same harness the other sync tests use.
fn synced_core() -> MahoCore {
    let mut core = MahoCore::new().with_storage(":memory:");
    core.start_sync("wss://localhost:8080", "room-field-lww");
    core.set_sync_status(SyncStatus::Synced);
    core
}

/// Build a `SyncEntity` whose `fields_hlc_json` is derived from its own payload
/// via the production `build_fields_hlc` helper, so every top-level field in the
/// payload carries the given `(version, device_id)` field-level HLC. This is the
/// exact mechanism the real client uses, so the per-field merge branch runs.
fn entity_with_field_hlc(
    entity_type: SyncEntityType,
    entity_id: &str,
    version: u64,
    device_id: u32,
    payload_json: String,
) -> SyncEntity {
    let fields_hlc_json = build_fields_hlc(&payload_json, version, device_id);
    assert!(
        fields_hlc_json.is_some(),
        "build_fields_hlc must produce a field map for object payloads (payload={payload_json})"
    );
    SyncEntity {
        entity_type,
        entity_id: entity_id.to_string(),
        version,
        device_id,
        schema_version: 1,
        modified_at: 1_700_000_000,
        payload_json,
        deleted: false,
        queue_row_id: None,
        fields_hlc_json,
        profile_id: None,
    }
}

/// Apply a single entity and return the merged entity the engine produced.
/// Fails loudly (readable panic) if the write was rejected when it should apply.
fn apply_one(core: &mut MahoCore, entity: SyncEntity) -> Option<SyncEntity> {
    let applied = core.apply_sync_remote_entities(vec![entity]);
    assert!(
        applied.len() <= 1,
        "single-entity apply must yield at most one merged entity, got {}",
        applied.len()
    );
    applied.into_iter().next()
}

/// A full desktop-authored Space payload with populated `folders` and
/// `root_order`. Keys are limited to the canonical serialized set (no `theme`
/// / `lastActiveTabId`, which are skipped when None) so the derived field HLC
/// map lines up with what the engine stores after its round-trip.
fn desktop_space_payload(name: &str) -> String {
    serde_json::json!({
        "id": "space-1",
        "profileId": "default",
        "name": name,
        "color": {"hue": 0.5, "saturation": 0.8, "brightness": 0.9, "grain": 0.1},
        "icon": null,
        "tabOrder": ["tab-a", "tab-b"],
        "folders": [
            {
                "id": "folder-1",
                "name": "Research",
                "tabIds": ["tab-b"],
                "isExpanded": true
            }
        ],
        "rootOrder": [
            {"kind": "tab", "id": "tab-a"},
            {"kind": "folder", "id": "folder-1"}
        ],
        "atcRules": [],
        "isActive": true,
        "createdAt": "2026-07-09T00:00:00Z"
    })
    .to_string()
}

/// A minimal, valid Tab payload in the sync wire format, with an explicit role.
fn tab_payload(id: &str, title: &str, role: serde_json::Value) -> String {
    serde_json::json!({
        "id": id,
        "profileId": "default",
        "parentId": null,
        "spaceId": "space-1",
        "url": "https://example.com",
        "title": title,
        "favicon": null,
        "state": {"kind": "active"},
        "role": role,
        "isMuted": false,
        "zoomLevel": 1.0,
        "createdAt": "2026-07-09T00:00:00Z",
        "lastActiveAt": "2026-07-09T00:00:00Z",
        "scrollPosition": {"x": 0.0, "y": 0.0}
    })
    .to_string()
}

fn parse_space(entity: &SyncEntity) -> Space {
    serde_json::from_str::<Space>(&entity.payload_json).unwrap_or_else(|e| {
        panic!(
            "merged Space payload must deserialize; err={e}; payload={}",
            entity.payload_json
        )
    })
}

fn parse_tab(entity: &SyncEntity) -> Tab {
    serde_json::from_str::<Tab>(&entity.payload_json).unwrap_or_else(|e| {
        panic!(
            "merged Tab payload must deserialize; err={e}; payload={}",
            entity.payload_json
        )
    })
}

/// (T1) A desktop write authors a Space with populated `folders` and
/// `root_order` (device A). A mobile client then issues a *partial* write that
/// only carries a single scalar field (`name`) — with its own, higher field-level
/// HLC (device B). Under naive whole-entity LWW the higher-HLC mobile write would
/// replace the entire Space and drop `folders` / `root_order`. Field-level LWW
/// must instead update only `name` and keep the desktop-authored structure.
#[test]
fn mobile_partial_write_preserves_folders_and_root_order() {
    let mut core = synced_core();
    let base = core.next_hlc_ts().expect("next_hlc_ts");

    // Device A (desktop): full Space with folders + root_order.
    let hlc_desktop = base;
    let device_a: u32 = 100;
    let desktop = entity_with_field_hlc(
        SyncEntityType::Space,
        "space-1",
        hlc_desktop,
        device_a,
        desktop_space_payload("Work"),
    );
    let applied = apply_one(&mut core, desktop).expect("first desktop write must apply");
    let seeded = parse_space(&applied);
    assert_eq!(
        seeded.folders.len(),
        1,
        "precondition: desktop seeded 1 folder"
    );
    assert_eq!(
        seeded.root_order.len(),
        2,
        "precondition: desktop seeded 2 root items"
    );

    // Device B (mobile): partial write carrying ONLY `name`, with a higher
    // field-level HLC so the field genuinely wins (would clobber under naive LWW).
    let hlc_mobile = base + 100;
    let device_b: u32 = 200;
    let mobile_payload = serde_json::json!({"name": "Work (mobile)"}).to_string();
    let mobile = entity_with_field_hlc(
        SyncEntityType::Space,
        "space-1",
        hlc_mobile,
        device_b,
        mobile_payload,
    );
    let merged_entity =
        apply_one(&mut core, mobile).expect("mobile partial write must apply (name field wins)");
    let merged = parse_space(&merged_entity);

    // The scalar field the mobile client actually edited must win...
    assert_eq!(
        merged.name, "Work (mobile)",
        "mobile's higher-HLC name edit should win"
    );
    // ...but the desktop-authored structure MUST survive intact.
    assert_eq!(
        merged.folders.len(),
        1,
        "partial mobile write must NOT clobber desktop folders"
    );
    assert_eq!(
        merged.folders[0].id.0, "folder-1",
        "the preserved folder must be the desktop-authored one"
    );
    assert_eq!(
        merged.root_order.len(),
        2,
        "partial mobile write must NOT clobber desktop root_order"
    );
    assert_eq!(
        merged.tab_order.len(),
        2,
        "partial mobile write must NOT clobber desktop tab_order"
    );
}

/// (T2) Two devices concurrently write DIFFERENT fields of the same Space:
/// device A updates `folders`, device B updates `name`, each with its own
/// field-level HLC. Applying both — in EITHER order — must converge to a state
/// where BOTH edits survive (no cross-field clobber).
#[test]
fn concurrent_field_writes_converge() {
    let device_base: u32 = 1;
    let device_a: u32 = 100;
    let device_b: u32 = 200;

    // Helper that seeds a base Space then applies A and B in a caller-chosen
    // order, returning the final merged Space.
    fn run_in_order(apply_a_first: bool, dev_base: u32, dev_a: u32, dev_b: u32) -> Space {
        let mut core = synced_core();
        let base = core.next_hlc_ts().expect("next_hlc_ts");

        // Seed: base Space with an empty folder list and a placeholder name.
        let seed_payload = serde_json::json!({
            "id": "space-2",
            "profileId": "default",
            "name": "Base",
            "color": {"hue": 0.5, "saturation": 0.8, "brightness": 0.9, "grain": 0.1},
            "icon": null,
            "tabOrder": [],
            "folders": [],
            "rootOrder": [],
            "atcRules": [],
            "isActive": true,
            "createdAt": "2026-07-09T00:00:00Z"
        })
        .to_string();
        let seed = entity_with_field_hlc(
            SyncEntityType::Space,
            "space-2",
            base,
            dev_base,
            seed_payload,
        );
        apply_one(&mut core, seed).expect("base seed must apply");

        // Device A: updates ONLY `folders`.
        let a_payload = serde_json::json!({
            "folders": [
                {"id": "folder-x", "name": "Deep Work", "tabIds": [], "isExpanded": true}
            ]
        })
        .to_string();
        let write_a = entity_with_field_hlc(
            SyncEntityType::Space,
            "space-2",
            base + 100,
            dev_a,
            a_payload,
        );

        // Device B: updates ONLY `name`.
        let b_payload = serde_json::json!({"name": "Renamed by B"}).to_string();
        let write_b = entity_with_field_hlc(
            SyncEntityType::Space,
            "space-2",
            base + 200,
            dev_b,
            b_payload,
        );

        let final_entity = if apply_a_first {
            apply_one(&mut core, write_a).expect("device A folders write must apply");
            apply_one(&mut core, write_b).expect("device B name write must apply")
        } else {
            apply_one(&mut core, write_b).expect("device B name write must apply");
            apply_one(&mut core, write_a).expect("device A folders write must apply")
        };
        parse_space(&final_entity)
    }

    let a_then_b = run_in_order(true, device_base, device_a, device_b);
    let b_then_a = run_in_order(false, device_base, device_a, device_b);

    for (label, space) in [("A→B", &a_then_b), ("B→A", &b_then_a)] {
        assert_eq!(
            space.name, "Renamed by B",
            "[{label}] device B's name edit must survive"
        );
        assert_eq!(
            space.folders.len(),
            1,
            "[{label}] device A's folders edit must survive"
        );
        assert_eq!(
            space.folders[0].id.0, "folder-x",
            "[{label}] device A's specific folder must survive"
        );
    }
}

/// (T3) A Tab's `role` (Pinned / Favorite) must NOT be reset to Normal by a
/// partial write that does not carry the `role` field. This locks the tab-level
/// equivalent of the field-level guarantee.
#[test]
fn tab_role_preserved_under_partial_space_write() {
    let mut core = synced_core();
    let base = core.next_hlc_ts().expect("next_hlc_ts");
    let device_desktop: u32 = 100;
    let device_mobile: u32 = 200;

    // --- Pinned tab ---
    let pinned_seed = entity_with_field_hlc(
        SyncEntityType::Tab,
        "tab-pinned",
        base,
        device_desktop,
        tab_payload("tab-pinned", "Docs", serde_json::json!({"type": "pinned"})),
    );
    let seeded = parse_tab(&apply_one(&mut core, pinned_seed).expect("pinned seed must apply"));
    assert_eq!(
        seeded.role,
        TabRole::Pinned,
        "precondition: seeded tab is Pinned"
    );

    // Mobile partial write: only updates `title`, higher HLC, NO role field.
    let pinned_partial = entity_with_field_hlc(
        SyncEntityType::Tab,
        "tab-pinned",
        base + 100,
        device_mobile,
        serde_json::json!({"profileId": "default", "title": "Docs (mobile)"}).to_string(),
    );
    let merged =
        parse_tab(&apply_one(&mut core, pinned_partial).expect("partial title write must apply"));
    assert_eq!(
        merged.title, "Docs (mobile)",
        "mobile's title edit should win"
    );
    assert_eq!(
        merged.role,
        TabRole::Pinned,
        "partial write without a role field must NOT reset Pinned -> Normal"
    );

    // --- Favorite tab (with an order) ---
    let fav_seed = entity_with_field_hlc(
        SyncEntityType::Tab,
        "tab-fav",
        base,
        device_desktop,
        tab_payload(
            "tab-fav",
            "Mail",
            serde_json::json!({"type": "favorite", "order": 3}),
        ),
    );
    let seeded_fav = parse_tab(&apply_one(&mut core, fav_seed).expect("favorite seed must apply"));
    assert_eq!(
        seeded_fav.role,
        TabRole::Favorite { order: 3 },
        "precondition: seeded tab is Favorite{{order:3}}"
    );

    let fav_partial = entity_with_field_hlc(
        SyncEntityType::Tab,
        "tab-fav",
        base + 100,
        device_mobile,
        serde_json::json!({"title": "Mail (mobile)"}).to_string(),
    );
    let merged_fav =
        parse_tab(&apply_one(&mut core, fav_partial).expect("partial title write must apply"));
    assert_eq!(
        merged_fav.title, "Mail (mobile)",
        "mobile's title edit should win"
    );
    assert_eq!(
        merged_fav.role,
        TabRole::Favorite { order: 3 },
        "partial write without a role field must NOT reset Favorite -> Normal"
    );
}
