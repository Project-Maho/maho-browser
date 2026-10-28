//! Client-held key mode (GAP-3): the user, not the relay, holds the sync seed
//! and the Vault key. Enabling it rotates the sync seed (new room + key), puts
//! a fresh full snapshot into the new room, rotates the Vault DEK with every
//! item re-encrypted, swaps the account (escrow) wrap for recovery-phrase
//! wraps plus a device wrap, and records `key_mode=client_held` locally.
//!
//! Ordering keeps escrow mode intact on any failure: everything is prepared in
//! memory, the snapshot is uploaded, and only then are the Vault slots, items
//! and mode written in one SQLite transaction. The runtime and the sync room
//! switch last, after that commit.

use base64::{engine::general_purpose::STANDARD, Engine as _};
use maho_types::vault::VaultLockState;

use crate::maho_core::MahoCore;
use crate::snapshot::SnapshotVaultKeys;
use crate::sync_crypto;
use crate::sync_models::SyncEntityType;
use crate::vault_manager::{
    VaultHydrationOutcome, VaultHydrationSlots, VaultManager, VaultManagerError, SLOT_KDF_PARAMS,
    SLOT_WRAPPED_ACCOUNT_KEY, SLOT_WRAPPED_RECOVERY_KEY, SLOT_WRAPPED_USER_KEY,
};

use super::vault_items::vault_item_sync_dto_from_row;

/// Uploads a snapshot body (`{hlc_ts_ceiling, creator_device_id, blob,
/// room_id}` JSON) for the given room.
pub type SyncSnapshotSink = Box<dyn Fn(&str, &str) -> Result<(), String> + Send + Sync>;

/// Vault metadata slot recording the local key custody mode.
pub const SLOT_KEY_MODE: &str = "key_mode";
pub const KEY_MODE_ESCROW: &str = "escrow";
pub const KEY_MODE_CLIENT_HELD: &str = "client_held";

impl MahoCore {
    /// Overrides how the client-held switch uploads the new room's snapshot.
    /// `None` restores the default `POST /sync/snapshots`.
    pub fn set_sync_snapshot_sink(&mut self, sink: Option<SyncSnapshotSink>) {
        self.sync_snapshot_sink = sink;
    }

