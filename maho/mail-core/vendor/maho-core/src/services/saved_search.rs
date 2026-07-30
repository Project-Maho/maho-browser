use crate::error::AppError;
use crate::models::saved_search::SavedSearch;
use rusqlite::Connection;
use uuid::Uuid;

pub fn save_search(
    conn: &Connection,
    name: &str,
    query: &str,
    account_id: Option<&str>,
) -> Result<SavedSearch, AppError> {
    let id = Uuid::new_v4().to_string();
    conn.execute(
        "INSERT INTO saved_searches (id, name, query, account_id) VALUES (?1, ?2, ?3, ?4)",
        rusqlite::params![id, name, query, account_id],
    )?;

    let saved = conn.query_row(
        "SELECT id, name, query, account_id, created_at FROM saved_searches WHERE id = ?1",
        [&id],
        |row| {
            Ok(SavedSearch {
                id: row.get(0)?,
                name: row.get(1)?,
                query: row.get(2)?,
                account_id: row.get(3)?,
                created_at: row.get(4)?,
            })
        },
    )?;

    Ok(saved)
}

pub fn list_saved_searches(
    conn: &Connection,
    account_id: Option<&str>,
) -> Result<Vec<SavedSearch>, AppError> {
    let (sql, params): (&str, Vec<Box<dyn rusqlite::types::ToSql>>) = match account_id {
        Some(aid) => (
            "SELECT id, name, query, account_id, created_at FROM saved_searches WHERE account_id = ?1 OR account_id IS NULL ORDER BY created_at DESC",
            vec![Box::new(aid.to_string()) as Box<dyn rusqlite::types::ToSql>],
        ),
        None => (
            "SELECT id, name, query, account_id, created_at FROM saved_searches ORDER BY created_at DESC",
            vec![],
        ),
    };

    let mut stmt = conn.prepare(sql)?;
    let rows = stmt
        .query_map(
            rusqlite::params_from_iter(params.iter().map(|p| p.as_ref())),
            |row| {
                Ok(SavedSearch {
                    id: row.get(0)?,
                    name: row.get(1)?,
                    query: row.get(2)?,
                    account_id: row.get(3)?,
                    created_at: row.get(4)?,
                })
            },
        )?
        .collect::<Result<Vec<_>, _>>()?;

    Ok(rows)
}

pub fn delete_saved_search(conn: &Connection, id: &str) -> Result<(), AppError> {
    let affected = conn.execute("DELETE FROM saved_searches WHERE id = ?1", [id])?;
    if affected == 0 {
        return Err(AppError::NotFound(format!("Saved search {id} not found")));
    }
    Ok(())
}
