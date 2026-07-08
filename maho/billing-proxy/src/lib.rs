#[cfg(target_arch = "wasm32")]
use worker::*;
#[cfg(target_arch = "wasm32")]
use chrono::{TimeZone, Utc};
#[cfg(target_arch = "wasm32")]
use std::rc::Rc;

#[cfg(target_arch = "wasm32")]
use serde::{Deserialize, Serialize};
use hmac::{Hmac, Mac};
use sha2::Sha256;

type HmacSha256 = Hmac<Sha256>;

// ─── Pure logic (testable on host target) ───────────────────────────────────

pub const DEGRADATION_MODE_DENY_PAID: &str = "deny-paid";
pub const DEGRADATION_MODE_FREE_ONLY: &str = "free-only";
pub const DEGRADATION_MODE_BYPASS_OVERRIDE: &str = "bypass-override";
pub const DEGRADATION_MODE_PAYG_FALLBACK: &str = "payg-fallback";

pub fn hash_token_for_cache(token: &str) -> String {
    use sha2::{Digest, Sha256};
    let mut hasher = Sha256::new();
    hasher.update(token.as_bytes());
    let result = hasher.finalize();
    let mut s = String::with_capacity(64);
    for byte in result {
        s.push_str(&format!("{:02x}", byte));
    }
    s
}

pub fn hex_decode(hex_str: &str) -> Option<Vec<u8>> {
    if hex_str.len() % 2 != 0 {
        return None;
    }
    let mut bytes = Vec::new();
    for i in (0..hex_str.len()).step_by(2) {
        let res = u8::from_str_radix(&hex_str[i..i+2], 16).ok()?;
        bytes.push(res);
    }
    Some(bytes)
}

pub fn verify_bypass_token_at(token: &str, service_token: &str, now_sec: i64) -> Option<i32> {
    let parts: Vec<&str> = token.split(':').collect();
    if parts.len() != 3 {
        return None;
    }
    let user_id_str = parts[0];
    let expires_at_str = parts[1];
    let sig_hex = parts[2];

    let sig_bytes = hex_decode(sig_hex)?;

    let expires_at: i64 = expires_at_str.parse().ok()?;
    if now_sec >= expires_at {
        return None;
    }

    let payload = format!("{}:{}", user_id_str, expires_at_str);
    let mut mac = HmacSha256::new_from_slice(service_token.as_bytes()).ok()?;
    mac.update(payload.as_bytes());
    let computed_sig = mac.finalize();

    let computed_bytes = computed_sig.into_bytes();
    if computed_bytes.len() != sig_bytes.len() {
        return None;
    }
    use subtle::ConstantTimeEq;
    if computed_bytes.as_slice().ct_eq(&sig_bytes).unwrap_u8() == 1 {
        user_id_str.parse().ok()
    } else {
        None
    }
}

// ─── Model allowlist (compiled-in fallback for when KV unreachable) ─────────

pub const FALLBACK_FREE_MODELS: &[&str] = &[
    "google/gemini-3-flash-lite:free",
    "meta-llama/llama-4-scout-17b-16e:free",
    "mistralai/mistral-small-3.2-24b:free",
    "qwen/qwen-3-32b:free",
];

pub const FALLBACK_PRO_MODELS: &[&str] = &[
    "google/gemini-3.5-flash",
    "anthropic/claude-haiku-4.5",
    "openai/gpt-5-nano",
    "openai/gpt-5-mini",
    "meta-llama/llama-4-maverick",
    "meta-llama/llama-4-70b-instruct",
    "deepseek/deepseek-v3.5",
    "z-ai/glm-4.7-flash",
];

pub const FALLBACK_MAX_MODELS: &[&str] = &[
    "google/gemini-3.5-pro",
    "anthropic/claude-sonnet-4.7",
    "qwen/qwen-3-72b-instruct",
    "mistralai/mistral-large-3",
    "x-ai/grok-4-mini",
];

/// Patterns that are ALWAYS forbidden in Maho-managed routing (BYOK only).
/// Defense-in-depth: even if KV contains bad data, these never pass.
pub const FORBIDDEN_FRONTIER_PATTERNS: &[&str] = &[
    "claude-opus",
    "gpt-5",       // bare gpt-5 only (allow gpt-5-nano/mini via explicit allowlist match first)
    "/o3",         // openai/o3, openai/o3-mini
    "/o4",         // openai/o4
    "gemini-3-ultra",
    ":thinking",
    ":reasoning-xhigh",
];

/// Check if a model slug matches any frontier-forbidden pattern.
/// NOTE: The allowlist check takes priority — a model explicitly in the allowlist
/// won't reach the frontier check in `is_model_allowed_for_tier`. This function
/// is defense-in-depth for when the allowlist itself is bad.
pub fn is_frontier_forbidden(model: &str) -> bool {
    for forbidden in FORBIDDEN_FRONTIER_PATTERNS {
        // Special case for "gpt-5": reject "gpt-5" but NOT "gpt-5-nano" or "gpt-5-mini"
        if *forbidden == "gpt-5" {
            if model.contains("gpt-5") && !model.contains("gpt-5-nano") && !model.contains("gpt-5-mini") {
                return true;
            }
            continue;
        }
        if model.contains(forbidden) {
            return true;
        }
    }
    false
}

/// Tier-aware model allowlist check.
/// Models in the allowlist for a given tier (cumulative: max includes pro includes free).
/// Frontier patterns are rejected as defense-in-depth even if they appear in the allowlist.
pub fn is_model_allowed_for_tier(
    model: &str,
    tier: &str,
    free_list: &[String],
    pro_list: &[String],
    max_list: &[String],
) -> bool {
    // Defense-in-depth: reject frontier patterns regardless of allowlist contents
    if is_frontier_forbidden(model) {
        return false;
    }

    let lower = tier.to_lowercase();
    match lower.as_str() {
        "free" => free_list.iter().any(|m| m == model),
        "pro" => free_list.iter().chain(pro_list.iter()).any(|m| m == model),
        "max" | "payg" => free_list.iter().chain(pro_list.iter()).chain(max_list.iter()).any(|m| m == model),
        _ => false,
    }
}

// ─── Per-request caps ───────────────────────────────────────────────────────

pub fn get_max_tokens_cap(tier: &str) -> u32 {
    match tier.to_lowercase().as_str() {
        "free" => 200,
        "pro" => 2000,
        "max" | "payg" => 8000,
        _ => 200,
    }
}

pub fn get_per_request_cost_cap_usd(tier: &str) -> f64 {
    match tier.to_lowercase().as_str() {
        "free" => 0.05,
        "pro" => 0.20,
        "max" | "payg" => 0.50,
        _ => 0.05,
    }
}

// ─── Cost calculation ───────────────────────────────────────────────────────

/// Calculate model cost with 50% subscription markup (updated from 30%).
pub fn calculate_model_cost(model: &str, prompt_tokens: u32, completion_tokens: u32) -> f64 {
    let is_free = FALLBACK_FREE_MODELS.contains(&model);

    let (prompt_price_per_m, completion_price_per_m) = if is_free {
        (0.0, 0.0)
    } else {
        match model {
            m if m.contains("claude-sonnet") => (3.0, 15.0),
            m if m.contains("claude-haiku") => (0.25, 1.25),
            m if m.contains("gemini-3.5-pro") => (1.25, 10.0),
            m if m.contains("gemini-3.5-flash") => (0.15, 0.60),
            m if m.contains("gpt-5-nano") => (0.10, 0.40),
            m if m.contains("gpt-5-mini") => (0.40, 1.60),
            m if m.contains("mistral-large") => (2.0, 6.0),
            m if m.contains("deepseek") => (0.27, 1.10),
            m if m.contains("grok-4-mini") => (0.30, 0.50),
            _ => (0.50, 2.0),
        }
    };

    let base_cost = (prompt_tokens as f64 * prompt_price_per_m + completion_tokens as f64 * completion_price_per_m) / 1_000_000.0;
    base_cost * 1.50
}

