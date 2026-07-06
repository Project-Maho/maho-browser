use std::net::SocketAddr;
use std::sync::Arc;

use axum::extract::{ConnectInfo, State};
use axum::http::StatusCode;
use axum::response::{IntoResponse, Response};
use axum::routing::post;
use axum::{Json, Router};
use maho_relay_entity::waitlist_signups;
use sea_orm::ActiveValue::Set;
use sea_orm::{ActiveModelTrait, DbErr, SqlErr};
use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};

use crate::server::AppState;

const VALID_SOURCES: &[&str] = &["browser", "mail", "general"];
const WAITLIST_RATE_LIMIT_PER_MINUTE: u32 = 5;

pub fn router() -> Router<Arc<AppState>> {
    Router::new()
        .route("/", post(create_waitlist))
        .layer(tower_http::cors::CorsLayer::permissive())
}

#[derive(Debug, Deserialize)]
pub struct CreateWaitlistRequest {
    pub email: String,
    pub source: Option<String>,
    pub metadata: Option<serde_json::Value>,
}

#[derive(Debug, Serialize)]
pub struct CreateWaitlistResponse {
    pub ok: bool,
    pub already_signed_up: bool,
}

#[derive(Debug, Serialize)]
struct ErrorResponse {
    error: String,
}

async fn create_waitlist(
    State(state): State<Arc<AppState>>,
    ConnectInfo(addr): ConnectInfo<SocketAddr>,
    Json(payload): Json<CreateWaitlistRequest>,
) -> Response {
    // Rate limit: 5 signups per IP per minute
    if !state.waitlist_rate_limiter.allow(addr.ip()) {
        return (
            StatusCode::TOO_MANY_REQUESTS,
            Json(ErrorResponse {
                error: "rate_limit_exceeded".to_string(),
            }),
        )
            .into_response();
    }

    // Validate email
    let email = payload.email.trim().to_lowercase();
    if email.is_empty() {
        return (
            StatusCode::BAD_REQUEST,
            Json(ErrorResponse {
                error: "missing_email".to_string(),
            }),
        )
            .into_response();
    }
    if email.len() > 254 || !email.contains('@') || email.len() < 3 {
        return (
            StatusCode::BAD_REQUEST,
            Json(ErrorResponse {
                error: "invalid_email".to_string(),
            }),
        )
            .into_response();
    }

    // Validate source
    let source = payload.source.as_deref().unwrap_or("general").to_string();
    if !VALID_SOURCES.contains(&source.as_str()) {
        return (
            StatusCode::BAD_REQUEST,
            Json(ErrorResponse {
                error: "invalid_source".to_string(),
            }),
        )
            .into_response();
    }

    // Hash the IP address
    let ip_hash = {
        let mut hasher = Sha256::new();
        hasher.update(addr.ip().to_string().as_bytes());
        format!("{:x}", hasher.finalize())
    };

    let metadata_str = match payload.metadata {
        Some(ref v) => {
            let s = serde_json::to_string(v).unwrap_or_default();
            if s.len() > 4096 {
                return (
                    StatusCode::BAD_REQUEST,
                    Json(ErrorResponse {
                        error: "metadata_too_large".to_string(),
                    }),
                )
                    .into_response();
            }
            Some(s)
        }
        None => None,
    };

    let now = chrono::Utc::now().timestamp();

    let active_model = waitlist_signups::ActiveModel {
        email: Set(email),
        source: Set(source),
        ip_hash: Set(ip_hash),
        created_at: Set(now),
        metadata: Set(metadata_str),
        ..Default::default()
    };

    match active_model.insert(&state.db).await {
        Ok(_) => (
            StatusCode::OK,
            Json(CreateWaitlistResponse {
                ok: true,
                already_signed_up: false,
            }),
        )
            .into_response(),
        Err(err) => {
            if matches!(err.sql_err(), Some(SqlErr::UniqueConstraintViolation(_))) {
                return (
                    StatusCode::OK,
                    Json(CreateWaitlistResponse {
                        ok: true,
                        already_signed_up: true,
                    }),
                )
                    .into_response();
            }
            if let DbErr::Query(ref re) | DbErr::Exec(ref re) = err {
                let s = re.to_string();
                if s.contains("UNIQUE constraint failed")
                    || s.contains("unique constraint")
                    || s.contains("2067")
                    || s.contains("1555")
                {
                    return (
                        StatusCode::OK,
                        Json(CreateWaitlistResponse {
                            ok: true,
                            already_signed_up: true,
                        }),
                    )
                        .into_response();
                }
            }
            tracing::error!("waitlist DB error: {}", err);
            (
                StatusCode::INTERNAL_SERVER_ERROR,
                Json(ErrorResponse {
                    error: "internal_error".to_string(),
                }),
            )
                .into_response()
        }
    }
}

pub const RATE_LIMIT_PER_MINUTE: u32 = WAITLIST_RATE_LIMIT_PER_MINUTE;
