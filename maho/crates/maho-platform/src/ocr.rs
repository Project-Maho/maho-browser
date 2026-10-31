//! Optical Character Recognition (OCR) capability (`maho desktop ocr`).

use std::path::Path;

use crate::error::PlatformError;
use crate::grounding::{Region, TextBlock};

/// Convert normalized Vision bounding box (origin bottom-left: x, y, w, h in [0.0, 1.0])
/// to top-left origin pixel coordinates (x, y, w, h).
#[cfg(any(target_os = "macos", test))]
pub(crate) fn vision_bbox_to_pixels(
    bbox: (f32, f32, f32, f32),
    img_w: u32,
    img_h: u32,
) -> (i32, i32, u32, u32) {
    let (bx, by, bw, bh) = bbox;
    let w_f = img_w as f32;
    let h_f = img_h as f32;

    let x = (bx * w_f).round() as i32;
    let y = ((1.0 - (by + bh)) * h_f).round() as i32;
    let w = (bw * w_f).round().max(0.0) as u32;
    let h = (bh * h_f).round().max(0.0) as u32;

    (x, y, w, h)
}

/// Perform optical character recognition on an image file, optionally bounded by a region.
///
/// Returns recognized text blocks with their top-left pixel coordinates and confidence scores.
pub fn ocr_image(path: &Path, region: Option<Region>) -> Result<Vec<TextBlock>, PlatformError> {
    #[cfg(target_os = "macos")]
    {
        mac::run_ocr(path, region)
    }
    #[cfg(target_os = "linux")]
    {
        return linux::run_ocr(path, region);
    }
    #[cfg(target_os = "windows")]
    {
        crate::win_ocr::run_ocr(path, region)
    }
}

#[cfg(target_os = "linux")]
mod linux {
    use super::*;
    use crate::probe::{CommandRunner, SystemRunner};

    pub(super) fn run_ocr(
        path: &Path,
        region: Option<Region>,
    ) -> Result<Vec<TextBlock>, PlatformError> {
        let chain =
            crate::backends::probe_chain("linux", crate::backends::GroundingCapability::Ocr);
        let tool = crate::linux_probe::select_tool(chain, &|t| SystemRunner.which(t).is_some())
            .ok_or_else(|| PlatformError::Unsupported {
                platform: "linux".to_string(),
                capability: "desktop.ocr".to_string(),
                remediation: "install tesseract-ocr (tesseract CLI with PNG support)".to_string(),
            })?;
        let runner = SystemRunner;
        let program = runner
            .which(tool)
            .unwrap_or_else(|| std::path::PathBuf::from(tool));
        let path_arg = path.to_string_lossy().into_owned();
        let out = runner.run(&program.to_string_lossy(), &[&path_arg, "stdout", "tsv"])?;
        if out.status != 0 {
            return Err(PlatformError::Io(format!(
                "tesseract failed: {}",
                out.stderr.trim()
            )));
        }
        let blocks = crate::linux_probe::parse_tesseract_tsv(&out.stdout)
            .into_iter()
            .map(|w| TextBlock {
                text: w.text,
                x: w.x,
                y: w.y,
                w: w.w,
                h: w.h,
                confidence: w.confidence,
            })
            .collect();
        Ok(crate::linux_probe::filter_region(blocks, region))
    }
}

// ---------------------------------------------------------------------------
// macOS backend (Apple Vision framework)
// ---------------------------------------------------------------------------

#[cfg(target_os = "macos")]
mod mac {
    use std::path::Path;

    use objc2::AnyThread;
    use objc2_foundation::{NSArray, NSDictionary, NSError, NSString, NSURL};
    use objc2_vision::{
        VNImageRequestHandler, VNRecognizeTextRequest, VNRequest, VNRequestTextRecognitionLevel,
    };

    use super::vision_bbox_to_pixels;
    use crate::error::PlatformError;
    use crate::grounding::{png_dimensions, Region, TextBlock};

    fn ns_err_text(err: &NSError) -> String {
        err.localizedDescription().to_string()
    }

    pub(super) fn run_ocr(
        path: &Path,
        region: Option<Region>,
    ) -> Result<Vec<TextBlock>, PlatformError> {
        let (img_w, img_h) = png_dimensions(path)?;

        let abs_path = std::fs::canonicalize(path).map_err(|e| PlatformError::Io(e.to_string()))?;
        let path_str = abs_path.to_str().ok_or_else(|| {
            PlatformError::Io(format!("path '{}' is not valid UTF-8", path.display()))
        })?;

        let ns_path = NSString::from_str(path_str);
        let url = NSURL::fileURLWithPath(&ns_path);
        let options = NSDictionary::new();

        // SAFETY: creating VNImageRequestHandler with valid file URL and empty options dictionary.
        let handler = unsafe {
            VNImageRequestHandler::initWithURL_options(
                VNImageRequestHandler::alloc(),
                &url,
                &options,
            )
        };

        let request = VNRecognizeTextRequest::new();
        request.setRecognitionLevel(VNRequestTextRecognitionLevel::Accurate);

        let lang_ko = NSString::from_str("ko-KR");
        let lang_en = NSString::from_str("en-US");
        let languages = NSArray::from_retained_slice(&[lang_ko, lang_en]);
        request.setRecognitionLanguages(&languages);
        request.setUsesLanguageCorrection(false);

        let req_ref: &VNRequest = &request;
        let requests = NSArray::from_slice(&[req_ref]);

        handler
            .performRequests_error(&requests)
            .map_err(|e| PlatformError::Io(ns_err_text(&e)))?;

        let observations = match request.results() {
            Some(obs) => obs,
            None => return Ok(Vec::new()),
        };

        let mut blocks = Vec::with_capacity(observations.len());
        for obs in observations.iter() {
            let candidates = obs.topCandidates(1);
            let top_candidate = match candidates.firstObject() {
                Some(c) => c,
                None => continue,
            };

            let text = top_candidate.string().to_string();
            let confidence = top_candidate.confidence();

            // SAFETY: observation boundingBox is a property read on a valid observation object.
            let bbox = unsafe { obs.boundingBox() };
            let (x, y, w, h) = vision_bbox_to_pixels(
                (
                    bbox.origin.x as f32,
                    bbox.origin.y as f32,
                    bbox.size.width as f32,
                    bbox.size.height as f32,
                ),
                img_w,
                img_h,
            );

            let block = TextBlock {
                text,
                x,
                y,
                w,
                h,
                confidence,
            };

            blocks.push(block);
        }

        Ok(crate::linux_probe::filter_region(blocks, region))
    }
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

#[cfg(test)]
mod tests {
    use super::*;
    use std::path::PathBuf;

