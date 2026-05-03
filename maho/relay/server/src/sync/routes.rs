use std::sync::Arc;

use axum::extract::{Query, State};
use axum::response::{IntoResponse, Response};
use axum::routing::{get, post};
use axum::{Json, Router};
use base64::Engine;
use crate::auth::middleware::BearerAuth;
use crate::auth::models::ErrorResponse;
use crate::server::AppState;
use crate::storage;

use super::models::{MAX_PULL_LIMIT, PullQuery, PullResponse, PulledMessage, PushAck, PushRequest, PushResponse};

pub fn router() -> Router<Arc<AppState>> {
    Router::new()
        .route("/push", post(push))
        .route("/pull", get(pull))
}

async fn push(
    State(state): State<Arc<AppState>>,
    auth: BearerAuth,
    Json(payload): Json<PushRequest>,
) -> Result<Json<PushResponse>, SyncError> {
    let room_id = auth.0.device.room_id.clone();
    let mut acks = Vec::with_capacity(payload.blobs.len());

    for blob in payload.blobs {
        let decoded = base64::engine::general_purpose::STANDARD
            .decode(blob.as_bytes())
            .map_err(|_| SyncError::bad_request("invalid base64 blob"))?;
        let seq = storage::store_message(&state.db, &room_id, decoded)
            .await
            .map_err(|_| SyncError::internal("failed to store message"))?;
        acks.push(PushAck { seq });
    }

    storage::touch_session_and_device(&state.db, auth.0.session.id, auth.0.device.id)
        .await
        .map_err(|_| SyncError::internal("failed to update sync activity"))?;

    Ok(Json(PushResponse { acks }))
}

async fn pull(
    State(state): State<Arc<AppState>>,
    auth: BearerAuth,
    Query(query): Query<PullQuery>,
) -> Result<Json<PullResponse>, SyncError> {
    let room_id = auth.0.device.room_id.clone();
    let limit = query.limit.min(MAX_PULL_LIMIT);
    let since_seq = query.since_seq;

    let mut messages = storage::get_messages_since(&state.db, &room_id, since_seq)
        .await
        .map_err(|_| SyncError::internal("failed to load messages"))?;

    let has_more = messages.len() > limit;
    if messages.len() > limit {
        messages.truncate(limit);
    }

    let messages = messages
        .into_iter()
        .map(|message| PulledMessage {
            seq: message.seq,
            blob: base64::engine::general_purpose::STANDARD.encode(message.blob),
        })
        .collect();

    storage::touch_session_and_device(&state.db, auth.0.session.id, auth.0.device.id)
        .await
        .map_err(|_| SyncError::internal("failed to update sync activity"))?;

    Ok(Json(PullResponse { messages, has_more }))
}

#[derive(Debug)]
struct SyncError {
    status: axum::http::StatusCode,
    message: String,
}

impl SyncError {
    fn bad_request(message: impl Into<String>) -> Self {
        Self {
            status: axum::http::StatusCode::BAD_REQUEST,
            message: message.into(),
        }
    }

    fn internal(message: impl Into<String>) -> Self {
        Self {
            status: axum::http::StatusCode::INTERNAL_SERVER_ERROR,
            message: message.into(),
        }
    }
}

impl IntoResponse for SyncError {
    fn into_response(self) -> Response {
        (
            self.status,
            Json(ErrorResponse {
                error: self.message,
            }),
        )
            .into_response()
    }
}
