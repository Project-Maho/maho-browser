use std::sync::Arc;

use axum::body::Body;
use axum::extract::{Path, Query, State};
use axum::http::{header, StatusCode};
use axum::response::{IntoResponse, Response};
use axum::routing::get;
use axum::{Json, Router};
use serde::{Deserialize, Serialize};
use tokio::fs;
use tokio_util::io::ReaderStream;

use crate::server::AppState;
use crate::updates::appcast;
use crate::updates::releases::{self, Platform, ReleaseError, ReleaseManifest};

pub fn router() -> Router<Arc<AppState>> {
    Router::new()
        .route("/macos/appcast", get(macos_appcast))
        .route("/macos/{channel}/{filename}", get(serve_binary_macos))
        .route("/windows/check", get(windows_check))
        .route("/windows/{channel}/{filename}", get(serve_binary_windows))
        .route("/linux/check", get(linux_check))
        .route("/linux/{channel}/{filename}", get(serve_binary_linux))
}

#[derive(Debug, Deserialize)]
pub struct AppcastQuery {
    pub channel: String,
    pub b: Option<u32>,
}

#[derive(Debug, Deserialize)]
pub struct ChannelQuery {
    pub channel: String,
}

#[derive(Debug, Serialize)]
pub struct SignedEnvelope {
    pub signature: String,
    pub payload: String,
}

#[derive(Debug, Serialize)]
struct WindowsPayload {
    version: String,
    channel: String,
    installer_url: String,
    installer_sha256: String,
    installer_signer_cn: String,
    rollout_bucket: u32,
    release_date: String,
}

#[derive(Debug, Serialize)]
struct LinuxPayload {
    version: String,
    channel: String,
    release_date: String,
}

async fn macos_appcast(
    State(state): State<Arc<AppState>>,
    Query(query): Query<AppcastQuery>,
) -> Result<Response, UpdateError> {
    let updates = require_updates(&state)?;
    if !releases::is_valid_channel(&query.channel) {
        return Err(UpdateError::not_found("unknown channel"));
    }

    let manifest = match updates.releases.load(Platform::Macos, &query.channel).await {
        Ok(m) => Some(m),
        Err(ReleaseError::NotFound) => None,
        Err(e) => return Err(UpdateError::internal(e.to_string())),
    };

    let item = match (&manifest, query.b) {
        (Some(m), Some(bucket)) if bucket >= m.rollout_bucket => None,
        (Some(m), _) => Some(m),
        (None, _) => None,
    };

    let xml = appcast::build_appcast_xml(&query.channel, &updates.public_base_url, item);

    Ok((
        StatusCode::OK,
        [(header::CONTENT_TYPE, "application/rss+xml; charset=utf-8")],
        xml,
    )
        .into_response())
}

async fn windows_check(
    State(state): State<Arc<AppState>>,
    Query(query): Query<ChannelQuery>,
) -> Result<Json<SignedEnvelope>, UpdateError> {
    let updates = require_updates(&state)?;
    if !releases::is_valid_channel(&query.channel) {
        return Err(UpdateError::not_found("unknown channel"));
    }
    let manifest = updates
        .releases
        .load(Platform::Windows, &query.channel)
        .await
        .map_err(map_release_error)?;

    let installer_url = format!(
        "{}/updates/windows/{}/{}",
        updates.public_base_url, manifest.channel, manifest.binary_filename
    );
    let payload = WindowsPayload {
        version: manifest.version.clone(),
        channel: manifest.channel.clone(),
        installer_url,
        installer_sha256: manifest.binary_sha256.clone(),
        installer_signer_cn: manifest.installer_signer_cn.clone(),
        rollout_bucket: manifest.rollout_bucket,
        release_date: manifest.release_date.clone(),
    };
    Ok(Json(seal(&payload, &updates.signer)))
}

async fn linux_check(
    State(state): State<Arc<AppState>>,
    Query(query): Query<ChannelQuery>,
) -> Result<Json<SignedEnvelope>, UpdateError> {
    let updates = require_updates(&state)?;
    if !releases::is_valid_channel(&query.channel) {
        return Err(UpdateError::not_found("unknown channel"));
    }
    let manifest = updates
        .releases
        .load(Platform::Linux, &query.channel)
        .await
        .map_err(map_release_error)?;

    let payload = LinuxPayload {
        version: manifest.version,
        channel: manifest.channel,
        release_date: manifest.release_date,
    };
    Ok(Json(seal(&payload, &updates.signer)))
}

