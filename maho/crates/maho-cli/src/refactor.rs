use std::future::Future;
use std::time::Duration;

use anyhow::{bail, Result};
use serde_json::{json, Map, Value};

use crate::browser;

pub const EDGE_SCROLL_PIXELS: i64 = i32::MAX as i64;
pub(crate) const SEMANTIC_MARKER: &str = "__maho_cli_semantic";

const MIN_SELECTOR_CONFIDENCE: usize = 60;
const SEMANTIC_LEASE_TTL_SECONDS: i64 = 60;
const SEMANTIC_HEARTBEAT_INTERVAL: Duration = Duration::from_secs(20);

pub fn scroll_args(tab_id: Option<i64>, amount: Option<i64>, to: Option<&str>) -> Result<Value> {
    if amount.is_some() && to.is_some() {
        bail!("--amount and --to cannot be used together");
    }
    let (direction, pixels) = match to {
        Some("top") => ("up", EDGE_SCROLL_PIXELS),
        Some("bottom") => ("down", EDGE_SCROLL_PIXELS),
        Some(other) => bail!("invalid scroll edge '{other}'; expected top or bottom"),
        None => {
            let amount = amount.unwrap_or(300);
            let direction = if amount < 0 { "up" } else { "down" };
            let pixels = amount.saturating_abs().min(EDGE_SCROLL_PIXELS);
            (direction, pixels)
        }
    };
    let mut args = json!({ "direction": direction, "pixels": pixels });
    apply_tab_id(&mut args, tab_id);
    Ok(args)
}

fn apply_tab_id(args: &mut Value, tab_id: Option<i64>) {
    if let Some(tab_id) = tab_id {
        args["tab_id"] = Value::Number(tab_id.into());
    }
}

fn tool_args_with_tab(tab_id: Option<i64>) -> Value {
    let mut args = json!({});
    apply_tab_id(&mut args, tab_id);
    args
}

fn projected_payload(value: &Value) -> &Value {
    value.get("result").unwrap_or(value)
}

async fn query_selector_match(
    client: &maho_browser_mcp::client::BrowserClient,
    selector: &str,
    tab_id: Option<i64>,
) -> Result<Option<(String, String)>> {
    let mut args = json!({ "selector": selector });
    apply_tab_id(&mut args, tab_id);
    let result = super::send_tool_call_with(client, "page_query_selector", args, false).await?;
    let payload = projected_payload(&result);
    if payload.get("match_count").and_then(Value::as_i64).unwrap_or(0) > 1 {
        bail!("selector matches multiple DOM elements; refine the selector");
    }
    let Some(ref_id) = payload
        .get("ref_id")
        .and_then(Value::as_str)
        .filter(|value| !value.is_empty())
    else {
        return Ok(None);
    };
    let tag = payload
        .get("tag")
        .and_then(Value::as_str)
        .unwrap_or("")
        .to_ascii_lowercase();
    Ok(Some((ref_id.to_string(), tag)))
}

async fn read_dom_attribute(
    client: &maho_browser_mcp::client::BrowserClient,
    ref_id: &str,
    attribute: &str,
    tab_id: Option<i64>,
) -> Result<String> {
    let mut args = json!({ "ref_id": ref_id, "attribute": attribute });
    apply_tab_id(&mut args, tab_id);
    let result = super::send_tool_call_with(client, "page_get_attribute", args, false).await?;
    let payload = projected_payload(&result);
    Ok(payload
        .get("value")
        .and_then(Value::as_str)
        .or_else(|| payload.get("attribute").and_then(Value::as_str))
        .unwrap_or("")
        .to_string())
}

async fn read_dom_text(
    client: &maho_browser_mcp::client::BrowserClient,
    ref_id: &str,
    tab_id: Option<i64>,
) -> Result<String> {
    let mut args = json!({ "ref_id": ref_id });
    apply_tab_id(&mut args, tab_id);
    let result = super::send_tool_call_with(client, "page_get_text", args, false).await?;
    Ok(projected_payload(&result)
        .get("text")
        .and_then(Value::as_str)
        .unwrap_or("")
        .to_string())
}

fn css_attribute_value(value: &str) -> String {
    value.replace('\\', "\\\\").replace('"', "\\\"")
}

