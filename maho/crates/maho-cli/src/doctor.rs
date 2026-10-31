use anyhow::Result;
use serde::Serialize;
use std::path::{Path, PathBuf};

use maho_browser_mcp::protocol::ControllerKind;

use crate::browser;

#[derive(Debug, Serialize)]
pub struct DoctorReport {
    pub executable: ExecutableReport,
    pub bundle_helper: HelperReport,
    pub socket: SocketReport,
    pub probe: ProbeReport,
    pub fixes: Vec<FixRecord>,
    pub healthy: bool,
    pub summary: String,
}

#[derive(Debug, Serialize)]
pub struct ExecutableReport {
    pub resolved_path: String,
    pub class: String,
}

#[derive(Debug, Serialize)]
pub struct HelperReport {
    pub candidates: Vec<String>,
    pub existing: Option<String>,
}

#[derive(Debug, Serialize)]
pub struct SocketCandidateReport {
    pub path: String,
    pub exists: bool,
}

#[derive(Debug, Serialize)]
pub struct SocketReport {
    pub env_override: Option<String>,
    pub default_resolved: String,
    pub candidates: Vec<SocketCandidateReport>,
}

#[derive(Debug, Serialize)]
pub struct ProbeReport {
    pub connected: bool,
    pub controller_kind: Option<String>,
    pub trusted: Option<bool>,
    pub error: Option<String>,
}

#[derive(Debug, Serialize)]
pub struct FixRecord {
    pub action: String,
    pub detail: String,
    pub applied: bool,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum ExecutableClass {
    AppBundleHelper,
    CargoTarget,
    CargoBin,
    SystemPath,
    Other,
}

impl ExecutableClass {
    pub const fn as_str(self) -> &'static str {
        match self {
            Self::AppBundleHelper => "app_bundle_helper",
            Self::CargoTarget => "cargo_target",
            Self::CargoBin => "cargo_bin",
            Self::SystemPath => "system_path",
            Self::Other => "other",
        }
    }
}

pub fn classify_executable(path: &Path) -> ExecutableClass {
    let normalized = path.to_string_lossy().to_lowercase().replace('\\', "/");
    if normalized.contains(".app/contents/helpers/") {
        ExecutableClass::AppBundleHelper
    } else if normalized.contains("/target/debug/") || normalized.contains("/target/release/") {
        ExecutableClass::CargoTarget
    } else if normalized.contains("/.cargo/bin/") {
        ExecutableClass::CargoBin
    } else if normalized.contains("/usr/local/bin/")
        || normalized.contains("/usr/bin/")
        || normalized.contains("/opt/homebrew/bin/")
        || normalized.contains("/opt/maho/")
    {
        ExecutableClass::SystemPath
    } else {
        ExecutableClass::Other
    }
}

fn controller_wire_name(kind: &ControllerKind) -> &'static str {
    match kind {
        ControllerKind::MahoCli => "maho-cli",
        ControllerKind::MahoCliRepl => "maho-cli-repl",
        ControllerKind::MahoBrowserMcp => "maho-browser-mcp",
        ControllerKind::ThirdParty => "third-party",
    }
}

#[cfg(target_os = "macos")]
pub fn bundle_helper_candidates() -> Vec<PathBuf> {
    let home = std::env::var("HOME").unwrap_or_else(|_| "/tmp".to_string());
    vec![
        PathBuf::from("/Applications/Maho.app/Contents/Helpers/maho"),
        PathBuf::from(home).join("Applications/Maho.app/Contents/Helpers/maho"),
    ]
}

#[cfg(not(target_os = "macos"))]
pub fn bundle_helper_candidates() -> Vec<PathBuf> {
    vec![
        PathBuf::from("/usr/bin/maho"),
        PathBuf::from("/opt/maho/bin/maho"),
    ]
}

#[cfg(target_os = "macos")]
fn socket_display_candidates() -> Vec<PathBuf> {
    maho_browser_mcp::paths::socket_path_candidates()
}

