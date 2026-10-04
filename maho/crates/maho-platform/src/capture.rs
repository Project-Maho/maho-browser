//! Screen capture capability (`maho desktop capture`).

use std::path::PathBuf;

use crate::error::PlatformError;
use crate::grounding::CaptureInfo;

pub const DEEP_LINK_SCREEN_CAPTURE: &str =
    "x-apple.systempreferences:com.apple.settings.PrivacySecurity.extension?Privacy_ScreenCapture";

/// Non-prompting preflight check for Screen Recording TCC permission.
///
/// Returns `true` if screen recording access is granted, `false` otherwise.
pub fn preflight_screen_capture() -> bool {
    #[cfg(target_os = "macos")]
    {
        // SAFETY: stateless framework query; does not prompt.
        unsafe { CGPreflightScreenCaptureAccess() }
    }
    #[cfg(not(target_os = "macos"))]
    {
        false
    }
}

#[cfg(target_os = "macos")]
#[link(name = "CoreGraphics", kind = "framework")]
extern "C" {
    fn CGPreflightScreenCaptureAccess() -> bool;
}

pub(crate) fn capture_target_path(output: Option<PathBuf>) -> Result<PathBuf, PlatformError> {
    use std::sync::atomic::{AtomicU64, Ordering};
    static NEXT_CAPTURE: AtomicU64 = AtomicU64::new(0);
    if let Some(path) = output {
        return Ok(path);
    }
    loop {
        let id = NEXT_CAPTURE.fetch_add(1, Ordering::Relaxed);
        let path =
            std::env::temp_dir().join(format!("maho-capture-{}-{id}.png", std::process::id()));
        let mut options = std::fs::OpenOptions::new();
        options.write(true).create_new(true);
        #[cfg(unix)]
        {
            use std::os::unix::fs::OpenOptionsExt;
            options.mode(0o600);
        }
        match options.open(&path) {
            Ok(_) => return Ok(path),
            Err(error) if error.kind() == std::io::ErrorKind::AlreadyExists => continue,
            Err(error) => return Err(PlatformError::Io(error.to_string())),
        }
    }
}

/// Capture the primary screen or active display to an image file.
///
/// If `output` is `None`, writes to a temporary file in `$TMPDIR`.
/// Returns the path and pixel dimensions of the captured image.
pub fn capture_screen(output: Option<PathBuf>) -> Result<CaptureInfo, PlatformError> {
    #[cfg(target_os = "macos")]
    {
        if !preflight_screen_capture() {
            return Err(PlatformError::PermissionRequired {
                scope: "screen-recording".to_string(),
                deep_link: DEEP_LINK_SCREEN_CAPTURE.to_string(),
            });
        }

        let target_path = capture_target_path(output)?;

        let result = std::process::Command::new("/usr/sbin/screencapture")
            .args(["-x", "-D", "1"])
            .arg(&target_path)
            .output()
            .map_err(|e| PlatformError::Io(format!("failed to execute screencapture: {e}")))?;

        if !result.status.success() {
            let stderr = String::from_utf8_lossy(&result.stderr);
            return Err(PlatformError::Io(format!(
                "screencapture failed with status {}: {}",
                result.status,
                stderr.trim()
            )));
        }

        let (width, height) = crate::grounding::png_dimensions(&target_path)?;

        Ok(CaptureInfo {
            path: target_path.to_string_lossy().into_owned(),
            width,
            height,
        })
    }
    #[cfg(target_os = "linux")]
    {
        linux::capture_screen(output)
    }
    #[cfg(target_os = "windows")]
    {
        crate::win_capture::capture_screen(output)
    }
}

#[cfg(target_os = "linux")]
mod linux {
    use super::*;
    use crate::probe::{CommandRunner, SystemRunner};

