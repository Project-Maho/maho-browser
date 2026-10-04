use serde::{Deserialize, Serialize};
use thiserror::Error;

#[derive(Debug, Error)]
pub enum BenchmarkError {
    #[error("I/O error: {0}")]
    Io(#[from] std::io::Error),
    #[error("Serialization error: {0}")]
    Serialization(#[from] serde_json::Error),
    #[error("Workload error: {0}")]
    Workload(String),
    #[error("Driver error: {0}")]
    Driver(String),
    #[error("Fixture validation failed: {0}")]
    FixtureValidation(String),
    #[error("Execution failed: {0}")]
    Execution(String),
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum DriverType {
    RawMcp,
    Repl,
    SemanticCli,
}

impl std::fmt::Display for DriverType {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            DriverType::RawMcp => write!(f, "raw_mcp"),
            DriverType::Repl => write!(f, "repl"),
            DriverType::SemanticCli => write!(f, "semantic_cli"),
        }
    }
}

impl std::str::FromStr for DriverType {
    type Err = BenchmarkError;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        match s {
            "raw_mcp" => Ok(DriverType::RawMcp),
            "repl" => Ok(DriverType::Repl),
            "semantic_cli" => Ok(DriverType::SemanticCli),
            _ => Err(BenchmarkError::Driver(format!("Unknown driver type: {s}"))),
        }
    }
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct BenchmarkResult {
    pub workload: String,
    pub driver: DriverType,
    pub goal_completion_wall_ms: u64,
    pub agent_decision_turns: usize,
    pub browser_rpc_count: usize,
    pub ax_snapshot_count: usize,
    pub ax_snapshot_wall_ms: Vec<f64>,
    pub observation_wire_bytes: usize,
    pub retries: usize,
}
