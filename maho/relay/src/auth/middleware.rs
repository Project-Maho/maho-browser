use worker::*;
use crate::errors::{AppError, Result};
use crate::db::{AuthContext, find_active_session_context_by_access_hash};
use crate::auth::tokens::hash_token_str;

pub async fn get_auth_context(req: &Request, db: &D1Database) -> Result<AuthContext> {
    let auth_header = req.headers().get("Authorization")?
        .ok_or_else(|| AppError::Auth("missing authorization header".to_string()))?;
        
    let token = auth_header
        .strip_prefix("Bearer ")
        .or_else(|| auth_header.strip_prefix("bearer "))
        .map(str::trim)
        .filter(|t| !t.is_empty())
        .ok_or_else(|| AppError::Auth("invalid bearer token".to_string()))?;
        
    let token_hash = hash_token_str(token);
    let context = find_active_session_context_by_access_hash(db, &token_hash).await?
        .ok_or_else(|| AppError::Auth("invalid or expired session".to_string()))?;
        
    Ok(context)
}
