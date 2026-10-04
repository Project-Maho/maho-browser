use crate::browser_cli::driver::{BenchmarkDriver, FakeDriver};
use crate::browser_cli::fixtures::validate_all_fixtures;
use crate::browser_cli::schema::{BenchmarkError, BenchmarkResult, DriverType};
use crate::browser_cli::workloads::{get_workload, w1_static_form_workload, WorkloadDefinition};
use std::path::{Path, PathBuf};

#[derive(Debug, Clone)]
pub struct HarnessConfig {
    pub output_dir: Option<PathBuf>,
    pub fixture_base_dir: Option<PathBuf>,
}

impl Default for HarnessConfig {
    fn default() -> Self {
        Self {
            output_dir: Some(PathBuf::from("browser_cli/sample-results")),
            fixture_base_dir: None,
        }
    }
}

pub struct BenchmarkHarness {
    config: HarnessConfig,
}

impl BenchmarkHarness {
    pub fn new(config: HarnessConfig) -> Self {
        Self { config }
    }

    pub fn run(
        &self,
        driver: &mut dyn BenchmarkDriver,
        workload: &WorkloadDefinition,
    ) -> Result<BenchmarkResult, BenchmarkError> {
        run_benchmark(driver, workload)
    }

    pub fn run_self_test(
        &self,
        custom_out_path: Option<&Path>,
    ) -> Result<BenchmarkResult, BenchmarkError> {
        run_self_test_with_config(&self.config, custom_out_path)
    }
}

pub fn run_benchmark(
    driver: &mut dyn BenchmarkDriver,
    workload: &WorkloadDefinition,
) -> Result<BenchmarkResult, BenchmarkError> {
    driver.reset(&workload.id)?;

    for step in &workload.steps {
        let outcome = driver.execute_step(step)?;
        if !outcome.success {
            return Err(BenchmarkError::Execution(format!(
                "Step execution failed in workload {}: {}",
                workload.id, outcome.message
            )));
        }
    }

    driver.get_metrics()
}

pub fn run_self_test(custom_out_path: Option<&Path>) -> Result<BenchmarkResult, BenchmarkError> {
    let default_config = HarnessConfig::default();
    run_self_test_with_config(&default_config, custom_out_path)
}

pub fn run_self_test_with_config(
    config: &HarnessConfig,
    custom_out_path: Option<&Path>,
) -> Result<BenchmarkResult, BenchmarkError> {
    // 1. Validate all 6 fixtures
    validate_all_fixtures(config.fixture_base_dir.as_deref())?;

    // 2. Run W1 static form workload end-to-end with FakeDriver
    let workload = get_workload("w1_static_form").unwrap_or_else(w1_static_form_workload);
    let mut driver = FakeDriver::new(DriverType::RawMcp);
    let result = run_benchmark(&mut driver, &workload)?;

    // 3. Determine output file path
    let out_path = if let Some(path) = custom_out_path {
        path.to_path_buf()
    } else if let Some(ref dir) = config.output_dir {
        dir.join("selftest.json")
    } else {
        PathBuf::from("browser_cli/sample-results/selftest.json")
    };

    if let Some(parent) = out_path.parent() {
        if !parent.as_os_str().is_empty() {
            std::fs::create_dir_all(parent)?;
        }
    }

    // 4. Write sample-results/selftest.json
    let json_bytes = serde_json::to_vec_pretty(&result)?;
    std::fs::write(&out_path, json_bytes)?;

    // 5. Read back and verify exact schema round-trip
    let read_back_str = std::fs::read_to_string(&out_path)?;
    let parsed: BenchmarkResult = serde_json::from_str(&read_back_str)?;

    if parsed != result {
        return Err(BenchmarkError::Execution(
            "Self-test schema round-trip assertion failed: parsed result does not match original"
                .into(),
        ));
    }

    Ok(parsed)
}
