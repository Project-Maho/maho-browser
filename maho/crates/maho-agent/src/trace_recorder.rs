// Trace v3 recorder: converts a live agent browsing session into a replayable
// routine definition. Steps are timestamped, rapid typing coalesces into a
// single fill, and a hover immediately preceding a click links to that click.

use serde::{Deserialize, Serialize};
use std::time::{Duration, Instant};

/// Window under which consecutive fills are treated as one typing stream.
pub const DEFAULT_COALESCE_WINDOW_MS: u64 = 400;

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq)]
#[serde(tag = "kind", rename_all = "snake_case")]
pub enum TraceStep {
    Click {
        target_ref: Option<String>,
        x: Option<f64>,
        y: Option<f64>,
    },
    Fill {
        selector: Option<String>,
        text: String,
    },
    Navigate {
        url: String,
    },
    Wait {
        ms: u64,
    },
}

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq)]
pub struct TimestampedStep {
    pub at_ms: u64,
    pub step: TraceStep,
}

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq)]
pub struct TraceV3 {
    pub version: u32,
    pub steps: Vec<TimestampedStep>,
}

#[derive(Clone, Debug)]
enum PendingHover {
    None,
    Ref(Option<String>),
}

/// Mutable recorder. Time is injectable so coalescing is testable without
/// real sleeps: |now_fn| returns the logical instant used for at_ms fields
/// and coalescing windows.
pub struct TraceRecorder<F: Fn() -> Instant> {
    steps: Vec<TimestampedStep>,
    start: Instant,
    now_fn: F,
    coalesce_window: Duration,
    pending_hover: PendingHover,
}

impl Default for TraceRecorder<fn() -> Instant> {
    fn default() -> Self {
        Self::new()
    }
}

impl TraceRecorder<fn() -> Instant> {
    pub fn new() -> Self {
        TraceRecorder::with_window(DEFAULT_COALESCE_WINDOW_MS)
    }

    pub fn with_window(coalesce_window_ms: u64) -> Self {
        TraceRecorder {
            steps: Vec::new(),
            start: Instant::now(),
            now_fn: Instant::now,
            coalesce_window: Duration::from_millis(coalesce_window_ms),
            pending_hover: PendingHover::None,
        }
    }
}

impl<F: Fn() -> Instant> TraceRecorder<F> {
    fn now_ms(&self) -> u64 {
        (self.now_fn)().duration_since(self.start).as_millis() as u64
    }

    fn push(&mut self, step: TraceStep) {
        let at_ms = self.now_ms();
        self.steps.push(TimestampedStep { at_ms, step });
    }

    /// Records a click. A pending hover ref (see record_hover) is linked as
    /// the target when the click carries no explicit ref.
    pub fn record_click(&mut self, target_ref: Option<String>, x: f64, y: f64) {
        let target_ref = match (target_ref, &self.pending_hover) {
            (None, PendingHover::Ref(hover_ref)) => hover_ref.clone(),
            (explicit, _) => explicit,
        };
        self.pending_hover = PendingHover::None;
        self.push(TraceStep::Click {
            target_ref,
            x: Some(x),
            y: Some(y),
        });
    }

    /// Records typed text. Consecutive fills inside the coalesce window that
    /// target the same selector (or carry no selector) merge into the previous
    /// fill (text concatenated); the merged step keeps the FIRST fill's
    /// timestamp and selector. A fill for a different field starts a new step.
    pub fn record_fill(&mut self, text: String, selector: Option<String>) {
        let now_ms = self.now_ms();
        if let Some(last) = self.steps.last_mut() {
            if let TraceStep::Fill {
                selector: prev_selector,
                text: prev_text,
            } = &mut last.step
            {
                let within = now_ms.saturating_sub(last.at_ms)
                    <= self.coalesce_window.as_millis() as u64;
                let same_target =
                    selector.is_none() || prev_selector.is_none() || *prev_selector == selector;
                if within && same_target {
                    prev_text.push_str(&text);
                    if prev_selector.is_none() {
                        *prev_selector = selector;
                    }
                    self.pending_hover = PendingHover::None;
                    return;
                }
            }
        }
        self.steps.push(TimestampedStep {
            at_ms: now_ms,
            step: TraceStep::Fill { selector, text },
        });
        self.pending_hover = PendingHover::None;
    }

    /// Records a navigation. Query and fragment are stripped so OAuth codes,
    /// tokens and other secrets carried in URLs never land in a trace.
    pub fn record_navigate(&mut self, url: String) {
        self.push(TraceStep::Navigate {
            url: redact_navigate_url(&url),
        });
    }

    pub fn record_wait(&mut self, ms: u64) {
        self.push(TraceStep::Wait { ms });
    }

    /// Sets a hover that links to the next click without an explicit ref.
    pub fn record_hover(&mut self, target_ref: Option<String>) {
        self.pending_hover = PendingHover::Ref(target_ref);
    }

    /// Finishes recording and returns the frozen trace.
    pub fn finish(self) -> TraceV3 {
        TraceV3 {
            version: 3,
            steps: self.steps,
        }
    }
}

/// Keeps scheme/host/path of a navigate URL and drops query + fragment.
/// Unparseable input is cut at the first '?' or '#'.
fn redact_navigate_url(raw: &str) -> String {
    match reqwest::Url::parse(raw) {
        Ok(mut url) => {
            url.set_query(None);
            url.set_fragment(None);
            url.to_string()
        }
        Err(_) => raw.split(['?', '#']).next().unwrap_or_default().to_string(),
    }
}

