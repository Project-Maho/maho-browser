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

fn kotlin_native_declarations() -> Vec<(String, String)> {
    let path =
        repo_root().join("maho/android-shell/app/src/main/java/dev/maho/browser/MahoBridge.kt");
    let src = fs::read_to_string(path).expect("read MahoBridge.kt");

    src.lines()
        .filter_map(|line| {
            let tokens: Vec<_> = line.split_whitespace().collect();
            let external = tokens.iter().position(|token| *token == "external")?;
            if tokens.get(external + 1) != Some(&"fun") {
                return None;
            }
            let name = tokens.get(external + 2)?.split('(').next()?;
            name.starts_with("native").then(|| {
                let visibility = tokens[..external].join(" ");
                (name.to_string(), visibility)
            })
        })
        .collect()
}

fn kotlin_native_names() -> BTreeSet<String> {
    kotlin_native_declarations()
        .into_iter()
        .map(|(name, _)| name)
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
fn maho_bridge_native_declarations_are_private_and_have_matching_rust_exports() {
    let declarations = kotlin_native_declarations();
    let non_private: Vec<_> = declarations
        .iter()
        .filter(|(_, visibility)| visibility != "private")
        .cloned()
        .collect();
    assert!(
        non_private.is_empty(),
        "Kotlin native declarations must be private to preserve unsuffixed JNI names: {non_private:?}",
    );

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
    let root = repo_root();
    let bridge = fs::read_to_string(
        root.join("maho/android-shell/app/src/main/java/dev/maho/browser/MahoBridge.kt"),
    )
    .expect("read MahoBridge.kt");
    let agent = fs::read_to_string(
        root.join("maho/android-shell/app/src/main/java/dev/maho/browser/bridge/BridgeAgent.kt"),
    )
    .expect("read BridgeAgent.kt");

    assert!(bridge.contains(
        "private external fun nativeAgentCreateSession(corePtr: Long, sessionId: String, artifactRoot: String): Long"
    ));
    assert!(bridge.contains("nativeAgentCreateSession(corePtr, sessionId, artifactRoot)"));
    assert!(agent.contains("File(context.filesDir, \"agent-artifacts\")"));
    assert!(!agent.contains("fun agentCreateSession(sessionId: String, artifactRoot:"));
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
    let agent = fs::read_to_string(
        root.join("maho/android-shell/app/src/main/java/dev/maho/browser/bridge/BridgeAgent.kt"),
    )
    .expect("read BridgeAgent.kt");
    let manifest =
        fs::read_to_string(root.join("maho/android-shell/app/src/main/AndroidManifest.xml"))
            .expect("read AndroidManifest.xml");
    let paths = fs::read_to_string(
        root.join("maho/android-shell/app/src/main/res/xml/artifact_file_paths.xml"),
    )
    .expect("read FileProvider paths");

    assert!(agent.contains("File(context.filesDir, \"agent-artifacts\")"));
    assert!(agent.contains("File(context.cacheDir, \"artifact_exports\")"));
    assert!(agent.contains("Intent.ACTION_SEND"));
    assert!(agent.contains("Intent.FLAG_GRANT_READ_URI_PERMISSION"));
    assert!(manifest.contains("android:exported=\"false\""));
    assert!(manifest.contains("@xml/artifact_file_paths"));
    assert!(paths.contains("<cache-path"));
    assert!(paths.contains("path=\"artifact_exports/\""));
    assert!(!paths.contains("<files-path"));
    assert!(!paths.contains("path=\".\""));
}
