// Copyright 2026 Maho Browser. All rights reserved.

//! Skill creation workflow from execution run journals.
//!
//! Converts successful run journals into proposed custom Skill manifests.
//! Invariant: No automatic installation—skills require explicit user approval
//! through the interaction broker or confirmation gate before installation.

use crate::interaction::{
    InteractionBroker, InteractionError, InteractionOption, InteractionRequestId,
};
use crate::run_journal::{AgentRunId, RunCheckpointKind, RunJournal};
use maho_core::skills_manager::{Skill, SkillsManager};
use serde::{Deserialize, Serialize};
use std::collections::HashSet;

/// Status of a proposed skill in the lifecycle.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum SkillActivationStatus {
    Proposed,
    Approved,
    Rejected,
    Installed,
    NotInstalled,
}

/// Proposed skill manifest generated from a run journal.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct SkillProposal {
    pub id: String,
    pub name: String,
    pub slash_command: String,
    pub description: String,
    pub trigger_prompt: String,
    pub system_prompt_template: String,
    pub allowed_tools: Vec<String>,
    pub steps_summary: Vec<String>,
    pub created_at: u64,
}

impl SkillProposal {
    /// Converts this proposal into a core `Skill` struct for installation.
    pub fn to_skill(&self) -> Skill {
        Skill {
            id: self.id.clone(),
            name: self.name.clone(),
            slash_command: self.slash_command.clone(),
            description: self.description.clone(),
            system_prompt: self.system_prompt_template.clone(),
            icon: "sparkles".to_string(),
            is_builtin: false,
            is_first_party: false,
            allowed_tools: self.allowed_tools.clone(),
            url_patterns: vec![],
            auto_inject: false,
            required_capability_id: None,
            user_handoff: true,
        }
    }
}

/// Outcome of submitting or activating a skill proposal.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct SkillProposalOutcome {
    pub proposal: SkillProposal,
    pub status: SkillActivationStatus,
    pub installed_skill_id: Option<String>,
    pub message: String,
}

/// Errors occurring during skill proposal generation or activation.
#[derive(Debug, Clone, PartialEq, Eq, thiserror::Error, Serialize, Deserialize)]
pub enum SkillCreatorError {
    #[error("Run journal for run '{0}' was not found")]
    RunNotFound(String),
    #[error("Cannot create skill from non-successful run (status: {0})")]
    RunNotSuccessful(String),
    #[error("Run journal contained no executable steps or user prompts to formulate a skill")]
    EmptyRunHistory,
    #[error("Interaction error: {0}")]
    InteractionError(String),
}

impl From<InteractionError> for SkillCreatorError {
    fn from(e: InteractionError) -> Self {
        Self::InteractionError(e.to_string())
    }
}

/// Analyzes a successful run journal and constructs a proposed skill manifest.
pub fn propose_skill_from_journal(
    run_id: &AgentRunId,
    journal: &RunJournal,
    custom_name: Option<&str>,
) -> Result<SkillProposal, SkillCreatorError> {
    let entries = journal
        .get_entries(run_id)
        .map_err(|_| SkillCreatorError::RunNotFound(run_id.0.clone()))?;

    // Check if run completed successfully
    let checkpoints = journal
        .get_checkpoints(run_id)
        .map_err(|_| SkillCreatorError::RunNotFound(run_id.0.clone()))?;

    let is_completed = checkpoints
        .iter()
        .any(|c| c.kind == RunCheckpointKind::Completed);

    if !is_completed {
        let terminal_kind = checkpoints.iter().find(|c| c.kind.is_terminal());
        let status_str = terminal_kind
            .map(|c| format!("{:?}", c.kind))
            .unwrap_or_else(|| "Running".to_string());
        return Err(SkillCreatorError::RunNotSuccessful(status_str));
    }

    if entries.is_empty() {
        return Err(SkillCreatorError::EmptyRunHistory);
    }

    let mut user_prompts = Vec::new();
    let mut steps_summary = Vec::new();
    let mut allowed_tools_set = HashSet::new();

    for entry in entries {
        let name = &entry.event_name;
        if let Ok(val) = serde_json::from_str::<serde_json::Value>(&entry.payload_json) {
            if let Some(user_input) = val
                .get("user_input")
                .or_else(|| val.get("prompt"))
                .and_then(|v| v.as_str())
            {
                user_prompts.push(user_input.to_string());
            } else if let Some(tool) = val
                .get("tool")
                .or_else(|| val.get("tool_call"))
                .and_then(|v| v.as_str())
            {
                allowed_tools_set.insert(tool.to_string());
                steps_summary.push(format!("Execute tool '{}'", tool));
            } else if name != "started"
                && name != "status"
                && name != "completed"
                && name != "user_prompt"
                && name != "user_input"
                && name != "prompt"
            {
                steps_summary.push(format!("Step '{}'", name));
            }
        }
    }

    let raw_trigger = user_prompts
        .first()
        .cloned()
        .unwrap_or_else(|| "Automated workflow".to_string());
    let skill_name = custom_name.map(|s| s.to_string()).unwrap_or_else(|| {
        format!(
            "Skill for {}",
            raw_trigger.chars().take(30).collect::<String>()
        )
    });

    let slug = skill_name
        .to_lowercase()
        .chars()
        .filter(|c| c.is_alphanumeric() || *c == '-' || *c == '_')
        .collect::<String>()
        .replace(' ', "-");
    let safe_id = if slug.is_empty() {
        format!("custom-skill-{}", run_id.0)
    } else {
        slug
    };

    let slash_command = format!("/{}", safe_id);
    let mut allowed_tools: Vec<String> = allowed_tools_set.into_iter().collect();
    allowed_tools.sort();

    let steps_text = if steps_summary.is_empty() {
        "1. Execute defined workflow steps".to_string()
    } else {
        steps_summary
            .iter()
            .enumerate()
            .map(|(i, s)| format!("{}. {}", i + 1, s))
            .collect::<Vec<_>>()
            .join("\n")
    };

    let system_prompt_template = format!(
        "When invoked with '{{user_input}}', follow these steps to accomplish the task:\n{}\nContext: {{page_content}}",
        steps_text
    );

    Ok(SkillProposal {
        id: safe_id,
        name: skill_name,
        slash_command,
        description: format!(
            "Automatically created skill from user workflow: {}",
            raw_trigger
        ),
        trigger_prompt: raw_trigger,
        system_prompt_template,
        allowed_tools,
        steps_summary,
        created_at: 0,
    })
}

