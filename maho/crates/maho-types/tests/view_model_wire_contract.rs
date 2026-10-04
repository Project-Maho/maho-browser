use std::collections::HashSet;

use maho_types::boost::*;
use maho_types::common::DateTime;
use maho_types::identifiers::{BoostId, ProfileId, SpaceId};
use maho_types::space::*;
use maho_types::traits::shell_renderer::{BoostViewModel, SpaceViewModel};

#[test]
fn test_space_view_model_wire_contract() {
    let space_vm = SpaceViewModel {
        id: SpaceId::new("space-123"),
        name: "Personal".to_string(),
        color: SpaceColor {
            hue: 210.5,
            saturation: 0.8,
            brightness: 0.9,
            grain: 0.1,
        },
        theme: Some(SpaceTheme::Solid {
            color: SpaceColor {
                hue: 210.5,
                saturation: 0.8,
                brightness: 0.9,
                grain: 0.1,
            },
            opacity: 1.0,
            schema_version: 0,
        }),
        tab_count: 5,
        is_active: true,
        icon: Some("rocket".to_string()),
        profile_id: Some(ProfileId::new("profile-abc")),
        profile_name: Some("Work Profile".to_string()),
        order_index: Some(2),
    };

    let json_val = serde_json::to_value(&space_vm).expect("SpaceViewModel should serialize");
    let map = json_val
        .as_object()
        .expect("SpaceViewModel must serialize to a JSON object");

    // Exact expected key set for SpaceViewModel
    let expected_keys: HashSet<&str> = [
        "id",
        "name",
        "color",
        "theme",
        "tabCount",
        "isActive",
        "icon",
        "profileId",
        "profileName",
        "orderIndex",
    ]
    .into_iter()
    .collect();

    let actual_keys: HashSet<&str> = map.keys().map(|s| s.as_str()).collect();
    assert_eq!(actual_keys, expected_keys, "SpaceViewModel keys mismatch");

    // Confirm color and theme are JSON objects
    assert!(
        map.get("color").expect("color field present").is_object(),
        "color must be a JSON object"
    );
    assert!(
        map.get("theme").expect("theme field present").is_object(),
        "theme must be a JSON object"
    );

    // Explicit absence of snake_case aliases
    let snake_case_aliases = [
        "tab_count",
        "is_active",
        "profile_id",
        "profile_name",
        "order_index",
    ];
    for alias in snake_case_aliases {
        assert!(
            !map.contains_key(alias),
            "SpaceViewModel wire output must not contain snake_case alias '{alias}'"
        );
    }

    // Optional field behavior deliberately: when None, skip_serializing_if applies for optional fields
    let space_vm_minimal = SpaceViewModel {
        id: SpaceId::new("space-456"),
        name: "Minimal".to_string(),
        color: SpaceColor {
            hue: 0.0,
            saturation: 0.0,
            brightness: 0.5,
            grain: 0.0,
        },
        theme: None,
        tab_count: 0,
        is_active: false,
        icon: None,
        profile_id: None,
        profile_name: None,
        order_index: None,
    };

    let json_val_min =
        serde_json::to_value(&space_vm_minimal).expect("SpaceViewModel minimal should serialize");
    let map_min = json_val_min
        .as_object()
        .expect("SpaceViewModel minimal must serialize to an object");

    let expected_keys_min: HashSet<&str> = ["id", "name", "color", "tabCount", "isActive"]
        .into_iter()
        .collect();
    let actual_keys_min: HashSet<&str> = map_min.keys().map(|s| s.as_str()).collect();
    assert_eq!(
        actual_keys_min, expected_keys_min,
        "Minimal SpaceViewModel keys mismatch (None optional fields must be omitted)"
    );
}

