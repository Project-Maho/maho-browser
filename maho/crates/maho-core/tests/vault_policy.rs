use chrono::{Duration, Utc};
use maho_core::maho_core::MahoCore;
use maho_storage::sqlite::VaultPolicyRow;
use maho_types::vault::{
    CredentialOrigin, VaultAgentPolicy, VaultAuditOperation, VaultItemId, VaultPolicy,
    VaultSchemaVersion,
};

const MASTER: &[u8] = b"correct horse battery staple";
const RECOVERY: &[u8] = b"policy-recovery-secret";

fn core_at(dir: &tempfile::TempDir, name: &str) -> MahoCore {
    maho_storage::sqlite::set_sqlcipher_key("vault-policy-test-key").expect("configure key");
    let path = dir.path().join(name);
    MahoCore::new().with_storage(path.to_str().expect("utf8 path"))
}

fn initialized_core(dir: &tempfile::TempDir, name: &str) -> MahoCore {
    let mut core = core_at(dir, name);
    core.initialize_vault(MASTER, RECOVERY)
        .expect("initialize vault");
    core
}

fn default_policy(
    policy: VaultAgentPolicy,
    expires_at: Option<chrono::DateTime<Utc>>,
) -> VaultPolicy {
    VaultPolicy {
        schema_version: VaultSchemaVersion::CURRENT,
        policy,
        item_id: None,
        origin: None,
        expires_at,
    }
}

fn audit_operations(core: &MahoCore) -> Vec<String> {
    core.storage_ref()
        .expect("storage")
        .list_vault_audit_events()
        .expect("audit rows")
        .into_iter()
        .map(|row| row.operation)
        .collect()
}

#[test]
fn vault_policy_persists_across_core_restart_and_lock_unlock() {
    let dir = tempfile::tempdir().unwrap();
    let db_path = dir.path().join("policy.sqlite");
    let db_path = db_path.to_str().expect("utf8 path").to_string();
    let mut core = initialized_core(&dir, "policy.sqlite");

    let saved = core
        .vault_set_agent_policy(default_policy(VaultAgentPolicy::WhileUnlocked, None))
        .expect("set default policy");
    assert_eq!(saved.policy, VaultAgentPolicy::WhileUnlocked);

    core.lock_vault().expect("lock vault");
    assert_eq!(
        core.vault_agent_policy_status().unwrap().policy,
        VaultAgentPolicy::WhileUnlocked
    );
    core.unlock_vault(MASTER).expect("unlock vault");
    assert_eq!(
        core.vault_agent_policy_status().unwrap().policy,
        VaultAgentPolicy::WhileUnlocked
    );
    drop(core);

    maho_storage::sqlite::set_sqlcipher_key("vault-policy-test-key").expect("configure key");
    let mut reopened = MahoCore::new().with_storage(&db_path);
    reopened.load_persisted_data().unwrap();
    assert_eq!(
        reopened.vault_agent_policy_status().unwrap().policy,
        VaultAgentPolicy::WhileUnlocked
    );
}

#[test]
fn vault_policy_expires_and_falls_back_to_deny_first_default() {
    let dir = tempfile::tempdir().unwrap();
    let core = initialized_core(&dir, "expiry.sqlite");
    let origin = CredentialOrigin::try_from("https://expiry.example").unwrap();
    assert_eq!(
        core.resolve_vault_agent_policy(None, Some(&origin), Utc::now())
            .unwrap()
            .policy,
        VaultAgentPolicy::Deny
    );

    core.vault_set_agent_policy(default_policy(
        VaultAgentPolicy::AlwaysAllow,
        Some(Utc::now() - Duration::minutes(1)),
    ))
    .expect("persist expired policy");
    assert_eq!(
        core.resolve_vault_agent_policy(None, Some(&origin), Utc::now())
            .unwrap()
            .policy,
        VaultAgentPolicy::Deny
    );
}

#[test]
fn vault_policy_reads_storage_at_decision_time_and_supports_overrides() {
    let dir = tempfile::tempdir().unwrap();
    let core = initialized_core(&dir, "decision-time.sqlite");
    let item_id = VaultItemId::new();
    let origin = CredentialOrigin::try_from("https://decision.example").unwrap();
    core.vault_set_agent_policy(default_policy(VaultAgentPolicy::Deny, None))
        .unwrap();
    core.vault_set_agent_policy(VaultPolicy {
        schema_version: VaultSchemaVersion::CURRENT,
        policy: VaultAgentPolicy::WhileUnlocked,
        item_id: None,
        origin: Some(origin.clone()),
        expires_at: None,
    })
    .unwrap();
    core.vault_set_agent_policy(VaultPolicy {
        schema_version: VaultSchemaVersion::CURRENT,
        policy: VaultAgentPolicy::AskEveryUse,
        item_id: Some(item_id),
        origin: None,
        expires_at: Some(Utc::now() - Duration::minutes(1)),
    })
    .unwrap();

    assert_eq!(
        core.resolve_vault_agent_policy(Some(item_id), Some(&origin), Utc::now())
            .unwrap()
            .policy,
        VaultAgentPolicy::WhileUnlocked,
        "expired item override falls back to origin override"
    );

    core.storage_ref()
        .unwrap()
        .save_vault_policy(&VaultPolicyRow {
            scope: "default".to_string(),
            schema_version: 1,
            policy: "always_allow".to_string(),
            item_id: None,
            origin: None,
            expires_at: None,
            updated_at: Utc::now().to_rfc3339(),
        })
        .unwrap();
    assert_eq!(
        core.resolve_vault_agent_policy(None, None, Utc::now())
            .unwrap()
            .policy,
        VaultAgentPolicy::AlwaysAllow,
        "decision seam must reread storage, not a cached policy"
    );
}

#[test]
fn vault_policy_change_emits_secret_free_policy_audit() {
    let dir = tempfile::tempdir().unwrap();
    let core = initialized_core(&dir, "audit-policy.sqlite");

    core.vault_set_agent_policy(default_policy(VaultAgentPolicy::AskEveryUse, None))
        .expect("set policy");

    let rows = core
        .storage_ref()
        .unwrap()
        .list_vault_audit_events()
        .unwrap();
    assert_eq!(audit_operations(&core), vec!["policy_changed"]);
    assert_eq!(rows[0].policy.as_deref(), Some("ask_every_use"));
    assert!(rows[0].item_id.is_none());
    assert!(rows[0].item_alias.is_none());
    assert_eq!(
        rows[0].operation,
        serde_json::to_value(VaultAuditOperation::PolicyChanged)
            .unwrap()
            .as_str()
            .unwrap()
    );
}

#[test]
fn vault_policy_backend_absence_is_not_allow_by_default() {
    let core = MahoCore::new();

    let error = core.vault_agent_policy_status().unwrap_err().to_string();
    assert!(error.contains("storage"));
    assert_eq!(
        core.vault_status().agent_policy_default,
        VaultAgentPolicy::Deny
    );
}
