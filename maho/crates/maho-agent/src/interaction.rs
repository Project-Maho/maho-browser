// Copyright 2026 Maho Browser. All rights reserved.

//! Interaction tools and state machine types for Agent-User dialogues.
//!
//! Distinct from permission policy approval, interaction requests represent
//! round-trip interactive questions (`ask_user_question`) and action confirmations
//! (`request_action_confirmation`) with suspend/resume/timeout/cancel semantics.

use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::sync::atomic::{AtomicU64, Ordering};

/// Unique identifier for an interaction request.
#[derive(Debug, Clone, PartialEq, Eq, Hash, Serialize, Deserialize)]
pub struct InteractionRequestId(pub String);

pub type InteractionId = InteractionRequestId;

impl InteractionRequestId {
    pub fn new(id: impl Into<String>) -> Self {
        Self(id.into())
    }

    pub fn from_u64(seq: u64) -> Self {
        Self(format!("interaction-req-{seq}"))
    }

    pub fn as_str(&self) -> &str {
        &self.0
    }
}

impl std::fmt::Display for InteractionRequestId {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}", self.0)
    }
}

/// A structured selectable option for multiple-choice questions.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct InteractionOption {
    pub id: String,
    pub label: String,
    pub description: Option<String>,
}

impl InteractionOption {
    pub fn new(id: impl Into<String>, label: impl Into<String>) -> Self {
        Self {
            id: id.into(),
            label: label.into(),
            description: None,
        }
    }

    pub fn with_description(
        id: impl Into<String>,
        label: impl Into<String>,
        description: impl Into<String>,
    ) -> Self {
        Self {
            id: id.into(),
            label: label.into(),
            description: Some(description.into()),
        }
    }
}

/// The kind of interaction requested from the user.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(tag = "kind", rename_all = "snake_case")]
pub enum InteractionKind {
    Question {
        question: String,
        options: Vec<InteractionOption>,
    },
    Confirmation {
        effect_description: String,
    },
}

/// The structured answer or resolution payload provided by the user.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(tag = "answer_kind", rename_all = "snake_case")]
pub enum InteractionAnswer {
    Text(String),
    SelectedOption(String),
    Confirmed,
    Denied,
}

impl InteractionAnswer {
    pub fn text(s: impl Into<String>) -> Self {
        Self::Text(s.into())
    }

    pub fn option(id: impl Into<String>) -> Self {
        Self::SelectedOption(id.into())
    }

    pub fn is_confirmed(&self) -> bool {
        matches!(self, Self::Confirmed)
    }

    pub fn is_denied(&self) -> bool {
        matches!(self, Self::Denied)
    }
}

impl From<&str> for InteractionAnswer {
    fn from(s: &str) -> Self {
        Self::Text(s.to_string())
    }
}

impl From<String> for InteractionAnswer {
    fn from(s: String) -> Self {
        Self::Text(s)
    }
}

/// State of an interaction request in the state machine.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(tag = "state", content = "payload", rename_all = "snake_case")]
pub enum InteractionState {
    Pending,
    Resolved(InteractionAnswer),
    Denied,
    Expired,
    Cancelled,
}

impl InteractionState {
    pub fn is_pending(&self) -> bool {
        matches!(self, Self::Pending)
    }

    pub fn is_resolved(&self) -> bool {
        matches!(self, Self::Resolved(_))
    }

    pub fn is_denied(&self) -> bool {
        matches!(self, Self::Denied)
    }

    pub fn is_expired(&self) -> bool {
        matches!(self, Self::Expired)
    }

    pub fn is_cancelled(&self) -> bool {
        matches!(self, Self::Cancelled)
    }

    pub fn is_terminal(&self) -> bool {
        !matches!(self, Self::Pending)
    }
}

/// An interaction request created by an agent tool.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct InteractionRequest {
    pub id: InteractionRequestId,
    pub kind: InteractionKind,
    pub state: InteractionState,
}

/// Errors that can occur during interaction lifecycle management.
#[derive(Debug, Clone, PartialEq, Eq, thiserror::Error)]
pub enum InteractionError {
    #[error("Question prompt cannot be empty or whitespace")]
    EmptyQuestion,
    #[error("Multiple-choice question must provide at least one option")]
    EmptyOptions,
    #[error("Option id and label cannot be empty or whitespace")]
    InvalidOption,
    #[error("Action confirmation effect description cannot be empty or whitespace")]
    EmptyEffectDescription,
    #[error("Interaction request not found: {0}")]
    NotFound(InteractionRequestId),
    #[error("Interaction request {id} is already in terminal state {state:?}")]
    AlreadyTerminal {
        id: InteractionRequestId,
        state: InteractionState,
    },
}

