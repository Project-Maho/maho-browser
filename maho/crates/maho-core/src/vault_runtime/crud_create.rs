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
    VaultCredentialFormDetails, VaultCrudError, VaultLoginInput, VaultManager, VaultPasskeyInput,
    VaultSecureItemInput, VaultTotpInput,
};
use crate::vault_runtime::repository::VaultRepository;

use super::VaultRuntime;

struct VaultItemCreate {
    provider: PasswordProviderKind,
    metadata: VaultItemPublicMetadata,
    username: Option<String>,
    secret: RecordSecret,
    form_details: Option<VaultCredentialFormDetails>,
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
                form_details: input.form_details,
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
                form_details: None,
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
                form_details: None,
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
                form_details: None,
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
                item.form_details.as_ref(),
            )?;
            let item_id = VaultItemId::new();
            let mut record = VaultRecordPayload {
                schema_version: VaultSchemaVersion::CURRENT,
                metadata,
                username: item.username,
                secret: item.secret,
                last_used_at: None,
                form_details: item.form_details,
                totp_seed: None,
            };
            record.metadata.has_notes = record.has_notes();
            let envelope = manager
                .encrypt_and_wrap(&serialize_record(&record)?, &item_id)
                .map_err(VaultCrudError::from)?;
            let row = build_item_row(&item_id, item.provider, kind, &encode_envelope(&envelope)?);
            let dto = public_dto(&row, &record)?;
            let count = match repository
                .insert_item(&row)
                .map_err(|error| VaultCrudError::Storage(error.to_string()))?
            {
                Some(count) => count,
                None => {
                    return Err(VaultCrudError::Storage(
                        "insert-only create did not persist (id collision)".to_string(),
                    ));
                }
            };
            manager.set_item_count(count);
            Ok(dto)
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
    form_details: Option<&crate::vault_manager::VaultCredentialFormDetails>,
) -> Result<(), VaultCrudError> {
    let provider = provider_token(provider);
    for (row, record) in super::crud_query::active_records(manager, repository)? {
        if row.provider != provider || record.metadata.item_kind != kind {
            continue;
        }
        if has_same_identity(&record, origins, username, form_details) {
            let id =
                VaultItemId::from_str(&row.id).map_err(|_| VaultCrudError::MalformedPayload)?;
            return Err(VaultCrudError::Duplicate { existing: id });
        }
    }
    Ok(())
}

pub(super) fn has_same_identity(
    record: &VaultRecordPayload,
    origins: &[maho_types::vault::CredentialOrigin],
    username: &str,
    form_details: Option<&VaultCredentialFormDetails>,
) -> bool {
    if record.username.as_deref().unwrap_or("").trim() != username.trim() {
        return false;
    }
    if let (Some(new_details), Some(stored_details)) = (form_details, &record.form_details) {
        if new_details.username_element != stored_details.username_element
            || new_details.password_element != stored_details.password_element
            || new_details.signon_realm != stored_details.signon_realm
        {
            return false;
        }
        // Non-web credentials (e.g. Android) use their exact realm instead of
        // a web origin. Empty/default form details do not establish identity.
        if origins.is_empty()
            && record.metadata.origins.is_empty()
            && !new_details.signon_realm.is_empty()
        {
            return true;
        }
    }
    origins
        .iter()
        .any(|origin| record.metadata.origins.contains(origin))
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
