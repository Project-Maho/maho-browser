use maho_bench::browser_cli::{
    all_workloads, get_workload, run_benchmark, run_self_test, validate_all_fixtures,
    validate_fixture_content, BenchmarkResult, DriverType, FakeDriver,
};
use std::fs;
use std::path::PathBuf;

#[test]
fn test_schema_round_trip() {
    let result = BenchmarkResult {
        workload: "w1_static_form".to_string(),
        driver: DriverType::RawMcp,
        goal_completion_wall_ms: 1542,
        agent_decision_turns: 6,
        browser_rpc_count: 11,
        ax_snapshot_count: 3,
        ax_snapshot_wall_ms: vec![14.5, 12.8, 15.1],
        observation_wire_bytes: 4096,
        retries: 0,
    };

    // Serialize to JSON string
    let json_str = serde_json::to_string_pretty(&result).expect("serialization failed");

    // Verify all keys are present in JSON
    let val: serde_json::Value = serde_json::from_str(&json_str).expect("parse json value failed");
    assert_eq!(val["workload"], "w1_static_form");
    assert_eq!(val["driver"], "raw_mcp");
    assert_eq!(val["goal_completion_wall_ms"], 1542);
    assert_eq!(val["agent_decision_turns"], 6);
    assert_eq!(val["browser_rpc_count"], 11);
    assert_eq!(val["ax_snapshot_count"], 3);
    assert!(val["ax_snapshot_wall_ms"].is_array());
    assert_eq!(val["ax_snapshot_wall_ms"].as_array().unwrap().len(), 3);
    assert_eq!(val["observation_wire_bytes"], 4096);
    assert_eq!(val["retries"], 0);

    // Deserialize back and assert equality
    let deserialized: BenchmarkResult =
        serde_json::from_str(&json_str).expect("deserialization failed");
    assert_eq!(result, deserialized);
}

#[test]
fn test_driver_type_parsing_and_serialization() {
    let drivers = [
        (DriverType::RawMcp, "raw_mcp"),
        (DriverType::Repl, "repl"),
        (DriverType::SemanticCli, "semantic_cli"),
    ];

    for (driver_type, expected_str) in drivers {
        let serialized = serde_json::to_string(&driver_type).expect("serialize driver");
        assert_eq!(serialized, format!("\"{expected_str}\""));

        let deserialized: DriverType =
            serde_json::from_str(&serialized).expect("deserialize driver");
        assert_eq!(deserialized, driver_type);

        let parsed: DriverType = expected_str.parse().expect("from_str driver");
        assert_eq!(parsed, driver_type);
    }
}

#[test]
fn test_workload_definitions_completeness() {
    let workloads = all_workloads();
    assert_eq!(workloads.len(), 6);

    let expected_ids = [
        "w1_static_form",
        "w2_spa_modal",
        "w3_dynamic_list",
        "w4_iframe_page",
        "w5_large_ax_tree",
        "w6_navigation_transition",
    ];

    for expected_id in expected_ids {
        let found = workloads.iter().find(|w| w.id == expected_id);
        assert!(
            found.is_some(),
            "Missing workload definition: {expected_id}"
        );
        let w = found.unwrap();
        assert!(!w.name.is_empty());
        assert!(!w.fixture_path.is_empty());
        assert!(!w.goal_description.is_empty());
        assert!(!w.steps.is_empty());
    }
}

#[test]
fn test_fixture_validation_all_fixtures() {
    let validated = validate_all_fixtures(None).expect("validate all fixtures failed");
    assert_eq!(validated.len(), 6);
    assert!(validated.contains(&"w1_static_form".to_string()));
    assert!(validated.contains(&"w2_spa_modal".to_string()));
    assert!(validated.contains(&"w3_dynamic_list".to_string()));
    assert!(validated.contains(&"w4_iframe_page".to_string()));
    assert!(validated.contains(&"w5_large_ax_tree".to_string()));
    assert!(validated.contains(&"w6_navigation_transition".to_string()));
}

#[test]
fn test_fixture_validation_individual_checks() {
    // W1: Check that missing required input fails validation
    let invalid_w1 = "<html><body><h1>No form inputs</h1></body></html>";
    assert!(validate_fixture_content("w1_static_form", invalid_w1).is_err());

    // W2: Check missing dialog fails validation
    let invalid_w2 = "<html><body><button id='open-modal-btn'>Open</button></body></html>";
    assert!(validate_fixture_content("w2_spa_modal", invalid_w2).is_err());

    // W3: Check missing dynamic list fails validation
    let invalid_w3 = "<html><body><div>No list here</div></body></html>";
    assert!(validate_fixture_content("w3_dynamic_list", invalid_w3).is_err());

    // W4: Check missing iframe fails validation
    let invalid_w4 = "<html><body><div>No iframe here</div></body></html>";
    assert!(validate_fixture_content("w4_iframe_page", invalid_w4).is_err());

    // W5: Check missing controls fails validation
    let invalid_w5 = "<html><body><div>Small tree</div></body></html>";
    assert!(validate_fixture_content("w5_large_ax_tree", invalid_w5).is_err());

    // W6: Check missing dashboard transition views fails validation
    let invalid_w6 = "<html><body><div>Only login</div></body></html>";
    assert!(validate_fixture_content("w6_navigation_transition", invalid_w6).is_err());
}

#[test]
fn test_fake_driver_execution() {
    let mut driver = FakeDriver::new(DriverType::Repl);
    let workload = get_workload("w1_static_form").expect("w1 not found");

    let result = run_benchmark(&mut driver, &workload).expect("benchmark run failed");

    assert_eq!(result.workload, "w1_static_form");
    assert_eq!(result.driver, DriverType::Repl);
    assert_eq!(result.agent_decision_turns, workload.steps.len());
    assert!(result.browser_rpc_count >= workload.steps.len());
    assert_eq!(result.ax_snapshot_count, 3);
    assert_eq!(result.ax_snapshot_wall_ms.len(), 3);
    assert!(result.observation_wire_bytes > 0);
    assert_eq!(result.retries, 0);
}

#[test]
fn test_all_workloads_end_to_end_with_fake_driver() {
    for workload in all_workloads() {
        for driver_type in [
            DriverType::RawMcp,
            DriverType::Repl,
            DriverType::SemanticCli,
        ] {
            let mut driver = FakeDriver::new(driver_type);
            let result = run_benchmark(&mut driver, &workload).unwrap_or_else(|e| {
                panic!("Failed running {} with {}: {}", workload.id, driver_type, e)
            });
            assert_eq!(result.workload, workload.id);
            assert_eq!(result.driver, driver_type);
            assert!(result.browser_rpc_count > 0);
        }
    }
}

#[test]
fn test_harness_self_test_mode_writes_and_verifies_artifact() {
    let out_path = PathBuf::from("browser_cli/sample-results/selftest.json");
    let result = run_self_test(Some(&out_path)).expect("self test failed");

    assert_eq!(result.workload, "w1_static_form");
    assert_eq!(result.driver, DriverType::RawMcp);

    // Verify file on disk
    assert!(out_path.exists(), "selftest.json was not created");
    let content = fs::read_to_string(&out_path).expect("failed reading selftest.json");
    let parsed: BenchmarkResult =
        serde_json::from_str(&content).expect("failed parsing selftest.json");
    assert_eq!(parsed, result);
}
