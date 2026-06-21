use std::collections::BTreeSet;
use std::fs;
use std::path::PathBuf;

fn repo_root() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .ancestors()
        .nth(3)
        .expect("repo root")
        .to_path_buf()
}

fn kotlin_native_names() -> BTreeSet<String> {
    let path = repo_root()
        .join("maho/android-shell/app/src/main/java/dev/maho/browser/MahoBridge.kt");
    let src = fs::read_to_string(path).expect("read MahoBridge.kt");

    src.lines()
        .filter_map(|line| {
            let marker = "private external fun ";
            let start = line.find(marker)? + marker.len();
            let rest = &line[start..];
            let name = rest.split('(').next()?.trim();
            name.starts_with("native").then(|| name.to_string())
        })
        .collect()
}

fn rust_export_names() -> BTreeSet<String> {
    let path = repo_root().join("maho/crates/maho-jni/src/lib.rs");
    let src = fs::read_to_string(path).expect("read maho-jni lib.rs");

    src.lines()
        .filter_map(|line| {
            let marker = "pub extern \"system\" fn Java_dev_maho_browser_MahoBridge_";
            let start = line.find(marker)? + marker.len();
            let rest = &line[start..];
            let name = rest.split(['(', '<']).next()?.trim();
            name.starts_with("native").then(|| name.to_string())
        })
        .collect()
}

#[test]
fn maho_bridge_native_declarations_have_matching_rust_exports() {
    let kotlin = kotlin_native_names();
    let rust = rust_export_names();
    let missing: Vec<_> = kotlin.difference(&rust).cloned().collect();

    assert!(
        missing.is_empty(),
        "Missing Rust JNI exports for Kotlin native declarations: {missing:?}",
    );
}
