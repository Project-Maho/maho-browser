// Copyright 2026 Maho Browser. All rights reserved.

//! Context budget tracking, compaction checkpoints, and conversation summarization contracts.
//!
//! Preserves the immutable system contract (#2), recent turn history, active tools,
//! and unresolved approvals while compacting model-facing context under token pressure.

use std::collections::hash_map::DefaultHasher;
use std::hash::{Hash, Hasher};
use serde::{Deserialize, Serialize};
use swiftide_core::chat_completion::ChatMessage;

pub const DEFAULT_RECENT_TAIL_COUNT: usize = 8;

/// Budget estimation and limits for conversation token management.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct ContextBudget {
    /// Maximum context window allowed by the active model (tokens).
    pub max_context_tokens: usize,
    /// Headroom reserved for model completion response (tokens).
    pub reserved_headroom_tokens: usize,
    /// Threshold at which emergency single-pass compaction triggers (tokens).
    pub emergency_compaction_threshold: usize,
    /// Target token count to achieve after compaction.
    pub target_compacted_tokens: usize,
}

impl Default for ContextBudget {
    fn default() -> Self {
        Self {
            max_context_tokens: 128_000,
            reserved_headroom_tokens: 8_192,
            emergency_compaction_threshold: 110_000,
            target_compacted_tokens: 32_000,
        }
    }
}

impl ContextBudget {
    /// Available token budget for model-facing context before reserved response headroom.
    pub fn effective_max_tokens(&self) -> usize {
        self.max_context_tokens.saturating_sub(self.reserved_headroom_tokens)
    }

    /// Whether context length exceeds compaction triggering thresholds.
    pub fn is_over_budget(&self, token_count: usize) -> bool {
        token_count > self.target_compacted_tokens
            || token_count >= self.effective_max_tokens()
            || token_count >= self.emergency_compaction_threshold
    }
}

/// Reason triggering conversation context compaction.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum CompactionTriggerReason {
    /// Proactive compaction triggered by budget threshold.
    BudgetExceeded,
    /// Emergency recovery after provider returned context_too_large.
    EmergencyOverflowRecovery,
    /// Manual or checkpoint-directed compaction.
    ExplicitSessionCheckpoint,
}

/// Checkpoint recording context compaction state for a session.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct CompactionCheckpoint {
    pub session_id: String,
    pub run_id: String,
    pub turn_index: u64,
    pub token_count_before: usize,
    pub compacted_at: u64,
    pub trigger_reason: CompactionTriggerReason,
    pub preserved_keys: Vec<String>,
}

/// Compacted summary replacing earlier conversational turns.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct CompactedSummary {
    /// Narrative summary preserving core entities, goals, and facts.
    pub summary_text: String,
    pub original_token_count: usize,
    pub compacted_token_count: usize,
    /// Invariant: must remain true to guarantee baseline prompt survived.
    pub preserved_system_contract: bool,
    /// Tool identifiers active during compaction.
    pub preserved_active_tools: Vec<String>,
    /// Artifact handles preserved across compaction.
    pub preserved_artifacts: Vec<String>,
}

/// Result produced by context compaction.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct CompactionResult {
    pub session_id: String,
    pub messages: Vec<ChatMessage>,
    pub summary: Option<CompactedSummary>,
    pub checkpoint: Option<CompactionCheckpoint>,
    pub dropped_indices: Vec<usize>,
    pub was_compacted: bool,
    pub estimated_tokens_before: usize,
    pub estimated_tokens_after: usize,
}

/// Errors occurring during context compaction.
#[derive(Debug, thiserror::Error, PartialEq, Eq)]
pub enum CompactionError {
    #[error("compaction exhausted: context cannot be compacted further without losing required state")]
    CompactionExhausted,
    #[error("invalid context budget: max_context_tokens {0} <= reserved_headroom_tokens {1}")]
    InvalidBudget(usize, usize),
}

