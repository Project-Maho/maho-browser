use crate::auth::tokens::hash_token_str;
use crate::db::{find_active_session_context_by_access_hash, AuthContext};
use crate::errors::{AppError, Result};
use worker::*;

pub async fn get_auth_context(req: &Request, db: &D1Database) -> Result<AuthContext> {
    let token = crate::auth::routes::extract_bearer(req)?;
    if token.is_empty() {
        return Err(AppError::Auth("invalid bearer token".to_string()));
    }

    let token_hash = hash_token_str(&token);
    let context = find_active_session_context_by_access_hash(db, &token_hash)
        .await?
        .ok_or_else(|| AppError::Auth("invalid or expired session".to_string()))?;

    Ok(context)
}