fn seal<T: Serialize>(payload: &T, signer: &crate::updates::signer::UpdateSigner) -> SignedEnvelope {
    let payload_str = serde_json::to_string(payload).expect("payload serialization");
    let signature = signer.sign_hex(payload_str.as_bytes());
    SignedEnvelope {
        signature,
        payload: payload_str,
    }
}

async fn serve_binary_macos(
    State(state): State<Arc<AppState>>,
    Path((channel, filename)): Path<(String, String)>,
) -> Result<Response, UpdateError> {
    serve_binary(state, Platform::Macos, &channel, &filename).await
}

async fn serve_binary_windows(
    State(state): State<Arc<AppState>>,
    Path((channel, filename)): Path<(String, String)>,
) -> Result<Response, UpdateError> {
    serve_binary(state, Platform::Windows, &channel, &filename).await
}

async fn serve_binary_linux(
    State(state): State<Arc<AppState>>,
    Path((channel, filename)): Path<(String, String)>,
) -> Result<Response, UpdateError> {
    serve_binary(state, Platform::Linux, &channel, &filename).await
}

async fn serve_binary(
    state: Arc<AppState>,
    platform: Platform,
    channel: &str,
    filename: &str,
) -> Result<Response, UpdateError> {
    let updates = require_updates(&state)?;
    if !releases::is_valid_channel(channel) {
        return Err(UpdateError::not_found("unknown channel"));
    }
    if !is_safe_filename(filename) {
        return Err(UpdateError::not_found("invalid filename"));
    }

    let manifest: ReleaseManifest = updates
        .releases
        .load(platform, channel)
        .await
        .map_err(map_release_error)?;

    if manifest.binary_filename != filename {
        return Err(UpdateError::not_found("filename does not match current release"));
    }

    let path = updates.releases.binary_path(platform, channel, filename);
    let file = fs::File::open(&path).await.map_err(|e| {
        if e.kind() == std::io::ErrorKind::NotFound {
            UpdateError::not_found("binary missing on disk")
        } else {
            UpdateError::internal(e.to_string())
        }
    })?;
    let metadata = file.metadata().await.map_err(|e| UpdateError::internal(e.to_string()))?;
    let size = metadata.len();

    let stream = ReaderStream::new(file);
    let body = Body::from_stream(stream);

    Ok((
        StatusCode::OK,
        [
            (header::CONTENT_TYPE, "application/octet-stream".to_string()),
            (header::CONTENT_LENGTH, size.to_string()),
        ],
        body,
    )
        .into_response())
}

fn is_safe_filename(name: &str) -> bool {
    !name.is_empty()
        && !name.contains('/')
        && !name.contains('\\')
        && !name.contains("..")
        && name.len() < 256
}

fn map_release_error(e: ReleaseError) -> UpdateError {
    match e {
        ReleaseError::NotFound => UpdateError::not_found("no release for channel"),
        ReleaseError::Parse(err) => UpdateError::internal(format!("parse: {err}")),
        ReleaseError::Io(err) => UpdateError::internal(format!("io: {err}")),
    }
}

fn require_updates(state: &AppState) -> Result<&crate::server::UpdatesState, UpdateError> {
    state
        .updates
        .as_ref()
        .ok_or_else(UpdateError::not_configured)
}

#[derive(Debug)]
pub struct UpdateError {
    status: StatusCode,
    message: String,
}

impl UpdateError {
    fn not_found(msg: impl Into<String>) -> Self {
        Self {
            status: StatusCode::NOT_FOUND,
            message: msg.into(),
        }
    }
    fn internal(msg: impl Into<String>) -> Self {
        Self {
            status: StatusCode::INTERNAL_SERVER_ERROR,
            message: msg.into(),
        }
    }
    fn not_configured() -> Self {
        Self {
            status: StatusCode::SERVICE_UNAVAILABLE,
            message: "updates subsystem is not configured on this relay".into(),
        }
    }
}

#[derive(Serialize)]
struct ErrorBody {
    error: String,
}

impl IntoResponse for UpdateError {
    fn into_response(self) -> Response {
        (
            self.status,
            Json(ErrorBody {
                error: self.message,
            }),
        )
            .into_response()
    }
}
