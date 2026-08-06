use crate::auth::middleware::get_auth_context;
use crate::config::{tier_ceiling_usd, VariantKind, VariantMap};
use crate::db;
use crate::errors::{AppError, Result};
use chrono::{Datelike, TimeZone, Utc};
use hmac::{Hmac, Mac};
use serde::{Deserialize, Serialize};
use sha2::Sha256;
use worker::*;

type HmacSha256 = Hmac<Sha256>;

#[derive(Debug, Deserialize)]
pub struct LemonSqueezyWebhook {
    pub meta: WebhookMeta,
    pub data: WebhookData,
}

#[derive(Debug, Deserialize)]
pub struct WebhookMeta {
    pub event_name: String,
    pub custom_data: Option<CustomData>,
}

#[derive(Debug, Deserialize)]
pub struct CustomData {
    pub user_id: Option<String>,
}

#[derive(Debug, Deserialize)]
pub struct WebhookData {
    pub id: String,
    #[allow(dead_code)]
    pub r#type: String,
    pub attributes: WebhookAttributes,
}

#[derive(Debug, Deserialize)]
pub struct WebhookAttributes {
    pub variant_id: Option<i32>,
    pub status: Option<String>,
    pub renews_at: Option<String>,
    pub ends_at: Option<String>,
    pub expires_at: Option<String>,
    pub created_at: Option<String>,
    pub updated_at: Option<String>,
    pub total: Option<i32>,
    // Cumulative amount refunded on an order, in cents (LemonSqueezy order
    // object). Used to deduct only the incremental refund, supporting partial
    // and repeated refunds.
    pub refunded_amount: Option<i32>,
    pub customer_id: Option<serde_json::Value>,
    // Order webhooks nest the purchased variant here; subscription webhooks put
    // variant_id at the top level. See order_created handling.
    pub first_order_item: Option<FirstOrderItem>,
}

#[derive(Debug, Deserialize)]
pub struct FirstOrderItem {
    pub variant_id: Option<i32>,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct PortalSessionResponse {
    pub url: String,
}

#[derive(Debug, Serialize)]
pub struct InvoiceEntry {
    pub id: String,
    pub created_at: i64,
    pub amount_usd: f64,
    pub currency: String,
    pub status: String,
    pub pdf_url: String,
}

#[derive(Debug, Serialize)]
pub struct InvoicesResponse {
    pub invoices: Vec<InvoiceEntry>,
}

fn verify_signature(secret: &str, body: &[u8], signature_hex: &str) -> bool {
    let signature_bytes = match hex_decode(signature_hex) {
        Some(bytes) => bytes,
        None => return false,
    };
    let mut mac = match HmacSha256::new_from_slice(secret.as_bytes()) {
        Ok(m) => m,
        Err(_) => return false,
    };
    mac.update(body);
    let result = mac.finalize();
    let computed_bytes = result.into_bytes();

    if computed_bytes.len() != signature_bytes.len() {
        return false;
    }
    use subtle::ConstantTimeEq;
    computed_bytes
        .as_slice()
        .ct_eq(&signature_bytes)
        .unwrap_u8()
        == 1
}

fn hex_decode(hex_str: &str) -> Option<Vec<u8>> {
    if !hex_str.len().is_multiple_of(2) {
        return None;
    }
    let mut bytes = Vec::new();
    for i in (0..hex_str.len()).step_by(2) {
        let res = u8::from_str_radix(&hex_str[i..i + 2], 16).ok()?;
        bytes.push(res);
    }
    Some(bytes)
}

fn calculate_period_start(created_at_sec: i64) -> i64 {
    let created_dt = match Utc.timestamp_opt(created_at_sec, 0) {
        chrono::LocalResult::Single(dt) => dt,
        _ => Utc::now(),
    };
    let now = Utc::now();
    let day = created_dt.day();

    let year = now.year();
    let month = now.month();

    let clamp_day = |y: i32, m: u32, d: u32| -> u32 {
        let mut target = d;
        while Utc
            .with_ymd_and_hms(y, m, target, 0, 0, 0)
            .single()
            .is_none()
        {
            target -= 1;
        }
        target
    };

    let this_month_day = clamp_day(year, month, day);
    let candidate_this_month = match Utc.with_ymd_and_hms(year, month, this_month_day, 0, 0, 0) {
        chrono::LocalResult::Single(dt) => dt.timestamp(),
        _ => now.timestamp(),
    };

    if now.timestamp() >= candidate_this_month {
        candidate_this_month
    } else {
        let (prev_year, prev_month) = if month == 1 {
            (year - 1, 12)
        } else {
            (year, month - 1)
        };
        let prev_month_day = clamp_day(prev_year, prev_month, day);
        match Utc.with_ymd_and_hms(prev_year, prev_month, prev_month_day, 0, 0, 0) {
            chrono::LocalResult::Single(dt) => dt.timestamp(),
            _ => now.timestamp(),
        }
    }
}

fn customer_id_to_string(v: &serde_json::Value) -> String {
    match v {
        serde_json::Value::Number(n) => n.to_string(),
        serde_json::Value::String(s) => s.clone(),
        _ => v.to_string(),
    }
}

fn subscription_period_end(attributes: &WebhookAttributes) -> Option<i64> {
    attributes
        .ends_at
        .as_deref()
        .or(attributes.expires_at.as_deref())
        .or(attributes.renews_at.as_deref())
        .and_then(|value| chrono::DateTime::parse_from_rfc3339(value).ok())
        .map(|date_time| date_time.timestamp())
}

fn subscription_tier_for_event<'a>(
    event_name: &str,
    mapped_tier: Option<&'a str>,
    period_end: Option<i64>,
    now_sec: i64,
) -> Option<&'a str> {
    match event_name {
        "subscription_expired" => Some("free"),
        "subscription_cancelled" if period_end.is_some_and(|end| end > now_sec) => mapped_tier,
        "subscription_cancelled" => Some("free"),
        _ => mapped_tier,
    }
}

