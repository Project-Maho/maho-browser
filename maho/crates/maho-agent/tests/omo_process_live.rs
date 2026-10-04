#![cfg(unix)]

use std::path::PathBuf;

use maho_agent::omo::config::OmoLaunchConfig;
use maho_agent::omo::process::OmoProcess;

fn rpc_entry() -> PathBuf {
    PathBuf::from(
        "/Users/indo/.bun/install/global/node_modules/@code-yeongyu/senpi/dist/rpc-entry.js",
    )
}

#[tokio::test(flavor = "multi_thread")]
async fn spawns_a_real_omo_runtime_and_reports_ready() {
    if !rpc_entry().exists() {
        eprintln!("skipping: omo runtime not installed at {:?}", rpc_entry());
        return;
    }

    let scratch = std::env::temp_dir().join(format!("maho-omo-live-{}", std::process::id()));
    let socket = scratch.join("rpc.sock");
    std::fs::create_dir_all(&scratch).expect("scratch dir");

    let config = OmoLaunchConfig::builder(scratch.clone())
        .runtime_binary("bun")
        .rpc_entry(rpc_entry())
        .socket_path(socket.clone())
        .build()
        .expect("config builds");

    let mut proc = OmoProcess::spawn(
        config.runtime_binary(),
        config.rpc_entry(),
        config.socket_path(),
        &config,
    )
    .await
    .expect("omo runtime reports readiness on stderr");

    assert!(proc.id().is_some(), "a live runtime must expose a pid");

    let meta = std::fs::metadata(&socket).expect("the bound socket must exist on disk");
    assert!(
        !meta.is_file(),
        "the bound path must be a socket, not a regular file"
    );

    proc.shutdown().await.expect("runtime shuts down");
    let _ = std::fs::remove_dir_all(&scratch);
}