// ─── Worker-only code (wasm32 target) ───────────────────────────────────────

#[cfg(target_arch = "wasm32")]
#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct UserUsageData {
    pub user_id: String,
    pub tier: String,
    pub cost_usd_accumulated: f64,
    pub base_ceiling_usd: f64,
    pub topup_ceiling_usd: f64,
    #[serde(default)]
    pub payg_balance_usd: f64,
    pub period_start_at: i64,
    pub next_reset_at: i64,
}

#[cfg(target_arch = "wasm32")]
#[durable_object]
pub struct UserUsage {
    state: State,
    env: Env,
}

#[cfg(target_arch = "wasm32")]
fn get_next_reset_timestamp(period_start_at: i64) -> i64 {
    if let Some(dt) = Utc.timestamp_opt(period_start_at, 0).single() {
        if let Some(next_dt) = dt.checked_add_months(chrono::Months::new(1)) {
            return next_dt.timestamp();
        }
    }
    period_start_at + 30 * 24 * 3600
}

#[cfg(target_arch = "wasm32")]
impl DurableObject for UserUsage {
    fn new(state: State, env: Env) -> Self {
        Self { state, env }
    }

    async fn fetch(&self, mut req: Request) -> Result<Response> {
        let path = req.path();
        let now_sec = Date::now().as_millis() as i64 / 1000;

        let mut data: UserUsageData = self.state.storage().get("data").await?.unwrap_or_else(|| {
            UserUsageData {
                user_id: "".to_string(),
                tier: "free".to_string(),
                cost_usd_accumulated: 0.0,
                base_ceiling_usd: 0.50,
                topup_ceiling_usd: 0.0,
                payg_balance_usd: 0.0,
                period_start_at: now_sec,
                next_reset_at: get_next_reset_timestamp(now_sec),
            }
        });

        if now_sec >= data.next_reset_at {
            data.cost_usd_accumulated = 0.0;
            data.topup_ceiling_usd = 0.0;
            data.period_start_at = data.next_reset_at;
            data.next_reset_at = get_next_reset_timestamp(data.period_start_at);
            self.state.storage().put("data", &data).await?;

            let delay_ms = (data.next_reset_at - now_sec).max(0) * 1000;
            self.state.storage().set_alarm(std::time::Duration::from_millis(delay_ms as u64)).await?;
        }

        match path.as_str() {
            "/usage" => {
                Response::from_json(&data)
            }
            "/update_tier" => {
                #[derive(Deserialize)]
                struct UpdateTierReq {
                    user_id: String,
                    tier: String,
                    period_start_at: i64,
                    base_ceiling_usd: f64,
                }
                let body: UpdateTierReq = req.json().await?;

                if body.period_start_at > data.period_start_at {
                    data.cost_usd_accumulated = 0.0;
                    data.topup_ceiling_usd = 0.0;
                    data.period_start_at = body.period_start_at;
                }
                data.user_id = body.user_id;
                data.tier = body.tier;
                data.base_ceiling_usd = body.base_ceiling_usd;
                data.next_reset_at = get_next_reset_timestamp(data.period_start_at);

                self.state.storage().put("data", &data).await?;

                let delay_ms = (data.next_reset_at - now_sec).max(0) * 1000;
                self.state.storage().set_alarm(std::time::Duration::from_millis(delay_ms as u64)).await?;

                Response::from_json(&data)
            }
            "/check_or_reserve" => {
                #[derive(Deserialize)]
                struct CheckOrReserveReq {
                    estimated_cost_usd: f64,
                }
                let body: CheckOrReserveReq = req.json().await?;
                let total_subscription_ceiling = data.base_ceiling_usd + data.topup_ceiling_usd;

                #[derive(Serialize)]
                struct CheckOrReserveResp {
                    allowed: bool,
                    source: String,
                }

                // Check subscription budget first
                if data.cost_usd_accumulated + body.estimated_cost_usd <= total_subscription_ceiling {
                    data.cost_usd_accumulated += body.estimated_cost_usd;
                    self.state.storage().put("data", &data).await?;
                    return Response::from_json(&CheckOrReserveResp {
                        allowed: true,
                        source: "subscription".to_string(),
                    });
                }

                // Fallback to PAYG balance
                if data.payg_balance_usd >= body.estimated_cost_usd {
                    data.payg_balance_usd -= body.estimated_cost_usd;
                    self.state.storage().put("data", &data).await?;
                    return Response::from_json(&CheckOrReserveResp {
                        allowed: true,
                        source: "payg".to_string(),
                    });
                }

                // Exhausted
                Response::from_json(&CheckOrReserveResp {
                    allowed: false,
                    source: "exhausted".to_string(),
                })
            }
            "/reserve" => {
                #[derive(Deserialize)]
                struct ReserveReq {
                    estimated_cost_usd: f64,
                }
                let body: ReserveReq = req.json().await?;
                let total_ceiling = data.base_ceiling_usd + data.topup_ceiling_usd;

                let allowed = data.cost_usd_accumulated < total_ceiling;
                if allowed {
                    data.cost_usd_accumulated += body.estimated_cost_usd;
                    self.state.storage().put("data", &data).await?;
                }

                #[derive(Serialize)]
                struct ReserveResp {
                    allowed: bool,
                    cost_usd_accumulated: f64,
                }
                Response::from_json(&ReserveResp {
                    allowed,
                    cost_usd_accumulated: data.cost_usd_accumulated,
                })
            }
            "/reconcile" => {
                #[derive(Deserialize)]
                struct ReconcileReq {
                    estimated_cost_usd: f64,
                    actual_cost_usd: f64,
                }
                let body: ReconcileReq = req.json().await?;
                let diff = body.actual_cost_usd - body.estimated_cost_usd;
                data.cost_usd_accumulated += diff;
                self.state.storage().put("data", &data).await?;

                Response::from_json(&data)
            }
            "/reconcile_payg" => {
                #[derive(Deserialize)]
                struct ReconcilePaygReq {
                    estimated_cost_usd: f64,
                    actual_cost_usd: f64,
                }
                let body: ReconcilePaygReq = req.json().await?;
                // Adjust payg balance: we reserved estimated, actual may differ
                let diff = body.actual_cost_usd - body.estimated_cost_usd;
                data.payg_balance_usd -= diff; // negative diff = refund to balance
                if data.payg_balance_usd < 0.0 {
                    data.payg_balance_usd = 0.0;
                }
                self.state.storage().put("data", &data).await?;

                Response::from_json(&data)
            }
            "/increment" => {
                #[derive(Deserialize)]
                struct IncrementReq {
                    cost_usd_to_add: f64,
                }
                let body: IncrementReq = req.json().await?;
                let total_ceiling = data.base_ceiling_usd + data.topup_ceiling_usd;

                let allowed = data.cost_usd_accumulated < total_ceiling;
                if allowed {
                    data.cost_usd_accumulated += body.cost_usd_to_add;
                    self.state.storage().put("data", &data).await?;
                }

                #[derive(Serialize)]
                struct IncrementResp {
                    allowed: bool,
                    remaining_usd: f64,
                    cost_usd_accumulated: f64,
                }

                let remaining_usd = (total_ceiling - data.cost_usd_accumulated).max(0.0);
                Response::from_json(&IncrementResp {
                    allowed,
                    remaining_usd,
                    cost_usd_accumulated: data.cost_usd_accumulated,
                })
            }
            "/add_topup" => {
                #[derive(Deserialize)]
                struct AddTopupReq {
                    amount_usd: f64,
                }
                let body: AddTopupReq = req.json().await?;
                data.topup_ceiling_usd += body.amount_usd;
                self.state.storage().put("data", &data).await?;

                Response::from_json(&data)
            }
            "/update_payg_balance" => {
                #[derive(Deserialize)]
                struct UpdatePaygBalanceReq {
                    balance_usd: f64,
                    #[allow(dead_code)]
                    last_sync_at: i64,
                }
                let body: UpdatePaygBalanceReq = req.json().await?;
                data.payg_balance_usd = body.balance_usd;
                self.state.storage().put("data", &data).await?;

                Response::from_json(&data)
            }
            _ => Response::error("Not Found", 404),
        }
    }

