use std::path::PathBuf;

use anyhow::{anyhow, bail, Context, Result};
use printpdf::{
    Mm, Op, ParsedFont, PdfDocument, PdfFontHandle, PdfPage, PdfSaveOptions, Point, Pt, TextItem,
};
use serde::Deserialize;

use super::{MAX_PDF_BLOCKS, MAX_PDF_PAGES};

#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
struct DocumentInput {
    blocks: Vec<Block>,
}

#[derive(Deserialize)]
#[serde(tag = "type", rename_all = "snake_case", deny_unknown_fields)]
enum Block {
    Heading { level: u8, text: String },
    Paragraph { text: String },
    Table { rows: Vec<Vec<String>> },
}

const PAGE_WIDTH: Mm = Mm(210.0);
const PAGE_HEIGHT: Mm = Mm(297.0);
const MARGIN_PT: f32 = 56.0;
const BODY_SIZE: f32 = 11.0;
const MAX_TABLE_ROWS: usize = 500;
const MAX_TABLE_COLUMNS: usize = 8;

/// Candidate CJK-capable system fonts, in order. Maho ships no font of its
/// own, so the PDF writer embeds (a subset of) whichever of these exists.
fn system_font_candidates() -> Vec<PathBuf> {
    let mut candidates = Vec::new();
    if let Some(path) = std::env::var_os("MAHO_PDF_FONT") {
        candidates.push(PathBuf::from(path));
    }
    #[cfg(target_os = "windows")]
    {
        let fonts = std::env::var_os("WINDIR")
            .map(|dir| PathBuf::from(dir).join("Fonts"))
            .unwrap_or_else(|| PathBuf::from(r"C:\Windows\Fonts"));
        for name in ["malgun.ttf", "msgothic.ttc", "msyh.ttc", "arial.ttf"] {
            candidates.push(fonts.join(name));
        }
    }
    #[cfg(target_os = "macos")]
    for path in [
        "/System/Library/Fonts/AppleSDGothicNeo.ttc",
        "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
    ] {
        candidates.push(PathBuf::from(path));
    }
    #[cfg(target_os = "linux")]
    for path in [
        "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/truetype/nanum/NanumGothic.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    ] {
        candidates.push(PathBuf::from(path));
    }
    candidates
}

/// Loads the first candidate font that covers every character of `text`.
/// Returns an error instead of producing a PDF with missing glyphs.
fn load_font_covering(text: &str, candidates: &[PathBuf]) -> Result<ParsedFont> {
    let needed: Vec<char> = text.chars().filter(|ch| !ch.is_whitespace()).collect();
    for path in candidates {
        let Ok(bytes) = std::fs::read(path) else {
            continue;
        };
        let Some(font) = ParsedFont::from_bytes(&bytes, 0, &mut Vec::new()) else {
            continue;
        };
        if needed
            .iter()
            .all(|ch| font.lookup_glyph_index(*ch as u32).is_some())
        {
            return Ok(font);
        }
    }
    Err(anyhow!(
        "no installed font covers the document text; set MAHO_PDF_FONT to a font file"
    ))
}

pub(super) fn create(input: &[u8]) -> Result<Vec<u8>> {
    create_with_fonts(input, &system_font_candidates())
}

fn create_with_fonts(input: &[u8], fonts: &[PathBuf]) -> Result<Vec<u8>> {
    let parsed: DocumentInput = serde_json::from_slice(input).context("parse pdf input")?;
    if parsed.blocks.len() > MAX_PDF_BLOCKS {
        bail!("pdf exceeds {MAX_PDF_BLOCKS} blocks");
    }
    let mut lines: Vec<(String, f32)> = Vec::new();
    for block in &parsed.blocks {
        match block {
            Block::Heading { level, text } => {
                let size = match level {
                    1 => 20.0,
                    2 => 16.0,
                    3 => 13.0,
                    _ => bail!("heading level must be 1, 2 or 3"),
                };
                lines.push((clean(text), size));
            }
            Block::Paragraph { text } => {
                for line in clean(text).split('\n') {
                    lines.push((line.to_string(), BODY_SIZE));
                }
            }
            Block::Table { rows } => {
                if rows.is_empty() || rows.len() > MAX_TABLE_ROWS {
                    bail!("table needs between 1 and {MAX_TABLE_ROWS} rows");
                }
                let width = rows[0].len();
                if width == 0 || width > MAX_TABLE_COLUMNS || rows.iter().any(|r| r.len() != width)
                {
                    bail!("table rows must all have the same 1..={MAX_TABLE_COLUMNS} columns");
                }
                for row in rows {
                    let cells: Vec<String> = row.iter().map(|cell| clean(cell)).collect();
                    lines.push((cells.join("  |  "), BODY_SIZE));
                }
            }
        }
        lines.push((String::new(), BODY_SIZE / 2.0));
    }

    let all_text: String = lines.iter().map(|(text, _)| text.as_str()).collect();
    let font = load_font_covering(&all_text, fonts)?;
    let mut document = PdfDocument::new("Maho artifact");
    let font_id = document.add_font(&font);

    let usable_width = Pt::from(PAGE_WIDTH).0 - 2.0 * MARGIN_PT;
    let top = Pt::from(PAGE_HEIGHT).0 - MARGIN_PT;
    let mut pages = Vec::new();
    let mut ops = Vec::new();
    let mut y = top;
    for (text, size) in lines {
        for segment in wrap(&text, size, usable_width) {
            let line_height = size * 1.4;
            if y - line_height < MARGIN_PT {
                pages.push(PdfPage::new(
                    PAGE_WIDTH,
                    PAGE_HEIGHT,
                    std::mem::take(&mut ops),
                ));
                if pages.len() >= MAX_PDF_PAGES {
                    bail!("pdf exceeds {MAX_PDF_PAGES} pages");
                }
                y = top;
            }
            y -= line_height;
            if segment.is_empty() {
                continue;
            }
            ops.extend([
                Op::StartTextSection,
                Op::SetFont {
                    font: PdfFontHandle::External(font_id.clone()),
                    size: Pt(size),
                },
                Op::SetTextCursor {
                    pos: Point {
                        x: Pt(MARGIN_PT),
                        y: Pt(y),
                    },
                },
                Op::ShowText {
                    items: vec![TextItem::Text(segment)],
                },
                Op::EndTextSection,
            ]);
        }
    }
    pages.push(PdfPage::new(PAGE_WIDTH, PAGE_HEIGHT, ops));
    Ok(document
        .with_pages(pages)
        .save(&PdfSaveOptions::default(), &mut Vec::new()))
}

/// Greedy wrap by an average glyph width estimate (CJK glyphs are full width).
fn wrap(text: &str, size: f32, width: f32) -> Vec<String> {
    let advance = |ch: char| {
        if ch.is_ascii() {
            size * 0.55
        } else {
            size
        }
    };
    let mut lines = Vec::new();
    let mut current = String::new();
    let mut used = 0.0;
    for ch in text.chars() {
        let w = advance(ch);
        if used + w > width && !current.is_empty() {
            lines.push(std::mem::take(&mut current));
            used = 0.0;
        }
        current.push(ch);
        used += w;
    }
    lines.push(current);
    lines
}

fn clean(text: &str) -> String {
    text.chars()
        .filter(|ch| !ch.is_control() || *ch == '\n')
        .map(|ch| if ch == '\t' { ' ' } else { ch })
        .collect()
}
