use std::time::{Duration, Instant};

use rquickjs::{Context, Runtime};
use serde::Serialize;

const MEMORY_LIMIT_BYTES: usize = 64 * 1024 * 1024;
const STACK_LIMIT_BYTES: usize = 1024 * 1024;
const EXECUTION_LIMIT: Duration = Duration::from_secs(2);
const INPUT_LIMIT_BYTES: usize = 1024 * 1024;
const OUTPUT_LIMIT_BYTES: usize = 1024 * 1024;
const LOG_LIMIT_BYTES: usize = 64 * 1024;

#[derive(Debug, Clone, PartialEq, Eq, thiserror::Error)]
pub enum JsSandboxError {
    #[error("invalid_input")]
    InvalidInput,
    #[error("input_too_large")]
    InputTooLarge,
    #[error("runtime_unavailable")]
    RuntimeUnavailable,
    #[error("timeout")]
    Timeout,
    #[error("out_of_memory")]
    OutOfMemory,
    #[error("execution_error")]
    Execution,
    #[error("result_not_json_serializable")]
    ResultNotSerializable,
    #[error("output_too_large")]
    OutputTooLarge,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
pub struct JsSandboxOutput {
    pub result: serde_json::Value,
    pub logs: Vec<String>,
}

fn capped_append(target: &mut String, value: &str, cap: usize) {
    let remaining = cap.saturating_sub(target.len());
    let mut end = value.len().min(remaining);
    while !value.is_char_boundary(end) {
        end -= 1;
    }
    target.push_str(&value[..end]);
}

pub fn eval_json(
    source: &str,
    input: &serde_json::Value,
) -> Result<JsSandboxOutput, JsSandboxError> {
    let input_json = serde_json::to_string(input).map_err(|_| JsSandboxError::InvalidInput)?;
    if source.len() > 256 * 1024 {
        return Err(JsSandboxError::InputTooLarge);
    }
    if input_json.len() > INPUT_LIMIT_BYTES {
        return Err(JsSandboxError::InputTooLarge);
    }

    let runtime = Runtime::new().map_err(|_| JsSandboxError::RuntimeUnavailable)?;
    runtime.set_memory_limit(MEMORY_LIMIT_BYTES);
    runtime.set_max_stack_size(STACK_LIMIT_BYTES);
    let started = Instant::now();
    runtime.set_interrupt_handler(Some(Box::new(move || started.elapsed() >= EXECUTION_LIMIT)));
    let context = Context::full(&runtime).map_err(|_| JsSandboxError::RuntimeUnavailable)?;
    let mut logs = String::new();
    let evaluation = context.with(|ctx| {
        let global = ctx.globals();
        global.set("input", ctx.json_parse(input_json.as_str())?)?;
        let console = rquickjs::Object::new(ctx.clone())?;
        let logs_ref = std::sync::Arc::new(std::sync::Mutex::new(String::new()));
        let captured = std::sync::Arc::clone(&logs_ref);
        console.set(
            "log",
            rquickjs::Function::new(
                ctx.clone(),
                move |values: rquickjs::prelude::Rest<rquickjs::Value>| {
                    let mut output = captured
                        .lock()
                        .unwrap_or_else(std::sync::PoisonError::into_inner);
                    for (index, value) in values.0.iter().enumerate() {
                        if index > 0 {
                            capped_append(&mut output, " ", LOG_LIMIT_BYTES);
                        }
                        let text = value
                            .get::<rquickjs::prelude::Coerced<String>>()
                            .map(|coerced| coerced.0)
                            .unwrap_or_else(|_| "[unprintable]".into());
                        capped_append(&mut output, &text, LOG_LIMIT_BYTES);
                    }
                    capped_append(&mut output, "\n", LOG_LIMIT_BYTES);
                    Ok::<(), rquickjs::Error>(())
                },
            )?,
        )?;
        global.set("console", console)?;
        let outcome = (|| {
            let value: rquickjs::Value = ctx.eval(source)?;
            let result = ctx
                .json_stringify(value)?
                .ok_or(rquickjs::Error::Exception)?;
            result.to_string()
        })();
        logs = logs_ref
            .lock()
            .unwrap_or_else(std::sync::PoisonError::into_inner)
            .clone();
        // A thrown JS exception surfaces in Rust only as Error::Exception;
        // its message (e.g. QuickJS "out of memory") is read from the
        // context so the failure can be classified.
        Ok::<_, rquickjs::Error>(outcome.map_err(|error| {
            match error {
                rquickjs::Error::Exception => ctx
                    .catch()
                    .get::<rquickjs::prelude::Coerced<String>>()
                    .map(|message| message.0)
                    .unwrap_or_else(|_| error.to_string()),
                other => other.to_string(),
            }
        }))
    });

    let evaluation = evaluation
        .map_err(|error| error.to_string())
        .and_then(|outcome| outcome);
    let result = match evaluation {
        Ok(json) => json,
        Err(_message) if started.elapsed() >= EXECUTION_LIMIT => {
            return Err(JsSandboxError::Timeout)
        }
        Err(message) => {
            let message = message.to_ascii_lowercase();
            if message.contains("memory") || message.contains("alloc") || message.contains("out of")
            {
                return Err(JsSandboxError::OutOfMemory);
            }
            return Err(JsSandboxError::Execution);
        }
    };
    if result.len() > OUTPUT_LIMIT_BYTES {
        return Err(JsSandboxError::OutputTooLarge);
    }
    let result =
        serde_json::from_str(&result).map_err(|_| JsSandboxError::ResultNotSerializable)?;
    Ok(JsSandboxOutput {
        result,
        logs: if logs.is_empty() {
            Vec::new()
        } else {
            logs.lines().map(str::to_owned).collect()
        },
    })
}

pub fn execute_tool(arguments: &serde_json::Value) -> Result<serde_json::Value, JsSandboxError> {
    let object = arguments.as_object().ok_or(JsSandboxError::InvalidInput)?;
    if object.keys().any(|key| key != "code" && key != "input") {
        return Err(JsSandboxError::InvalidInput);
    }
    let code = object
        .get("code")
        .and_then(serde_json::Value::as_str)
        .ok_or(JsSandboxError::InvalidInput)?;
    let input = object.get("input").unwrap_or(&serde_json::Value::Null);
    serde_json::to_value(eval_json(code, input)?).map_err(|_| JsSandboxError::ResultNotSerializable)
}
