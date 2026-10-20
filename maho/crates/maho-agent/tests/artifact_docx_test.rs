use std::io::Read;
use std::path::Path;

use maho_agent::artifacts::{self, ArtifactHandle};

fn temp_root(test_name: &str) -> tempfile::TempDir {
    tempfile::Builder::new()
        .prefix(&format!("maho-artifact-{test_name}-"))
        .tempdir()
        .unwrap()
}

fn create(root: &Path, name: &str, json: &str) -> anyhow::Result<ArtifactHandle> {
    artifacts::create(root, name, "docx", json.as_bytes())
}

fn document_xml(path: &Path) -> String {
    let file = std::fs::File::open(path).unwrap();
    let mut archive = zip::ZipArchive::new(file).expect("docx is a valid zip archive");
    for required in ["[Content_Types].xml", "_rels/.rels", "word/document.xml"] {
        assert!(
            archive.by_name(required).is_ok(),
            "docx is missing {required}"
        );
    }
    let mut xml = String::new();
    archive
        .by_name("word/document.xml")
        .unwrap()
        .read_to_string(&mut xml)
        .unwrap();
    xml
}

#[test]
fn docx_round_trip_contains_headings_paragraphs_and_tables() {
    let root = temp_root("docx-roundtrip");
    let handle = create(
        root.path(),
        "report.docx",
        r#"{"blocks":[{"type":"heading","level":1,"text":"분기 보고서"},{"type":"paragraph","text":"Revenue grew"},{"type":"table","rows":[["Item","Count"],["Book","12"]]}]}"#,
    )
    .unwrap();
    assert_eq!(
        handle.mime_type,
        "application/vnd.openxmlformats-officedocument.wordprocessingml.document"
    );
    let path = artifacts::resolve(root.path(), &handle).unwrap();
    assert_eq!(handle.size_bytes, std::fs::metadata(&path).unwrap().len());
    let xml = document_xml(&path);
    for text in ["분기 보고서", "Revenue grew", "Item", "Count", "Book", "12"] {
        assert!(xml.contains(text), "document.xml lacks {text:?}");
    }
    assert!(xml.contains("<w:tbl>"), "table not emitted");
}

#[test]
fn docx_escapes_markup_and_drops_control_characters() {
    let root = temp_root("docx-escape");
    let handle = create(
        root.path(),
        "escape.docx",
        &serde_json::json!({"blocks":[{"type":"paragraph","text":"<w:b/> & \"quoted\" \u{7}bell"}]})
            .to_string(),
    )
    .unwrap();
    let xml = document_xml(&artifacts::resolve(root.path(), &handle).unwrap());
    assert!(xml.contains("&lt;w:b/&gt; &amp;"), "markup was not escaped");
    assert!(!xml.contains('\u{7}'), "control character reached the XML");
}

#[test]
fn empty_document_is_valid() {
    let root = temp_root("docx-empty");
    let handle = create(root.path(), "blank.docx", r#"{"blocks":[]}"#).unwrap();
    document_xml(&artifacts::resolve(root.path(), &handle).unwrap());
}

#[test]
fn docx_rejects_malformed_and_out_of_bounds_input() {
    let root = temp_root("docx-reject");
    for (name, json) in [
        (
            "a.docx",
            r#"{"blocks":[{"type":"heading","level":7,"text":"x"}]}"#,
        ),
        ("b.docx", r#"{"blocks":[{"type":"table","rows":[]}]}"#),
        (
            "c.docx",
            r#"{"blocks":[{"type":"table","rows":[["a","b"],["c"]]}]}"#,
        ),
        ("d.docx", r#"{"blocks":[{"type":"image","src":"x"}]}"#),
        ("e.docx", r#"{"blocks":[],"extra":1}"#),
        ("f.docx", "not json"),
    ] {
        assert!(create(root.path(), name, json).is_err(), "{name} accepted");
        assert!(!root.path().join(name).exists(), "{name} written on error");
    }
    let too_many = format!(
        r#"{{"blocks":[{}]}}"#,
        vec![r#"{"type":"paragraph","text":"x"}"#; artifacts::MAX_DOCX_BLOCKS + 1].join(",")
    );
    assert!(create(root.path(), "g.docx", &too_many).is_err());
}

#[test]
fn docx_path_rules_match_xlsx() {
    let root = temp_root("docx-paths");
    let doc = r#"{"blocks":[]}"#;
    assert!(create(root.path(), "../escape.docx", doc).is_err());
    assert!(create(root.path(), "report.xlsx", doc).is_err());
    create(root.path(), "once.docx", doc).unwrap();
    assert!(
        create(root.path(), "once.docx", doc).is_err(),
        "existing artifact was overwritten"
    );
}