/// Synchronous state-machine broker for managing interactive Agent-User dialogs.
#[derive(Debug, Default)]
pub struct InteractionBroker {
    requests: HashMap<InteractionRequestId, InteractionRequest>,
    counter: AtomicU64,
}

impl InteractionBroker {
    pub fn new() -> Self {
        Self {
            requests: HashMap::new(),
            counter: AtomicU64::new(1),
        }
    }

    /// Create an `ask_user_question` interaction request.
    ///
    /// Fails closed if the question prompt or any option is empty/whitespace.
    pub fn ask_user_question(
        &mut self,
        question: impl Into<String>,
        options: Vec<InteractionOption>,
    ) -> Result<InteractionRequestId, InteractionError> {
        let q = question.into();
        if q.trim().is_empty() {
            return Err(InteractionError::EmptyQuestion);
        }
        if options.is_empty() {
            return Err(InteractionError::EmptyOptions);
        }
        for opt in &options {
            if opt.id.trim().is_empty() || opt.label.trim().is_empty() {
                return Err(InteractionError::InvalidOption);
            }
        }

        let next_seq = self.counter.fetch_add(1, Ordering::SeqCst);
        let id = InteractionRequestId::from_u64(next_seq);
        let req = InteractionRequest {
            id: id.clone(),
            kind: InteractionKind::Question {
                question: q,
                options,
            },
            state: InteractionState::Pending,
        };
        self.requests.insert(id.clone(), req);
        Ok(id)
    }

    /// Create a `request_action_confirmation` interaction request.
    ///
    /// Fails closed if effect description is empty/whitespace.
    pub fn request_action_confirmation(
        &mut self,
        effect_description: impl Into<String>,
    ) -> Result<InteractionRequestId, InteractionError> {
        let desc = effect_description.into();
        if desc.trim().is_empty() {
            return Err(InteractionError::EmptyEffectDescription);
        }

        let next_seq = self.counter.fetch_add(1, Ordering::SeqCst);
        let id = InteractionRequestId::from_u64(next_seq);
        let req = InteractionRequest {
            id: id.clone(),
            kind: InteractionKind::Confirmation {
                effect_description: desc,
            },
            state: InteractionState::Pending,
        };
        self.requests.insert(id.clone(), req);
        Ok(id)
    }

    /// Resolve a pending interaction request with a user answer.
    ///
    /// If the answer is `InteractionAnswer::Denied`, transitions directly to `Denied`.
    /// Otherwise transitions to `Resolved(answer)`.
    pub fn resolve(
        &mut self,
        id: &InteractionRequestId,
        answer: impl Into<InteractionAnswer>,
    ) -> Result<(), InteractionError> {
        let req = self
            .requests
            .get_mut(id)
            .ok_or_else(|| InteractionError::NotFound(id.clone()))?;

        if req.state.is_terminal() {
            return Err(InteractionError::AlreadyTerminal {
                id: id.clone(),
                state: req.state.clone(),
            });
        }

        let ans = answer.into();
        if ans.is_denied() {
            req.state = InteractionState::Denied;
        } else {
            req.state = InteractionState::Resolved(ans);
        }
        Ok(())
    }

    /// Explicitly deny a pending action confirmation.
    pub fn deny(&mut self, id: &InteractionRequestId) -> Result<(), InteractionError> {
        let req = self
            .requests
            .get_mut(id)
            .ok_or_else(|| InteractionError::NotFound(id.clone()))?;

        if req.state.is_terminal() {
            return Err(InteractionError::AlreadyTerminal {
                id: id.clone(),
                state: req.state.clone(),
            });
        }

        req.state = InteractionState::Denied;
        Ok(())
    }

    /// Timeout an interaction request (fail-closed, never auto-approves).
    pub fn timeout(&mut self, id: &InteractionRequestId) -> Result<(), InteractionError> {
        let req = self
            .requests
            .get_mut(id)
            .ok_or_else(|| InteractionError::NotFound(id.clone()))?;

        if req.state.is_terminal() {
            return Err(InteractionError::AlreadyTerminal {
                id: id.clone(),
                state: req.state.clone(),
            });
        }

        req.state = InteractionState::Expired;
        Ok(())
    }

    /// Cancel an interaction request (session termination / turn steer).
    pub fn cancel(&mut self, id: &InteractionRequestId) -> Result<(), InteractionError> {
        let req = self
            .requests
            .get_mut(id)
            .ok_or_else(|| InteractionError::NotFound(id.clone()))?;

        if req.state.is_terminal() {
            return Err(InteractionError::AlreadyTerminal {
                id: id.clone(),
                state: req.state.clone(),
            });
        }

        req.state = InteractionState::Cancelled;
        Ok(())
    }

    /// Get a reference to an interaction request by id.
    pub fn get(&self, id: &InteractionRequestId) -> Option<&InteractionRequest> {
        self.requests.get(id)
    }

