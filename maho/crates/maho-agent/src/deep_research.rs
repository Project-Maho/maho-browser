//! Bounded deep-research mode (plan task 17).
//!
//! A Max/PAYG-gated, multi-step research loop with HARD budget caps
//! (max steps, max wall-clock, max cost) that emits citations and evidence.
//! The loop can never run unbounded: `max_steps` is a hard ceiling checked at
//! the top of every iteration, so termination is guaranteed even if the
//! underlying model never signals completion.
//!
//! Managed tiers may only use non-frontier models. The frontier gate mirrors
//! `maho/billing-proxy/src/lib.rs::FORBIDDEN_FRONTIER_PATTERNS` — the
//! billing-proxy remains the canonical enforcement point for managed routing;
//! this is a defense-in-depth mirror because `maho-agent` cannot depend on the
//! separate Cloudflare Worker crate. BYOK runs (user's own key and cost) skip
//! the frontier gate but still obey the budget caps.

use crate::{AgentError, AgentRuntime};
use async_trait::async_trait;
use maho_types::account::UserTier;
use maho_types::chat::{ChatContent, ChatMessage};
use std::sync::Arc;
use std::time::{Duration, Instant};

/// Hard ceiling on `max_steps` regardless of caller-supplied overrides, so a
/// bad config can never turn the bounded loop into an unbounded one.
pub const MAX_STEPS_CEILING: u32 = 20;

/// Frontier model patterns that are never allowed in Maho-managed routing.
/// Kept in sync with billing-proxy's `FORBIDDEN_FRONTIER_PATTERNS`.
const FRONTIER_PATTERNS: &[&str] = &[
    "claude-opus",
    "gpt-5",
    "/o3",
    "/o4",
    "gemini-3-ultra",
    ":thinking",
    ":reasoning-xhigh",
];

/// Returns true if `model` matches a frontier-forbidden pattern (managed only).
pub fn is_frontier_model(model: &str) -> bool {
    for pattern in FRONTIER_PATTERNS {
        if *pattern == "gpt-5" {
            if model.contains("gpt-5")
                && !model.contains("gpt-5-nano")
                && !model.contains("gpt-5-mini")
            {
                return true;
            }
            continue;
        }
        if model.contains(pattern) {
            return true;
        }
    }
    false
}

/// What a user is entitled to. Deep research is a Max feature; PAYG users
/// (any tier holding prepaid credits) are also entitled, mirroring the
/// billing-proxy's `max | payg` grouping.
#[derive(Clone, Copy, Debug)]
pub struct ResearchEntitlement {
    pub tier: UserTier,
    pub payg_credits_usd: f64,
}

impl ResearchEntitlement {
    pub fn allows_deep_research(&self) -> bool {
        matches!(self.tier, UserTier::Max) || self.payg_credits_usd > 0.0
    }
}

/// Strict budget caps. Every field is an upper bound; the loop stops at the
/// first one reached.
#[derive(Clone, Copy, Debug)]
pub struct ResearchBudget {
    pub max_steps: u32,
    pub max_wall_clock: Duration,
    pub max_cost_usd: f64,
}

impl ResearchBudget {
    /// Bounded managed default: 6 steps, 120s, $0.50 (Max/PAYG per-request cap).
    pub fn managed_default() -> Self {
        Self {
            max_steps: 6,
            max_wall_clock: Duration::from_secs(120),
            max_cost_usd: 0.50,
        }
    }

    /// Clamp `max_steps` to the hard ceiling so no override is unbounded.
    pub fn clamped(mut self) -> Self {
        if self.max_steps > MAX_STEPS_CEILING {
            self.max_steps = MAX_STEPS_CEILING;
        }
        self
    }
}

/// A single source discovered during research. Metadata only — no raw page
/// bodies are retained here.
#[derive(Clone, Debug, PartialEq)]
pub struct Citation {
    pub url: String,
    pub note: String,
}

/// Why the bounded loop stopped.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum StopReason {
    Completed,
    MaxStepsReached,
    Timeout,
    BudgetExceeded,
    ToolDenied,
}

/// The result of a bounded research run.
#[derive(Clone, Debug)]
pub struct DeepResearchReport {
    pub summary: String,
    pub citations: Vec<Citation>,
    pub evidence: Vec<String>,
    pub steps_taken: u32,
    pub estimated_cost_usd: f64,
    pub stop_reason: StopReason,
    pub model: String,
}

