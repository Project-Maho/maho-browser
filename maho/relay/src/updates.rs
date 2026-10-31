use crate::db::{self, ConfigUpdate, ReleaseUpdate};
use crate::errors::{AppError, Result};
use base64::Engine;
use ed25519_dalek::{Signature, Verifier, VerifyingKey};
use worker::*;

const CONFIG_UPDATE_ADMIN_TOKEN_HEADER: &str = "X-Config-Update-Admin-Token";
const CONFIG_UPDATE_ADMIN_TOKEN_SECRET: &str = "CONFIG_UPDATE_ADMIN_TOKEN";
const CONFIG_UPDATE_PUBLIC_KEY_VAR: &str = "CONFIG_UPDATE_PUBLIC_KEY";

const RELEASE_ADMIN_TOKEN_HEADER: &str = "X-Release-Admin-Token";
const RELEASE_ADMIN_TOKEN_SECRET: &str = "RELEASE_ADMIN_TOKEN";

const CACHE_CONTROL_HEADER: &str = "public, max-age=60, s-maxage=300, stale-while-revalidate=60";

pub const EMPTY_APPCAST_XML: &str = r#"<?xml version="1.0" encoding="utf-8"?>
<rss version="2.0" xmlns:sparkle="http://www.andymatuschak.org/xml-namespaces/sparkle" xmlns:dc="http://purl.org/dc/elements/1.1/">
  <channel>
    <title>Maho Updates</title>
    <link>https://maho.app</link>
    <description>Maho Updates</description>
    <language>en</language>
  </channel>
</rss>"#;

/// Fail-closed rollout bucket sentinel: strictly greater than any valid
/// `rollout_threshold` (a 0..=100 percentage), so it never satisfies
/// `bucket < rollout_threshold`. Clients compute buckets in `0..=99` (see
/// `maho-chromium/browser/updates/rollout_bucket.h`).
const ROLLOUT_BUCKET_SENTINEL: i32 = 100;

/// Fails closed to [`ROLLOUT_BUCKET_SENTINEL`] for any `b` outside `0..=99`
/// (missing, unparseable, negative, or `>= 100`), preventing a crafted
/// request such as `b=-1` from always receiving the canary configuration.
fn parse_rollout_bucket(raw: Option<&str>) -> i32 {
    match raw.and_then(|s| s.parse::<i32>().ok()) {
        Some(bucket) if (0..ROLLOUT_BUCKET_SENTINEL).contains(&bucket) => bucket,
        _ => ROLLOUT_BUCKET_SENTINEL,
    }
}

pub async fn get_config(req: Request, env: Env) -> Result<Response> {
    let db = env.d1("MAHO_DB")?;
    let url = req.url()?;
    let query: std::collections::HashMap<String, String> = url.query_pairs().into_owned().collect();

    let channel = query
        .get("channel")
        .cloned()
        .unwrap_or_else(|| "stable".to_string());
    let bucket = parse_rollout_bucket(query.get("b").map(String::as_str));

    match db::find_latest_config_update(&db, &channel, bucket).await? {
        Some(config) => Ok(Response::from_json(&config)?),
        None => Err(AppError::NotFound("no matching configuration found".into())),
    }
}

pub async fn post_config(mut req: Request, env: Env) -> Result<Response> {
    require_config_update_admin(&req, &env)?;

    let db = env.d1("MAHO_DB")?;
    let update: ConfigUpdate = req.json().await?;

    let pubkey_str = env
        .var(CONFIG_UPDATE_PUBLIC_KEY_VAR)
        .map(|v| v.to_string())
        .map_err(|_| AppError::Internal("config update public key not configured".to_string()))?;

    verify_config_signature(&pubkey_str, &update.signature, &update.payload)?;

    db::insert_config_update(&db, &update).await?;

    Ok(Response::ok("config updated")?)
}

pub async fn get_macos_appcast(req: Request, env: Env) -> Result<Response> {
    let db = env.d1("MAHO_DB")?;
    let url = req.url()?;
    let query: std::collections::HashMap<String, String> = url.query_pairs().into_owned().collect();

    let channel = query
        .get("channel")
        .cloned()
        .unwrap_or_else(|| "stable".to_string());
    let bucket = parse_rollout_bucket(query.get("b").map(String::as_str));

    let body = match db::find_latest_macos_release(&db, &channel, bucket).await? {
        Some(release) => match release.appcast_xml {
            Some(xml) if !xml.trim().is_empty() => xml,
            _ => EMPTY_APPCAST_XML.to_string(),
        },
        None => EMPTY_APPCAST_XML.to_string(),
    };

    let mut res = Response::ok(body)?;
    res.headers_mut()
        .set("Content-Type", "application/xml; charset=utf-8")?;
    res.headers_mut()
        .set("Cache-Control", CACHE_CONTROL_HEADER)?;
    Ok(res)
}