async fn notify_proxy_tier_update(
    env: &Env,
    user_id: i32,
    tier: &str,
    period_start_sec: i64,
    base_ceiling_usd: f64,
) {
    let proxy_url = match env.var("MAHO_PROXY_URL").map(|v| v.to_string()) {
        Ok(v) if !v.trim().is_empty() => v,
        _ => {
            console_error!("MAHO_PROXY_URL missing; skipping proxy tier update");
            return;
        }
    };
    let service_token = match env
        .secret("MAHO_RELAY_SERVICE_TOKEN")
        .map(|v| v.to_string())
    {
        Ok(v) if !v.trim().is_empty() => v,
        _ => {
            console_error!("MAHO_RELAY_SERVICE_TOKEN missing; skipping proxy tier update");
            return;
        }
    };

    #[derive(Serialize)]
    struct UpdateTierReq {
        user_id: String,
        tier: String,
        period_start_at: i64,
        base_ceiling_usd: f64,
    }

    let body = match serde_json::to_string(&UpdateTierReq {
        user_id: user_id.to_string(),
        tier: tier.to_string(),
        period_start_at: period_start_sec,
        base_ceiling_usd,
    }) {
        Ok(b) => b,
        Err(e) => {
            console_error!("failed to serialize proxy tier update: {e}");
            return;
        }
    };

    let headers = Headers::new();
    let _ = headers.set("Content-Type", "application/json");
    let _ = headers.set("Authorization", &format!("Bearer {service_token}"));

    let mut init = RequestInit::new();
    init.with_method(Method::Post)
        .with_headers(headers)
        .with_body(Some(body.into()));

    let url = format!("{proxy_url}/billing/update_tier");
    match Request::new_with_init(&url, &init) {
        Ok(req) => match Fetch::Request(req).send().await {
            Ok(resp) if (200..300).contains(&resp.status_code()) => {
                console_log!("proxy tier updated: user={user_id} tier={tier}");
            }
            Ok(resp) => {
                console_error!(
                    "proxy tier update failed: user={user_id} status={}",
                    resp.status_code()
                );
            }
            Err(e) => {
                console_error!("proxy tier update fetch err user={user_id}: {e}");
            }
        },
        Err(e) => console_error!("proxy tier update request build err: {e}"),
    }
}

