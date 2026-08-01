use serde::Serialize;
use worker::*;

#[derive(Serialize)]
struct ErrorResponse<'a> {
    error: &'a str,
    #[serde(skip_serializing_if = "Option::is_none")]
    error_id: Option<&'a str>,
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
        let (status, msg) = self.status_and_message();

        // 5xx bodies must not leak internal detail (D1 errors can carry SQL,
        // table and column names). Replace with a fixed token plus a random
        // error_id echoed to the server log for correlation. 4xx keep their
        // meaningful strings, which are a client-facing contract.
        if status >= 500 {
            let error_id = short_error_id();
            console_error!("5xx error_id={error_id}: {msg}");
            let res = Response::from_json(&ErrorResponse {
                error: Self::public_error(status, &msg),
                error_id: Some(&error_id),
            })?;
            return Ok(res.with_status(status));
        }

        let mut res = Response::from_json(&ErrorResponse {
            error: Self::public_error(status, &msg),
            error_id: None,
        })?
        .with_status(status);
        if status == 429 {
            res.headers_mut().set("Retry-After", "60")?;
        }
        Ok(res)
    }

    fn status_and_message(&self) -> (u16, String) {
        match self {
            AppError::Worker(error) => (500, error.to_string()),
            AppError::Auth(message) => (401, message.clone()),
            AppError::Unauthorized(message) => (401, message.clone()),
            AppError::NotFound(message) => (404, message.clone()),
            AppError::BadRequest(message) => (400, message.clone()),
            AppError::RateLimit(message) => (429, message.clone()),
            AppError::Conflict(message) => (409, message.clone()),
            AppError::Internal(message) => (500, message.clone()),
        }
    }

    fn public_error(status: u16, message: &str) -> &str {
        if status >= 500 {
            "internal_error"
        } else {
            message
        }
    }
}

fn short_error_id() -> String {
    let mut bytes = [0u8; 4];
    if getrandom::getrandom(&mut bytes).is_err() {
        return "unknown".to_string();
    }
    bytes.iter().map(|b| format!("{b:02x}")).collect()
}

pub type Result<T> = std::result::Result<T, AppError>;

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn rate_limit_error_preserves_the_429_client_contract() {
        // Given: a real limiter denial reaches the route caller.
        let error = AppError::RateLimit("rate_limit_exceeded".to_string());

        // When: the HTTP error response is selected.
        let (status, message) = error.status_and_message();

        // Then: the caller retains its 429 status and client-facing message.
        assert_eq!(status, 429);
        assert_eq!(
            AppError::public_error(status, &message),
            "rate_limit_exceeded"
        );
    }

    #[test]
    fn unavailable_limiter_binding_is_redacted_as_internal_error() {
        // Given: a non-development binding failure is converted to an internal error.
        let error = AppError::Internal("rate limiter binding RL_SIGNUP unavailable".to_string());

        // When: the HTTP error response is selected.
        let (status, message) = error.status_and_message();

        // Then: infrastructure detail is never exposed to the client.
        assert_eq!(status, 500);
        assert_eq!(AppError::public_error(status, &message), "internal_error");
    }
}
