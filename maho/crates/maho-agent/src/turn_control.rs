// Copyright 2026 Maho Browser. All rights reserved.

//! Turn control semantics and active follow-up behavior contracts.
//!
//! Defines how user submissions during an active agent turn are routed:
//! Continue (stream integration), Queue (FIFO after current turn),
//! Steer (inject mid-flight guidance), or Interrupt (abort and start new turn).

use serde::{Deserialize, Serialize};
use std::collections::VecDeque;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use thiserror::Error;

/// Behavior applied when a user submits a message during an active turn.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum FollowUpBehavior {
    /// Continue execution by appending directly to the current streaming turn.
    Continue,
    /// Queue the follow-up to run FIFO as a distinct turn after the active turn finishes.
    Queue,
    /// Inject mid-flight guidance/steering into the current agent iteration.
    Steer,
    /// Cancel the active turn immediately and begin a new turn with this message.
    Interrupt,
}

/// Errors arising from turn control operations.
#[derive(Debug, Clone, PartialEq, Eq, Error, Serialize, Deserialize)]
pub enum TurnControlError {
    #[error("Queue capacity exceeded for session {session_id}: limit is {limit}")]
    QueueCapacityExceeded { session_id: String, limit: usize },
    #[error("Session is not currently active")]
    SessionNotActive,
    #[error("Invalid follow-up operation: {0}")]
    InvalidOperation(String),
}

/// A queued follow-up message waiting for turn completion.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct QueuedFollowUp {
    pub id: String,
    pub session_id: String,
    pub message: String,
    pub behavior: FollowUpBehavior,
    pub queued_at: u64,
}

/// Result of submitting a message to an active turn controller.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub enum FollowUpSubmissionResult {
    /// Follow-up was enqueued to be executed after the current turn.
    Queued { id: String, position: usize },
    /// Active turn was cancelled; new turn can begin.
    Interrupted { cancelled_turn_id: String },
    /// Steering guidance was accepted for the current turn.
    Steered { guidance: String },
    /// Message was merged directly into the active streaming turn.
    Continued,
}

/// Bounded per-session queue for follow-up turn control.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct TurnControlQueue {
    pub session_id: String,
    pub max_queue_size: usize,
    pub items: VecDeque<QueuedFollowUp>,
}

impl TurnControlQueue {
    pub const DEFAULT_MAX_QUEUE_SIZE: usize = 8;

    pub fn new(session_id: impl Into<String>, max_queue_size: usize) -> Self {
        Self {
            session_id: session_id.into(),
            max_queue_size,
            items: VecDeque::new(),
        }
    }

    pub fn len(&self) -> usize {
        self.items.len()
    }

    pub fn is_empty(&self) -> bool {
        self.items.is_empty()
    }

    pub fn is_full(&self) -> bool {
        self.items.len() >= self.max_queue_size
    }

    /// Enqueue a follow-up item. Returns an error if queue capacity is exceeded.
    pub fn enqueue(&mut self, item: QueuedFollowUp) -> Result<usize, TurnControlError> {
        if self.is_full() {
            return Err(TurnControlError::QueueCapacityExceeded {
                session_id: self.session_id.clone(),
                limit: self.max_queue_size,
            });
        }
        self.items.push_back(item);
        Ok(self.items.len())
    }

    /// Pop the next follow-up item in FIFO order.
    pub fn pop_next(&mut self) -> Option<QueuedFollowUp> {
        self.items.pop_front()
    }

    /// Peek at the next item in FIFO order.
    pub fn peek_next(&self) -> Option<&QueuedFollowUp> {
        self.items.front()
    }

    /// Clear all queued items.
    pub fn clear(&mut self) {
        self.items.clear();
    }
}

/// Active turn lifecycle handle supporting cancellation flags and clean transitions.
pub struct ActiveTurn {
    pub turn_id: String,
    pub session_id: String,
    pub cancelled: Arc<AtomicBool>,
    pub steer_guidance: Arc<tokio::sync::Mutex<Option<String>>>,
}

impl ActiveTurn {
    pub fn new(session_id: impl Into<String>, turn_id: impl Into<String>) -> Self {
        Self {
            session_id: session_id.into(),
            turn_id: turn_id.into(),
            cancelled: Arc::new(AtomicBool::new(false)),
            steer_guidance: Arc::new(tokio::sync::Mutex::new(None)),
        }
    }

    /// Check if this turn has been cancelled.
    pub fn is_cancelled(&self) -> bool {
        self.cancelled.load(Ordering::SeqCst)
    }

    /// Cancel this turn.
    pub fn cancel(&self) {
        self.cancelled.store(true, Ordering::SeqCst);
    }

