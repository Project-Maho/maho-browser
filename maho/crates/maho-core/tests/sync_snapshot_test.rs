use maho_core::maho_core::MahoCore;
use maho_types::common::Url;
use maho_types::events::shell_event::ShellEvent;
use maho_types::identifiers::ProfileId;
use maho_types::space::SpaceColor;

#[test]
fn test_sync_snapshot_export_and_apply_round_trip() {
    let _ = maho_storage::sqlite::set_sqlcipher_key("test_key");

    let mut core_src = MahoCore::new().with_storage(":memory:");
    let profile_id = ProfileId::new("profile-1");
    let color_blue = SpaceColor {
        hue: 210.0,
        saturation: 0.8,
        brightness: 0.9,
        grain: 0.0,
    };
    let color_purple = SpaceColor {
        hue: 280.0,
        saturation: 0.7,
        brightness: 0.8,
        grain: 0.0,
    };

    let work_space = core_src.create_space("Work", color_blue, profile_id.clone());
    let personal_space = core_src.create_space("Personal", color_purple, profile_id.clone());

    core_src.handle_event(ShellEvent::CreateTab {
        space_id: work_space.id.clone(),
        url: Some(Url::new("https://work.example.com")),
        parent_id: None,
        tab_id: None,
        window_id: None,
        is_private: false,
    });
    core_src.handle_event(ShellEvent::CreateTab {
        space_id: work_space.id.clone(),
        url: Some(Url::new("https://docs.example.com")),
        parent_id: None,
        tab_id: None,
        window_id: None,
        is_private: false,
    });

    core_src.handle_event(ShellEvent::CreateTab {
        space_id: personal_space.id.clone(),
        url: Some(Url::new("https://personal.example.com")),
        parent_id: None,
        tab_id: None,
        window_id: None,
        is_private: false,
    });

    let snapshot_json = {
        // A sync snapshot carries tabs, notes, memories, conversations and
        // autofill. Exporting it without an encryption key would emit all of
        // that as base64 PLAINTEXT, so the API must fail closed exactly like
        // its sibling `build_snapshot` does.
        let err = core_src
            .export_sync_snapshot()
            .expect_err("export must fail closed when no encryption key is set");
        assert!(
            err.contains("Encryption key is not set"),
            "keyless export must name the missing key, got {err:?}"
        );

        // With a key configured the same export succeeds, and the rest of this
        // test exercises the apply + idempotency contract over that output.
        use base64::Engine;
        let seed_b64 = base64::engine::general_purpose::STANDARD.encode([3u8; 32]);
        core_src
            .configure_sync_encryption_from_bootstrap("https://relay.mahobrowser.com", &seed_b64)
            .expect("configure bootstrap should succeed");
        core_src
            .export_sync_snapshot()
            .expect("export_sync_snapshot should succeed once a key is configured")
    };

    let envelope: serde_json::Value =
        serde_json::from_str(&snapshot_json).expect("snapshot output should be valid JSON");
    assert!(
        envelope.get("blob").and_then(|v| v.as_str()).is_some(),
        "envelope must carry a blob"
    );
    // The payload is encrypted, so plaintext space names and URLs must not
    // appear anywhere in the serialized envelope.
    assert!(
        !snapshot_json.contains("https://work.example.com"),
        "exported snapshot must not expose plaintext tab URLs"
    );

    let mut core_target = MahoCore::new().with_storage(":memory:");
    {
        use base64::Engine;
        let seed_b64 = base64::engine::general_purpose::STANDARD.encode([3u8; 32]);
        core_target
            .configure_sync_encryption_from_bootstrap("https://relay.mahobrowser.com", &seed_b64)
            .expect("target bootstrap configure should succeed");
    }

    let applied_count = core_target
        .apply_sync_snapshot(&snapshot_json)
        .expect("apply_sync_snapshot should succeed");

    assert!(applied_count > 0, "should have applied snapshot entities");

    let target_spaces = core_target.get_space_view_models();
    let target_space_names: Vec<String> = target_spaces.iter().map(|s| s.name.clone()).collect();
    assert!(
        target_space_names.contains(&"Work".to_string()),
        "target should have Work space after snapshot apply"
    );
    assert!(
        target_space_names.contains(&"Personal".to_string()),
        "target should have Personal space after snapshot apply"
    );

    let target_tabs = core_target.get_tab_view_models();
    let target_tab_urls: Vec<String> = target_tabs.iter().map(|t| t.url.clone()).collect();
    assert!(
        target_tab_urls.contains(&"https://work.example.com".to_string()),
        "target should contain work tab from snapshot"
    );

    let _ = core_target
        .apply_sync_snapshot(&snapshot_json)
        .expect("re-applying snapshot should succeed");
    let after_spaces = core_target.get_space_view_models();
    assert_eq!(
        target_spaces.len(),
        after_spaces.len(),
        "re-applying snapshot must not duplicate spaces"
    );
}

