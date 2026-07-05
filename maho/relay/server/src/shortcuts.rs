use std::sync::Arc;
use axum::extract::State;
use axum::response::{IntoResponse, Response};
use axum::routing::get;
use axum::{Json, Router};
use crate::auth::middleware::BearerAuth;
use crate::auth::models::ErrorResponse;
use crate::server::AppState;
use sea_orm::{ConnectionTrait, Statement};

pub fn router() -> Router<Arc<AppState>> {
    Router::new()
        .route("/", get(get_shortcuts).put(put_shortcuts))
}

#[derive(serde::Deserialize)]
pub struct PutShortcutsRequest {
    pub json_data: String,
}

#[derive(serde::Serialize)]
pub struct GetShortcutsResponse {
    pub json_data: String,
}

#[derive(serde::Deserialize, serde::Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ShortcutBindingServer {
    pub action: String,
    pub key_combo: serde_json::Value,
    pub enabled: bool,
    pub updated_at: u64,
}

async fn get_shortcuts(
    State(state): State<Arc<AppState>>,
    auth: BearerAuth,
) -> Result<Json<GetShortcutsResponse>, ShortcutsError> {
    let room_id = auth.0.device.room_id.clone();
    let rows = state.db.query_all(Statement::from_sql_and_values(
        sea_orm::DatabaseBackend::Sqlite,
        "SELECT action, key_combo_json, enabled, updated_at FROM shortcuts WHERE room_id = ?1",
        vec![room_id.clone().into()],
    ))
    .await
    .map_err(|e| ShortcutsError::internal(e.to_string()))?;

    let mut shortcuts = Vec::new();
    for row in rows {
        let action: String = row.try_get("", "action").unwrap_or_default();
        let key_combo_json: String = row.try_get("", "key_combo_json").unwrap_or_default();
        let enabled: i32 = row.try_get("", "enabled").unwrap_or(1);
        let updated_at: i64 = row.try_get("", "updated_at").unwrap_or(0);
        
        let key_combo: serde_json::Value = serde_json::from_str(&key_combo_json).unwrap_or(serde_json::Value::Null);

        shortcuts.push(serde_json::json!({
            "action": action,
            "keyCombo": key_combo,
            "isCustom": true,
            "enabled": enabled != 0,
            "updated_at": updated_at as u64
        }));
    }

    let envelope = serde_json::json!({
        "version": 1,
        "platform": "all",
        "exportedAt": chrono::Utc::now().to_rfc3339(),
        "shortcuts": shortcuts
    });
    
    Ok(Json(GetShortcutsResponse {
        json_data: envelope.to_string(),
    }))
}

async fn put_shortcuts(
    State(state): State<Arc<AppState>>,
    auth: BearerAuth,
    Json(payload): Json<PutShortcutsRequest>,
) -> Result<impl IntoResponse, ShortcutsError> {
    let room_id = auth.0.device.room_id.clone();
    let value: serde_json::Value = serde_json::from_str(&payload.json_data)
        .map_err(|e| ShortcutsError::internal(e.to_string()))?;
    
    let bindings: Vec<ShortcutBindingServer> = if value.is_array() {
        serde_json::from_value(value).map_err(|e| ShortcutsError::internal(e.to_string()))?
    } else if let Some(shortcuts_val) = value.get("shortcuts") {
        serde_json::from_value(shortcuts_val.clone()).map_err(|e| ShortcutsError::internal(e.to_string()))?
    } else {
        return Err(ShortcutsError::internal("Invalid shortcuts payload format"));
    };

    for b in bindings {
        state.db.execute(Statement::from_sql_and_values(
            sea_orm::DatabaseBackend::Sqlite,
            "INSERT INTO shortcuts (room_id, action, key_combo_json, enabled, updated_at) \
             VALUES (?1, ?2, ?3, ?4, ?5) \
             ON CONFLICT(room_id, action) DO UPDATE SET \
             key_combo_json = case when excluded.updated_at > shortcuts.updated_at then excluded.key_combo_json else shortcuts.key_combo_json end, \
             enabled = case when excluded.updated_at > shortcuts.updated_at then excluded.enabled else shortcuts.enabled end, \
             updated_at = case when excluded.updated_at > shortcuts.updated_at then excluded.updated_at else shortcuts.updated_at end",
            vec![
                room_id.clone().into(),
                b.action.clone().into(),
                b.key_combo.to_string().into(),
                (b.enabled as i32).into(),
                (b.updated_at as i64).into(),
            ],
        ))
        .await
        .map_err(|e| ShortcutsError::internal(e.to_string()))?;
    }

    Ok(axum::http::StatusCode::OK)
}

#[derive(Debug)]
struct ShortcutsError {
    status: axum::http::StatusCode,
    message: String,
}

impl ShortcutsError {
    fn internal(message: impl Into<String>) -> Self {
        Self {
            status: axum::http::StatusCode::INTERNAL_SERVER_ERROR,
            message: message.into(),
        }
    }
}

impl IntoResponse for ShortcutsError {
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
