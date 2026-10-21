use std::path::Path;

use calamine::{open_workbook_auto, Data, Reader};
use maho_agent::artifacts::{self, ArtifactHandle};

fn temp_root(test_name: &str) -> tempfile::TempDir {
    tempfile::Builder::new()
        .prefix(&format!("maho-artifact-{test_name}-"))
        .tempdir()
        .unwrap()
}

fn create(root: &Path, name: &str, json: &str) -> anyhow::Result<ArtifactHandle> {
    artifacts::create(root, name, "xlsx", json.as_bytes())
}

#[test]
fn xlsx_roundtrip_preserves_supported_cell_values() {
    let root = temp_root("roundtrip");
    let handle = create(
        root.path(),
        "sales.xlsx",
        r#"{"sheets":[{"name":"Sales","rows":[["Item","Count","Active","Empty"],["Book",12.5,true,null]]}]}"#,
    )
    .unwrap();

    assert_eq!(handle.storage_rel_path, "sales.xlsx");
    assert_eq!(
        handle.mime_type,
        "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"
    );
    assert_eq!(
        handle.size_bytes,
        std::fs::metadata(root.path().join("sales.xlsx"))
            .unwrap()
            .len()
    );
    let mut workbook = open_workbook_auto(root.path().join(&handle.storage_rel_path)).unwrap();
    let range = workbook.worksheet_range("Sales").unwrap();
    assert_eq!(range.get_value((0, 0)), Some(&Data::String("Item".into())));
    assert_eq!(range.get_value((1, 1)), Some(&Data::Float(12.5)));
    assert_eq!(range.get_value((1, 2)), Some(&Data::Bool(true)));
    assert_eq!(range.get_value((1, 3)), Some(&Data::Empty));
}

#[test]
fn xlsx_rejects_traversal_without_writing_outside_root() {
    let root = temp_root("traversal");
    let parent = root.path().parent().unwrap();
    let escape = parent.join("outside.xlsx");
    let result = create(
        root.path(),
        "../outside.xlsx",
        r#"{"sheets":[{"name":"Sheet1","rows":[]}]}"#,
    );
    assert!(result.is_err());
    assert!(!escape.exists());
    assert!(std::fs::read_dir(root.path()).unwrap().next().is_none());
}

#[test]
fn xlsx_rejects_malformed_schema_and_cells() {
    let root = temp_root("malformed");
    let invalid = [
        r#"{"sheets":[]}"#,
        r#"{"sheets":[{"name":"Bad/Name","rows":[]}]}"#,
        r#"{"sheets":[{"name":"Sheet1","rows":[[{}]]}]}"#,
        r#"{"sheets":[{"name":"Sheet1","rows":[],"extra":true}]}"#,
    ];
    for input in invalid {
        assert!(
            create(root.path(), "invalid.xlsx", input).is_err(),
            "accepted: {input}"
        );
    }
    assert!(std::fs::read_dir(root.path()).unwrap().next().is_none());
}

#[test]
fn xlsx_enforces_workbook_input_limits() {
    let root = temp_root("limits");
    let too_many_rows = format!(
        r#"{{"sheets":[{{"name":"Sheet1","rows":[{}]}}]}}"#,
        vec!["[]"; artifacts::MAX_XLSX_ROWS + 1].join(",")
    );
    assert!(create(root.path(), "rows.xlsx", &too_many_rows).is_err());

    let large_input = vec![b' '; artifacts::MAX_XLSX_INPUT_BYTES + 1];
    assert!(artifacts::create(root.path(), "large.xlsx", "xlsx", &large_input).is_err());
    assert!(std::fs::read_dir(root.path()).unwrap().next().is_none());
}

#[test]
fn artifact_handle_resolution_rejects_modified_traversal() {
    let root = temp_root("handle");
    let mut handle = create(
        root.path(),
        "safe.xlsx",
        r#"{"sheets":[{"name":"Sheet1","rows":[]}]}"#,
    )
    .unwrap();
    assert_eq!(
        artifacts::resolve(root.path(), &handle).unwrap(),
        root.path().join("safe.xlsx")
    );
    handle.storage_rel_path = "../outside.xlsx".to_string();
    assert!(artifacts::resolve(root.path(), &handle).is_err());
}
