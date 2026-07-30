use std::fs;
use std::path::{Path, PathBuf};

use rusqlite::Connection;

use crate::error::AppError;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct DbCopyReport {
    pub destination_path: PathBuf,
    pub backup_path: Option<PathBuf>,
    pub bytes_copied: u64,
}

pub fn copy_sqlcipher_db_atomically(
    source: &Path,
    destination: &Path,
    sqlcipher_key: &str,
) -> Result<DbCopyReport, AppError> {
    checkpoint_source(source, sqlcipher_key)?;
    copy_db_atomically_with_verifier(source, destination, |candidate| {
        verify_sqlcipher_db(candidate, sqlcipher_key)
    })
}

pub(crate) fn copy_db_atomically_with_verifier<F>(
    source: &Path,
    destination: &Path,
    verifier: F,
) -> Result<DbCopyReport, AppError>
where
    F: FnOnce(&Path) -> Result<(), AppError>,
{
    let parent = destination.parent().ok_or_else(|| {
        AppError::Validation("destination must have a parent directory".to_string())
    })?;
    fs::create_dir_all(parent)?;
    let temp_path = sidecar_path(destination, "tmp");
    let backup_path = sidecar_path(destination, "backup");
    let bytes_copied = fs::copy(source, &temp_path)?;

    if let Err(err) = verifier(&temp_path) {
        let _ = fs::remove_file(&temp_path);
        return Err(err);
    }

    let backup = if destination.exists() {
        if backup_path.exists() {
            fs::remove_file(&backup_path)?;
        }
        fs::rename(destination, &backup_path)?;
        Some(backup_path)
    } else {
        None
    };

    match fs::rename(&temp_path, destination) {
        Ok(()) => Ok(DbCopyReport {
            destination_path: destination.to_path_buf(),
            backup_path: backup,
            bytes_copied,
        }),
        Err(err) => {
            if let Some(ref backup_path) = backup {
                let _ = fs::rename(backup_path, destination);
            }
            let _ = fs::remove_file(&temp_path);
            Err(AppError::Io(err))
        }
    }
}

fn checkpoint_source(source: &Path, sqlcipher_key: &str) -> Result<(), AppError> {
    let conn = Connection::open(source)?;
    conn.pragma_update(None, "key", sqlcipher_key)?;
    conn.execute_batch("PRAGMA wal_checkpoint(FULL);")?;
    Ok(())
}

fn verify_sqlcipher_db(path: &Path, sqlcipher_key: &str) -> Result<(), AppError> {
    let conn = Connection::open(path)?;
    conn.pragma_update(None, "key", sqlcipher_key)?;
    conn.query_row("SELECT COUNT(*) FROM sqlite_master", [], |row| {
        row.get::<_, i64>(0)
    })?;
    Ok(())
}

fn sidecar_path(path: &Path, suffix: &str) -> PathBuf {
    let extension = path
        .extension()
        .map(|ext| format!("{}.{}", ext.to_string_lossy(), suffix))
        .unwrap_or_else(|| suffix.to_string());
    path.with_extension(format!("{extension}.{}", uuid::Uuid::new_v4()))
}