#[derive(Debug, thiserror::Error)]
pub enum DeepResearchError {
    #[error("deep research requires the Max tier or PAYG credits")]
    TierNotEntitled,
    #[error("model {0} is a frontier model and is BYOK-only, not available in managed tiers")]
    FrontierModelBlocked(String),
    #[error("research step failed: {0}")]
    Stepper(String),
    #[error("a required tool was denied")]
    ToolDenied,
}

/// Output of one research step.
#[derive(Clone, Debug, Default)]
pub struct StepOutcome {
    /// The model signalled it has gathered enough to conclude.
    pub done: bool,
    pub citations: Vec<Citation>,
    pub evidence_note: String,
    pub step_cost_usd: f64,
}

/// Drives one bounded research iteration and the final synthesis. Implementors
/// wrap a real agent/LLM; the loop below owns all budget accounting.
#[async_trait]
pub trait ResearchStepper: Send + Sync {
    async fn step(
        &self,
        query: &str,
        prior: &[Citation],
        step_idx: u32,
    ) -> Result<StepOutcome, DeepResearchError>;

    async fn synthesize(
        &self,
        query: &str,
        citations: &[Citation],
        evidence: &[String],
    ) -> Result<String, DeepResearchError>;
}

/// Policy inputs governing whether a run is permitted and how it is billed.
#[derive(Clone, Debug)]
pub struct ResearchPolicy {
    pub entitlement: ResearchEntitlement,
    pub model: String,
    /// True for Maho-managed billing (frontier gate applies); false for BYOK.
    pub managed: bool,
}

/// Run a bounded deep-research loop. Gating is enforced first, then the loop
/// runs under strict caps and always terminates.
pub async fn run_deep_research(
    query: &str,
    policy: &ResearchPolicy,
    budget: ResearchBudget,
    stepper: &dyn ResearchStepper,
) -> Result<DeepResearchReport, DeepResearchError> {
    if !policy.entitlement.allows_deep_research() {
        return Err(DeepResearchError::TierNotEntitled);
    }
    if policy.managed && is_frontier_model(&policy.model) {
        return Err(DeepResearchError::FrontierModelBlocked(
            policy.model.clone(),
        ));
    }

    let budget = budget.clamped();
    let start = Instant::now();
    let mut citations: Vec<Citation> = Vec::new();
    let mut evidence: Vec<String> = Vec::new();
    let mut steps: u32 = 0;
    let mut cost: f64 = 0.0;

    let stop_reason = loop {
        if steps >= budget.max_steps {
            break StopReason::MaxStepsReached;
        }
        if start.elapsed() >= budget.max_wall_clock {
            break StopReason::Timeout;
        }
        if cost >= budget.max_cost_usd {
            break StopReason::BudgetExceeded;
        }

        match stepper.step(query, &citations, steps).await {
            Ok(outcome) => {
                cost += outcome.step_cost_usd.max(0.0);
                citations.extend(outcome.citations);
                if !outcome.evidence_note.is_empty() {
                    evidence.push(outcome.evidence_note);
                }
                steps += 1;
                if outcome.done {
                    break StopReason::Completed;
                }
            }
            Err(DeepResearchError::ToolDenied) => break StopReason::ToolDenied,
            Err(other) => return Err(other),
        }
    };

    let summary = stepper.synthesize(query, &citations, &evidence).await?;

    Ok(DeepResearchReport {
        summary,
        citations,
        evidence,
        steps_taken: steps,
        estimated_cost_usd: cost,
        stop_reason,
        model: policy.model.clone(),
    })
}

/// Extract bare `http(s)` URLs from free text, trimming trailing punctuation.
pub fn extract_urls(text: &str) -> Vec<String> {
    let mut out = Vec::new();
    for token in
        text.split(|c: char| c.is_whitespace() || c == '(' || c == ')' || c == '<' || c == '>')
    {
        if token.starts_with("http://") || token.starts_with("https://") {
            let trimmed = token.trim_end_matches(['.', ',', ')', ']', '"', '\'', ';', ':']);
            if trimmed.len() > "https://".len() && !out.iter().any(|u| u == trimmed) {
                out.push(trimmed.to_string());
            }
        }
    }
    out
}

