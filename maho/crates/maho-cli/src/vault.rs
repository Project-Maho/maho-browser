use maho_agent::ZeroizedString;
use maho_core::maho_core::MahoCore;
use maho_core::oscrypt::{VaultDeviceBinding, VaultDevicePlatform, VaultDeviceProtector};
use maho_core::vault_manager::VaultManagerError;
use std::io;
use std::io::IsTerminal;
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

impl TerminalRecoverySecretReader {
    /// Reads the recovery material, given whether an interactive terminal is
    /// available. Split from the terminal probe so the non-interactive
    /// branch is directly testable without a controlling TTY.
    ///
    /// `rpassword::prompt_password` opens `/dev/tty` directly rather than
    /// reading stdin, so redirecting or nulling stdin does not stop it: with
    /// no human present it blocks forever. Probe first and fail fast.
    fn read_recovery_secret_when(&mut self, interactive: bool) -> io::Result<ZeroizedString> {
        if !interactive {
            return Err(terminal_unavailable_error());
        }
        rpassword::prompt_password("Vault recovery material: ").map(ZeroizedString::new)
    }
}

/// True only when a real interactive terminal is available to prompt on.
/// Both stdin and stderr are required: stdin because a piped/cron/CI session
/// has no human to answer, stderr because the prompt itself must be visible.
fn interactive_terminal_available() -> bool {
    io::stdin().is_terminal() && io::stderr().is_terminal()
}

/// The fail-fast error for a non-interactive `maho vault recover`. Names the
/// missing precondition and the resolution, because this fires precisely in
/// automated contexts where nobody is watching a prompt. It deliberately
/// offers no argv/env alternative: recovery material is accepted ONLY from
/// the interactive prompt, so that it never lands in a shell history, a
/// process listing, or a CI log.
fn terminal_unavailable_error() -> io::Error {
    io::Error::new(
        io::ErrorKind::NotConnected,
        "`maho vault recover` needs an interactive terminal to prompt for recovery \
         material, and none is attached (stdin or stderr is redirected). Recovery \
         material is accepted only at the prompt — never from a flag, an argument, \
         or an environment variable — so it cannot be supplied non-interactively. \
         Re-run this command directly in a terminal.",
    )
}

impl RecoverySecretReader for TerminalRecoverySecretReader {
    fn read_recovery_secret(&mut self) -> io::Result<ZeroizedString> {
        self.read_recovery_secret_when(interactive_terminal_available())
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
    // Probe before touching the profile store: with no terminal this command
    // can only fail, and opening the vault/profile state first would spend
    // real work (and emit core log noise) on the way to that failure.
    if !interactive_terminal_available() {
        return Err(terminal_unavailable_error());
    }

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

#[cfg(test)]
mod tests {
    use super::*;

    /// `maho vault recover` must never block waiting for a human when no
    /// interactive terminal exists. `rpassword` reads `/dev/tty` directly
    /// (rpassword `unix.rs`: `DEFAULT_INPUT_PATH = "/dev/tty"`), so a
    /// null/redirected stdin does NOT stop it: under cron, CI, a pipe, or an
    /// agent the process hung forever with no timeout and no diagnostic.
    /// The reader must fail fast instead.
    #[test]
    fn non_interactive_terminal_fails_fast_with_actionable_error() {
        let error = super::terminal_unavailable_error();
        assert_eq!(error.kind(), io::ErrorKind::NotConnected);
        let message = error.to_string();
        // The message has to tell the operator what happened and what to do,
        // since this now surfaces in exactly the automated contexts where
        // nobody is watching a prompt.
        assert!(
            message.contains("interactive terminal"),
            "error must name the missing precondition, got: {message}"
        );
        assert!(
            message.contains("maho vault recover"),
            "error must name the command that requires a terminal, got: {message}"
        );
    }

    /// The security property this command is built around: recovery material
    /// is read ONLY from the interactive terminal prompt. The fail-fast path
    /// must not become a backdoor that reads argv or the environment.
    #[test]
    fn fail_fast_path_never_reads_recovery_material_from_environment_or_argv() {
        const SENTINEL: &str = "S3NTINEL-maho-vault-9F4C";
        // SAFETY: single-threaded test process mutation, removed below.
        unsafe { std::env::set_var("MAHO_VAULT_RECOVERY_SECRET", SENTINEL) };

        let mut reader = TerminalRecoverySecretReader;
        let result = reader.read_recovery_secret_when(false);

        unsafe { std::env::remove_var("MAHO_VAULT_RECOVERY_SECRET") };

        let error = result.expect_err("a non-interactive session must not yield a secret");
        assert!(
            !error.to_string().contains(SENTINEL),
            "the environment sentinel must never reach the error surface"
        );
    }
}
