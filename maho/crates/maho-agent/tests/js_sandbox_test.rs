use maho_agent::js_sandbox::{eval_json, execute_tool, JsSandboxError};
use maho_agent::tool_registry::{get_platform_tools, js_eval_definition};

#[test]
fn sums_large_input_and_returns_json_result() {
    let values: Vec<u64> = (0..10_000).collect();
    let output = eval_json(
        "input.reduce((sum, value) => sum + value, 0)",
        &serde_json::json!(values),
    )
    .expect("bounded calculation succeeds");
    assert_eq!(output.result, serde_json::json!(49_995_000));
}

#[test]
fn interrupts_infinite_loop() {
    assert_eq!(
        eval_json("while (true) {}", &serde_json::Value::Null),
        Err(JsSandboxError::Timeout)
    );
}

#[test]
fn contains_memory_exhaustion() {
    let result = eval_json(
        "new ArrayBuffer(100 * 1024 * 1024)",
        &serde_json::Value::Null,
    );
    assert!(matches!(result, Err(JsSandboxError::OutOfMemory)));
}

#[test]
fn does_not_expose_host_require() {
    let output = eval_json("typeof require", &serde_json::Value::Null).expect("expression runs");
    assert_eq!(output.result, serde_json::json!("undefined"));
}

#[test]
fn captures_logs_and_executes_strict_json_envelope() {
    let output = execute_tool(&serde_json::json!({"code": "console.log('sum', input.a + input.b); input.a + input.b", "input": {"a": 2, "b": 3}}))
        .expect("tool envelope executes");
    assert_eq!(output["result"], 5);
    assert_eq!(output["logs"], serde_json::json!(["sum 5"]));
    assert_eq!(
        execute_tool(&serde_json::json!({"code": "1", "extra": true})),
        Err(JsSandboxError::InvalidInput)
    );
}

#[test]
fn advertises_js_eval_on_every_platform() {
    let definition = js_eval_definition();
    assert_eq!(definition.name, "js_eval");
    assert!(!definition.sensitive);
    assert!(get_platform_tools()
        .iter()
        .any(|tool| tool.name == "js_eval"));
}
