//! Native document artifact authoring.

mod xlsx;

use std::path::{Component, Path, PathBuf};

use anyhow::{bail, Context, Result};

// Maximum serialized workbook input accepted by the authoring API.
pub const MAX_XLSX_INPUT_BYTES: usize = 1_048_576;
// Maximum number of data rows in one worksheet, including its header.
pub const MAX_XLSX_ROWS: usize = 10_000;
// Maximum number of columns in one worksheet.
pub const MAX_XLSX_COLUMNS: usize = 100;

/// Handle returned for a generated artifact.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ArtifactHandle {
    pub storage_rel_path: String,
    pub display_name: String,
    pub mime_type: &'static str,
    pub size_bytes: u64,
}

/// Creates an XLSX from JSON containing named sheets and rows of scalar cells.
/// Only a single filename within the artifact root is accepted.
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
    if kind != "xlsx" {
        bail!("unsupported artifact kind: {kind}");
    }
    if !relative_path.to_ascii_lowercase().ends_with(".xlsx") {
        bail!("xlsx artifact path must end with .xlsx");
    }
    if input.len() > MAX_XLSX_INPUT_BYTES {
        bail!("xlsx input exceeds {MAX_XLSX_INPUT_BYTES} bytes");
    }

    std::fs::create_dir_all(artifact_root).context("create artifact root")?;
    let bytes = xlsx::create(input)?;
    let target = artifact_root.join(relative_path);
    let mut file = std::fs::OpenOptions::new()
        .write(true)
        .create_new(true)
        .open(&target)
        .context("create xlsx artifact")?;
    use std::io::Write;
    file.write_all(&bytes).context("write xlsx artifact")?;
    Ok(ArtifactHandle {
        storage_rel_path: relative_path.to_string(),
        display_name: relative_path.to_string(),
        mime_type: "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet",
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
