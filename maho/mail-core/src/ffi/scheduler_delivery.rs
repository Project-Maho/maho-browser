//! Durable pending claims; transport completion alone may mark a schedule sent.
use crate::error::{MailFfiError, Result};
use crate::ffi::compose_api::{get_smtp_credentials, send_smtp_message, ComposeEmailRequest};
use rusqlite::{params, OptionalExtension};

fn claim(conn: &rusqlite::Connection, now: &str) -> Result<Option<String>> {
    // One SQL statement is the ownership boundary across concurrent ticks/processes.
    // A crashed owner leaves `sending`, which is deliberately never auto-replayed.
    Ok(conn
        .query_row(
            "UPDATE send_later SET status='sending', retry_count=retry_count+1, last_error=NULL
         WHERE id=(SELECT id FROM send_later WHERE status='pending'
           AND julianday(scheduled_at)<=julianday(?1)
           ORDER BY julianday(scheduled_at),id LIMIT 1) AND status='pending' RETURNING id",
            [now],
            |row| row.get(0),
        )
        .optional()?)
}

fn request(conn: &rusqlite::Connection, id: &str) -> Result<ComposeEmailRequest> {
    let value = conn.query_row(
        "SELECT account_id,to_addresses,cc_addresses,bcc_addresses,subject,body_text,body_html,
         read_receipt,attachments_json,in_reply_to,email_references FROM send_later WHERE id=?1",
        [id],
        |row| {
            Ok((
                row.get::<_, String>(0)?,
                row.get::<_, String>(1)?,
                row.get::<_, Option<String>>(2)?,
                row.get::<_, Option<String>>(3)?,
                row.get::<_, String>(4)?,
                row.get::<_, Option<String>>(5)?,
                row.get::<_, Option<String>>(6)?,
                row.get::<_, Option<bool>>(7)?,
                row.get::<_, Option<String>>(8)?,
                row.get::<_, Option<String>>(9)?,
                row.get::<_, Option<String>>(10)?,
            ))
        },
    )?;
    Ok(ComposeEmailRequest {
        account_id: value.0,
        to: serde_json::from_str(&value.1)?,
        cc: value.2.as_deref().map(serde_json::from_str).transpose()?,
        bcc: value.3.as_deref().map(serde_json::from_str).transpose()?,
        subject: value.4,
        body_text: value.5,
        body_html: value.6,
        read_receipt: value.7,
        attachments: value.8.as_deref().map(serde_json::from_str).transpose()?,
        in_reply_to: value.9,
        references: value.10,
    })
}

async fn deliver(ctx: &crate::state::AppCtx, id: &str) -> Result<()> {
    let pool = ctx.pool.clone();
    let key = ctx.credential_key;
    let id = id.to_owned();
    let (req, creds) = tokio::task::spawn_blocking(move || -> Result<_> {
        let conn = pool.get().map_err(|e| MailFfiError::Pool(e.to_string()))?;
        let req = request(&conn, &id)?;
        let creds = get_smtp_credentials(&conn, &key, &req.account_id)?;
        Ok((req, creds))
    }).await.map_err(|e| MailFfiError::Internal(format!("scheduled credentials: {e}")))??;
    let from = creds.email.clone();
    let receipt = req.read_receipt.unwrap_or(false).then(|| from.clone());
    let oauth = matches!(creds.auth, crate::account::ImapAuth::OAuth2 { .. });
    send_smtp_message(
        creds,
        oauth,
        from,
        req.to,
        req.cc.unwrap_or_default(),
        req.bcc.unwrap_or_default(),
        req.subject,
        req.body_text,
        req.body_html,
        req.in_reply_to,
        req.references,
        receipt,
        req.attachments.unwrap_or_default(),
        None,
    )
    .await
}

pub(super) async fn run(ctx: &crate::state::AppCtx, now: &str) -> Result<()> {
    for _ in 0..50 {
        let id = {
            let conn = ctx
                .pool
                .get()
                .map_err(|e| MailFfiError::Pool(e.to_string()))?;
            claim(&conn, now)?
        };
        let Some(id) = id else { break };
        let outcome = deliver(ctx, &id).await;
        let (status, error) = match outcome {
            Ok(()) => ("sent", None),
            Err(error @ MailFfiError::Core(maho_core::error::AppError::DeliveryUncertain(_))) => {
                ("uncertain", Some(error.to_string()))
            }
            Err(error) => ("failed", Some(error.to_string())),
        };
        let conn = ctx
            .pool
            .get()
            .map_err(|e| MailFfiError::Pool(e.to_string()))?;
        conn.execute(
            "UPDATE send_later SET status=?2,last_error=?3 WHERE id=?1 AND status='sending'",
            params![id, status, error],
        )?;
    }
    Ok(())
}

#[cfg(test)]
#[path = "scheduler_delivery_tests.rs"]
mod tests;
