use maho_types::cost::CostEstimate;

/// Heuristic token estimation for LLM prompts (no external tokenizer required).
///
/// Uses a GPT-style approximation: ~4 chars per token, but weighted by word count
/// to better handle natural language vs. dense code/JSON.
pub fn estimate_tokens(text: &str) -> usize {
    let len = text.len();
    if len == 0 {
        return 0;
    }
    let word_count = text.split_whitespace().count();
    let char_count = text.chars().count();
    // Mix: chars/4 baseline, floored by word count
    ((char_count as f32 / 4.0).ceil() as usize).max(word_count)
}

/// Compute the estimated cost in USD for a model call.
///
/// Prices are approximate as of June 2026 per 1K tokens.
/// Returns 0.0 for unknown models.
pub fn cost_usd(model: &str, prompt_tokens: usize, completion_tokens: usize) -> f64 {
    let (prompt_per_1k, completion_per_1k) = match model {
        "gpt-4o" => (0.0025, 0.010),
        "gpt-4-turbo" => (0.01, 0.03),
        "gpt-3.5-turbo" => (0.0005, 0.0015),
        m if m.starts_with("claude-sonnet") => (0.003, 0.015),
        m if m.starts_with("claude-opus") => (0.015, 0.075),
        m if m.starts_with("claude-3-5-haiku") => (0.0008, 0.004),
        "sonar" => (0.001, 0.001),
        "sonar-pro" => (0.003, 0.015),
        "sonar-reasoning" => (0.001, 0.005),
        "sonar-reasoning-pro" => (0.002, 0.008),
        _ => (0.0, 0.0),
    };
    (prompt_tokens as f64 / 1000.0) * prompt_per_1k
        + (completion_tokens as f64 / 1000.0) * completion_per_1k
}

/// Estimate cost of summarizing the given page text with the given model.
/// Heuristic: output tokens ≈ 15% of input tokens (summarization compression).
pub fn estimate_summarize_cost(page_text: &str, model: &str) -> CostEstimate {
    let input_tokens = estimate_tokens(page_text);
    let output_tokens = (input_tokens as f64 * 0.15).ceil() as u32;
    let cost = cost_usd(model, input_tokens, output_tokens as usize);
    CostEstimate {
        estimated_input_tokens: input_tokens as u32,
        estimated_output_tokens: output_tokens,
        estimated_cost_usd: cost,
        model: model.to_string(),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_estimate_tokens_empty() {
        assert_eq!(estimate_tokens(""), 0);
    }

    #[test]
    fn test_estimate_tokens_short() {
        // "hello world" → 2 words, 11 chars → max(ceil(11/4), 2) = max(3, 2) = 3
        let result = estimate_tokens("hello world");
        assert_eq!(result, 3);
    }

    #[test]
    fn test_estimate_tokens_long() {
        // 1000 chars of 'a' separated by spaces → ~250 via chars/4
        let text = "a ".repeat(500); // 1000 chars
        let result = estimate_tokens(&text);
        assert!(result >= 250, "expected >= 250 for ~1000 chars, got {result}");
    }

    #[test]
    fn test_estimate_tokens_single_word() {
        // Single word, word_count=1 fallback
        let result = estimate_tokens("hello");
        assert!(result >= 1);
    }

    #[test]
    fn test_cost_gpt4o() {
        // 1000 prompt + 500 completion at gpt-4o rates
        // prompt: 1000/1000 * 0.0025 = 0.0025
        // completion: 500/1000 * 0.010 = 0.005
        // total: 0.0075
        let cost = cost_usd("gpt-4o", 1000, 500);
        assert!((cost - 0.0075).abs() < 1e-9, "expected 0.0075, got {cost}");
    }

    #[test]
    fn test_cost_unknown() {
        let cost = cost_usd("unknown-model", 1000, 500);
        assert_eq!(cost, 0.0);
    }

    #[test]
    fn test_cost_gpt4_turbo() {
        // 1000 prompt + 1000 completion at gpt-4-turbo rates
        // 1.0 * 0.01 + 1.0 * 0.03 = 0.04
        let cost = cost_usd("gpt-4-turbo", 1000, 1000);
        assert!((cost - 0.04).abs() < 1e-9, "expected 0.04, got {cost}");
    }

    #[test]
    fn test_cost_claude_sonnet() {
        // 1000 prompt + 1000 completion at claude-sonnet rates
        // 1.0 * 0.003 + 1.0 * 0.015 = 0.018
        let cost = cost_usd("claude-sonnet-4", 1000, 1000);
        assert!((cost - 0.018).abs() < 1e-9, "expected 0.018, got {cost}");
    }

    #[test]
    fn test_cost_zero_tokens() {
        let cost = cost_usd("gpt-4o", 0, 0);
        assert_eq!(cost, 0.0);
    }

    #[test]
    fn test_cost_gpt35_turbo() {
        // 2000 prompt + 1000 completion at gpt-3.5-turbo rates
        // 2.0 * 0.0005 + 1.0 * 0.0015 = 0.001 + 0.0015 = 0.0025
        let cost = cost_usd("gpt-3.5-turbo", 2000, 1000);
        assert!((cost - 0.0025).abs() < 1e-9, "expected 0.0025, got {cost}");
    }

    #[test]
    fn estimate_summarize_cost_returns_nonzero_for_realistic_input() {
        let page = "Lorem ipsum dolor sit amet, consectetur adipiscing elit. ".repeat(100);
        let est = estimate_summarize_cost(&page, "gpt-4o");
        assert!(est.estimated_input_tokens > 0);
        assert!(est.estimated_output_tokens > 0);
        assert!(est.estimated_cost_usd > 0.0);
        assert!(est.estimated_cost_usd < 0.50, "5KB summarize should not exceed $0.50");
        assert_eq!(est.model, "gpt-4o");
    }

    #[test]
    fn estimate_summarize_cost_perplexity_models_priced() {
        let page = "Test content. ".repeat(50);
        for model in &["sonar", "sonar-pro", "sonar-reasoning"] {
            let est = estimate_summarize_cost(&page, model);
            assert!(est.estimated_cost_usd > 0.0, "Perplexity {} must have non-zero cost", model);
        }
    }
}
