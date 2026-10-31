//! Desktop input capability (`maho desktop ...`) — OS-level keyboard/mouse control.
//!
//! SECURITY CONTRACT:
//! - Every mutation requires an explicit `approve` flag; without it the typed
//!   `ApprovalRequired` error is returned and the OS is never touched.
//! - `dry_run` composes and returns the action description without touching the OS.
//! - macOS additionally requires the Accessibility TCC grant, checked via
//!   `AXIsProcessTrusted()` (the non-prompting variant); unauthorized processes get
//!   the typed `PermissionRequired` error with the System Settings deep link.

use crate::error::PlatformError;
#[cfg(target_os = "macos")]
use crate::error::DEEP_LINK_ACCESSIBILITY;
use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub enum MouseButton {
    Left,
    Right,
    Middle,
}

impl MouseButton {
    pub fn parse(s: &str) -> Result<Self, PlatformError> {
        Ok(match s.to_lowercase().as_str() {
            "left" => Self::Left,
            "right" => Self::Right,
            "middle" => Self::Middle,
            other => return Err(PlatformError::Io(format!("unknown mouse button: {other}"))),
        })
    }
}

/// One desktop input action.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub enum DesktopAction {
    MoveMouse { x: i32, y: i32 },
    Click { button: MouseButton },
    Scroll { dx: i32, dy: i32 },
    Type { text: String },
    Hotkey { keys: Vec<String> },
}

