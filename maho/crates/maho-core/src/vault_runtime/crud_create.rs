use std::str::FromStr;

use chrono::Utc;
use maho_storage::sqlite::EncryptedVaultItemRow;
use maho_types::passwords::PasswordProviderKind;
use maho_types::vault::{
    VaultItemId, VaultItemKind, VaultItemPublicDto, VaultItemPublicMetadata, VaultSchemaVersion,
};

use crate::vault_manager::record::{
    canonical_origins, encode_envelope, kind_token, mask_username, provider_token, public_dto,
    serialize_record, RecordSecret, VaultRecordPayload,
};
use crate::vault_manager::{
    VaultCrudError, VaultLoginInput, VaultManager, VaultPasskeyInput, VaultSecureItemInput,
    VaultTotpInput,
};
use crate::vault_runtime::repository::VaultRepository;

use super::VaultRuntime;

struct VaultItemCreate {
    provider: PasswordProviderKind,
    metadata: VaultItemPublicMetadata,
    username: Option<String>,
    secret: RecordSecret,
}

impl VaultRuntime {
    pub(crate) fn add_login(
        &self,
        repository: VaultRepository<'_>,
        input: VaultLoginInput,
    ) -> Result<VaultItemPublicDto, VaultCrudError> {
        let mut metadata = input.metadata;
        metadata.item_kind = VaultItemKind::Login;
        self.create_item(
            repository,
            VaultItemCreate {
                provider: PasswordProviderKind::MahoNative,
                metadata,
                username: Some(input.username.trim().to_string()),
                secret: RecordSecret::Login {
                    password: input.password.as_str().to_string(),
                },
            },
        )
    }

    pub(crate) fn add_totp(
        &self,
        repository: VaultRepository<'_>,
        input: VaultTotpInput,
    ) -> Result<VaultItemPublicDto, VaultCrudError> {
        let mut metadata = input.metadata;
        metadata.item_kind = VaultItemKind::Totp;
        self.create_item(
            repository,
            VaultItemCreate {
                provider: PasswordProviderKind::MahoNative,
                metadata,
                username: None,
                secret: RecordSecret::Totp {
                    seed: input.seed.to_vec(),
                },
            },
        )
    }

    pub(crate) fn add_passkey(
        &self,
        repository: VaultRepository<'_>,
        input: VaultPasskeyInput,
    ) -> Result<VaultItemPublicDto, VaultCrudError> {
        let mut metadata = input.metadata;
        metadata.item_kind = VaultItemKind::Passkey;
        self.create_item(
            repository,
            VaultItemCreate {
                provider: PasswordProviderKind::MahoNative,
                metadata,
                username: None,
                secret: RecordSecret::Passkey {
                    private_key: input.private_key.to_vec(),
                },
            },
        )
    }

    pub(crate) fn add_secure_item(
        &self,
        repository: VaultRepository<'_>,
        input: VaultSecureItemInput,
    ) -> Result<VaultItemPublicDto, VaultCrudError> {
        let mut metadata = input.metadata;
        metadata.item_kind = VaultItemKind::SecureItem;
        self.create_item(
            repository,
            VaultItemCreate {
                provider: PasswordProviderKind::MahoNative,
                metadata,
                username: None,
                secret: RecordSecret::SecureItem {
                    bytes: input.bytes.to_vec(),
                    notes: input.notes.map(|notes| notes.as_str().to_string()),
                },
            },
        )
    }

    fn create_item(
        &self,
        repository: VaultRepository<'_>,
        item: VaultItemCreate,
    ) -> Result<VaultItemPublicDto, VaultCrudError> {
        self.with_manager(|manager| {
            manager.active_key().map_err(VaultCrudError::from)?;
            let metadata = canonicalize_metadata(item.metadata, item.username.as_deref())?;
            let kind = metadata.item_kind;
            reject_duplicate(
                manager,
                &repository,
                item.provider.clone(),
                kind,
                &metadata.origins,
                item.username.as_deref().unwrap_or(""),
            )?;
            let item_id = VaultItemId::new();
            let record = VaultRecordPayload {
                schema_version: VaultSchemaVersion::CURRENT,
                metadata,
                username: item.username,
                secret: item.secret,
                last_used_at: None,
            };
            let envelope = manager
                .encrypt_and_wrap(&serialize_record(&record)?, &item_id)
                .map_err(VaultCrudError::from)?;
            let row = build_item_row(&item_id, item.provider, kind, &encode_envelope(&envelope)?);
            if !repository
                .insert_item(&row)
                .map_err(|error| VaultCrudError::Storage(error.to_string()))?
            {
                return Err(VaultCrudError::Storage(
                    "insert-only create did not persist (id collision)".to_string(),
                ));
            }
            let item_count = repository
                .count_active_items()
                .map_err(|error| VaultCrudError::Storage(error.to_string()))?;
            manager.set_item_count(item_count);
            public_dto(&row, &record)
        })
        .map_err(VaultCrudError::from)?
    }
}

fn reject_duplicate(
    manager: &VaultManager,
    repository: &VaultRepository<'_>,
    provider: PasswordProviderKind,
    kind: VaultItemKind,
    origins: &[maho_types::vault::CredentialOrigin],
    username: &str,
) -> Result<(), VaultCrudError> {
    let provider = provider_token(provider);
    for (row, record) in super::crud_query::active_records(manager, repository)? {
        if row.provider != provider || record.metadata.item_kind != kind {
            continue;
        }
        if record.username.as_deref().unwrap_or("").trim() != username.trim() {
            continue;
        }
        let overlaps = origins.iter().any(|origin| {
            record
                .metadata
                .origins
                .iter()
                .any(|stored| stored == origin)
        });
        if overlaps {
            let id =
                VaultItemId::from_str(&row.id).map_err(|_| VaultCrudError::MalformedPayload)?;
            return Err(VaultCrudError::Duplicate { existing: id });
        }
    }
    Ok(())
}

fn build_item_row(
    id: &VaultItemId,
    provider: PasswordProviderKind,
    kind: VaultItemKind,
    envelope: &[u8],
) -> EncryptedVaultItemRow {
    let timestamp = Utc::now().to_rfc3339();
    EncryptedVaultItemRow {
        id: id.to_string(),
        schema_version: i64::from(u16::from(VaultSchemaVersion::CURRENT)),
        revision: 1,
        provider: provider_token(provider),
        item_kind: kind_token(kind),
        envelope: envelope.to_vec(),
        created_at: timestamp.clone(),
        updated_at: timestamp,
        deleted_at: None,
    }
}

fn canonicalize_metadata(
    mut metadata: VaultItemPublicMetadata,
    username: Option<&str>,
) -> Result<VaultItemPublicMetadata, VaultCrudError> {
    metadata.origins = canonical_origins(&metadata.origins)?;
    if let Some(username) = username {
        metadata.username_hint = mask_username(username);
    }
    Ok(metadata)
}