    async fn alarm(&self) -> Result<Response> {
        let now_sec = Date::now().as_millis() as i64 / 1000;
        if let Some(mut data) = self.state.storage().get::<UserUsageData>("data").await? {
            if now_sec >= data.next_reset_at {
                data.cost_usd_accumulated = 0.0;
                data.topup_ceiling_usd = 0.0;
                data.period_start_at = data.next_reset_at;
                data.next_reset_at = get_next_reset_timestamp(data.period_start_at);
                self.state.storage().put("data", &data).await?;
            }
            let delay_ms = (data.next_reset_at - now_sec).max(0) * 1000;
            self.state.storage().set_alarm(std::time::Duration::from_millis(delay_ms as u64)).await?;
        }
        Response::ok("alarm handled")
    }
}

#[cfg(target_arch = "wasm32")]
#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct IntrospectResponse {
    pub user_id: i32,
    pub tier: String,
    pub subscription_status: String,
    pub email_verified: bool,
    pub expires_at: i64,
    pub period_start_at: Option<i64>,
}

#[cfg(target_arch = "wasm32")]
fn verify_bypass_token(token: &str, service_token: &str) -> Option<i32> {
    let now = Date::now().as_millis() as i64 / 1000;
    verify_bypass_token_at(token, service_token, now)
}

