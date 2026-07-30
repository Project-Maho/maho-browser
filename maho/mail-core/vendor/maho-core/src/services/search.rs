use super::email::row_to_email_summary;
use crate::error::AppError;
use crate::models::search::{SearchQuery, SearchResult};
use rusqlite::types::Value;

#[allow(dead_code)]
fn has_search_operators(input: &str) -> bool {
    let operators = [
        "from:", "to:", "has:", "is:", "before:", "after:", "subject:", "in:",
    ];
    operators.iter().any(|op| input.contains(op))
}

/// Parse Gmail-style search operators (from:, to:, has:, is:, before:, after:, subject:, in:)
/// from a raw query string. Quoted values supported. Remaining text goes to FTS `query` field.
pub fn parse_search_query(input: &str) -> SearchQuery {
    let mut query = SearchQuery::default();
    let mut fts_parts: Vec<String> = Vec::new();

    let tokens = tokenize_query(input);

    for token in tokens {
        if let Some(value) = token.strip_prefix("from:") {
            let v = unquote(value);
            if !v.is_empty() {
                query.from = Some(v);
            }
        } else if let Some(value) = token.strip_prefix("to:") {
            let v = unquote(value);
            if !v.is_empty() {
                query.to = Some(v);
            }
        } else if let Some(value) = token.strip_prefix("has:") {
            let v = unquote(value).to_lowercase();
            if v == "attachment" || v == "attachments" {
                query.has_attachment = Some(true);
            }
        } else if let Some(value) = token.strip_prefix("is:") {
            let v = unquote(value).to_lowercase();
            match v.as_str() {
                "unread" => query.is_unread = Some(true),
                "starred" => query.is_starred = Some(true),
                "read" => {}
                _ => {}
            }
        } else if let Some(value) = token.strip_prefix("before:") {
            let v = unquote(value);
            if !v.is_empty() {
                query.date_to = Some(v);
            }
        } else if let Some(value) = token.strip_prefix("after:") {
            let v = unquote(value);
            if !v.is_empty() {
                query.date_from = Some(v);
            }
        } else if let Some(value) = token.strip_prefix("subject:") {
            let v = unquote(value);
            if !v.is_empty() {
                query.subject = Some(v);
            }
        } else if let Some(value) = token.strip_prefix("in:") {
            let v = unquote(value);
            if !v.is_empty() {
                query.folder_id = Some(v);
            }
        } else {
            let t = token.trim().to_string();
            if !t.is_empty() {
                fts_parts.push(t);
            }
        }
    }

    query.query = fts_parts.join(" ");
    query
}

/// Split input into operator:value tokens while respecting quoted strings.
fn tokenize_query(input: &str) -> Vec<String> {
    let mut tokens: Vec<String> = Vec::new();
    let mut current = String::new();
    let chars = input.chars();
    let mut in_quote = false;

    for ch in chars {
        if ch == '"' {
            in_quote = !in_quote;
            current.push(ch);
        } else if ch == ' ' && !in_quote {
            let trimmed = current.trim().to_string();
            if !trimmed.is_empty() {
                tokens.push(trimmed);
            }
            current.clear();
        } else {
            current.push(ch);
        }
    }

    let trimmed = current.trim().to_string();
    if !trimmed.is_empty() {
        tokens.push(trimmed);
    }

    tokens
}

/// Strip surrounding quotes from a value.
fn unquote(value: &str) -> String {
    let v = value.trim();
    if v.len() >= 2 && v.starts_with('"') && v.ends_with('"') {
        v[1..v.len() - 1].to_string()
    } else {
        v.to_string()
    }
}

fn sanitize_fts_query(input: &str) -> String {
    input
        .split_whitespace()
        .filter(|t| !t.is_empty())
        .map(|token| {
            let escaped = token.replace('"', "\"\"");
            format!("\"{escaped}\"")
        })
        .collect::<Vec<_>>()
        .join(" ")
}

