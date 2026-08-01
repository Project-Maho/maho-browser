use crate::auth::middleware::get_auth_context;
use crate::db;
use crate::errors::{AppError, Result};
use base64::Engine;
use serde::{Deserialize, Serialize};
use worker::*;

#[derive(Debug, Deserialize)]
pub struct UploadSnapshotRequest {
    pub blob: String,
    pub hlc_ts_ceiling: i64,
}

#[derive(Debug, Serialize)]
pub struct DownloadSnapshotResponse {
    pub hlc_ts_ceiling: i64,
    pub created_at: i64,
    pub creator_device_id: i32,
    pub blob: String,
}

pub async fn upload_snapshot(mut req: Request, env: Env) -> Result<Response> {
    let db = env.d1("MAHO_DB")?;
    let auth = get_auth_context(&req, &db).await?;
    let now = chrono::Utc::now().timestamp();

    // Snapshot room is the caller's device room, independent of any WebSocket
    // connection. Reject a room owned by another user; claim it if unowned.
    let room_id = auth.device.room_id.clone();
    match db::get_room_owner_user_id(&db, &room_id).await? {
        Some(owner) if owner != auth.user.id => {
            return Err(AppError::Unauthorized(
                "room_owned_by_another_user".to_string(),
            ));
        }
        Some(_) => {}
        None => {
            db::claim_room_ownership(&db, &room_id, auth.user.id, now).await?;
            // Re-verify fully fail-closed: after the ON CONFLICT DO NOTHING
            // claim, require the owner to be exactly this user (a concurrent
            // claim may have won; None should be unreachable but is rejected).
            if db::get_room_owner_user_id(&db, &room_id).await? != Some(auth.user.id) {
                return Err(AppError::Unauthorized(
                    "room_owned_by_another_user".to_string(),
                ));
            }
        }
    }

    let max_bytes = crate::config::max_snapshot_bytes(&env);

    let payload: UploadSnapshotRequest = req.json().await.map_err(|e| {
        console_error!("invalid_json detail (snapshot upload): {e}");
        AppError::BadRequest("invalid_json".to_string())
    })?;

    // Reject oversize uploads on the encoded length before allocating a decode
    // buffer, then confirm the exact decoded size.
    if crate::config::base64_exceeds_cap(payload.blob.len(), max_bytes) {
        return Err(AppError::BadRequest("snapshot_too_large".to_string()));
    }

    let decoded = base64::engine::general_purpose::STANDARD
        .decode(payload.blob.as_bytes())
        .map_err(|_| AppError::BadRequest("invalid_base64_blob".to_string()))?;

    if decoded.len() > max_bytes {
        return Err(AppError::BadRequest("snapshot_too_large".to_string()));
    }

    // Single conditional upsert: the newer-hlc guard is in the statement, so no
    // SELECT-then-write TOCTOU. Older/equal hlc returns no row -> snapshot_not_newer.
    let blob_js: wasm_bindgen::JsValue =
        serde_wasm_bindgen::to_value(&decoded).map_err(|e| AppError::Internal(e.to_string()))?;
    let stmt = db.prepare(
        "INSERT INTO snapshots (room_id, hlc_ts_ceiling, creator_device_id, blob, created_at)
         VALUES (?1, ?2, ?3, ?4, ?5)
         ON CONFLICT(room_id) DO UPDATE SET
            hlc_ts_ceiling = ?2,
            creator_device_id = ?3,
            blob = ?4,
            created_at = ?5
         WHERE excluded.hlc_ts_ceiling > snapshots.hlc_ts_ceiling
         RETURNING room_id",
    );
    let stmt = stmt.bind(&[
        db::js(&room_id)?,
        db::js(&payload.hlc_ts_ceiling)?,
        db::js(&auth.device.id)?,
        blob_js,
        db::js(&now)?,
    ])?;
    let applied = stmt.first::<serde_json::Value>(None).await?.is_some();
    if !applied {
        return Err(AppError::Conflict("snapshot_not_newer".to_string()));
    }

    let _ =
        db::touch_session_and_device(&db, auth.user.id, auth.session.id, auth.device.id, now).await;

    Response::ok("")
        .map(|r| r.with_status(200))
        .map_err(AppError::from)
}

pub async fn latest_snapshot(req: Request, env: Env) -> Result<Response> {
    let db = env.d1("MAHO_DB")?;
    let auth = get_auth_context(&req, &db).await?;
    let room_id = auth.device.room_id.clone();

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
        None => Response::error("not_found", 404).map_err(AppError::from),
    }
}