/// Checks if a character belongs to East Asian / CJK Unicode scripts:
/// Hangul, Kanji/Hanzi (CJK Unified Ideographs), Hiragana, Katakana, Bopomofo,
/// and fullwidth punctuation/forms.
pub fn is_cjk_char(c: char) -> bool {
    matches!(
        c,
        '\u{1100}'..='\u{11FF}'   // Hangul Jamo
        | '\u{2E80}'..='\u{2EFF}' // CJK Radicals Supplement
        | '\u{3000}'..='\u{303F}' // CJK Symbols and Punctuation
        | '\u{3040}'..='\u{309F}' // Hiragana
        | '\u{30A0}'..='\u{30FF}' // Katakana
        | '\u{3100}'..='\u{312F}' // Bopomofo
        | '\u{3130}'..='\u{318F}' // Hangul Compatibility Jamo
        | '\u{3200}'..='\u{32FF}' // Enclosed CJK Letters and Months
        | '\u{3400}'..='\u{4DBF}' // CJK Unified Ideographs Extension A
        | '\u{4E00}'..='\u{9FFF}' // CJK Unified Ideographs
        | '\u{AC00}'..='\u{D7AF}' // Hangul Syllables
        | '\u{F900}'..='\u{FAFF}' // CJK Compatibility Ideographs
        | '\u{FF00}'..='\u{FFEF}' // Halfwidth and Fullwidth Forms
        | '\u{20000}'..='\u{2FA1F}' // CJK Extensions B..F
    )
}

/// Estimates token count for text using ~chars/4 for ASCII/Latin and 1 token per
/// character for CJK scripts.
///
/// Approximation document:
/// - In modern BPE tokenizers (OpenAI cl100k_base / o200k_base / Gemini SentencePiece),
///   English text averages ~4 characters per token.
/// - CJK characters (Hangul syllables, Chinese Hanzi, Japanese Kanji/Kana) generally tokenize
///   to ~1 token (or 1-2 tokens) per ideograph/syllable rather than sharing bytes across 4 characters.
/// - Empty string estimates to 0 tokens.
pub fn estimate_tokens(text: &str) -> usize {
    if text.is_empty() {
        return 0;
    }

    let mut cjk_count = 0usize;
    let mut non_cjk_count = 0usize;

    for c in text.chars() {
        if is_cjk_char(c) {
            cjk_count += 1;
        } else {
            non_cjk_count += 1;
        }
    }

    let non_cjk_tokens = if non_cjk_count == 0 {
        0
    } else {
        (non_cjk_count + 2) / 4
    };

    cjk_count + non_cjk_tokens
}

/// Estimates tokens for a single chat message including tool call metadata.
pub fn estimate_message_tokens(msg: &ChatMessage) -> usize {
    let mut tokens = 3; // Standard per-message role/framing token overhead
    tokens += estimate_tokens(msg.as_ref());

    match msg {
        ChatMessage::Assistant(_, Some(calls)) => {
            for call in calls {
                tokens += 4;
                tokens += estimate_tokens(call.name());
                if let Some(args) = call.args() {
                    tokens += estimate_tokens(args);
                }
            }
        }
        ChatMessage::ToolOutput(call, output) => {
            tokens += 3;
            tokens += estimate_tokens(call.name());
            if let Some(content) = output.content() {
                tokens += estimate_tokens(content);
            }
        }
        _ => {}
    }

    tokens
}

/// Estimates total tokens for a slice of chat messages.
pub fn estimate_messages_tokens(messages: &[ChatMessage]) -> usize {
    messages.iter().map(estimate_message_tokens).sum()
}

/// Determines whether a message contains unresolved tool calls, approval markers,
/// or pending confirmation signals that must not be stripped during compaction.
pub fn is_unresolved_or_marker_message(msg: &ChatMessage) -> bool {
    let text = msg.as_ref();
    let text_markers = [
        "approval_required",
        "approval_pending",
        "request_action_confirmation",
        "ask_user_question",
        "ApprovalRequest",
        "[APPROVAL_PENDING]",
        "[TOOL_CALL]",
        "[UNRESOLVED]",
        "needs_confirmation",
        "needs_user_resume",
        "tool_call",
        "tool_result",
    ];

    if text_markers.iter().any(|&m| text.contains(m)) {
        return true;
    }

    match msg {
        ChatMessage::Assistant(_, Some(calls)) if !calls.is_empty() => true,
        ChatMessage::ToolOutput(_, output) => {
            if let Some(content) = output.content() {
                text_markers.iter().any(|&m| content.contains(m))
            } else {
                false
            }
        }
        _ => false,
    }
}