const RESEARCH_COMPLETE_SENTINEL: &str = "RESEARCH_COMPLETE";

/// A `ResearchStepper` backed by any `AgentRuntime` (e.g. `SwiftideBackend`).
/// Each step issues a scoped research turn; citations are the URLs the model
/// surfaces. The step's cost is an estimate the caller supplies per step, so
/// the loop's budget accounting stays honest without needing token telemetry.
pub struct AgentResearchStepper {
    runtime: Arc<dyn AgentRuntime>,
    session_id: String,
    per_step_cost_usd: f64,
}

impl AgentResearchStepper {
    pub fn new(
        runtime: Arc<dyn AgentRuntime>,
        session_id: impl Into<String>,
        per_step_cost_usd: f64,
    ) -> Self {
        Self {
            runtime,
            session_id: session_id.into(),
            per_step_cost_usd,
        }
    }

    async fn ask(&self, prompt: String) -> Result<String, DeepResearchError> {
        let msg = ChatMessage::user(ChatContent::text(prompt));
        let reply = self
            .runtime
            .run_turn_simple(&self.session_id, msg, None, None)
            .await
            .map_err(|e| match e {
                AgentError::PermissionDenied => DeepResearchError::ToolDenied,
                other => DeepResearchError::Stepper(other.to_string()),
            })?;
        match reply.content {
            ChatContent::Text(t) => Ok(t),
            _ => Ok(String::new()),
        }
    }
}

#[async_trait]
impl ResearchStepper for AgentResearchStepper {
    async fn step(
        &self,
        query: &str,
        prior: &[Citation],
        step_idx: u32,
    ) -> Result<StepOutcome, DeepResearchError> {
        let known = if prior.is_empty() {
            "none yet".to_string()
        } else {
            prior
                .iter()
                .map(|c| c.url.as_str())
                .collect::<Vec<_>>()
                .join(", ")
        };
        let prompt = format!(
            "You are running research step {step} on: {query}\n\
             Sources gathered so far: {known}\n\
             Investigate ONE new aspect using available browser/search tools. \
             Report concise findings and include every source URL you used. \
             When you have gathered enough to answer thoroughly, end your reply \
             with the token {sentinel} on its own line.",
            step = step_idx + 1,
            query = query,
            known = known,
            sentinel = RESEARCH_COMPLETE_SENTINEL,
        );
        let text = self.ask(prompt).await?;
        let done = text.contains(RESEARCH_COMPLETE_SENTINEL);
        let citations = extract_urls(&text)
            .into_iter()
            .map(|url| Citation {
                url,
                note: String::new(),
            })
            .collect();
        Ok(StepOutcome {
            done,
            citations,
            evidence_note: text
                .replace(RESEARCH_COMPLETE_SENTINEL, "")
                .trim()
                .to_string(),
            step_cost_usd: self.per_step_cost_usd,
        })
    }