async fn text_for_selector(
    client: &maho_browser_mcp::client::BrowserClient,
    selector: &str,
    tab_id: Option<i64>,
) -> Result<String> {
    let Some((ref_id, _)) = query_selector_match(client, selector, tab_id).await? else {
        return Ok(String::new());
    };
    read_dom_text(client, &ref_id, tab_id).await
}

async fn read_associated_label(
    client: &maho_browser_mcp::client::BrowserClient,
    selector: &str,
    element_id: &str,
    aria_labelledby: &str,
    tab_id: Option<i64>,
) -> Result<String> {
    if !aria_labelledby.trim().is_empty() {
        let mut parts = Vec::new();
        for id in aria_labelledby.split_whitespace() {
            let selector = format!("[id=\"{}\"]", css_attribute_value(id));
            let text = text_for_selector(client, &selector, tab_id).await?;
            if !text.trim().is_empty() {
                parts.push(text);
            }
        }
        if !parts.is_empty() {
            return Ok(parts.join(" "));
        }
    }

    if !element_id.is_empty() {
        let label_for = format!("label[for=\"{}\"]", css_attribute_value(element_id));
        let text = text_for_selector(client, &label_for, tab_id).await?;
        if !text.trim().is_empty() {
            return Ok(text);
        }
    }

    // Covers the common `<label>Text <input ...></label>` form without any
    // backend-specific DOM->AX identity primitive. Chromium supports :has().
    let nested_label = format!("label:has({selector})");
    text_for_selector(client, &nested_label, tab_id).await
}

#[derive(Debug, Default)]
struct SelectorFingerprint {
    tag: String,
    text: String,
    label_text: String,
    element_id: String,
    aria_label: String,
    field_name: String,
    placeholder: String,
    input_type: String,
    data_field: String,
    title: String,
    role: String,
}

fn normalized_text(value: &str) -> String {
    value.split_whitespace().collect::<Vec<_>>().join(" ")
}

fn expected_accessibility_role(fingerprint: &SelectorFingerprint) -> Option<&'static str> {
    match fingerprint.tag.as_str() {
        "button" => Some("button"),
        "textarea" => Some("textbox"),
        "select" => Some("combobox"),
        "a" => Some("link"),
        "input" => match fingerprint.input_type.to_ascii_lowercase().as_str() {
            "checkbox" => Some("checkbox"),
            "radio" => Some("radio"),
            "button" | "submit" | "reset" | "image" => Some("button"),
            "range" => Some("slider"),
            "number" => Some("spinbutton"),
            "hidden" | "file" | "color" | "date" | "datetime-local" | "month" | "time" | "week" => {
                None
            }
            _ => Some("textbox"),
        },
        _ => None,
    }
}

fn required_exact_field_score(
    node: &Map<String, Value>,
    key: &str,
    expected: &str,
    weight: usize,
) -> Option<usize> {
    if expected.is_empty() {
        return Some(0);
    }
    match node.get(key).and_then(Value::as_str) {
        Some(actual) if actual == expected => Some(weight),
        Some(_) | None => None,
    }
}

fn selector_node_score(
    node: &Map<String, Value>,
    fingerprint: &SelectorFingerprint,
) -> Option<usize> {
    node.get("ref").and_then(Value::as_i64)?;

    // Strong DOM identity fields must be projected by the AX candidate and
    // match exactly. Treating an absent expected field as neutral lets an
    // unrelated same-role node inherit the score, which is unsafe when the
    // queried DOM node is missing from the accessibility snapshot.
    let mut score = 0usize;
    for (key, expected, weight) in [
        ("elementId", fingerprint.element_id.as_str(), 100usize),
        ("ariaLabel", fingerprint.aria_label.as_str(), 80),
        ("fieldName", fingerprint.field_name.as_str(), 70),
        ("placeholder", fingerprint.placeholder.as_str(), 60),
        ("inputType", fingerprint.input_type.as_str(), 20),
        ("dataField", fingerprint.data_field.as_str(), 90),
    ] {
        score += required_exact_field_score(node, key, expected, weight)?;
    }

    let node_role = node.get("role").and_then(Value::as_str).unwrap_or("");
    let expected_role: Option<&str> = if !fingerprint.role.is_empty() {
        Some(fingerprint.role.as_str())
    } else {
        expected_accessibility_role(fingerprint)
    };
    if let Some(expected_role) = expected_role {
        // Role is a compatibility gate, not standalone identity evidence.
        if node_role.is_empty() || !node_role.eq_ignore_ascii_case(expected_role) {
            return None;
        }
        score += if fingerprint.role.is_empty() { 10 } else { 30 };
    }

    let node_name = normalized_text(node.get("name").and_then(Value::as_str).unwrap_or(""));
    for candidate in [
        fingerprint.text.as_str(),
        fingerprint.label_text.as_str(),
        fingerprint.title.as_str(),
        fingerprint.aria_label.as_str(),
        fingerprint.placeholder.as_str(),
    ] {
        let candidate = normalized_text(candidate);
        if !candidate.is_empty() && candidate == node_name {
            score += 25;
            break;
        }
    }

    (score > 0).then_some(score)
}