    pub fn get_request(&self, id: &InteractionRequestId) -> Option<&InteractionRequest> {
        self.get(id)
    }

    /// Get a mutable reference to an interaction request by id.
    pub fn get_mut(&mut self, id: &InteractionRequestId) -> Option<&mut InteractionRequest> {
        self.requests.get_mut(id)
    }

    /// Check if the active turn can resume based on the interaction state.
    ///
    /// Only returns true if the request is resolved with an accepted answer / confirmation.
    /// Denied, Expired, Cancelled, and Pending requests return false.
    pub fn can_resume(&self, id: &InteractionRequestId) -> bool {
        self.requests.get(id).map_or(false, |req| match &req.state {
            InteractionState::Resolved(ans) => !ans.is_denied(),
            _ => false,
        })
    }

    /// Check if an action confirmation was explicitly confirmed by the user.
    pub fn is_confirmed(&self, id: &InteractionRequestId) -> bool {
        self.requests.get(id).map_or(false, |req| match &req.state {
            InteractionState::Resolved(ans) => ans.is_confirmed(),
            _ => false,
        })
    }

    /// List all currently pending interaction requests.
    pub fn pending_requests(&self) -> Vec<&InteractionRequest> {
        self.requests
            .values()
            .filter(|req| req.state.is_pending())
            .collect()
    }

