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
            // Default source: live screen capture into a temp file. An explicit
            // --output names an image file to OCR instead of capturing.
            let (image_path, screen_source) = match output {
                Some(ref p) => (p.clone(), false),
                None => match maho_platform::capture::capture_screen(None) {
                    Ok(info) => (std::path::PathBuf::from(info.path), true),
                    Err(err) => return print_platform_result::<serde_json::Value>(Err(err)),
                },
            };
            let blocks = match maho_platform::ocr::ocr_image(&image_path, region_parsed) {
                Ok(blocks) => blocks,
                Err(err) => return print_platform_result::<serde_json::Value>(Err(err)),
            };
            // Privacy contract: live-screen OCR exposes coordinates only unless
            // --full is passed; explicit file sources always include text.
            let payload = if screen_source && !full {
                json!({
                    "image": image_path,
                    "text_redacted": true,
                    "blocks": blocks.iter().map(|b| json!({
                        "x": b.x, "y": b.y, "w": b.w, "h": b.h,
                    })).collect::<Vec<_>>(),
                })
            } else {
                json!({ "image": image_path, "text_redacted": false, "blocks": blocks })
            };
            println!("{}", serde_json::to_string_pretty(&payload)?);
            return Ok(());
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