impl DesktopAction {
    /// Deliberately never echoes `Type` text (privacy: it may hold secrets).
    pub fn describe(&self) -> String {
        match self {
            Self::MoveMouse { x, y } => format!("move mouse to ({x}, {y})"),
            Self::Click { button } => format!(
                "click {} mouse button",
                match button {
                    MouseButton::Left => "left",
                    MouseButton::Right => "right",
                    MouseButton::Middle => "middle",
                }
            ),
            Self::Scroll { dx, dy } => format!("scroll by ({dx}, {dy})"),
            Self::Type { text } => {
                format!("type {} characters", text.chars().count())
            }
            Self::Hotkey { keys } => format!("press hotkey {}", keys.join("+")),
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct DesktopInputRequest {
    pub action: DesktopAction,
    pub approve: bool,
    pub dry_run: bool,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub enum DesktopOutcome {
    DryRun { description: String },
    Performed { description: String },
}

/// Test seam: records dispatched actions instead of touching the OS.
pub trait InputBackend {
    fn move_mouse(&mut self, x: i32, y: i32) -> Result<(), String>;
    fn click(&mut self, button: MouseButton) -> Result<(), String>;
    fn scroll(&mut self, dx: i32, dy: i32) -> Result<(), String>;
    fn type_text(&mut self, text: &str) -> Result<(), String>;
    fn hotkey(&mut self, keys: &[String]) -> Result<(), String>;
}

pub fn perform_input_with(
    req: DesktopInputRequest,
    backend: &mut dyn InputBackend,
    accessibility_trusted: bool,
) -> Result<DesktopOutcome, PlatformError> {
    let description = req.action.describe();
    if req.dry_run {
        return Ok(DesktopOutcome::DryRun { description });
    }
    if !req.approve {
        return Err(PlatformError::ApprovalRequired {
            action: "desktop.input".to_string(),
        });
    }
    #[cfg(target_os = "macos")]
    if !accessibility_trusted {
        return Err(PlatformError::PermissionRequired {
            scope: "accessibility".to_string(),
            deep_link: DEEP_LINK_ACCESSIBILITY.to_string(),
        });
    }
    #[cfg(not(target_os = "macos"))]
    let _ = accessibility_trusted;

    let result = match req.action {
        DesktopAction::MoveMouse { x, y } => backend.move_mouse(x, y),
        DesktopAction::Click { button } => backend.click(button),
        DesktopAction::Scroll { dx, dy } => backend.scroll(dx, dy),
        DesktopAction::Type { text } => backend.type_text(&text),
        DesktopAction::Hotkey { keys } => backend.hotkey(&keys),
    };
    result.map_err(PlatformError::Io)?;
    Ok(DesktopOutcome::Performed { description })
}

pub fn perform_input(req: DesktopInputRequest) -> Result<DesktopOutcome, PlatformError> {
    perform_input_with_factory(req, EnigoBackend::new, accessibility_trusted())
}

fn perform_input_with_factory<B: InputBackend>(
    req: DesktopInputRequest,
    factory: impl FnOnce() -> Result<B, PlatformError>,
    trusted: bool,
) -> Result<DesktopOutcome, PlatformError> {
    if req.dry_run {
        return Ok(DesktopOutcome::DryRun {
            description: req.action.describe(),
        });
    }
    if !req.approve {
        return Err(PlatformError::ApprovalRequired {
            action: "desktop.input".into(),
        });
    }
    #[cfg(target_os = "macos")]
    if !trusted {
        return Err(PlatformError::PermissionRequired {
            scope: "accessibility".into(),
            deep_link: DEEP_LINK_ACCESSIBILITY.into(),
        });
    }
    let mut backend = factory()?;
    perform_input_with(req, &mut backend, trusted)
}

/// Non-prompting Accessibility TCC probe (`AXIsProcessTrusted`).
pub fn accessibility_trusted() -> bool {
    #[cfg(target_os = "macos")]
    {
        // SAFETY: stateless framework query; does not prompt.
        unsafe { AXIsProcessTrusted() != 0 }
    }
    #[cfg(not(target_os = "macos"))]
    {
        true
    }
}

#[cfg(target_os = "macos")]
#[link(name = "ApplicationServices", kind = "framework")]
extern "C" {
    fn AXIsProcessTrusted() -> u8;
}

struct EnigoBackend {
    inner: enigo::Enigo,
}

impl EnigoBackend {
    fn new() -> Result<Self, PlatformError> {
        Ok(Self {
            inner: enigo::Enigo::new(&enigo::Settings::default())
                .map_err(|e| PlatformError::Io(e.to_string()))?,
        })
    }
}

impl InputBackend for EnigoBackend {
    fn move_mouse(&mut self, x: i32, y: i32) -> Result<(), String> {
        use enigo::{Coordinate, Mouse};
        self.inner
            .move_mouse(x, y, Coordinate::Abs)
            .map_err(|e| e.to_string())
    }

    fn click(&mut self, button: MouseButton) -> Result<(), String> {
        use enigo::{Direction, Mouse};
        let b = match button {
            MouseButton::Left => enigo::Button::Left,
            MouseButton::Right => enigo::Button::Right,
            MouseButton::Middle => enigo::Button::Middle,
        };
        self.inner
            .button(b, Direction::Click)
            .map_err(|e| e.to_string())
    }

    fn scroll(&mut self, dx: i32, dy: i32) -> Result<(), String> {
        use enigo::{Axis, Mouse};
        if dx != 0 {
            self.inner
                .scroll(dx, Axis::Horizontal)
                .map_err(|e| e.to_string())?;
        }
        if dy != 0 {
            self.inner
                .scroll(dy, Axis::Vertical)
                .map_err(|e| e.to_string())?;
        }
        Ok(())
    }

    fn type_text(&mut self, text: &str) -> Result<(), String> {
        use enigo::Keyboard;
        self.inner.text(text).map_err(|e| e.to_string())
    }

    fn hotkey(&mut self, keys: &[String]) -> Result<(), String> {
        use enigo::{Direction, Keyboard};
        if keys.is_empty() {
            return Err("hotkey requires at least one key".to_string());
        }
        let (modifiers, tap) = keys.split_at(keys.len() - 1);
        for m in modifiers {
            self.inner
                .key(map_key(m)?, Direction::Press)
                .map_err(|e| e.to_string())?;
        }
        self.inner
            .key(map_key(&tap[0])?, Direction::Click)
            .map_err(|e| e.to_string())?;
        for m in modifiers.iter().rev() {
            self.inner
                .key(map_key(m)?, Direction::Release)
                .map_err(|e| e.to_string())?;
        }
        Ok(())
    }
}

fn map_key(name: &str) -> Result<enigo::Key, String> {
    use enigo::Key;
    let lowered = name.to_lowercase();
    if lowered.chars().count() == 1 {
        let c = lowered.chars().next().expect("checked non-empty above");
        return Ok(Key::Unicode(c));
    }
    Ok(match lowered.as_str() {
        "cmd" | "meta" | "super" | "command" => Key::Meta,
        "ctrl" | "control" => Key::Control,
        "shift" => Key::Shift,
        "alt" | "option" | "opt" => Key::Alt,
        "enter" | "return" => Key::Return,
        "tab" => Key::Tab,
        "space" => Key::Space,
        "esc" | "escape" => Key::Escape,
        "backspace" | "delete" => Key::Backspace,
        "up" => Key::UpArrow,
        "down" => Key::DownArrow,
        "left" => Key::LeftArrow,
        "right" => Key::RightArrow,
        "f1" => Key::F1,
        "f2" => Key::F2,
        "f3" => Key::F3,
        "f4" => Key::F4,
        "f5" => Key::F5,
        "f10" => Key::F10,
        "f11" => Key::F11,
        "f12" => Key::F12,
        other => return Err(format!("unknown key: {other}")),
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    struct RecordingBackend {
        calls: Vec<String>,
        fail_next: bool,
    }

    impl RecordingBackend {
        fn new() -> Self {
            Self {
                calls: Vec::new(),
                fail_next: false,
            }
        }
        fn record(&mut self, call: &str) -> Result<(), String> {
            self.calls.push(call.to_string());
            if self.fail_next {
                return Err("backend failure".to_string());
            }
            Ok(())
        }
    }

    impl InputBackend for RecordingBackend {
        fn move_mouse(&mut self, x: i32, y: i32) -> Result<(), String> {
            self.record(&format!("move {x},{y}"))
        }
        fn click(&mut self, button: MouseButton) -> Result<(), String> {
            let b = match button {
                MouseButton::Left => "left",
                MouseButton::Right => "right",
                MouseButton::Middle => "middle",
            };
            self.record(&format!("click {b}"))
        }
        fn scroll(&mut self, dx: i32, dy: i32) -> Result<(), String> {
            self.record(&format!("scroll {dx},{dy}"))
        }
        fn type_text(&mut self, text: &str) -> Result<(), String> {
            self.record(&format!("type {text}"))
        }
        fn hotkey(&mut self, keys: &[String]) -> Result<(), String> {
            self.record(&format!("hotkey {}", keys.join("+")))
        }
    }

    fn req(action: DesktopAction, approve: bool, dry_run: bool) -> DesktopInputRequest {
        DesktopInputRequest {
            action,
            approve,
            dry_run,
        }
    }

    #[test]
    fn factory_failure_does_not_override_dry_run_or_approval() {
        let factory = || -> Result<RecordingBackend, PlatformError> {
            Err(PlatformError::Io("desktop unavailable".into()))
        };
        let dry = perform_input_with_factory(
            req(
                DesktopAction::Click {
                    button: MouseButton::Left,
                },
                false,
                true,
            ),
            factory,
            false,
        );
        assert!(matches!(dry, Ok(DesktopOutcome::DryRun { .. })), "{dry:?}");
        let unapproved = perform_input_with_factory(
            req(
                DesktopAction::Click {
                    button: MouseButton::Left,
                },
                false,
                false,
            ),
            factory,
            false,
        );
        assert!(
            matches!(unapproved, Err(PlatformError::ApprovalRequired { .. })),
            "{unapproved:?}"
        );
        #[cfg(target_os = "macos")]
        assert!(matches!(
            perform_input_with_factory(
                req(
                    DesktopAction::Click {
                        button: MouseButton::Left
                    },
                    true,
                    false
                ),
                factory,
                false
            ),
            Err(PlatformError::PermissionRequired { .. })
        ));
    }

    #[test]
    fn dry_run_composes_description_without_touching_backend() {
        let mut backend = RecordingBackend::new();
        let out = perform_input_with(
            req(DesktopAction::MoveMouse { x: 10, y: 20 }, false, true),
            &mut backend,
            true,
        )
        .expect("dry run must succeed");
        assert_eq!(
            out,
            DesktopOutcome::DryRun {
                description: "move mouse to (10, 20)".to_string()
            }
        );
        assert!(
            backend.calls.is_empty(),
            "dry run must not call the backend"
        );
    }

    #[test]
    fn unapproved_input_returns_typed_error_and_never_calls_backend() {
        let mut backend = RecordingBackend::new();
        let err = perform_input_with(
            req(
                DesktopAction::Click {
                    button: MouseButton::Left,
                },
                false,
                false,
            ),
            &mut backend,
            true,
        )
        .expect_err("must require approval");
        match err {
            PlatformError::ApprovalRequired { action } => {
                assert_eq!(action, "desktop.input");
            }
            other => panic!("expected ApprovalRequired, got {other:?}"),
        }
        assert!(backend.calls.is_empty(), "gate must precede backend call");
    }

    #[test]
    fn untrusted_macos_returns_permission_required_and_never_calls_backend() {
        let mut backend = RecordingBackend::new();
        let err = perform_input_with(
            req(
                DesktopAction::Click {
                    button: MouseButton::Left,
                },
                true,
                false,
            ),
            &mut backend,
            false,
        )
        .expect_err("untrusted must be rejected");
        match err {
            PlatformError::PermissionRequired { scope, deep_link } => {
                assert_eq!(scope, "accessibility");
                assert!(deep_link.contains("Privacy_Accessibility"));
            }
            other => panic!("expected PermissionRequired, got {other:?}"),
        }
        assert!(backend.calls.is_empty());
    }

    #[test]
    fn approved_input_dispatches_to_backend() {
        let mut backend = RecordingBackend::new();
        let out = perform_input_with(
            req(
                DesktopAction::Hotkey {
                    keys: vec!["cmd".into(), "t".into()],
                },
                true,
                false,
            ),
            &mut backend,
            true,
        )
        .expect("approved must run");
        assert_eq!(
            out,
            DesktopOutcome::Performed {
                description: "press hotkey cmd+t".to_string()
            }
        );
        assert_eq!(backend.calls, vec!["hotkey cmd+t"]);
    }

    #[test]
    fn backend_failure_maps_to_io_error() {
        let mut backend = RecordingBackend {
            calls: Vec::new(),
            fail_next: true,
        };
        let err = perform_input_with(
            req(DesktopAction::Type { text: "hi".into() }, true, false),
            &mut backend,
            true,
        )
        .expect_err("backend failure must surface");
        assert!(matches!(err, PlatformError::Io(_)));
    }

    #[test]
    fn describe_never_echoes_typed_text() {
        let d = DesktopAction::Type {
            text: "secret-token-1234567890".to_string(),
        }
        .describe();
        assert!(
            !d.contains("secret-token"),
            "description must not leak text"
        );
        assert!(d.contains("23 characters"));
    }

    #[test]
    fn button_parse_is_case_insensitive_and_strict() {
        assert_eq!(MouseButton::parse("LEFT").expect("ok"), MouseButton::Left);
        assert!(MouseButton::parse("side").is_err());
    }
}