    /// Check if any interaction request is pending.
    pub fn has_pending(&self) -> bool {
        self.requests.values().any(|req| req.state.is_pending())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_interaction_ask_user_question_resolves_and_sets_same_turn_resume_marker() {
        let mut broker = InteractionBroker::new();
        let options = vec![
            InteractionOption::new("opt_prod", "Production"),
            InteractionOption::new("opt_staging", "Staging"),
        ];
        let id = broker
            .ask_user_question("Which deployment target should we use?", options.clone())
            .expect("Valid question request should be created");

        let req = broker.get(&id).expect("Request must exist in broker");
        assert_eq!(req.id, id);
        assert_eq!(req.state, InteractionState::Pending);
        match &req.kind {
            InteractionKind::Question {
                question,
                options: opts,
            } => {
                assert_eq!(question, "Which deployment target should we use?");
                assert_eq!(opts.len(), 2);
            }
            _ => panic!("Expected InteractionKind::Question"),
        }

        // Turn cannot resume while pending
        assert!(!broker.can_resume(&id));

        // User answers the question
        broker
            .resolve(&id, InteractionAnswer::option("opt_prod"))
            .expect("Resolve on pending question must succeed");

        let resolved_req = broker.get(&id).expect("Request must exist");
        assert_eq!(
            resolved_req.state,
            InteractionState::Resolved(InteractionAnswer::option("opt_prod"))
        );
        // Same turn resume marker must be set
        assert!(broker.can_resume(&id));
    }

    #[test]
    fn test_interaction_request_action_confirmation_deny_and_approve() {
        let mut broker = InteractionBroker::new();

        // Case 1: Denied confirmation -> No side effect executed
        let id_deny = broker
            .request_action_confirmation("Drop database production_db_v2")
            .expect("Valid confirmation request should be created");

        let mut side_effect_executed = false;
        broker
            .resolve(&id_deny, InteractionAnswer::Denied)
            .expect("Denying request must succeed");

        let req_deny = broker.get(&id_deny).expect("Request must exist");
        assert_eq!(req_deny.state, InteractionState::Denied);
        assert!(!broker.can_resume(&id_deny));
        assert!(!broker.is_confirmed(&id_deny));

        if broker.can_resume(&id_deny) && broker.is_confirmed(&id_deny) {
            side_effect_executed = true;
        }
        assert!(
            !side_effect_executed,
            "Side effect must never execute on Denied state"
        );

        // Case 2: Approved confirmation -> Confirmed, caller executes side-effect
        let id_approve = broker
            .request_action_confirmation("Restart web worker daemon")
            .expect("Valid confirmation request should be created");

        broker
            .resolve(&id_approve, InteractionAnswer::Confirmed)
            .expect("Approving request must succeed");

        let req_approve = broker.get(&id_approve).expect("Request must exist");
        assert_eq!(
            req_approve.state,
            InteractionState::Resolved(InteractionAnswer::Confirmed)
        );
        assert!(broker.can_resume(&id_approve));
        assert!(broker.is_confirmed(&id_approve));

        if broker.can_resume(&id_approve) && broker.is_confirmed(&id_approve) {
            side_effect_executed = true;
        }
        assert!(
            side_effect_executed,
            "Side effect executes on Confirmed state"
        );
    }

    #[test]
    fn test_interaction_timeout_fails_closed_never_auto_approves() {
        let mut broker = InteractionBroker::new();
        let id = broker
            .request_action_confirmation("Delete all inactive users")
            .expect("Valid confirmation request should be created");

        broker
            .timeout(&id)
            .expect("Timeout transition must succeed");

        let req = broker.get(&id).expect("Request must exist");
        assert_eq!(req.state, InteractionState::Expired);
        // Fail-closed invariants
        assert!(!broker.can_resume(&id), "Expired request must not resume");
        assert!(
            !broker.is_confirmed(&id),
            "Expired request must never auto-approve"
        );

        // Attempting to resolve after timeout fails
        let res = broker.resolve(&id, InteractionAnswer::Confirmed);
        assert!(matches!(res, Err(InteractionError::AlreadyTerminal { .. })));
    }

    #[test]
    fn test_interaction_cancel_cancels_with_no_resume() {
        let mut broker = InteractionBroker::new();
        let id = broker
            .ask_user_question(
                "Select environment",
                vec![InteractionOption::new("a", "Option A")],
            )
            .expect("Valid question request");

        broker.cancel(&id).expect("Cancel transition must succeed");

        let req = broker.get(&id).expect("Request must exist");
        assert_eq!(req.state, InteractionState::Cancelled);
        assert!(!broker.can_resume(&id), "Cancelled request must not resume");

        // Attempting to resolve after cancel fails
        let res = broker.resolve(&id, InteractionAnswer::text("test"));
        assert!(matches!(res, Err(InteractionError::AlreadyTerminal { .. })));
    }

    #[test]
    fn test_interaction_adversarial_cancel_resume_probes() {
        let mut broker = InteractionBroker::new();

        // Probe 1: Expired request cannot be cancelled, resolved, or denied
        let id1 = broker
            .request_action_confirmation("Action 1")
            .expect("created");
        broker.timeout(&id1).expect("timeout");
        assert!(matches!(
            broker.cancel(&id1),
            Err(InteractionError::AlreadyTerminal {
                state: InteractionState::Expired,
                ..
            })
        ));
        assert!(matches!(
            broker.resolve(&id1, InteractionAnswer::Confirmed),
            Err(InteractionError::AlreadyTerminal {
                state: InteractionState::Expired,
                ..
            })
        ));

        // Probe 2: Cancelled request cannot be timed out or resolved
        let id2 = broker
            .request_action_confirmation("Action 2")
            .expect("created");
        broker.cancel(&id2).expect("cancel");
        assert!(matches!(
            broker.timeout(&id2),
            Err(InteractionError::AlreadyTerminal {
                state: InteractionState::Cancelled,
                ..
            })
        ));
        assert!(matches!(
            broker.resolve(&id2, InteractionAnswer::Confirmed),
            Err(InteractionError::AlreadyTerminal {
                state: InteractionState::Cancelled,
                ..
            })
        ));

        // Probe 3: Non-existent request returns NotFound
        let unknown_id = InteractionRequestId::new("non-existent-999");
        assert_eq!(
            broker.resolve(&unknown_id, InteractionAnswer::Confirmed),
            Err(InteractionError::NotFound(unknown_id.clone()))
        );
        assert_eq!(
            broker.timeout(&unknown_id),
            Err(InteractionError::NotFound(unknown_id.clone()))
        );
        assert_eq!(
            broker.cancel(&unknown_id),
            Err(InteractionError::NotFound(unknown_id.clone()))
        );
    }

    #[test]
    fn test_interaction_adversarial_malformed_input_rejected_without_panic() {
        let mut broker = InteractionBroker::new();

        // Empty question
        let res = broker.ask_user_question("", vec![InteractionOption::new("1", "One")]);
        assert_eq!(res, Err(InteractionError::EmptyQuestion));

        // Whitespace question
        let res = broker.ask_user_question("   \n\t  ", vec![InteractionOption::new("1", "One")]);
        assert_eq!(res, Err(InteractionError::EmptyQuestion));

        // Empty options
        let res = broker.ask_user_question("Valid question?", vec![]);
        assert_eq!(res, Err(InteractionError::EmptyOptions));

        // Option with empty id
        let res = broker.ask_user_question(
            "Valid question?",
            vec![InteractionOption::new("", "Valid Label")],
        );
        assert_eq!(res, Err(InteractionError::InvalidOption));

        // Option with empty label
        let res = broker.ask_user_question(
            "Valid question?",
            vec![InteractionOption::new("opt_1", "   ")],
        );
        assert_eq!(res, Err(InteractionError::InvalidOption));

        // Empty confirmation effect description
        let res = broker.request_action_confirmation("");
        assert_eq!(res, Err(InteractionError::EmptyEffectDescription));

        // Whitespace confirmation effect description
        let res = broker.request_action_confirmation("  \n  ");
        assert_eq!(res, Err(InteractionError::EmptyEffectDescription));
    }
}
