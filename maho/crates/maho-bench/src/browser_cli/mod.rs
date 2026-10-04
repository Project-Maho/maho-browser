pub mod driver;
pub mod fixtures;
pub mod harness;
pub mod schema;
pub mod workloads;

pub use driver::{BenchmarkDriver, DriverStepOutcome, FakeDriver};
pub use fixtures::{
    get_embedded_fixture, validate_all_fixtures, validate_fixture_content, FixtureValidationError,
};
pub use harness::{
    run_benchmark, run_self_test, run_self_test_with_config, BenchmarkHarness, HarnessConfig,
};
pub use schema::{BenchmarkError, BenchmarkResult, DriverType};
pub use workloads::{
    all_workloads, get_workload, w1_static_form_workload, w2_spa_modal_workload,
    w3_dynamic_list_workload, w4_iframe_page_workload, w5_large_ax_tree_workload,
    w6_navigation_transition_workload, WorkloadDefinition, WorkloadStep,
};
