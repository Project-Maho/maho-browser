// Copyright 2026 Maho Browser. All rights reserved.

//! Model routing policy contracts based on task classification.
//!
//! Routes requests to optimal models per category with explicit fallback reasons.

use serde::{Deserialize, Serialize};
use std::collections::HashMap;

/// Task category classified from user intent or agent execution context.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum TaskCategory {
    Chat,
    GeneralChat,
    Code,
    Coding,
    Research,
    DeepResearch,
    FastReasoning,
    Vision,
    TabAutomation,
    ToolUse,
}

/// Routing decision indicating the selected model and reasoning.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct RoutingDecision {
    pub category: TaskCategory,
    pub model: String,
    pub selected_model: String,
    pub fallback_model: Option<String>,
    pub reason: String,
    pub confidence: f32,
    pub is_fallback: bool,
}

impl RoutingDecision {
    pub fn model(&self) -> &str {
        &self.model
    }

    pub fn reason(&self) -> &str {
        &self.reason
    }

    pub fn is_fallback(&self) -> bool {
        self.is_fallback
    }
}

/// Typed error when model routing cannot resolve an available model.
#[derive(Debug, Clone, PartialEq, Eq, thiserror::Error, Serialize, Deserialize)]
pub enum ModelRoutingError {
    #[error("No candidate models are currently available for category {category:?}: {reason}")]
    TypedUnavailable {
        category: TaskCategory,
        attempted: Vec<String>,
        reason: String,
    },
    #[error("Model unavailable: {0}")]
    Unavailable(String),
}

/// Configuration mapping categories to default and fallback model names.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct ModelRoutingConfig {
    pub default_model_by_category: HashMap<TaskCategory, String>,
    pub fallback_model_by_category: HashMap<TaskCategory, String>,
}

impl Default for ModelRoutingConfig {
    fn default() -> Self {
        let mut default_map = HashMap::new();
        let mut fallback_map = HashMap::new();

        default_map.insert(
            TaskCategory::Chat,
            "google/gemini-3-flash-lite:free".to_string(),
        );
        default_map.insert(
            TaskCategory::GeneralChat,
            "google/gemini-3-flash-lite:free".to_string(),
        );
        default_map.insert(
            TaskCategory::Code,
            "anthropic/claude-3-7-sonnet".to_string(),
        );
        default_map.insert(
            TaskCategory::Coding,
            "anthropic/claude-3-7-sonnet".to_string(),
        );
        default_map.insert(
            TaskCategory::Research,
            "anthropic/claude-3-7-sonnet".to_string(),
        );
        default_map.insert(
            TaskCategory::DeepResearch,
            "anthropic/claude-3-7-sonnet".to_string(),
        );
        default_map.insert(
            TaskCategory::FastReasoning,
            "google/gemini-3-flash-lite:free".to_string(),
        );
        default_map.insert(
            TaskCategory::Vision,
            "google/gemini-3-flash-lite:free".to_string(),
        );
        default_map.insert(
            TaskCategory::TabAutomation,
            "anthropic/claude-3-7-sonnet".to_string(),
        );
        default_map.insert(
            TaskCategory::ToolUse,
            "anthropic/claude-3-7-sonnet".to_string(),
        );

        fallback_map.insert(
            TaskCategory::Chat,
            "anthropic/claude-3-7-sonnet".to_string(),
        );
        fallback_map.insert(
            TaskCategory::GeneralChat,
            "anthropic/claude-3-7-sonnet".to_string(),
        );
        fallback_map.insert(
            TaskCategory::Code,
            "google/gemini-3-flash-lite:free".to_string(),
        );
        fallback_map.insert(
            TaskCategory::Coding,
            "google/gemini-3-flash-lite:free".to_string(),
        );
        fallback_map.insert(
            TaskCategory::Research,
            "google/gemini-3-flash-lite:free".to_string(),
        );
        fallback_map.insert(
            TaskCategory::DeepResearch,
            "google/gemini-3-flash-lite:free".to_string(),
        );
        fallback_map.insert(
            TaskCategory::FastReasoning,
            "anthropic/claude-3-7-sonnet".to_string(),
        );
        fallback_map.insert(
            TaskCategory::Vision,
            "anthropic/claude-3-7-sonnet".to_string(),
        );
        fallback_map.insert(
            TaskCategory::TabAutomation,
            "google/gemini-3-flash-lite:free".to_string(),
        );
        fallback_map.insert(
            TaskCategory::ToolUse,
            "google/gemini-3-flash-lite:free".to_string(),
        );

        Self {
            default_model_by_category: default_map,
            fallback_model_by_category: fallback_map,
        }
    }
}

