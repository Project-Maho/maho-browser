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
    let path =
        repo_root().join("maho/android-shell/app/src/main/java/dev/maho/browser/MahoBridge.kt");
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
            let marker = "extern \"system\" fn Java_dev_maho_browser_MahoBridge_";
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

#[test]
fn content_blocker_jni_exports_do_not_bypass_candidate_promotion() {
    let path = repo_root().join("maho/crates/maho-jni/src/lib.rs");
    let source = fs::read_to_string(path).expect("read maho-jni lib.rs");

    assert!(source.contains("nativeApplyFilterListUpdateResult"));
    assert!(!source.contains("core.update_filter_list_content(&id_str, content_str)"));
    assert!(!source.contains("core.rebuild_content_rules();"));
}

#[test]
fn active_agent_create_descriptor_and_call_include_trusted_artifact_root() {
    let path =
        repo_root().join("maho/android-shell/app/src/main/java/dev/maho/browser/MahoBridge.kt");
    let source = fs::read_to_string(path).expect("read MahoBridge.kt");

    assert!(source.contains(
        "private external fun nativeAgentCreateSession(corePtr: Long, sessionId: String, artifactRoot: String): Long"
    ));
    assert!(source.contains("nativeAgentCreateSession(ptr, sessionId, artifactRoot)"));
    assert!(source.contains("File(context.filesDir, \"agent-artifacts\")"));
    assert!(!source.contains("fun agentCreateSession(sessionId: String, artifactRoot:"));
}

#[test]
fn active_agent_artifact_api_uses_opaque_ids_and_bytes_not_paths() {
    let path =
        repo_root().join("maho/android-shell/app/src/main/java/dev/maho/browser/MahoBridge.kt");
    let source = fs::read_to_string(path).expect("read MahoBridge.kt");

    assert!(source.contains("fun agentListArtifacts(sessionHandle: Long): String?"));
    assert!(source
        .contains("fun agentReadArtifact(sessionHandle: Long, artifactId: String): ByteArray?"));
    assert!(source
        .contains("private external fun nativeAgentListArtifacts(sessionHandle: Long): String?"));
    assert!(source.contains("private external fun nativeAgentReadArtifact(sessionHandle: Long, artifactId: String): ByteArray?"));
    assert!(!source.contains("nativeAgentReadArtifactPath"));
}

#[test]
fn active_android_artifact_share_uses_restricted_file_provider() {
    let root = repo_root();
    let bridge = fs::read_to_string(
        root.join("maho/android-shell/app/src/main/java/dev/maho/browser/MahoBridge.kt"),
    )
    .expect("read MahoBridge.kt");
    let manifest = fs::read_to_string(
        root.join("maho/android-shell/app/src/main/AndroidManifest.xml"),
    )
    .expect("read AndroidManifest.xml");
    let paths = fs::read_to_string(
        root.join("maho/android-shell/app/src/main/res/xml/artifact_file_paths.xml"),
    )
    .expect("read FileProvider paths");

    assert!(bridge.contains("File(context.filesDir, \"agent-artifacts\")"));
    assert!(bridge.contains("File(context.cacheDir, \"artifact_exports\")"));
    assert!(bridge.contains("Intent.ACTION_SEND"));
    assert!(bridge.contains("Intent.FLAG_GRANT_READ_URI_PERMISSION"));
    assert!(manifest.contains("android:exported=\"false\""));
    assert!(manifest.contains("@xml/artifact_file_paths"));
    assert!(paths.contains("<cache-path"));
    assert!(paths.contains("path=\"artifact_exports/\""));
    assert!(!paths.contains("<files-path"));
    assert!(!paths.contains("path=\".\""));
}