fn find_unique_ref(snapshot: &Value, fingerprint: &SelectorFingerprint) -> Result<i64> {
    let mut candidates: Vec<(usize, i64)> = Vec::new();
    let mut stack = vec![snapshot];
    while let Some(node) = stack.pop() {
        match node {
            Value::Object(map) => {
                if let (Some(score), Some(reference)) = (
                    selector_node_score(map, fingerprint),
                    map.get("ref").and_then(Value::as_i64),
                ) {
                    candidates.push((score, reference));
                }
                stack.extend(map.values());
            }
            Value::Array(items) => stack.extend(items),
            _ => {}
        }
    }

    candidates.sort_unstable_by(|a, b| b.0.cmp(&a.0).then_with(|| a.1.cmp(&b.1)));
    let Some((best_score, best_ref)) = candidates.first().copied() else {
        bail!(
            "selector matched a DOM element, but it has no uniquely identifiable interactive accessibility ref"
        );
    };
    if candidates
        .get(1)
        .is_some_and(|candidate| candidate.0 == best_score)
    {
        bail!(
            "selector matched a DOM element, but its accessibility ref is ambiguous; refine the selector"
        );
    }
    if best_score < MIN_SELECTOR_CONFIDENCE {
        bail!(
            "selector matched a DOM element, but the best accessibility candidate has insufficient identity confidence ({best_score} < {MIN_SELECTOR_CONFIDENCE})"
        );
    }
    Ok(best_ref)
}

fn snapshot_contains_ref(snapshot: &Value, wanted: i64) -> bool {
    let mut stack = vec![snapshot];
    while let Some(node) = stack.pop() {
        match node {
            Value::Object(map) => {
                if map.get("ref").and_then(Value::as_i64) == Some(wanted) {
                    return true;
                }
                stack.extend(map.values());
            }
            Value::Array(items) => stack.extend(items),
            _ => {}
        }
    }
    false
}