impl TraceV3 {
    pub fn to_json(&self) -> Result<String, serde_json::Error> {
        serde_json::to_string(self)
    }

    pub fn from_json(s: &str) -> Result<TraceV3, serde_json::Error> {
        serde_json::from_str(s)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::cell::Cell;

    #[test]
    fn typing_coalesced_to_single_fill() {
        let base = Instant::now();
        let current = Cell::new(base);
        let mut recorder: TraceRecorder<_> = TraceRecorder {
            steps: Vec::new(),
            start: base,
            now_fn: || current.get(),
            coalesce_window: Duration::from_millis(400),
            pending_hover: PendingHover::None,
        };
        recorder.record_fill("hel".into(), Some("#q".into()));
        current.set(base + Duration::from_millis(100));
        recorder.record_fill("lo".into(), None);
        current.set(base + Duration::from_millis(200));
        recorder.record_fill(" world".into(), None);
        let trace = recorder.finish();
        assert_eq!(trace.steps.len(), 1);
        match &trace.steps[0].step {
            TraceStep::Fill { selector, text } => {
                assert_eq!(text, "hello world");
                assert_eq!(selector.as_deref(), Some("#q"));
            }
            other => panic!("expected fill, got {other:?}"),
        }
        assert_eq!(trace.steps[0].at_ms, 0);
    }

    #[test]
    fn slow_typing_not_coalesced() {
        let base = Instant::now();
        let current = Cell::new(base);
        let mut recorder: TraceRecorder<_> = TraceRecorder {
            steps: Vec::new(),
            start: base,
            now_fn: || current.get(),
            coalesce_window: Duration::from_millis(400),
            pending_hover: PendingHover::None,
        };
        recorder.record_fill("a".into(), None);
        current.set(base + Duration::from_millis(500));
        recorder.record_fill("b".into(), None);
        let trace = recorder.finish();
        assert_eq!(trace.steps.len(), 2);
    }

    #[test]
    fn hover_links_to_next_click() {
        let base = Instant::now();
        let mut recorder: TraceRecorder<_> = TraceRecorder {
            steps: Vec::new(),
            start: base,
            now_fn: || base,
            coalesce_window: Duration::from_millis(400),
            pending_hover: PendingHover::None,
        };
        recorder.record_hover(Some("@e3".into()));
        recorder.record_click(None, 10.0, 20.0);
        recorder.record_click(Some("@e7".into()), 30.0, 40.0);
        let trace = recorder.finish();
        match &trace.steps[0].step {
            TraceStep::Click { target_ref, .. } => {
                assert_eq!(target_ref.as_deref(), Some("@e3"));
            }
            other => panic!("expected click, got {other:?}"),
        }
        // The explicit ref on the second click must NOT pick up the stale hover.
        match &trace.steps[1].step {
            TraceStep::Click { target_ref, .. } => {
                assert_eq!(target_ref.as_deref(), Some("@e7"));
            }
            other => panic!("expected click, got {other:?}"),
        }
    }

    #[test]
    fn round_trip_json() {
        let base = Instant::now();
        let mut recorder: TraceRecorder<_> = TraceRecorder {
            steps: Vec::new(),
            start: base,
            now_fn: || base,
            coalesce_window: Duration::from_millis(400),
            pending_hover: PendingHover::None,
        };
        recorder.record_navigate("https://example.test".into());
        recorder.record_click(Some("@e1".into()), 1.0, 2.0);
        recorder.record_fill("secret".into(), Some("#pw".into()));
        recorder.record_wait(250);
        let trace = recorder.finish();
        let json = trace.to_json().expect("serialize");
        let parsed = TraceV3::from_json(&json).expect("deserialize");
        assert_eq!(parsed, trace);
    }

    #[test]
    fn empty_trace_serializes() {
        let trace = TraceRecorder::<fn() -> Instant>::with_window(400).finish();
        assert!(trace.steps.is_empty());
        let json = trace.to_json().expect("serialize");
        let parsed = TraceV3::from_json(&json).expect("deserialize");
        assert_eq!(parsed, trace);
    }

    #[test]
    fn version_always_three() {
        let base = Instant::now();
        let trace = TraceRecorder::<fn() -> Instant>::with_window(400).finish();
        assert_eq!(trace.version, 3);
        let _ = base;
    }

    #[test]
    fn navigate_strips_query_and_fragment_secrets() {
        let base = Instant::now();
        let mut recorder: TraceRecorder<_> = TraceRecorder {
            steps: Vec::new(),
            start: base,
            now_fn: || base,
            coalesce_window: Duration::from_millis(400),
            pending_hover: PendingHover::None,
        };
        recorder.record_navigate(
            "https://app.example/callback?code=SECRET&state=x#access_token=TOKEN".into(),
        );
        let trace = recorder.finish();
        assert_eq!(
            trace.steps[0].step,
            TraceStep::Navigate {
                url: "https://app.example/callback".into()
            }
        );
    }

    #[test]
    fn fills_for_different_selectors_are_not_coalesced() {
        let base = Instant::now();
        let mut recorder: TraceRecorder<_> = TraceRecorder {
            steps: Vec::new(),
            start: base,
            now_fn: || base,
            coalesce_window: Duration::from_millis(400),
            pending_hover: PendingHover::None,
        };
        recorder.record_fill("alice".into(), Some("#user".into()));
        recorder.record_fill("hunter2".into(), Some("#pw".into()));
        let trace = recorder.finish();
        assert_eq!(trace.steps.len(), 2);
    }
}
