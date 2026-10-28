use anyhow::{bail, Context, Result};
use docx_rs::{Docx, Paragraph, Run, Table, TableCell, TableRow};
use serde::Deserialize;

use super::MAX_DOCX_BLOCKS;

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

const MAX_TABLE_ROWS: usize = 1_000;
const MAX_TABLE_COLUMNS: usize = 50;

/// An empty block list is a valid (blank) document; only malformed or
/// oversized input is rejected.
pub(super) fn create(input: &[u8]) -> Result<Vec<u8>> {
    let parsed: DocumentInput = serde_json::from_slice(input).context("parse docx input")?;
    if parsed.blocks.len() > MAX_DOCX_BLOCKS {
        bail!("docx exceeds {MAX_DOCX_BLOCKS} blocks");
    }
    let mut document = Docx::new();
    for block in parsed.blocks {
        document = match block {
            Block::Heading { level, text } => {
                if !(1..=3).contains(&level) {
                    bail!("heading level must be 1, 2 or 3");
                }
                let size = match level {
                    1 => 40,
                    2 => 32,
                    _ => 28,
                };
                document.add_paragraph(
                    Paragraph::new().add_run(Run::new().add_text(clean(&text)).bold().size(size)),
                )
            }
            Block::Paragraph { text } => {
                document.add_paragraph(Paragraph::new().add_run(Run::new().add_text(clean(&text))))
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
                let rows = rows
                    .iter()
                    .map(|row| {
                        TableRow::new(
                            row.iter()
                                .map(|cell| {
                                    TableCell::new().add_paragraph(
                                        Paragraph::new().add_run(Run::new().add_text(clean(cell))),
                                    )
                                })
                                .collect(),
                        )
                    })
                    .collect();
                document.add_table(Table::new(rows))
            }
        };
    }
    let mut buffer = std::io::Cursor::new(Vec::new());
    document
        .build()
        .pack(&mut buffer)
        .context("serialize docx document")?;
    Ok(buffer.into_inner())
}

/// XML 1.0 forbids most control characters; docx-rs escapes markup but not
/// these, so they are dropped (tab and newline are kept as text).
fn clean(text: &str) -> String {
    text.chars()
        .filter(|ch| !ch.is_control() || matches!(ch, '\t' | '\n'))
        .collect()
}