    /// `"client_held"` once [`Self::enable_client_held_keys`] committed (or a
    /// client-held Vault was adopted), `"escrow"` otherwise.
    pub fn sync_key_mode(&self) -> &'static str {
        if self.is_client_held() {
            KEY_MODE_CLIENT_HELD
        } else {
            KEY_MODE_ESCROW
        }
    }

    pub(crate) fn is_client_held(&self) -> bool {
        self.storage
            .as_ref()
            .and_then(|storage| storage.get_vault_metadata(SLOT_KEY_MODE).ok().flatten())
            .is_some_and(|row| row.payload == KEY_MODE_CLIENT_HELD.as_bytes())
    }

    /// Switches this account to client-held keys and returns the new 24-word
    /// recovery phrase, which every other device must use to re-join. Requires
    /// configured sync, storage, and an unlocked (or not yet created) Vault.
    /// On any error nothing local changes: the escrow room, key and Vault stay
    /// as they were.
    pub fn enable_client_held_keys(&mut self) -> Result<String, String> {
        if self.is_client_held() {
            return Err("client_held_already_enabled".to_string());
        }
        let server_url = self
            .sync_manager
            .as_ref()
            .map(|sm| sm.config().server_url.clone())
            .ok_or_else(|| "sync is not configured".to_string())?;
        if self.storage.is_none() {
            return Err("storage is not configured".to_string());
        }
        match self.vault_runtime.status().lock_state {
            VaultLockState::Unlocked | VaultLockState::Uninitialized => {}
            _ => return Err("vault_locked".to_string()),
        }

        // (1) New seed, key and room.
        let seed = zeroize::Zeroizing::new(sync_crypto::generate_sync_seed());
        let phrase = zeroize::Zeroizing::new(sync_crypto::encode_recovery_phrase(&seed)?);
        let new_key = sync_crypto::derive_encryption_key(&seed)?;
        let new_room = sync_crypto::derive_room_id(&seed)?;

        // (3, prepare) Rotated DEK + re-encrypted items, nothing installed yet.
        let rows = self
            .storage
            .as_ref()
            .map(|storage| storage.list_vault_items())
            .transpose()
            .map_err(|error| error.to_string())?
            .unwrap_or_default();
        let prepared = self
            .vault_runtime
            .prepare_client_held_rotation(phrase.as_bytes(), &rows)
            .map_err(|error| format!("vault rotation failed: {error}"))?;

        // (2) Full snapshot for the new room, carrying the rotated items and
        // the phrase-wrapped Vault key slots.
        let mut snap = self.build_raw_snapshot()?;
        let mut wire = crate::sync_manager::SyncManager::new(Default::default());
        let local_profile = self.profile_manager.default_profile_id().map(ToString::to_string);
        wire.set_default_profile_alias(
            local_profile.map(|local| (local, sync_crypto::derive_default_profile_wire_id(&seed))),
        );
        for entity in snap.entities.values_mut().flatten() {
            wire.to_wire_profile_scope(entity);
            if entity.entity_type != SyncEntityType::VaultItem {
                continue;
            }
            let rotated = prepared.rows().iter().find(|row| row.id == entity.entity_id);
            if let Some(dto) = rotated.and_then(vault_item_sync_dto_from_row) {
                entity.payload_json = serde_json::to_string(&dto).map_err(|e| e.to_string())?;
            }
        }
        let slots = prepared.slots();
        snap.vault_keys = Some(SnapshotVaultKeys {
            kdf_params: STANDARD.encode(&slots.kdf_params),
            wrapped_user_key: STANDARD.encode(&slots.wrapped_user_key),
            wrapped_recovery_key: STANDARD.encode(&slots.wrapped_recovery_key),
        });
        let snap_json = serde_json::to_vec(&snap).map_err(|e| e.to_string())?;
        let blob = sync_crypto::encrypt_update(&snap_json, &new_key)?;
        let body = serde_json::json!({
            "hlc_ts_ceiling": snap.hlc_ts_ceiling,
            "creator_device_id": self.account_manager.device_id_hash(),
            "blob": STANDARD.encode(&blob),
            "room_id": new_room,
        })
        .to_string();
        self.upload_room_snapshot(&server_url, &new_room, &body)
            .map_err(|error| format!("snapshot upload failed: {error}"))?;

        // (3, persist) + (4): slots, items and mode in one transaction.
        let now = chrono::Utc::now();
        let updated_at = now.to_rfc3339();
        let schema_version = i64::from(u16::from(maho_types::vault::VaultSchemaVersion::CURRENT));
        let storage = self
            .storage
            .as_ref()
            .ok_or_else(|| "storage is not configured".to_string())?;
        storage
            .vault_transaction(|tx| {
                for (slot, payload) in [
                    (SLOT_KDF_PARAMS, slots.kdf_params.as_slice()),
                    (SLOT_WRAPPED_USER_KEY, slots.wrapped_user_key.as_slice()),
                    (SLOT_WRAPPED_RECOVERY_KEY, slots.wrapped_recovery_key.as_slice()),
                    (SLOT_KEY_MODE, KEY_MODE_CLIENT_HELD.as_bytes()),
                ] {
                    tx.upsert_metadata(&maho_storage::sqlite::VaultMetadataRow {
                        slot: slot.to_string(),
                        schema_version,
                        payload: payload.to_vec(),
                        updated_at: updated_at.clone(),
                    })?;
                }
                tx.delete_metadata(SLOT_WRAPPED_ACCOUNT_KEY)?;
                // Device wraps hold the retired DEK; the new one is enrolled below.
                tx.delete_all_device_keys()?;
                for row in prepared.rows() {
                    tx.upsert_item(row)?;
                }
                Ok(())
            })
            .map_err(|error| format!("vault persistence failed: {error}"))?;

        // Durable from here on: install the new key and switch rooms.
        self.vault_runtime
            .commit_client_held_rotation(prepared, now)
            .map_err(|error| error.to_string())?;
        self.last_account_seed = None;
        self.client_held_phrase = Some(zeroize::Zeroizing::new(phrase.to_string()));
        self.enroll_platform_device_best_effort();
        self.start_sync(&server_url, &new_room);
        if let Some(sm) = self.sync_manager.as_mut() {
            sm.set_encryption_key(new_key);
        }
        self.bind_default_profile_to_account(&seed);
        Ok(phrase.to_string())
    }

    fn upload_room_snapshot(&self, server_url: &str, room_id: &str, body: &str) -> Result<(), String> {
        if let Some(sink) = self.sync_snapshot_sink.as_ref() {
            return sink(room_id, body);
        }
        let rest_url = server_url
            .replace("wss://", "https://")
            .replace("ws://", "http://");
        let endpoint = format!("{}/sync/snapshots", rest_url.trim_end_matches('/'));
        let token = self.get_sync_auth_token();
        let client = crate::http::shared_http_client();
        let body = body.to_string();
        let request = async move {
            let response = client
                .post(&endpoint)
                .header("Authorization", format!("Bearer {token}"))
                .header("Content-Type", "application/json")
                .body(body)
                .send()
                .await
                .map_err(|e| format!("request failed: {e}"))?;
            if response.status().is_success() {
                Ok(())
            } else {
                Err(format!("status {}", response.status()))
            }
        };
        std::thread::scope(|scope| {
            scope
                .spawn(|| crate::memory_manager::get_runtime().block_on(request))
                .join()
                .map_err(|_| "snapshot upload thread panicked".to_string())?
        })
    }

    /// Vault ensure for a recovery-phrase session: remembers the canonical
    /// phrase (in memory) and unlocks an existing client-held Vault with it. A
    /// device without a Vault adopts one from the next snapshot that carries
    /// client-held key slots. Failures are recorded, never fatal to sync setup.
    pub(crate) fn ensure_vault_for_recovery_phrase(&mut self, seed: &[u8; 32]) {
        let Ok(phrase) = sync_crypto::encode_recovery_phrase(seed) else {
            return;
        };
        self.client_held_phrase = Some(zeroize::Zeroizing::new(phrase));
        if self.storage.is_none() || !self.is_client_held() {
            return;
        }
        if matches!(
            self.vault_runtime.status().lock_state,
            VaultLockState::Locked | VaultLockState::AutoLocked
        ) {
            let Some(phrase) = self.client_held_phrase.clone() else {
                return;
            };
            match self
                .vault_runtime
                .unlock_with_recovery(phrase.as_bytes(), chrono::Utc::now())
            {
                Ok(()) => self.enroll_platform_device_best_effort(),
                Err(error) => self.last_vault_lifecycle_error = Some(error),
            }
        }
    }

    /// Client-held Vault key slots for outgoing snapshots, so devices joining
    /// later with the phrase can unlock synced items. None in escrow mode.
    pub(crate) fn client_held_snapshot_vault_keys(&self) -> Option<SnapshotVaultKeys> {
        if !self.is_client_held() {
            return None;
        }
        let storage = self.storage.as_ref()?;
        let slots = Self::read_vault_slots(storage).ok()?;
        Some(SnapshotVaultKeys {
            kdf_params: STANDARD.encode(slots.kdf_params?),
            wrapped_user_key: STANDARD.encode(slots.wrapped_user_key?),
            wrapped_recovery_key: STANDARD.encode(slots.wrapped_recovery_key?),
        })
    }

    /// Adopts a snapshot's client-held Vault key slots on a vault-free device
    /// that joined with the recovery phrase. The slots are verified against the
    /// phrase in a scratch manager before anything is written (fail-closed).
    pub(crate) fn adopt_snapshot_vault_keys(&mut self, keys: Option<&SnapshotVaultKeys>) {
        let (Some(keys), Some(phrase)) = (keys, self.client_held_phrase.clone()) else {
            return;
        };
        if let Err(error) = self.adopt_snapshot_vault_keys_inner(keys, phrase.as_bytes()) {
            eprintln!("[vault] client-held vault adoption from snapshot failed");
            self.last_vault_lifecycle_error = Some(error);
        }
    }

    fn adopt_snapshot_vault_keys_inner(
        &mut self,
        keys: &SnapshotVaultKeys,
        phrase: &[u8],
    ) -> Result<(), VaultManagerError> {
        let storage = self.storage.as_ref().ok_or_else(|| {
            VaultManagerError::Storage("vault storage is not configured".to_string())
        })?;
        let existing = Self::read_vault_slots(storage)?;
        if existing.kdf_params.is_some()
            || existing.wrapped_user_key.is_some()
            || existing.wrapped_recovery_key.is_some()
            || existing.wrapped_account_key.is_some()
        {
            return Ok(());
        }
        let decode = |value: &str| {
            STANDARD
                .decode(value)
                .map_err(|_| VaultManagerError::CorruptedVaultData)
        };
        let slots = VaultHydrationSlots {
            kdf_params: Some(decode(&keys.kdf_params)?),
            wrapped_user_key: Some(decode(&keys.wrapped_user_key)?),
            wrapped_recovery_key: Some(decode(&keys.wrapped_recovery_key)?),
            wrapped_account_key: None,
        };
        let mut scratch = VaultManager::new();
        if scratch.hydrate_locked(slots.clone())? != VaultHydrationOutcome::Locked {
            return Err(VaultManagerError::CorruptedVaultData);
        }
        scratch.unlock_with_recovery(phrase, chrono::Utc::now())?;
        drop(scratch);

        let updated_at = chrono::Utc::now().to_rfc3339();
        let schema_version = i64::from(u16::from(maho_types::vault::VaultSchemaVersion::CURRENT));
        storage
            .vault_transaction(|tx| {
                for (slot, payload) in [
                    (SLOT_KDF_PARAMS, slots.kdf_params.clone().unwrap_or_default()),
                    (SLOT_WRAPPED_USER_KEY, slots.wrapped_user_key.clone().unwrap_or_default()),
                    (
                        SLOT_WRAPPED_RECOVERY_KEY,
                        slots.wrapped_recovery_key.clone().unwrap_or_default(),
                    ),
                    (SLOT_KEY_MODE, KEY_MODE_CLIENT_HELD.as_bytes().to_vec()),
                ] {
                    tx.upsert_metadata(&maho_storage::sqlite::VaultMetadataRow {
                        slot: slot.to_string(),
                        schema_version,
                        payload,
                        updated_at: updated_at.clone(),
                    })?;
                }
                Ok(())
            })
            .map_err(|error| VaultManagerError::Storage(error.to_string()))?;
        self.refresh_vault_hydration()?;
        self.vault_runtime
            .unlock_with_recovery(phrase, chrono::Utc::now())?;
        self.enroll_platform_device_best_effort();
        Ok(())
    }
}
