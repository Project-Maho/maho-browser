// Copyright 2026 Maho Browser. All rights reserved.

use std::sync::Arc;

use maho_core::models::account::AccountSummary;
use maho_core::models::email::{EmailDetailResponse, EmailSummary};
use maho_core::models::folder::Folder;
use maho_core::models::search::SearchResult;
use serde::{Deserialize, Serialize};

use crate::account::{connect_imap, resolve_account_auth, safe_logout};
use crate::error::{MailFfiError, Result};
use crate::state::AppCtx;

/// Boundary input for [`search_emails`]. `query` carries the raw Gmail-style
/// string parsed by `maho_core::services::search::parse_search_query`; the
/// remaining fields, when present, override whatever the parsed query produced.
#[derive(Debug, Clone, Default, Serialize, Deserialize)]
pub struct SearchParams {
    pub query: String,
    pub account_id: Option<String>,
    pub folder_id: Option<String>,
    pub limit: Option<i64>,
    pub offset: Option<i64>,
    pub from: Option<String>,
    pub to: Option<String>,
    pub subject: Option<String>,
    pub has_attachment: Option<bool>,
    pub is_unread: Option<bool>,
    pub is_starred: Option<bool>,
    pub date_from: Option<String>,
    pub date_to: Option<String>,
}

pub fn list_accounts(ctx: &AppCtx) -> Result<Vec<AccountSummary>> {
    let conn = ctx
        .pool
        .get()
        .map_err(|e| MailFfiError::Pool(e.to_string()))?;
    maho_core::services::account::list_accounts(&conn).map_err(Into::into)
}

pub fn list_folders(ctx: &AppCtx, account_id: &str) -> Result<Vec<Folder>> {
    let conn = ctx
        .pool
        .get()
        .map_err(|e| MailFfiError::Pool(e.to_string()))?;
    maho_core::services::folder::list_folders(&conn, account_id).map_err(Into::into)
}

pub fn list_local_emails(
    ctx: &AppCtx,
    account_id: &str,
    folder_id: &str,
    limit: i64,
    offset: i64,
) -> Result<Vec<EmailSummary>> {
    let conn = ctx
        .pool
        .get()
        .map_err(|e| MailFfiError::Pool(e.to_string()))?;
    maho_core::services::email::list_emails(&conn, account_id, folder_id, limit, offset)
        .map_err(Into::into)
}

pub fn list_thread(ctx: &AppCtx, account_id: &str, message_id: &str) -> Result<Vec<EmailSummary>> {
    let conn = ctx
        .pool
        .get()
        .map_err(|e| MailFfiError::Pool(e.to_string()))?;
    maho_core::services::email::list_thread_emails(&conn, account_id, message_id)
        .map_err(Into::into)
}

pub fn search_emails(ctx: &AppCtx, params: &SearchParams) -> Result<SearchResult> {
    let conn = ctx
        .pool
        .get()
        .map_err(|e| MailFfiError::Pool(e.to_string()))?;
    let mut query = maho_core::services::search::parse_search_query(&params.query);
    if params.account_id.is_some() {
        query.account_id = params.account_id.clone();
    }
    if params.folder_id.is_some() {
        query.folder_id = params.folder_id.clone();
        query.mailbox = None;
    }
    if params.limit.is_some() {
        query.limit = params.limit;
    }
    if params.offset.is_some() {
        query.offset = params.offset;
    }
    query.from = params.from.clone().or(query.from);
    query.to = params.to.clone().or(query.to);
    query.subject = params.subject.clone().or(query.subject);
    query.has_attachment = params.has_attachment.or(query.has_attachment);
    query.is_unread = params.is_unread.or(query.is_unread);
    query.is_starred = params.is_starred.or(query.is_starred);
    query.date_from = params.date_from.clone().or(query.date_from);
    query.date_to = params.date_to.clone().or(query.date_to);
    maho_core::services::search::search_emails(&conn, &query).map_err(Into::into)
}