pub(crate) async fn notify_proxy_credits_update(
    env: &Env,
    user_id: i32,
    new_balance_usd: f64,
    delta_usd: f64,
) {
    let proxy_url = match env.var("MAHO_PROXY_URL").map(|v| v.to_string()) {
        Ok(v) if !v.trim().is_empty() => v,
        _ => {
            console_error!("MAHO_PROXY_URL missing; skipping proxy credits update");
            return;
        }
    };
    let service_token = match env
        .secret("MAHO_RELAY_SERVICE_TOKEN")
        .map(|v| v.to_string())
    {
        Ok(v) if !v.trim().is_empty() => v,
        _ => {
            console_error!("MAHO_RELAY_SERVICE_TOKEN missing; skipping proxy credits update");
            return;
        }
    };

    #[derive(Serialize)]
    struct CreditsUpdateReq {
        user_id: String,
        new_balance_usd: f64,
        delta_usd: f64,
    }

    let body = match serde_json::to_string(&CreditsUpdateReq {
        user_id: user_id.to_string(),
        new_balance_usd,
        delta_usd,
    }) {
        Ok(b) => b,
        Err(e) => {
            console_error!("failed to serialize proxy credits update: {e}");
            return;
        }
    };

    let headers = Headers::new();
    let _ = headers.set("Content-Type", "application/json");
    let _ = headers.set("Authorization", &format!("Bearer {service_token}"));

    let mut init = RequestInit::new();
    init.with_method(Method::Post)
        .with_headers(headers)
        .with_body(Some(body.into()));

    let url = format!("{proxy_url}/billing/credits_update");
    match Request::new_with_init(&url, &init) {
        Ok(req) => match Fetch::Request(req).send().await {
            Ok(resp) if (200..300).contains(&resp.status_code()) => {
                console_log!("proxy credits updated: user={user_id} balance={new_balance_usd}");
            }
            Ok(resp) => {
                console_error!(
                    "proxy credits update failed: user={user_id} status={}",
                    resp.status_code()
                );
            }
            Err(e) => {
                console_error!("proxy credits update fetch err user={user_id}: {e}");
            }
        },
        Err(e) => console_error!("proxy credits update request build err: {e}"),
    }
}

async fn notify_proxy_topup(env: &Env, user_id: i32, amount_usd: f64) {
    let proxy_url = match env.var("MAHO_PROXY_URL").map(|v| v.to_string()) {
        Ok(v) if !v.trim().is_empty() => v,
        _ => {
            console_error!("MAHO_PROXY_URL missing; skipping proxy topup");
            return;
        }
    };
    let service_token = match env
        .secret("MAHO_RELAY_SERVICE_TOKEN")
        .map(|v| v.to_string())
    {
        Ok(v) if !v.trim().is_empty() => v,
        _ => {
            console_error!("MAHO_RELAY_SERVICE_TOKEN missing; skipping proxy topup");
            return;
        }
    };

    #[derive(Serialize)]
    struct AddTopupReq {
        user_id: String,
        amount_usd: f64,
    }

    let body = match serde_json::to_string(&AddTopupReq {
        user_id: user_id.to_string(),
        amount_usd,
    }) {
        Ok(b) => b,
        Err(e) => {
            console_error!("failed to serialize proxy topup: {e}");
            return;
        }
    };

    let headers = Headers::new();
    let _ = headers.set("Content-Type", "application/json");
    let _ = headers.set("Authorization", &format!("Bearer {service_token}"));

    let mut init = RequestInit::new();
    init.with_method(Method::Post)
        .with_headers(headers)
        .with_body(Some(body.into()));

    let url = format!("{proxy_url}/billing/topup");
    match Request::new_with_init(&url, &init) {
        Ok(req) => match Fetch::Request(req).send().await {
            Ok(resp) if (200..300).contains(&resp.status_code()) => {
                console_log!("proxy topup: user={user_id} amount={amount_usd}");
            }
            Ok(resp) => {
                console_error!(
                    "proxy topup failed: user={user_id} status={}",
                    resp.status_code()
                );
            }
            Err(e) => {
                console_error!("proxy topup fetch err user={user_id}: {e}");
            }
        },
        Err(e) => console_error!("proxy topup request build err: {e}"),
    }
}