/// Routing policy resolving optimal model per category with preference and fallback rules.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize, Default)]
pub struct RoutingPolicy {
    pub config: ModelRoutingConfig,
}

impl RoutingPolicy {
    pub fn new(config: ModelRoutingConfig) -> Self {
        Self { config }
    }

    /// Pure function routing resolution based on category, user preference, and model availability.
    pub fn resolve(
        category: TaskCategory,
        preference: Option<String>,
        available: &[String],
    ) -> Result<RoutingDecision, ModelRoutingError> {
        let available_refs: Vec<&str> = available.iter().map(|s| s.as_str()).collect();
        Self::default().resolve_with_availability(category, preference.as_deref(), &available_refs)
    }

    /// Pure function routing resolution accepting string slices for availability.
    pub fn resolve_refs(
        category: TaskCategory,
        preference: Option<&str>,
        available: &[&str],
    ) -> Result<RoutingDecision, ModelRoutingError> {
        Self::default().resolve_with_availability(category, preference, available)
    }

    /// Instance resolution method with custom policy configuration.
    pub fn resolve_decision(
        &self,
        category: TaskCategory,
        preference: Option<String>,
        available: &[String],
    ) -> Result<RoutingDecision, ModelRoutingError> {
        let available_refs: Vec<&str> = available.iter().map(|s| s.as_str()).collect();
        self.resolve_with_availability(category, preference.as_deref(), &available_refs)
    }

