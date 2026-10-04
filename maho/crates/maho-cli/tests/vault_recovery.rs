use maho_agent::ZeroizedString;
use maho_cli::vault::{
    recover_with, ProfileRecoveryUnlocker, RecoverySecretReader, RecoveryUnlockResult,
    RecoveryUnlocker,
};
use maho_core::maho_core::MahoCore;
use maho_core::oscrypt::{
    VaultDeviceBinding, VaultDevicePlatform, VaultDeviceProtector, VaultDeviceProtectorError,
};
use maho_storage::sqlite::SqliteStorage;
use std::io;
use std::path::PathBuf;
use std::process::{Command, Stdio};
use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::{Arc, Mutex};
use tempfile::TempDir;
use zeroize::Zeroizing;

const SENTINEL: &str = "S3NTINEL-maho-vault-9F4C";
static PROFILE_STORAGE_LOCK: Mutex<()> = Mutex::new(());

fn maho_cmd() -> Command {
    let mut command = Command::new(env!("CARGO_BIN_EXE_maho"));
    command.env("MAHO_CLI_TEST", "1");
    command.stdin(Stdio::null());
    command
}

struct FixedRecoverySecretReader {
    secret: Option<ZeroizedString>,
}

impl FixedRecoverySecretReader {
    fn new(secret: &str) -> Self {
        Self {
            secret: Some(ZeroizedString::new(secret.to_owned())),
        }
    }
}

impl RecoverySecretReader for FixedRecoverySecretReader {
    fn read_recovery_secret(&mut self) -> io::Result<ZeroizedString> {
        self.secret.take().ok_or_else(|| {
            io::Error::new(
                io::ErrorKind::UnexpectedEof,
                "test reader has no remaining recovery material",
            )
        })
    }
}

#[derive(Default)]
struct FakeRecoveryUnlocker {
    expected_recovery_material: String,
    device_row_writes: usize,
    provider_state_writes: usize,
}

#[derive(Clone)]
struct TestDeviceProtector {
    secret: Vec<u8>,
    device_secret_calls: Arc<AtomicUsize>,
}

impl TestDeviceProtector {
    fn new(secret: &[u8]) -> Self {
        Self {
            secret: secret.to_vec(),
            device_secret_calls: Arc::new(AtomicUsize::new(0)),
        }
    }

    fn call_count_handle(&self) -> Arc<AtomicUsize> {
        Arc::clone(&self.device_secret_calls)
    }
}

impl VaultDeviceProtector for TestDeviceProtector {
    fn device_secret(&self) -> Result<Zeroizing<Vec<u8>>, VaultDeviceProtectorError> {
        self.device_secret_calls.fetch_add(1, Ordering::SeqCst);
        Ok(Zeroizing::new(self.secret.clone()))
    }
}

struct ProfileVaultFixture {
    _profile_dir: TempDir,
    db_path: PathBuf,
    profile_id: String,
    device_id: String,
    recovery_material: &'static str,
    original_device_row: Vec<u8>,
}

fn profile_vault_fixture() -> ProfileVaultFixture {
    let profile_dir = tempfile::tempdir().expect("create profile directory");
    let db_path = profile_dir.path().join("maho.db");
    maho_cli::workspace::open_storage(&db_path).expect("configure profile storage");
    let mut core = MahoCore::new()
        .with_storage(&db_path.to_string_lossy())
        .with_lmdb_storage(&profile_dir.path().join("state"));
    core.load_state().expect("hydrate test profile");
    core.save_state().expect("persist profile identity");

    let profile_id = core
        .get_active_profile_id()
        .expect("active test profile")
        .to_string();
    let device_id = core.sync_device_id();
    let binding = VaultDeviceBinding::new(&profile_id, &device_id, VaultDevicePlatform::Desktop);
    let recovery_material = "profile-owned-recovery-material";
    let protector = TestDeviceProtector::new(b"original-device-protector");
    core.initialize_vault_with_device(
        b"test-master-passphrase",
        recovery_material.as_bytes(),
        &binding,
        &protector,
    )
    .expect("create profile-owned vault");
    core.lock_vault().expect("lock test vault");

    let original_device_row = core
        .storage_ref()
        .expect("profile storage")
        .get_vault_device_key(&device_id)
        .expect("read device row")
        .expect("device row")
        .wrapped_key;
    drop(core);

    ProfileVaultFixture {
        _profile_dir: profile_dir,
        db_path,
        profile_id,
        device_id,
        recovery_material,
        original_device_row,
    }
}

impl FakeRecoveryUnlocker {
    fn expecting(recovery_material: &str) -> Self {
        Self {
            expected_recovery_material: recovery_material.to_owned(),
            ..Self::default()
        }
    }
}

impl RecoveryUnlocker for FakeRecoveryUnlocker {
    fn unlock_with_recovery(&mut self, recovery_material: &ZeroizedString) -> RecoveryUnlockResult {
        if recovery_material.as_str() != self.expected_recovery_material {
            return RecoveryUnlockResult::InvalidRecoveryMaterial;
        }

        self.device_row_writes += 1;
        self.provider_state_writes += 1;
        RecoveryUnlockResult::Unlocked
    }
}

