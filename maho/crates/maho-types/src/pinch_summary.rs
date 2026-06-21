use serde::{Deserialize, Serialize};

/// State machine for Pinch-to-Summarize feature lifecycle.
///
/// Pinch-LLM is ADDITIVE over the existing extractive summarizer:
/// extractive runs first (offline-capable, fast); LLM enhancement is
/// optional and gated by BYOK availability.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(tag = "kind", rename_all = "snake_case")]
pub enum PinchSummaryState {
    ExtractiveDefault,
    LlmPending,
    LlmStreaming,
    LlmSuccess,
    /// LLM failed (timeout, error, cancellation); fall back to extractive.
    LlmFailed {
        reason: String,
    },
}

/// Mode the user has selected for Pinch summarization.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum PinchSummaryMode {
    /// Use extractive only (offline, free, fast).
    Extractive,
    /// Run LLM enhancement after extractive (requires BYOK key + network).
    LlmEnhanced,
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn pinch_summary_state_transitions_serialize_correctly() {
        let states = vec![
            PinchSummaryState::ExtractiveDefault,
            PinchSummaryState::LlmPending,
            PinchSummaryState::LlmStreaming,
            PinchSummaryState::LlmSuccess,
            PinchSummaryState::LlmFailed {
                reason: "timeout".into(),
            },
        ];
        for state in states {
            let json = serde_json::to_string(&state).unwrap();
            let back: PinchSummaryState = serde_json::from_str(&json).unwrap();
            assert_eq!(state, back);
        }
    }

    #[test]
    fn pinch_summary_mode_serializes_as_snake_case() {
        let json = serde_json::to_string(&PinchSummaryMode::LlmEnhanced).unwrap();
        assert_eq!(json, "\"llm_enhanced\"");
    }
}