pub(crate) async fn resolve_selector_ref(
    client: &maho_browser_mcp::client::BrowserClient,
    selector: &str,
    tab_id: Option<i64>,
) -> Result<i64> {
    if let Some(reference) = selector.strip_prefix('@') {
        let reference = reference
            .strip_prefix('e')
            .unwrap_or(reference)
            .parse::<i64>()
            .map_err(|_| anyhow::anyhow!("invalid accessibility ref selector: {selector}"))?;
        let snapshot = super::send_tool_call_with(
            client,
            "browser_accessibility_snapshot",
            tool_args_with_tab(tab_id),
            false,
        )
        .await?;
        if snapshot_contains_ref(projected_payload(&snapshot), reference) {
            return Ok(reference);
        }
        bail!("accessibility ref @{reference} is not present in the current snapshot");
    }

    let Some((ref_id, tag)) = query_selector_match(client, selector, tab_id).await? else {
        bail!("no element found for selector: {selector}");
    };

    // The first-stage metadata reads depend only on the immutable DOM handle,
    // so they execute concurrently. Label lookup then reuses those attributes.
    let text = read_dom_text(client, &ref_id, tab_id);
    let element_id = read_dom_attribute(client, &ref_id, "id", tab_id);
    let aria_label = read_dom_attribute(client, &ref_id, "aria-label", tab_id);
    let aria_labelledby = read_dom_attribute(client, &ref_id, "aria-labelledby", tab_id);
    let field_name = read_dom_attribute(client, &ref_id, "name", tab_id);
    let placeholder = read_dom_attribute(client, &ref_id, "placeholder", tab_id);
    let input_type = read_dom_attribute(client, &ref_id, "type", tab_id);
    let data_field = read_dom_attribute(client, &ref_id, "data-maho-field", tab_id);
    let title = read_dom_attribute(client, &ref_id, "title", tab_id);
    let role = read_dom_attribute(client, &ref_id, "role", tab_id);
    let snapshot = super::send_tool_call_with(
        client,
        "browser_accessibility_snapshot",
        tool_args_with_tab(tab_id),
        false,
    );
    let (
        text,
        element_id,
        aria_label,
        aria_labelledby,
        field_name,
        placeholder,
        input_type,
        data_field,
        title,
        role,
        snapshot,
    ) = tokio::try_join!(
        text,
        element_id,
        aria_label,
        aria_labelledby,
        field_name,
        placeholder,
        input_type,
        data_field,
        title,
        role,
        snapshot,
    )?;

    let label_text =
        read_associated_label(client, selector, &element_id, &aria_labelledby, tab_id).await?;

    let fingerprint = SelectorFingerprint {
        tag,
        text,
        label_text,
        element_id,
        aria_label,
        field_name,
        placeholder,
        input_type,
        data_field,
        title,
        role,
    };
    find_unique_ref(projected_payload(&snapshot), &fingerprint).map_err(|error| {
        anyhow::anyhow!(
            "cannot map selector '{selector}' to a safe action ref: {error}. Use @<ref> from a fresh accessibility snapshot when the page has duplicate controls"
        )
    })
}

pub(super) fn is_semantic_call(args: &Value) -> bool {
    args.get(SEMANTIC_MARKER).and_then(Value::as_bool) == Some(true)
}

async fn resolve_semantic_tab(
    client: &maho_browser_mcp::client::BrowserClient,
    requested_tab_id: Option<i64>,
) -> Result<i64> {
    let result = super::send_tool_call_with(client, "browser_tab_list", json!({}), true).await?;
    let tabs = projected_payload(&result)
        .get("tabs")
        .and_then(Value::as_array)
        .ok_or_else(|| anyhow::anyhow!("browser tab list omitted tabs"))?;

    if let Some(requested_tab_id) = requested_tab_id {
        return tabs
            .iter()
            .find(|tab| tab.get("id").and_then(Value::as_i64) == Some(requested_tab_id))
            .map(|_| requested_tab_id)
            .ok_or_else(|| anyhow::anyhow!("tab {requested_tab_id} is not present"));
    }

    tabs.iter()
        .find(|tab| tab.get("is_active").and_then(Value::as_bool) == Some(true))
        .and_then(|tab| tab.get("id").and_then(Value::as_i64))
        .ok_or_else(|| anyhow::anyhow!("no active tab available for semantic browser action"))
}

fn semantic_lease_args(tab_id: i64) -> Value {
    json!({ "tab_id": tab_id, "ttl_seconds": SEMANTIC_LEASE_TTL_SECONDS })
}

struct SemanticLeaseGuard<'a> {
    client: &'a maho_browser_mcp::client::BrowserClient,
    tab_id: i64,
    released: bool,
}

impl<'a> SemanticLeaseGuard<'a> {
    async fn acquire(
        client: &'a maho_browser_mcp::client::BrowserClient,
        tab_id: i64,
    ) -> Result<Self> {
        // Acquisition errors are authoritative: selector/snapshot work must not
        // begin unless this exact connection owns the resolved tab lease.
        browser::send_control_call(client, "browser_acquire_lease", semantic_lease_args(tab_id))
            .await?;

        let mut guard = Self {
            client,
            tab_id,
            released: false,
        };

        // Verify the lease on the same session before taking any DOM/AX snapshot.
        if let Err(verify_error) = guard.heartbeat().await {
            let release_error = guard.release_now().await.err();
            return Err(match release_error {
                Some(release_error) => anyhow::anyhow!(
                    "semantic lease verification failed: {verify_error}; release also failed: {release_error}"
                ),
                None => anyhow::anyhow!("semantic lease verification failed: {verify_error}"),
            });
        }
        Ok(guard)
    }

