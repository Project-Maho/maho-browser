use maho_types::ShellEventKind;

#[test]
fn test_event_kind_classification() {
    assert_eq!(
        ShellEventKind::classify("create_tab"),
        ShellEventKind::Structural
    );
    assert_eq!(
        ShellEventKind::classify("close_tab"),
        ShellEventKind::Structural
    );
    assert_eq!(
        ShellEventKind::classify("reorder_tab"),
        ShellEventKind::Structural
    );
    assert_eq!(
        ShellEventKind::classify("move_tab_to_folder"),
        ShellEventKind::Structural
    );
    assert_eq!(
        ShellEventKind::classify("pin_tab"),
        ShellEventKind::Structural
    );
    assert_eq!(
        ShellEventKind::classify("tab_title_updated"),
        ShellEventKind::Cosmetic
    );
    assert_eq!(
        ShellEventKind::classify("tab_url_updated"),
        ShellEventKind::Cosmetic
    );
    assert_eq!(
        ShellEventKind::classify("tab_favicon_updated"),
        ShellEventKind::Cosmetic
    );
    assert_eq!(
        ShellEventKind::classify("tab_loading_state_changed"),
        ShellEventKind::Cosmetic
    );
    // Unknown events default to Structural (safe fallback)
    assert_eq!(
        ShellEventKind::classify("unknown_event"),
        ShellEventKind::Structural
    );
}

#[test]
fn test_image_data_deserialization() {
    use maho_types::common::{ImageData, ImageFormat};

    // Case 1: Deserialize from JSON array of numbers (standard Vec<u8> behavior)
    let json_seq = r#"{
        "data": [73, 78, 68, 79],
        "width": 16,
        "height": 16,
        "format": "png"
    }"#;
    let decoded_seq: ImageData = serde_json::from_str(json_seq).unwrap();
    assert_eq!(decoded_seq.data, vec![73, 78, 68, 79]);
    assert_eq!(decoded_seq.width, 16);
    assert_eq!(decoded_seq.height, 16);
    assert!(matches!(decoded_seq.format, ImageFormat::Png));

    // Case 2: Deserialize from base64-encoded string
    let json_b64 = r#"{
        "data": "SU5ETw==",
        "width": 16,
        "height": 16,
        "format": "png"
    }"#;
    let decoded_b64: ImageData = serde_json::from_str(json_b64).unwrap();
    assert_eq!(decoded_b64.data, vec![73, 78, 68, 79]);
    assert_eq!(decoded_b64.width, 16);
    assert_eq!(decoded_b64.height, 16);
    assert!(matches!(decoded_b64.format, ImageFormat::Png));
}
