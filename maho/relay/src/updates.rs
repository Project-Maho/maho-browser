use crate::db::{self, ConfigUpdate};
use crate::errors::{AppError, Result};
use base64::Engine;
use ed25519_dalek::{Signature, Verifier, VerifyingKey};
use worker::*;

const CONFIG_UPDATE_ADMIN_TOKEN_HEADER: &str = "X-Config-Update-Admin-Token";
const CONFIG_UPDATE_ADMIN_TOKEN_SECRET: &str = "CONFIG_UPDATE_ADMIN_TOKEN";
const CONFIG_UPDATE_PUBLIC_KEY_VAR: &str = "CONFIG_UPDATE_PUBLIC_KEY";

pub async fn get_config(req: Request, env: Env) -> Result<Response> {
    let db = env.d1("MAHO_DB")?;
    let url = req.url()?;
    let query: std::collections::HashMap<String, String> = url.query_pairs().into_owned().collect();

    let channel = query
        .get("channel")
        .cloned()
        .unwrap_or_else(|| "stable".to_string());
    let bucket: i32 = query
        .get("b")
        .and_then(|s| s.parse::<i32>().ok())
        .unwrap_or(0);

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
}