pub async fn get_email(ctx: Arc<AppCtx>, email_id: &str) -> Result<EmailDetailResponse> {
    let email_id = email_id.to_string();
    tokio::task::spawn_blocking(move || get_email_blocking(&ctx, &email_id))
        .await
        .map_err(|e| MailFfiError::Internal(format!("read join error: {e}")))?
}

fn get_email_blocking(ctx: &AppCtx, email_id: &str) -> Result<EmailDetailResponse> {
    get_email_with_connector(ctx, email_id, connect_imap)
}

fn get_email_with_connector(
    ctx: &AppCtx,
    email_id: &str,
    connect: impl FnOnce(&crate::account::AccountAuth) -> Result<maho_core::imap_client::ImapClient>,
) -> Result<EmailDetailResponse> {
    // The database is one connection behind a mutex, so the guard is scoped to
    // each database step and released for the length of the IMAP fetch.
    let (mut email, attachments, needs_fetch) =
        ctx.with_db(|conn| Ok(maho_core::services::email::get_email_from_db(conn, email_id)?))?;
    if needs_fetch {
        let uid = u32::try_from(email.uid).ok().filter(|uid| *uid > 0)
            .ok_or(MailFfiError::InvalidArg("email has no valid IMAP UID"))?;
        let (folder_path, epoch, resolved) = ctx.with_db(|conn| {
            let (folder_path, epoch): (String, Option<u32>) = conn.query_row(
                "SELECT path, uid_validity FROM folders WHERE id = ?1 AND account_id = ?2",
                rusqlite::params![email.folder_id, email.account_id],
                |row| Ok((row.get(0)?, row.get(1)?)),
            )?;
            let epoch = epoch.filter(|epoch| *epoch > 0)
                .ok_or(MailFfiError::InvalidArg("mailbox epoch is unavailable"))?;
            let resolved = resolve_account_auth(conn, &ctx.credential_key, &email.account_id)?;
            Ok((folder_path, epoch, resolved))
        })?;
        let mut client = connect(&resolved)?;
        let fetched = (|| -> Result<_> {
            if client.get_uid_validity(&folder_path)? != epoch {
                return Err(MailFfiError::Internal("mailbox epoch changed before body fetch".into()));
            }
            Ok(client.fetch_body_with_mdn_by_uid(&folder_path, uid)?)
        })();
        safe_logout(client);
        let fetched = fetched?;
        let mut conn = ctx
            .pool
            .get()
            .map_err(|e| MailFfiError::Pool(e.to_string()))?;
        let transaction = conn.transaction_with_behavior(rusqlite::TransactionBehavior::Immediate)?;
        let current: bool = transaction.query_row(
            "SELECT EXISTS(SELECT 1 FROM emails e JOIN folders f ON e.folder_id=f.id
             AND e.account_id=f.account_id WHERE e.id=?1 AND e.account_id=?2
             AND e.folder_id=?3 AND e.uid=?4 AND f.path=?5 AND f.uid_validity=?6)",
            rusqlite::params![email_id, email.account_id, email.folder_id, uid, folder_path, epoch],
            |row| row.get(0),
        )?;
        if !current {
            return Err(MailFfiError::Internal("mailbox identity changed during body fetch".into()));
        }
        if let Some(fetched) = fetched {
            maho_core::services::email::update_email_body_with_mdn(
                &transaction,
                email_id,
                fetched.body_text.clone(),
                fetched.body_html.clone(),
                fetched.mdn_requested,
            )?;
            email.body_text = fetched.body_text;
            email.body_html = fetched.body_html;
        }
        transaction.commit()?;
    }
    Ok(EmailDetailResponse { email, attachments })
}

#[cfg(test)]
#[path = "read_body_review_tests.rs"]
mod body_review_tests;

#[cfg(test)]
#[path = "read_query_tests.rs"]
mod read_query_tests;

#[cfg(test)]
#[allow(clippy::disallowed_methods)]
mod tests {
    use super::*;

