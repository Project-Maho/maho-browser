//! Epoch-checked delivery of durable IMAP mutation intents.
use crate::account::{connect_imap, resolve_account_auth, safe_logout};
use crate::error::{MailFfiError, Result};
use maho_core::services::offline_queue::{self, PendingMutation};

pub(super) fn deliver_and_ack(
    conn: &rusqlite::Connection,
    key: &[u8; 32],
    mutation: &PendingMutation,
) -> Result<()> {
    let payload: serde_json::Value = serde_json::from_str(mutation.payload_json.as_deref().unwrap_or("{}"))?;
    let epoch = payload.get("uid_validity").and_then(|v| v.as_u64())
        .and_then(|v| u32::try_from(v).ok()).filter(|v| *v != 0)
        .ok_or(MailFfiError::InvalidArg("mail mutation has no valid UIDVALIDITY"))?;
    let uid = mutation.email_uid.and_then(|v| u32::try_from(v).ok()).filter(|v| *v != 0)
        .ok_or(MailFfiError::InvalidArg("mail mutation has no valid UID"))?;
    let folder = mutation.folder_path.as_deref().ok_or(MailFfiError::InvalidArg("mail mutation has no mailbox"))?;
    let resolved = resolve_account_auth(conn, key, &mutation.account_id)?;
    let mut client = connect_imap(&resolved)?;
    let outcome = (|| -> Result<()> {
        client.validate_uid_validity(folder, epoch)?;
        match mutation.mutation_type.as_str() {
            "mark_read" => client.set_flags(folder, uid, "\\Seen")?,
            "mark_unread" => client.remove_flags(folder, uid, "\\Seen")?,
            "star" => client.set_flags(folder, uid, "\\Flagged")?,
            "unstar" => client.remove_flags(folder, uid, "\\Flagged")?,
            "delete" => client.delete_email(folder, uid)?,
            "move" => client.move_email(folder, uid, mutation.target_folder.as_deref()
                .ok_or(MailFfiError::InvalidArg("move has no target mailbox"))?)?,
            _ => return Err(MailFfiError::InvalidArg("unsupported mail mutation")),
        }
        Ok(())
    })();
    safe_logout(client);
    outcome?;
    offline_queue::remove_mutation(conn, &mutation.id)?;
    Ok(())
}

pub(super) async fn flush(ctx: &crate::state::AppCtx, account: &str) -> Result<usize> {
    let guard = super::super::calendar_api::calendar_replay::lock(ctx, account).await?;
    let pool = ctx.pool.clone();
    let key = ctx.credential_key;
    let account = account.to_owned();
    tokio::task::spawn_blocking(move || {
        let _guard = guard;
        // One connection behind a mutex: take it per step so the IMAP round
        // trips below do not hold it against the rest of the process.
        let rows = {
            let conn = pool.get().map_err(|e| MailFfiError::Pool(e.to_string()))?;
            offline_queue::list_pending_mutations(&conn, &account)?
        };
        let mut delivered = 0;
        let mut errors = Vec::new();
        for row in rows.into_iter().filter(|row| !row.mutation_type.starts_with("calendar_")).take(50) {
            let conn = pool.get().map_err(|e| MailFfiError::Pool(e.to_string()))?;
            match deliver_and_ack(&conn, &key, &row) {
                Ok(()) => delivered += 1,
                Err(error) => errors.push(format!("{}: {error}", row.id)),
            }
        }
        if !errors.is_empty() {
            return Err(MailFfiError::Internal(format!("Pending mail delivery failed: {}", errors.join("; "))));
        }
        Ok(delivered)
    }).await.map_err(|e| MailFfiError::Internal(format!("mail replay task: {e}")))?
}