/// Explicit approval-gated installation into the skill catalog.
///
/// If `approved` is true, the skill is registered in `SkillsManager`.
/// If `approved` is false, installation is rejected fail-closed with `NotInstalled`.
pub fn activate_skill_proposal(
    proposal: &SkillProposal,
    approved: bool,
    catalog: &mut SkillsManager,
) -> SkillProposalOutcome {
    if !approved {
        return SkillProposalOutcome {
            proposal: proposal.clone(),
            status: SkillActivationStatus::NotInstalled,
            installed_skill_id: None,
            message: "User rejected skill installation; skill was not activated".to_string(),
        };
    }

    let skill = proposal.to_skill();
    let skill_id = skill.id.clone();
    catalog.add_skill(skill);

    SkillProposalOutcome {
        proposal: proposal.clone(),
        status: SkillActivationStatus::Installed,
        installed_skill_id: Some(skill_id),
        message: "Skill approved and successfully installed into catalog".to_string(),
    }
}

/// Explicit approval-gated update to an existing skill in the catalog.
///
/// Invariant: Skill modification requires explicit approval. If `approved` is false
/// or if the skill is not found in the catalog, the update fails closed without mutation.
pub fn update_skill(
    proposal: &SkillProposal,
    approved: bool,
    catalog: &mut SkillsManager,
) -> SkillProposalOutcome {
    if !approved {
        return SkillProposalOutcome {
            proposal: proposal.clone(),
            status: SkillActivationStatus::NotInstalled,
            installed_skill_id: None,
            message: "User rejected skill update; skill was not modified".to_string(),
        };
    }

    if !catalog.contains_skill(&proposal.id) {
        return SkillProposalOutcome {
            proposal: proposal.clone(),
            status: SkillActivationStatus::NotInstalled,
            installed_skill_id: None,
            message: format!(
                "Cannot update skill '{}': skill does not exist in catalog",
                proposal.id
            ),
        };
    }

    let skill = proposal.to_skill();
    let skill_id = skill.id.clone();
    catalog.add_skill(skill);

    SkillProposalOutcome {
        proposal: proposal.clone(),
        status: SkillActivationStatus::Installed,
        installed_skill_id: Some(skill_id),
        message: "Skill update approved and successfully applied to catalog".to_string(),
    }
}

