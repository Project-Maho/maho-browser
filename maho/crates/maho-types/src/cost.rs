use serde::{Deserialize, Serialize};

/// Pre-execution cost estimate for an LLM operation.
/// Used by mobile shells to show "~$X.XXX" UX before user confirms LLM run.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct CostEstimate {
    pub estimated_input_tokens: u32,
    /// ≈ 15% of input for summarization (compression heuristic)
    pub estimated_output_tokens: u32,
    pub estimated_cost_usd: f64,
    pub model: String,
}
