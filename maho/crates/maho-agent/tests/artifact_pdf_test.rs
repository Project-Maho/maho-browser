use std::path::Path;

use maho_agent::artifacts::{self, ArtifactHandle};

fn temp_root(test_name: &str) -> tempfile::TempDir {
    tempfile::Builder::new()
        .prefix(&format!("maho-artifact-{test_name}-"))
        .tempdir()
        .unwrap()
}

fn create(root: &Path, name: &str, json: &str) -> anyhow::Result<ArtifactHandle> {
    artifacts::create(root, name, "pdf", json.as_bytes())
}

fn env_lock() -> std::sync::MutexGuard<'static, ()> {
    static LOCK: std::sync::Mutex<()> = std::sync::Mutex::new(());
    LOCK.lock()
        .unwrap_or_else(std::sync::PoisonError::into_inner)
}

#[test]
fn korean_pdf_is_a_valid_pdf_with_extractable_text() {
    let _lock = env_lock();
    let root = temp_root("pdf-korean");
    let handle = create(
        root.path(),
        "report.pdf",
        r#"{"blocks":[{"type":"heading","level":1,"text":"분기 보고서"},{"type":"paragraph","text":"매출이 12% 증가했습니다."},{"type":"table","rows":[["항목","수량"],["책","12"]]}]}"#,
    )
    .unwrap();
    assert_eq!(handle.mime_type, "application/pdf");
    let path = artifacts::resolve(root.path(), &handle).unwrap();
    let bytes = std::fs::read(&path).unwrap();
    assert!(bytes.starts_with(b"%PDF-"), "missing PDF signature");
    assert_eq!(handle.size_bytes, bytes.len() as u64);
    let document = lopdf::Document::load_mem(&bytes).expect("pdf parses");
    let pages: Vec<u32> = document.get_pages().keys().copied().collect();
    let text = document.extract_text(&pages).expect("text extracts");
    for expected in ["분기 보고서", "매출이", "항목", "수량"] {
        assert!(
            text.contains(expected),
            "{expected:?} not extractable: {text:?}"
        );
    }
}

#[test]
fn long_documents_paginate_and_page_cap_is_enforced() {
    let _lock = env_lock();
    let root = temp_root("pdf-pages");
    let paragraphs = |n: usize| {
        format!(
            r#"{{"blocks":[{}]}}"#,
            vec![r#"{"type":"paragraph","text":"line"}"#; n].join(",")
        )
    };
    let handle = create(root.path(), "two.pdf", &paragraphs(60)).unwrap();
    let bytes = std::fs::read(artifacts::resolve(root.path(), &handle).unwrap()).unwrap();
    let pages = lopdf::Document::load_mem(&bytes).unwrap().get_pages().len();
    assert!(pages >= 2, "60 paragraphs fit on {pages} page(s)");

    let too_long = format!(
        r#"{{"blocks":[{{"type":"paragraph","text":"{}"}}]}}"#,
        "가".repeat(400_000)
    );
    assert!(create(root.path(), "huge.pdf", &too_long).is_err());
    assert!(!root.path().join("huge.pdf").exists());
}

#[test]
fn missing_font_fails_instead_of_dropping_glyphs() {
    let _lock = env_lock();
    let root = temp_root("pdf-nofont");
    let empty_font_dir = temp_root("pdf-nofont-dir");
    std::env::set_var("MAHO_PDF_FONT", empty_font_dir.path().join("absent.ttf"));
    let result = create(
        root.path(),
        "x.pdf",
        r#"{"blocks":[{"type":"paragraph","text":"\uD800\uDC00 不存在的字"}]}"#,
    );
    std::env::remove_var("MAHO_PDF_FONT");
    let error = result.expect_err("text no font covers must be rejected");
    assert!(error.to_string().contains("font"), "{error:#}");
    assert!(!root.path().join("x.pdf").exists());
}

#[test]
fn pdf_rejects_malformed_input_and_bad_paths() {
    let _lock = env_lock();
    let root = temp_root("pdf-reject");
    for (name, json) in [
        (
            "a.pdf",
            r#"{"blocks":[{"type":"heading","level":9,"text":"x"}]}"#,
        ),
        (
            "b.pdf",
            r#"{"blocks":[{"type":"table","rows":[["a"],["b","c"]]}]}"#,
        ),
        ("c.pdf", r#"{"blocks":[],"x":1}"#),
        ("d.pdf", "not json"),
    ] {
        assert!(create(root.path(), name, json).is_err(), "{name} accepted");
        assert!(!root.path().join(name).exists());
    }
    assert!(create(root.path(), "../out.pdf", r#"{"blocks":[]}"#).is_err());
    assert!(create(root.path(), "out.docx", r#"{"blocks":[]}"#).is_err());
}