pub async fn lemonsqueezy_webhook(mut req: Request, env: Env) -> Result<Response> {
    let secret = env
        .secret("LS_WEBHOOK_SECRET")
        .map(|v| v.to_string())
        .map_err(|_| AppError::Internal("LS_WEBHOOK_SECRET missing".to_string()))?;
    if secret.trim().is_empty() {
        return Err(AppError::Internal("LS_WEBHOOK_SECRET empty".to_string()));
    }

    let signature = req.headers().get("X-Signature")?.unwrap_or_default();
    let body_bytes = req.bytes().await?;

    if !verify_signature(&secret, &body_bytes, &signature) {
        return Err(AppError::Unauthorized("invalid_signature".to_string()));
    }

    let payload: LemonSqueezyWebhook = serde_json::from_slice(&body_bytes).map_err(|e| {
        console_error!("invalid_json detail (billing webhook): {e}");
        AppError::BadRequest("invalid_json".to_string())
    })?;

    let event_name = &payload.meta.event_name;
    let event_id = &payload.data.id;
    let now_sec = Utc::now().timestamp();

    let db = env.d1("MAHO_DB")?;

    // Subscription Invoice events (payment_failed/recovered) arrive as invoice
    // objects without checkout custom_data, so they carry no attributable user
    // and are not tier-processed here. Acknowledge cleanly (200) rather than
    // erroring after the claim. Whether a failed payment revokes access is a
    // billing-policy decision surfaced via subscription_expired, not here.
    if matches!(
        event_name.as_str(),
        "subscription_payment_failed" | "subscription_payment_recovered"
    ) {
        console_log!("invoice event acknowledged (not tier-processed): {event_name}");
        return Response::ok("Invoice event acknowledged").map_err(AppError::from);
    }

    // Claim BEFORE any side effect (signature already verified above). The key
    // combines event_name + data.id (LemonSqueezy's data.id is the RESOURCE id,
    // shared across event types) + the event timestamp (updated_at): a retry of
    // one delivery carries the same updated_at and dedups, while two distinct
    // events on the same resource (e.g. successive subscription_updated) differ
    // and both process. On a later side-effect failure we keep the marker and
    // return 500 (retries no-op; event logged for reconciliation) — trading a
    // possible miss for zero double-credit.
    let event_ts = payload
        .data
        .attributes
        .updated_at
        .as_deref()
        .or(payload.data.attributes.created_at.as_deref())
        .unwrap_or("");
    let claim_key = format!("{event_name}:{event_id}:{event_ts}");
    if !db::claim_event(&db, &claim_key, now_sec).await? {
        return Response::ok("Event already processed").map_err(AppError::from);
    }

    let custom_data = payload
        .meta
        .custom_data
        .as_ref()
        .ok_or_else(|| AppError::BadRequest("missing_custom_data".to_string()))?;
    let user_id_str = custom_data
        .user_id
        .as_ref()
        .ok_or_else(|| AppError::BadRequest("missing_user_id".to_string()))?;
    let user_id: i32 = user_id_str
        .parse()
        .map_err(|_| AppError::BadRequest("invalid_user_id".to_string()))?;

    let is_sub_event = matches!(
        event_name.as_str(),
        "subscription_created"
            | "subscription_updated"
            | "subscription_cancelled"
            | "subscription_resumed"
            | "subscription_expired"
    );

    let variant_map = VariantMap::from_env(&env);

    if is_sub_event {
        let variant_id = payload.data.attributes.variant_id.unwrap_or(0);
        let status = payload
            .data
            .attributes
            .status
            .clone()
            .unwrap_or_else(|| "active".to_string());

        let mapped_tier = match variant_map.map_variant(variant_id) {
            Some(VariantKind::Subscription(t)) => Some(t),
            _ => {
                console_error!(
                    "unmapped LS variant_id={variant_id} event={event_name} user={user_id}"
                );
                None
            }
        };
        let period_end = subscription_period_end(&payload.data.attributes);
        let tier = subscription_tier_for_event(event_name, mapped_tier, period_end, now_sec);
        // Cancellation must retain lifecycle-event ordering precedence even when
        // its future period end means the paid tier remains active for now.
        let wins_equal_event_tie = matches!(
            event_name.as_str(),
            "subscription_cancelled" | "subscription_expired"
        );

        if let Some(tier) = tier {
            let created_at_sec = payload
                .data
                .attributes
                .created_at
                .as_ref()
                .and_then(|s| chrono::DateTime::parse_from_rfc3339(s).ok())
                .map(|dt| dt.timestamp())
                .unwrap_or(now_sec);

            // Ordering guard uses the EVENT time (updated_at), not the
            // subscription's creation time, which is constant across lifecycle
            // events and would let a late cancel/update lose the monotonic race.
            let event_at_sec = payload
                .data
                .attributes
                .updated_at
                .as_ref()
                .and_then(|s| chrono::DateTime::parse_from_rfc3339(s).ok())
                .map(|dt| dt.timestamp())
                .unwrap_or(created_at_sec);

            let expires_at = period_end;

            let created_dt = Utc
                .timestamp_opt(created_at_sec, 0)
                .single()
                .unwrap_or_else(Utc::now);
            let anniversary_reset_day = Some(created_dt.day() as i32);
            let period_start_sec = calculate_period_start(created_at_sec);
            let period_start_at = Some(period_start_sec);

            let customer_id_str = payload
                .data
                .attributes
                .customer_id
                .as_ref()
                .map(customer_id_to_string);

            let has_payment = match event_name.as_str() {
                "subscription_created" => Some(true),
                "subscription_expired" => Some(false),
                _ => None,
            };

            let applied = db::update_user_subscription(
                &db,
                user_id,
                tier,
                &status,
                expires_at,
                period_start_at,
                anniversary_reset_day,
                has_payment,
                customer_id_str,
                Some(event_at_sec),
                wins_equal_event_tie,
            )
            .await?;

            if applied {
                let base_ceiling = tier_ceiling_usd(tier);
                notify_proxy_tier_update(&env, user_id, tier, period_start_sec, base_ceiling).await;
            } else {
                console_log!(
                    "stale subscription event skipped: user={user_id} event_at={event_at_sec}"
                );
            }
        }
    } else if event_name == "order_created" {
        // Orders nest the purchased variant under first_order_item; fall back to
        // the top-level field defensively.
        let variant_id = payload
            .data
            .attributes
            .first_order_item
            .as_ref()
            .and_then(|item| item.variant_id)
            .or(payload.data.attributes.variant_id)
            .unwrap_or(0);
        let status = payload.data.attributes.status.clone().unwrap_or_default();

        if status == "paid" || status == "completed" || status.is_empty() {
            let customer_id_str = payload
                .data
                .attributes
                .customer_id
                .as_ref()
                .map(customer_id_to_string);
            if let Err(e) =
                db::update_user_payment_method_and_customer(&db, user_id, true, customer_id_str)
                    .await
            {
                console_error!(
                    "update_user_payment_method_and_customer failed for user {user_id}: {e}"
                );
            }

            let payg_amount = match variant_map.map_variant(variant_id) {
                Some(VariantKind::PaygCredit(amt)) => Some(amt),
                // Pay-what-you-want: credit the amount actually paid (order
                // total, cents -> USD) rather than a fixed pack size.
                Some(VariantKind::PaygCreditPaid) => {
                    Some((payload.data.attributes.total.unwrap_or(0) as f64) / 100.0)
                }
                _ => None,
            };
            let amount_usd = payg_amount
                .unwrap_or_else(|| (payload.data.attributes.total.unwrap_or(0) as f64) / 100.0);

            let is_payg_credit = payg_amount.is_some();

            if is_payg_credit {
                match db::add_credit_balance(&db, user_id, amount_usd, now_sec).await {
                    Ok(new_balance) => {
                        console_log!(
                            "PAYG credit +${amount_usd} user={user_id} balance=${new_balance}"
                        );
                        notify_proxy_credits_update(&env, user_id, new_balance, amount_usd).await;
                    }
                    Err(e) => {
                        console_error!(
                            "add_credit_balance failed user={user_id} event={event_id}: {e}"
                        );
                        return Err(AppError::Internal(format!("failed to add credit: {e}")));
                    }
                }
            } else {
                notify_proxy_topup(&env, user_id, amount_usd).await;
            }
        }
    } else if event_name == "order_refunded" {
        // Deduct only the incremental refund: LemonSqueezy reports the
        // cumulative refunded_amount per order, so partial and repeated refunds
        // must charge back the delta, not the full order total. event_id is the
        // order id for order events.
        let new_refunded_cents = payload.data.attributes.refunded_amount.unwrap_or(0) as i64;
        match db::record_refund_delta(&db, event_id, new_refunded_cents, now_sec).await {
            Ok(Some(delta_cents)) => {
                let refund_amount_usd = delta_cents as f64 / 100.0;
                match db::chargeback_credit(&db, user_id, refund_amount_usd, now_sec).await {
                    Ok(new_balance) => {
                        console_log!("REFUND: -${refund_amount_usd} user={user_id} event={event_id} balance=${new_balance}");
                        notify_proxy_credits_update(&env, user_id, new_balance, -refund_amount_usd)
                            .await;
                    }
                    Err(e) => {
                        console_error!(
                            "chargeback_credit failed user={user_id} event={event_id}: {e}"
                        );
                        return Err(AppError::Internal(format!("failed to refund credit: {e}")));
                    }
                }
            }
            Ok(None) => {
                console_log!("order_refunded no new refund delta user={user_id} event={event_id}");
            }
            Err(e) => {
                console_error!("record_refund_delta failed user={user_id} event={event_id}: {e}");
                return Err(AppError::Internal(format!("failed to record refund: {e}")));
            }
        }
    }

    Response::ok("Webhook processed successfully").map_err(AppError::from)
}