#[cfg(target_arch = "wasm32")]
enum ProxyAuthOutcome {
    Authenticated(IntrospectResponse, Option<&'static str>),
    Degraded(Response),
}

/// Fetch tier allowlist from KV with compiled-in fallback.
#[cfg(target_arch = "wasm32")]
async fn fetch_allowlist(ctx: &RouteContext<Rc<Context>>, tier_key: &str) -> Vec<String> {
    if let Ok(kv) = ctx.kv("MAHO_MODEL_ALLOWLIST") {
        if let Ok(Some(text)) = kv.get(tier_key).text().await {
            return text.lines().filter(|l| !l.trim().is_empty()).map(String::from).collect();
        }
    }
    let fallback: &[&str] = match tier_key {
        "free" => FALLBACK_FREE_MODELS,
        "pro" => FALLBACK_PRO_MODELS,
        "max" => FALLBACK_MAX_MODELS,
        _ => FALLBACK_FREE_MODELS,
    };
    fallback.iter().map(|s| s.to_string()).collect()
}

#[cfg(target_arch = "wasm32")]
async fn authenticate_request(
    req: &Request,
    ctx: &RouteContext<Rc<Context>>,
    _now_sec: i64,
) -> Result<ProxyAuthOutcome> {
    let auth_header = match req.headers().get("Authorization")? {
        Some(h) => h,
        None => return Err(worker::Error::from("Missing Authorization Header")),
    };

    let user_token = match auth_header.strip_prefix("Bearer ").or_else(|| auth_header.strip_prefix("bearer ")) {
        Some(t) => t.trim(),
        None => return Err(worker::Error::from("Invalid Authorization Format")),
    };

    let bypass_header = req.headers().get("X-Maho-Verify-Bypass")?;
    let mut bypass_user_id: Option<i32> = None;
    let mut use_bypass = false;
    if let Some(tok) = &bypass_header {
        let relay_url = ctx.var("RELAY_URL")?.to_string();
        let service_token = ctx.var("SERVICE_TOKEN")?.to_string();

        #[derive(Serialize)]
        struct ConsumeBypassBody {
            token: String,
        }
        #[derive(Deserialize)]
        struct ConsumeBypassResponse {
            user_id: i32,
        }

        let consume_headers = Headers::new();
        consume_headers.set("Authorization", &format!("Bearer {}", service_token))?;
        consume_headers.set("Content-Type", "application/json")?;
        let consume_body = serde_json::to_string(&ConsumeBypassBody { token: tok.clone() })?;
        let consume_req = Request::new_with_init(
            &format!("{}/auth/consume_bypass", relay_url),
            RequestInit::new()
                .with_method(Method::Post)
                .with_headers(consume_headers)
                .with_body(Some(wasm_bindgen::JsValue::from_str(&consume_body))),
        )?;
        if let Ok(mut resp) = Fetch::Request(consume_req).send().await {
            if resp.status_code() == 200 {
                if let Ok(data) = resp.json::<ConsumeBypassResponse>().await {
                    bypass_user_id = Some(data.user_id);
                    use_bypass = true;
                }
            }
        }
    }

    let token_hash = hash_token_for_cache(user_token);
    let cache_key = format!("session:{}", token_hash);

    let auth_cache = ctx.kv("MAHO_AUTH_CACHE")?;
    let mut introspect_data: Option<IntrospectResponse> = None;

    if !use_bypass {
        if let Ok(Some(cached_str)) = auth_cache.get(&cache_key).text().await {
            if let Ok(data) = serde_json::from_str::<IntrospectResponse>(&cached_str) {
                introspect_data = Some(data);
            }
        }
    }

    if introspect_data.is_none() {
        let relay_url = ctx.var("RELAY_URL")?.to_string();
        let service_token = ctx.var("SERVICE_TOKEN")?.to_string();

        let headers = Headers::new();
        headers.set("Authorization", &format!("Bearer {}", service_token))?;
        headers.set("Content-Type", "application/json")?;

        #[derive(Serialize)]
        struct IntrospectBody {
            token: String,
        }

        let body_str = serde_json::to_string(&IntrospectBody { token: user_token.to_string() })?;
        let intro_req = Request::new_with_init(
            &format!("{}/auth/introspect", relay_url),
            RequestInit::new()
                .with_method(Method::Post)
                .with_headers(headers)
                .with_body(Some(wasm_bindgen::JsValue::from_str(&body_str))),
        )?;

        match Fetch::Request(intro_req).send().await {
            Ok(mut resp) => {
                if resp.status_code() == 200 {
                    if let Ok(data) = resp.json::<IntrospectResponse>().await {
                        let cached_str = serde_json::to_string(&data)?;
                        let _ = auth_cache.put(&cache_key, cached_str)?.expiration_ttl(60).execute().await;
                        introspect_data = Some(data);
                    }
                }
            }
            Err(err) => {
                if use_bypass {
                    console_log!(
                        "[degradation={}] relay unreachable; continuing with bypass-override: {}",
                        DEGRADATION_MODE_BYPASS_OVERRIDE,
                        err
                    );
                } else if let Some(cached) = introspect_data.take().filter(|d| d.tier.to_lowercase() == "free") {
                    console_log!(
                        "[degradation={}] relay unreachable; downgrading cached free-tier user",
                        DEGRADATION_MODE_FREE_ONLY
                    );
                    return Ok(ProxyAuthOutcome::Authenticated(
                        IntrospectResponse {
                            tier: "free".to_string(),
                            ..cached
                        },
                        Some(DEGRADATION_MODE_FREE_ONLY),
                    ));
                } else {
                    console_log!(
                        "[degradation={}] relay unreachable; returning 503: {}",
                        DEGRADATION_MODE_DENY_PAID,
                        err
                    );
                    let mut deny_resp = Response::from_json(&serde_json::json!({
                        "error": "Relay temporarily unreachable. Try again in 30s.",
                        "mode": DEGRADATION_MODE_DENY_PAID
                    }))?
                    .with_status(503);
                    let _ = deny_resp.headers_mut().set("Retry-After", "30");
                    return Ok(ProxyAuthOutcome::Degraded(deny_resp));
                }
            }
        }
    }

    let (user_info, degradation_mode) = match introspect_data {
        Some(info) => (info, None),
        None => {
            if use_bypass {
                console_log!(
                    "[degradation={}] relay introspect unavailable; proceeding with bypass user_id={}",
                    DEGRADATION_MODE_BYPASS_OVERRIDE,
                    bypass_user_id.unwrap_or(0)
                );
                (
                    IntrospectResponse {
                        user_id: bypass_user_id.unwrap_or(0),
                        tier: "free".to_string(),
                        subscription_status: "active".to_string(),
                        email_verified: true,
                        expires_at: 0,
                        period_start_at: None,
                    },
                    Some(DEGRADATION_MODE_BYPASS_OVERRIDE),
                )
            } else {
                let mut deny_resp = Response::from_json(&serde_json::json!({
                    "error": "Relay temporarily unreachable. Try again in 30s.",
                    "mode": DEGRADATION_MODE_DENY_PAID
                }))?
                .with_status(503);
                let _ = deny_resp.headers_mut().set("Retry-After", "30");
                return Ok(ProxyAuthOutcome::Degraded(deny_resp));
            }
        }
    };

    Ok(ProxyAuthOutcome::Authenticated(user_info, degradation_mode))
}

#[cfg(target_arch = "wasm32")]
#[event(fetch)]
pub async fn main(req: Request, env: Env, ctx: Context) -> Result<Response> {
    let shared_ctx = Rc::new(ctx);
    let router = Router::with_data(shared_ctx);

    router
        .get("/health", |_, _| Response::ok("ok"))
        .get_async("/v1/models", |req, route_ctx| async move {
            let now_sec = Date::now().as_millis() as i64 / 1000;

            let (user_info, _degradation_mode) = match authenticate_request(&req, &route_ctx, now_sec).await {
                Ok(ProxyAuthOutcome::Authenticated(info, mode)) => (info, mode),
                Ok(ProxyAuthOutcome::Degraded(resp)) => return Ok(resp),
                Err(err) => return Response::error(format!("Unauthorized: {err}"), 401),
            };

            let free_list = fetch_allowlist(&route_ctx, "free").await;
            let pro_list = fetch_allowlist(&route_ctx, "pro").await;
            let max_list = fetch_allowlist(&route_ctx, "max").await;

            let tier = user_info.tier.to_lowercase();
            let mut models = Vec::new();
            match tier.as_str() {
                "free" => {
                    models.extend(free_list);
                }
                "pro" => {
                    models.extend(free_list);
                    models.extend(pro_list);
                }
                "max" | "payg" => {
                    models.extend(free_list);
                    models.extend(pro_list);
                    models.extend(max_list);
                }
                _ => {}
            }

            models.retain(|m| !is_frontier_forbidden(m));

            #[derive(Serialize)]
            struct ModelEntry {
                id: String,
                object: String,
            }
            #[derive(Serialize)]
            struct ModelsListResponse {
                object: String,
                data: Vec<ModelEntry>,
            }

            let data = models.into_iter().map(|id| ModelEntry {
                id,
                object: "model".to_string(),
            }).collect();

            Response::from_json(&ModelsListResponse {
                object: "list".to_string(),
                data,
            })
        })
        .post_async("/v1/chat/completions", |mut req, route_ctx| {
            let ctx = route_ctx.data.clone();
            async move {
                let now_sec = Date::now().as_millis() as i64 / 1000;

                let (user_info, degradation_mode) = match authenticate_request(&req, &route_ctx, now_sec).await {
                    Ok(ProxyAuthOutcome::Authenticated(info, mode)) => (info, mode),
                    Ok(ProxyAuthOutcome::Degraded(resp)) => return Ok(resp),
                    Err(err) => return Response::error(format!("Unauthorized: {err}"), 401),
                };

                let mut req_body: serde_json::Value = req.json().await?;
                let model = req_body.get("model").and_then(|v| v.as_str()).unwrap_or("google/gemini-3-flash-lite:free").to_string();

                // Fetch allowlists (lazy, once per request)
                let free_list = fetch_allowlist(&route_ctx, "free").await;
                let pro_list = fetch_allowlist(&route_ctx, "pro").await;
                let max_list = fetch_allowlist(&route_ctx, "max").await;

                let tier = user_info.tier.to_lowercase();
                if !is_model_allowed_for_tier(&model, &tier, &free_list, &pro_list, &max_list) {
                    return Response::error(
                        format!("Model '{}' is not available on the {} tier", model, tier),
                        403,
                    );
                }

                let max_tokens_cap = get_max_tokens_cap(&tier);
                if let Some(obj) = req_body.as_object_mut() {
                    obj.insert("max_tokens".to_string(), serde_json::Value::from(max_tokens_cap));
                }

                let do_namespace = route_ctx.durable_object("MAHO_USER_USAGE")?;
                let do_stub = do_namespace.id_from_name(&user_info.user_id.to_string())?.get_stub()?;

                // Update tier in DO
                #[derive(Serialize)]
                struct UpdateTierReq {
                    user_id: String,
                    tier: String,
                    period_start_at: i64,
                    base_ceiling_usd: f64,
                }
                let base_ceiling = match tier.as_str() {
                    "pro" => 2.10,
                    "max" => 13.30,
                    _ => 0.50,
                };

                let update_headers = Headers::new();
                update_headers.set("Content-Type", "application/json")?;
                let update_body_str = serde_json::to_string(&UpdateTierReq {
                    user_id: user_info.user_id.to_string(),
                    tier: tier.clone(),
                    period_start_at: user_info.period_start_at.unwrap_or(now_sec),
                    base_ceiling_usd: base_ceiling,
                })?;

                let update_req = Request::new_with_init(
                    "http://durable/update_tier",
                    RequestInit::new()
                        .with_method(Method::Post)
                        .with_headers(update_headers)
                        .with_body(Some(wasm_bindgen::JsValue::from_str(&update_body_str))),
                )?;
                do_stub.fetch_with_request(update_req).await?;

                // Check-or-reserve: atomic subscription/payg budget check
                let estimated_cost_usd = get_per_request_cost_cap_usd(&tier).min(0.05);
                #[derive(Serialize)]
                struct CheckOrReserveReq {
                    estimated_cost_usd: f64,
                }
                #[derive(Deserialize)]
                struct CheckOrReserveResp {
                    allowed: bool,
                    source: String,
                }
                let cor_headers = Headers::new();
                cor_headers.set("Content-Type", "application/json")?;
                let cor_body = serde_json::to_string(&CheckOrReserveReq { estimated_cost_usd })?;
                let cor_req = Request::new_with_init(
                    "http://durable/check_or_reserve",
                    RequestInit::new()
                        .with_method(Method::Post)
                        .with_headers(cor_headers)
                        .with_body(Some(wasm_bindgen::JsValue::from_str(&cor_body))),
                )?;

                let mut cor_resp = do_stub.fetch_with_request(cor_req).await?;
                let cor_data: CheckOrReserveResp = cor_resp.json().await?;

                if !cor_data.allowed {
                    return Response::error(
                        "Billing quota exhausted. Add PAYG credits or upgrade your plan.",
                        402,
                    );
                }

                let billing_source = cor_data.source.clone();
                let effective_degradation = if billing_source == "payg" {
                    Some(DEGRADATION_MODE_PAYG_FALLBACK)
                } else {
                    degradation_mode
                };

                // If PAYG source, verify relay credit balance (defense-in-depth)
                if billing_source == "payg" {
                    let relay_url = route_ctx.var("RELAY_URL")?.to_string();
                    let service_token = route_ctx.var("SERVICE_TOKEN")?.to_string();

                    let bal_headers = Headers::new();
                    bal_headers.set("Authorization", &format!("Bearer {}", service_token))?;
                    bal_headers.set("Content-Type", "application/json")?;

                    #[derive(Serialize)]
                    struct CreditBalanceReq {
                        user_id: i32,
                    }
                    let bal_body = serde_json::to_string(&CreditBalanceReq { user_id: user_info.user_id })?;
                    let bal_req = Request::new_with_init(
                        &format!("{}/billing/credit_balance", relay_url),
                        RequestInit::new()
                            .with_method(Method::Post)
                            .with_headers(bal_headers)
                            .with_body(Some(wasm_bindgen::JsValue::from_str(&bal_body))),
                    )?;

                    match Fetch::Request(bal_req).send().await {
                        Ok(mut resp) => {
                            if resp.status_code() == 200 {
                                #[derive(Deserialize)]
                                struct CreditBalanceResp {
                                    balance_usd: f64,
                                }
                                if let Ok(bal_data) = resp.json::<CreditBalanceResp>().await {
                                    if bal_data.balance_usd <= 0.0 {
                                        return Response::error(
                                            "PAYG credit balance exhausted. Please add credits.",
                                            402,
                                        );
                                    }
                                }
                            }
                            // If relay returns non-200, allow through (DO already reserved)
                        }
                        Err(_) => {
                            // Relay unreachable for balance check — allow through, DO already reserved
                        }
                    }
                }

                let openrouter_key = route_ctx.var("OPENROUTER_API_KEY")?.to_string();
                let upstream_url = route_ctx
                    .var("UPSTREAM_LLM_URL")
                    .ok()
                    .map(|v| v.to_string())
                    .unwrap_or_else(|| "https://openrouter.ai/api/v1/chat/completions".to_string());
                let or_headers = Headers::new();
                or_headers.set("Authorization", &format!("Bearer {}", openrouter_key))?;
                or_headers.set("Content-Type", "application/json")?;

                let or_req_body_str = serde_json::to_string(&req_body)?;
                let or_req = Request::new_with_init(
                    &upstream_url,
                    RequestInit::new()
                        .with_method(Method::Post)
                        .with_headers(or_headers)
                        .with_body(Some(wasm_bindgen::JsValue::from_str(&or_req_body_str))),
                )?;

                let do_ns = do_namespace.clone();
                let user_id_str = user_info.user_id.to_string();
                let billing_source_clone = billing_source.clone();

                let mut or_resp = match Fetch::Request(or_req).send().await {
                    Ok(resp) => resp,
                    Err(err) => {
                        let do_ns_clone = do_ns.clone();
                        let user_id_str_clone = user_id_str.clone();
                        let source = billing_source.clone();
                        ctx.wait_until(async move {
                            if let Ok(stub) = do_ns_clone.id_from_name(&user_id_str_clone).and_then(|id| id.get_stub()) {
                                if source == "payg" {
                                    let _ = do_reconcile_payg(&stub, estimated_cost_usd, 0.0).await;
                                } else {
                                    let _ = do_reconcile(&stub, estimated_cost_usd, 0.0).await;
                                }
                            }
                        });
                        return Response::error(format!("OpenRouter unreachable: {err}"), 502);
                    }
                };

                let stream = req_body.get("stream").and_then(|v| v.as_bool()).unwrap_or(false);
                if !stream {
                    let or_body_json: serde_json::Value = or_resp.json().await?;
                    let prompt_tokens = or_body_json.get("usage").and_then(|u| u.get("prompt_tokens")).and_then(|v| v.as_u64()).unwrap_or(0) as u32;
                    let completion_tokens = or_body_json.get("usage").and_then(|u| u.get("completion_tokens")).and_then(|v| v.as_u64()).unwrap_or(0) as u32;

                    let cost = calculate_model_cost(&model, prompt_tokens, completion_tokens);

                    let do_ns_clone = do_ns.clone();
                    let user_id_str_clone = user_id_str.clone();
                    let source = billing_source_clone.clone();
                    let relay_url = route_ctx.var("RELAY_URL")?.to_string();
                    let service_token = route_ctx.var("SERVICE_TOKEN")?.to_string();
                    let uid = user_info.user_id;

                    ctx.wait_until(async move {
                        if let Ok(stub) = do_ns_clone.id_from_name(&user_id_str_clone).and_then(|id| id.get_stub()) {
                            if source == "payg" {
                                let _ = do_reconcile_payg(&stub, estimated_cost_usd, cost).await;
                                // Fire-and-forget: consume credit from relay
                                let _ = relay_consume_credit(&relay_url, &service_token, uid, cost).await;
                            } else {
                                let _ = do_reconcile(&stub, estimated_cost_usd, cost).await;
                            }
                        }
                    });

                    let mut final_resp = Response::from_json(&or_body_json)?;
                    if let Some(mode) = effective_degradation {
                        let _ = final_resp.headers_mut().set("X-Maho-Degradation-Mode", mode);
                    }
                    Ok(final_resp)
                } else {
                    let mut cloned_resp = or_resp.cloned()?;
                    let model_clone = model.clone();
                    let do_ns_clone = do_ns.clone();
                    let user_id_str_clone = user_id_str.clone();
                    let source = billing_source_clone.clone();
                    let relay_url = route_ctx.var("RELAY_URL")?.to_string();
                    let service_token = route_ctx.var("SERVICE_TOKEN")?.to_string();
                    let uid = user_info.user_id;

                    ctx.wait_until(async move {
                        let mut prompt_tokens = 0;
                        let mut completion_tokens = 0;
                        if let Ok(total_string) = cloned_resp.text().await {
                            for line in total_string.lines() {
                                if line.starts_with("data: ") {
                                    let json_part = line.strip_prefix("data: ").unwrap().trim();
                                    if json_part == "[DONE]" { continue; }
                                    if let Ok(parsed) = serde_json::from_str::<serde_json::Value>(json_part) {
                                        if let Some(usage) = parsed.get("usage") {
                                            prompt_tokens = usage.get("prompt_tokens").and_then(|v| v.as_u64()).unwrap_or(0) as u32;
                                            completion_tokens = usage.get("completion_tokens").and_then(|v| v.as_u64()).unwrap_or(0) as u32;
                                            break;
                                        }
                                    }
                                }
                            }
                        }
                        let cost = calculate_model_cost(&model_clone, prompt_tokens, completion_tokens);
                        if let Ok(stub) = do_ns_clone.id_from_name(&user_id_str_clone).and_then(|id| id.get_stub()) {
                            if source == "payg" {
                                let _ = do_reconcile_payg(&stub, estimated_cost_usd, cost).await;
                                let _ = relay_consume_credit(&relay_url, &service_token, uid, cost).await;
                            } else {
                                let _ = do_reconcile(&stub, estimated_cost_usd, cost).await;
                            }
                        }
                    });

                    if let Some(mode) = effective_degradation {
                        let _ = or_resp.headers_mut().set("X-Maho-Degradation-Mode", mode);
                    }
                    Ok(or_resp)
                }
            }
        })
        .get_async("/usage", |req, route_ctx| async move {
            let now_sec = Date::now().as_millis() as i64 / 1000;
            let (user_info, _degradation_mode) = match authenticate_request(&req, &route_ctx, now_sec).await {
                Ok(ProxyAuthOutcome::Authenticated(info, mode)) => (info, mode),
                Ok(ProxyAuthOutcome::Degraded(resp)) => return Ok(resp),
                Err(err) => return Response::error(format!("Unauthorized: {err}"), 401),
            };

            let do_namespace = route_ctx.durable_object("MAHO_USER_USAGE")?;
            let do_stub = do_namespace.id_from_name(&user_info.user_id.to_string())?.get_stub()?;
            do_stub.fetch_with_str("http://durable/usage").await
        })
        .post_async("/billing/invalidate_cache", |mut req, route_ctx| async move {
            let auth_header = match req.headers().get("Authorization")? {
                Some(h) => h,
                None => return Response::error("Missing Authorization Header", 401),
            };

            let token = match auth_header.strip_prefix("Bearer ").or_else(|| auth_header.strip_prefix("bearer ")) {
                Some(t) => t.trim(),
                None => return Response::error("Invalid Authorization Format", 401),
            };

            let service_token = route_ctx.var("SERVICE_TOKEN")?.to_string();
            if token != service_token {
                return Response::error("Unauthorized", 401);
            }

            #[derive(Deserialize)]
            struct InvalidateReq {
                keys: Vec<String>,
            }
            let body: InvalidateReq = req.json().await?;
            let auth_cache = route_ctx.kv("MAHO_AUTH_CACHE")?;
            for key in body.keys {
                let _ = auth_cache.delete(&key).await;
            }
            Response::ok("ok")
        })
        .post_async("/billing/update_tier", |mut req, route_ctx| async move {
            let auth_header = match req.headers().get("Authorization")? {
                Some(h) => h,
                None => return Response::error("Missing Authorization Header", 401),
            };

            let token = match auth_header.strip_prefix("Bearer ").or_else(|| auth_header.strip_prefix("bearer ")) {
                Some(t) => t.trim(),
                None => return Response::error("Invalid Authorization Format", 401),
            };

            let service_token = route_ctx.var("SERVICE_TOKEN")?.to_string();
            if token != service_token {
                return Response::error("Unauthorized", 401);
            }

            #[derive(Deserialize, Serialize)]
            struct UpdateTierBody {
                user_id: String,
                tier: String,
                period_start_at: i64,
                base_ceiling_usd: f64,
            }

            let body: UpdateTierBody = req.json().await?;
            let do_namespace = route_ctx.durable_object("MAHO_USER_USAGE")?;
            let do_stub = do_namespace.id_from_name(&body.user_id)?.get_stub()?;

            let update_headers = Headers::new();
            update_headers.set("Content-Type", "application/json")?;
            let update_body_str = serde_json::to_string(&body)?;

            let do_req = Request::new_with_init(
                "http://durable/update_tier",
                RequestInit::new()
                    .with_method(Method::Post)
                    .with_headers(update_headers)
                    .with_body(Some(wasm_bindgen::JsValue::from_str(&update_body_str))),
            )?;
            do_stub.fetch_with_request(do_req).await
        })
        .post_async("/billing/topup", |mut req, route_ctx| async move {
            let auth_header = match req.headers().get("Authorization")? {
                Some(h) => h,
                None => return Response::error("Missing Authorization Header", 401),
            };

            let token = match auth_header.strip_prefix("Bearer ").or_else(|| auth_header.strip_prefix("bearer ")) {
                Some(t) => t.trim(),
                None => return Response::error("Invalid Authorization Format", 401),
            };

            let service_token = route_ctx.var("SERVICE_TOKEN")?.to_string();
            if token != service_token {
                return Response::error("Unauthorized", 401);
            }

            #[derive(Deserialize, Serialize)]
            struct AddTopupBody {
                user_id: String,
                amount_usd: f64,
            }

            let body: AddTopupBody = req.json().await?;
            let do_namespace = route_ctx.durable_object("MAHO_USER_USAGE")?;
            let do_stub = do_namespace.id_from_name(&body.user_id)?.get_stub()?;

            let inc_headers = Headers::new();
            inc_headers.set("Content-Type", "application/json")?;

            #[derive(Serialize)]
            struct AddTopupReq {
                amount_usd: f64,
            }
            let inc_body_str = serde_json::to_string(&AddTopupReq { amount_usd: body.amount_usd })?;

            let do_req = Request::new_with_init(
                "http://durable/add_topup",
                RequestInit::new()
                    .with_method(Method::Post)
                    .with_headers(inc_headers)
                    .with_body(Some(wasm_bindgen::JsValue::from_str(&inc_body_str))),
            )?;
            do_stub.fetch_with_request(do_req).await
        })
        .post_async("/billing/credits_update", |mut req, route_ctx| async move {
            // Service-token gated webhook: relay calls this after add_credit_balance
            let auth_header = match req.headers().get("Authorization")? {
                Some(h) => h,
                None => return Response::error("Missing Authorization Header", 401),
            };

            let token = match auth_header.strip_prefix("Bearer ").or_else(|| auth_header.strip_prefix("bearer ")) {
                Some(t) => t.trim(),
                None => return Response::error("Invalid Authorization Format", 401),
            };

            let service_token = route_ctx.var("SERVICE_TOKEN")?.to_string();
            if token != service_token {
                return Response::error("Unauthorized", 401);
            }

            #[derive(Deserialize)]
            struct CreditsUpdateBody {
                user_id: i32,
                new_balance_usd: f64,
                #[allow(dead_code)]
                delta_usd: f64,
            }

            let body: CreditsUpdateBody = req.json().await?;
            let do_namespace = route_ctx.durable_object("MAHO_USER_USAGE")?;
            let do_stub = do_namespace.id_from_name(&body.user_id.to_string())?.get_stub()?;

            #[derive(Serialize)]
            struct UpdatePaygReq {
                balance_usd: f64,
                last_sync_at: i64,
            }
            let now_sec = Date::now().as_millis() as i64 / 1000;
            let payg_headers = Headers::new();
            payg_headers.set("Content-Type", "application/json")?;
            let payg_body = serde_json::to_string(&UpdatePaygReq {
                balance_usd: body.new_balance_usd,
                last_sync_at: now_sec,
            })?;

            let do_req = Request::new_with_init(
                "http://durable/update_payg_balance",
                RequestInit::new()
                    .with_method(Method::Post)
                    .with_headers(payg_headers)
                    .with_body(Some(wasm_bindgen::JsValue::from_str(&payg_body))),
            )?;
            do_stub.fetch_with_request(do_req).await
        })
        .run(req, env)
        .await
}

#[cfg(target_arch = "wasm32")]
async fn do_reconcile(do_stub: &Stub, estimated_cost_usd: f64, actual_cost_usd: f64) -> Result<Response> {
    #[derive(Serialize)]
    struct ReconcileReq {
        estimated_cost_usd: f64,
        actual_cost_usd: f64,
    }
    let recon_headers = Headers::new();
    recon_headers.set("Content-Type", "application/json")?;
    let recon_body = serde_json::to_string(&ReconcileReq { estimated_cost_usd, actual_cost_usd })?;
    let recon_req = Request::new_with_init(
        "http://durable/reconcile",
        RequestInit::new()
            .with_method(Method::Post)
            .with_headers(recon_headers)
            .with_body(Some(wasm_bindgen::JsValue::from_str(&recon_body))),
    )?;
    do_stub.fetch_with_request(recon_req).await
}

#[cfg(target_arch = "wasm32")]
async fn do_reconcile_payg(do_stub: &Stub, estimated_cost_usd: f64, actual_cost_usd: f64) -> Result<Response> {
    #[derive(Serialize)]
    struct ReconcilePaygReq {
        estimated_cost_usd: f64,
        actual_cost_usd: f64,
    }
    let recon_headers = Headers::new();
    recon_headers.set("Content-Type", "application/json")?;
    let recon_body = serde_json::to_string(&ReconcilePaygReq { estimated_cost_usd, actual_cost_usd })?;
    let recon_req = Request::new_with_init(
        "http://durable/reconcile_payg",
        RequestInit::new()
            .with_method(Method::Post)
            .with_headers(recon_headers)
            .with_body(Some(wasm_bindgen::JsValue::from_str(&recon_body))),
    )?;
    do_stub.fetch_with_request(recon_req).await
}

/// Fire-and-forget credit consumption call to relay.
#[cfg(target_arch = "wasm32")]
async fn relay_consume_credit(relay_url: &str, service_token: &str, user_id: i32, amount_usd: f64) -> Result<()> {
    #[derive(Serialize)]
    struct ConsumeCreditReq {
        user_id: i32,
        amount_usd: f64,
    }
    let headers = Headers::new();
    headers.set("Authorization", &format!("Bearer {}", service_token))?;
    headers.set("Content-Type", "application/json")?;
    let body = serde_json::to_string(&ConsumeCreditReq { user_id, amount_usd })?;
    let req = Request::new_with_init(
        &format!("{}/billing/consume_credit", relay_url),
        RequestInit::new()
            .with_method(Method::Post)
            .with_headers(headers)
            .with_body(Some(wasm_bindgen::JsValue::from_str(&body))),
    )?;
    let _ = Fetch::Request(req).send().await;
    Ok(())
}

// ─── Tests (host target only) ───────────────────────────────────────────────

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_degradation_mode_strings() {
        assert_eq!(DEGRADATION_MODE_DENY_PAID, "deny-paid");
        assert_eq!(DEGRADATION_MODE_FREE_ONLY, "free-only");
        assert_eq!(DEGRADATION_MODE_BYPASS_OVERRIDE, "bypass-override");
        assert_eq!(DEGRADATION_MODE_PAYG_FALLBACK, "payg-fallback");
    }

