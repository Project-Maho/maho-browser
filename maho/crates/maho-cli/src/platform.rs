use anyhow::Result;
use maho_platform::error::PlatformError;
use maho_platform::send::{OsRunner, SendRequest};
use serde::Serialize;
use serde_json::json;

use crate::{ContactsCommands, DesktopCommands, DiskCommands, ImessageCommands};

pub fn error_to_json(err: &PlatformError) -> serde_json::Value {
    match err {
        PlatformError::Unimplemented { capability } => {
            json!({
                "code": err.code(),
                "capability": capability,
                "message": err.to_string(),
            })
        }
        PlatformError::PermissionRequired { scope, deep_link } => {
            json!({
                "code": err.code(),
                "scope": scope,
                "deep_link": deep_link,
                "message": err.to_string(),
            })
        }
        PlatformError::Unsupported {
            platform,
            capability,
            remediation,
        } => {
            json!({
                "code": err.code(),
                "platform": platform,
                "capability": capability,
                "remediation": remediation,
                "message": err.to_string(),
            })
        }
        PlatformError::ApprovalRequired { action } => {
            json!({
                "code": err.code(),
                "action": action,
                "message": err.to_string(),
            })
        }
        PlatformError::Io(msg) => {
            json!({
                "code": err.code(),
                "message": msg,
            })
        }
        PlatformError::Sqlite(msg) => {
            json!({
                "code": err.code(),
                "message": msg,
            })
        }
    }
}

/// Runs `desktop ocr`. With no `--output`, the live screen is captured into a
/// temp file that is deleted after OCR (success or failure) and never reported:
/// the redacted payload must not leave a full screenshot behind on disk. An
/// explicit `--output` image is caller-owned, kept, and echoed back.
fn ocr_payload_with(
    output: Option<std::path::PathBuf>,
    full: bool,
    capture: impl FnOnce() -> Result<maho_platform::grounding::CaptureInfo, PlatformError>,
    ocr: impl FnOnce(
        &std::path::Path,
    ) -> Result<Vec<maho_platform::grounding::TextBlock>, PlatformError>,
) -> Result<serde_json::Value, PlatformError> {
    let Some(image_path) = output else {
        let capture_path = std::path::PathBuf::from(capture()?.path);
        let result = ocr(&capture_path);
        std::fs::remove_file(&capture_path).map_err(|error| {
            PlatformError::Io(format!(
                "failed to remove screen capture: {error}; OCR error: {:?}",
                result.as_ref().err()
            ))
        })?;
        let blocks = result?;
        // Privacy contract: live-screen OCR exposes coordinates only unless
        // --full is passed.
        return Ok(if full {
            json!({ "text_redacted": false, "blocks": blocks })
        } else {
            json!({
                "text_redacted": true,
                "blocks": blocks.iter().map(|b| json!({
                    "x": b.x, "y": b.y, "w": b.w, "h": b.h,
                })).collect::<Vec<_>>(),
            })
        });
    };
    let blocks = ocr(&image_path)?;
    Ok(json!({ "image": image_path, "text_redacted": false, "blocks": blocks }))
}

pub fn print_platform_result<T: Serialize>(res: Result<T, PlatformError>) -> Result<()> {
    match res {
        Ok(data) => {
            println!("{}", serde_json::to_string_pretty(&data)?);
        }
        Err(err) => {
            let json_val = error_to_json(&err);
            println!("{}", serde_json::to_string_pretty(&json_val)?);
        }
    }
    Ok(())
}

pub async fn cmd_contacts(command: ContactsCommands) -> Result<()> {
    match command {
        ContactsCommands::Search { query } => {
            let result = maho_platform::contacts::search(&query);
            print_platform_result(result)
        }
        ContactsCommands::Resolve { handles } => {
            let result = maho_platform::contacts::resolve(&handles);
            print_platform_result(result)
        }
    }
}

pub async fn cmd_imessage(command: ImessageCommands) -> Result<()> {
    match command {
        ImessageCommands::Chats { db_path } => {
            let result = maho_platform::imessage::chats(db_path.as_deref());
            print_platform_result(result)
        }
        ImessageCommands::History {
            chat_guid,
            limit,
            db_path,
        } => {
            let result = maho_platform::imessage::history(&chat_guid, limit, db_path.as_deref());
            print_platform_result(result)
        }
        ImessageCommands::Search {
            query,
            sender,
            db_path,
        } => {
            let result = maho_platform::imessage::search(
                query.as_deref(),
                sender.as_deref(),
                db_path.as_deref(),
            );
            print_platform_result(result)
        }
        ImessageCommands::Send {
            to,
            text,
            chat_guid,
            dry_run,
            approve,
            db_path: _,
        } => {
            let req = SendRequest {
                to,
                chat_guid,
                text,
                approve,
                dry_run,
            };
            let runner = OsRunner;
            let result = maho_platform::send::send(req, &runner);
            print_platform_result(result)
        }
    }
}

pub async fn cmd_disk(command: DiskCommands) -> Result<()> {
    match command {
        DiskCommands::Status => {
            let result = maho_platform::disk::status();
            print_platform_result(result)
        }
    }
}