#[test]
fn vault_recover_rejects_recovery_secret_flag_and_positional_material() {
    let help_output = maho_cmd()
        .args(["vault", "recover", "--help"])
        .output()
        .expect("run maho vault recover --help");
    let flag_output = maho_cmd()
        .args(["vault", "recover", "--recovery-secret", "via-flag"])
        .output()
        .expect("run maho vault recover with forbidden flag");
    let positional_output = maho_cmd()
        .args(["vault", "recover", "via-positional"])
        .output()
        .expect("run maho vault recover with forbidden positional material");

    let help_stdout = String::from_utf8_lossy(&help_output.stdout);
    assert!(help_output.status.success());
    assert!(!help_stdout.contains("recovery-secret"));
    assert!(!help_stdout.contains("<RECOVERY"));
    assert!(!flag_output.status.success());
    assert!(!positional_output.status.success());
}

#[test]
fn vault_recover_ignores_environment_recovery_material_and_never_leaks_it() {
    let output = maho_cmd()
        .env("MAHO_VAULT_RECOVERY_SECRET", SENTINEL)
        .args(["vault", "recover"])
        .output()
        .expect("run maho vault recover with forbidden environment material");
    let stdout = String::from_utf8_lossy(&output.stdout);
    let stderr = String::from_utf8_lossy(&output.stderr);

    assert!(!output.status.success());
    assert!(!stdout.contains(SENTINEL));
    assert!(!stderr.contains(SENTINEL));
}

#[test]
fn vault_wrong_recovery_material_from_injected_reader_does_not_mutate_device_or_provider_state() {
    let mut reader = FixedRecoverySecretReader::new(SENTINEL);
    let mut unlocker = FakeRecoveryUnlocker::expecting("correct recovery material");

    let result = recover_with(&mut reader, &mut unlocker).expect("read injected recovery material");

    assert_eq!(result, RecoveryUnlockResult::InvalidRecoveryMaterial);
    assert_eq!(unlocker.device_row_writes, 0);
    assert_eq!(unlocker.provider_state_writes, 0);
}

#[test]
fn vault_injected_recovery_material_is_not_present_in_command_arguments_or_terminal_output() {
    let args = ["maho", "vault", "recover"];
    let mut reader = FixedRecoverySecretReader::new(SENTINEL);
    let mut unlocker = FakeRecoveryUnlocker::expecting("correct recovery material");

    let result = recover_with(&mut reader, &mut unlocker).expect("read injected recovery material");

    assert_eq!(result, RecoveryUnlockResult::InvalidRecoveryMaterial);
    assert!(args.iter().all(|argument| *argument != SENTINEL));
}

#[test]
fn vault_profile_recovery_rewraps_the_active_device_without_exposing_recovery_material() {
    // Given a locked Vault owned by a persisted profile and a recovery reader.
    let _storage_guard = PROFILE_STORAGE_LOCK
        .lock()
        .unwrap_or_else(|error| error.into_inner());
    let fixture = profile_vault_fixture();
    let mut reader = FixedRecoverySecretReader::new(fixture.recovery_material);
    let replacement_protector = TestDeviceProtector::new(b"replacement-device-protector");
    let expected_profile_id = fixture.profile_id.clone();
    let expected_device_id = fixture.device_id.clone();
    let mut unlocker =
        ProfileRecoveryUnlocker::open(&fixture.db_path, move |profile_id, device_id| {
            assert_eq!(profile_id, expected_profile_id);
            assert_eq!(device_id, expected_device_id);
            replacement_protector
        })
        .expect("open locked profile Vault");

    // When recovery uses the injected material.
    let result = recover_with(&mut reader, &mut unlocker).expect("read injected recovery material");

    // Then the profile's device wrapper is replaced and the material is absent from profile data.
    let storage =
        SqliteStorage::open(&fixture.db_path.to_string_lossy()).expect("open profile storage");
    let rewrapped = storage
        .get_vault_device_key(&fixture.device_id)
        .expect("read rewrapped device row")
        .expect("rewrapped device row");
    let profile_bytes = std::fs::read(&fixture.db_path).expect("read profile database");

    assert_eq!(result, RecoveryUnlockResult::Unlocked);
    assert_ne!(rewrapped.wrapped_key, fixture.original_device_row);
    assert!(!profile_bytes
        .windows(fixture.recovery_material.len())
        .any(|bytes| bytes == fixture.recovery_material.as_bytes()));
}

#[test]
fn vault_profile_recovery_with_wrong_material_preserves_the_device_wrapper() {
    // Given a locked profile-owned Vault and a replacement protector.
    let _storage_guard = PROFILE_STORAGE_LOCK
        .lock()
        .unwrap_or_else(|error| error.into_inner());
    let fixture = profile_vault_fixture();
    let mut reader = FixedRecoverySecretReader::new("wrong recovery material");
    let replacement_protector = TestDeviceProtector::new(b"replacement-device-protector");
    let provider_call_count = replacement_protector.call_count_handle();
    let mut unlocker =
        ProfileRecoveryUnlocker::open(&fixture.db_path, move |_, _| replacement_protector)
            .expect("open locked profile Vault");

    // When the supplied material is wrong.
    let result = recover_with(&mut reader, &mut unlocker).expect("read injected recovery material");

    // Then no device wrapper mutation is committed.
    let storage =
        SqliteStorage::open(&fixture.db_path.to_string_lossy()).expect("open profile storage");
    let device_row = storage
        .get_vault_device_key(&fixture.device_id)
        .expect("read device row")
        .expect("device row");

    assert_eq!(result, RecoveryUnlockResult::InvalidRecoveryMaterial);
    assert_eq!(device_row.wrapped_key, fixture.original_device_row);
    assert_eq!(provider_call_count.load(Ordering::SeqCst), 0);
}