pub fn search_emails(
    conn: &rusqlite::Connection,
    query: &SearchQuery,
) -> Result<SearchResult, AppError> {
    let has_fts = !query.query.trim().is_empty();
    let has_filters = query.from.is_some()
        || query.to.is_some()
        || query.subject.is_some()
        || query.has_attachment == Some(true)
        || query.is_unread == Some(true)
        || query.is_starred == Some(true)
        || query.date_from.is_some()
        || query.date_to.is_some();

    if !has_fts && !has_filters {
        return Ok(SearchResult {
            emails: Vec::new(),
            total_count: 0,
            query: query.query.clone(),
        });
    }

    let limit = query.limit.unwrap_or(50);
    let offset = query.offset.unwrap_or(0);

    let mut params: Vec<Value> = Vec::new();
    let mut where_clauses: Vec<String> = Vec::new();

    let join_clause = if has_fts {
        let sanitized = sanitize_fts_query(query.query.trim());
        params.push(Value::Text(sanitized));
        where_clauses.push(format!("emails_fts MATCH ?{}", params.len()));
        "JOIN emails_fts fts ON fts.rowid = e.rowid"
    } else {
        ""
    };

    if let Some(ref account_id) = query.account_id {
        params.push(Value::Text(account_id.clone()));
        where_clauses.push(format!("e.account_id = ?{}", params.len()));
    }
    if let Some(ref folder_id) = query.folder_id {
        params.push(Value::Text(folder_id.clone()));
        where_clauses.push(format!("e.folder_id = ?{}", params.len()));
    }
    if let Some(ref from) = query.from {
        params.push(Value::Text(format!("%{from}%")));
        where_clauses.push(format!("e.from_address LIKE ?{}", params.len()));
    }
    if let Some(ref to) = query.to {
        params.push(Value::Text(format!("%{to}%")));
        where_clauses.push(format!("e.to_addresses LIKE ?{}", params.len()));
    }
    if let Some(ref subject) = query.subject {
        params.push(Value::Text(format!("%{subject}%")));
        where_clauses.push(format!("e.subject LIKE ?{}", params.len()));
    }
    if query.has_attachment == Some(true) {
        where_clauses.push("e.has_attachments = 1".to_string());
    }
    if query.is_unread == Some(true) {
        where_clauses.push("e.is_read = 0".to_string());
    }
    if query.is_starred == Some(true) {
        where_clauses.push("e.is_starred = 1".to_string());
    }
    if let Some(ref date_from) = query.date_from {
        params.push(Value::Text(date_from.clone()));
        where_clauses.push(format!("e.date >= ?{}", params.len()));
    }
    if let Some(ref date_to) = query.date_to {
        params.push(Value::Text(format!("{date_to}T23:59:59")));
        where_clauses.push(format!("e.date <= ?{}", params.len()));
    }

    let where_sql = if where_clauses.is_empty() {
        "WHERE 1=1".to_string()
    } else {
        format!("WHERE {}", where_clauses.join(" AND "))
    };

    let count_sql = format!("SELECT COUNT(*) FROM emails e {join_clause} {where_sql}");

    let total_count: i64 = conn.query_row(
        &count_sql,
        rusqlite::params_from_iter(params.iter()),
        |row| row.get(0),
    )?;

    params.push(Value::Integer(limit));
    let limit_idx = params.len();
    params.push(Value::Integer(offset));
    let offset_idx = params.len();

    let select_sql = format!(
        "SELECT e.id, e.account_id, e.folder_id, e.uid, e.message_id, e.subject, e.from_address,
                e.from_name, e.date, e.snippet, e.is_read, e.is_starred,
                e.is_draft, e.has_attachments
         FROM emails e
         {join_clause} {where_sql}
         ORDER BY e.date DESC
         LIMIT ?{limit_idx} OFFSET ?{offset_idx}",
    );

    let mut stmt = conn.prepare(&select_sql)?;
    let emails = stmt
        .query_map(rusqlite::params_from_iter(params.iter()), |row| {
            row_to_email_summary(row)
        })?
        .collect::<Result<Vec<_>, _>>()?;

    Ok(SearchResult {
        emails,
        total_count,
        query: query.query.clone(),
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::test_helpers::{seed_test_data, setup_test_db};

    #[test]
    fn test_search_empty_query() {
        let conn = setup_test_db();
        seed_test_data(&conn);
        let query = SearchQuery {
            query: "".to_string(),
            account_id: None,
            folder_id: None,
            limit: None,
            offset: None,
            from: None,
            to: None,
            subject: None,
            has_attachment: None,
            is_unread: None,
            is_starred: None,
            date_from: None,
            date_to: None,
        };
        let result = search_emails(&conn, &query).unwrap();
        assert_eq!(result.emails.len(), 0);
        assert_eq!(result.total_count, 0);
    }

    #[test]
    fn test_search_by_subject() {
        let conn = setup_test_db();
        seed_test_data(&conn);
        let query = SearchQuery {
            query: "Hello".to_string(),
            account_id: None,
            folder_id: None,
            limit: None,
            offset: None,
            from: None,
            to: None,
            subject: None,
            has_attachment: None,
            is_unread: None,
            is_starred: None,
            date_from: None,
            date_to: None,
        };
        let result = search_emails(&conn, &query).unwrap();
        assert_eq!(result.emails.len(), 2);
    }

    #[test]
    fn test_search_by_body() {
        let conn = setup_test_db();
        seed_test_data(&conn);
        let query = SearchQuery {
            query: "reply".to_string(),
            account_id: None,
            folder_id: None,
            limit: None,
            offset: None,
            from: None,
            to: None,
            subject: None,
            has_attachment: None,
            is_unread: None,
            is_starred: None,
            date_from: None,
            date_to: None,
        };
        let result = search_emails(&conn, &query).unwrap();
        assert_eq!(result.emails.len(), 1);
        assert_eq!(result.emails[0].id, "em2");
    }

    #[test]
    fn test_search_with_account_filter() {
        let conn = setup_test_db();
        seed_test_data(&conn);
        let query = SearchQuery {
            query: "Hello".to_string(),
            account_id: Some("acc1".to_string()),
            folder_id: None,
            limit: None,
            offset: None,
            from: None,
            to: None,
            subject: None,
            has_attachment: None,
            is_unread: None,
            is_starred: None,
            date_from: None,
            date_to: None,
        };
        let result = search_emails(&conn, &query).unwrap();
        assert_eq!(result.emails.len(), 2);
    }

    #[test]
    fn test_search_with_limit() {
        let conn = setup_test_db();
        seed_test_data(&conn);
        let query = SearchQuery {
            query: "test".to_string(),
            account_id: None,
            folder_id: None,
            limit: Some(1),
            offset: None,
            from: None,
            to: None,
            subject: None,
            has_attachment: None,
            is_unread: None,
            is_starred: None,
            date_from: None,
            date_to: None,
        };
        let result = search_emails(&conn, &query).unwrap();
        assert_eq!(result.emails.len(), 1);
    }

    #[test]
    fn test_search_empty_query_and_no_filters() {
        let conn = setup_test_db();
        seed_test_data(&conn);
        let query = SearchQuery {
            query: "".to_string(),
            account_id: None,
            folder_id: None,
            limit: None,
            offset: None,
            from: None,
            to: None,
            subject: None,
            has_attachment: None,
            is_unread: None,
            is_starred: None,
            date_from: None,
            date_to: None,
        };
        let result = search_emails(&conn, &query).unwrap();
        assert_eq!(result.emails.len(), 0);
        assert_eq!(result.total_count, 0);
    }

    #[test]
    fn test_fts_only() {
        let conn = setup_test_db();
        seed_test_data(&conn);
        let query = SearchQuery {
            query: "Hello".to_string(),
            account_id: None,
            folder_id: None,
            limit: None,
            offset: None,
            from: None,
            to: None,
            subject: None,
            has_attachment: None,
            is_unread: None,
            is_starred: None,
            date_from: None,
            date_to: None,
        };
        let result = search_emails(&conn, &query).unwrap();
        assert_eq!(result.emails.len(), 2);
    }

    #[test]
    fn test_filters_only_no_fts() {
        let conn = setup_test_db();
        seed_test_data(&conn);
        let query = SearchQuery {
            query: "".to_string(),
            account_id: None,
            folder_id: None,
            limit: None,
            offset: None,
            from: Some("sender".to_string()),
            to: None,
            subject: None,
            has_attachment: None,
            is_unread: None,
            is_starred: None,
            date_from: None,
            date_to: None,
        };
        let result = search_emails(&conn, &query).unwrap();
        assert_eq!(result.emails.len(), 1);
        assert_eq!(result.emails[0].id, "em1");
    }

    #[test]
    fn test_fts_plus_filters() {
        let conn = setup_test_db();
        seed_test_data(&conn);
        let query = SearchQuery {
            query: "Hello".to_string(),
            account_id: None,
            folder_id: None,
            limit: None,
            offset: None,
            from: None,
            to: None,
            subject: None,
            has_attachment: None,
            is_unread: None,
            is_starred: Some(true),
            date_from: None,
            date_to: None,
        };
        let result = search_emails(&conn, &query).unwrap();
        assert_eq!(result.emails.len(), 1);
        assert_eq!(result.emails[0].id, "em2");
    }

    #[test]
    fn test_from_filter() {
        let conn = setup_test_db();
        seed_test_data(&conn);
        let query = SearchQuery {
            query: "".to_string(),
            account_id: None,
            folder_id: None,
            limit: None,
            offset: None,
            from: Some("user@example.com".to_string()),
            to: None,
            subject: None,
            has_attachment: None,
            is_unread: None,
            is_starred: None,
            date_from: None,
            date_to: None,
        };
        let result = search_emails(&conn, &query).unwrap();
        assert_eq!(result.emails.len(), 2);
    }

    #[test]
    fn test_to_filter() {
        let conn = setup_test_db();
        seed_test_data(&conn);
        let query = SearchQuery {
            query: "".to_string(),
            account_id: None,
            folder_id: None,
            limit: None,
            offset: None,
            from: None,
            to: Some("sender".to_string()),
            subject: None,
            has_attachment: None,
            is_unread: None,
            is_starred: None,
            date_from: None,
            date_to: None,
        };
        let result = search_emails(&conn, &query).unwrap();
        assert_eq!(result.emails.len(), 1);
        assert_eq!(result.emails[0].id, "em2");
    }

    #[test]
    fn test_date_range() {
        let conn = setup_test_db();
        seed_test_data(&conn);
        let query = SearchQuery {
            query: "".to_string(),
            account_id: None,
            folder_id: None,
            limit: None,
            offset: None,
            from: None,
            to: None,
            subject: None,
            has_attachment: None,
            is_unread: None,
            is_starred: None,
            date_from: Some("2024-01-16".to_string()),
            date_to: None,
        };
        let result = search_emails(&conn, &query).unwrap();
        assert_eq!(result.emails.len(), 1);
        assert_eq!(result.emails[0].id, "em3");
    }

    #[test]
    fn test_special_chars_in_query() {
        let conn = setup_test_db();
        seed_test_data(&conn);
        let query = SearchQuery {
            query: "hello \"world".to_string(),
            account_id: None,
            folder_id: None,
            limit: None,
            offset: None,
            from: None,
            to: None,
            subject: None,
            has_attachment: None,
            is_unread: None,
            is_starred: None,
            date_from: None,
            date_to: None,
        };
        let result = search_emails(&conn, &query);
        assert!(result.is_ok());
    }

    #[test]
    fn test_fts_column_prefix_injection() {
        let conn = setup_test_db();
        seed_test_data(&conn);
        let query = SearchQuery {
            query: "subject:hack".to_string(),
            account_id: None,
            folder_id: None,
            limit: None,
            offset: None,
            from: None,
            to: None,
            subject: None,
            has_attachment: None,
            is_unread: None,
            is_starred: None,
            date_from: None,
            date_to: None,
        };
        let result = search_emails(&conn, &query);
        assert!(result.is_ok());
    }

    #[test]
    fn test_quotes_in_query() {
        let conn = setup_test_db();
        seed_test_data(&conn);
        let query = SearchQuery {
            query: "\"test\"".to_string(),
            account_id: None,
            folder_id: None,
            limit: None,
            offset: None,
            from: None,
            to: None,
            subject: None,
            has_attachment: None,
            is_unread: None,
            is_starred: None,
            date_from: None,
            date_to: None,
        };
        let result = search_emails(&conn, &query);
        assert!(result.is_ok());
    }

    #[test]
    fn test_pagination() {
        let conn = setup_test_db();
        seed_test_data(&conn);
        let query_page1 = SearchQuery {
            query: "Hello".to_string(),
            account_id: None,
            folder_id: None,
            limit: Some(1),
            offset: Some(0),
            from: None,
            to: None,
            subject: None,
            has_attachment: None,
            is_unread: None,
            is_starred: None,
            date_from: None,
            date_to: None,
        };
        let result1 = search_emails(&conn, &query_page1).unwrap();
        assert_eq!(result1.emails.len(), 1);
        assert_eq!(result1.total_count, 2);

        let query_page2 = SearchQuery {
            query: "Hello".to_string(),
            account_id: None,
            folder_id: None,
            limit: Some(1),
            offset: Some(1),
            from: None,
            to: None,
            subject: None,
            has_attachment: None,
            is_unread: None,
            is_starred: None,
            date_from: None,
            date_to: None,
        };
        let result2 = search_emails(&conn, &query_page2).unwrap();
        assert_eq!(result2.emails.len(), 1);
        assert_eq!(result2.total_count, 2);
        assert_ne!(result1.emails[0].id, result2.emails[0].id);
    }
}
