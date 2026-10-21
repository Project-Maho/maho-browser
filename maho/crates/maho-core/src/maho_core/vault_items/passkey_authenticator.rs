use chrono::Utc;
use maho_types::passwords::PasswordProviderKind;
use maho_types::vault::{
    PasskeyMetadata, PasskeyRpId, PasskeyTransport, PasskeyUserVerification, VaultItemId,
    VaultItemKind, VaultItemListRequest, VaultItemPublicMetadata, VaultTrashFilter,
};
use p256::ecdsa::signature::Signer;
use p256::ecdsa::{Signature, SigningKey};
use rand::RngCore;
use sha2::{Digest, Sha256};
use zeroize::Zeroizing;

use crate::maho_core::MahoCore;
use crate::vault_manager::{VaultCrudError, VaultPasskeyInput};

#[cfg(test)]
#[path = "passkey_authenticator/tests.rs"]
mod tests;

#[derive(Debug, thiserror::Error)]
pub enum PasskeyError {
    #[error("user verification is required")]
    UserVerificationRequired,
    #[error("passkey not found")]
    NotFound,
    #[error("Vault is locked")]
    Locked,
    #[error("passkey RP ID does not match")]
    RpMismatch,
    #[error("invalid passkey request")]
    InvalidRequest,
    #[error("Vault operation failed: {0}")]
    Vault(#[from] VaultCrudError),
}

pub struct PasskeyCreateResult {
    pub credential_id: Vec<u8>,
    pub cose_key: Vec<u8>,
    pub item_id: VaultItemId,
}

pub struct PasskeyAssertion {
    pub credential_id: Vec<u8>,
    pub authenticator_data: Vec<u8>,
    pub signature: Vec<u8>,
    pub user_handle: Vec<u8>,
}

impl MahoCore {
    pub(crate) fn vault_runtime_increment_passkey_count(
        &mut self,
        id: VaultItemId,
        count: u32,
    ) -> Result<(), PasskeyError> {
        let repository = self
            .vault_mutation_repository(maho_types::vault::VaultAuditOperation::PasskeyUsed)
            .map_err(|error| VaultCrudError::Storage(error.to_string()))?;
        let result = self
            .vault_runtime
            .increment_passkey_sign_count(&repository, id, count);
        let outcome = self.record_vault_operation_result(
            maho_types::vault::VaultAuditOperation::PasskeyUsed,
            result,
        );
        outcome.map(|_| ()).map_err(map_vault_error)
    }

    pub fn vault_passkey_create(
        &mut self,
        rp_id: PasskeyRpId,
        user_id_b64url: String,
        user_name: String,
        display_name: String,
        require_uv: bool,
        uv_performed: bool,
    ) -> Result<PasskeyCreateResult, PasskeyError> {
        require_user_verification(require_uv, uv_performed)?;
        let user_handle = decode_base64url(&user_id_b64url)?;
        if user_handle.is_empty() || user_name.trim().is_empty() || display_name.trim().is_empty() {
            return Err(PasskeyError::InvalidRequest);
        }
        let signing_key = SigningKey::random(&mut rand::rngs::OsRng);
        let private_key = Zeroizing::new(signing_key.to_bytes().to_vec());
        let mut credential_id = vec![0; 16];
        rand::rngs::OsRng.fill_bytes(&mut credential_id);
        let point = signing_key.verifying_key().to_encoded_point(false);
        let cose_key = encode_cose_key(
            point.x().ok_or(PasskeyError::InvalidRequest)?,
            point.y().ok_or(PasskeyError::InvalidRequest)?,
        );
        let now = Utc::now();
        let metadata = PasskeyMetadata {
            rp_id,
            user_name_hint: user_name.clone(),
            display_name: display_name.clone(),
            credential_id: credential_id.clone(),
            user_handle,
            transports: vec![PasskeyTransport::Internal],
            discoverable: true,
            backup_eligible: false,
            backup_state: false,
            user_verification: if require_uv {
                PasskeyUserVerification::Required
            } else {
                PasskeyUserVerification::Preferred
            },
            sign_count: 0,
            created_at: now,
            last_used_at: None,
        };
        let public = self
            .vault_add_passkey(VaultPasskeyInput {
                metadata: VaultItemPublicMetadata {
                    favorite: false,
                    trashed_at: None,
                    has_notes: false,
                    title: display_name,
                    origins: Vec::new(),
                    username_hint: user_name,
                    item_kind: VaultItemKind::Passkey,
                    totp: None,
                    passkey: Some(metadata),
                },
                private_key,
            })
            .map_err(map_vault_error)?;
        Ok(PasskeyCreateResult {
            credential_id,
            cose_key,
            item_id: public.id,
        })
    }

