use serde::{Deserialize, Serialize};

use crate::passwords::PasswordProviderKind;

pub use crate::passwords::PasswordProviderKind as PasswordAutofillProvider;

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct AutofillAddress {
    pub id: String,
    pub name: String,
    pub street: String,
    pub city: String,
    pub state: String,
    pub zip: String,
    pub country: String,
    pub phone: Option<String>,
    pub email: Option<String>,
    #[serde(default)]
    pub address_line2: Option<String>,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct AutofillPayment {
    pub id: String,
    pub card_name: String,
    pub last_four: String,
    pub expiry: String,
    #[serde(default)]
    pub card_network: Option<String>,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct AutofillSettings {
    pub addresses_enabled: bool,
    pub payments_enabled: bool,
    #[serde(default = "default_true")]
    pub passwords_enabled: bool,
    #[serde(default)]
    pub password_provider: PasswordAutofillProvider,
    /// Vault inactivity auto-lock timeout in minutes. `0` disables the
    /// inactivity auto-lock entirely (the Vault stays unlocked until it is
    /// locked explicitly or the process exits).
    #[serde(default = "default_vault_auto_lock_minutes")]
    pub vault_auto_lock_minutes: u32,
    /// Whether password/Vault operations require a fresh device
    /// re-authentication (Touch ID / platform authenticator) before each use.
    #[serde(default = "default_true")]
    pub vault_require_device_auth: bool,
}

fn default_true() -> bool {
    true
}

/// Mirrors `DEFAULT_AUTO_LOCK_MINUTES` in `maho-core`'s `VaultManager` so a
/// profile without persisted settings behaves like a fresh Vault.
fn default_vault_auto_lock_minutes() -> u32 {
    15
}

impl Default for AutofillSettings {
    fn default() -> Self {
        Self {
            addresses_enabled: true,
            payments_enabled: true,
            passwords_enabled: true,
            password_provider: PasswordProviderKind::default(),
            vault_auto_lock_minutes: default_vault_auto_lock_minutes(),
            vault_require_device_auth: true,
        }
    }
}

#[derive(Clone, Debug, Serialize, Deserialize, Default)]
#[serde(rename_all = "camelCase")]
pub struct AutofillSettingsUpdate {
    #[serde(skip_serializing_if = "Option::is_none")]
    pub addresses_enabled: Option<bool>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub payments_enabled: Option<bool>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub passwords_enabled: Option<bool>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub password_provider: Option<PasswordAutofillProvider>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub vault_auto_lock_minutes: Option<u32>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub vault_require_device_auth: Option<bool>,
}
