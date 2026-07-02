use std::sync::Arc;
use axum::{
    extract::State,
    http::{HeaderMap, StatusCode},
    response::IntoResponse,
};
use serde::{Deserialize, Serialize};
use crate::server::AppState;
use crate::storage;
use sha2::Sha256;
use hmac::{Hmac, Mac};
use chrono::{Datelike, TimeZone, Utc};
use crate::auth::middleware::BearerAuth;
use crate::auth::models::{InvoiceEntry, InvoicesResponse};

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
    pub total: Option<i32>, // in cents
    pub customer_id: Option<serde_json::Value>,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct PortalSessionResponse {
    pub url: String,
}

// Verify LemonSqueezy signature (R-2)
fn verify_signature(secret: &str, body: &[u8], signature_hex: &str) -> bool {
    let signature_bytes = match hex_decode(signature_hex) {
        Some(bytes) => bytes,
        None => return false,
    };
    let mut mac = HmacSha256::new_from_slice(secret.as_bytes()).unwrap();
    mac.update(body);
    let result = mac.finalize();
    let computed_bytes = result.into_bytes();
    
    // Constant time comparison to prevent timing attacks
    if computed_bytes.len() != signature_bytes.len() {
        return false;
    }
    use subtle::ConstantTimeEq;
    computed_bytes.as_slice().ct_eq(&signature_bytes).unwrap_u8() == 1
}