pub async fn cmd_desktop(command: DesktopCommands) -> Result<()> {
    use maho_platform::desktop::{DesktopAction, DesktopInputRequest, MouseButton};

    let (action, approve, dry_run) = match command {
        DesktopCommands::Status => {
            let trusted = maho_platform::desktop::accessibility_trusted();
            let platform = std::env::consts::OS.to_string();
            let value = json!({
                "platform": platform,
                "accessibility_trusted": trusted,
                "backend": "enigo",
            });
            println!("{}", serde_json::to_string_pretty(&value)?);
            return Ok(());
        }
        DesktopCommands::Capture { output } => {
            let result = maho_platform::capture::capture_screen(output);
            return print_platform_result(result);
        }
        DesktopCommands::Ocr {
            region,
            full,
            output,
        } => {
            let region_parsed = match region {
                Some(r) => match maho_platform::grounding::parse_region(&r) {
                    Ok(reg) => Some(reg),
                    Err(err) => return print_platform_result::<serde_json::Value>(Err(err)),
                },
                None => None,
            };
            let payload = ocr_payload_with(
                output,
                full,
                || maho_platform::capture::capture_screen(None),
                |path| maho_platform::ocr::ocr_image(path, region_parsed),
            );
            return print_platform_result(payload);
        }
        DesktopCommands::Elements { app, role, output } => {
            let app_target = match app {
                Some(ref s) if s.trim().eq_ignore_ascii_case("frontmost") => {
                    maho_platform::grounding::AppTarget::Frontmost
                }
                Some(ref s) => match s.trim().parse::<i32>() {
                    Ok(pid) => maho_platform::grounding::AppTarget::Pid(pid),
                    Err(_) => {
                        return print_platform_result::<serde_json::Value>(Err(PlatformError::Io(
                            format!(
                                "invalid app target '{s}': expected 'frontmost' or numeric PID"
                            ),
                        )));
                    }
                },
                None => maho_platform::grounding::AppTarget::Frontmost,
            };
            let elements = match maho_platform::ax_tree::read_elements(app_target) {
                Ok(elements) => elements,
                Err(err) => {
                    return print_platform_result::<Vec<maho_platform::grounding::ScreenElement>>(
                        Err(err),
                    );
                }
            };
            let selected = match &role {
                Some(r) => maho_platform::ax_tree::filter_by_role(&elements, r),
                None => elements,
            };
            if let Some(out) = &output {
                std::fs::write(out, serde_json::to_string_pretty(&selected)?)?;
            }
            println!("{}", serde_json::to_string_pretty(&selected)?);
            return Ok(());
        }
        DesktopCommands::FindText { text, image } => {
            let source = match image {
                Some(p) => maho_platform::grounding::FindSource::File(p),
                None => maho_platform::grounding::FindSource::Screen,
            };
            let result = maho_platform::find_text::find_text(&text, source);
            return print_platform_result(result);
        }
        DesktopCommands::Move {
            x,
            y,
            approve,
            dry_run,
        } => (DesktopAction::MoveMouse { x, y }, approve, dry_run),
        DesktopCommands::Click {
            button,
            approve,
            dry_run,
        } => {
            let button = MouseButton::parse(&button);
            let button = match button {
                Ok(b) => b,
                Err(err) => return print_platform_result::<serde_json::Value>(Err(err)),
            };
            (DesktopAction::Click { button }, approve, dry_run)
        }
        DesktopCommands::Scroll {
            dx,
            dy,
            approve,
            dry_run,
        } => (DesktopAction::Scroll { dx, dy }, approve, dry_run),
        DesktopCommands::Type {
            text,
            approve,
            dry_run,
        } => (DesktopAction::Type { text }, approve, dry_run),
        DesktopCommands::Hotkey {
            keys,
            approve,
            dry_run,
        } => (
            DesktopAction::Hotkey {
                keys: keys.split('+').map(str::to_string).collect(),
            },
            approve,
            dry_run,
        ),
    };
    let req = DesktopInputRequest {
        action,
        approve,
        dry_run,
    };
    print_platform_result(maho_platform::desktop::perform_input(req))
}

#[cfg(test)]
mod tests {
    use super::*;
    use maho_platform::grounding::{CaptureInfo, TextBlock};

    fn block() -> TextBlock {
        TextBlock { text: "secret".into(), x: 1, y: 2, w: 3, h: 4, confidence: 1.0 }
    }

    fn capture_into(path: &std::path::Path) -> Result<CaptureInfo, PlatformError> {
        std::fs::write(path, b"screen").unwrap();
        Ok(CaptureInfo { path: path.to_string_lossy().into_owned(), width: 1, height: 1 })
    }

    #[test]
    fn live_screen_ocr_deletes_capture_after_success_and_failure() {
        for fail in [false, true] {
            let path = std::env::temp_dir()
                .join(format!("maho-cli-ocr-cleanup-{}-{fail}.png", std::process::id()));
            let result = ocr_payload_with(None, false, || capture_into(&path), |_| {
                if fail { Err(PlatformError::Io("ocr failed".into())) } else { Ok(vec![block()]) }
            });
            let leaked = path.exists();
            let _ = std::fs::remove_file(&path);
            assert!(!leaked, "live-screen capture was left on disk (fail={fail})");
            assert_eq!(result.is_err(), fail);
            if let Ok(payload) = result {
                assert_eq!(payload["text_redacted"], true);
                assert!(payload.get("image").is_none());
                assert!(!payload.to_string().contains("secret"));
            }
        }
    }

    #[test]
    fn explicit_image_is_kept_and_reported() {
        let path = std::env::temp_dir()
            .join(format!("maho-cli-ocr-owned-{}.png", std::process::id()));
        std::fs::write(&path, b"caller image").unwrap();
        let payload = ocr_payload_with(
            Some(path.clone()),
            false,
            || panic!("must not capture"),
            |_| Ok(vec![block()]),
        )
        .unwrap();
        let kept = path.is_file();
        let _ = std::fs::remove_file(&path);
        assert!(kept);
        assert_eq!(payload["text_redacted"], false);
        assert_eq!(payload["image"], serde_json::json!(path));
    }
}