/// Extracts artifact handles, task references, and tool names mentioned across messages.
pub fn extract_entities(messages: &[ChatMessage]) -> (Vec<String>, Vec<String>, Vec<String>) {
    let mut artifacts = Vec::new();
    let mut tasks = Vec::new();
    let mut tools = Vec::new();

    for msg in messages {
        match msg {
            ChatMessage::Assistant(_, Some(calls)) => {
                for call in calls {
                    let name = call.name().to_string();
                    if !tools.contains(&name) {
                        tools.push(name);
                    }
                }
            }
            ChatMessage::ToolOutput(call, _) => {
                let name = call.name().to_string();
                if !tools.contains(&name) {
                    tools.push(name);
                }
            }
            _ => {}
        }

        let text = msg.as_ref();
        for word in text.split_whitespace() {
            let clean = word.trim_matches(|c: char| !c.is_alphanumeric() && c != '-' && c != '_' && c != '.');
            if clean.starts_with("artifact-")
                || clean.starts_with("artifact_")
                || clean.starts_with("art_")
                || clean.ends_with(".pdf")
                || clean.ends_with(".xlsx")
            {
                let s = clean.to_string();
                if !artifacts.contains(&s) {
                    artifacts.push(s);
                }
            } else if clean.starts_with("task-")
                || clean.starts_with("task_")
                || clean.starts_with("run-")
                || clean.starts_with("run_")
            {
                let s = clean.to_string();
                if !tasks.contains(&s) {
                    tasks.push(s);
                }
            }
        }
    }

    (artifacts, tasks, tools)
}

fn compute_messages_fingerprint(messages: &[ChatMessage]) -> u64 {
    let mut hasher = DefaultHasher::new();
    messages.len().hash(&mut hasher);
    for m in messages {
        m.as_ref().hash(&mut hasher);
    }
    hasher.finish()
}

/// Session-scoped compactor that tracks compaction cycles and enforces emergency compaction limits.
#[derive(Debug, Clone)]
pub struct Compactor {
    pub session_id: String,
    pub emergency_compact_count: usize,
    pub last_compacted_fingerprint: Option<u64>,
}

impl Compactor {
    pub fn new(session_id: impl Into<String>) -> Self {
        Self {
            session_id: session_id.into(),
            emergency_compact_count: 0,
            last_compacted_fingerprint: None,
        }
    }

    pub fn compact(
        &mut self,
        messages: &[ChatMessage],
        budget: &ContextBudget,
    ) -> Result<CompactionResult, CompactionError> {
        let session_id = self.session_id.clone();
        compact_with_state(
            Some(self),
            &session_id,
            messages,
            budget,
            DEFAULT_RECENT_TAIL_COUNT,
        )
    }
}

/// Compacts model-facing conversation messages to fit within context budget.
pub fn compact(
    session_id: &str,
    messages: &[ChatMessage],
    budget: &ContextBudget,
) -> Result<CompactionResult, CompactionError> {
    compact_with_state(None, session_id, messages, budget, DEFAULT_RECENT_TAIL_COUNT)
}

/// Compacts model-facing conversation messages with custom recent tail count K.
pub fn compact_with_tail(
    session_id: &str,
    messages: &[ChatMessage],
    budget: &ContextBudget,
    tail_k: usize,
) -> Result<CompactionResult, CompactionError> {
    compact_with_state(None, session_id, messages, budget, tail_k)
}

/// Emergency single-pass compaction forced after provider context overflow.
pub fn emergency_compact(
    session_id: &str,
    messages: &[ChatMessage],
) -> Result<CompactionResult, CompactionError> {
    let budget = ContextBudget {
        max_context_tokens: 0,
        reserved_headroom_tokens: 0,
        emergency_compaction_threshold: 0,
        target_compacted_tokens: 0,
    };
    compact_with_state(
        None,
        session_id,
        messages,
        &budget,
        DEFAULT_RECENT_TAIL_COUNT.min(messages.len().saturating_sub(2).max(1)),
    )
}

