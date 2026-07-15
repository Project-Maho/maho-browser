use crate::auth::middleware::get_auth_context;
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
    pub created_at: Option<String>,
    pub total: Option<i32>,
    pub customer_id: Option<serde_json::Value>,
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
    if hex_str.len() % 2 != 0 {
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

    let mut headers = Headers::new();
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

async fn notify_proxy_credits_update(
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

    let mut headers = Headers::new();
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

    let mut headers = Headers::new();
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

    let payload: LemonSqueezyWebhook = serde_json::from_slice(&body_bytes)
        .map_err(|e| AppError::BadRequest(format!("invalid_json: {e}")))?;

    let event_id = &payload.data.id;
    let now_sec = Utc::now().timestamp();

    let db = env.d1("MAHO_DB")?;

    if db::has_processed_event(&db, event_id).await? {
        return Response::ok("Event already processed").map_err(AppError::from);
    }

    let event_name = &payload.meta.event_name;
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
            | "subscription_payment_failed"
            | "subscription_payment_recovered"
    );

    if is_sub_event {
        let variant_id = payload.data.attributes.variant_id.unwrap_or(0);
        let status = payload
            .data
            .attributes
            .status
            .clone()
            .unwrap_or_else(|| "active".to_string());

        let tier = if matches!(
            event_name.as_str(),
            "subscription_cancelled" | "subscription_expired" | "subscription_payment_failed"
        ) {
            "free"
        } else {
            match variant_id {
                1001 => "pro",
                2002 => "max",
                _ => "free",
            }
        };

        let created_at_sec = payload
            .data
            .attributes
            .created_at
            .as_ref()
            .and_then(|s| chrono::DateTime::parse_from_rfc3339(s).ok())
            .map(|dt| dt.timestamp())
            .unwrap_or(now_sec);

        let expires_at = payload
            .data
            .attributes
            .renews_at
            .as_ref()
            .and_then(|s| chrono::DateTime::parse_from_rfc3339(s).ok())
            .map(|dt| dt.timestamp());

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
            "subscription_cancelled" | "subscription_expired" => Some(false),
            _ => None,
        };

        db::update_user_subscription(
            &db,
            user_id,
            tier,
            &status,
            expires_at,
            period_start_at,
            anniversary_reset_day,
            has_payment,
            customer_id_str,
            Some(created_at_sec),
        )
        .await?;

        let base_ceiling = match tier {
            "pro" => 2.10,
            "max" => 13.30,
            _ => 0.50,
        };
        notify_proxy_tier_update(&env, user_id, tier, period_start_sec, base_ceiling).await;
    } else if event_name == "order_created" {
        let variant_id = payload.data.attributes.variant_id.unwrap_or(0);
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

            let amount_usd = match variant_id {
                4001 => 10.0,
                4002 => 50.0,
                4003 => 100.0,
                _ => (payload.data.attributes.total.unwrap_or(0) as f64) / 100.0,
            };

            let is_payg_credit = matches!(variant_id, 4001..=4003);

            if is_payg_credit {
                match db::add_credit_balance(&db, user_id, amount_usd, now_sec).await {
                    Ok(new_balance) => {
                        console_log!(
                            "PAYG credit +${amount_usd} user={user_id} balance=${new_balance}"
                        );
                        notify_proxy_credits_update(&env, user_id, new_balance, amount_usd).await;
                    }
                    Err(e) => {
                        console_error!("add_credit_balance failed user={user_id}: {e}");
                        return Err(AppError::Internal(format!("failed to add credit: {e}")));
                    }
                }
            } else {
                notify_proxy_topup(&env, user_id, amount_usd).await;
            }
        }
    } else if event_name == "order_refunded" {
        let refund_total_cents = payload.data.attributes.total.unwrap_or(0);
        let refund_amount_usd = (refund_total_cents as f64) / 100.0;

        if refund_amount_usd <= 0.0 {
            console_error!("order_refunded zero/negative for user {user_id} event {event_id}");
        } else {
            match db::chargeback_credit(&db, user_id, refund_amount_usd, now_sec).await {
                Ok(new_balance) => {
                    console_log!("REFUND: -${refund_amount_usd} user={user_id} event={event_id} balance=${new_balance}");
                    notify_proxy_credits_update(&env, user_id, new_balance, -refund_amount_usd)
                        .await;
                }
                Err(e) => {
                    console_error!("chargeback_credit failed user={user_id}: {e}");
                    return Err(AppError::Internal(format!("failed to refund credit: {e}")));
                }
            }
        }
    }

    db::mark_event_processed(&db, event_id, now_sec).await?;

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

    let mut headers = Headers::new();
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

    let mut headers = Headers::new();
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
