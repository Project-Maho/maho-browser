//! Verifies Safari parser behavior with and without macOS Full Disk Access.

use maho_import::parsers::safari::bookmarks::parse_safari_bookmarks;
use maho_import::parsers::safari::history::parse_safari_history;
use maho_import::ImportError;
use std::path::Path;

#[test]
#[ignore]
fn safari_without_fda_returns_permission_denied() {
    let safari_dir = Path::new("/Users/indo/Library/Safari");
    let bookmarks = safari_dir.join("Bookmarks.plist");
    if !bookmarks.exists() {
        eprintln!("No Safari install; skipping");
        return;
    }
    if std::fs::read(&bookmarks).is_ok() {
        eprintln!("FDA appears granted; this test verifies the unauthorized path. Skipping.");
        return;
    }
    let err = parse_safari_bookmarks(safari_dir).unwrap_err();
    match &err {
        ImportError::PermissionDenied(p) => {
            assert!(
                p.contains("Bookmarks.plist"),
                "expected path in error: {}",
                p
            );
        }
        other => panic!("expected PermissionDenied, got {:?}", other),
    }
}

#[test]
#[ignore]
fn safari_with_fda_via_accessible_copy() {
    let safari_dir = Path::new("/Users/indo/Library/Safari");
    let src = safari_dir.join("Bookmarks.plist");
    if !src.exists() {
        return;
    }
    let data = match std::fs::read(&src) {
        Ok(d) => d,
        Err(_) => {
            eprintln!("FDA not granted; cannot stage accessible fixture. Skipping.");
            return;
        }
    };
    let tmp = tempfile::tempdir().unwrap();
    std::fs::write(tmp.path().join("Bookmarks.plist"), &data).unwrap();
    let bookmarks =
        parse_safari_bookmarks(tmp.path()).expect("parse should succeed on accessible copy");
    assert!(
        !bookmarks.is_empty(),
        "real Safari profile should have at least one bookmark/reading-list entry"
    );
}

#[test]
#[ignore]
fn safari_history_without_fda_returns_permission_denied() {
    let safari_dir = Path::new("/Users/indo/Library/Safari");
    let db = safari_dir.join("History.db");
    if !db.exists() {
        eprintln!("No Safari history install; skipping");
        return;
    }
    if std::fs::read(&db).is_ok() {
        eprintln!("FDA appears granted; this test verifies the unauthorized path. Skipping.");
        return;
    }
    let err = parse_safari_history(safari_dir).unwrap_err();
    match &err {
        ImportError::PermissionDenied(p) => {
            assert!(p.contains("History.db"), "expected path in error: {}", p);
        }
        other => panic!("expected PermissionDenied, got {:?}", other),
    }
}

#[test]
#[ignore]
fn safari_history_with_fda_via_accessible_copy() {
    let safari_dir = Path::new("/Users/indo/Library/Safari");
    let src = safari_dir.join("History.db");
    if !src.exists() {
        return;
    }
    let data = match std::fs::read(&src) {
        Ok(d) => d,
        Err(_) => {
            eprintln!("FDA not granted; cannot stage accessible fixture. Skipping.");
            return;
        }
    };
    let tmp = tempfile::tempdir().unwrap();
    std::fs::write(tmp.path().join("History.db"), &data).unwrap();
    let history =
        parse_safari_history(tmp.path()).expect("parse should succeed on accessible copy");
    // History db could be empty for a new user, but let's assert it runs without error.
    println!("Safari history entries parsed: {}", history.len());
}