    #[test]
    fn test_get_max_tokens_cap_free() {
        assert_eq!(get_max_tokens_cap("free"), 200);
        assert_eq!(get_max_tokens_cap("Free"), 200);
    }

    #[test]
    fn test_get_max_tokens_cap_pro() {
        assert_eq!(get_max_tokens_cap("pro"), 2000);
        assert_eq!(get_max_tokens_cap("Pro"), 2000);
    }

    #[test]
    fn test_get_max_tokens_cap_max() {
        assert_eq!(get_max_tokens_cap("max"), 8000);
        assert_eq!(get_max_tokens_cap("Max"), 8000);
    }

    #[test]
    fn test_get_max_tokens_cap_unknown_defaults_to_free() {
        assert_eq!(get_max_tokens_cap("unknown"), 200);
        assert_eq!(get_max_tokens_cap(""), 200);
    }

    #[test]
    fn test_calculate_model_cost_free_model_is_zero() {
        let cost = calculate_model_cost("google/gemini-3-flash-lite:free", 1000, 1000);
        assert_eq!(cost, 0.0);
    }

    #[test]
    fn test_calculate_model_cost_claude_sonnet() {
        // claude-sonnet: $3/M prompt + $15/M completion, 50% markup
        let cost = calculate_model_cost("anthropic/claude-sonnet-4.7", 1_000_000, 1_000_000);
        let expected = (3.0 + 15.0) * 1.50;
        assert!((cost - expected).abs() < 1e-10);
    }

