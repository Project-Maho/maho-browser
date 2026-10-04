use crate::error::AppError;
use crate::models::folder::{Folder, FolderCount, FolderType};

pub fn list_folders(
    conn: &rusqlite::Connection,
    account_id: &str,
) -> Result<Vec<Folder>, AppError> {
    let mut stmt = conn.prepare(
        "SELECT id, account_id, name, path, folder_type, unread_count, total_count
         FROM folders WHERE account_id = ?1 ORDER BY name ASC",
    )?;
    let folders = stmt
        .query_map([account_id], |row| {
            let folder_type_str: String = row.get("folder_type")?;
            Ok(Folder {
                id: row.get("id")?,
                account_id: row.get("account_id")?,
                name: row.get("name")?,
                path: row.get("path")?,
                folder_type: FolderType::from_str_lossy(&folder_type_str),
                unread_count: row.get("unread_count")?,
                total_count: row.get("total_count")?,
            })
        })?
        .collect::<Result<Vec<_>, _>>()?;
    Ok(folders)
}

pub fn get_folder_counts(
    conn: &rusqlite::Connection,
    account_id: &str,
) -> Result<Vec<FolderCount>, AppError> {
    let mut stmt =
        conn.prepare("SELECT id, unread_count, total_count FROM folders WHERE account_id = ?1")?;
    let counts = stmt
        .query_map([account_id], |row| {
            Ok(FolderCount {
                folder_id: row.get("id")?,
                unread_count: row.get("unread_count")?,
                total_count: row.get("total_count")?,
            })
        })?
        .collect::<Result<Vec<_>, _>>()?;
    Ok(counts)
}

pub fn update_folder_unread_count(
    conn: &rusqlite::Connection,
    email_id: &str,
) -> Result<(), AppError> {
    let folder_id: String = conn
        .query_row(
            "SELECT folder_id FROM emails WHERE id = ?1",
            [email_id],
            |row| row.get(0),
        )
        .map_err(|_| AppError::NotFound(format!("Email {email_id} not found")))?;

    conn.execute(
        "UPDATE folders SET unread_count = (SELECT COUNT(*) FROM emails WHERE folder_id = ?1 AND is_read = 0) WHERE id = ?1",
        [&folder_id],
    )?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::test_helpers::{seed_test_data, setup_test_db};

    #[test]
    fn test_list_folders() {
        let conn = setup_test_db();
        seed_test_data(&conn);
        let folders = list_folders(&conn, "acc1").unwrap();
        assert_eq!(folders.len(), 2);
        assert_eq!(folders[0].name, "INBOX");
        assert_eq!(folders[1].name, "Sent");
    }

    #[test]
    fn test_list_folders_empty() {
        let conn = setup_test_db();
        let folders = list_folders(&conn, "nonexistent").unwrap();
        assert!(folders.is_empty());
    }

    #[test]
    fn test_get_folder_counts() {
        let conn = setup_test_db();
        seed_test_data(&conn);
        let counts = get_folder_counts(&conn, "acc1").unwrap();
        assert_eq!(counts.len(), 2);
        for count in &counts {
            assert_eq!(count.unread_count, 0);
            assert_eq!(count.total_count, 0);
        }
    }
}
