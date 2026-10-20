use anyhow::{bail, Context, Result};
use rust_xlsxwriter::{Workbook, Worksheet};
use serde::Deserialize;

use super::{MAX_XLSX_COLUMNS, MAX_XLSX_ROWS};

#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
struct WorkbookInput {
    sheets: Vec<SheetInput>,
}

#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
struct SheetInput {
    name: String,
    rows: Vec<Vec<serde_json::Value>>,
}

pub(super) fn create(input: &[u8]) -> Result<Vec<u8>> {
    let parsed: WorkbookInput = serde_json::from_slice(input).context("parse xlsx input")?;
    if parsed.sheets.is_empty() || parsed.sheets.len() > 20 {
        bail!("xlsx requires between 1 and 20 worksheets");
    }

    let mut workbook = Workbook::new();
    for sheet_input in parsed.sheets {
        validate_sheet(&sheet_input)?;
        let worksheet = workbook.add_worksheet();
        worksheet.set_name(&sheet_input.name)?;
        write_rows(worksheet, sheet_input.rows)?;
    }
    workbook.save_to_buffer().context("serialize xlsx workbook")
}

fn validate_sheet(sheet: &SheetInput) -> Result<()> {
    if sheet.name.is_empty()
        || sheet.name.chars().count() > 31
        || sheet
            .name
            .chars()
            .any(|ch| matches!(ch, '[' | ']' | ':' | '*' | '?' | '/' | '\\'))
        || sheet.name.starts_with('\'')
        || sheet.name.ends_with('\'')
    {
        bail!("invalid worksheet name: {:?}", sheet.name);
    }
    if sheet.rows.len() > MAX_XLSX_ROWS {
        bail!("worksheet exceeds {MAX_XLSX_ROWS} rows");
    }
    if sheet.rows.iter().any(|row| row.len() > MAX_XLSX_COLUMNS) {
        bail!("worksheet exceeds {MAX_XLSX_COLUMNS} columns");
    }
    for value in sheet.rows.iter().flatten() {
        if let Some(number) = value.as_f64() {
            if !number.is_finite() {
                bail!("cell numbers must be finite");
            }
        } else if !value.is_null() && !value.is_string() && !value.is_boolean() {
            bail!("cells must be strings, finite numbers, booleans, or null");
        }
        if value.as_str().is_some_and(|text| text.len() > 32_767) {
            bail!("cell text exceeds 32767 bytes");
        }
    }
    Ok(())
}

fn write_rows(worksheet: &mut Worksheet, rows: Vec<Vec<serde_json::Value>>) -> Result<()> {
    for (row_index, row) in rows.into_iter().enumerate() {
        for (column_index, value) in row.into_iter().enumerate() {
            let row = row_index as u32;
            let column = column_index as u16;
            if let Some(text) = value.as_str() {
                worksheet.write_string(row, column, text)?;
            } else if let Some(number) = value.as_f64() {
                worksheet.write_number(row, column, number)?;
            } else if let Some(boolean) = value.as_bool() {
                worksheet.write_boolean(row, column, boolean)?;
            }
            // A null cell is left empty: an unformatted blank is not stored in
            // the workbook, so there is nothing to write.
        }
    }
    Ok(())
}