    /// Resolves the routing decision against available models.
    pub fn resolve_with_availability(
        &self,
        category: TaskCategory,
        preference: Option<&str>,
        available: &[&str],
    ) -> Result<RoutingDecision, ModelRoutingError> {
        let mut candidates_to_try: Vec<String> = Vec::new();
        let preferred = preference.and_then(|p| {
            let trimmed = p.trim();
            if trimmed.is_empty() {
                None
            } else {
                Some(trimmed.to_string())
            }
        });

        if let Some(ref pref) = preferred {
            candidates_to_try.push(pref.clone());
        }

        if let Some(def) = self.config.default_model_by_category.get(&category) {
            if !candidates_to_try.contains(def) {
                candidates_to_try.push(def.clone());
            }
        }

        if let Some(fb) = self.config.fallback_model_by_category.get(&category) {
            if !candidates_to_try.contains(fb) {
                candidates_to_try.push(fb.clone());
            }
        }

        let global_default = "google/gemini-3-flash-lite:free".to_string();
        if !candidates_to_try.contains(&global_default) {
            candidates_to_try.push(global_default);
        }

        let mut attempted = Vec::new();

        for candidate in candidates_to_try {
            attempted.push(candidate.clone());
            let is_candidate_available = available
                .iter()
                .any(|avail| *avail == candidate || avail.eq_ignore_ascii_case(&candidate));

            if is_candidate_available {
                let (is_fallback, fallback_model, reason, confidence) = match preferred {
                    Some(ref pref) if *pref == candidate => (
                        false,
                        None,
                        format!("Selected preferred model '{}' for category {:?}", candidate, category),
                        1.0,
                    ),
                    Some(ref pref) => (
                        true,
                        Some(candidate.clone()),
                        format!(
                            "Preferred model '{}' is unavailable in active provider list; fell back to '{}' for category {:?}",
                            pref, candidate, category
                        ),
                        0.9,
                    ),
                    None => {
                        let is_cat_default = self
                            .config
                            .default_model_by_category
                            .get(&category)
                            .map_or(false, |d| *d == candidate);

                        if is_cat_default {
                            (
                                false,
                                None,
                                format!("Selected category default model '{}' for category {:?}", candidate, category),
                                1.0,
                            )
                        } else {
                            (
                                true,
                                Some(candidate.clone()),
                                format!(
                                    "Category default model is unavailable; fell back to '{}' for category {:?}",
                                    candidate, category
                                ),
                                0.85,
                            )
                        }
                    }
                };

                return Ok(RoutingDecision {
                    category,
                    model: candidate.clone(),
                    selected_model: candidate,
                    fallback_model,
                    reason,
                    confidence,
                    is_fallback,
                });
            }
        }

        let reason = if available.is_empty() {
            "No models are currently available in the active provider list".to_string()
        } else {
            format!(
                "None of the candidate models ({}) are available in active provider list ({})",
                attempted.join(", "),
                available.join(", ")
            )
        };

        Err(ModelRoutingError::TypedUnavailable {
            category,
            attempted,
            reason,
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_category_default_mapping() {
        let available = vec![
            "google/gemini-3-flash-lite:free".to_string(),
            "anthropic/claude-3-7-sonnet".to_string(),
        ];

        // Chat -> default chat model
        let chat_decision = RoutingPolicy::resolve(TaskCategory::Chat, None, &available)
            .expect("should resolve chat default");
        assert_eq!(
            chat_decision.selected_model,
            "google/gemini-3-flash-lite:free"
        );
        assert_eq!(chat_decision.model, "google/gemini-3-flash-lite:free");
        assert!(!chat_decision.is_fallback);
        assert!(!chat_decision.reason.is_empty());

        // GeneralChat -> default chat model
        let gen_chat_decision = RoutingPolicy::resolve(TaskCategory::GeneralChat, None, &available)
            .expect("should resolve general chat default");
        assert_eq!(
            gen_chat_decision.selected_model,
            "google/gemini-3-flash-lite:free"
        );
        assert_eq!(gen_chat_decision.model, "google/gemini-3-flash-lite:free");
        assert!(!gen_chat_decision.is_fallback);
        assert!(!gen_chat_decision.reason.is_empty());

        // Research -> research-default
        let research_decision = RoutingPolicy::resolve(TaskCategory::Research, None, &available)
            .expect("should resolve research default");
        assert_eq!(
            research_decision.selected_model,
            "anthropic/claude-3-7-sonnet"
        );
        assert_eq!(research_decision.model, "anthropic/claude-3-7-sonnet");
        assert!(!research_decision.is_fallback);
        assert!(!research_decision.reason.is_empty());

        // DeepResearch -> research-default
        let deep_research_decision =
            RoutingPolicy::resolve(TaskCategory::DeepResearch, None, &available)
                .expect("should resolve deep research default");
        assert_eq!(
            deep_research_decision.selected_model,
            "anthropic/claude-3-7-sonnet"
        );
        assert_eq!(deep_research_decision.model, "anthropic/claude-3-7-sonnet");
        assert!(!deep_research_decision.is_fallback);
        assert!(!deep_research_decision.reason.is_empty());

        // Code -> code-default
        let code_decision = RoutingPolicy::resolve(TaskCategory::Code, None, &available)
            .expect("should resolve code default");
        assert_eq!(code_decision.selected_model, "anthropic/claude-3-7-sonnet");
        assert_eq!(code_decision.model, "anthropic/claude-3-7-sonnet");
        assert!(!code_decision.is_fallback);
        assert!(!code_decision.reason.is_empty());

        // Coding -> code-default
        let coding_decision = RoutingPolicy::resolve(TaskCategory::Coding, None, &available)
            .expect("should resolve coding default");
        assert_eq!(
            coding_decision.selected_model,
            "anthropic/claude-3-7-sonnet"
        );
        assert_eq!(coding_decision.model, "anthropic/claude-3-7-sonnet");
        assert!(!coding_decision.is_fallback);
        assert!(!coding_decision.reason.is_empty());
    }

    #[test]
    fn test_preferred_model_stored_in_profile_prefs_wins_over_category_default() {
        let available = vec![
            "google/gemini-3-flash-lite:free".to_string(),
            "anthropic/claude-3-7-sonnet".to_string(),
            "openai/gpt-4o".to_string(),
        ];

        let decision = RoutingPolicy::resolve(
            TaskCategory::Chat,
            Some("openai/gpt-4o".to_string()),
            &available,
        )
        .expect("preferred model should resolve");

        assert_eq!(decision.selected_model, "openai/gpt-4o");
        assert_eq!(decision.model, "openai/gpt-4o");
        assert!(!decision.is_fallback);
        assert!(!decision.reason.is_empty());
    }

    #[test]
    fn test_preferred_model_unavailable_falls_back_to_category_default_with_reason() {
        let available_strs = &[
            "google/gemini-3-flash-lite:free",
            "anthropic/claude-3-7-sonnet",
        ];

        let decision = RoutingPolicy::resolve_refs(
            TaskCategory::Chat,
            Some("openai/gpt-4o-nonexistent"),
            available_strs,
        )
        .expect("should fall back to category default");

        assert_eq!(decision.selected_model, "google/gemini-3-flash-lite:free");
        assert_eq!(decision.model, "google/gemini-3-flash-lite:free");
        assert!(decision.is_fallback, "Must be flagged as fallback");
        assert_eq!(
            decision.fallback_model.as_deref(),
            Some("google/gemini-3-flash-lite:free")
        );
        // Adversarial class: misleading_success_output (reason recorded on fallback — asserted non-empty)
        assert!(
            !decision.reason.is_empty(),
            "Fallback reason must be recorded and non-empty"
        );
        assert!(
            decision.reason.contains("openai/gpt-4o-nonexistent")
                && decision.reason.contains("unavailable"),
            "Fallback reason must explain why fallback happened: got '{}'",
            decision.reason
        );
    }

    #[test]
    fn test_no_available_candidate_returns_typed_unavailable_error() {
        let available: Vec<String> = vec!["unrelated-unsupported-model".to_string()];

        let result = RoutingPolicy::resolve(
            TaskCategory::Code,
            Some("preferred-offline-model".to_string()),
            &available,
        );

        match result {
            Err(ModelRoutingError::TypedUnavailable {
                category,
                attempted,
                reason,
            }) => {
                assert_eq!(category, TaskCategory::Code);
                assert!(attempted.contains(&"preferred-offline-model".to_string()));
                assert!(attempted.contains(&"anthropic/claude-3-7-sonnet".to_string()));
                assert!(
                    !reason.is_empty(),
                    "Reason must describe why no candidate is available"
                );
            }
            Err(other) => panic!("Expected TypedUnavailable error variant, got {:?}", other),
            Ok(decision) => panic!(
                "Expected failure on no available models, but got silent pick {:?}",
                decision
            ),
        }
    }

    #[test]
    fn test_empty_available_list_returns_typed_unavailable() {
        let available: Vec<String> = vec![];

        let result = RoutingPolicy::resolve(TaskCategory::GeneralChat, None, &available);

        match result {
            Err(ModelRoutingError::TypedUnavailable {
                category,
                attempted,
                reason,
            }) => {
                assert_eq!(category, TaskCategory::GeneralChat);
                assert!(!attempted.is_empty());
                assert!(!reason.is_empty());
            }
            Err(other) => panic!("Expected TypedUnavailable, got {:?}", other),
            Ok(decision) => panic!(
                "Expected failure on empty available list, got {:?}",
                decision
            ),
        }
    }

    #[test]
    fn test_category_default_unavailable_falls_back_to_secondary_fallback() {
        // Default for Code is claude-3-7-sonnet, fallback is gemini-3-flash-lite:free
        let available = vec!["google/gemini-3-flash-lite:free".to_string()];

        let decision = RoutingPolicy::resolve(TaskCategory::Code, None, &available)
            .expect("should resolve secondary fallback");

        assert_eq!(decision.selected_model, "google/gemini-3-flash-lite:free");
        assert!(decision.is_fallback);
        assert!(!decision.reason.is_empty());
        assert_eq!(
            decision.fallback_model.as_deref(),
            Some("google/gemini-3-flash-lite:free")
        );
    }

    #[test]
    fn test_custom_model_routing_config() {
        let mut custom_config = ModelRoutingConfig::default();
        custom_config
            .default_model_by_category
            .insert(TaskCategory::Chat, "custom/llama-3-70b".to_string());

        let policy = RoutingPolicy::new(custom_config);
        let available = vec!["custom/llama-3-70b".to_string()];

        let decision = policy
            .resolve_decision(TaskCategory::Chat, None, &available)
            .expect("should resolve with custom config");

        assert_eq!(decision.selected_model, "custom/llama-3-70b");
        assert!(!decision.is_fallback);
    }
}