#[cfg(not(target_os = "macos"))]
fn socket_display_candidates() -> Vec<PathBuf> {
    vec![maho_browser_mcp::paths::default_socket_path()]
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum RepointAction {
    AlreadyCurrent,
    Repoint,
    RealBinaryRefuse,
    MissingEntry,
}

pub fn plan_cargo_bin_repoint(cargo_bin: &Path, helper: &Path) -> RepointAction {
    match std::fs::symlink_metadata(cargo_bin) {
        Ok(md) if md.file_type().is_symlink() => match std::fs::read_link(cargo_bin) {
            Ok(target) if target == helper => RepointAction::AlreadyCurrent,
            Ok(_) => RepointAction::Repoint,
            Err(_) => RepointAction::Repoint,
        },
        Ok(_) => RepointAction::RealBinaryRefuse,
        Err(_) => RepointAction::MissingEntry,
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum BridgeAction {
    AlreadyBridged,
    BridgedElsewhere(PathBuf),
    BrandedLive,
    CreateBridge,
    NoSocket,
}

pub fn plan_socket_bridge(app_support: &Path) -> BridgeAction {
    let branded = app_support.join("Maho").join("maho.sock");
    let unbranded = app_support.join("Chromium").join("maho.sock");
    match std::fs::symlink_metadata(&branded) {
        Ok(md) if md.file_type().is_symlink() => {
            let target = std::fs::read_link(&branded).unwrap_or_default();
            if target == unbranded {
                BridgeAction::AlreadyBridged
            } else {
                BridgeAction::BridgedElsewhere(target)
            }
        }
        Ok(_) => BridgeAction::BrandedLive,
        Err(_) => {
            if unbranded.exists() {
                BridgeAction::CreateBridge
            } else {
                BridgeAction::NoSocket
            }
        }
    }
}

#[cfg(target_os = "macos")]
fn apply_fixes(report: &mut DoctorReport) {
    if report.probe.trusted != Some(true) {
        if let Some(helper_str) = &report.bundle_helper.existing {
            let helper = Path::new(helper_str);
            let home = std::env::var("HOME").unwrap_or_else(|_| "/tmp".to_string());
            let cargo_bin = PathBuf::from(home).join(".cargo/bin/maho");
            match plan_cargo_bin_repoint(&cargo_bin, helper) {
                RepointAction::AlreadyCurrent => {
                    report.fixes.push(FixRecord {
                        action: "repoint-cargo-bin".into(),
                        detail: format!(
                            "{} already links to {}",
                            cargo_bin.display(),
                            helper.display()
                        ),
                        applied: false,
                    });
                }
                RepointAction::Repoint => {
                    let remove_res = std::fs::remove_file(&cargo_bin);
                    #[cfg(unix)]
                    let symlink_res =
                        remove_res.and_then(|_| std::os::unix::fs::symlink(helper, &cargo_bin));
                    #[cfg(not(unix))]
                    let symlink_res: std::io::Result<()> = Ok(());

                    match symlink_res {
                        Ok(_) => {
                            report.fixes.push(FixRecord {
                                action: "repoint-cargo-bin".into(),
                                detail: format!(
                                    "re-pointed {} -> {}",
                                    cargo_bin.display(),
                                    helper.display()
                                ),
                                applied: true,
                            });
                        }
                        Err(e) => {
                            report.fixes.push(FixRecord {
                                action: "repoint-cargo-bin".into(),
                                detail: format!("failed to re-point {}: {e}", cargo_bin.display()),
                                applied: false,
                            });
                        }
                    }
                }
                RepointAction::RealBinaryRefuse => {
                    report.fixes.push(FixRecord {
                        action: "repoint-cargo-bin".into(),
                        detail: format!(
                            "{} is a real binary from `cargo install`, not a symlink; replace manually: ln -sfn '{}' '{}'",
                            cargo_bin.display(),
                            helper.display(),
                            cargo_bin.display()
                        ),
                        applied: false,
                    });
                }
                RepointAction::MissingEntry => {
                    report.fixes.push(FixRecord {
                        action: "repoint-cargo-bin".into(),
                        detail: format!("no existing {} entry to re-point", cargo_bin.display()),
                        applied: false,
                    });
                }
            }
        }
    }

    let home = std::env::var("HOME").unwrap_or_else(|_| "/tmp".to_string());
    let app_support = PathBuf::from(home).join("Library/Application Support");
    match plan_socket_bridge(&app_support) {
        BridgeAction::AlreadyBridged => {
            report.fixes.push(FixRecord {
                action: "bridge-socket".into(),
                detail: "Maho/maho.sock -> Chromium/maho.sock symlink already active".into(),
                applied: false,
            });
        }
        BridgeAction::BridgedElsewhere(target) => {
            report.fixes.push(FixRecord {
                action: "bridge-socket".into(),
                detail: format!(
                    "Maho/maho.sock links to {}; leaving untouched",
                    target.display()
                ),
                applied: false,
            });
        }
        BridgeAction::BrandedLive => {
            report.fixes.push(FixRecord {
                action: "bridge-socket".into(),
                detail: "Maho/maho.sock exists as a real socket/dir (browser binds branded dir); no bridge needed".into(),
                applied: false,
            });
        }
        BridgeAction::CreateBridge => {
            let branded = app_support.join("Maho").join("maho.sock");
            let unbranded = app_support.join("Chromium").join("maho.sock");
            let _ = std::fs::create_dir_all(app_support.join("Maho"));
            #[cfg(unix)]
            let res = std::os::unix::fs::symlink(&unbranded, &branded);
            #[cfg(not(unix))]
            let res: std::io::Result<()> = Ok(());

            match res {
                Ok(_) => {
                    report.fixes.push(FixRecord {
                        action: "bridge-socket".into(),
                        detail: format!(
                            "created symlink {} -> {}",
                            branded.display(),
                            unbranded.display()
                        ),
                        applied: true,
                    });
                }
                Err(e) => {
                    report.fixes.push(FixRecord {
                        action: "bridge-socket".into(),
                        detail: format!("failed to create symlink {}: {e}", branded.display()),
                        applied: false,
                    });
                }
            }
        }
        BridgeAction::NoSocket => {
            report.fixes.push(FixRecord {
                action: "bridge-socket".into(),
                detail: "no live Chromium/maho.sock found; browser not running".into(),
                applied: false,
            });
        }
    }
}

#[cfg(not(target_os = "macos"))]
fn apply_fixes(report: &mut DoctorReport) {
    report.fixes.push(FixRecord {
        action: "fix-unavailable".into(),
        detail: "--fix is implemented for macOS; this platform needs manual setup".into(),
        applied: false,
    });
}

async fn probe(override_path: Option<&str>) -> ProbeReport {
    match browser::connect(override_path).await {
        Ok(client) => {
            let kind = client.session_info().await.map(|info| info.controller_kind);
            let trusted = kind
                .as_ref()
                .map(|k| !matches!(k, ControllerKind::ThirdParty));
            let name = kind.as_ref().map(controller_wire_name).map(str::to_string);
            ProbeReport {
                connected: true,
                controller_kind: name,
                trusted,
                error: None,
            }
        }
        Err(e) => ProbeReport {
            connected: false,
            controller_kind: None,
            trusted: None,
            error: Some(e.to_string()),
        },
    }
}

fn summarize(report: &DoctorReport) -> String {
    if report.healthy {
        "connected to the running Maho browser as a TRUSTED controller (maho-cli)".to_string()
    } else if !report.probe.connected {
        format!(
            "browser not reachable ({})",
            report.probe.error.as_deref().unwrap_or("unknown error")
        )
    } else {
        let kind = report.probe.controller_kind.as_deref().unwrap_or("unknown");
        format!(
            "connected but UNTRUSTED ({kind}): this binary is not the running Maho.app bundle helper — mutations/control calls will be refused (-32003/-32007). Run `maho doctor --fix`."
        )
    }
}

async fn gather(override_path: Option<&str>) -> DoctorReport {
    let exe = std::env::current_exe().unwrap_or_else(|_| PathBuf::from("<unresolved>"));
    let executable = ExecutableReport {
        resolved_path: exe.display().to_string(),
        class: classify_executable(&exe).as_str().to_string(),
    };

    let helper_candidates = bundle_helper_candidates();
    let existing_helper = helper_candidates
        .iter()
        .find(|p| p.exists())
        .map(|p| p.display().to_string());
    let bundle_helper = HelperReport {
        candidates: helper_candidates
            .iter()
            .map(|p| p.display().to_string())
            .collect(),
        existing: existing_helper,
    };

    let env_override = std::env::var(maho_browser_mcp::paths::ENV_SOCKET_PATH)
        .ok()
        .filter(|p| !p.is_empty());
    let candidates = socket_display_candidates()
        .into_iter()
        .map(|p| SocketCandidateReport {
            exists: p.exists(),
            path: p.display().to_string(),
        })
        .collect();
    let default_resolved = override_path.map(|p| p.to_string()).unwrap_or_else(|| {
        maho_browser_mcp::paths::default_socket_path()
            .display()
            .to_string()
    });
    let socket = SocketReport {
        env_override,
        default_resolved,
        candidates,
    };

    let probe_report = probe(override_path).await;
    let healthy = probe_report.connected && probe_report.trusted == Some(true);

    let mut report = DoctorReport {
        executable,
        bundle_helper,
        socket,
        probe: probe_report,
        fixes: Vec::new(),
        healthy,
        summary: String::new(),
    };
    report.summary = summarize(&report);
    report
}

fn print_human(r: &DoctorReport) {
    println!("maho doctor");
    println!(
        "  executable      : {} ({})",
        r.executable.resolved_path, r.executable.class
    );
    let helper_status = r
        .bundle_helper
        .existing
        .as_deref()
        .map(|p| format!("found ({p})"))
        .unwrap_or_else(|| "not found".to_string());
    println!("  bundle helper   : {helper_status}");

    let env_note = r
        .socket
        .env_override
        .as_deref()
        .map(|e| format!(" [via MAHO_MCP_SOCKET_PATH={e}]"))
        .unwrap_or_default();
    println!(
        "  socket target   : {}{}",
        r.socket.default_resolved, env_note
    );
    for c in &r.socket.candidates {
        let mark = if c.exists { "✓" } else { "✗" };
        println!("    [{mark}] {}", c.path);
    }

    if r.probe.connected {
        let trust_mark = match r.probe.trusted {
            Some(true) => "✓ TRUSTED",
            Some(false) => "✗ UNTRUSTED",
            None => "? UNKNOWN",
        };
        let kind = r.probe.controller_kind.as_deref().unwrap_or("unknown");
        println!("  live probe      : connected ({kind}, {trust_mark})");
    } else {
        let err = r.probe.error.as_deref().unwrap_or("unknown error");
        println!("  live probe      : ✗ disconnected ({err})");
    }

    for f in &r.fixes {
        let mark = if f.applied { "✓" } else { "—" };
        println!("  fix [{mark}] {} : {}", f.action, f.detail);
    }

    let verdict = if r.healthy { "HEALTHY" } else { "PROBLEM" };
    println!("  verdict         : {verdict}");
    println!("  summary         : {}", r.summary);
}

pub async fn run(fix: bool, override_path: Option<&str>, json_output: bool) -> Result<()> {
    let mut report = gather(override_path).await;
    if fix {
        apply_fixes(&mut report);
        report.probe = probe(override_path).await;
        report.healthy = report.probe.connected && report.probe.trusted == Some(true);
        report.summary = summarize(&report);
        if report.fixes.iter().any(|f| f.applied) {
            report.fixes.push(FixRecord {
                action: "note".into(),
                detail: "fixes applied; re-run `maho doctor` from your PATH entry to verify".into(),
                applied: false,
            });
        }
    }

    if json_output {
        println!("{}", serde_json::to_string_pretty(&report)?);
    } else {
        print_human(&report);
    }

    if !report.healthy {
        use std::io::Write;
        let _ = std::io::stdout().flush();
        std::process::exit(1);
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_classify_executable() {
        assert_eq!(
            classify_executable(Path::new("/Applications/Maho.app/Contents/Helpers/maho")),
            ExecutableClass::AppBundleHelper
        );
        assert_eq!(
            classify_executable(Path::new("/Users/indo/code/maho/target/debug/maho")),
            ExecutableClass::CargoTarget
        );
        assert_eq!(
            classify_executable(Path::new("/Users/indo/code/maho/target/release/maho")),
            ExecutableClass::CargoTarget
        );
        assert_eq!(
            classify_executable(Path::new("/Users/indo/.cargo/bin/maho")),
            ExecutableClass::CargoBin
        );
        assert_eq!(
            classify_executable(Path::new("/usr/local/bin/maho")),
            ExecutableClass::SystemPath
        );
        assert_eq!(
            classify_executable(Path::new("/tmp/some_custom_tool")),
            ExecutableClass::Other
        );
    }

    #[cfg(unix)]
    #[test]
    fn test_plan_cargo_bin_repoint() {
        let temp_dir =
            std::env::temp_dir().join(format!("maho_doctor_test_{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&temp_dir);
        std::fs::create_dir_all(&temp_dir).unwrap();

        let helper_a = temp_dir.join("helper_a");
        let helper_b = temp_dir.join("helper_b");
        std::fs::write(&helper_a, b"").unwrap();
        std::fs::write(&helper_b, b"").unwrap();

        let link = temp_dir.join("maho_symlink");

        assert_eq!(
            plan_cargo_bin_repoint(&link, &helper_a),
            RepointAction::MissingEntry
        );

        let real_file = temp_dir.join("real_bin");
        std::fs::write(&real_file, b"").unwrap();
        assert_eq!(
            plan_cargo_bin_repoint(&real_file, &helper_a),
            RepointAction::RealBinaryRefuse
        );

        std::os::unix::fs::symlink(&helper_a, &link).unwrap();
        assert_eq!(
            plan_cargo_bin_repoint(&link, &helper_b),
            RepointAction::Repoint
        );

        assert_eq!(
            plan_cargo_bin_repoint(&link, &helper_a),
            RepointAction::AlreadyCurrent
        );

        let _ = std::fs::remove_dir_all(&temp_dir);
    }

    #[cfg(unix)]
    #[test]
    fn test_plan_socket_bridge() {
        let temp_dir =
            std::env::temp_dir().join(format!("maho_bridge_test_{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&temp_dir);
        std::fs::create_dir_all(&temp_dir).unwrap();

        let maho_dir = temp_dir.join("Maho");
        let chromium_dir = temp_dir.join("Chromium");
        std::fs::create_dir_all(&maho_dir).unwrap();
        std::fs::create_dir_all(&chromium_dir).unwrap();

        let branded = maho_dir.join("maho.sock");
        let unbranded = chromium_dir.join("maho.sock");

        assert_eq!(plan_socket_bridge(&temp_dir), BridgeAction::NoSocket);

        std::fs::write(&unbranded, b"").unwrap();
        assert_eq!(plan_socket_bridge(&temp_dir), BridgeAction::CreateBridge);

        std::os::unix::fs::symlink(&unbranded, &branded).unwrap();
        assert_eq!(plan_socket_bridge(&temp_dir), BridgeAction::AlreadyBridged);

        std::fs::remove_file(&branded).unwrap();
        std::fs::write(&branded, b"").unwrap();
        assert_eq!(plan_socket_bridge(&temp_dir), BridgeAction::BrandedLive);

        let _ = std::fs::remove_dir_all(&temp_dir);
    }
}