/// Initiates an approval question in the interaction broker for installing a proposed skill.
pub fn request_skill_approval_interaction(
    broker: &mut InteractionBroker,
    proposal: &SkillProposal,
) -> Result<InteractionRequestId, SkillCreatorError> {
    let question = format!(
        "Would you like to install the proposed skill '{}' ({}) with tools {:?}?",
        proposal.name, proposal.slash_command, proposal.allowed_tools
    );
    let options = vec![
        InteractionOption::new("install_approve", "Approve and Install"),
        InteractionOption::new("install_reject", "Reject"),
    ];

    let id = broker.ask_user_question(question, options)?;
    Ok(id)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::interaction::InteractionAnswer;

    #[test]
    fn test_skill_proposal_generation_from_run_journal() {
        let mut journal = RunJournal::new();
        let run_id = AgentRunId::new("run-skill-1");
        journal.start_run(run_id.clone(), "session-abc").unwrap();

        journal
            .append_event(
                &run_id,
                "user_prompt",
                r#"{"user_input":"Research Rust async runtimes"}"#,
            )
            .unwrap();
        journal
            .append_event(
                &run_id,
                "tool_call",
                r#"{"tool":"web_search","query":"rust async"}"#,
            )
            .unwrap();
        journal
            .append_event(
                &run_id,
                "tool_call",
                r#"{"tool":"fs_write","relative_path":"summary.md"}"#,
            )
            .unwrap();
        journal
            .record_terminal(&run_id, RunCheckpointKind::Completed, None)
            .unwrap();

        let proposal =
            propose_skill_from_journal(&run_id, &journal, Some("Async Research Skill")).unwrap();

        assert_eq!(proposal.name, "Async Research Skill");
        assert_eq!(proposal.trigger_prompt, "Research Rust async runtimes");
        assert_eq!(proposal.allowed_tools, vec!["fs_write", "web_search"]);
        assert_eq!(proposal.steps_summary.len(), 2);
        assert!(proposal.system_prompt_template.contains("web_search"));
        assert!(proposal.system_prompt_template.contains("{user_input}"));
    }

    #[test]
    fn test_skill_proposal_fails_on_unsuccessful_run() {
        let mut journal = RunJournal::new();
        let run_id = AgentRunId::new("run-fail");
        journal.start_run(run_id.clone(), "session-fail").unwrap();
        journal
            .append_event(&run_id, "user_prompt", r#"{"user_input":"Broken task"}"#)
            .unwrap();
        journal
            .record_terminal(&run_id, RunCheckpointKind::Failed, None)
            .unwrap();

        let err = propose_skill_from_journal(&run_id, &journal, None).unwrap_err();
        match err {
            SkillCreatorError::RunNotSuccessful(status) => {
                assert!(status.contains("Failed"));
            }
            other => panic!("Expected RunNotSuccessful, got {:?}", other),
        }
    }

    #[test]
    fn test_skill_approval_gate_rejected_fails_closed() {
        let mut catalog = SkillsManager::new();
        let proposal = SkillProposal {
            id: "test-skill".to_string(),
            name: "Test Skill".to_string(),
            slash_command: "/test-skill".to_string(),
            description: "Test description".to_string(),
            trigger_prompt: "Do test".to_string(),
            system_prompt_template: "Template".to_string(),
            allowed_tools: vec!["web_search".to_string()],
            steps_summary: vec!["Execute tool 'web_search'".to_string()],
            created_at: 0,
        };

        // User denies installation
        let outcome = activate_skill_proposal(&proposal, false, &mut catalog);
        assert_eq!(outcome.status, SkillActivationStatus::NotInstalled);
        assert!(outcome.installed_skill_id.is_none());
        assert!(!catalog.contains_skill("test-skill"));
    }

    #[test]
    fn test_skill_approval_gate_approved_installs_into_catalog() {
        let mut catalog = SkillsManager::new();
        let proposal = SkillProposal {
            id: "approved-skill".to_string(),
            name: "Approved Skill".to_string(),
            slash_command: "/approved-skill".to_string(),
            description: "Approved description".to_string(),
            trigger_prompt: "Do approved".to_string(),
            system_prompt_template: "Template".to_string(),
            allowed_tools: vec!["web_search".to_string()],
            steps_summary: vec!["Step 1".to_string()],
            created_at: 0,
        };

        // User approves installation
        let outcome = activate_skill_proposal(&proposal, true, &mut catalog);
        assert_eq!(outcome.status, SkillActivationStatus::Installed);
        assert_eq!(
            outcome.installed_skill_id,
            Some("approved-skill".to_string())
        );
        assert!(catalog.contains_skill("approved-skill"));

        let installed = catalog.get_skill("approved-skill").unwrap();
        assert_eq!(installed.name, "Approved Skill");
        assert_eq!(installed.allowed_tools, vec!["web_search"]);
    }

    #[test]
    fn test_skill_proposal_interaction_broker_roundtrip() {
        let mut broker = InteractionBroker::new();
        let mut catalog = SkillsManager::new();
        let proposal = SkillProposal {
            id: "interactive-skill".to_string(),
            name: "Interactive Skill".to_string(),
            slash_command: "/interactive-skill".to_string(),
            description: "Interactive".to_string(),
            trigger_prompt: "Interact".to_string(),
            system_prompt_template: "Template".to_string(),
            allowed_tools: vec!["fs_write".to_string()],
            steps_summary: vec!["Write step".to_string()],
            created_at: 0,
        };

        let req_id = request_skill_approval_interaction(&mut broker, &proposal).unwrap();

        // 1. First scenario: Deny
        broker
            .resolve(&req_id, InteractionAnswer::option("install_reject"))
            .unwrap();
        let req = broker.get(&req_id).unwrap();
        let is_approved = match &req.state {
            crate::interaction::InteractionState::Resolved(InteractionAnswer::SelectedOption(
                opt,
            )) => opt == "install_approve",
            _ => false,
        };
        let outcome1 = activate_skill_proposal(&proposal, is_approved, &mut catalog);
        assert_eq!(outcome1.status, SkillActivationStatus::NotInstalled);
        assert!(!catalog.contains_skill("interactive-skill"));

        // 2. Second scenario: Approve
        let req_id2 = request_skill_approval_interaction(&mut broker, &proposal).unwrap();
        broker
            .resolve(&req_id2, InteractionAnswer::option("install_approve"))
            .unwrap();
        let req2 = broker.get(&req_id2).unwrap();
        let is_approved2 = match &req2.state {
            crate::interaction::InteractionState::Resolved(InteractionAnswer::SelectedOption(
                opt,
            )) => opt == "install_approve",
            _ => false,
        };
        let outcome2 = activate_skill_proposal(&proposal, is_approved2, &mut catalog);
        assert_eq!(outcome2.status, SkillActivationStatus::Installed);
        assert!(catalog.contains_skill("interactive-skill"));
    }

    #[test]
    fn test_skill_create_and_update_lifecycle_roundtrip() {
        let mut catalog = SkillsManager::new();

        let initial_proposal = SkillProposal {
            id: "custom-summarizer".to_string(),
            name: "Summarizer Skill".to_string(),
            slash_command: "/summarizer".to_string(),
            description: "Initial description v1".to_string(),
            trigger_prompt: "Summarize this page".to_string(),
            system_prompt_template: "Template v1".to_string(),
            allowed_tools: vec!["web_search".to_string()],
            steps_summary: vec!["Step 1: Search".to_string()],
            created_at: 100,
        };

        // 1. Create and install skill
        let create_outcome = activate_skill_proposal(&initial_proposal, true, &mut catalog);
        assert_eq!(create_outcome.status, SkillActivationStatus::Installed);
        assert!(catalog.contains_skill("custom-summarizer"));
        assert_eq!(
            catalog.get_skill("custom-summarizer").unwrap().description,
            "Initial description v1"
        );

        // 2. Prepare updated proposal
        let mut updated_proposal = initial_proposal.clone();
        updated_proposal.description = "Updated description v2 with extra context".to_string();
        updated_proposal.system_prompt_template = "Template v2 modified".to_string();
        updated_proposal.allowed_tools = vec!["web_search".to_string(), "fs_write".to_string()];

        // 3. Update without approval -> Rejected fail-closed
        let denied_update = update_skill(&updated_proposal, false, &mut catalog);
        assert_eq!(denied_update.status, SkillActivationStatus::NotInstalled);
        // Verify catalog was NOT changed
        let current = catalog.get_skill("custom-summarizer").unwrap();
        assert_eq!(current.description, "Initial description v1");
        assert_eq!(current.allowed_tools, vec!["web_search"]);

        // 4. Update with approval -> Mutates existing skill
        let approved_update = update_skill(&updated_proposal, true, &mut catalog);
        assert_eq!(approved_update.status, SkillActivationStatus::Installed);
        let updated = catalog.get_skill("custom-summarizer").unwrap();
        assert_eq!(
            updated.description,
            "Updated description v2 with extra context"
        );
        assert_eq!(updated.system_prompt, "Template v2 modified");
        assert_eq!(updated.allowed_tools, vec!["web_search", "fs_write"]);

        // 5. Update non-existent skill -> Rejected fail-closed
        let ghost_proposal = SkillProposal {
            id: "ghost-skill".to_string(),
            name: "Ghost".to_string(),
            slash_command: "/ghost".to_string(),
            description: "Ghost".to_string(),
            trigger_prompt: "Ghost".to_string(),
            system_prompt_template: "Ghost".to_string(),
            allowed_tools: vec![],
            steps_summary: vec![],
            created_at: 0,
        };
        let ghost_update = update_skill(&ghost_proposal, true, &mut catalog);
        assert_eq!(ghost_update.status, SkillActivationStatus::NotInstalled);
        assert!(!catalog.contains_skill("ghost-skill"));
    }
}
