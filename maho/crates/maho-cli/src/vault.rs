use maho_agent::ZeroizedString;
use maho_core::maho_core::MahoCore;
use maho_core::oscrypt::{VaultDeviceBinding, VaultDevicePlatform, VaultDeviceProtector};
use maho_core::vault_manager::VaultManagerError;
use std::io;
use std::path::Path;

pub trait RecoverySecretReader {
    fn read_recovery_secret(&mut self) -> io::Result<ZeroizedString>;
}

pub trait RecoveryUnlocker {
    fn unlock_with_recovery(&mut self, recovery_material: &ZeroizedString) -> RecoveryUnlockResult;
}

#[derive(Debug, PartialEq, Eq)]
pub enum RecoveryUnlockResult {
    Unlocked,
    InvalidRecoveryMaterial,
    Failed,
}

pub struct TerminalRecoverySecretReader;

impl RecoverySecretReader for TerminalRecoverySecretReader {
    fn read_recovery_secret(&mut self) -> io::Result<ZeroizedString> {
        rpassword::prompt_password("Vault recovery material: ").map(ZeroizedString::new)
    }
}

pub struct ProfileRecoveryUnlocker<P> {
    core: MahoCore,
    binding: VaultDeviceBinding,
    protector: P,
}

impl<P> ProfileRecoveryUnlocker<P>
where
    P: VaultDeviceProtector,
{
    pub fn open(db_path: &Path, make_protector: impl FnOnce(&str, &str) -> P) -> io::Result<Self> {
        crate::workspace::open_storage(db_path).map_err(|_| profile_recovery_open_error())?;
        let profile_dir = db_path.parent().ok_or_else(profile_recovery_open_error)?;
        let mut core = MahoCore::new()
            .with_storage(&db_path.to_string_lossy())
            .with_lmdb_storage(&profile_dir.join("state"));
        if core.storage_ref().is_none() {
            return Err(profile_recovery_open_error());
        }
        core.load_state()
            .map_err(|_| profile_recovery_open_error())?;

        let profile_id = core
            .get_active_profile_id()
            .ok_or_else(profile_recovery_open_error)?
            .to_string();
        let device_id = core.sync_device_id();
        let binding =
            VaultDeviceBinding::new(&profile_id, &device_id, VaultDevicePlatform::Desktop);
        let protector = make_protector(&profile_id, &device_id);

        Ok(Self {
            core,
            binding,
            protector,
        })
    }
}

impl<P> RecoveryUnlocker for ProfileRecoveryUnlocker<P>
where
    P: VaultDeviceProtector,
{
    fn unlock_with_recovery(&mut self, recovery_material: &ZeroizedString) -> RecoveryUnlockResult {
        let rewrap = self.core.rewrap_vault_device_with_recovery(
            recovery_material.as_str().as_bytes(),
            &self.binding,
            &self.protector,
        );
        let locked = self.core.lock_vault();

        match (rewrap, locked) {
            (Ok(()), Ok(())) => RecoveryUnlockResult::Unlocked,
            (Err(VaultManagerError::InvalidCredentials), Ok(())) => {
                RecoveryUnlockResult::InvalidRecoveryMaterial
            }
            _ => RecoveryUnlockResult::Failed,
        }
    }
}

pub fn recover_with(
    reader: &mut dyn RecoverySecretReader,
    unlocker: &mut dyn RecoveryUnlocker,
) -> io::Result<RecoveryUnlockResult> {
    let recovery_material = reader.read_recovery_secret()?;
    Ok(unlocker.unlock_with_recovery(&recovery_material))
}

pub fn recover_from_terminal() -> io::Result<RecoveryUnlockResult> {
    let mut reader = TerminalRecoverySecretReader;
    let db_path = crate::workspace::default_db_path();

    #[cfg(target_os = "macos")]
    {
        let mut unlocker = ProfileRecoveryUnlocker::open(&db_path, |_, device_id| {
            maho_core::oscrypt::MacOsVaultDeviceProtector::for_device_id(device_id)
        })?;
        return recover_with(&mut reader, &mut unlocker);
    }

    #[cfg(target_os = "linux")]
    {
        let mut unlocker = ProfileRecoveryUnlocker::open(&db_path, |profile_id, device_id| {
            maho_core::oscrypt::LinuxVaultDeviceProtector::for_device(profile_id, device_id)
        })?;
        return recover_with(&mut reader, &mut unlocker);
    }

    #[cfg(target_os = "windows")]
    {
        let _ = (&mut reader, db_path);
        return Err(profile_recovery_open_error());
    }

    #[cfg(not(any(target_os = "macos", target_os = "linux", target_os = "windows")))]
    {
        let _ = (&mut reader, db_path);
        Err(profile_recovery_open_error())
    }
}

fn profile_recovery_open_error() -> io::Error {
    io::Error::other("Vault recovery could not start")
}
