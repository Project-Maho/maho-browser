use super::email::{collapse_folder_copies, email_date_instant, row_to_email_summary};
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
                "read" => query.is_unread = Some(false),
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
                query.mailbox = Some(v);
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
        || query.has_attachment.is_some()
        || query.is_unread.is_some()
        || query.is_starred.is_some()
        || query.account_id.is_some()
        || query.folder_id.is_some()
        || query.mailbox.is_some()
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
    let mut where_clauses = vec![
        "(e.snoozed_until IS NULL OR julianday(e.snoozed_until) <= julianday('now'))".to_string(),
    ];

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
    if let Some(ref mailbox) = query.mailbox {
        params.push(Value::Text(mailbox.clone()));
        let idx = params.len();
        where_clauses.push(format!(
            "EXISTS (SELECT 1 FROM folders f WHERE f.id = e.folder_id
             AND f.account_id = e.account_id
             AND (f.name = ?{idx} COLLATE NOCASE OR f.path = ?{idx} COLLATE NOCASE
                  OR f.folder_type = ?{idx} COLLATE NOCASE))"
        ));
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
    for (column, value) in [
        ("e.has_attachments", query.has_attachment),
        ("e.is_read", query.is_unread.map(|unread| !unread)),
        ("e.is_starred", query.is_starred),
    ] {
        if let Some(value) = value {
            params.push(Value::Integer(i64::from(value)));
            where_clauses.push(format!("{column} = ?{}", params.len()));
        }
    }
    let date_from = query
        .date_from
        .as_deref()
        .map(parse_date_bound)
        .transpose()?;
    let date_to = query.date_to.as_deref().map(parse_date_bound).transpose()?;
    // Preserve inclusive calendar-day date_to while comparing timezone-aware instants.
    let date_to_exclusive = query
        .date_to
        .as_deref()
        .filter(|value| chrono::NaiveDate::parse_from_str(value, "%Y-%m-%d").is_ok())
        .and_then(|_| date_to)
        .and_then(|date| date.checked_add_signed(chrono::Duration::days(1)));

    let where_sql = if where_clauses.is_empty() {
        "WHERE 1=1".to_string()
    } else {
        format!("WHERE {}", where_clauses.join(" AND "))
    };

    let select_sql = format!(
        "SELECT e.id, e.account_id, e.folder_id, e.uid, e.message_id, e.subject, e.from_address,
                e.from_name, e.date, e.snippet, e.is_read, e.is_starred,
                e.is_draft, e.has_attachments
         FROM emails e
         {join_clause} {where_sql}",
    );

    let mut stmt = conn.prepare(&select_sql)?;
    let mut emails = stmt
        .query_map(
            rusqlite::params_from_iter(params.iter()),
            row_to_email_summary,
        )?
        .collect::<Result<Vec<_>, _>>()?;
    // Legacy RFC2822 values cannot be compared by SQLite's text/date functions.
    // Filter and order candidates as instants before counting or slicing the page.
    emails.retain(|email| {
        if date_from.is_none() && date_to.is_none() {
            return true;
        }
        let Some(date) = email_date_instant(&email.date) else {
            return false;
        };
        date_from.is_none_or(|from| date >= from)
            && match (date_to_exclusive, date_to) {
                (Some(end), _) => date < end,
                (_, Some(end)) => date <= end,
                _ => true,
            }
    });
    emails.sort_by_cached_key(|email| {
        (
            std::cmp::Reverse(email_date_instant(&email.date)),
            email.id.clone(),
        )
    });
    let emails = if query.folder_id.is_none() {
        collapse_folder_copies(conn, emails)?
    } else {
        emails
    };
    let total_count = emails.len() as i64;
    let emails = emails
        .into_iter()
        .skip(offset.max(0) as usize)
        .take(if limit < 0 {
            usize::MAX
        } else {
            limit as usize
        })
        .collect();

    Ok(SearchResult {
        emails,
        total_count,
        query: query.query.clone(),
    })
}

fn parse_date_bound(value: &str) -> Result<chrono::DateTime<chrono::Utc>, AppError> {
    email_date_instant(value)
        .ok_or_else(|| AppError::Validation(format!("Invalid search date: {value}")))
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
            ..Default::default()
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
            ..Default::default()
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
            ..Default::default()
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
            ..Default::default()
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
            ..Default::default()
        };
        let result = search_emails(&conn, &query).unwrap();
        assert_eq!(result.emails.len(), 1);
    }

    #[test]
    fn test_search_without_folder_collapses_gmail_label_copies() {
        let conn = setup_test_db();
        seed_test_data(&conn);
        conn.execute(
            "INSERT INTO folders (id, account_id, name, path, folder_type) VALUES ('allmail', 'acc1', 'All Mail', '[Gmail]/All Mail', 'archive')",
            [],
        )
        .unwrap();
        conn.execute(
            "INSERT INTO emails (id, account_id, folder_id, uid, message_id, subject, from_address, date, snippet, is_read, is_starred, is_draft, has_attachments)
             SELECT 'em1-allmail', account_id, 'allmail', 901, message_id, subject, from_address, date, snippet, is_read, is_starred, is_draft, has_attachments FROM emails WHERE id = 'em1'",
            [],
        )
        .unwrap();
        let query = SearchQuery {
            query: "".to_string(),
            account_id: Some("acc1".to_string()),
            is_unread: Some(true),
            ..Default::default()
        };

        let result = search_emails(&conn, &query).unwrap();

        let ids: Vec<&str> = result.emails.iter().map(|email| email.id.as_str()).collect();
        assert_eq!(ids, vec!["em1"]);
        assert_eq!(result.total_count, 1);
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
            ..Default::default()
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
            ..Default::default()
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
            ..Default::default()
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
            ..Default::default()
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
            ..Default::default()
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
            ..Default::default()
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
            ..Default::default()
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
            ..Default::default()
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
            ..Default::default()
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
            ..Default::default()
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
            ..Default::default()
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
            ..Default::default()
        };
        let result2 = search_emails(&conn, &query_page2).unwrap();
        assert_eq!(result2.emails.len(), 1);
        assert_eq!(result2.total_count, 2);
        assert_ne!(result1.emails[0].id, result2.emails[0].id);
    }
}
