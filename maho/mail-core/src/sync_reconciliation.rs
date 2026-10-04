use super::*;

pub(super) struct Selection {
    cursor: u32,
    version: i64,
    uids: Vec<u32>,
}

pub(super) struct Snapshot {
    selection: Selection,
    flags: Vec<(u32, bool, bool)>,
}

pub(super) fn select(conn: &rusqlite::Connection, folder: &FolderSyncTarget) -> Result<Selection> {
    let (cursor, version) = conn.query_row("SELECT reconciliation_uid, reconciliation_version FROM folders WHERE id=?1",
        [&folder.id], |row| Ok((row.get(0)?, row.get(1)?)))?;
    let mut statement = conn.prepare("SELECT uid FROM emails WHERE folder_id=?1 AND uid>?2 ORDER BY uid LIMIT 500")?;
    let mut uids = statement.query_map(params![folder.id, cursor], |row| row.get(0))?
        .collect::<rusqlite::Result<Vec<u32>>>()?;
    if uids.is_empty() && cursor != 0 {
        uids = statement.query_map(params![folder.id, 0], |row| row.get(0))?
            .collect::<rusqlite::Result<Vec<u32>>>()?;
    }
    Ok(Selection { cursor, version, uids })
}

pub(super) fn fetch(
    client: &mut maho_core::imap_client::ImapClient,
    folder: &FolderSyncTarget,
    epoch: u32,
    selection: Selection,
) -> Result<Snapshot> {
    let (flags, selected_epoch) = client.fetch_uid_flags(&folder.path, &selection.uids)?;
    if selected_epoch != epoch {
        return Err(MailFfiError::Internal("mailbox epoch changed during flag reconciliation".into()));
    }
    Ok(Snapshot { selection, flags })
}

pub(super) fn is_current(conn: &rusqlite::Connection, folder: &FolderSyncTarget, snapshot: &Snapshot) -> Result<bool> {
    let version: i64 = conn.query_row("SELECT reconciliation_version FROM folders WHERE id=?1", [&folder.id], |r| r.get(0))?;
    Ok(version == snapshot.selection.version)
}

pub(super) fn apply(
    conn: &rusqlite::Connection,
    folder: &FolderSyncTarget,
    snapshot: &Snapshot,
) -> Result<()> {
    let (current_cursor, current_version): (u32, i64) = conn.query_row(
        "SELECT reconciliation_uid, reconciliation_version FROM folders WHERE id=?1",
        [&folder.id], |row| Ok((row.get(0)?, row.get(1)?)))?;
    if current_cursor != snapshot.selection.cursor || current_version != snapshot.selection.version {
        return Ok(());
    }
    let flags: HashMap<u32, (bool, bool)> = snapshot.flags.iter()
        .map(|(uid, read, starred)| (*uid, (*read, *starred))).collect();
    for uid in &snapshot.selection.uids {
        let account: String = conn.query_row("SELECT account_id FROM folders WHERE id=?1", [&folder.id], |r| r.get(0))?;
        if maho_core::services::offline_queue::has_pending_mail_mutation(conn, &account, &folder.path, *uid)? {
            continue;
        }
        if let Some((read, starred)) = flags.get(uid) {
            conn.execute("UPDATE emails SET is_read=?1, is_starred=?2 WHERE folder_id=?3 AND uid=?4",
                params![read, starred, folder.id, uid])?;
        } else {
            conn.execute("DELETE FROM emails WHERE folder_id=?1 AND uid=?2", params![folder.id, uid])?;
        }
    }
    let cursor = snapshot.selection.uids.last().copied().unwrap_or(0);
    conn.execute("UPDATE folders SET reconciliation_uid=?1, reconciliation_version=reconciliation_version+1,
        total_count=(SELECT count(*) FROM emails WHERE folder_id=?2),
        unread_count=(SELECT count(*) FROM emails WHERE folder_id=?2 AND is_read=0) WHERE id=?2",
        params![cursor, folder.id])?;
    Ok(())
}

#[cfg(test)]
#[path = "sync_reconciliation_tests.rs"]
mod tests;