    async fn heartbeat(&self) -> Result<()> {
        browser::send_control_call(
            self.client,
            "browser_heartbeat_lease",
            json!({ "tab_id": self.tab_id }),
        )
        .await
        .map(|_| ())
    }

    async fn release_now(&mut self) -> Result<()> {
        let result = browser::send_control_call(
            self.client,
            "browser_release_lease",
            json!({ "tab_id": self.tab_id }),
        )
        .await
        .map(|_| ());
        if result.is_ok() {
            self.released = true;
        }
        result
    }

    async fn finish<T>(mut self, outcome: Result<T>) -> Result<T> {
        let release = self.release_now().await;
        match (outcome, release) {
            (Ok(value), Ok(())) => Ok(value),
            (Ok(_), Err(release_error)) => Err(anyhow::anyhow!(
                "semantic action completed but lease release failed: {release_error}"
            )),
            (Err(action_error), Ok(())) => Err(action_error),
            (Err(action_error), Err(release_error)) => Err(anyhow::anyhow!(
                "semantic action failed: {action_error}; lease release also failed: {release_error}"
            )),
        }
    }
}

impl Drop for SemanticLeaseGuard<'_> {
    fn drop(&mut self) {
        if !self.released {
            // Rust Drop cannot await the same session-bound control connection,
            // and a new connection cannot release this lease. Normal errors flow
            // through `finish()` for async release. On task cancellation/Ctrl+C,
            // the browser's 60-second lease TTL is the fail-safe cleanup backstop.
        }
    }
}

async fn run_with_semantic_heartbeat<F, T>(
    lease: &SemanticLeaseGuard<'_>,
    operation: F,
) -> Result<T>
where
    F: Future<Output = Result<T>>,
{
    tokio::pin!(operation);
    let mut heartbeat = tokio::time::interval(SEMANTIC_HEARTBEAT_INTERVAL);
    heartbeat.set_missed_tick_behavior(tokio::time::MissedTickBehavior::Delay);
    // `interval` ticks immediately; acquisition already performed a verification
    // heartbeat, so consume that initial tick and renew only if work runs long.
    heartbeat.tick().await;

    loop {
        tokio::select! {
            outcome = &mut operation => return outcome,
            _ = heartbeat.tick() => lease.heartbeat().await?,
        }
    }
}

fn build_v2_locator(selector: Option<&str>, object: &Map<String, Value>) -> Result<Value> {
    if let Some(selector) = selector {
        if let Some(reference) = selector.strip_prefix('@') {
            let reference = reference
                .strip_prefix('e')
                .unwrap_or(reference)
                .parse::<i64>()
                .map_err(|_| anyhow::anyhow!("invalid accessibility ref selector: {selector}"))?;
            return Ok(json!({ "ref": reference }));
        }
        if let Ok(value) = serde_json::from_str::<Value>(selector) {
            if value.is_object()
                && (value.get("ref").is_some()
                    || value.get("css").is_some()
                    || value.get("role").is_some())
            {
                return Ok(value);
            }
        }
        return Ok(json!({ "css": selector }));
    }

    if let Some(reference) = object.get("ref").and_then(Value::as_i64) {
        return Ok(json!({ "ref": reference }));
    }

    Ok(json!({ "css": ":focus" }))
}

async fn try_execute_v2_semantic_call(
    client: &maho_browser_mcp::client::BrowserClient,
    tool: &str,
    requested_tab_id: Option<i64>,
    selector: Option<&str>,
    submit: bool,
    object: &Map<String, Value>,
) -> Result<Option<Value>> {
    let capability_id = match tool {
        "browser_click" | "input.click" => "input.locator_click",
        "browser_type" | "input.type" => "input.locator_type",
        _ => return Ok(None),
    };

    let locator = build_v2_locator(selector, object)?;
    let mut capability_args = json!({
        "locator": locator,
        "lease": "scoped",
    });

    if let Some(tab_id) = requested_tab_id {
        capability_args["tab_id"] = Value::Number(tab_id.into());
    }
    if capability_id == "input.locator_type" {
        if let Some(text) = object.get("text") {
            capability_args["text"] = text.clone();
        }
        if submit {
            capability_args["submit"] = Value::Bool(true);
        }
        if let Some(allow) = object.get("allow_credentials") {
            capability_args["allow_credentials"] = allow.clone();
        }
    }
    if let Some(wait) = object.get("wait") {
        capability_args["wait"] = wait.clone();
    }
    if let Some(observe) = object.get("observe") {
        capability_args["observe"] = observe.clone();
    }

    let raw = client
        .call_capability_with_timeout(
            capability_id,
            capability_args,
            crate::browser::tool_timeout(capability_id),
        )
        .await
        .map_err(|err| super::describe_tool_call_failure(capability_id, err))?;

    Ok(Some(super::project_cli_result(raw)))
}

