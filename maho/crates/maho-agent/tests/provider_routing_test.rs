use maho_agent::swiftide_backend::{resolve_byok_client, LlmProviderKind};
use maho_agent::{AgentError, SecureKeyBundle, ZeroizedString};
use std::sync::{Arc, Mutex};

fn anthropic_only_callback(provider: &str) -> Result<SecureKeyBundle, AgentError> {
    if provider == "anthropic" || provider == "claude" {
        Ok(SecureKeyBundle {
            key: ZeroizedString::new("sk-ant-test".to_string()),
            base_url: None,
            model: None,
        })
    } else {
        Err(AgentError::ExecutionError(format!(
            "provider {provider} not selected"
        )))
    }
}

fn override_ignores_arg_callback(_provider: &str) -> Result<SecureKeyBundle, AgentError> {
    Ok(SecureKeyBundle {
        key: ZeroizedString::new("sk-ant-test".to_string()),
        base_url: None,
        model: None,
    })
}

#[test]
fn provider_anthropic_builds_anthropic_client() {
    let (_client, kind) = resolve_byok_client(Some("anthropic"), &anthropic_only_callback, "")
        .expect("anthropic BYOK client should build");
    assert_eq!(
        kind,
        LlmProviderKind::Anthropic,
        "--provider anthropic must build an Anthropic client, never an OpenAI client"
    );
}

#[test]
fn provider_anthropic_builds_anthropic_even_when_callback_ignores_arg() {
    let (_client, kind) =
        resolve_byok_client(Some("anthropic"), &override_ignores_arg_callback, "")
            .expect("anthropic BYOK client should build");
    assert_eq!(
        kind,
        LlmProviderKind::Anthropic,
        "preferred provider must be probed first even if the callback ignores its argument"
    );
}

#[test]
fn ffi_default_probe_falls_through_to_anthropic_callback_family() {
    // Given: an FFI-created backend has no preferred provider, while the secure
    // callback has an available credential only for the Anthropic family.
    let probes = Arc::new(Mutex::new(Vec::new()));
    let callback_probes = Arc::clone(&probes);
    let callback = move |provider: &str| {
        callback_probes.lock().unwrap().push(provider.to_string());
        if provider == "anthropic" {
            Ok(SecureKeyBundle {
                key: ZeroizedString::new("test-key".to_string()),
                base_url: None,
                model: Some("test-model".to_string()),
            })
        } else {
            Err(AgentError::ExecutionError(
                "credential unavailable for provider family".to_string(),
            ))
        }
    };

    // When: the BYOK resolver uses the FFI session's unset provider preference.
    let (_client, kind) = resolve_byok_client(None, &callback, "resolved-model")
        .expect("the Anthropic callback family has an available test credential");

    // Then: OpenAI is probed first, then the callback selects Anthropic and the
    // client family follows the callback that supplied the credential.
    assert_eq!(*probes.lock().unwrap(), vec!["openai", "anthropic"]);
    assert_eq!(kind, LlmProviderKind::Anthropic);
}

#[test]
fn selected_provider_does_not_fall_back_to_another_provider() {
    // Given: the selected provider has no credential while another provider does.
    let probes = Arc::new(Mutex::new(Vec::new()));
    let callback_probes = Arc::clone(&probes);
    let callback = move |provider: &str| {
        callback_probes.lock().unwrap().push(provider.to_string());
        if provider == "openai" {
            Ok(SecureKeyBundle::from_key(ZeroizedString::new(
                "openai-only-test-key".to_string(),
            )))
        } else {
            Err(AgentError::ExecutionError(
                "selected provider has no test credential".to_string(),
            ))
        }
    };

    // When: Anthropic is explicitly selected.
    let result = resolve_byok_client(Some("anthropic"), &callback, "resolved-model");

    // Then: no cross-provider fallback is attempted.
    assert!(result.is_err());
    assert_eq!(*probes.lock().unwrap(), vec!["anthropic"]);
}
