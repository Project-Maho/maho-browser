//! Vault trust settings: inactivity auto-lock timeout and device
//! re-authentication requirement are persisted settings that drive Vault state.

use std::path::Path;

use chrono::{Duration, Utc};

use maho_core::maho_core::MahoCore;
use maho_types::autofill::AutofillSettingsUpdate;
use maho_types::settings::SettingsUpdate;
use maho_types::vault::VaultLockState;

const MASTER: &[u8] = b"vault-trust-settings-master-passphrase";
const RECOVERY: &[u8] = b"vault-trust-settings-recovery-secret";
const DEFAULT_AUTO_LOCK_MINUTES: u32 = 15;

fn autofill_update(update: AutofillSettingsUpdate) -> SettingsUpdate {
    SettingsUpdate {
        autofill: Some(update),
        ..Default::default()
    }
}

fn storage_backed_core(path: &Path) -> MahoCore {
    maho_storage::sqlite::set_sqlcipher_key("vault-trust-settings-test-key")
        .expect("configure key");
    MahoCore::new().with_storage(path.to_str().expect("utf8 path"))
}

#[test]
fn vault_trust_settings_default_like_bitwarden() {
    let core = MahoCore::new();
    let settings = core.get_settings();
    assert_eq!(
        settings.autofill.vault_auto_lock_minutes,
        DEFAULT_AUTO_LOCK_MINUTES
    );
    assert!(settings.autofill.vault_require_device_auth);
    assert_eq!(
        core.vault_status().auto_lock_minutes,
        DEFAULT_AUTO_LOCK_MINUTES
    );
}

#[test]
fn vault_trust_settings_partial_updates_preserve_the_other_setting() {
    let mut core = MahoCore::new();
    core.update_settings(autofill_update(AutofillSettingsUpdate {
        vault_auto_lock_minutes: Some(60),
        ..Default::default()
    }));
    assert_eq!(core.vault_status().auto_lock_minutes, 60);
    assert!(core.get_settings().autofill.vault_require_device_auth);

    core.update_settings(autofill_update(AutofillSettingsUpdate {
        vault_require_device_auth: Some(false),
        ..Default::default()
    }));
    assert_eq!(core.vault_status().auto_lock_minutes, 60);
    assert!(!core.get_settings().autofill.vault_require_device_auth);

    core.update_settings(autofill_update(AutofillSettingsUpdate {
        vault_auto_lock_minutes: Some(0),
        ..Default::default()
    }));
    assert_eq!(core.vault_status().auto_lock_minutes, 0);
    assert!(!core.get_settings().autofill.vault_require_device_auth);
}

#[test]
fn vault_auto_lock_timeout_zero_disables_and_minutes_drive_the_runtime() {
    let dir = tempfile::tempdir().expect("temp dir");
    let path = dir.path().join("vault-trust-settings.sqlite");
    let mut core = storage_backed_core(&path);

    core.update_settings(autofill_update(AutofillSettingsUpdate {
        vault_auto_lock_minutes: Some(0),
        ..Default::default()
    }));
    core.initialize_vault(MASTER, RECOVERY)
        .expect("initialize vault");
    assert_eq!(core.vault_status().lock_state, VaultLockState::Unlocked);
    assert!(!core.evaluate_vault_auto_lock_at(Utc::now() + Duration::days(2)));
    assert_eq!(core.vault_status().lock_state, VaultLockState::Unlocked);

    core.update_settings(autofill_update(AutofillSettingsUpdate {
        vault_auto_lock_minutes: Some(30),
        ..Default::default()
    }));
    assert!(!core.evaluate_vault_auto_lock_at(Utc::now() + Duration::minutes(29)));
    assert_eq!(core.vault_status().lock_state, VaultLockState::Unlocked);
    assert!(core.evaluate_vault_auto_lock_at(Utc::now() + Duration::minutes(31)));
    assert_eq!(core.vault_status().lock_state, VaultLockState::AutoLocked);
}

#[test]
fn vault_trust_settings_persist_across_reopen() {
    let dir = tempfile::tempdir().expect("temp dir");
    let path = dir.path().join("vault-trust-persistence.sqlite");
    {
        let mut core = storage_backed_core(&path);
        core.update_settings(autofill_update(AutofillSettingsUpdate {
            vault_auto_lock_minutes: Some(240),
            vault_require_device_auth: Some(false),
            ..Default::default()
        }));
    }

    let reopened = storage_backed_core(&path);
    assert_eq!(reopened.get_settings().autofill.vault_auto_lock_minutes, 240);
    assert!(!reopened.get_settings().autofill.vault_require_device_auth);
    assert_eq!(reopened.vault_status().auto_lock_minutes, 240);
}