pub async fn portal_session(req: Request, env: Env) -> Result<Response> {
    let db = env.d1("MAHO_DB")?;
    let auth = get_auth_context(&req, &db).await?;
    let user = auth.user;

    let fallback_url = "https://app.lemonsqueezy.com/my-orders".to_string();

    let customer_id = match user.lemonsqueezy_customer_id {
        Some(id) if !id.trim().is_empty() => id,
        _ => {
            return Response::from_json(&PortalSessionResponse { url: fallback_url })
                .map_err(AppError::from);
        }
    };

    let api_key = match env.secret("LEMONSQUEEZY_API_KEY").map(|v| v.to_string()) {
        Ok(k) if !k.trim().is_empty() => k,
        _ => {
            return Response::from_json(&PortalSessionResponse { url: fallback_url })
                .map_err(AppError::from);
        }
    };

    let headers = Headers::new();
    let _ = headers.set("Authorization", &format!("Bearer {api_key}"));
    let _ = headers.set("Accept", "application/vnd.api+json");
    let _ = headers.set("Content-Type", "application/vnd.api+json");

    let mut init = RequestInit::new();
    init.with_method(Method::Get).with_headers(headers);

    let url = format!("https://api.lemonsqueezy.com/v1/customers/{customer_id}");
    let request = Request::new_with_init(&url, &init)?;

    match Fetch::Request(request).send().await {
        Ok(mut resp) if (200..300).contains(&resp.status_code()) => {
            #[derive(Deserialize)]
            struct LSResponse {
                data: LSData,
            }
            #[derive(Deserialize)]
            struct LSData {
                attributes: LSAttributes,
            }
            #[derive(Deserialize)]
            struct LSAttributes {
                urls: LSUrls,
            }
            #[derive(Deserialize)]
            struct LSUrls {
                customer_portal: String,
            }

            if let Ok(ls_resp) = resp.json::<LSResponse>().await {
                return Response::from_json(&PortalSessionResponse {
                    url: ls_resp.data.attributes.urls.customer_portal,
                })
                .map_err(AppError::from);
            }
        }
        Ok(resp) => console_error!("LS Customers API status {}", resp.status_code()),
        Err(e) => console_error!("LS Customers API err: {e}"),
    }

    Response::from_json(&PortalSessionResponse { url: fallback_url }).map_err(AppError::from)
}

