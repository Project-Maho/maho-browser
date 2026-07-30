use crate::error::AppError;
use crate::models::account::AccountSummary;

pub fn list_accounts(conn: &rusqlite::Connection) -> Result<Vec<AccountSummary>, AppError> {
    let mut stmt = conn.prepare(
        "SELECT id, email, display_name, auth_type FROM accounts ORDER BY created_at ASC",
    )?;
    let accounts = stmt
        .query_map([], |row| {
            let auth_type: Option<String> = row.get("auth_type")?;
            Ok(AccountSummary {
                id: row.get("id")?,
                email: row.get("email")?,
                display_name: row.get("display_name")?,
                auth_type: auth_type.unwrap_or_else(|| "password".to_string()),
            })
        })?
        .collect::<Result<Vec<_>, _>>()?;
    Ok(accounts)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::test_helpers::{seed_test_data, setup_test_db};

    #[test]
    fn test_list_accounts_empty_db() {
        let conn = setup_test_db();
        let accounts = list_accounts(&conn).unwrap();
        assert!(accounts.is_empty());
    }

    #[test]
    fn test_list_accounts_returns_all() {
        let conn = setup_test_db();
        seed_test_data(&conn);
        let accounts = list_accounts(&conn).unwrap();
        assert_eq!(accounts.len(), 2);

        assert_eq!(accounts[0].id, "acc1");
        assert_eq!(accounts[0].email, "user@example.com");
        assert_eq!(accounts[0].display_name, "Test User");
        assert_eq!(accounts[0].auth_type, "password");

        assert_eq!(accounts[1].id, "acc2");
        assert_eq!(accounts[1].email, "other@example.com");
        assert_eq!(accounts[1].display_name, "Other User");
        assert_eq!(accounts[1].auth_type, "oauth2");
    }

    #[test]
    fn test_list_accounts_ordered_by_created_at() {
        let conn = setup_test_db();
        conn.execute(
            "INSERT INTO accounts (id, email, display_name, imap_host, imap_port, imap_encryption, smtp_host, smtp_port, smtp_encryption, username, auth_type, created_at)
             VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12)",
            rusqlite::params!["acc_b", "b@example.com", "B User", "imap.b.com", 993, "tls", "smtp.b.com", 587, "tls", "b@example.com", "password", "2024-01-02T00:00:00Z"],
        ).unwrap();
        conn.execute(
            "INSERT INTO accounts (id, email, display_name, imap_host, imap_port, imap_encryption, smtp_host, smtp_port, smtp_encryption, username, auth_type, created_at)
             VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12)",
            rusqlite::params!["acc_a", "a@example.com", "A User", "imap.a.com", 993, "tls", "smtp.a.com", 587, "tls", "a@example.com", "password", "2024-01-01T00:00:00Z"],
        ).unwrap();
        let accounts = list_accounts(&conn).unwrap();
        assert_eq!(accounts.len(), 2);
        assert_eq!(accounts[0].id, "acc_a");
        assert_eq!(accounts[1].id, "acc_b");
    }
}