pub async fn get_windows_check(req: Request, env: Env) -> Result<Response> {
    let db = env.d1("MAHO_DB")?;
    let url = req.url()?;
    let query: std::collections::HashMap<String, String> = url.query_pairs().into_owned().collect();

    let channel = query
        .get("channel")
        .cloned()
        .unwrap_or_else(|| "stable".to_string());

    let body = match db::find_latest_windows_release(&db, &channel).await? {
        Some(release) => match release.win_envelope {
            Some(envelope) if !envelope.trim().is_empty() => envelope,
            _ => "{}".to_string(),
        },
        None => "{}".to_string(),
    };

    let mut res = Response::ok(body)?;
    res.headers_mut()
        .set("Content-Type", "application/json")?;
    res.headers_mut()
        .set("Cache-Control", CACHE_CONTROL_HEADER)?;
    Ok(res)
}

/// Serves the signed Linux update envelope read by
/// `maho_update_delegate_linux.cc` (`GET /updates/linux/check?channel=`).
/// Linux is notification-only, so there is no rollout bucket: an absent
/// release returns `{}`, which the client treats as an unsigned manifest.
pub async fn get_linux_check(req: Request, env: Env) -> Result<Response> {
    let db = env.d1("MAHO_DB")?;
    let url = req.url()?;
    let query: std::collections::HashMap<String, String> = url.query_pairs().into_owned().collect();

    let channel = query
        .get("channel")
        .cloned()
        .unwrap_or_else(|| "stable".to_string());

    let body = match db::find_latest_linux_release(&db, &channel).await? {
        Some(release) => match release.linux_envelope {
            Some(envelope) if !envelope.trim().is_empty() => envelope,
            _ => "{}".to_string(),
        },
        None => "{}".to_string(),
    };

    let mut res = Response::ok(body)?;
    res.headers_mut()
        .set("Content-Type", "application/json")?;
    res.headers_mut()
        .set("Cache-Control", CACHE_CONTROL_HEADER)?;
    Ok(res)
}

pub async fn post_release(mut req: Request, env: Env) -> Result<Response> {
    require_release_admin(&req, &env)?;

    let db = env.d1("MAHO_DB")?;
    let update: ReleaseUpdate = req.json().await?;

    db::upsert_release_update(&db, &update).await?;

    Ok(Response::ok("release updated")?)
}

/// Requires a dedicated config-update admin token before accepting signed updates.
///
/// The token must be sent in `X-Config-Update-Admin-Token` and match the
/// `CONFIG_UPDATE_ADMIN_TOKEN` Worker secret via constant-time equality.
fn require_config_update_admin(req: &Request, env: &Env) -> Result<()> {
    let presented_token = req
        .headers()
        .get(CONFIG_UPDATE_ADMIN_TOKEN_HEADER)?
        .map(|token| token.trim().to_string())
        .filter(|token| !token.is_empty())
        .ok_or_else(|| AppError::Auth("missing config update admin token".to_string()))?;

    let expected_token = env
        .secret(CONFIG_UPDATE_ADMIN_TOKEN_SECRET)
        .map(|secret| secret.to_string())
        .map_err(|_| AppError::Internal("config update admin token not configured".to_string()))?;

    if expected_token.is_empty() {
        return Err(AppError::Internal(
            "config update admin token not configured".to_string(),
        ));
    }

    if !constant_time_eq(presented_token.as_bytes(), expected_token.as_bytes()) {
        return Err(AppError::Auth(
            "invalid config update admin token".to_string(),
        ));
    }

    Ok(())
}

pub(crate) fn validate_release_admin_token(
    presented: Option<&str>,
    expected: &str,
) -> Result<()> {
    let presented_token = presented
        .map(str::trim)
        .filter(|token| !token.is_empty())
        .ok_or_else(|| AppError::Auth("missing release admin token".to_string()))?;

    if expected.is_empty() {
        return Err(AppError::Internal(
            "release admin token not configured".to_string(),
        ));
    }

    if !constant_time_eq(presented_token.as_bytes(), expected.as_bytes()) {
        return Err(AppError::Auth(
            "invalid release admin token".to_string(),
        ));
    }

    Ok(())
}

/// Requires a dedicated release admin token before accepting releases.
///
/// The token must be sent in `X-Release-Admin-Token` and match the
/// `RELEASE_ADMIN_TOKEN` Worker secret via constant-time equality.
fn require_release_admin(req: &Request, env: &Env) -> Result<()> {
    let presented = req.headers().get(RELEASE_ADMIN_TOKEN_HEADER)?;
    let expected = env
        .secret(RELEASE_ADMIN_TOKEN_SECRET)
        .map(|secret| secret.to_string())
        .map_err(|_| AppError::Internal("release admin token not configured".to_string()))?;

    validate_release_admin_token(presented.as_deref(), &expected)
}

fn constant_time_eq(left: &[u8], right: &[u8]) -> bool {
    use subtle::ConstantTimeEq;
    left.ct_eq(right).unwrap_u8() == 1
}

