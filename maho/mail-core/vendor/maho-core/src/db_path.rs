use std::path::PathBuf;

const APP_IDENTIFIER: &str = "app.mahomail.desktop";
const DB_FILENAME: &str = "maho_mail.db";

/// Resolve the path to the maho_mail SQLite database.
/// Uses platform-specific data directories matching Tauri's app_data_dir().
///
/// - macOS: ~/Library/Application Support/app.mahomail.desktop/maho_mail.db
/// - Linux: $XDG_DATA_HOME/app.mahomail.desktop/maho_mail.db
/// - Windows: %APPDATA%/app.mahomail.desktop/maho_mail.db
pub fn resolve_db_path() -> Option<PathBuf> {
    dirs::data_dir().map(|d| d.join(APP_IDENTIFIER).join(DB_FILENAME))
}
