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

    let config = match cbindgen::Config::from_file(crate_dir.join("cbindgen.toml")) {
        Ok(cfg) => cfg,
        Err(err) => {
            println!("cargo:warning=cbindgen config load failed: {err}");
            return;
        }
    };

    let builder = cbindgen::Builder::new()
        .with_crate(&crate_dir)
        .with_config(config);

    match builder.generate() {
        Ok(bindings) => {
            let out_dir = crate_dir.join("generated");
            if let Err(err) = std::fs::create_dir_all(&out_dir) {
                println!("cargo:warning=failed to create generated dir: {err}");
                return;
            }
            bindings.write_to_file(out_dir.join("maho_mail_ffi.h"));
        }
        Err(err) => {
            // Do not fail the build if header generation hiccups; the static
            // lib is the primary artifact. Surface a warning instead.
            println!("cargo:warning=cbindgen header generation failed: {err}");
        }
    }
}