pub async fn invoices(req: Request, env: Env) -> Result<Response> {
    let db = env.d1("MAHO_DB")?;
    let auth = get_auth_context(&req, &db).await?;
    let user = auth.user;

    let empty = InvoicesResponse { invoices: vec![] };

    let api_key = match env.secret("LEMONSQUEEZY_API_KEY").map(|v| v.to_string()) {
        Ok(k) if !k.trim().is_empty() => k,
        _ => return Response::from_json(&empty).map_err(AppError::from),
    };

    let headers = Headers::new();
    let _ = headers.set("Authorization", &format!("Bearer {api_key}"));
    let _ = headers.set("Accept", "application/vnd.api+json");
    let _ = headers.set("Content-Type", "application/vnd.api+json");

    let mut init = RequestInit::new();
    init.with_method(Method::Get).with_headers(headers);

    let email_encoded = urlencoding_encode(&user.email);
    let url = format!("https://api.lemonsqueezy.com/v1/orders?filter[user_email]={email_encoded}");
    let request = Request::new_with_init(&url, &init)?;

    match Fetch::Request(request).send().await {
        Ok(mut resp) if (200..300).contains(&resp.status_code()) => {
            #[derive(Deserialize)]
            struct LSOrdersResponse {
                data: Vec<LSOrderData>,
            }
            #[derive(Deserialize)]
            struct LSOrderData {
                id: String,
                attributes: LSOrderAttributes,
            }
            #[derive(Deserialize)]
            struct LSOrderAttributes {
                created_at: String,
                total: i64,
                currency: String,
                status: String,
                urls: LSOrderUrls,
            }
            #[derive(Deserialize)]
            struct LSOrderUrls {
                receipt: Option<String>,
            }

            if let Ok(ls_resp) = resp.json::<LSOrdersResponse>().await {
                let invoices: Vec<InvoiceEntry> = ls_resp
                    .data
                    .into_iter()
                    .map(|order| {
                        let created_at =
                            chrono::DateTime::parse_from_rfc3339(&order.attributes.created_at)
                                .map(|dt| dt.timestamp())
                                .unwrap_or(0);
                        InvoiceEntry {
                            id: order.id,
                            created_at,
                            amount_usd: (order.attributes.total as f64) / 100.0,
                            currency: order.attributes.currency,
                            status: order.attributes.status,
                            pdf_url: order.attributes.urls.receipt.unwrap_or_default(),
                        }
                    })
                    .collect();
                return Response::from_json(&InvoicesResponse { invoices }).map_err(AppError::from);
            }
        }
        Ok(resp) => console_error!("LS Orders API status {}", resp.status_code()),
        Err(e) => console_error!("LS Orders API err: {e}"),
    }

    Response::from_json(&empty).map_err(AppError::from)
}

