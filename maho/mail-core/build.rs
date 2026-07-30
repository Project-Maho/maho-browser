// Copyright 2026 Maho Browser. All rights reserved.
//
// Generates the C ABI header (maho_mail_ffi.h) from the `extern "C"` surface
// via cbindgen, mirroring the maho_ffi.h convention used by the sibling
// maho-ffi crate. The header is written into `generated/` so it can be copied
// into maho-chromium/third_party/maho/.

use std::path::PathBuf;

fn main() {
    println!("cargo:rerun-if-changed=src/ffi.rs");
    println!("cargo:rerun-if-changed=cbindgen.toml");
    println!("cargo:rerun-if-changed=build.rs");

    let crate_dir = match std::env::var("CARGO_MANIFEST_DIR") {
        Ok(dir) => PathBuf::from(dir),
        Err(_) => return,
    };

    bake_oauth_client_ids();

    let config = match cbindgen::Config::from_file(crate_dir.join("cbindgen.toml")) {
        Ok(cfg) => cfg,
        Err(err) => {
            panic!("cbindgen config load failed: {err}");
        }
    };

    let builder = cbindgen::Builder::new()
        .with_crate(&crate_dir)
        .with_config(config);

    match builder.generate() {
        Ok(bindings) => {
            let out_dir = crate_dir.join("generated");
            if let Err(err) = std::fs::create_dir_all(&out_dir) {
                panic!("failed to create generated dir: {err}");
            }
            bindings.write_to_file(out_dir.join("maho_mail_ffi.h"));
        }
        Err(err) => {
            // Fail the build: a stale header combined with a changed Rust ABI is
            // silent UB (argument count/type mismatch) at the C++ FFI boundary.
            panic!("cbindgen header generation failed: {err}");
        }
    }
}

/// Bake the OAuth client identifiers into the binary via `option_env!`, read
/// from the build environment (CI/secret store) rather than a checked-in file,
/// so the build is reproducible without the discarded `mail/` tree. Absent
/// variables leave the corresponding `option_env!(...)` as `None`.
///
/// Client SECRETS are deliberately NOT baked: a compiled-in secret is trivially
/// extractable via `strings` and would let a third party impersonate Maho's
/// OAuth consent screen. Sign-in uses PKCE (public/desktop client) instead.
fn bake_oauth_client_ids() {
    for key in ["GMAIL_CLIENT_ID", "OUTLOOK_CLIENT_ID"] {
        println!("cargo:rerun-if-env-changed={key}");
        if let Ok(value) = std::env::var(key) {
            println!("cargo:rustc-env={key}={value}");
        }
    }
}
