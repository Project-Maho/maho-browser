//! Password vault entry insertion helper for `MahoCoreDestination`.

use maho_core::maho_core::MahoCore;
use maho_types::vault::{CredentialOrigin, VaultItemKind, VaultItemPublicMetadata};

pub(super) fn add_password(core: &mut MahoCore, entry: maho_import::PasswordEntry) -> bool {
    if entry.origin_url.is_empty() {
        return false;
    }
    let origin = if let Ok(origin) = CredentialOrigin::try_from(entry.origin_url.as_str()) {
        origin
    } else {
        return false;
    };
    let input = maho_core::vault_manager::VaultLoginInput {
        metadata: VaultItemPublicMetadata {
            favorite: false,
            trashed_at: None,
            has_notes: false,
            title: entry.origin_url,
            origins: vec![origin],
            username_hint: entry.username.clone(),
            item_kind: VaultItemKind::Login,
            totp: None,
            passkey: None,
        },
        username: entry.username,
        password: zeroize::Zeroizing::new(entry.password),
        form_details: None,
    };
    core.vault_add_login(input).is_ok()
}