    async fn synthesize(
        &self,
        query: &str,
        citations: &[Citation],
        evidence: &[String],
    ) -> Result<String, DeepResearchError> {
        let sources = citations
            .iter()
            .map(|c| format!("- {}", c.url))
            .collect::<Vec<_>>()
            .join("\n");
        let notes = evidence.join("\n\n");
        let prompt = format!(
            "Synthesize a cited answer to: {query}\n\n\
             Evidence gathered:\n{notes}\n\n\
             Sources:\n{sources}\n\n\
             Write a concise summary and cite sources inline where relevant.",
        );
        self.ask(prompt).await
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::atomic::{AtomicU32, Ordering};

    fn max_policy(managed: bool, model: &str) -> ResearchPolicy {
        ResearchPolicy {
            entitlement: ResearchEntitlement {
                tier: UserTier::Max,
                payg_credits_usd: 0.0,
            },
            model: model.to_string(),
            managed,
        }
    }

    /// A stepper that never signals `done` and reports a fixed per-step cost —
    /// used to prove the loop stops on its own via the caps.
    struct NeverDoneStepper {
        step_cost: f64,
        calls: AtomicU32,
    }

    #[async_trait]
    impl ResearchStepper for NeverDoneStepper {
        async fn step(
            &self,
            _q: &str,
            _p: &[Citation],
            idx: u32,
        ) -> Result<StepOutcome, DeepResearchError> {
            self.calls.fetch_add(1, Ordering::SeqCst);
            Ok(StepOutcome {
                done: false,
                citations: vec![Citation {
                    url: format!("https://example.com/{idx}"),
                    note: String::new(),
                }],
                evidence_note: format!("finding {idx}"),
                step_cost_usd: self.step_cost,
            })
        }
        async fn synthesize(
            &self,
            _q: &str,
            c: &[Citation],
            _e: &[String],
        ) -> Result<String, DeepResearchError> {
            Ok(format!("summary with {} sources", c.len()))
        }
    }

    #[tokio::test]
    async fn stops_at_max_steps_when_model_never_completes() {
        let stepper = NeverDoneStepper {
            step_cost: 0.0,
            calls: AtomicU32::new(0),
        };
        let budget = ResearchBudget {
            max_steps: 3,
            max_wall_clock: Duration::from_secs(60),
            max_cost_usd: 100.0,
        };
        let report = run_deep_research("q", &max_policy(false, "byok/model"), budget, &stepper)
            .await
            .expect("should produce a bounded report");
        assert_eq!(report.stop_reason, StopReason::MaxStepsReached);
        assert_eq!(report.steps_taken, 3);
        assert_eq!(stepper.calls.load(Ordering::SeqCst), 3);
        assert_eq!(report.citations.len(), 3);
    }

    #[tokio::test]
    async fn stops_on_budget_before_max_steps() {
        let stepper = NeverDoneStepper {
            step_cost: 0.05,
            calls: AtomicU32::new(0),
        };
        let budget = ResearchBudget {
            max_steps: 100,
            max_wall_clock: Duration::from_secs(60),
            max_cost_usd: 0.10,
        };
        let report = run_deep_research("q", &max_policy(false, "byok/model"), budget, &stepper)
            .await
            .expect("bounded report");
        assert_eq!(report.stop_reason, StopReason::BudgetExceeded);
        // 2 steps => cost 0.10 >= cap; loop breaks before a 3rd step.
        assert_eq!(report.steps_taken, 2);
        assert!(report.estimated_cost_usd >= 0.10);
    }

    #[tokio::test]
    async fn stops_immediately_on_zero_time_budget() {
        let stepper = NeverDoneStepper {
            step_cost: 0.0,
            calls: AtomicU32::new(0),
        };
        let budget = ResearchBudget {
            max_steps: 100,
            max_wall_clock: Duration::ZERO,
            max_cost_usd: 100.0,
        };
        let report = run_deep_research("q", &max_policy(false, "byok/model"), budget, &stepper)
            .await
            .expect("bounded report");
        assert_eq!(report.stop_reason, StopReason::Timeout);
        assert_eq!(report.steps_taken, 0);
    }

    #[tokio::test]
    async fn clamps_max_steps_to_ceiling() {
        let stepper = NeverDoneStepper {
            step_cost: 0.0,
            calls: AtomicU32::new(0),
        };
        let budget = ResearchBudget {
            max_steps: 10_000,
            max_wall_clock: Duration::from_secs(60),
            max_cost_usd: 1e9,
        };
        let report = run_deep_research("q", &max_policy(false, "byok/model"), budget, &stepper)
            .await
            .expect("bounded report");
        assert_eq!(report.steps_taken, MAX_STEPS_CEILING);
        assert_eq!(report.stop_reason, StopReason::MaxStepsReached);
    }

    struct CompletesStepper;
    #[async_trait]
    impl ResearchStepper for CompletesStepper {
        async fn step(
            &self,
            _q: &str,
            _p: &[Citation],
            idx: u32,
        ) -> Result<StepOutcome, DeepResearchError> {
            Ok(StepOutcome {
                done: idx >= 1,
                citations: vec![Citation {
                    url: "https://src.example/a".into(),
                    note: String::new(),
                }],
                evidence_note: "note".into(),
                step_cost_usd: 0.01,
            })
        }
        async fn synthesize(
            &self,
            _q: &str,
            _c: &[Citation],
            _e: &[String],
        ) -> Result<String, DeepResearchError> {
            Ok("done".into())
        }
    }

    #[tokio::test]
    async fn completes_when_model_signals_done() {
        let report = run_deep_research(
            "q",
            &max_policy(true, "google/gemini-3-flash-lite:free"),
            ResearchBudget::managed_default(),
            &CompletesStepper,
        )
        .await
        .expect("bounded report");
        assert_eq!(report.stop_reason, StopReason::Completed);
        assert_eq!(report.steps_taken, 2);
    }

    struct DeniedStepper;
    #[async_trait]
    impl ResearchStepper for DeniedStepper {
        async fn step(
            &self,
            _q: &str,
            _p: &[Citation],
            _i: u32,
        ) -> Result<StepOutcome, DeepResearchError> {
            Err(DeepResearchError::ToolDenied)
        }
        async fn synthesize(
            &self,
            _q: &str,
            _c: &[Citation],
            _e: &[String],
        ) -> Result<String, DeepResearchError> {
            Ok(String::new())
        }
    }

    #[tokio::test]
    async fn tool_denial_stops_without_error() {
        let report = run_deep_research(
            "q",
            &max_policy(false, "byok/model"),
            ResearchBudget::managed_default(),
            &DeniedStepper,
        )
        .await
        .expect("bounded report");
        assert_eq!(report.stop_reason, StopReason::ToolDenied);
        assert_eq!(report.steps_taken, 0);
    }

    #[tokio::test]
    async fn free_and_pro_are_not_entitled() {
        let stepper = CompletesStepper;
        for tier in [UserTier::Free, UserTier::Pro] {
            let policy = ResearchPolicy {
                entitlement: ResearchEntitlement {
                    tier,
                    payg_credits_usd: 0.0,
                },
                model: "google/gemini-3-flash-lite:free".into(),
                managed: true,
            };
            let err = run_deep_research("q", &policy, ResearchBudget::managed_default(), &stepper)
                .await
                .unwrap_err();
            assert!(matches!(err, DeepResearchError::TierNotEntitled));
        }
    }

    #[tokio::test]
    async fn payg_credits_entitle_non_max_tier() {
        let policy = ResearchPolicy {
            entitlement: ResearchEntitlement {
                tier: UserTier::Pro,
                payg_credits_usd: 2.5,
            },
            model: "google/gemini-3-flash-lite:free".into(),
            managed: true,
        };
        let report = run_deep_research(
            "q",
            &policy,
            ResearchBudget::managed_default(),
            &CompletesStepper,
        )
        .await
        .expect("payg user is entitled");
        assert_eq!(report.stop_reason, StopReason::Completed);
    }

    #[tokio::test]
    async fn managed_blocks_frontier_model() {
        let err = run_deep_research(
            "q",
            &max_policy(true, "anthropic/claude-opus-4"),
            ResearchBudget::managed_default(),
            &CompletesStepper,
        )
        .await
        .unwrap_err();
        assert!(matches!(err, DeepResearchError::FrontierModelBlocked(_)));
    }

    #[tokio::test]
    async fn byok_allows_frontier_model() {
        let report = run_deep_research(
            "q",
            &max_policy(false, "anthropic/claude-opus-4"),
            ResearchBudget::managed_default(),
            &CompletesStepper,
        )
        .await
        .expect("byok may use frontier models");
        assert_eq!(report.stop_reason, StopReason::Completed);
    }

    #[test]
    fn frontier_detection_matches_billing_proxy_patterns() {
        assert!(is_frontier_model("anthropic/claude-opus-4"));
        assert!(is_frontier_model("openai/o3-mini"));
        assert!(is_frontier_model("openai/gpt-5"));
        assert!(is_frontier_model("google/gemini-3-ultra"));
        assert!(!is_frontier_model("openai/gpt-5-mini"));
        assert!(!is_frontier_model("openai/gpt-5-nano"));
        assert!(!is_frontier_model("google/gemini-3-flash-lite:free"));
    }

    #[test]
    fn extract_urls_dedupes_and_trims_punctuation() {
        let text =
            "See https://a.example/x. and (https://b.example/y) and https://a.example/x again";
        let urls = extract_urls(text);
        assert_eq!(
            urls,
            vec![
                "https://a.example/x".to_string(),
                "https://b.example/y".to_string()
            ]
        );
    }
}