/// Helper function implementing compaction logic with optional state tracking.
fn compact_with_state(
    mut state: Option<&mut Compactor>,
    session_id: &str,
    messages: &[ChatMessage],
    budget: &ContextBudget,
    tail_k: usize,
) -> Result<CompactionResult, CompactionError> {
    if messages.is_empty() {
        return Ok(CompactionResult {
            session_id: session_id.to_string(),
            messages: vec![],
            summary: None,
            checkpoint: None,
            dropped_indices: vec![],
            was_compacted: false,
            estimated_tokens_before: 0,
            estimated_tokens_after: 0,
        });
    }

    let token_count_before = estimate_messages_tokens(messages);

    // If already within target budget and not exceeding limits, return as is
    if !budget.is_over_budget(token_count_before) {
        return Ok(CompactionResult {
            session_id: session_id.to_string(),
            messages: messages.to_vec(),
            summary: None,
            checkpoint: None,
            dropped_indices: vec![],
            was_compacted: false,
            estimated_tokens_before: token_count_before,
            estimated_tokens_after: token_count_before,
        });
    }

    let input_fingerprint = compute_messages_fingerprint(messages);

    // Infinite loop / exhaustion prevention:
    // If a stateful compactor is passed and was already compacted on this exact context without new content,
    // prevent infinite loop by returning CompactionExhausted.
    if let Some(ref compactor) = state {
        if compactor.emergency_compact_count > 0
            && compactor.last_compacted_fingerprint == Some(input_fingerprint)
        {
            return Err(CompactionError::CompactionExhausted);
        }
    }

    let total_len = messages.len();

    // Partition the messages:
    // 1. First message (system contract / initial setup) -> preserved verbatim
    // 2. Recent tail (last K messages) -> preserved verbatim
    // 3. Middle messages -> inspect for unresolved markers vs droppable
    let tail_start = total_len.saturating_sub(tail_k).max(1);

    let mut preserved_middle = Vec::new();
    let mut dropped_indices = Vec::new();
    let mut dropped_messages = Vec::new();

    for (idx, msg) in messages.iter().enumerate().take(tail_start).skip(1) {
        if is_unresolved_or_marker_message(msg) {
            preserved_middle.push(msg.clone());
        } else {
            dropped_indices.push(idx);
            dropped_messages.push(msg.clone());
        }
    }

    // If nothing was dropped (e.g. all middle messages have markers or len <= tail + 1), check if exhausted
    if dropped_indices.is_empty() {
        if let Some(ref mut compactor) = state {
            compactor.last_compacted_fingerprint = Some(input_fingerprint);
            compactor.emergency_compact_count += 1;
        }
        return Ok(CompactionResult {
            session_id: session_id.to_string(),
            messages: messages.to_vec(),
            summary: None,
            checkpoint: None,
            dropped_indices: vec![],
            was_compacted: false,
            estimated_tokens_before: token_count_before,
            estimated_tokens_after: token_count_before,
        });
    }

    let (artifacts, tasks, tools) = extract_entities(&dropped_messages);

    let mut summary_parts = Vec::new();
    summary_parts.push(format!(
        "Compacted conversation summary: dropped {} turns at indices {:?}.",
        dropped_indices.len(),
        dropped_indices
    ));

    if !artifacts.is_empty() {
        summary_parts.push(format!("Extracted artifact references: {}.", artifacts.join(", ")));
    }
    if !tasks.is_empty() {
        summary_parts.push(format!("Extracted task references: {}.", tasks.join(", ")));
    }
    if !tools.is_empty() {
        summary_parts.push(format!("Active tools in compacted turns: {}.", tools.join(", ")));
    }
    summary_parts.push("Preserved baseline system contract, active tool/approval states, and recent conversation turns.".to_string());

    let summary_text = summary_parts.join(" ");

    let summary = CompactedSummary {
        summary_text: summary_text.clone(),
        original_token_count: token_count_before,
        compacted_token_count: 0, // calculated after constructing new_messages
        preserved_system_contract: true,
        preserved_active_tools: tools,
        preserved_artifacts: artifacts.clone(),
    };

    let checkpoint = CompactionCheckpoint {
        session_id: session_id.to_string(),
        run_id: format!("run-{}", session_id),
        turn_index: total_len as u64,
        token_count_before,
        compacted_at: std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap_or_default()
            .as_secs(),
        trigger_reason: CompactionTriggerReason::BudgetExceeded,
        preserved_keys: artifacts,
    };

    // Assemble new model-facing message list:
    // [0]: First message (system contract verbatim)
    // [1]: Compacted Summary message
    // [2..]: Preserved middle messages (unresolved approvals/tool calls)
    // [..]: Recent tail messages
    let mut new_messages = Vec::with_capacity(1 + 1 + preserved_middle.len() + (total_len - tail_start));
    new_messages.push(messages[0].clone());
    new_messages.push(ChatMessage::new_summary(summary_text));
    new_messages.extend(preserved_middle);
    new_messages.extend(messages[tail_start..].iter().cloned());

    let token_count_after = estimate_messages_tokens(&new_messages);

    let mut final_summary = summary;
    final_summary.compacted_token_count = token_count_after;

    if let Some(ref mut compactor) = state {
        compactor.last_compacted_fingerprint = Some(input_fingerprint);
        compactor.emergency_compact_count += 1;
    }

    Ok(CompactionResult {
        session_id: session_id.to_string(),
        messages: new_messages,
        summary: Some(final_summary),
        checkpoint: Some(checkpoint),
        dropped_indices,
        was_compacted: true,
        estimated_tokens_before: token_count_before,
        estimated_tokens_after: token_count_after,
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use swiftide_core::chat_completion::ToolCall;

    #[test]
    fn test_estimate_tokens_chars_and_cjk_weighting() {
        // ASCII / Latin: ~4 chars per token
        assert_eq!(estimate_tokens(""), 0);
        assert_eq!(estimate_tokens("abcd"), 1);
        assert_eq!(estimate_tokens("abcdefghijklmnop"), 4);

        // CJK characters: 1 token each approximation
        assert_eq!(estimate_tokens("안녕하세요"), 5);
        assert_eq!(estimate_tokens("日本語テスト"), 6);
        assert_eq!(estimate_tokens("你好世界"), 4);

        // Mixed ASCII and CJK: "Hello " (6 chars -> 2 tokens) + "세계" (2 chars -> 2 tokens) = 4 tokens
        let mixed = "Hello 세계";
        assert_eq!(estimate_tokens(mixed), 4);
    }

    #[test]
    fn test_compact_preserves_system_baseline_verbatim() {
        let system_contract = "SYSTEM CONTRACT: Always provide 30s status updates and final screenshot proof. Do not ignore.";
        let mut messages = vec![ChatMessage::new_system(system_contract)];
        for i in 1..=20 {
            messages.push(ChatMessage::new_user(format!("Turn {i}: user request with some long context {}", "x".repeat(100))));
            messages.push(ChatMessage::new_assistant(Some(format!("Turn {i}: assistant answer with info {}", "y".repeat(100))), None));
        }

        let budget = ContextBudget {
            max_context_tokens: 500,
            reserved_headroom_tokens: 100,
            emergency_compaction_threshold: 400,
            target_compacted_tokens: 300,
        };

        let result = compact("sess-contract-test", &messages, &budget).unwrap();
        assert!(result.was_compacted, "Must be compacted when over budget");
        assert!(!result.messages.is_empty());

        // First message must survive verbatim
        match &result.messages[0] {
            ChatMessage::System(text) => {
                assert_eq!(text, system_contract, "System contract must survive verbatim at index 0");
            }
            other => panic!("Expected System message at index 0, got {other:?}"),
        }

        let summary = result.summary.expect("Must produce CompactedSummary");
        assert!(summary.preserved_system_contract, "preserved_system_contract flag must be true");
    }

    #[test]
    fn test_compact_preserves_recent_tail_default_k8() {
        let mut messages = vec![ChatMessage::new_system("System baseline")];
        for i in 1..=20 {
            messages.push(ChatMessage::new_user(format!("User message {i} {}", "content ".repeat(30))));
        }

        let budget = ContextBudget {
            max_context_tokens: 400,
            reserved_headroom_tokens: 50,
            emergency_compaction_threshold: 300,
            target_compacted_tokens: 200,
        };

        let result = compact("sess-tail-test", &messages, &budget).unwrap();
        assert!(result.was_compacted);

        // The last 8 messages of original messages must be present at the tail of compacted messages
        let original_tail = &messages[messages.len() - 8..];
        let result_tail = &result.messages[result.messages.len() - 8..];
        for (orig, res) in original_tail.iter().zip(result_tail.iter()) {
            assert_eq!(orig.as_ref(), res.as_ref(), "Tail messages must match verbatim");
        }
    }

    #[test]
    fn test_compact_preserves_unresolved_tool_and_approval_markers() {
        let mut messages = vec![
            ChatMessage::new_system("System baseline"),
            ChatMessage::new_user("Turn 1"),
            ChatMessage::new_assistant(Some("Turn 1 response".to_string()), None),
            // Unresolved tool call / approval marker at middle turn
            ChatMessage::new_assistant(
                Some("Pending approval for action [APPROVAL_PENDING] request_action_confirmation".to_string()),
                Some(vec![ToolCall::builder().id("call-42").name("delete_account").args(r#"{"id":"acc-1"}"#).build().unwrap()]),
            ),
            ChatMessage::new_user("Turn 3 filler content ".repeat(50)),
            ChatMessage::new_assistant(Some("Turn 3 response ".repeat(50)), None),
            ChatMessage::new_user("Turn 4 filler content ".repeat(50)),
            ChatMessage::new_assistant(Some("Turn 4 response ".repeat(50)), None),
        ];
        // Add 8 more tail messages
        for i in 5..=12 {
            messages.push(ChatMessage::new_user(format!("Tail turn {i} {}", "data ".repeat(30))));
        }

        let budget = ContextBudget {
            max_context_tokens: 300,
            reserved_headroom_tokens: 50,
            emergency_compaction_threshold: 250,
            target_compacted_tokens: 150,
        };

        let result = compact("sess-markers", &messages, &budget).unwrap();
        assert!(result.was_compacted);

        // Assert unresolved tool call / approval marker was preserved in output
        let has_marker = result.messages.iter().any(|m| {
            m.as_ref().contains("APPROVAL_PENDING")
                || m.as_ref().contains("request_action_confirmation")
                || matches!(m, ChatMessage::Assistant(_, Some(calls)) if calls.iter().any(|c| c.id() == "call-42"))
        });
        assert!(has_marker, "Unresolved tool/approval markers must be preserved in compacted message list");
    }

    #[test]
    fn test_compact_extracts_and_preserves_artifact_and_task_ids() {
        let mut messages = vec![
            ChatMessage::new_system("System baseline"),
            ChatMessage::new_user("Please review task_1042 and export artifact-report-2026.pdf"),
            ChatMessage::new_assistant(Some("Processed art_998124 and finished task-abc-789".to_string()), None),
        ];
        for i in 3..=20 {
            messages.push(ChatMessage::new_user(format!("Filler message {i} {}", "lots of words ".repeat(25))));
        }

        let budget = ContextBudget {
            max_context_tokens: 400,
            reserved_headroom_tokens: 50,
            emergency_compaction_threshold: 300,
            target_compacted_tokens: 200,
        };

        let result = compact("sess-artifacts", &messages, &budget).unwrap();
        assert!(result.was_compacted);

        let summary = result.summary.expect("CompactedSummary must exist");
        assert!(
            summary.preserved_artifacts.iter().any(|a| a.contains("artifact-report-2026.pdf") || a.contains("art_998124")),
            "Extracted artifacts must include dropped artifact handles, got: {:?}",
            summary.preserved_artifacts
        );
        assert!(
            summary.summary_text.contains("task_1042") || summary.summary_text.contains("task-abc-789") || summary.summary_text.contains("artifact"),
            "Summary text must mention task/artifact context: {}",
            summary.summary_text
        );
    }

    #[test]
    fn test_compact_summary_truncation_list_and_session_id() {
        let mut messages = vec![ChatMessage::new_system("System prompt")];
        for i in 1..=15 {
            messages.push(ChatMessage::new_user(format!("User turn {i} {}", "words ".repeat(40))));
        }

        let budget = ContextBudget {
            max_context_tokens: 400,
            reserved_headroom_tokens: 50,
            emergency_compaction_threshold: 300,
            target_compacted_tokens: 200,
        };

        let result = compact("session-xyz-123", &messages, &budget).unwrap();
        assert!(result.was_compacted);
        assert_eq!(result.session_id, "session-xyz-123");
        assert!(!result.dropped_indices.is_empty(), "Must record dropped turn indices");

        // Assert summary text references dropped indices
        let summary = result.summary.unwrap();
        for idx in &result.dropped_indices {
            assert!(
                summary.summary_text.contains(&idx.to_string()),
                "Summary text must contain dropped index {idx}, got: {}",
                summary.summary_text
            );
        }
    }

    #[test]
    fn test_emergency_compact_once_prevents_infinite_loop() {
        let mut compactor = Compactor::new("sess-loop-guard");
        let mut messages = vec![ChatMessage::new_system("System prompt")];
        for i in 1..=12 {
            messages.push(ChatMessage::new_user(format!("User turn {i} {}", "content ".repeat(50))));
        }

        let budget = ContextBudget {
            max_context_tokens: 300,
            reserved_headroom_tokens: 50,
            emergency_compaction_threshold: 250,
            target_compacted_tokens: 150,
        };

        // First compaction: succeeds
        let res1 = compactor.compact(&messages, &budget);
        assert!(res1.is_ok(), "First compaction must succeed");

        // Second consecutive compaction on same messages without progress -> CompactionExhausted error
        let res2 = compactor.compact(&messages, &budget);
        assert_eq!(
            res2.unwrap_err(),
            CompactionError::CompactionExhausted,
            "Second consecutive compaction on unprogressed context must return CompactionExhausted"
        );
    }

    #[test]
    fn test_storage_invariant_original_transcript_untouched() {
        let original_messages = vec![
            ChatMessage::new_system("System baseline prompt"),
            ChatMessage::new_user("User first message"),
            ChatMessage::new_assistant(Some("Assistant first reply".to_string()), None),
            ChatMessage::new_user("User second message ".repeat(50)),
            ChatMessage::new_assistant(Some("Assistant second reply ".repeat(50)), None),
            ChatMessage::new_user("User third message ".repeat(50)),
            ChatMessage::new_assistant(Some("Assistant third reply ".repeat(50)), None),
            ChatMessage::new_user("User fourth message ".repeat(50)),
            ChatMessage::new_assistant(Some("Assistant fourth reply ".repeat(50)), None),
            ChatMessage::new_user("User fifth message ".repeat(50)),
            ChatMessage::new_assistant(Some("Assistant fifth reply ".repeat(50)), None),
        ];

        let original_len = original_messages.len();
        let budget = ContextBudget {
            max_context_tokens: 200,
            reserved_headroom_tokens: 40,
            emergency_compaction_threshold: 180,
            target_compacted_tokens: 100,
        };

        let result = compact("sess-invariant", &original_messages, &budget).unwrap();
        assert!(result.was_compacted);

        // Storage invariant: original slice untouched
        assert_eq!(original_messages.len(), original_len, "Original transcript length must remain untouched");
        assert_eq!(original_messages[0].as_ref(), "System baseline prompt");
        // Output is a separate new list with different length
        assert_ne!(result.messages.len(), original_messages.len());
    }

    #[test]
    fn test_malformed_empty_messages() {
        let empty_messages: Vec<ChatMessage> = vec![];
        let budget = ContextBudget::default();

        let result = compact("sess-empty", &empty_messages, &budget);
        assert!(result.is_ok(), "Empty messages must not panic");
        let res = result.unwrap();
        assert!(!res.was_compacted);
        assert!(res.messages.is_empty());
    }

    #[tokio::test]
    async fn test_emergency_compaction_and_same_session_retry() {
        use std::sync::atomic::{AtomicUsize, Ordering};
        use std::sync::{Arc, Mutex};
        use swiftide_core::chat_completion::{
            ChatCompletion, ChatCompletionRequest, ChatCompletionResponse,
            ChatMessage as SwiftideChatMessage,
        };
        use crate::swiftide_backend::RetryChatCompletion;
        use crate::run_journal::{AgentRunId, RunJournal};

        #[derive(Clone)]
        struct MockCompactingLLM {
            call_count: Arc<AtomicUsize>,
            fail_always: bool,
        }

        #[async_trait::async_trait]
        impl ChatCompletion for MockCompactingLLM {
            async fn complete(
                &self,
                request: &ChatCompletionRequest,
            ) -> Result<ChatCompletionResponse, swiftide_core::chat_completion::errors::LanguageModelError> {
                let current_call = self.call_count.fetch_add(1, Ordering::SeqCst) + 1;
                if current_call == 1 || self.fail_always {
                    Err(swiftide_core::chat_completion::errors::LanguageModelError::context_length_exceeded(
                        "context_length_exceeded: maximum context window reached",
                    ))
                } else {
                    assert!(request.messages().iter().any(|m| m.as_ref().contains("Compacted conversation summary")));
                    Ok(ChatCompletionResponse {
                        message: Some("recovered after emergency compaction".to_string()),
                        tool_calls: None,
                        delta: None,
                        id: uuid::Uuid::new_v4(),
                        usage: None,
                    })
                }
            }

            async fn complete_stream(
                &self,
                _request: &ChatCompletionRequest,
            ) -> swiftide_core::chat_completion::ChatCompletionStream {
                unimplemented!()
            }
        }

        // Test 1: First call fails with context_too_large, second succeeds -> 2 calls, journal has compaction event
        let call_count1 = Arc::new(AtomicUsize::new(0));
        let mock_llm1 = Arc::new(MockCompactingLLM {
            call_count: call_count1.clone(),
            fail_always: false,
        });

        let mut journal = RunJournal::new();
        let run_id = AgentRunId::new("run-compaction-retry");
        journal.start_run(run_id.clone(), "session-retry").unwrap();
        let journal_arc = Arc::new(Mutex::new(journal));

        let retry_llm = RetryChatCompletion {
            inner: mock_llm1,
            retry_policy: crate::retry_policy::RetryPolicy::default(),
            fallback_reasons: Arc::new(Mutex::new(Vec::new())),
            model_name: "test-model".to_string(),
            fallback_model_name: None,
            journal: Some(journal_arc.clone()),
            run_id: Some(run_id.clone()),
        };

        let mut msgs = vec![SwiftideChatMessage::new_system("System baseline")];
        for i in 1..=10 {
            msgs.push(SwiftideChatMessage::new_user(format!("turn {i}: {}", "data ".repeat(30))));
        }
        let req = ChatCompletionRequest::from(msgs.clone());

        let res = retry_llm.complete(&req).await;
        assert!(res.is_ok(), "Emergency compaction retry must succeed");
        assert_eq!(res.unwrap().message.unwrap(), "recovered after emergency compaction");
        assert_eq!(call_count1.load(Ordering::SeqCst), 2, "Must make exactly 2 model calls");

        let entries = journal_arc.lock().unwrap().get_entries(&run_id).unwrap().to_vec();
        assert!(
            entries.iter().any(|e| e.event_name == "compacting"),
            "Journal must contain a compaction event"
        );

        // Test 2: Fails second time -> typed ContextTooLarge surfaced (no infinite loop)
        let call_count2 = Arc::new(AtomicUsize::new(0));
        let mock_llm2 = Arc::new(MockCompactingLLM {
            call_count: call_count2.clone(),
            fail_always: true,
        });

        let retry_llm2 = RetryChatCompletion {
            inner: mock_llm2,
            retry_policy: crate::retry_policy::RetryPolicy::default(),
            fallback_reasons: Arc::new(Mutex::new(Vec::new())),
            model_name: "test-model".to_string(),
            fallback_model_name: None,
            journal: None,
            run_id: None,
        };

        let res2 = retry_llm2.complete(&req).await;
        assert!(res2.is_err(), "Second failure must return error");
        assert_eq!(call_count2.load(Ordering::SeqCst), 2, "Must stop after 2 calls and not loop infinitely");
        match res2.unwrap_err() {
            swiftide_core::chat_completion::errors::LanguageModelError::ContextLengthExceeded(_) => {}
            other => panic!("Expected ContextLengthExceeded error, got: {:?}", other),
        }
    }
}
