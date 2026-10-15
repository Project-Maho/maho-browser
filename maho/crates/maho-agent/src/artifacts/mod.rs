//! Native document artifact authoring.

mod docx;
mod xlsx;

use std::path::{Component, Path, PathBuf};

use anyhow::{bail, Context, Result};

// Maximum serialized workbook input accepted by the authoring API.
pub const MAX_XLSX_INPUT_BYTES: usize = 1_048_576;
// Maximum number of data rows in one worksheet, including its header.
pub const MAX_XLSX_ROWS: usize = 10_000;
// Maximum number of columns in one worksheet.
pub const MAX_XLSX_COLUMNS: usize = 100;
// Maximum serialized document input accepted for docx.
pub const MAX_DOCX_INPUT_BYTES: usize = 1_048_576;
// Maximum number of blocks (headings, paragraphs, tables) in one document.
pub const MAX_DOCX_BLOCKS: usize = 5_000;

/// Handle returned for a generated artifact.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ArtifactHandle {
    pub storage_rel_path: String,
    pub display_name: String,
    pub mime_type: &'static str,
    pub size_bytes: u64,
}

/// Creates a document artifact of `kind` ("xlsx" or "docx") from its JSON
/// input. Only a single filename within the artifact root is accepted, and
/// an existing file is never overwritten.
pub fn create(
    artifact_root: &Path,
    relative_path: &str,
    kind: &str,
    input: &[u8],
) -> Result<ArtifactHandle> {
    if relative_path.is_empty() {
        bail!("artifact path must not be empty");
    }
    let path = Path::new(relative_path);
    let mut components = path.components();
    if path.is_absolute()
        || !matches!(components.next(), Some(Component::Normal(_)))
        || components.next().is_some()
        || relative_path.contains(['/', '\\'])
    {
        bail!("artifact path must be a single filename within the artifact root");
    }
    let (extension, max_input, mime_type): (&str, usize, &'static str) = match kind {
        "xlsx" => (
            ".xlsx",
            MAX_XLSX_INPUT_BYTES,
            "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet",
        ),
        "docx" => (
            ".docx",
            MAX_DOCX_INPUT_BYTES,
            "application/vnd.openxmlformats-officedocument.wordprocessingml.document",
        ),
        _ => bail!("unsupported artifact kind: {kind}"),
    };
    if !relative_path.to_ascii_lowercase().ends_with(extension) {
        bail!("{kind} artifact path must end with {extension}");
    }
    if input.len() > max_input {
        bail!("{kind} input exceeds {max_input} bytes");
    }

    std::fs::create_dir_all(artifact_root).context("create artifact root")?;
    let bytes = match kind {
        "xlsx" => xlsx::create(input)?,
        _ => docx::create(input)?,
    };
    let target = artifact_root.join(relative_path);
    let mut file = std::fs::OpenOptions::new()
        .write(true)
        .create_new(true)
        .open(&target)
        .with_context(|| format!("create {kind} artifact"))?;
    use std::io::Write;
    file.write_all(&bytes)
        .with_context(|| format!("write {kind} artifact"))?;
    Ok(ArtifactHandle {
        storage_rel_path: relative_path.to_string(),
        display_name: relative_path.to_string(),
        mime_type,
        size_bytes: bytes.len() as u64,
    })
}

/// Resolve a generated artifact handle beneath the artifact root.
pub fn resolve(root: &Path, handle: &ArtifactHandle) -> Result<PathBuf> {
    let path = Path::new(&handle.storage_rel_path);
    if handle.storage_rel_path.is_empty()
        || path.is_absolute()
        || path.components().count() != 1
        || !matches!(path.components().next(), Some(Component::Normal(_)))
        || handle.storage_rel_path.contains(['/', '\\'])
    {
        bail!("invalid artifact handle path");
    }
    Ok(root.join(path))
}