fn urlencoding_encode(s: &str) -> String {
    let mut out = String::with_capacity(s.len() * 3);
    for b in s.bytes() {
        match b {
            b'A'..=b'Z' | b'a'..=b'z' | b'0'..=b'9' | b'-' | b'_' | b'.' | b'~' => {
                out.push(b as char);
            }
            _ => {
                out.push_str(&format!("%{b:02X}"));
            }
        }
    }
    out
}

#[derive(Debug, Deserialize)]
pub struct CreditBalanceRequest {
    pub user_id: i32,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct CreditBalanceResponse {
    pub balance_usd: f64,
    pub lifetime_purchased_usd: f64,
    pub last_consumption_at: Option<i64>,
}

pub async fn get_credit_balance(mut req: Request, env: Env) -> Result<Response> {
    crate::auth::routes::require_service_token(&req, &env)?;

    let user_id = if let Ok(url) = req.url() {
        if let Some((_, uid_str)) = url.query_pairs().find(|(k, _)| k == "user_id") {
            uid_str.parse::<i32>().ok()
        } else {
            None
        }
    } else {
        None
    };

    let user_id = match user_id {
        Some(uid) => uid,
        None => {
            let body = req.json::<CreditBalanceRequest>().await.map_err(|_| {
                AppError::BadRequest("user_id parameter or JSON body required".to_string())
            })?;
            body.user_id
        }
    };

    let db = env.d1("MAHO_DB")?;
    let stmt = db.prepare("SELECT balance_usd, lifetime_purchased_usd, last_consumption_at FROM credits WHERE user_id = ?1");
    let stmt = stmt.bind(&[crate::db::js(&user_id)?])?;

    #[derive(Deserialize)]
    struct CreditRow {
        balance_usd: f64,
        lifetime_purchased_usd: f64,
        last_consumption_at: Option<i64>,
    }

    let (balance_usd, lifetime_purchased_usd, last_consumption_at) =
        if let Some(row) = stmt.first::<CreditRow>(None).await? {
            (
                row.balance_usd,
                row.lifetime_purchased_usd,
                row.last_consumption_at,
            )
        } else {
            (0.0, 0.0, None)
        };

    let resp = CreditBalanceResponse {
        balance_usd,
        lifetime_purchased_usd,
        last_consumption_at,
    };

    Ok(Response::from_json(&resp)?)
}

#[derive(Debug, Deserialize)]
pub struct ConsumeCreditRequest {
    pub user_id: i32,
    pub amount_usd: f64,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct ConsumeCreditResponse {
    pub success: bool,
    pub remaining_balance_usd: f64,
}

/// All-or-nothing outcome of a PAYG consume request against a known balance.
#[derive(Debug, PartialEq)]
pub enum ConsumeDecision {
    /// `amount_usd` was not a positive, finite number.
    Rejected,
    /// Balance is too low; nothing is consumed and the balance is unchanged.
    Insufficient { remaining_usd: f64 },
    /// Balance covers the amount; the exact amount is deducted.
    Consumed { remaining_usd: f64 },
}

pub fn decide_credit_consume(current_usd: f64, amount_usd: f64) -> ConsumeDecision {
    if !(amount_usd.is_finite() && amount_usd > 0.0) {
        return ConsumeDecision::Rejected;
    }
    if current_usd >= amount_usd {
        ConsumeDecision::Consumed {
            remaining_usd: current_usd - amount_usd,
        }
    } else {
        ConsumeDecision::Insufficient {
            remaining_usd: current_usd,
        }
    }
}

pub async fn consume_credit(mut req: Request, env: Env) -> Result<Response> {
    crate::auth::routes::require_service_token(&req, &env)?;

    let payload = req.json::<ConsumeCreditRequest>().await?;
    let db = env.d1("MAHO_DB")?;
    let now = chrono::Utc::now().timestamp();

    // Amount validity does not depend on the stored balance, so classify it with
    // a saturating balance: only ConsumeDecision::Rejected (non-positive or
    // non-finite) can result here, never Insufficient.
    if decide_credit_consume(f64::MAX, payload.amount_usd) == ConsumeDecision::Rejected {
        return Err(AppError::BadRequest(
            "amount_usd must be a positive number".to_string(),
        ));
    }

    match crate::db::try_consume_credit(&db, payload.user_id, payload.amount_usd, now).await? {
        Some(remaining_usd) => {
            let resp = ConsumeCreditResponse {
                success: true,
                remaining_balance_usd: remaining_usd,
            };
            Ok(Response::from_json(&resp)?)
        }
        None => {
            let remaining_usd = crate::db::get_credit_balance_usd(&db, payload.user_id).await?;
            let resp = ConsumeCreditResponse {
                success: false,
                remaining_balance_usd: remaining_usd,
            };
            Ok(Response::from_json(&resp)?.with_status(402))
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn cancelled_subscription_keeps_paid_tier_until_period_end() {
        let payload: LemonSqueezyWebhook = serde_json::from_value(serde_json::json!({
            "meta": {
                "event_name": "subscription_cancelled",
                "custom_data": { "user_id": "42" }
            },
            "data": {
                "id": "subscription-1",
                "type": "subscriptions",
                "attributes": {
                    "variant_id": 123,
                    "status": "cancelled",
                    "ends_at": "2026-08-31T00:00:00Z"
                }
            }
        }))
        .unwrap();
        let period_end = subscription_period_end(&payload.data.attributes).unwrap();

        assert_eq!(
            subscription_tier_for_event(
                &payload.meta.event_name,
                Some("pro"),
                Some(period_end),
                period_end - 1,
            ),
            Some("pro")
        );
        assert_eq!(
            subscription_tier_for_event(
                &payload.meta.event_name,
                Some("pro"),
                Some(period_end),
                period_end,
            ),
            Some("free")
        );
        assert_eq!(
            subscription_tier_for_event(
                "subscription_expired",
                Some("pro"),
                Some(period_end),
                period_end - 1,
            ),
            Some("free")
        );
    }

    #[test]
    fn test_billing_credit_balance_response_serialization() {
        let resp = CreditBalanceResponse {
            balance_usd: 10.0,
            lifetime_purchased_usd: 50.0,
            last_consumption_at: Some(1700000000),
        };
        let json = serde_json::to_string(&resp).unwrap();
        assert!(json.contains("\"balance_usd\":10.0"));
        assert!(json.contains("\"lifetime_purchased_usd\":50.0"));
        assert!(json.contains("\"last_consumption_at\":1700000000"));
    }

    #[test]
    fn consume_deducts_exact_amount_when_balance_sufficient() {
        assert_eq!(
            decide_credit_consume(10.0, 2.5),
            ConsumeDecision::Consumed { remaining_usd: 7.5 }
        );
    }

    #[test]
    fn consume_of_full_balance_leaves_zero() {
        assert_eq!(
            decide_credit_consume(2.5, 2.5),
            ConsumeDecision::Consumed { remaining_usd: 0.0 }
        );
    }

    #[test]
    fn consume_does_not_overdraw_when_balance_too_low() {
        // Requesting more than available must NOT clamp-and-succeed: the
        // balance is left untouched and the outcome is Insufficient.
        assert_eq!(
            decide_credit_consume(0.10, 0.50),
            ConsumeDecision::Insufficient {
                remaining_usd: 0.10
            }
        );
    }

    #[test]
    fn consume_of_empty_balance_is_insufficient() {
        assert_eq!(
            decide_credit_consume(0.0, 0.01),
            ConsumeDecision::Insufficient { remaining_usd: 0.0 }
        );
    }

    #[test]
    fn consume_rejects_non_positive_or_non_finite_amounts() {
        assert_eq!(decide_credit_consume(10.0, 0.0), ConsumeDecision::Rejected);
        assert_eq!(decide_credit_consume(10.0, -1.0), ConsumeDecision::Rejected);
        assert_eq!(
            decide_credit_consume(10.0, f64::NAN),
            ConsumeDecision::Rejected
        );
        assert_eq!(
            decide_credit_consume(10.0, f64::INFINITY),
            ConsumeDecision::Rejected
        );
    }
}