#[test]
fn test_boost_contract_types_wire_contract() {
    let boost = Boost {
        id: BoostId::new("boost-789"),
        domain: "example.com".to_string(),
        name: "Dark Mode Boost".to_string(),
        color: ColorBoost {
            dot_pos: Point { x: 10.0, y: 20.0 },
            dot_distance: 5.0,
            dot_angle_deg: 45.0,
            secondary_dot_pos: Point { x: 15.0, y: 25.0 },
            secondary_dot_angle_deg_delta: 90.0,
            magic_theme: true,
            color_boost_enabled: true,
            smart_invert: false,
            contrast: 1.1,
            brightness: 0.95,
            saturation: 1.2,
        },
        typography: TypographyBoost {
            font_family: "Roboto".to_string(),
            case_mode: CaseMode::Upper,
            size_mode: SizeMode::K110,
        },
        zap_selectors: vec!["#ad-banner".to_string(), ".popup".to_string()],
        custom_css: "body { background: #121212 !important; }".to_string(),
        change_was_made: true,
        created_at: DateTime::now(),
        updated_at: DateTime::now(),
    };

    let json_val = serde_json::to_value(&boost).expect("Boost should serialize");
    let map = json_val
        .as_object()
        .expect("Boost must serialize to a JSON object");

    let expected_keys: HashSet<&str> = [
        "id",
        "domain",
        "name",
        "color",
        "typography",
        "zapSelectors",
        "customCss",
        "changeWasMade",
        "createdAt",
        "updatedAt",
    ]
    .into_iter()
    .collect();

    let actual_keys: HashSet<&str> = map.keys().map(|s| s.as_str()).collect();
    assert_eq!(actual_keys, expected_keys, "Boost keys mismatch");

    // ColorBoost exact keys
    let color_map = map
        .get("color")
        .expect("color field")
        .as_object()
        .expect("color is object");
    let expected_color_keys: HashSet<&str> = [
        "dotPos",
        "dotDistance",
        "dotAngleDeg",
        "secondaryDotPos",
        "secondaryDotAngleDegDelta",
        "magicTheme",
        "colorBoostEnabled",
        "smartInvert",
        "contrast",
        "brightness",
        "saturation",
    ]
    .into_iter()
    .collect();
    let actual_color_keys: HashSet<&str> = color_map.keys().map(|s| s.as_str()).collect();
    assert_eq!(
        actual_color_keys, expected_color_keys,
        "ColorBoost keys mismatch"
    );

    // Explicit absence of snake_case aliases in Boost & ColorBoost
    let snake_case_aliases = [
        "zap_selectors",
        "custom_css",
        "change_was_made",
        "created_at",
        "updated_at",
        "dot_pos",
        "dot_distance",
        "dot_angle_deg",
        "secondary_dot_pos",
        "secondary_dot_angle_deg_delta",
        "magic_theme",
        "color_boost_enabled",
        "smart_invert",
    ];
    for alias in snake_case_aliases {
        assert!(
            !map.contains_key(alias) && !color_map.contains_key(alias),
            "Boost wire output must not contain snake_case alias '{alias}'"
        );
    }

    // BoostViewModel returned over FFI
    let boost_vm = BoostViewModel {
        id: BoostId::new("boost-789"),
        domain: "example.com".to_string(),
        name: "Dark Mode Boost".to_string(),
        custom_css: Some("body { background: #121212 !important; }".to_string()),
        enabled: true,
    };

    let vm_val = serde_json::to_value(&boost_vm).expect("BoostViewModel should serialize");
    let vm_map = vm_val
        .as_object()
        .expect("BoostViewModel must serialize to object");

    let expected_vm_keys: HashSet<&str> = ["id", "domain", "name", "customCss", "enabled"]
        .into_iter()
        .collect();
    let actual_vm_keys: HashSet<&str> = vm_map.keys().map(|s| s.as_str()).collect();
    assert_eq!(
        actual_vm_keys, expected_vm_keys,
        "BoostViewModel keys mismatch"
    );
    assert!(!vm_map.contains_key("custom_css"));

    // ZenCompatBoostData wire contract
    let zen_dto = ZenCompatBoostData::from(&boost);
    let zen_val = serde_json::to_value(&zen_dto).expect("ZenCompatBoostData should serialize");
    let zen_map = zen_val
        .as_object()
        .expect("ZenCompatBoostData must serialize to object");

    let expected_zen_keys: HashSet<&str> = [
        "boostName",
        "dotPos",
        "dotDistance",
        "dotAngleDeg",
        "secondaryDotPos",
        "secondaryDotAngleDegDelta",
        "magicTheme",
        "enableColorBoost",
        "smartInvert",
        "contrast",
        "brightness",
        "saturation",
        "fontFamily",
        "textCaseOverride",
        "sizeOverride",
        "zapSelectors",
        "customCSS",
        "changeWasMade",
    ]
    .into_iter()
    .collect();
    let actual_zen_keys: HashSet<&str> = zen_map.keys().map(|s| s.as_str()).collect();
    assert_eq!(
        actual_zen_keys, expected_zen_keys,
        "ZenCompatBoostData keys mismatch"
    );

    let zen_snake_aliases = [
        "boost_name",
        "dot_pos",
        "dot_distance",
        "dot_angle_deg",
        "secondary_dot_pos",
        "secondary_dot_angle_deg_delta",
        "magic_theme",
        "enable_color_boost",
        "smart_invert",
        "font_family",
        "text_case_override",
        "size_override",
        "zap_selectors",
        "custom_css",
        "change_was_made",
    ];
    for alias in zen_snake_aliases {
        assert!(
            !zen_map.contains_key(alias),
            "ZenCompatBoostData wire output must not contain snake_case alias '{alias}'"
        );
    }
}