    #[test]
    fn test_vision_bbox_to_pixels_full() {
        let bbox = (0.0_f32, 0.0_f32, 1.0_f32, 1.0_f32);
        let (x, y, w, h) = vision_bbox_to_pixels(bbox, 900, 220);
        assert_eq!((x, y, w, h), (0, 0, 900, 220));
    }

    #[test]
    fn test_vision_bbox_to_pixels_quadrants_and_offsets() {
        // Vision top-left quadrant: origin=(0.0, 0.5), size=(0.5, 0.5) in 1000x1000
        // In pixel coords (top-left origin), this should map to x=0, y=0, w=500, h=500.
        let tl = (0.0_f32, 0.5_f32, 0.5_f32, 0.5_f32);
        assert_eq!(vision_bbox_to_pixels(tl, 1000, 1000), (0, 0, 500, 500));

        // Vision bottom-right quadrant: origin=(0.5, 0.0), size=(0.5, 0.5) in 1000x1000
        // In pixel coords (top-left origin), this should map to x=500, y=500, w=500, h=500.
        let br = (0.5_f32, 0.0_f32, 0.5_f32, 0.5_f32);
        assert_eq!(vision_bbox_to_pixels(br, 1000, 1000), (500, 500, 500, 500));

        // Arbitrary sub-rect: origin=(0.1, 0.2), size=(0.3, 0.4) in 1000x1000
        // y_top = 1000 - (0.2 + 0.4) * 1000 = 400
        let sub = (0.1_f32, 0.2_f32, 0.3_f32, 0.4_f32);
        assert_eq!(vision_bbox_to_pixels(sub, 1000, 1000), (100, 400, 300, 400));
    }

    #[test]
    #[cfg(target_os = "macos")]
    fn test_ocr_image_fixture_macos() {
        let manifest_dir = env!("CARGO_MANIFEST_DIR");
        let fixture_path = PathBuf::from(manifest_dir).join("tests/fixtures/ocr_fixture.png");

        let blocks = ocr_image(&fixture_path, None).expect("OCR should succeed on valid fixture");

        assert!(
            !blocks.is_empty(),
            "OCR should find at least one text block"
        );

        let has_grounding = blocks
            .iter()
            .any(|b| b.text.to_uppercase().contains("GROUNDING"));
        assert!(
            has_grounding,
            "Expected OCR to recognize 'GROUNDING' in fixture, got: {:?}",
            blocks.iter().map(|b| &b.text).collect::<Vec<_>>()
        );

        // Assert all coordinates are within the 900x220 image bounds
        for block in &blocks {
            assert!(block.x >= 0, "Block x ({}) must be non-negative", block.x);
            assert!(block.y >= 0, "Block y ({}) must be non-negative", block.y);
            assert!(
                block.x + (block.w as i32) <= 900,
                "Block right ({}) must be <= image width 900",
                block.x + (block.w as i32)
            );
            assert!(
                block.y + (block.h as i32) <= 220,
                "Block bottom ({}) must be <= image height 220",
                block.y + (block.h as i32)
            );
            assert!(
                block.confidence > 0.0,
                "Block confidence must be greater than 0"
            );
        }
    }

    #[test]
    #[cfg(target_os = "macos")]
    fn test_ocr_image_with_region_filter() {
        let manifest_dir = env!("CARGO_MANIFEST_DIR");
        let fixture_path = PathBuf::from(manifest_dir).join("tests/fixtures/ocr_fixture.png");

        // Fixture renders "MAHO GROUNDING 2026" at top and "fixture target label" at bottom.
        // First run full image to get all blocks
        let all_blocks = ocr_image(&fixture_path, None).expect("full OCR");

        // Region covering only the top half
        let top_region = Region {
            x: 0,
            y: 0,
            w: 900,
            h: 110,
        };
        let top_blocks = ocr_image(&fixture_path, Some(top_region)).expect("top region OCR");

        assert!(!top_blocks.is_empty(), "Top region should find text blocks");
        assert!(
            top_blocks.len() <= all_blocks.len(),
            "Region-filtered blocks should be subset of all blocks"
        );

        for b in &top_blocks {
            let cy = b.y + (b.h as i32) / 2;
            assert!(
                cy < 110,
                "Block '{}' center y ({}) must be inside top half region (< 110)",
                b.text,
                cy
            );
        }
    }

    #[test]
    fn test_ocr_image_nonexistent_file() {
        let invalid_path = PathBuf::from("nonexistent_image_12345.png");
        let res = ocr_image(&invalid_path, None);
        assert!(res.is_err());
    }
}