#[test]
fn test_sync_snapshot_encrypted_round_trip() {
    let _ = maho_storage::sqlite::set_sqlcipher_key("test_key");

    let mut core_src = MahoCore::new().with_storage(":memory:");
    let profile_id = ProfileId::new("profile-1");

    // Configure sync encryption key via bootstrap seed (base64 of 32 bytes)
    use base64::Engine;
    let seed_b64 = base64::engine::general_purpose::STANDARD.encode([7u8; 32]);
    core_src
        .configure_sync_encryption_from_bootstrap("https://relay.mahobrowser.com", &seed_b64)
        .expect("configure bootstrap should succeed");

    let color_blue = SpaceColor {
        hue: 210.0,
        saturation: 0.8,
        brightness: 0.9,
        grain: 0.0,
    };
    let work_space = core_src.create_space("Work Encrypted", color_blue, profile_id.clone());
    core_src.activate_space(&work_space.id);

    core_src.handle_event(ShellEvent::CreateTab {
        space_id: work_space.id.clone(),
        url: Some(Url::new("https://secret.example.com/project")),
        parent_id: None,
        tab_id: None,
        window_id: None,
        is_private: false,
    });

    let snapshot_json = core_src
        .export_sync_snapshot()
        .expect("export_sync_snapshot should succeed");

    // CRITICAL: Ensure plaintext data is NOT exposed in the JSON envelope
    assert!(
        !snapshot_json.contains("Work Encrypted"),
        "snapshot payload must be encrypted, plaintext space name must not appear"
    );
    assert!(
        !snapshot_json.contains("https://secret.example.com"),
        "snapshot payload must be encrypted, plaintext URL must not appear"
    );

    let parsed_envelope: serde_json::Value =
        serde_json::from_str(&snapshot_json).expect("snapshot output must be valid JSON");
    assert!(
        parsed_envelope.get("blob").is_some(),
        "must contain blob field"
    );
    assert!(
        parsed_envelope.get("hlc_ts_ceiling").is_some(),
        "must contain hlc_ts_ceiling"
    );

    // Target with matching encryption key
    let mut core_target = MahoCore::new().with_storage(":memory:");
    core_target
        .configure_sync_encryption_from_bootstrap("https://relay.mahobrowser.com", &seed_b64)
        .expect("target bootstrap configure should succeed");

    let applied_count = core_target
        .apply_sync_snapshot(&snapshot_json)
        .expect("apply_sync_snapshot should decrypt and succeed");

    assert!(
        applied_count > 0,
        "must apply entities from encrypted snapshot"
    );

    let target_spaces = core_target.get_space_view_models();
    let target_names: Vec<String> = target_spaces.iter().map(|s| s.name.clone()).collect();
    assert!(
        target_names.contains(&"Work Encrypted".to_string()),
        "target must contain decrypted Work Encrypted space"
    );

    let target_tabs = core_target.get_tab_view_models();
    let target_urls: Vec<String> = target_tabs.iter().map(|t| t.url.clone()).collect();
    assert!(
        target_urls.contains(&"https://secret.example.com/project".to_string()),
        "target must contain decrypted tab URL"
    );

    // Active space should switch to the hydrated space
    assert_eq!(
        core_target.get_active_space_id(),
        work_space.id,
        "target must activate the hydrated space immediately"
    );
}

#[test]
fn apply_sync_snapshot_rejects_forged_plaintext_when_a_key_is_configured() {
    let _ = maho_storage::sqlite::set_sqlcipher_key("test_key");

    // Given: a core with a configured sync encryption key. Every legitimate
    // snapshot it accepts is AES-GCM authenticated under that key.
    let mut core = MahoCore::new().with_storage(":memory:");
    use base64::Engine;
    let seed_b64 = base64::engine::general_purpose::STANDARD.encode([9u8; 32]);
    core.configure_sync_encryption_from_bootstrap("https://relay.mahobrowser.com", &seed_b64)
        .expect("configure bootstrap should succeed");

    // When: an attacker who does NOT know the key submits a snapshot envelope
    // whose blob is VALID ciphertext-shaped bytes that fail AES-GCM
    // authentication but still deserialize as a Snapshot once the failed
    // decryption falls through to treating them as plaintext.
    let forged_snapshot = serde_json::json!({
        "hlc_ts_ceiling": 0,
        "entities": [],
        "spaces": [{
            "id": "forged-space",
            "name": "Injected By Attacker",
            "color": { "hue": 0.0, "saturation": 0.0, "brightness": 0.0, "grain": 0.0 },
            "profile_id": "profile-1",
        }],
    });
    let forged_bytes = serde_json::to_vec(&forged_snapshot).expect("forged json serializes");
    let forged_envelope = serde_json::json!({
        "hlc_ts_ceiling": 0,
        "blob": base64::engine::general_purpose::STANDARD.encode(&forged_bytes),
    })
    .to_string();

    let result = core.apply_sync_snapshot(&forged_envelope);

    // Then: the core must refuse it. Falling back to treating unauthenticated
    // bytes as plaintext lets anyone who can reach this API inject or delete
    // browser state without knowing the sync key.
    assert!(
        result.is_err(),
        "unauthenticated snapshot must be rejected while a sync key is configured, got {result:?}"
    );

    // And the same must hold for a RAW Snapshot object submitted with no
    // envelope at all: that path skips decryption entirely.
    let raw_result = core.apply_sync_snapshot(&forged_snapshot.to_string());
    assert!(
        raw_result.is_err(),
        "raw unauthenticated Snapshot JSON must be rejected while a sync key is configured, got {raw_result:?}"
    );

    let names: Vec<String> = core
        .get_space_view_models()
        .iter()
        .map(|s| s.name.clone())
        .collect();
    assert!(
        !names.contains(&"Injected By Attacker".to_string()),
        "forged snapshot state must not be applied, spaces were {names:?}"
    );
}
