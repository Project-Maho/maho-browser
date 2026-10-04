// Integration test for `maho mcp` (D2): the command must exec-replace the
// current process with the sibling `maho-browser-mcp` binary resolved via
// std::env::current_exe() parent dir.
//
// We copy the real `maho` binary into a temp dir, drop a stub executable named
// `maho-browser-mcp` next to it, then run `<tmp>/maho mcp`. Because the CLI
// exec-replaces itself (on Unix), the stub's stdout becomes the process stdout.

#![cfg(unix)]

use std::os::unix::fs::PermissionsExt;

#[test]
fn mcp_exec_replaces_with_sibling_binary() {
    let real_bin = env!("CARGO_BIN_EXE_maho");
    let dir = tempfile::tempdir().unwrap();

    // Copy the real maho binary into the temp dir so current_exe() resolves to
    // a directory we control.
    let maho_copy = dir.path().join("maho");
    std::fs::copy(real_bin, &maho_copy).unwrap();
    let mut perms = std::fs::metadata(&maho_copy).unwrap().permissions();
    perms.set_mode(0o755);
    std::fs::set_permissions(&maho_copy, perms).unwrap();

    // Drop a stub `maho-browser-mcp` sibling that prints a unique marker.
    let stub = dir.path().join("maho-browser-mcp");
    std::fs::write(&stub, "#!/bin/sh\necho MAHO_MCP_STUB_STARTED\nexit 0\n").unwrap();
    let mut sperms = std::fs::metadata(&stub).unwrap().permissions();
    sperms.set_mode(0o755);
    std::fs::set_permissions(&stub, sperms).unwrap();

    let output = std::process::Command::new(&maho_copy)
        .arg("mcp")
        .output()
        .expect("failed to run copied maho binary");

    let stdout = String::from_utf8_lossy(&output.stdout);
    assert!(
        stdout.contains("MAHO_MCP_STUB_STARTED"),
        "expected stub to be exec'd; stdout={stdout}, stderr={}",
        String::from_utf8_lossy(&output.stderr)
    );
    assert!(output.status.success(), "expected exec'd stub to exit 0");
}

#[test]
fn mcp_missing_sibling_errors_clearly() {
    let real_bin = env!("CARGO_BIN_EXE_maho");
    let dir = tempfile::tempdir().unwrap();
    let maho_copy = dir.path().join("maho");
    std::fs::copy(real_bin, &maho_copy).unwrap();
    let mut perms = std::fs::metadata(&maho_copy).unwrap().permissions();
    perms.set_mode(0o755);
    std::fs::set_permissions(&maho_copy, perms).unwrap();

    // No sibling maho-browser-mcp present.
    let output = std::process::Command::new(&maho_copy)
        .arg("mcp")
        .output()
        .expect("failed to run copied maho binary");

    assert!(
        !output.status.success(),
        "expected nonzero exit when sibling missing"
    );
    let stderr = String::from_utf8_lossy(&output.stderr);
    assert!(
        stderr.contains("maho-browser-mcp"),
        "expected error mentioning the missing binary, got: {stderr}"
    );
}
