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

use axum::http::StatusCode;
use super::models::{
    MAX_PULL_LIMIT, PullQuery, PullResponse, PulledMessage, PushAck, PushRequest, PushResponse,
    UploadSnapshotRequest, DownloadSnapshotResponse,
};

pub fn router() -> Router<Arc<AppState>> {
    Router::new()
        .route("/push", post(push))
        .route("/pull", get(pull))
        .route("/snapshots", post(upload_snapshot).get(download_snapshot).head(get_snapshot_metadata))
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

    fn conflict(message: impl Into<String>) -> Self {
        Self {
            status: axum::http::StatusCode::CONFLICT,
            message: message.into(),
        }
    }

    fn payload_too_large(message: impl Into<String>) -> Self {
        Self {
            status: axum::http::StatusCode::PAYLOAD_TOO_LARGE,
            message: message.into(),
        }
    }
}

async fn upload_snapshot(
    State(state): State<Arc<AppState>>,
    auth: BearerAuth,
    Json(payload): Json<UploadSnapshotRequest>,
) -> Result<StatusCode, SyncError> {
    let room_id = auth.0.device.room_id.clone();
    let decoded = base64::engine::general_purpose::STANDARD
        .decode(payload.blob.as_bytes())
        .map_err(|_| SyncError::bad_request("invalid base64 blob"))?;
        
    if decoded.len() > 100 * 1024 * 1024 {
        return Err(SyncError::payload_too_large("Snapshot exceeds 100MB limit"));
    }

    storage::save_snapshot(
        &state.db,
        &room_id,
        payload.hlc_ts_ceiling,
        payload.creator_device_id,
        decoded,
    )
    .await
    .map_err(|e| {
        if e.to_string().contains("Snapshot is not newer") {
            SyncError::conflict("Snapshot is not newer than existing")
        } else {
            SyncError::internal("failed to save snapshot")
        }
    })?;

    Ok(StatusCode::OK)
}

async fn download_snapshot(
    State(state): State<Arc<AppState>>,
    auth: BearerAuth,
) -> Result<Response, SyncError> {
    let room_id = auth.0.device.room_id.clone();
    let snap_opt = storage::get_snapshot(&state.db, &room_id)
        .await
        .map_err(|_| SyncError::internal("failed to load snapshot"))?;

    if let Some(snap) = snap_opt {
        let encoded = base64::engine::general_purpose::STANDARD.encode(snap.blob);
        let resp = DownloadSnapshotResponse {
            hlc_ts_ceiling: snap.hlc_ts_ceiling,
            created_at: snap.created_at,
            creator_device_id: snap.creator_device_id,
            blob: encoded,
        };
        Ok(Json(resp).into_response())
    } else {
        Ok(StatusCode::NOT_FOUND.into_response())
    }
}

async fn get_snapshot_metadata(
    State(state): State<Arc<AppState>>,
    auth: BearerAuth,
) -> Result<Response, SyncError> {
    let room_id = auth.0.device.room_id.clone();
    let snap_opt = storage::get_snapshot(&state.db, &room_id)
        .await
        .map_err(|_| SyncError::internal("failed to load snapshot"))?;

    if let Some(snap) = snap_opt {
        let response = Response::builder()
            .status(StatusCode::OK)
            .header("X-Hlc-Ts-Ceiling", snap.hlc_ts_ceiling.to_string())
            .header("X-Created-At", snap.created_at.to_string())
            .header("X-Creator-Device-Id", snap.creator_device_id.to_string())
            .body(axum::body::Body::empty())
            .map_err(|_| SyncError::internal("failed to construct response"))?;
        Ok(response)
    } else {
        Ok(StatusCode::NOT_FOUND.into_response())
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
