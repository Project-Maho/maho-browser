use maho_agent::vault_tools::{
    decide_vault_credential_request, LeaseValidationError, VaultCredentialLeaseManager,
    VaultCredentialRequestContext, VaultPolicyDecision,
};
use maho_types::vault::{VaultAgentPolicy, VaultLockState};

fn context(lock_state: VaultLockState, explicitly_approved: bool) -> VaultCredentialRequestContext {
    VaultCredentialRequestContext {
        vault_available: true,
        lock_state,
        executor_context_present: true,
        explicitly_approved,
    }
}

#[test]
fn policy_lock_and_approval_matrix_decides_lease_issuance() {
    // (policy, allowed when unlocked + approved, allowed when unlocked without approval)
    let policies = [
        (VaultAgentPolicy::Deny, false, false),
        (VaultAgentPolicy::AskEveryUse, true, false),
        (VaultAgentPolicy::AllowForTask, true, false),
        (VaultAgentPolicy::WhileUnlocked, true, true),
        (VaultAgentPolicy::AlwaysAllow, true, true),
    ];
    for (policy, with_approval, without_approval) in policies {
        for (lock_state, unlocked) in [
            (VaultLockState::Unlocked, true),
            (VaultLockState::Locked, false),
            (VaultLockState::AutoLocked, false),
            (VaultLockState::Uninitialized, false),
        ] {
            for explicitly_approved in [false, true] {
                let expected = unlocked
                    && if explicitly_approved {
                        with_approval
                    } else {
                        without_approval
                    };
                let actual = decide_vault_credential_request(
                    policy,
                    context(lock_state, explicitly_approved),
                );
                assert_eq!(
                    actual,
                    if expected {
                        VaultPolicyDecision::Allowed
                    } else {
                        VaultPolicyDecision::Denied
                    },
                    "policy={policy:?}, lock={lock_state:?}, approved={explicitly_approved}"
                );
            }
        }
    }
}

#[test]
fn unavailable_vault_or_missing_context_denies_every_policy() {
    for policy in [
        VaultAgentPolicy::Deny,
        VaultAgentPolicy::AskEveryUse,
        VaultAgentPolicy::AllowForTask,
        VaultAgentPolicy::WhileUnlocked,
        VaultAgentPolicy::AlwaysAllow,
    ] {
        let mut unavailable = context(VaultLockState::Unlocked, true);
        unavailable.vault_available = false;
        assert_eq!(
            decide_vault_credential_request(policy, unavailable),
            VaultPolicyDecision::Denied
        );

        let mut missing_context = context(VaultLockState::Unlocked, true);
        missing_context.executor_context_present = false;
        assert_eq!(
            decide_vault_credential_request(policy, missing_context),
            VaultPolicyDecision::Denied
        );
    }
}

#[test]
fn denied_request_does_not_issue_a_lease() {
    let mut manager = VaultCredentialLeaseManager::new(300);
    assert_eq!(
        manager.grant_lease_for_request(
            "item_abc",
            "https://trusted.example",
            VaultAgentPolicy::Deny,
            context(VaultLockState::Unlocked, true),
        ),
        Err(LeaseValidationError::PolicyDenied)
    );
    assert_eq!(
        manager.validate_and_consume("vh_nonexistent", "https://trusted.example"),
        Err(LeaseValidationError::ExpiredOrNotFound)
    );
}

#[test]
fn allowed_request_issues_single_use_lease_only_after_approval() {
    let mut manager = VaultCredentialLeaseManager::new(300);
    let handle = manager
        .grant_lease_for_request(
            "item_abc",
            "https://trusted.example",
            VaultAgentPolicy::AskEveryUse,
            context(VaultLockState::Unlocked, true),
        )
        .expect("explicitly approved request should receive a lease");
    assert!(manager
        .validate_and_consume(&handle.handle_id, "https://trusted.example")
        .is_ok());
    assert_eq!(
        manager.validate_and_consume(&handle.handle_id, "https://trusted.example"),
        Err(LeaseValidationError::ExpiredOrNotFound)
    );
}
