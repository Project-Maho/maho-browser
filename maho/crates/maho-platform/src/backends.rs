//! Grounding backend registry — single source of truth for which native backend
//! serves each OS/capability pair, and the ordered runtime probe chain used to
//! discover external helpers. Probe-chain entries starting with `builtin:` are
//! compiled into Maho; every other entry is an external executable located on
//! `PATH` at call time.

/// A desktop grounding capability.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum GroundingCapability {
    Capture,
    Ocr,
    Elements,
}

/// Ordered probe chain for `os` + `capability`. Empty means no backend is
/// implemented yet (callers must return the typed `Unsupported` error).
pub fn probe_chain(os: &str, capability: GroundingCapability) -> &'static [&'static str] {
    match (os, capability) {
        ("macos", GroundingCapability::Capture) => &["builtin:screencapture"],
        ("macos", GroundingCapability::Ocr) => &["builtin:vision"],
        ("macos", GroundingCapability::Elements) => &["builtin:ax"],
        ("linux", GroundingCapability::Capture) => {
            &["grim", "gnome-screenshot", "spectacle", "scrot", "import"]
        }
        ("linux", GroundingCapability::Ocr) => &["tesseract"],
        ("linux", GroundingCapability::Elements) => &["python3:gi-atspi"],
        ("windows", GroundingCapability::Capture) => &["builtin:gdi"],
        ("windows", GroundingCapability::Ocr) => &["builtin:winrt-ocr"],
        ("windows", GroundingCapability::Elements) => &["builtin:uia"],
        _ => &[],
    }
}

#[cfg(test)]
mod tests {
    use super::{probe_chain, GroundingCapability as C};

    #[test]
    fn macos_uses_builtin_backends() {
        assert_eq!(probe_chain("macos", C::Capture), &["builtin:screencapture"]);
        assert_eq!(probe_chain("macos", C::Ocr), &["builtin:vision"]);
        assert_eq!(probe_chain("macos", C::Elements), &["builtin:ax"]);
    }

    #[test]
    fn linux_probe_chain_discovers_estimated_helpers() {
        // Wayland-native first (wlroots), then GNOME, then KDE, then generic X11.
        assert_eq!(
            probe_chain("linux", C::Capture),
            &["grim", "gnome-screenshot", "spectacle", "scrot", "import"]
        );
        assert_eq!(probe_chain("linux", C::Ocr), &["tesseract"]);
        assert_eq!(probe_chain("linux", C::Elements), &["python3:gi-atspi"]);
    }

    #[test]
    fn windows_uses_builtin_backends() {
        assert_eq!(probe_chain("windows", C::Capture), &["builtin:gdi"]);
        assert_eq!(probe_chain("windows", C::Ocr), &["builtin:winrt-ocr"]);
        assert_eq!(probe_chain("windows", C::Elements), &["builtin:uia"]);
    }

    #[test]
    fn unknown_pairs_have_no_backend() {
        assert!(probe_chain("freebsd", C::Capture).is_empty());
    }
}