fn hex_decode(hex_str: &str) -> Option<Vec<u8>> {
    if !hex_str.len().is_multiple_of(2) {
        return None;
    }
    let mut bytes = Vec::new();
    for i in (0..hex_str.len()).step_by(2) {
        let res = u8::from_str_radix(&hex_str[i..i+2], 16).ok()?;
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
        while Utc.with_ymd_and_hms(y, m, target, 0, 0, 0).single().is_none() {
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

pub async fn lemonsqueezy_webhook(
    State(state): State<Arc<AppState>>,
    headers: HeaderMap,
    body_bytes: axum::body::Bytes,
) -> impl IntoResponse {
    // C5: Webhook HMAC fails-open fix (empty/missing secret = fatal abort)
    let secret = match std::env::var("LEMONSQUEEZY_WEBHOOK_SECRET") {
        Ok(val) if !val.trim().is_empty() => val,
        _ => {
            tracing::error!("FATAL: LEMONSQUEEZY_WEBHOOK_SECRET env var is missing or empty!");
            return (StatusCode::INTERNAL_SERVER_ERROR, "Configuration Error: Webhook secret missing").into_response();
        }
    };
    
    let signature = headers
        .get("X-Signature")
        .and_then(|h| h.to_str().ok())
        .unwrap_or_default();
    
    if !verify_signature(&secret, &body_bytes, signature) {
        return (StatusCode::UNAUTHORIZED, "Invalid signature").into_response();
    }

    // Parse payload
    let payload: LemonSqueezyWebhook = match serde_json::from_slice(&body_bytes) {
        Ok(p) => p,
        Err(e) => return (StatusCode::BAD_REQUEST, format!("Invalid JSON: {e}")).into_response(),
    };

    // Deduplicate event processing (R-2)
    let event_id = &payload.data.id;
    let now_sec = Utc::now().timestamp();
    
    match storage::has_processed_event(&state.db, event_id).await {
        Ok(true) => return (StatusCode::OK, "Event already processed").into_response(),
        Err(e) => return (StatusCode::INTERNAL_SERVER_ERROR, format!("DB error: {e}")).into_response(),
        _ => {}
    }

    // Process event
    let event_name = &payload.meta.event_name;
    let custom_data = match &payload.meta.custom_data {
        Some(cd) => cd,
        None => return (StatusCode::BAD_REQUEST, "Missing custom data").into_response(),
    };
    
    let user_id_str = match &custom_data.user_id {
        Some(uid) => uid,
        None => return (StatusCode::BAD_REQUEST, "Missing user_id in custom data").into_response(),
    };
    
    let user_id: i32 = match user_id_str.parse() {
        Ok(id) => id,
        Err(_) => return (StatusCode::BAD_REQUEST, "Invalid user_id format").into_response(),
    };

    let is_sub_event = event_name == "subscription_created"
        || event_name == "subscription_updated"
        || event_name == "subscription_cancelled"
        || event_name == "subscription_resumed"
        || event_name == "subscription_expired"
        || event_name == "subscription_payment_failed"
        || event_name == "subscription_payment_recovered";

    if is_sub_event {
        let variant_id = payload.data.attributes.variant_id.unwrap_or(0);
        let status = payload.data.attributes.status.clone().unwrap_or_else(|| "active".to_string());
        
        // C3: Handle all 8 billing states (subscription cancelled/resumed/expired/failed/recovered)
        // If expired or payment failed, degrade to free.
        let tier = if event_name == "subscription_cancelled"
            || event_name == "subscription_expired"
            || event_name == "subscription_payment_failed"
        {
            "free"
        } else {
            match variant_id {
                1001 => "pro",
                2002 => "max",
                _ => "free",
            }
        };

        let created_at_sec = payload.data.attributes.created_at
            .as_ref()
            .and_then(|s| chrono::DateTime::parse_from_rfc3339(s).ok())
            .map(|dt| dt.timestamp())
            .unwrap_or(now_sec);

        let expires_at = payload.data.attributes.renews_at
            .as_ref()
            .and_then(|s| chrono::DateTime::parse_from_rfc3339(s).ok())
            .map(|dt| dt.timestamp());

        let created_dt = Utc.timestamp_opt(created_at_sec, 0).unwrap();
        let anniversary_reset_day = Some(created_dt.day() as i32);
        let period_start_sec = calculate_period_start(created_at_sec);
        let period_start_at = Some(period_start_sec);

        let customer_id_str = payload.data.attributes.customer_id.as_ref().map(|v| match v {
            serde_json::Value::Number(n) => n.to_string(),
            serde_json::Value::String(s) => s.clone(),
            _ => v.to_string(),
        });

        let has_payment = if event_name == "subscription_created" {
            Some(true)
        } else if event_name == "subscription_cancelled" || event_name == "subscription_expired" {
            Some(false)
        } else {
            None
        };

        if let Err(e) = storage::update_user_subscription(
            &state.db,
            user_id,
            tier,
            &status,
            expires_at,
            period_start_at,
            anniversary_reset_day,
            has_payment,
            customer_id_str,
        ).await {
            return (StatusCode::INTERNAL_SERVER_ERROR, format!("Failed to update user subscription: {e}")).into_response();
        }

        // Notify billing proxy of subscription change
        let proxy_url = match std::env::var("MAHO_PROXY_URL") {
            Ok(val) if !val.trim().is_empty() => val,
            _ => {
                tracing::error!("FATAL: MAHO_PROXY_URL is missing or empty!");
                return (StatusCode::INTERNAL_SERVER_ERROR, "Configuration Error: Proxy URL missing").into_response();
            }
        };

        let service_token = match std::env::var("MAHO_RELAY_SERVICE_TOKEN") {
            Ok(val) if !val.trim().is_empty() => val,
            _ => {
                tracing::error!("FATAL: MAHO_RELAY_SERVICE_TOKEN is missing or empty!");
                return (StatusCode::INTERNAL_SERVER_ERROR, "Configuration Error: Service token missing").into_response();
            }
        };
        
        #[derive(Serialize)]
        struct UpdateTierReq {
            user_id: String,
            tier: String,
            period_start_at: i64,
            base_ceiling_usd: f64,
        }
        let base_ceiling = match tier {
            "pro" => 2.10,
            "max" => 13.30,
            _ => 0.50,
        };

        let client = reqwest::Client::new();
        // H13: Log proxy update error instead of silent ignore
        match client.post(format!("{}/billing/update_tier", proxy_url))
            .header("Authorization", format!("Bearer {service_token}"))
            .json(&UpdateTierReq {
                user_id: user_id.to_string(),
                tier: tier.to_string(),
                period_start_at: period_start_sec,
                base_ceiling_usd: base_ceiling,
            })
            .send()
            .await
        {
            Ok(resp) if resp.status().is_success() => {
                tracing::info!("Successfully updated proxy tier to {} for user {}", tier, user_id);
            }
            Ok(resp) => {
                tracing::error!("Failed to update billing proxy tier for user {}: status {}", user_id, resp.status());
            }
            Err(e) => {
                tracing::error!("Failed to contact billing proxy for user {} update: {}", user_id, e);
            }
        }

    } else if event_name == "order_created" {
        // Handle Top-up SKU payment event
        let variant_id = payload.data.attributes.variant_id.unwrap_or(0);
        let status = payload.data.attributes.status.clone().unwrap_or_default();

        if status == "paid" || status == "completed" || status.is_empty() {
            let customer_id_str = payload.data.attributes.customer_id.as_ref().map(|v| match v {
                serde_json::Value::Number(n) => n.to_string(),
                serde_json::Value::String(s) => s.clone(),
                _ => v.to_string(),
            });
            if let Err(e) = storage::update_user_payment_method_and_customer(&state.db, user_id, true, customer_id_str).await {
                tracing::error!("Failed to update payment method for user {}: {}", user_id, e);
            }

            // PAYG credit packs: 4001=$10, 4002=$50, 4003=$100
            // Legacy variants 3001-3003 (v1 subscription top-ups) are deliberately NOT aliased here:
            // they fall through to the `total / 100` fallback, which gives the actual v1 amount the
            // user paid (in cents), preserving correct accounting for any in-flight v1 orders.
            let amount_usd = match variant_id {
                4001 => 10.0,
                4002 => 50.0,
                4003 => 100.0,
                _ => (payload.data.attributes.total.unwrap_or(0) as f64) / 100.0,
            };

            let is_payg_credit = matches!(variant_id, 4001..=4003);

            if is_payg_credit {
                match storage::add_credit_balance(&state.db, user_id, amount_usd, now_sec).await {
                    Ok(new_balance) => {
                        tracing::info!(
                            "Added ${} PAYG credit for user {} (variant {}), new balance: ${}",
                            amount_usd, user_id, variant_id, new_balance
                        );

                        let proxy_url = match std::env::var("MAHO_PROXY_URL") {
                            Ok(val) if !val.trim().is_empty() => val,
                            _ => {
                                tracing::error!("FATAL: MAHO_PROXY_URL is missing or empty!");
                                return (StatusCode::INTERNAL_SERVER_ERROR, "Configuration Error: Proxy URL missing").into_response();
                            }
                        };

                        let service_token = match std::env::var("MAHO_RELAY_SERVICE_TOKEN") {
                            Ok(val) if !val.trim().is_empty() => val,
                            _ => {
                                tracing::error!("FATAL: MAHO_RELAY_SERVICE_TOKEN is missing or empty!");
                                return (StatusCode::INTERNAL_SERVER_ERROR, "Configuration Error: Service token missing").into_response();
                            }
                        };

                        #[derive(Serialize)]
                        struct CreditsUpdateReq {
                            user_id: String,
                            new_balance_usd: f64,
                            delta_usd: f64,
                        }

                        let client = reqwest::Client::new();
                        match client.post(format!("{}/billing/credits_update", proxy_url))
                            .header("Authorization", format!("Bearer {service_token}"))
                            .json(&CreditsUpdateReq {
                                user_id: user_id.to_string(),
                                new_balance_usd: new_balance,
                                delta_usd: amount_usd,
                            })
                            .send()
                            .await
                        {
                            Ok(resp) if resp.status().is_success() => {
                                tracing::info!("Successfully notified proxy of credits_update for user {}", user_id);
                            }
                            Ok(resp) => {
                                tracing::error!("Failed to notify proxy of credits_update for user {}: status {}", user_id, resp.status());
                            }
                            Err(e) => {
                                tracing::error!("Failed to contact proxy for credits_update for user {}: {}", user_id, e);
                            }
                        }
                    }
                    Err(e) => {
                        tracing::error!("Failed to add credit balance for user {}: {}", user_id, e);
                        return (StatusCode::INTERNAL_SERVER_ERROR, format!("Failed to add credit: {e}")).into_response();
                    }
                }
            } else {
                let proxy_url = match std::env::var("MAHO_PROXY_URL") {
                    Ok(val) if !val.trim().is_empty() => val,
                    _ => {
                        tracing::error!("FATAL: MAHO_PROXY_URL is missing or empty!");
                        return (StatusCode::INTERNAL_SERVER_ERROR, "Configuration Error: Proxy URL missing").into_response();
                    }
                };

                let service_token = match std::env::var("MAHO_RELAY_SERVICE_TOKEN") {
                    Ok(val) if !val.trim().is_empty() => val,
                    _ => {
                        tracing::error!("FATAL: MAHO_RELAY_SERVICE_TOKEN is missing or empty!");
                        return (StatusCode::INTERNAL_SERVER_ERROR, "Configuration Error: Service token missing").into_response();
                    }
                };

                #[derive(Serialize)]
                struct AddTopupReq {
                    user_id: String,
                    amount_usd: f64,
                }

                let client = reqwest::Client::new();
                match client.post(format!("{}/billing/topup", proxy_url))
                    .header("Authorization", format!("Bearer {service_token}"))
                    .json(&AddTopupReq {
                        user_id: user_id.to_string(),
                        amount_usd,
                    })
                    .send()
                    .await
                {
                    Ok(resp) if resp.status().is_success() => {
                        tracing::info!("Successfully added top-up ${} for user {} to proxy", amount_usd, user_id);
                    }
                    Ok(resp) => {
                        tracing::error!("Failed to add top-up for user {} in proxy: status {}", user_id, resp.status());
                    }
                    Err(e) => {
                        tracing::error!("Failed to contact proxy to add top-up for user {}: {}", user_id, e);
                    }
                }
            }
        }
    } else if event_name == "order_refunded" {
        // Per ADR 0010 §refund-policy: refund capped at unconsumed balance.
        // storage::chargeback_credit handles the cap (deduction.min(current_balance)).
        let refund_total_cents = payload.data.attributes.total.unwrap_or(0);
        let refund_amount_usd = (refund_total_cents as f64) / 100.0;

        if refund_amount_usd <= 0.0 {
            tracing::warn!(
                "order_refunded received with zero or negative total for user {} event {}",
                user_id, event_id
            );
        } else {
            match storage::chargeback_credit(&state.db, user_id, refund_amount_usd, now_sec).await {
                Ok(new_balance) => {
                    tracing::warn!(
                        "REFUND: deducted ${} from user {} PAYG credits (event {}), new balance: ${}",
                        refund_amount_usd, user_id, event_id, new_balance
                    );

                    let proxy_url = match std::env::var("MAHO_PROXY_URL") {
                        Ok(val) if !val.trim().is_empty() => val,
                        _ => {
                            tracing::error!("FATAL: MAHO_PROXY_URL is missing or empty!");
                            return (StatusCode::INTERNAL_SERVER_ERROR, "Configuration Error: Proxy URL missing").into_response();
                        }
                    };

                    let service_token = match std::env::var("MAHO_RELAY_SERVICE_TOKEN") {
                        Ok(val) if !val.trim().is_empty() => val,
                        _ => {
                            tracing::error!("FATAL: MAHO_RELAY_SERVICE_TOKEN is missing or empty!");
                            return (StatusCode::INTERNAL_SERVER_ERROR, "Configuration Error: Service token missing").into_response();
                        }
                    };

                    #[derive(Serialize)]
                    struct CreditsUpdateReq {
                        user_id: String,
                        new_balance_usd: f64,
                        delta_usd: f64,
                    }

                    let client = reqwest::Client::new();
                    if let Err(e) = client.post(format!("{}/billing/credits_update", proxy_url))
                        .header("Authorization", format!("Bearer {service_token}"))
                        .json(&CreditsUpdateReq {
                            user_id: user_id.to_string(),
                            new_balance_usd: new_balance,
                            delta_usd: -refund_amount_usd,
                        })
                        .send()
                        .await
                    {
                        tracing::error!("Failed to contact proxy for refund credits_update for user {}: {}", user_id, e);
                    }
                }
                Err(e) => {
                    tracing::error!("Failed to refund credit for user {}: {}", user_id, e);
                    return (StatusCode::INTERNAL_SERVER_ERROR, format!("Failed to refund credit: {e}")).into_response();
                }
            }
        }
    }

    // Save event ID to deduplicate in the future
    if let Err(e) = storage::mark_event_processed(&state.db, event_id, now_sec).await {
        return (StatusCode::INTERNAL_SERVER_ERROR, format!("Failed to record processed event: {e}")).into_response();
    }

    (StatusCode::OK, "Webhook processed successfully").into_response()
}

pub async fn portal_session(
    State(_state): State<Arc<AppState>>,
    auth: BearerAuth,
) -> Result<axum::Json<PortalSessionResponse>, StatusCode> {
    let user = auth.0.user;
    
    // Check if user has lemonsqueezy_customer_id
    let customer_id = match user.lemonsqueezy_customer_id {
        Some(id) if !id.trim().is_empty() => id,
        _ => {
            return Ok(axum::Json(PortalSessionResponse {
                url: "https://app.lemonsqueezy.com/my-orders".to_string(),
            }));
        }
    };

    let api_key = match std::env::var("LEMONSQUEEZY_API_KEY") {
        Ok(key) if !key.trim().is_empty() => key,
        _ => {
            tracing::warn!("LEMONSQUEEZY_API_KEY is not set; returning fallback billing portal URL");
            return Ok(axum::Json(PortalSessionResponse {
                url: "https://app.lemonsqueezy.com/my-orders".to_string(),
            }));
        }
    };

    let client = reqwest::Client::new();
    let response = client
        .get(format!("https://api.lemonsqueezy.com/v1/customers/{}", customer_id))
        .bearer_auth(api_key)
        .header("Accept", "application/vnd.api+json")
        .header("Content-Type", "application/vnd.api+json")
        .send()
        .await;

    match response {
        Ok(resp) if resp.status().is_success() => {
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
                return Ok(axum::Json(PortalSessionResponse {
                    url: ls_resp.data.attributes.urls.customer_portal,
                }));
            }
        }
        Ok(resp) => {
            tracing::error!("LemonSqueezy Customers API returned status {}", resp.status());
        }
        Err(e) => {
            tracing::error!("Failed to request LemonSqueezy Customers API: {}", e);
        }
    }

    Ok(axum::Json(PortalSessionResponse {
        url: "https://app.lemonsqueezy.com/my-orders".to_string(),
    }))
}

pub async fn invoices(
    State(_state): State<Arc<AppState>>,
    auth: BearerAuth,
) -> Result<axum::Json<InvoicesResponse>, StatusCode> {
    let user = auth.0.user;
    
    let api_key = match std::env::var("LEMONSQUEEZY_API_KEY") {
        Ok(key) if !key.trim().is_empty() => key,
        _ => {
            return Ok(axum::Json(InvoicesResponse { invoices: vec![] }));
        }
    };

    let client = reqwest::Client::new();
    let response = client
        .get("https://api.lemonsqueezy.com/v1/orders")
        .query(&[("filter[user_email]", &user.email)])
        .bearer_auth(api_key)
        .header("Accept", "application/vnd.api+json")
        .header("Content-Type", "application/vnd.api+json")
        .send()
        .await;

    match response {
        Ok(resp) if resp.status().is_success() => {
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
                let mut invoices = Vec::new();
                for order in ls_resp.data {
                    let created_at = chrono::DateTime::parse_from_rfc3339(&order.attributes.created_at)
                        .map(|dt| dt.timestamp())
                        .unwrap_or(0);
                    invoices.push(InvoiceEntry {
                        id: order.id,
                        created_at,
                        amount_usd: (order.attributes.total as f64) / 100.0,
                        currency: order.attributes.currency,
                        status: order.attributes.status,
                        pdf_url: order.attributes.urls.receipt.unwrap_or_default(),
                    });
                }
                return Ok(axum::Json(InvoicesResponse { invoices }));
            }
        }
        Ok(resp) => {
            tracing::error!("LemonSqueezy Orders API returned status {}", resp.status());
        }
        Err(e) => {
            tracing::error!("Failed to request LemonSqueezy Orders API: {}", e);
        }
    }

    Ok(axum::Json(InvoicesResponse { invoices: vec![] }))
}

pub fn router() -> axum::Router<Arc<AppState>> {
    axum::Router::new()
        .route("/lemonsqueezy", axum::routing::post(lemonsqueezy_webhook))
        .route("/portal_session", axum::routing::post(portal_session))
        .route("/invoices", axum::routing::get(invoices))
}