    pub(super) fn capture_screen(output: Option<PathBuf>) -> Result<CaptureInfo, PlatformError> {
        let runner = SystemRunner;
        let chain =
            crate::backends::probe_chain("linux", crate::backends::GroundingCapability::Capture);
        let target_path = capture_target_path(output)?;
        let mut attempted: Vec<&str> = Vec::new();
        for tool in chain {
            let Some(program) = runner.which(tool) else {
                continue;
            };
            let args = crate::linux_probe::capture_args(tool, &target_path);
            let arg_refs: Vec<&str> = args.iter().map(|s| s.as_str()).collect();
            attempted.push(tool);
            if let Ok(out) = runner.run(&program.to_string_lossy(), &arg_refs) {
                if out.status == 0 && std::path::Path::new(&target_path).is_file() {
                    let (width, height) = crate::grounding::png_dimensions(&target_path)?;
                    return Ok(CaptureInfo {
                        path: target_path.to_string_lossy().into_owned(),
                        width,
                        height,
                    });
                }
            }
        }
        let remediation = if attempted.is_empty() {
            "install one of: grim (wlroots), gnome-screenshot (GNOME), spectacle (KDE), scrot or import (X11)".to_string()
        } else {
            format!("capture tools failed: {}", attempted.join(", "))
        };
        Err(PlatformError::Unsupported {
            platform: "linux".to_string(),
            capability: "desktop.capture".to_string(),
            remediation,
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn default_capture_paths_are_reserved_and_distinct() {
        let first = capture_target_path(None).unwrap();
        std::fs::write(&first, b"first image").unwrap();
        let second = capture_target_path(None).unwrap();
        std::fs::write(&second, b"second image").unwrap();
        assert_eq!(std::fs::read(&first).unwrap(), b"first image");
        std::fs::remove_file(first).unwrap();
        std::fs::remove_file(second).unwrap();
    }

    #[test]
    fn test_preflight_screen_capture_smoke() {
        let granted = preflight_screen_capture();
        println!("preflight_screen_capture result: {granted}");
    }

    #[test]
    fn test_capture_screen_integration() {
        let granted = preflight_screen_capture();
        let result = capture_screen(None);

        if granted {
            let info = result.expect("capture_screen should succeed when preflight is true");
            assert!(info.width > 0, "width should be greater than 0");
            assert!(info.height > 0, "height should be greater than 0");
            assert!(
                std::path::Path::new(&info.path).exists(),
                "captured file should exist at {}",
                info.path
            );
            // Clean up temporary capture file created by the test
            let _ = std::fs::remove_file(&info.path);
        } else {
            match result {
                Err(PlatformError::PermissionRequired { scope, deep_link }) => {
                    assert_eq!(scope, "screen-recording");
                    assert_eq!(deep_link, DEEP_LINK_SCREEN_CAPTURE);
                }
                other => panic!("expected PermissionRequired when not granted, got {other:?}"),
            }
        }
    }

    #[test]
    fn test_capture_screen_with_custom_output_path() {
        let granted = preflight_screen_capture();
        let temp_file = std::env::temp_dir().join(format!(
            "maho-test-custom-capture-{}.png",
            std::process::id()
        ));
        let result = capture_screen(Some(temp_file.clone()));

        if granted {
            let info = result.expect("capture_screen with custom path should succeed");
            assert_eq!(info.path, temp_file.to_string_lossy());
            assert!(info.width > 0);
            assert!(info.height > 0);
            assert!(temp_file.exists());
            let _ = std::fs::remove_file(&temp_file);
        } else {
            match result {
                Err(PlatformError::PermissionRequired { scope, deep_link }) => {
                    assert_eq!(scope, "screen-recording");
                    assert_eq!(deep_link, DEEP_LINK_SCREEN_CAPTURE);
                }
                other => panic!("expected PermissionRequired when not granted, got {other:?}"),
            }
        }
    }

    #[test]
    fn test_capture_screen_error_paths() {
        let granted = preflight_screen_capture();
        if granted {
            // When preflight passes, providing an impossible path returns an Io error
            let invalid_path = PathBuf::from("/nonexistent_dir_12345/impossible_output.png");
            let result = capture_screen(Some(invalid_path));
            assert!(
                matches!(result, Err(PlatformError::Io(_))),
                "expected Io error for invalid path, got {result:?}"
            );
        } else {
            // When preflight fails, even with an invalid path it returns PermissionRequired first
            let invalid_path = PathBuf::from("/nonexistent_dir_12345/impossible_output.png");
            let result = capture_screen(Some(invalid_path));
            match result {
                Err(PlatformError::PermissionRequired { scope, deep_link }) => {
                    assert_eq!(scope, "screen-recording");
                    assert_eq!(deep_link, DEEP_LINK_SCREEN_CAPTURE);
                }
                other => panic!("expected PermissionRequired, got {other:?}"),
            }
        }
    }
}
