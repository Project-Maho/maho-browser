//! Text search and coordinate grounding capability (`maho desktop find-text`).

use std::path::Path;

use crate::error::PlatformError;
use crate::grounding::{FindMatch, FindSource};

/// Find text occurrences on screen or within an image, returning match coordinates.
///
/// Searches via OCR to find matching text blocks and their coordinates.
pub fn find_text(text: &str, source: FindSource) -> Result<Vec<FindMatch>, PlatformError> {
    find_text_with(
        text,
        source,
        || crate::capture::capture_screen(None),
        |path| crate::ocr::ocr_image(path, None),
    )
}

fn find_text_with(
    text: &str,
    source: FindSource,
    capture: impl FnOnce() -> Result<crate::grounding::CaptureInfo, PlatformError>,
    ocr: impl FnOnce(&Path) -> Result<Vec<crate::grounding::TextBlock>, PlatformError>,
) -> Result<Vec<FindMatch>, PlatformError> {
    if text.is_empty() {
        return Ok(Vec::new());
    }

    let blocks = match source {
        FindSource::Screen => {
            let info = capture()?;
            let result = ocr(Path::new(&info.path));
            std::fs::remove_file(&info.path).map_err(|error| {
                PlatformError::Io(format!(
                    "failed to remove screen capture: {error}; OCR error: {:?}",
                    result.as_ref().err()
                ))
            })?;
            result?
        }
        FindSource::File(path) => ocr(&path)?,
    };

    let query = text.to_lowercase();
    let matches = blocks
        .into_iter()
        .filter(|block| block.text.to_lowercase().contains(&query))
        .map(|block| FindMatch {
            text: block.text,
            x: block.x,
            y: block.y,
            w: block.w,
            h: block.h,
            source: "ocr".to_string(),
        })
        .collect();

    Ok(matches)
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::path::PathBuf;

    #[test]
    fn screen_capture_is_deleted_after_success_and_ocr_failure() {
        for fail in [false, true] {
            let path = std::env::temp_dir().join(format!(
                "maho-find-cleanup-{}-{fail}.png",
                std::process::id()
            ));
            std::fs::write(&path, b"sensitive screen").unwrap();
            let result = find_text_with(
                "text",
                FindSource::Screen,
                || {
                    Ok(crate::grounding::CaptureInfo {
                        path: path.to_string_lossy().into_owned(),
                        width: 1,
                        height: 1,
                    })
                },
                |input| {
                    assert!(input.is_file());
                    if fail {
                        Err(PlatformError::Io("OCR failed".into()))
                    } else {
                        Ok(vec![])
                    }
                },
            );
            assert_eq!(result.is_err(), fail);
            let leaked = path.exists();
            if leaked {
                std::fs::remove_file(&path).unwrap();
            }
            assert!(
                !leaked,
                "screen screenshot was retained after OCR (fail={fail})"
            );
        }
    }

    #[test]
    fn caller_owned_image_is_retained() {
        let path = std::env::temp_dir().join(format!("maho-find-owned-{}.png", std::process::id()));
        std::fs::write(&path, b"caller image").unwrap();
        find_text_with(
            "text",
            FindSource::File(path.clone()),
            || panic!("must not capture"),
            |_| Ok(vec![]),
        )
        .unwrap();
        assert!(path.is_file());
        std::fs::remove_file(path).unwrap();
    }

    #[test]
    #[cfg(target_os = "macos")]
    fn test_find_text_fixture_grounding() {
        let manifest_dir = env!("CARGO_MANIFEST_DIR");
        let fixture_path = PathBuf::from(manifest_dir).join("tests/fixtures/ocr_fixture.png");

        let matches = find_text("grounding", FindSource::File(fixture_path))
            .expect("find_text should succeed on fixture");

        println!("fixture grounding matches: {matches:#?}");

        assert!(
            !matches.is_empty(),
            "Expected at least 1 match for 'grounding'"
        );

        for m in &matches {
            assert!(
                m.text.to_lowercase().contains("grounding"),
                "Match text '{}' must contain 'grounding'",
                m.text
            );
            assert_eq!(m.source, "ocr");
            assert!(m.x >= 0, "x ({}) must be >= 0", m.x);
            assert!(m.y >= 0, "y ({}) must be >= 0", m.y);
            assert!(
                m.x + (m.w as i32) <= 900,
                "right edge ({}) must be <= 900",
                m.x + (m.w as i32)
            );
            assert!(
                m.y + (m.h as i32) <= 220,
                "bottom edge ({}) must be <= 220",
                m.y + (m.h as i32)
            );
        }
    }

    #[test]
    #[cfg(target_os = "macos")]
    fn test_find_text_case_insensitive() {
        let manifest_dir = env!("CARGO_MANIFEST_DIR");
        let fixture_path = PathBuf::from(manifest_dir).join("tests/fixtures/ocr_fixture.png");

        let matches_lower =
            find_text("maho", FindSource::File(fixture_path.clone())).expect("lowercase query");
        let matches_upper =
            find_text("MAHO", FindSource::File(fixture_path)).expect("uppercase query");

        assert!(!matches_lower.is_empty());
        assert_eq!(matches_lower, matches_upper);
    }

    #[test]
    fn test_find_text_empty_query() {
        let manifest_dir = env!("CARGO_MANIFEST_DIR");
        let fixture_path = PathBuf::from(manifest_dir).join("tests/fixtures/ocr_fixture.png");

        let matches = find_text("", FindSource::File(fixture_path))
            .expect("empty query should return Ok([])");
        assert!(matches.is_empty());
    }

    #[test]
    #[cfg(target_os = "macos")]
    fn test_find_text_no_match() {
        let manifest_dir = env!("CARGO_MANIFEST_DIR");
        let fixture_path = PathBuf::from(manifest_dir).join("tests/fixtures/ocr_fixture.png");

        let matches = find_text(
            "nonexistent_query_token_xyz_987",
            FindSource::File(fixture_path),
        )
        .expect("no-match query should return Ok([])");
        assert!(matches.is_empty());
    }

    #[test]
    fn test_find_text_nonexistent_file() {
        let invalid_path = PathBuf::from("nonexistent_image_12345.png");
        let res = find_text("grounding", FindSource::File(invalid_path));
        assert!(res.is_err());
    }
}
