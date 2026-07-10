use maho_types::ai::{AiProfile, AiWorkspace, ResolvedConfig, SessionConfig};

pub const DEFAULT_MODEL: &str = "gpt-4o";

/// Precedence: session override > agent profile > workspace default
pub fn resolve_config(
    session_override: Option<&SessionConfig>,
    profile: &AiProfile,
    workspace: &AiWorkspace,
) -> ResolvedConfig {
    let _ws_id = &workspace.profile_id;
    ResolvedConfig {
        system_prompt: session_override
            .and_then(|s| s.system_prompt.as_ref())
            .or(Some(&profile.system_prompt))
            .cloned()
            .unwrap_or_default(),
        model: session_override
            .and_then(|s| s.model.as_ref())
            .or(profile.preferred_model.as_ref())
            .cloned()
            .unwrap_or_else(|| DEFAULT_MODEL.to_string()),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_resolve_config_precedence() {
        let ws = AiWorkspace {
            id: "ws".into(),
            name: "ws".into(),
            profile_id: Some("blank".into()),
            space_id: None,
            workspace_root: None,
            created_at: "".into(),
            updated_at: "".into(),
        };

        let profile = AiProfile {
            id: "blank".into(),
            name: "blank".into(),
            system_prompt: "profile prompt".into(),
            preferred_model: Some("profile-model".into()),
            tools: vec![],
            mcp_servers: vec![],
            is_default: true,
            created_at: "".into(),
            updated_at: "".into(),
        };

        // 1. All override
        let over = SessionConfig {
            system_prompt: Some("session prompt".into()),
            model: Some("session-model".into()),
        };
        let resolved = resolve_config(Some(&over), &profile, &ws);
        assert_eq!(resolved.system_prompt, "session prompt");
        assert_eq!(resolved.model, "session-model");

        // 2. Profile fallback
        let resolved = resolve_config(None, &profile, &ws);
        assert_eq!(resolved.system_prompt, "profile prompt");
        assert_eq!(resolved.model, "profile-model");

        // 3. System default fallback
        let mut profile_no_model = profile.clone();
        profile_no_model.preferred_model = None;
        let resolved = resolve_config(None, &profile_no_model, &ws);
        assert_eq!(resolved.model, DEFAULT_MODEL);
    }
}
