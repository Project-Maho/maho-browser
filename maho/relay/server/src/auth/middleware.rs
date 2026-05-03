use std::sync::Arc;

use axum::extract::FromRequestParts;
use axum::http::{header, request::Parts, StatusCode};
use axum::response::{IntoResponse, Response};
use axum::Json;

use crate::auth::models::ErrorResponse;
use crate::auth::tokens::hash_token_str;
use crate::server::AppState;
use crate::storage::{self, AuthContext};

pub struct BearerAuth(pub AuthContext);

impl FromRequestParts<Arc<AppState>> for BearerAuth {
    type Rejection = Response;

    async fn from_request_parts(
        parts: &mut Parts,
        state: &Arc<AppState>,
    ) -> Result<Self, Self::Rejection> {
        let header_value = parts
            .headers
            .get(header::AUTHORIZATION)
            .and_then(|value| value.to_str().ok())
            .ok_or_else(|| unauthorized("missing authorization header"))?;

        let token = header_value
            .strip_prefix("Bearer ")
            .or_else(|| header_value.strip_prefix("bearer "))
            .map(str::trim)
            .filter(|token| !token.is_empty())
            .ok_or_else(|| unauthorized("invalid bearer token"))?;

        let token_hash = hash_token_str(token);
        let context = storage::find_active_session_context_by_access_hash(&state.db, token_hash)
            .await
            .map_err(|_| internal_error("failed to load session"))?
            .ok_or_else(|| unauthorized("invalid or expired session"))?;

        Ok(BearerAuth(context))
    }
}

fn unauthorized(message: &str) -> Response {
    (
        StatusCode::UNAUTHORIZED,
        Json(ErrorResponse {
            error: message.to_string(),
        }),
    )
        .into_response()
}

fn internal_error(message: &str) -> Response {
    (
        StatusCode::INTERNAL_SERVER_ERROR,
        Json(ErrorResponse {
            error: message.to_string(),
        }),
    )
        .into_response()
}