    #[test]
    fn test_calculate_model_cost_default_fallback() {
        // Unknown model: $0.50/M prompt + $2.0/M completion, 50% markup
        let cost = calculate_model_cost("some/unknown-model", 1_000_000, 1_000_000);
        let expected = (0.50 + 2.0) * 1.50;
        assert!((cost - expected).abs() < 1e-10);
    }

    #[test]
    fn test_hash_token_for_cache_deterministic() {
        let h1 = hash_token_for_cache("my-secret-token");
        let h2 = hash_token_for_cache("my-secret-token");
        assert_eq!(h1, h2);
    }

    #[test]
    fn test_hash_token_for_cache_is_64_hex_chars() {
        let h = hash_token_for_cache("test-token-value");
        assert_eq!(h.len(), 64);
        assert!(h.chars().all(|c| c.is_ascii_hexdigit()));
    }

    #[test]
    fn test_hash_token_for_cache_different_inputs_differ() {
        let h1 = hash_token_for_cache("token-a");
        let h2 = hash_token_for_cache("token-b");
        assert_ne!(h1, h2);
    }

    fn make_valid_bypass_token(user_id: i32, expires_at: i64, secret: &str) -> String {
        let payload = format!("{}:{}", user_id, expires_at);
        let mut mac = HmacSha256::new_from_slice(secret.as_bytes()).unwrap();
        mac.update(payload.as_bytes());
        let sig = mac.finalize().into_bytes();
        let sig_hex: String = sig.iter().map(|b| format!("{:02x}", b)).collect();
        format!("{}:{}:{}", user_id, expires_at, sig_hex)
    }