    #[test]
    fn list_local_emails_returns_seeded_messages() {
        let pool = crate::test_support::pool_with_seeded_data();
        let ctx = crate::test_support::ctx(pool);
        let emails = list_local_emails(&ctx, "acc1", "fold1", 10, 0).unwrap();
        assert_eq!(emails.len(), 2);
    }

    #[test]
    fn list_accounts_returns_all_seeded_accounts() {
        let pool = crate::test_support::pool_with_seeded_data();
        let ctx = crate::test_support::ctx(pool);
        let accounts = list_accounts(&ctx).unwrap();
        assert_eq!(accounts.len(), 2);
        assert_eq!(accounts[0].id, "acc1");
        assert_eq!(accounts[1].id, "acc2");
    }

    #[test]
    fn list_folders_returns_account_folders_ordered_by_name() {
        let pool = crate::test_support::pool_with_seeded_data();
        let ctx = crate::test_support::ctx(pool);
        let folders = list_folders(&ctx, "acc1").unwrap();
        assert_eq!(folders.len(), 2);
        assert_eq!(folders[0].name, "INBOX");
        assert_eq!(folders[1].name, "Sent");
    }

    #[test]
    fn list_thread_returns_full_thread_in_date_order() {
        let pool = crate::test_support::pool_with_seeded_data();
        let ctx = crate::test_support::ctx(pool);
        let thread = list_thread(&ctx, "acc1", "<msg1@example.com>").unwrap();
        assert_eq!(thread.len(), 2);
        assert_eq!(thread[0].id, "em1");
        assert_eq!(thread[1].id, "em2");
    }

    #[test]
    fn search_matches_subject_across_thread() {
        let pool = crate::test_support::pool_with_seeded_data();
        let ctx = crate::test_support::ctx(pool);
        let params = SearchParams {
            query: "Hello".to_string(),
            ..Default::default()
        };
        let result = search_emails(&ctx, &params).unwrap();
        assert_eq!(result.emails.len(), 2);
        assert_eq!(result.total_count, 2);
    }

    #[test]
    fn search_is_starred_operator_filters_to_starred_only() {
        let pool = crate::test_support::pool_with_seeded_data();
        let ctx = crate::test_support::ctx(pool);
        let params = SearchParams {
            query: "is:starred".to_string(),
            ..Default::default()
        };
        let result = search_emails(&ctx, &params).unwrap();
        assert_eq!(result.emails.len(), 1);
        assert_eq!(result.emails[0].id, "em2");
    }

    #[test]
    fn search_explicit_pagination_overlays_parsed_query() {
        let pool = crate::test_support::pool_with_seeded_data();
        let ctx = crate::test_support::ctx(pool);
        let page1 = search_emails(
            &ctx,
            &SearchParams {
                query: "Hello".to_string(),
                account_id: Some("acc1".to_string()),
                folder_id: None,
                limit: Some(1),
                offset: Some(0),
                ..Default::default()
            },
        )
        .unwrap();
        let page2 = search_emails(
            &ctx,
            &SearchParams {
                query: "Hello".to_string(),
                account_id: Some("acc1".to_string()),
                folder_id: None,
                limit: Some(1),
                offset: Some(1),
                ..Default::default()
            },
        )
        .unwrap();
        assert_eq!(page1.emails.len(), 1);
        assert_eq!(page1.total_count, 2);
        assert_eq!(page2.emails.len(), 1);
        assert_eq!(page2.total_count, 2);
        assert_ne!(page1.emails[0].id, page2.emails[0].id);
    }

    #[test]
    fn search_params_deserialize_from_json_boundary() {
        let params: SearchParams =
            serde_json::from_str(r#"{"query":"is:starred","account_id":"acc1","limit":5}"#)
                .unwrap();
        assert_eq!(params.query, "is:starred");
        assert_eq!(params.account_id.as_deref(), Some("acc1"));
        assert_eq!(params.limit, Some(5));
        assert_eq!(params.offset, None);
    }
}