pub(super) async fn execute_semantic_call(
    client: &maho_browser_mcp::client::BrowserClient,
    tool: &str,
    mut args: Value,
) -> Result<Value> {
    let object = args
        .as_object_mut()
        .ok_or_else(|| anyhow::anyhow!("semantic tool arguments must be a JSON object"))?;
    object.remove(SEMANTIC_MARKER);
    let submit = object
        .remove("submit")
        .and_then(|value| value.as_bool())
        .unwrap_or(false);
    let selector = object
        .remove("selector")
        .and_then(|value| value.as_str().map(str::to_string));
    let requested_tab_id = object.get("tab_id").and_then(Value::as_i64);

    if submit && tool != "browser_type" {
        bail!("--submit is only valid for browser_type");
    }
    if !matches!(
        tool,
        "browser_click"
            | "browser_type"
            | "browser_key_press"
            | "browser_scroll"
            | "browser_hover"
            | "browser_select"
    ) {
        bail!("unsupported semantic browser tool '{tool}'");
    }
    if selector.is_some()
        && !matches!(
            tool,
            "browser_click" | "browser_type" | "browser_hover" | "browser_select"
        )
    {
        bail!("semantic selector is not valid for tool '{tool}'");
    }

    // Direct browser-side V2 locator path when advertised
    if super::is_v2_available(client).await {
        if let Some(v2_result) = try_execute_v2_semantic_call(
            client,
            tool,
            requested_tab_id,
            selector.as_deref(),
            submit,
            object,
        )
        .await?
        {
            return Ok(v2_result);
        }
    }

    // Pin the action to one concrete tab before acquiring the lease. This is the
    // only pre-lease browser read; selector/DOM/AX work begins after verification.
    let tab_id = resolve_semantic_tab(client, requested_tab_id).await?;
    object.insert("tab_id".to_string(), Value::Number(tab_id.into()));

    let lease = SemanticLeaseGuard::acquire(client, tab_id).await?;
    let operation = async move {
        if let Some(selector) = selector {
            let reference = resolve_selector_ref(client, &selector, Some(tab_id)).await?;
            args["ref"] = Value::Number(reference.into());
        }

        let primary = super::send_tool_call_with(client, tool, args, false).await?;
        if submit {
            let mut key_args = json!({ "key": "Enter" });
            apply_tab_id(&mut key_args, Some(tab_id));
            let submitted =
                super::send_tool_call_with(client, "browser_key_press", key_args, false).await?;
            Ok(json!({ "typed": primary, "submitted": submitted }))
        } else {
            Ok(primary)
        }
    };
    let outcome = run_with_semantic_heartbeat(&lease, operation).await;
    lease.finish(outcome).await
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn scroll_request_maps_sign_and_edges() {
        assert_eq!(
            scroll_args(Some(7), Some(-450), None).unwrap(),
            json!({"direction":"up","pixels":450,"tab_id":7})
        );
        assert_eq!(
            scroll_args(None, None, Some("bottom")).unwrap(),
            json!({"direction":"down","pixels":EDGE_SCROLL_PIXELS})
        );
        assert!(scroll_args(None, Some(10), Some("top")).is_err());
    }

    #[test]
    fn projected_payload_unwraps_receipt_envelopes() {
        let wrapped = json!({"result":{"ref_id":"ref_3"},"receipt":{"receiptId":"r"}});
        assert_eq!(projected_payload(&wrapped)["ref_id"], "ref_3");
        let plain = json!({"ref_id":"ref_4"});
        assert_eq!(projected_payload(&plain)["ref_id"], "ref_4");
    }

    #[test]
    fn selector_fingerprint_rejects_role_only_candidate_when_dom_identity_is_absent() {
        // The queried DOM input is not represented by the AX tree. A different
        // same-role textbox must never be selected merely because it is unique.
        let snapshot = json!({
            "role":"WebArea",
            "children":[
                {"role":"textbox","name":"Customer name","ref":1},
                {"role":"combobox","name":"Country","ref":2}
            ]
        });
        let fingerprint = SelectorFingerprint {
            tag: "input".into(),
            field_name: "missing-dom-node".into(),
            input_type: "text".into(),
            ..Default::default()
        };
        assert!(find_unique_ref(&snapshot, &fingerprint).is_err());
    }

    #[test]
    fn selector_fingerprint_uses_associated_label_for_form_controls() {
        let snapshot = json!({
            "role":"WebArea",
            "children":[
                {"role":"textbox","name":"Customer name:","fieldName":"custname","inputType":"text","ref":1},
                {"role":"textbox","name":"telephone:","fieldName":"telephone","inputType":"text","ref":2}
            ]
        });
        let fingerprint = SelectorFingerprint {
            tag: "input".into(),
            label_text: "Customer name: ".into(),
            field_name: "custname".into(),
            input_type: "text".into(),
            ..Default::default()
        };
        assert_eq!(find_unique_ref(&snapshot, &fingerprint).unwrap(), 1);
    }

    #[test]
    fn selector_fingerprint_prefers_unique_stable_identity() {
        let snapshot = json!({
            "role":"webArea",
            "children":[
                {"role":"button","name":"Submit","elementId":"other","ref":1},
                {"role":"button","name":"Submit","elementId":"checkout","ref":2}
            ]
        });
        let fingerprint = SelectorFingerprint {
            tag: "button".into(),
            text: "Submit".into(),
            element_id: "checkout".into(),
            ..Default::default()
        };
        assert_eq!(find_unique_ref(&snapshot, &fingerprint).unwrap(), 2);
    }

    #[test]
    fn incompatible_role_is_rejected_even_with_matching_strong_identity() {
        let snapshot = json!({
            "role":"webArea",
            "children":[
                {"role":"textbox","name":"Submit","elementId":"checkout","ref":1}
            ]
        });
        let fingerprint = SelectorFingerprint {
            tag: "button".into(),
            text: "Submit".into(),
            element_id: "checkout".into(),
            ..Default::default()
        };
        assert!(find_unique_ref(&snapshot, &fingerprint).is_err());
    }

    #[test]
    fn duplicate_accessible_controls_are_not_guessed() {
        let snapshot = json!({
            "role":"webArea",
            "children":[
                {"role":"button","name":"Continue","elementId":"continue","ref":1},
                {"role":"button","name":"Continue","elementId":"continue","ref":2}
            ]
        });
        let fingerprint = SelectorFingerprint {
            tag: "button".into(),
            text: "Continue".into(),
            element_id: "continue".into(),
            ..Default::default()
        };
        assert!(find_unique_ref(&snapshot, &fingerprint).is_err());
    }

    #[test]
    fn observed_ref_selector_accepts_e_prefix_and_rejects_malformed_ref() {
        let args = Map::new();
        assert_eq!(build_v2_locator(Some("@e31"), &args).unwrap(), json!({"ref":31}));
        assert_eq!(build_v2_locator(Some("@31"), &args).unwrap(), json!({"ref":31}));
        assert!(build_v2_locator(Some("@e"), &args).is_err());
    }

    #[test]
    fn native_select_matches_unique_field_name_not_another_combobox() {
        let snapshot = json!({"role":"WebArea","children":[
            {"role":"combobox","fieldName":"OtherSelect","ref":30},
            {"role":"combobox","fieldName":"CategorySelect","ref":31}
        ]});
        let fingerprint = SelectorFingerprint {
            tag: "select".into(),
            field_name: "CategorySelect".into(),
            ..Default::default()
        };
        assert_eq!(find_unique_ref(&snapshot, &fingerprint).unwrap(), 31);
        let duplicate = json!({"role":"WebArea","children":[
            {"role":"combobox","fieldName":"CategorySelect","ref":31},
            {"role":"combobox","fieldName":"CategorySelect","ref":32}
        ]});
        assert!(find_unique_ref(&duplicate, &fingerprint).is_err());
    }
}