    pub fn vault_passkey_get_assertion(
        &mut self,
        rp_id: PasskeyRpId,
        client_data_hash: [u8; 32],
        allow_credential_ids: Vec<Vec<u8>>,
        require_uv: bool,
        uv_performed: bool,
    ) -> Result<PasskeyAssertion, PasskeyError> {
        require_user_verification(require_uv, uv_performed)?;
        let items = self
            .vault_list_items(&VaultItemListRequest {
                trash: VaultTrashFilter::Exclude,
                favorites_only: false,
                schema_version: maho_types::vault::VaultSchemaVersion::CURRENT,
                provider: Some(PasswordProviderKind::MahoNative),
                kinds: vec![VaultItemKind::Passkey],
                cursor: None,
                limit: 0,
            })
            .map_err(map_vault_error)?;
        let item = items
            .iter()
            .find(|item| {
                item.passkey.as_ref().is_some_and(|metadata| {
                    metadata.rp_id == rp_id
                        && (allow_credential_ids.is_empty()
                            || allow_credential_ids
                                .iter()
                                .any(|credential| credential == &metadata.credential_id))
                })
            })
            .ok_or(PasskeyError::NotFound)?;
        let metadata = item.passkey.as_ref().ok_or(PasskeyError::NotFound)?;
        let key_bytes = self
            .vault_use_passkey_key(item.id)
            .map_err(map_vault_error)?;
        let signing_key = SigningKey::from_slice(key_bytes.as_slice())
            .map_err(|_| PasskeyError::InvalidRequest)?;
        let count = metadata
            .sign_count
            .checked_add(1)
            .ok_or(PasskeyError::InvalidRequest)?;
        let mut authenticator_data = Sha256::digest(rp_id.as_ref().as_bytes()).to_vec();
        authenticator_data.push(0x01 | if uv_performed { 0x04 } else { 0 });
        authenticator_data.extend_from_slice(&count.to_be_bytes());
        let mut signed = authenticator_data.clone();
        signed.extend_from_slice(&client_data_hash);
        let signature: Signature = signing_key.sign(&signed);
        let signature = signature.to_der().as_bytes().to_vec();
        self.vault_runtime_increment_passkey_count(item.id, count)?;
        Ok(PasskeyAssertion {
            credential_id: metadata.credential_id.clone(),
            authenticator_data,
            signature,
            user_handle: metadata.user_handle.clone(),
        })
    }
}

fn require_user_verification(required: bool, performed: bool) -> Result<(), PasskeyError> {
    if required && !performed {
        Err(PasskeyError::UserVerificationRequired)
    } else {
        Ok(())
    }
}

fn decode_base64url(value: &str) -> Result<Vec<u8>, PasskeyError> {
    use base64::Engine;
    base64::engine::general_purpose::URL_SAFE_NO_PAD
        .decode(value)
        .map_err(|_| PasskeyError::InvalidRequest)
}

fn encode_cose_key(x: &[u8], y: &[u8]) -> Vec<u8> {
    let mut out = vec![0xa5, 0x01, 0x02, 0x03, 0x26, 0x20, 0x01, 0x21, 0x58, 0x20];
    out.extend_from_slice(x);
    out.extend_from_slice(&[0x22, 0x58, 0x20]);
    out.extend_from_slice(y);
    out
}

fn map_vault_error(error: VaultCrudError) -> PasskeyError {
    match error {
        VaultCrudError::Locked => PasskeyError::Locked,
        VaultCrudError::ItemNotFound => PasskeyError::NotFound,
        other => PasskeyError::Vault(other),
    }
}