    #[test]
    fn test_verify_bypass_token_valid() {
        let secret = "test-service-token";
        let expires_at = 2000000000_i64; // far future
        let token = make_valid_bypass_token(42, expires_at, secret);
        let result = verify_bypass_token_at(&token, secret, 1000000000);
        assert_eq!(result, Some(42));
    }

    #[test]
    fn test_verify_bypass_token_expired() {
        let secret = "test-service-token";
        let expires_at = 1000000000_i64;
        let token = make_valid_bypass_token(42, expires_at, secret);
        // now_sec >= expires_at → rejected
        let result = verify_bypass_token_at(&token, secret, 1000000000);
        assert_eq!(result, None);
    }

    #[test]
    fn test_verify_bypass_token_invalid_signature() {
        let secret = "test-service-token";
        let token = "42:2000000000:deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef";
        let result = verify_bypass_token_at(token, secret, 1000000000);
        assert_eq!(result, None);
    }

    #[test]
    fn test_verify_bypass_token_wrong_secret() {
        let token = make_valid_bypass_token(42, 2000000000, "correct-secret");
        let result = verify_bypass_token_at(&token, "wrong-secret", 1000000000);
        assert_eq!(result, None);
    }

    #[test]
    fn test_verify_bypass_token_malformed() {
        assert_eq!(verify_bypass_token_at("not-a-token", "secret", 1000), None);
        assert_eq!(verify_bypass_token_at("only:two", "secret", 1000), None);
        assert_eq!(verify_bypass_token_at("a:b:c:d", "secret", 1000), None);
        assert_eq!(verify_bypass_token_at("", "secret", 1000), None);
    }

    // ─── New tests: Model allowlist ─────────────────────────────────────────

    fn make_lists() -> (Vec<String>, Vec<String>, Vec<String>) {
        let free: Vec<String> = FALLBACK_FREE_MODELS.iter().map(|s| s.to_string()).collect();
        let pro: Vec<String> = FALLBACK_PRO_MODELS.iter().map(|s| s.to_string()).collect();
        let max: Vec<String> = FALLBACK_MAX_MODELS.iter().map(|s| s.to_string()).collect();
        (free, pro, max)
    }

    #[test]
    fn test_is_model_allowed_free_tier_only_accepts_free_slugs() {
        let (free, pro, max) = make_lists();
        // Free-tier models pass
        assert!(is_model_allowed_for_tier("google/gemini-3-flash-lite:free", "free", &free, &pro, &max));
        assert!(is_model_allowed_for_tier("qwen/qwen-3-32b:free", "free", &free, &pro, &max));
        // Pro models rejected on free tier
        assert!(!is_model_allowed_for_tier("google/gemini-3.5-flash", "free", &free, &pro, &max));
        assert!(!is_model_allowed_for_tier("anthropic/claude-haiku-4.5", "free", &free, &pro, &max));
        // Max models rejected on free tier
        assert!(!is_model_allowed_for_tier("google/gemini-3.5-pro", "free", &free, &pro, &max));
        // Random model rejected
        assert!(!is_model_allowed_for_tier("random/nonexistent", "free", &free, &pro, &max));
    }

