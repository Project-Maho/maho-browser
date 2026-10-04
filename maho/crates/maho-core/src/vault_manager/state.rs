//! Private runtime lifecycle for the Vault state machine.
//!
//! Callers are dynamic (FFI, shell events), so the lifecycle is a runtime enum
//! rather than a type-state machine. `Unlocking` is a transient, never-public
//! state: it maps fail-closed to [`VaultLockState::Locked`] so any observer of a
//! mid-unlock Vault treats it as unusable.

use maho_types::vault::VaultLockState;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) enum RuntimeState {
    Uninitialized,
    Locked,
    Unlocking,
    Unlocked,
    AutoLocked,
}

impl RuntimeState {
    /// Project the private runtime state onto the public, secret-free lock state.
    /// The transient `Unlocking` maps to `Locked` (fail-closed).
    pub(crate) const fn public_lock_state(self) -> VaultLockState {
        match self {
            Self::Uninitialized => VaultLockState::Uninitialized,
            Self::Locked | Self::Unlocking => VaultLockState::Locked,
            Self::Unlocked => VaultLockState::Unlocked,
            Self::AutoLocked => VaultLockState::AutoLocked,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn vault_state_unlocking_transient_maps_to_locked() {
        assert_eq!(
            RuntimeState::Unlocking.public_lock_state(),
            VaultLockState::Locked
        );
        assert_eq!(
            RuntimeState::Unlocked.public_lock_state(),
            VaultLockState::Unlocked
        );
        assert_eq!(
            RuntimeState::AutoLocked.public_lock_state(),
            VaultLockState::AutoLocked
        );
        assert_eq!(
            RuntimeState::Uninitialized.public_lock_state(),
            VaultLockState::Uninitialized
        );
    }
}
