//! Windows OCR via the WinRT Windows.Media.Ocr engine. Windows exposes no
//! per-word confidence, so confidence is reported as 100.0 (engine output with
//! no uncertainty measure) — documented, not a fabricated score.

#![cfg(target_os = "windows")]

use crate::error::PlatformError;
use crate::grounding::{Region, TextBlock};
use std::path::Path;
use windows::Globalization::Language;
use windows::Graphics::Imaging::{BitmapDecoder, BitmapPixelFormat, SoftwareBitmap};
use windows::Media::Ocr::OcrEngine;
use windows::Storage::Streams::{DataWriter, InMemoryRandomAccessStream};

pub fn run_ocr(path: &Path, region: Option<Region>) -> Result<Vec<TextBlock>, PlatformError> {
    let bytes = std::fs::read(path).map_err(|e| PlatformError::Io(e.to_string()))?;

    let stream = InMemoryRandomAccessStream::new().map_err(hr)?;
    let writer = DataWriter::CreateDataWriter(&stream).map_err(hr)?;
    writer.WriteBytes(&bytes).map_err(hr)?;
    writer.StoreAsync().map_err(hr)?.get().map_err(hr)?;
    writer.FlushAsync().map_err(hr)?.get().map_err(hr)?;
    stream.Seek(0u64).map_err(hr)?;

    let decoder = BitmapDecoder::CreateAsync(&stream)
        .map_err(hr)?
        .get()
        .map_err(hr)?;
    let bitmap = decoder
        .GetSoftwareBitmapAsync()
        .map_err(hr)?
        .get()
        .map_err(hr)?;
    let bitmap = SoftwareBitmap::Convert(&bitmap, BitmapPixelFormat::Bgra8).map_err(hr)?;

    let engine = match OcrEngine::TryCreateFromUserProfileLanguages() {
        Ok(e) => Some(e),
        Err(_) => None,
    };
    let engine = match engine {
        Some(e) => e,
        None => {
            let lang = windows::core::HSTRING::from("en-US");
            let language = Language::CreateLanguage(&lang).map_err(hr)?;
            OcrEngine::TryCreateFromLanguage(&language).map_err(|_| {
                PlatformError::Unsupported {
                    platform: "windows".into(),
                    capability: "desktop.ocr".into(),
                    remediation: "install an OCR language pack (Settings > Time & language > Language & region); the engine requires a language with OCR support".into(),
                }
            })?
        }
    };

    let result = engine
        .RecognizeAsync(&bitmap)
        .map_err(hr)?
        .get()
        .map_err(hr)?;

    let mut blocks = Vec::new();
    for line in result.Lines().map_err(hr)? {
        for word in line.Words().map_err(hr)? {
            let rect = word.BoundingRect().map_err(hr)?;
            let text = word.Text().map_err(hr)?.to_string();
            if text.trim().is_empty() {
                continue;
            }
            blocks.push(TextBlock {
                text,
                x: rect.X as i32,
                y: rect.Y as i32,
                w: rect.Width as u32,
                h: rect.Height as u32,
                confidence: 100.0,
            });
        }
    }
    Ok(crate::linux_probe::filter_region(blocks, region))
}

fn hr(e: windows::core::Error) -> PlatformError {
    PlatformError::Io(format!("windows OCR backend error: {e}"))
}
