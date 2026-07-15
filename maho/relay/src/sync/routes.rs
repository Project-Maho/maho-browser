use worker::*;
use serde::{Deserialize, Serialize};
use base64::Engine;
use crate::errors::{AppError, Result};
use crate::db;
use crate::auth::middleware::get_auth_context;

#[derive(Debug, Deserialize)]
pub struct UploadSnapshotRequest {
    pub blob: String,
    pub hlc_ts_ceiling: i64,
    pub creator_device_id: i32,
}

#[derive(Debug, Serialize)]
pub struct DownloadSnapshotResponse {
    pub hlc_ts_ceiling: i64,
    pub created_at: i64,
    pub creator_device_id: i32,
    pub blob: String,
}

const MAX_SNAPSHOT_BYTES: usize = 100 * 1024 * 1024;

pub async fn upload_snapshot(mut req: Request, env: Env) -> Result<Response> {
    let db = env.d1("MAHO_DB")?;
    let auth = get_auth_context(&req, &db).await?;
    let room_id = db::get_latest_room_for_user(&db, auth.user.id).await?
        .ok_or_else(|| AppError::BadRequest("no_room_for_user".to_string()))?;

    let payload: UploadSnapshotRequest = req.json().await
        .map_err(|e| AppError::BadRequest(format!("invalid_json: {e}")))?;

    let decoded = base64::engine::general_purpose::STANDARD
        .decode(payload.blob.as_bytes())
        .map_err(|_| AppError::BadRequest("invalid_base64_blob".to_string()))?;

    if decoded.len() > MAX_SNAPSHOT_BYTES {
        return Err(AppError::BadRequest("snapshot_too_large".to_string()));
    }

    let db = env.d1("MAHO_DB")?;
    let now = chrono::Utc::now().timestamp();

    let existing = db.prepare("SELECT hlc_ts_ceiling FROM snapshots WHERE room_id = ?1")
        .bind(&[db::js(&room_id)?])?
        .first::<serde_json::Value>(None).await?;

    if let Some(row) = existing {
        if let Some(existing_hlc) = row.get("hlc_ts_ceiling").and_then(|v| v.as_i64()) {
            if payload.hlc_ts_ceiling <= existing_hlc {
                return Err(AppError::Conflict("snapshot_not_newer".to_string()));
            }
        }
    }

    let stmt = db.prepare(
        "INSERT INTO snapshots (room_id, hlc_ts_ceiling, creator_device_id, blob, created_at)
         VALUES (?1, ?2, ?3, ?4, ?5)
         ON CONFLICT(room_id) DO UPDATE SET
            hlc_ts_ceiling = ?2,
            creator_device_id = ?3,
            blob = ?4,
            created_at = ?5"
    );
    let blob_js: wasm_bindgen::JsValue = serde_wasm_bindgen::to_value(&decoded)
        .map_err(|e| AppError::Internal(e.to_string()))?;
    let stmt = stmt.bind(&[
        db::js(&room_id)?,
        db::js(&payload.hlc_ts_ceiling)?,
        db::js(&payload.creator_device_id)?,
        blob_js,
        db::js(&now)?,
    ])?;
    stmt.run().await?;

    let _ = db::touch_session_and_device(&db, auth.session.id, auth.device.id, now).await;

    Response::ok("").map(|r| r.with_status(200)).map_err(AppError::from)
}

pub async fn latest_snapshot(req: Request, env: Env) -> Result<Response> {
    let db = env.d1("MAHO_DB")?;
    let auth = get_auth_context(&req, &db).await?;
    let room_id = match db::get_latest_room_for_user(&db, auth.user.id).await? {
        Some(r) => r,
        None => return Response::error("not_found", 404).map_err(AppError::from),
    };

    let db = env.d1("MAHO_DB")?;

    #[derive(Deserialize)]
    struct SnapshotRow {
        hlc_ts_ceiling: i64,
        created_at: i64,
        creator_device_id: i32,
        blob: Vec<u8>,
    }

    let row = db.prepare("SELECT hlc_ts_ceiling, created_at, creator_device_id, blob FROM snapshots WHERE room_id = ?1")
        .bind(&[room_id.into()])?
        .first::<SnapshotRow>(None).await?;

    match row {
        Some(snap) => {
            let encoded = base64::engine::general_purpose::STANDARD.encode(&snap.blob);
            let response = DownloadSnapshotResponse {
                hlc_ts_ceiling: snap.hlc_ts_ceiling,
                created_at: snap.created_at,
                creator_device_id: snap.creator_device_id,
                blob: encoded,
            };
            Response::from_json(&response).map_err(AppError::from)
        }
        None => {
            Response::error("not_found", 404).map_err(AppError::from)
        }
    }
}
