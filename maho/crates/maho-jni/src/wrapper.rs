// Keep the existing JNI surface intact and add the mobile browser-tool bridge
// without rewriting the large generated/native binding file.
include!("lib.rs");

static ANDROID_AGENT_BRIDGE_CLASS: OnceLock<GlobalRef> = OnceLock::new();
static ANDROID_BROWSER_EXECUTION_SEQ: AtomicU64 = AtomicU64::new(1);

fn call_android_browser_tool_callback(
    tool_name: &str,
    arguments_json: &str,
) -> Result<String, maho_agent::BrowserToolBridgeError> {
    let Some(mut env_guard) = get_jni_env() else {
        return Err(maho_agent::BrowserToolBridgeError::new(
            "bridge_offline",
            "Android JVM is unavailable for browser tool execution",
            true,
        ));
    };
    let env = &mut *env_guard;
    let Some(class_ref) = ANDROID_AGENT_BRIDGE_CLASS.get() else {
        return Err(maho_agent::BrowserToolBridgeError::new(
            "bridge_offline",
            "Android agent bridge class is unavailable",
            true,
        ));
    };
    let class = unsafe {
        &*(class_ref.as_obj() as *const jni::objects::JObject as *const jni::objects::JClass)
    };
    let tool = env.new_string(tool_name).map_err(|error| {
        maho_agent::BrowserToolBridgeError::new(
            "invalid_request",
            format!("Failed to encode browser tool name: {error}"),
            false,
        )
    })?;
    let arguments = env.new_string(arguments_json).map_err(|error| {
        maho_agent::BrowserToolBridgeError::new(
            "invalid_arguments",
            format!("Failed to encode browser tool arguments: {error}"),
            false,
        )
    })?;

    let value = env.call_static_method(
        class,
        "invokeAgentBrowserTool",
        "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
        &[
            jni::objects::JValue::Object(tool.as_ref()).into(),
            jni::objects::JValue::Object(arguments.as_ref()).into(),
        ],
    );
    let value = match value {
        Ok(value) => value,
        Err(error) => {
            let _ = env.exception_clear();
            return Err(maho_agent::BrowserToolBridgeError::new(
                "bridge_offline",
                format!("Android browser callback failed: {error}"),
                true,
            ));
        }
    };
    let object = value.l().map_err(|error| {
        maho_agent::BrowserToolBridgeError::new(
            "malformed_execution",
            format!("Android browser callback returned a non-object value: {error}"),
            false,
        )
    })?;
    if object.is_null() {
        return Err(maho_agent::BrowserToolBridgeError::new(
            "bridge_offline",
            "Android browser callback returned no result",
            true,
        ));
    }
    let result_string = JString::from(object);
    let result = env.get_string(&result_string).map_err(|error| {
        maho_agent::BrowserToolBridgeError::new(
            "malformed_execution",
            format!("Android browser callback result is not a string: {error}"),
            false,
        )
    })?;
    Ok(result.into())
}

struct AndroidBrowserToolBridge;

#[async_trait]
impl maho_agent::BrowserToolBridge for AndroidBrowserToolBridge {
    async fn list_tool_descriptors(
        &self,
    ) -> Result<Vec<maho_agent::BrowserToolDescriptor>, maho_agent::BrowserToolBridgeError> {
        let json = call_android_browser_tool_callback("tools/list", "{}")?;
        let value: serde_json::Value = serde_json::from_str(&json).map_err(|error| {
            maho_agent::BrowserToolBridgeError::new(
                "malformed_discovery",
                format!("Android browser tools/list returned invalid JSON: {error}"),
                false,
            )
        })?;
        let tools = value.get("tools").cloned().ok_or_else(|| {
            maho_agent::BrowserToolBridgeError::new(
                "malformed_discovery",
                "Android browser tools/list response is missing tools",
                false,
            )
        })?;
        serde_json::from_value(tools).map_err(|error| {
            maho_agent::BrowserToolBridgeError::new(
                "malformed_discovery",
                format!("Android browser tool descriptors are invalid: {error}"),
                false,
            )
        })
    }

    async fn execute_tool(
        &self,
        capability_id: &str,
        args_json: &str,
    ) -> Result<maho_agent::BrowserToolExecution, maho_agent::BrowserToolBridgeError> {
        serde_json::from_str::<serde_json::Value>(args_json).map_err(|error| {
            maho_agent::BrowserToolBridgeError::new(
                "invalid_arguments",
                format!("Browser tool arguments are not valid JSON: {error}"),
                false,
            )
        })?;
        let output_json = call_android_browser_tool_callback(capability_id, args_json)?;
        let value: serde_json::Value = serde_json::from_str(&output_json).map_err(|error| {
            maho_agent::BrowserToolBridgeError::new(
                "malformed_execution",
                format!("Android browser tool returned invalid JSON: {error}"),
                false,
            )
        })?;
        if value.get("ok").and_then(serde_json::Value::as_bool) == Some(false) {
            let code = value
                .get("error")
                .and_then(serde_json::Value::as_str)
                .unwrap_or("browser_tool_failed");
            let message = value
                .get("message")
                .and_then(serde_json::Value::as_str)
                .unwrap_or(code);
            let retryable = matches!(
                code,
                "no_active_page" | "page_script_timeout" | "page_script_failed"
            );
            return Err(maho_agent::BrowserToolBridgeError::new(
                code, message, retryable,
            ));
        }
        let sequence = ANDROID_BROWSER_EXECUTION_SEQ.fetch_add(1, Ordering::Relaxed);
        Ok(maho_agent::BrowserToolExecution {
            output_json,
            receipt: maho_agent::BrowserToolExecutionReceipt {
                capability_id: capability_id.to_string(),
                execution_id: format!("android-mobile-{sequence}"),
                metadata: serde_json::json!({"source": "android-webview"}),
            },
        })
    }
}

#[no_mangle]
pub unsafe extern "system" fn Java_dev_maho_browser_bridge_BridgeAgent_nativeEnableAgenticBrowsing<
    'local,
>(
    env: JNIEnv<'local>,
    class: JClass<'local>,
    session_handle: jlong,
) -> jboolean {
    if let Ok(global_class) = env.new_global_ref(class) {
        let _ = ANDROID_AGENT_BRIDGE_CLASS.set(global_class);
    }
    let Some(session) = acquire_agent_session(session_handle) else {
        return 0;
    };
    match session
        .runtime
        .set_browser_tool_bridge(Arc::new(AndroidBrowserToolBridge))
    {
        Ok(()) => 1,
        Err(_) => 0,
    }
}