    /// Cancellation flag handle.
    pub fn cancellation_token(&self) -> Arc<AtomicBool> {
        Arc::clone(&self.cancelled)
    }
}

/// Session turn controller managing active turn state, follow-up submissions, and bounded queues.
pub struct SessionTurnController {
    pub session_id: String,
    pub queue: TurnControlQueue,
    pub active_turn: Option<ActiveTurn>,
    next_id: u64,
}

impl SessionTurnController {
    pub fn new(session_id: impl Into<String>) -> Self {
        let sid = session_id.into();
        Self {
            queue: TurnControlQueue::new(sid.clone(), TurnControlQueue::DEFAULT_MAX_QUEUE_SIZE),
            session_id: sid,
            active_turn: None,
            next_id: 1,
        }
    }

    pub fn with_max_queue_size(session_id: impl Into<String>, max_queue_size: usize) -> Self {
        let sid = session_id.into();
        Self {
            queue: TurnControlQueue::new(sid.clone(), max_queue_size),
            session_id: sid,
            active_turn: None,
            next_id: 1,
        }
    }

    pub fn is_active(&self) -> bool {
        self.active_turn.is_some()
    }

    /// Start a new turn. If a turn is already active, returns an error.
    pub fn start_turn(
        &mut self,
        turn_id: impl Into<String>,
    ) -> Result<Arc<AtomicBool>, TurnControlError> {
        if self.is_active() {
            return Err(TurnControlError::InvalidOperation(
                "A turn is already active in this session".to_string(),
            ));
        }
        let turn = ActiveTurn::new(&self.session_id, turn_id);
        let cancel_token = turn.cancellation_token();
        self.active_turn = Some(turn);
        Ok(cancel_token)
    }

    /// Complete the currently active turn and pop the next queued follow-up if any.
    pub fn complete_current_turn(&mut self) -> Option<QueuedFollowUp> {
        self.active_turn = None;
        self.queue.pop_next()
    }

