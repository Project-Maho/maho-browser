use crate::browser_cli::schema::{BenchmarkError, BenchmarkResult, DriverType};
use crate::browser_cli::workloads::WorkloadStep;
use std::time::Instant;

#[derive(Debug, Clone, PartialEq)]
pub struct DriverStepOutcome {
    pub success: bool,
    pub message: String,
    pub rpc_count: usize,
    pub snapshot_duration_ms: Option<f64>,
    pub wire_bytes: usize,
}

pub trait BenchmarkDriver {
    fn driver_type(&self) -> DriverType;
    fn reset(&mut self, workload_id: &str) -> Result<(), BenchmarkError>;
    fn execute_step(&mut self, step: &WorkloadStep) -> Result<DriverStepOutcome, BenchmarkError>;
    fn get_metrics(&self) -> Result<BenchmarkResult, BenchmarkError>;
}

#[derive(Debug, Clone)]
pub struct FakeDriver {
    driver_type: DriverType,
    workload_id: String,
    start_time: Instant,
    elapsed_ms_override: Option<u64>,
    rpc_count: usize,
    decision_turns: usize,
    snapshot_count: usize,
    snapshot_timings: Vec<f64>,
    wire_bytes: usize,
    retries: usize,
}

impl FakeDriver {
    pub fn new(driver_type: DriverType) -> Self {
        Self {
            driver_type,
            workload_id: String::new(),
            start_time: Instant::now(),
            elapsed_ms_override: None,
            rpc_count: 0,
            decision_turns: 0,
            snapshot_count: 0,
            snapshot_timings: Vec::new(),
            wire_bytes: 0,
            retries: 0,
        }
    }

    pub fn with_elapsed_ms(mut self, elapsed_ms: u64) -> Self {
        self.elapsed_ms_override = Some(elapsed_ms);
        self
    }
}

impl BenchmarkDriver for FakeDriver {
    fn driver_type(&self) -> DriverType {
        self.driver_type
    }

    fn reset(&mut self, workload_id: &str) -> Result<(), BenchmarkError> {
        self.workload_id = workload_id.to_string();
        self.start_time = Instant::now();
        self.rpc_count = 0;
        self.decision_turns = 0;
        self.snapshot_count = 0;
        self.snapshot_timings.clear();
        self.wire_bytes = 0;
        self.retries = 0;
        Ok(())
    }

    fn execute_step(&mut self, step: &WorkloadStep) -> Result<DriverStepOutcome, BenchmarkError> {
        self.decision_turns += 1;
        match step {
            WorkloadStep::Navigate { url } => {
                self.rpc_count += 1;
                let bytes = 256 + url.len();
                self.wire_bytes += bytes;
                Ok(DriverStepOutcome {
                    success: true,
                    message: format!("Navigated to {url}"),
                    rpc_count: 1,
                    snapshot_duration_ms: None,
                    wire_bytes: bytes,
                })
            }
            WorkloadStep::TakeSnapshot => {
                self.rpc_count += 1;
                self.snapshot_count += 1;
                // Simulated snapshot duration ms
                let duration_ms = match self.driver_type {
                    DriverType::RawMcp => 14.5,
                    DriverType::Repl => 8.2,
                    DriverType::SemanticCli => 4.1,
                };
                self.snapshot_timings.push(duration_ms);
                let bytes = 1024;
                self.wire_bytes += bytes;
                Ok(DriverStepOutcome {
                    success: true,
                    message: "AX snapshot captured".into(),
                    rpc_count: 1,
                    snapshot_duration_ms: Some(duration_ms),
                    wire_bytes: bytes,
                })
            }
            WorkloadStep::Click { selector } => {
                self.rpc_count += 1;
                let bytes = 128 + selector.len();
                self.wire_bytes += bytes;
                Ok(DriverStepOutcome {
                    success: true,
                    message: format!("Clicked {selector}"),
                    rpc_count: 1,
                    snapshot_duration_ms: None,
                    wire_bytes: bytes,
                })
            }
            WorkloadStep::Fill { selector, value } => {
                self.rpc_count += 1;
                let bytes = 128 + selector.len() + value.len();
                self.wire_bytes += bytes;
                Ok(DriverStepOutcome {
                    success: true,
                    message: format!("Filled {selector} with '{value}'"),
                    rpc_count: 1,
                    snapshot_duration_ms: None,
                    wire_bytes: bytes,
                })
            }
            WorkloadStep::SelectOption { selector, value } => {
                self.rpc_count += 1;
                let bytes = 128 + selector.len() + value.len();
                self.wire_bytes += bytes;
                Ok(DriverStepOutcome {
                    success: true,
                    message: format!("Selected option '{value}' in {selector}"),
                    rpc_count: 1,
                    snapshot_duration_ms: None,
                    wire_bytes: bytes,
                })
            }
            WorkloadStep::Check { selector } => {
                self.rpc_count += 1;
                let bytes = 96 + selector.len();
                self.wire_bytes += bytes;
                Ok(DriverStepOutcome {
                    success: true,
                    message: format!("Checked {selector}"),
                    rpc_count: 1,
                    snapshot_duration_ms: None,
                    wire_bytes: bytes,
                })
            }
            WorkloadStep::AssertText { selector, expected } => {
                self.rpc_count += 1;
                let bytes = 64 + selector.len() + expected.len();
                self.wire_bytes += bytes;
                Ok(DriverStepOutcome {
                    success: true,
                    message: format!("Asserted text '{expected}' at {selector}"),
                    rpc_count: 1,
                    snapshot_duration_ms: None,
                    wire_bytes: bytes,
                })
            }
            WorkloadStep::WaitUntil { selector } => {
                self.rpc_count += 1;
                let bytes = 64 + selector.len();
                self.wire_bytes += bytes;
                Ok(DriverStepOutcome {
                    success: true,
                    message: format!("Waited for {selector}"),
                    rpc_count: 1,
                    snapshot_duration_ms: None,
                    wire_bytes: bytes,
                })
            }
            WorkloadStep::Custom { name, .. } => {
                self.rpc_count += 1;
                let bytes = 256;
                self.wire_bytes += bytes;
                Ok(DriverStepOutcome {
                    success: true,
                    message: format!("Executed custom action {name}"),
                    rpc_count: 1,
                    snapshot_duration_ms: None,
                    wire_bytes: bytes,
                })
            }
        }
    }

    fn get_metrics(&self) -> Result<BenchmarkResult, BenchmarkError> {
        let wall_ms = self
            .elapsed_ms_override
            .unwrap_or_else(|| self.start_time.elapsed().as_millis() as u64);

        Ok(BenchmarkResult {
            workload: self.workload_id.clone(),
            driver: self.driver_type,
            goal_completion_wall_ms: wall_ms,
            agent_decision_turns: self.decision_turns,
            browser_rpc_count: self.rpc_count,
            ax_snapshot_count: self.snapshot_count,
            ax_snapshot_wall_ms: self.snapshot_timings.clone(),
            observation_wire_bytes: self.wire_bytes,
            retries: self.retries,
        })
    }
}
