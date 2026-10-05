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
    let stored_uid = mutation.email_uid.and_then(|v| u32::try_from(v).ok()).filter(|v| *v != 0);
    let folder = mutation.folder_path.as_deref().ok_or(MailFfiError::InvalidArg("mail mutation has no mailbox"))?;
    // Validate addressability before opening a connection so a mutation that can
    // never name its server copy fails fast instead of holding an IMAP session.
    if stored_uid.is_none() && payload.get("message_id").and_then(|v| v.as_str()).is_none() {
        return Err(MailFfiError::InvalidArg("mail mutation has no valid UID"));
    }
    let resolved = resolve_account_auth(conn, key, &mutation.account_id)?;
    let mut client = connect_imap(&resolved)?;
    let outcome = (|| -> Result<()> {
        client.validate_uid_validity(folder, epoch)?;
        let uid = match stored_uid {
            Some(uid) => uid,
            None => {
                let message_id = payload.get("message_id").and_then(|v| v.as_str())
                    .ok_or(MailFfiError::InvalidArg("mail mutation has no valid UID"))?;
                client.find_uid_by_message_id(folder, message_id)?
                    .ok_or_else(|| MailFfiError::Internal("mail mutation UID is not resolvable yet".into()))?
            }
        };
        match mutation.mutation_type.as_str() {
            "mark_read" => client.set_flags(folder, uid, "\\Seen")?,
            "mark_unread" => client.remove_flags(folder, uid, "\\Seen")?,
            "star" => client.set_flags(folder, uid, "\\Flagged")?,
            "unstar" => client.remove_flags(folder, uid, "\\Flagged")?,
            "delete" => client.delete_email(folder, uid)?,
            "move" => {
                let target = mutation.target_folder.as_deref()
                    .ok_or(MailFfiError::InvalidArg("move has no target mailbox"))?;
                client.move_email(folder, uid, target)?;
                if let Some(message_id) = payload.get("message_id").and_then(|v| v.as_str()) {
                    if let Some(moved) = client.find_uid_by_message_id(target, message_id)? {
                        adopt_resolved_uid(conn, &mutation.account_id, target, message_id, moved)?;
                    }
                }
            }
            _ => return Err(MailFfiError::InvalidArg("unsupported mail mutation")),
        }
        Ok(())
    })();
    safe_logout(client);
    outcome?;
    offline_queue::remove_mutation(conn, &mutation.id)?;
    Ok(())
}

/// Replace the negative placeholder UID a folder move parks on the moved row
/// once the server reports the real UID it now has in the target mailbox.
fn adopt_resolved_uid(
    conn: &rusqlite::Connection,
    account_id: &str,
    folder_path: &str,
    message_id: &str,
    uid: u32,
) -> Result<()> {
    conn.execute(
        "UPDATE emails SET uid = ?1 WHERE account_id = ?2 AND message_id = ?3 AND uid < 0
           AND folder_id = (SELECT id FROM folders WHERE account_id = ?2 AND path = ?4)",
        rusqlite::params![i64::from(uid), account_id, message_id, folder_path],
    )?;
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
