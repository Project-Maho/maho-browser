use maho_agent::swiftide_backend::{resolve_byok_client, LlmProviderKind};
use maho_agent::{AgentError, SecureKeyBundle, ZeroizedString};

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
