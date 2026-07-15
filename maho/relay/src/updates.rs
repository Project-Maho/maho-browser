use worker::*;
use crate::errors::{AppError, Result};
use crate::db::{self, ConfigUpdate};
use ed25519_dalek::{VerifyingKey, Signature, Verifier};
use base64::Engine;

pub async fn get_config(req: Request, env: Env) -> Result<Response> {
    let db = env.d1("MAHO_DB")?;
    let url = req.url()?;
    let query: std::collections::HashMap<String, String> = url.query_pairs().into_owned().collect();

    let channel = query.get("channel").cloned().unwrap_or_else(|| "stable".to_string());
    let bucket: i32 = query.get("b")
        .and_then(|s| s.parse::<i32>().ok())
        .unwrap_or(0);

    match db::find_latest_config_update(&db, &channel, bucket).await? {
        Some(config) => Ok(Response::from_json(&config)?),
        None => Err(AppError::NotFound("no matching configuration found".into())),
    }
}

pub async fn post_config(mut req: Request, env: Env) -> Result<Response> {
    let db = env.d1("MAHO_DB")?;
    let update: ConfigUpdate = req.json().await?;

    // Load and decode public key from Env
    let pubkey_str = env.var("CONFIG_UPDATE_PUBLIC_KEY")
        .map(|v| v.to_string())
        .unwrap_or_else(|_| {
            // Default mock public key for development/tests if env is missing
            // 32-byte public key corresponding to a mock seed
            "MCowBQYDK2VwAyEAX1z3H4Xo927K8VbT7K+9hF3Q7Q9jPz3m3V7fS7KjXz4=".to_string()
        });

    let pubkey_bytes = base64::prelude::BASE64_STANDARD.decode(&pubkey_str)
        .map_err(|e| AppError::BadRequest(format!("invalid public key base64: {e}")))?;

    let verifying_key = VerifyingKey::try_from(pubkey_bytes.as_slice())
        .map_err(|e| AppError::BadRequest(format!("invalid public key format: {e}")))?;

    // Decode signature
    let sig_bytes = base64::prelude::BASE64_STANDARD.decode(&update.signature)
        .map_err(|e| AppError::BadRequest(format!("invalid signature base64: {e}")))?;

    let signature = Signature::from_slice(&sig_bytes)
        .map_err(|e| AppError::BadRequest(format!("invalid signature length/format: {e}")))?;

    // Verify
    verifying_key.verify(update.payload.as_bytes(), &signature)
        .map_err(|_| AppError::BadRequest("bad signature".into()))?;

    // Store in database
    db::insert_config_update(&db, &update).await?;

    Ok(Response::ok("config updated")?)
}

#[cfg(test)]
mod tests {
    use super::*;
    use ed25519_dalek::SigningKey;
    use ed25519_dalek::Signer;

    #[test]
    fn test_signature_verification_success() {
        let seed = [0u8; 32];
        let signing_key = SigningKey::from_bytes(&seed);
        let verifying_key = signing_key.verifying_key();
        
        let payload = "{\"test\": true}";
        let sig = signing_key.sign(payload.as_bytes());
        
        let pubkey_b64 = base64::prelude::BASE64_STANDARD.encode(verifying_key.to_bytes());
        let sig_b64 = base64::prelude::BASE64_STANDARD.encode(sig.to_bytes());
        
        let pubkey_bytes = base64::prelude::BASE64_STANDARD.decode(&pubkey_b64).unwrap();
        let verifying_key_decoded = VerifyingKey::try_from(pubkey_bytes.as_slice()).unwrap();
        
        let sig_bytes = base64::prelude::BASE64_STANDARD.decode(&sig_b64).unwrap();
        let signature_decoded = Signature::from_slice(&sig_bytes).unwrap();
        
        assert!(verifying_key_decoded.verify(payload.as_bytes(), &signature_decoded).is_ok());
    }

    #[test]
    fn test_signature_verification_failure_modified_payload() {
        let seed = [0u8; 32];
        let signing_key = SigningKey::from_bytes(&seed);
        let verifying_key = signing_key.verifying_key();
        
        let payload = "{\"test\": true}";
        let sig = signing_key.sign(payload.as_bytes());
        
        let pubkey_b64 = base64::prelude::BASE64_STANDARD.encode(verifying_key.to_bytes());
        let sig_b64 = base64::prelude::BASE64_STANDARD.encode(sig.to_bytes());
        
        let pubkey_bytes = base64::prelude::BASE64_STANDARD.decode(&pubkey_b64).unwrap();
        let verifying_key_decoded = VerifyingKey::try_from(pubkey_bytes.as_slice()).unwrap();
        
        let sig_bytes = base64::prelude::BASE64_STANDARD.decode(&sig_b64).unwrap();
        let signature_decoded = Signature::from_slice(&sig_bytes).unwrap();
        
        // Verifying key verification on modified payload should fail
        assert!(verifying_key_decoded.verify("{\"test\": false}".as_bytes(), &signature_decoded).is_err());
    }
}