pub(crate) fn verify_config_signature(
    pubkey_b64: &str,
    sig_b64: &str,
    payload: &str,
) -> Result<()> {
    let pubkey_bytes = base64::prelude::BASE64_STANDARD
        .decode(pubkey_b64)
        .map_err(|e| AppError::BadRequest(format!("invalid public key base64: {e}")))?;

    let verifying_key = VerifyingKey::try_from(pubkey_bytes.as_slice())
        .map_err(|e| AppError::BadRequest(format!("invalid public key format: {e}")))?;

    let sig_bytes = base64::prelude::BASE64_STANDARD
        .decode(sig_b64)
        .map_err(|e| AppError::BadRequest(format!("invalid signature base64: {e}")))?;

    let signature = Signature::from_slice(&sig_bytes)
        .map_err(|e| AppError::BadRequest(format!("invalid signature length/format: {e}")))?;

    verifying_key
        .verify(payload.as_bytes(), &signature)
        .map_err(|_| AppError::BadRequest("bad signature".into()))?;

    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use ed25519_dalek::Signer;
    use ed25519_dalek::SigningKey;

    #[test]
    fn verify_config_signature_accepts_valid_signature() -> Result<()> {
        let seed = [0u8; 32];
        let signing_key = SigningKey::from_bytes(&seed);
        let verifying_key = signing_key.verifying_key();

        let payload = "{\"test\": true}";
        let sig = signing_key.sign(payload.as_bytes());

        let pubkey_b64 = base64::prelude::BASE64_STANDARD.encode(verifying_key.to_bytes());
        let sig_b64 = base64::prelude::BASE64_STANDARD.encode(sig.to_bytes());

        verify_config_signature(&pubkey_b64, &sig_b64, payload)?;
        Ok(())
    }

    #[test]
    fn verify_config_signature_rejects_tampered_payload() {
        let seed = [0u8; 32];
        let signing_key = SigningKey::from_bytes(&seed);
        let verifying_key = signing_key.verifying_key();

        let payload = "{\"test\": true}";
        let sig = signing_key.sign(payload.as_bytes());

        let pubkey_b64 = base64::prelude::BASE64_STANDARD.encode(verifying_key.to_bytes());
        let sig_b64 = base64::prelude::BASE64_STANDARD.encode(sig.to_bytes());

        let result = verify_config_signature(&pubkey_b64, &sig_b64, "{\"test\": false}");

        assert!(matches!(
            result,
            Err(AppError::BadRequest(message)) if message == "bad signature"
        ));
    }

    #[test]
    fn parse_rollout_bucket_accepts_valid_range() {
        assert_eq!(parse_rollout_bucket(Some("0")), 0);
        assert_eq!(parse_rollout_bucket(Some("50")), 50);
        assert_eq!(parse_rollout_bucket(Some("99")), 99);
    }

    #[test]
    fn parse_rollout_bucket_fails_closed_for_invalid_input() {
        assert_eq!(parse_rollout_bucket(None), ROLLOUT_BUCKET_SENTINEL);
        assert_eq!(parse_rollout_bucket(Some("")), ROLLOUT_BUCKET_SENTINEL);
        assert_eq!(parse_rollout_bucket(Some("abc")), ROLLOUT_BUCKET_SENTINEL);
        assert_eq!(parse_rollout_bucket(Some("-1")), ROLLOUT_BUCKET_SENTINEL);
        assert_eq!(parse_rollout_bucket(Some("100")), ROLLOUT_BUCKET_SENTINEL);
        assert_eq!(parse_rollout_bucket(Some("9999")), ROLLOUT_BUCKET_SENTINEL);
    }

    #[test]
    fn test_auth_failure_401() {
        // Missing token -> 401 Auth error
        let err_missing = validate_release_admin_token(None, "secret_token").unwrap_err();
        assert!(matches!(err_missing, AppError::Auth(ref msg) if msg.contains("missing")));

        // Empty token -> 401 Auth error
        let err_empty = validate_release_admin_token(Some("   "), "secret_token").unwrap_err();
        assert!(matches!(err_empty, AppError::Auth(ref msg) if msg.contains("missing")));

        // Invalid token -> 401 Auth error
        let err_invalid = validate_release_admin_token(Some("wrong_token"), "secret_token").unwrap_err();
        assert!(matches!(err_invalid, AppError::Auth(ref msg) if msg.contains("invalid")));

        // Valid token -> Ok
        assert!(validate_release_admin_token(Some("secret_token"), "secret_token").is_ok());
    }

    #[test]
    fn test_empty_channel_200() {
        assert_eq!(CACHE_CONTROL_HEADER, "public, max-age=60, s-maxage=300, stale-while-revalidate=60");
        assert!(EMPTY_APPCAST_XML.contains("<rss version=\"2.0\""));
        assert!(EMPTY_APPCAST_XML.contains("xmlns:sparkle=\"http://www.andymatuschak.org/xml-namespaces/sparkle\""));
        assert!(EMPTY_APPCAST_XML.contains("<channel>"));
        assert!(EMPTY_APPCAST_XML.contains("<title>Maho Updates</title>"));
        assert!(EMPTY_APPCAST_XML.contains("</rss>"));
    }
}