    /// Submit a follow-up message when a turn may or may not be active.
    pub fn submit_follow_up(
        &mut self,
        message: impl Into<String>,
        behavior: FollowUpBehavior,
        timestamp: u64,
    ) -> Result<FollowUpSubmissionResult, TurnControlError> {
        let msg = message.into();
        let follow_up_id = format!("followup-{}", self.next_id);
        self.next_id += 1;

        match behavior {
            FollowUpBehavior::Queue => {
                let item = QueuedFollowUp {
                    id: follow_up_id.clone(),
                    session_id: self.session_id.clone(),
                    message: msg,
                    behavior,
                    queued_at: timestamp,
                };
                let pos = self.queue.enqueue(item)?;
                Ok(FollowUpSubmissionResult::Queued {
                    id: follow_up_id,
                    position: pos,
                })
            }
            FollowUpBehavior::Steer => {
                if let Some(ref active) = self.active_turn {
                    // Inject steer guidance into the active turn
                    if let Ok(mut lock) = active.steer_guidance.try_lock() {
                        *lock = Some(msg.clone());
                    }
                    Ok(FollowUpSubmissionResult::Steered { guidance: msg })
                } else {
                    Err(TurnControlError::SessionNotActive)
                }
            }
            FollowUpBehavior::Interrupt => {
                if let Some(active) = self.active_turn.take() {
                    active.cancel();
                    Ok(FollowUpSubmissionResult::Interrupted {
                        cancelled_turn_id: active.turn_id,
                    })
                } else {
                    Err(TurnControlError::SessionNotActive)
                }
            }
            FollowUpBehavior::Continue => {
                if self.active_turn.is_some() {
                    Ok(FollowUpSubmissionResult::Continued)
                } else {
                    Err(TurnControlError::SessionNotActive)
                }
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_queue_follow_up_fifo_execution_and_once_only() {
        let mut controller = SessionTurnController::new("session-1");
        let cancel_token = controller.start_turn("turn-1").unwrap();
        assert!(!cancel_token.load(Ordering::SeqCst));
        assert!(controller.is_active());

        // Submit multiple follow-ups with Queue behavior
        let res1 = controller
            .submit_follow_up("msg 1", FollowUpBehavior::Queue, 100)
            .unwrap();
        let res2 = controller
            .submit_follow_up("msg 2", FollowUpBehavior::Queue, 101)
            .unwrap();
        let res3 = controller
            .submit_follow_up("msg 3", FollowUpBehavior::Queue, 102)
            .unwrap();

        assert_eq!(
            res1,
            FollowUpSubmissionResult::Queued {
                id: "followup-1".to_string(),
                position: 1
            }
        );
        assert_eq!(
            res2,
            FollowUpSubmissionResult::Queued {
                id: "followup-2".to_string(),
                position: 2
            }
        );
        assert_eq!(
            res3,
            FollowUpSubmissionResult::Queued {
                id: "followup-3".to_string(),
                position: 3
            }
        );
        assert_eq!(controller.queue.len(), 3);

        // Turn 1 completes -> first queued follow-up popped exactly once
        let next1 = controller
            .complete_current_turn()
            .expect("should have item 1");
        assert_eq!(next1.message, "msg 1");
        assert_eq!(next1.id, "followup-1");
        assert_eq!(controller.queue.len(), 2);
        assert!(!controller.is_active());

        // Start turn for item 1 and complete it
        let _ = controller.start_turn("turn-2").unwrap();
        let next2 = controller
            .complete_current_turn()
            .expect("should have item 2");
        assert_eq!(next2.message, "msg 2");
        assert_eq!(next2.id, "followup-2");
        assert_eq!(controller.queue.len(), 1);

        // Start turn for item 2 and complete it
        let _ = controller.start_turn("turn-3").unwrap();
        let next3 = controller
            .complete_current_turn()
            .expect("should have item 3");
        assert_eq!(next3.message, "msg 3");
        assert_eq!(next3.id, "followup-3");
        assert_eq!(controller.queue.len(), 0);

        // Complete turn 3 -> no more queued items
        let _ = controller.start_turn("turn-4").unwrap();
        let next4 = controller.complete_current_turn();
        assert!(next4.is_none());
        assert_eq!(controller.queue.len(), 0);
    }

    #[test]
    fn test_steer_sets_guidance_and_preserves_active_turn() {
        let mut controller = SessionTurnController::new("session-steer");
        let _ = controller.start_turn("turn-1").unwrap();

        let result = controller
            .submit_follow_up("focus only on rust files", FollowUpBehavior::Steer, 100)
            .unwrap();
        assert_eq!(
            result,
            FollowUpSubmissionResult::Steered {
                guidance: "focus only on rust files".to_string(),
            }
        );

        // Active turn is still running and not cancelled
        assert!(controller.is_active());
        let active = controller.active_turn.as_ref().unwrap();
        assert!(!active.is_cancelled());
        let guidance = active.steer_guidance.try_lock().unwrap().clone();
        assert_eq!(guidance, Some("focus only on rust files".to_string()));
    }

    #[test]
    fn test_interrupt_cancels_current_turn_and_no_callback_bleed() {
        let mut controller = SessionTurnController::new("session-interrupt");
        let cancel_token = controller.start_turn("turn-active").unwrap();
        assert!(!cancel_token.load(Ordering::SeqCst));

        // Submit Interrupt
        let result = controller
            .submit_follow_up(
                "stop and do something else",
                FollowUpBehavior::Interrupt,
                100,
            )
            .unwrap();
        assert_eq!(
            result,
            FollowUpSubmissionResult::Interrupted {
                cancelled_turn_id: "turn-active".to_string(),
            }
        );

        // Current turn's cancellation token must be true
        assert!(cancel_token.load(Ordering::SeqCst));
        // Controller is no longer running the old turn
        assert!(!controller.is_active());

        // A new turn can start cleanly
        let new_cancel_token = controller.start_turn("turn-new").unwrap();
        assert!(!new_cancel_token.load(Ordering::SeqCst));
        // Old cancel token remains cancelled (no callback bleed across turns)
        assert!(cancel_token.load(Ordering::SeqCst));
    }

    #[test]
    fn test_bounded_queue_rejection_at_max_capacity() {
        let mut controller = SessionTurnController::with_max_queue_size("session-bounded", 8);
        let _ = controller.start_turn("turn-initial").unwrap();

        // Enqueue 8 messages (up to capacity)
        for i in 1..=8 {
            let res = controller.submit_follow_up(
                format!("msg {i}"),
                FollowUpBehavior::Queue,
                100 + i as u64,
            );
            assert!(res.is_ok(), "item {i} should be accepted");
        }
        assert_eq!(controller.queue.len(), 8);
        assert!(controller.queue.is_full());

        // 9th message must be rejected with typed error
        let res9 = controller.submit_follow_up("msg 9", FollowUpBehavior::Queue, 109);
        assert!(res9.is_err());
        match res9 {
            Err(TurnControlError::QueueCapacityExceeded { session_id, limit }) => {
                assert_eq!(session_id, "session-bounded");
                assert_eq!(limit, 8);
            }
            other => panic!("Expected QueueCapacityExceeded, got {other:?}"),
        }

        // Empty queue ops & edge cases
        let mut empty_queue = TurnControlQueue::new("empty-session", 8);
        assert!(empty_queue.is_empty());
        assert_eq!(empty_queue.pop_next(), None);
        assert_eq!(empty_queue.peek_next(), None);
    }
}