    #[test]
    fn test_is_model_allowed_pro_tier_accepts_free_and_pro() {
        let (free, pro, max) = make_lists();
        // Free models pass on pro
        assert!(is_model_allowed_for_tier("google/gemini-3-flash-lite:free", "pro", &free, &pro, &max));
        // Pro models pass on pro
        assert!(is_model_allowed_for_tier("google/gemini-3.5-flash", "pro", &free, &pro, &max));
        assert!(is_model_allowed_for_tier("openai/gpt-5-nano", "pro", &free, &pro, &max));
        assert!(is_model_allowed_for_tier("openai/gpt-5-mini", "pro", &free, &pro, &max));
        // Max models rejected on pro
        assert!(!is_model_allowed_for_tier("google/gemini-3.5-pro", "pro", &free, &pro, &max));
        assert!(!is_model_allowed_for_tier("anthropic/claude-sonnet-4.7", "pro", &free, &pro, &max));
    }

    #[test]
    fn test_is_model_allowed_max_tier_accepts_all_except_frontier() {
        let (free, pro, max) = make_lists();
        // Free models pass on max
        assert!(is_model_allowed_for_tier("google/gemini-3-flash-lite:free", "max", &free, &pro, &max));
        // Pro models pass on max
        assert!(is_model_allowed_for_tier("google/gemini-3.5-flash", "max", &free, &pro, &max));
        // Max models pass on max
        assert!(is_model_allowed_for_tier("google/gemini-3.5-pro", "max", &free, &pro, &max));
        assert!(is_model_allowed_for_tier("anthropic/claude-sonnet-4.7", "max", &free, &pro, &max));
        assert!(is_model_allowed_for_tier("x-ai/grok-4-mini", "max", &free, &pro, &max));
        // PAYG tier uses same allowlist as max
        assert!(is_model_allowed_for_tier("google/gemini-3.5-pro", "payg", &free, &pro, &max));
        // Frontier ALWAYS rejected
        assert!(!is_model_allowed_for_tier("anthropic/claude-opus-5", "max", &free, &pro, &max));
    }

    #[test]
    fn test_forbidden_frontier_pattern_rejected_at_all_tiers() {
        let (free, pro, max) = make_lists();
        let frontier_models = &[
            "anthropic/claude-opus-4",
            "anthropic/claude-opus-5",
            "openai/gpt-5",
            "openai/o3-pro",
            "openai/o4-mini",
            "google/gemini-3-ultra",
            "anthropic/claude-sonnet-4.7:thinking",
            "openai/gpt-5-mini:reasoning-xhigh",
        ];
        for tier in &["free", "pro", "max", "payg"] {
            for model in frontier_models {
                assert!(
                    !is_model_allowed_for_tier(model, tier, &free, &pro, &max),
                    "Expected '{}' to be REJECTED on tier '{}', but it was allowed",
                    model, tier
                );
            }
        }
    }

    #[test]
    fn test_get_max_tokens_cap_rescaled_values_match_spec() {
        assert_eq!(get_max_tokens_cap("free"), 200);
        assert_eq!(get_max_tokens_cap("pro"), 2000);
        assert_eq!(get_max_tokens_cap("max"), 8000);
        assert_eq!(get_max_tokens_cap("payg"), 8000);
    }

    #[test]
    fn test_get_per_request_cost_cap_matches_spec() {
        assert!((get_per_request_cost_cap_usd("free") - 0.05).abs() < 1e-10);
        assert!((get_per_request_cost_cap_usd("pro") - 0.20).abs() < 1e-10);
        assert!((get_per_request_cost_cap_usd("max") - 0.50).abs() < 1e-10);
        assert!((get_per_request_cost_cap_usd("payg") - 0.50).abs() < 1e-10);
        assert!((get_per_request_cost_cap_usd("unknown") - 0.05).abs() < 1e-10);
    }

    #[test]
    fn test_payg_tier_uses_max_allowlist_and_caps() {
        let (free, pro, max) = make_lists();
        // PAYG uses same allowlist as max
        assert!(is_model_allowed_for_tier("google/gemini-3.5-pro", "payg", &free, &pro, &max));
        assert!(is_model_allowed_for_tier("anthropic/claude-sonnet-4.7", "payg", &free, &pro, &max));
        assert!(is_model_allowed_for_tier("openai/gpt-5-mini", "payg", &free, &pro, &max));
        // Caps match max
        assert_eq!(get_max_tokens_cap("payg"), 8000);
        assert!((get_per_request_cost_cap_usd("payg") - 0.50).abs() < 1e-10);
    }

    #[test]
    fn test_calculate_model_cost_with_new_lineup() {
        // gemini-3.5-flash: $0.15/M prompt + $0.60/M completion, 50% markup
        let cost = calculate_model_cost("google/gemini-3.5-flash", 1_000_000, 1_000_000);
        let expected = (0.15 + 0.60) * 1.50;
        assert!((cost - expected).abs() < 1e-10);

        // claude-haiku-4.5: $0.25/M + $1.25/M, 50% markup
        let cost2 = calculate_model_cost("anthropic/claude-haiku-4.5", 500_000, 500_000);
        let expected2 = (500_000.0 * 0.25 + 500_000.0 * 1.25) / 1_000_000.0 * 1.50;
        assert!((cost2 - expected2).abs() < 1e-10);

        // gpt-5-nano: $0.10/M + $0.40/M, 50% markup
        let cost3 = calculate_model_cost("openai/gpt-5-nano", 2_000_000, 1_000_000);
        let expected3 = (2_000_000.0 * 0.10 + 1_000_000.0 * 0.40) / 1_000_000.0 * 1.50;
        assert!((cost3 - expected3).abs() < 1e-10);

        // Free model always zero
        let cost4 = calculate_model_cost("qwen/qwen-3-32b:free", 10_000_000, 5_000_000);
        assert_eq!(cost4, 0.0);
    }

    #[test]
    fn test_is_frontier_forbidden_allows_nano_mini() {
        // gpt-5-nano and gpt-5-mini should NOT be frontier-blocked
        assert!(!is_frontier_forbidden("openai/gpt-5-nano"));
        assert!(!is_frontier_forbidden("openai/gpt-5-mini"));
        // But bare gpt-5 is forbidden
        assert!(is_frontier_forbidden("openai/gpt-5"));
        assert!(is_frontier_forbidden("openai/gpt-5-turbo")); // hypothetical variant still matches
    }
    #[test]
    fn test_v1_models_tier_filter_filtering() {
        let free_list = vec![
            "google/gemini-3-flash-lite:free".to_string(),
            "qwen/qwen-3-32b:free".to_string(),
        ];
        let pro_list = vec![
            "google/gemini-3.5-flash".to_string(),
            "openai/gpt-5-nano".to_string(),
            "openai/gpt-5-mini".to_string(),
        ];
        let max_list = vec![
            "google/gemini-3.5-pro".to_string(),
            "anthropic/claude-sonnet-4.7".to_string(),
            "openai/gpt-5".to_string(),
        ];

        let mut free_models = Vec::new();
        free_models.extend(free_list.clone());
        free_models.retain(|m| !is_frontier_forbidden(m));
        assert!(free_models.contains(&"google/gemini-3-flash-lite:free".to_string()));
        assert!(!free_models.contains(&"openai/gpt-5".to_string()));

        let mut pro_models = Vec::new();
        pro_models.extend(free_list.clone());
        pro_models.extend(pro_list.clone());
        pro_models.retain(|m| !is_frontier_forbidden(m));
        assert!(pro_models.contains(&"google/gemini-3.5-flash".to_string()));
        assert!(!pro_models.contains(&"openai/gpt-5".to_string()));

        let mut max_models = Vec::new();
        max_models.extend(free_list.clone());
        max_models.extend(pro_list.clone());
        max_models.extend(max_list.clone());
        max_models.retain(|m| !is_frontier_forbidden(m));
        assert!(max_models.contains(&"google/gemini-3.5-pro".to_string()));
        assert!(max_models.contains(&"anthropic/claude-sonnet-4.7".to_string()));
        assert!(!max_models.contains(&"openai/gpt-5".to_string()));
    }
}
