use serde::{Deserialize, Serialize};

#[derive(Debug, Deserialize)]
pub struct SignupRequest {
    pub email: String,
    pub password: String,
    #[serde(default)]
    pub display_name: Option<String>,
    #[serde(default)]
    pub device_name: Option<String>,
    #[serde(default)]
    pub device_type: Option<String>,
}

#[derive(Debug, Deserialize)]
pub struct LoginRequest {
    pub email: String,
    pub password: String,
    #[serde(default)]
    pub device_name: Option<String>,
    #[serde(default)]
    pub device_type: Option<String>,
}

#[derive(Debug, Deserialize)]
pub struct RefreshRequest {
    pub refresh_token: String,
}

#[derive(Debug, Serialize)]
pub struct AuthResponse {
    pub access_token: String,
    pub refresh_token: String,
    pub token_type: &'static str,
    pub expires_at: i64,
    pub refresh_expires_at: i64,
    pub account: PublicUser,
    pub device: PublicDevice,
    pub session: PublicSession,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct PublicUser {
    pub id: i32,
    pub email: String,
    pub display_name: String,
    pub created_at: i64,
    pub locked_until: Option<i64>,
    pub failed_login_attempts: i32,
    pub last_failed_login_at: Option<i64>,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct PublicDevice {
    pub id: i32,
    pub user_id: i32,
    pub name: String,
    pub device_type: String,
    pub room_id: String,
    pub created_at: i64,
    pub last_seen: Option<i64>,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct PublicSession {
    pub id: i32,
    pub user_id: i32,
    pub device_id: i32,
    pub expires_at: i64,
    pub refresh_expires_at: i64,
    pub last_used_at: Option<i64>,
    pub created_at: i64,
}

#[derive(Debug, Serialize)]
pub struct MeResponse {
    pub account: PublicUser,
    pub devices: Vec<PublicDevice>,
    pub current_device: PublicDevice,
    pub current_session: PublicSession,
}

#[derive(Debug, Serialize)]
pub struct LogoutResponse {
    pub ok: bool,
}

#[derive(Debug, Serialize)]
pub struct DeleteDeviceResponse {
    pub ok: bool,
}

#[derive(Debug, Serialize)]
pub struct ErrorResponse {
    pub error: String,
}

pub fn normalize_email(email: &str) -> Option<String> {
    let normalized = email.trim().to_ascii_lowercase();
    if normalized.is_empty() {
        None
    } else {
        Some(normalized)
    }
}

pub fn normalize_display_name(display_name: Option<&str>, email: &str) -> String {
    let candidate = display_name.unwrap_or("").trim();
    if !candidate.is_empty() {
        candidate.to_string()
    } else {
        let prefix = email.split('@').next().unwrap_or("").trim();
        if prefix.is_empty() {
            "New user".to_string()
        } else {
            prefix.to_string()
        }
    }
}

pub fn normalize_device_name(device_name: Option<&str>) -> String {
    let candidate = device_name.unwrap_or("").trim();
    if candidate.is_empty() {
        "Default device".to_string()
    } else {
        candidate.to_string()
    }
}

pub fn normalize_device_type(device_type: Option<&str>) -> String {
    let candidate = device_type.unwrap_or("").trim();
    if candidate.is_empty() {
        "unknown".to_string()
    } else {
        candidate.to_string()
    }
}

#[derive(Debug, Deserialize)]
pub struct IntrospectRequest {
    pub token: String,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct IntrospectResponse {
    pub user_id: i32,
    pub tier: String,
    pub subscription_status: String,
    pub email_verified: bool,
    pub expires_at: i64,
    pub period_start_at: Option<i64>,
}

#[derive(Debug, Deserialize)]
pub struct ConsumeBypassRequest {
    pub token: String,
}

#[derive(Debug, Serialize)]
pub struct ConsumeBypassResponse {
    pub user_id: i32,
}

#[derive(Debug, Deserialize)]
pub struct VerifyRequest {
    pub token: String,
}

#[derive(Debug, Serialize)]
pub struct VerifyResponse {
    pub status: String,
    pub verify_bypass_token: String,
}

#[derive(Debug, Serialize)]
pub struct RequestVerifyResponse {
    pub status: String,
    pub dev_token: Option<String>,
}

#[derive(Debug, Deserialize)]
pub struct AdminForceVerifyEmailRequest {
    pub user_id: i32,
    pub reason: String,
}

#[derive(Debug, Serialize)]
pub struct AdminForceVerifyEmailResponse {
    pub user_id: i32,
    pub email_verified: bool,
}

#[derive(Debug, Deserialize)]
pub struct AdminSetCreditBalanceRequest {
    pub user_id: i32,
    pub new_balance_usd: f64,
    pub reason: String,
}

#[derive(Debug, Serialize)]
pub struct AdminSetCreditBalanceResponse {
    pub user_id: i32,
    pub new_balance_usd: f64,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct SubscriptionResponse {
    pub tier: String,
    pub subscription_status: String,
    pub subscription_expires_at: Option<i64>,
    pub period_start_at: Option<i64>,
    pub anniversary_reset_day: Option<i32>,
    pub tier_ceiling_usd: f64,
    pub credit_balance_usd: f64,
    pub lifetime_purchased_usd: f64,
    pub has_payment_method: bool,
}
