use serde::Serialize;
use worker::*;

#[derive(Serialize)]
struct ErrorResponse<'a> {
    error: &'a str,
}

#[derive(Debug)]
pub enum AppError {
    Worker(worker::Error),
    Auth(String),
    Unauthorized(String),
    NotFound(String),
    BadRequest(String),
    RateLimit(String),
    Conflict(String),
    Internal(String),
}

impl std::fmt::Display for AppError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            AppError::Worker(e) => write!(f, "worker: {e}"),
            AppError::Auth(m) => write!(f, "auth: {m}"),
            AppError::Unauthorized(m) => write!(f, "unauthorized: {m}"),
            AppError::NotFound(m) => write!(f, "not_found: {m}"),
            AppError::BadRequest(m) => write!(f, "bad_request: {m}"),
            AppError::RateLimit(m) => write!(f, "rate_limit: {m}"),
            AppError::Conflict(m) => write!(f, "conflict: {m}"),
            AppError::Internal(m) => write!(f, "internal: {m}"),
        }
    }
}

impl std::error::Error for AppError {}

impl From<worker::Error> for AppError {
    fn from(err: worker::Error) -> Self {
        AppError::Worker(err)
    }
}

impl From<crate::auth::passwords::PasswordError> for AppError {
    fn from(err: crate::auth::passwords::PasswordError) -> Self {
        AppError::Internal(err.to_string())
    }
}

impl AppError {
    pub fn to_response(&self) -> worker::Result<Response> {
        let (status, msg) = match self {
            AppError::Worker(e) => (500, e.to_string()),
            AppError::Auth(msg) => (401, msg.clone()),
            AppError::Unauthorized(msg) => (401, msg.clone()),
            AppError::NotFound(msg) => (404, msg.clone()),
            AppError::BadRequest(msg) => (400, msg.clone()),
            AppError::RateLimit(msg) => (429, msg.clone()),
            AppError::Conflict(msg) => (409, msg.clone()),
            AppError::Internal(msg) => (500, msg.clone()),
        };

        let res = Response::from_json(&ErrorResponse { error: &msg })?;
        Ok(res.with_status(status))
    }
}

pub type Result<T> = std::result::Result<T, AppError>;
