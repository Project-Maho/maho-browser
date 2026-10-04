use crate::sqlite::SqliteStorage;
use crate::StorageError;

impl SqliteStorage {
    /// Return unacknowledged relay leases to the retryable state when sync is
    /// started again after a process/session ended before the ACK was stored.
    /// The delivery_id is preserved, so relay retries remain idempotent.
    pub fn recover_inflight_sync_entities(&self) -> Result<(), StorageError> {
        self.conn.execute(
            "UPDATE sync_queue SET state = 'pending' WHERE state = 'inflight' AND acked_at IS NULL",
            [],
        )?;
        Ok(())
    }
}
