//! Shared types and pure helpers for desktop grounding (capture, OCR, accessibility inspection).

use serde::{Deserialize, Serialize};
use std::path::{Path, PathBuf};

use crate::error::PlatformError;

/// A rectangular region on the screen in top-left origin coordinates.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub struct Region {
    pub x: i32,
    pub y: i32,
    pub w: u32,
    pub h: u32,
}

/// A recognized block of text with top-left origin pixel coordinates.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct TextBlock {
    pub text: String,
    pub x: i32,
    pub y: i32,
    pub w: u32,
    pub h: u32,
    pub confidence: f32,
}

/// A UI element from the Accessibility tree.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct ScreenElement {
    pub role: String,
    pub label: Option<String>,
    pub x: i32,
    pub y: i32,
    pub w: u32,
    pub h: u32,
    pub pid: i32,
}

/// A matched text location on screen or in an image.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct FindMatch {
    pub text: String,
    pub x: i32,
    pub y: i32,
    pub w: u32,
    pub h: u32,
    pub source: String,
}

/// Information about a captured screen image.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct CaptureInfo {
    pub path: String,
    pub width: u32,
    pub height: u32,
}

/// Target application for accessibility inspection.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub enum AppTarget {
    Frontmost,
    Pid(i32),
}

/// Source for text search operations.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub enum FindSource {
    File(PathBuf),
    Screen,
}

/// Parse a bounding region from a string formatted as "x,y,w,h".
pub fn parse_region(s: &str) -> Result<Region, PlatformError> {
    let parts: Vec<&str> = s.split(',').map(str::trim).collect();
    if parts.len() != 4 {
        return Err(PlatformError::Io(format!(
            "invalid region format '{s}': expected 'x,y,w,h'"
        )));
    }
    let x: i32 = parts[0]
        .parse()
        .map_err(|e| PlatformError::Io(format!("invalid x coordinate in region '{s}': {e}")))?;
    let y: i32 = parts[1]
        .parse()
        .map_err(|e| PlatformError::Io(format!("invalid y coordinate in region '{s}': {e}")))?;
    let w: u32 = parts[2]
        .parse()
        .map_err(|e| PlatformError::Io(format!("invalid width in region '{s}': {e}")))?;
    let h: u32 = parts[3]
        .parse()
        .map_err(|e| PlatformError::Io(format!("invalid height in region '{s}': {e}")))?;

    Ok(Region { x, y, w, h })
}

/// PNG magic signature bytes (RFC 2083).
const PNG_SIGNATURE: [u8; 8] = [0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A];

/// Parse image dimensions (width, height) directly from the PNG IHDR header bytes.
pub fn png_dimensions(path: &Path) -> Result<(u32, u32), PlatformError> {
    let bytes = std::fs::read(path).map_err(|e| PlatformError::Io(e.to_string()))?;
    if bytes.len() < 24 {
        return Err(PlatformError::Io(format!(
            "file '{}' is too short to be a valid PNG ({} bytes)",
            path.display(),
            bytes.len()
        )));
    }

    if bytes[0..8] != PNG_SIGNATURE {
        return Err(PlatformError::Io(format!(
            "file '{}' does not have a valid PNG signature",
            path.display()
        )));
    }

    // Offset 16..20: Width (Big-Endian u32)
    // Offset 20..24: Height (Big-Endian u32)
    let width = u32::from_be_bytes([bytes[16], bytes[17], bytes[18], bytes[19]]);
    let height = u32::from_be_bytes([bytes[20], bytes[21], bytes[22], bytes[23]]);

    Ok((width, height))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_parse_region_valid() {
        let region = parse_region("10,20,300,400").expect("valid region");
        assert_eq!(
            region,
            Region {
                x: 10,
                y: 20,
                w: 300,
                h: 400,
            }
        );

        let region_spaced =
            parse_region(" -5 , 0 , 1920 , 1080 ").expect("valid region with spaces");
        assert_eq!(
            region_spaced,
            Region {
                x: -5,
                y: 0,
                w: 1920,
                h: 1080,
            }
        );
    }

    #[test]
    fn test_parse_region_invalid() {
        assert!(parse_region("10,20,300").is_err());
        assert!(parse_region("10,20,300,400,500").is_err());
        assert!(parse_region("abc,20,300,400").is_err());
        assert!(parse_region("10,-20,300,-400").is_err()); // negative height invalid for u32
        assert!(parse_region("").is_err());
    }

    #[test]
    fn test_png_dimensions_fixture() {
        let manifest_dir = env!("CARGO_MANIFEST_DIR");
        let fixture_path = PathBuf::from(manifest_dir).join("tests/fixtures/ocr_fixture.png");
        let (w, h) = png_dimensions(&fixture_path).expect("fixture png dimensions");
        assert_eq!(w, 900);
        assert_eq!(h, 220);
    }

    #[test]
    fn test_png_dimensions_synthetic_header() {
        let mut header = Vec::new();
        header.extend_from_slice(&PNG_SIGNATURE);
        header.extend_from_slice(&[0x00, 0x00, 0x00, 0x0D]); // IHDR length = 13
        header.extend_from_slice(b"IHDR");
        header.extend_from_slice(&1920u32.to_be_bytes()); // Width = 1920
        header.extend_from_slice(&1080u32.to_be_bytes()); // Height = 1080
        header.extend_from_slice(&[0x08, 0x06, 0x00, 0x00, 0x00]); // 8-bit RGBA etc.

        let temp_dir = std::env::temp_dir();
        let temp_file = temp_dir.join(format!("maho_test_synthetic_{}.png", std::process::id()));
        std::fs::write(&temp_file, &header).expect("write synthetic png header");

        let (w, h) = png_dimensions(&temp_file).expect("parse synthetic png");
        let _ = std::fs::remove_file(&temp_file);

        assert_eq!(w, 1920);
        assert_eq!(h, 1080);
    }

    #[test]
    fn test_png_dimensions_invalid() {
        let temp_dir = std::env::temp_dir();
        let temp_file = temp_dir.join(format!("maho_test_invalid_{}.png", std::process::id()));
        std::fs::write(&temp_file, b"not a png at all").expect("write invalid png");

        let res = png_dimensions(&temp_file);
        let _ = std::fs::remove_file(&temp_file);

        assert!(res.is_err());
    }
}
