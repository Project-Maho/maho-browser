use crate::errors::{AppError, Result};
use base64::engine::general_purpose::URL_SAFE_NO_PAD;
use base64::Engine;
use serde::Deserialize;
use sha2::{Digest, Sha256};

pub const GOOGLE_JWKS_URL: &str = "https://www.googleapis.com/oauth2/v3/certs";

const ISSUERS: [&str; 2] = ["accounts.google.com", "https://accounts.google.com"];
const CLOCK_SKEW_SECS: i64 = 120;
const MAX_TOKEN_AGE_SECS: i64 = 10 * 60;

#[derive(Debug, Deserialize)]
struct JwtHeader {
    alg: String,
    kid: String,
}

#[derive(Debug, Deserialize)]
pub struct GoogleClaims {
    pub iss: String,
    pub aud: String,
    pub sub: String,
    pub exp: i64,
    pub iat: i64,
    #[serde(default)]
    pub nonce: Option<String>,
    #[serde(default)]
    pub email: Option<String>,
    #[serde(default)]
    pub email_verified: Option<bool>,
    #[serde(default)]
    pub name: Option<String>,
}

#[derive(Debug, Clone, Deserialize)]
pub struct Jwk {
    pub kid: String,
    pub n: String,
    pub e: String,
}

#[derive(Debug, Clone, Deserialize)]
pub struct Jwks {
    pub keys: Vec<Jwk>,
}

pub struct VerifiedIdentity {
    pub provider_sub: String,
    pub email: String,
    pub email_verified: bool,
    pub display_name: Option<String>,
    pub nonce: String,
}

pub fn parse_audiences(raw: &str) -> Vec<String> {
    raw.split(',')
        .map(|s| s.trim())
        .filter(|s| !s.is_empty())
        .map(|s| s.to_string())
        .collect()
}

pub fn hash_nonce(nonce: &str) -> [u8; 32] {
    let mut hasher = Sha256::new();
    hasher.update(nonce.as_bytes());
    hasher.finalize().into()
}

struct SplitToken<'a> {
    header: JwtHeader,
    claims_b64: &'a str,
    signing_input: &'a str,
    signature: Vec<u8>,
}

fn split_token(id_token: &str) -> Result<SplitToken<'_>> {
    let mut parts = id_token.split('.');
    let (Some(header_b64), Some(claims_b64), Some(sig_b64), None) =
        (parts.next(), parts.next(), parts.next(), parts.next())
    else {
        return Err(AppError::Auth("malformed id_token".to_string()));
    };

    let header_bytes = URL_SAFE_NO_PAD
        .decode(header_b64)
        .map_err(|_| AppError::Auth("malformed id_token header".to_string()))?;
    let header: JwtHeader = serde_json::from_slice(&header_bytes)
        .map_err(|_| AppError::Auth("malformed id_token header".to_string()))?;

    if header.alg != "RS256" {
        return Err(AppError::Auth("unsupported id_token alg".to_string()));
    }

    let signature = URL_SAFE_NO_PAD
        .decode(sig_b64)
        .map_err(|_| AppError::Auth("malformed id_token signature".to_string()))?;

    // Signing input is the raw header.claims substring, not a re-encode: any
    // re-serialization would change bytes and break signature verification.
    let signing_input = &id_token[..header_b64.len() + 1 + claims_b64.len()];

    Ok(SplitToken {
        header,
        claims_b64,
        signing_input,
        signature,
    })
}

fn decode_claims(claims_b64: &str) -> Result<GoogleClaims> {
    let bytes = URL_SAFE_NO_PAD
        .decode(claims_b64)
        .map_err(|_| AppError::Auth("malformed id_token claims".to_string()))?;
    serde_json::from_slice(&bytes)
        .map_err(|_| AppError::Auth("malformed id_token claims".to_string()))
}

pub fn validate_claims(
    claims: &GoogleClaims,
    allowed_audiences: &[String],
    expected_nonce: &str,
    now: i64,
) -> Result<VerifiedIdentity> {
    if !ISSUERS.contains(&claims.iss.as_str()) {
        return Err(AppError::Auth("unexpected id_token issuer".to_string()));
    }
    if allowed_audiences.is_empty() {
        return Err(AppError::Internal(
            "GOOGLE_OAUTH_ALLOWED_AUDS not configured".to_string(),
        ));
    }
    if !allowed_audiences.contains(&claims.aud) {
        return Err(AppError::Auth("unexpected id_token audience".to_string()));
    }
    if claims.exp <= now - CLOCK_SKEW_SECS {
        return Err(AppError::Auth("id_token expired".to_string()));
    }
    if claims.iat > now + CLOCK_SKEW_SECS {
        return Err(AppError::Auth("id_token issued in the future".to_string()));
    }
    if now - claims.iat > MAX_TOKEN_AGE_SECS {
        return Err(AppError::Auth("id_token too old".to_string()));
    }
    if claims.sub.trim().is_empty() {
        return Err(AppError::Auth("id_token missing sub".to_string()));
    }

    if expected_nonce.trim().is_empty() {
        return Err(AppError::BadRequest("nonce is required".to_string()));
    }
    let token_nonce = claims.nonce.as_deref().unwrap_or_default();
    // Constant-time compare: `!=` on str short-circuits at the first differing
    // byte, which would leak how much of the nonce an attacker guessed.
    use subtle::ConstantTimeEq;
    let nonce_ok = token_nonce.len() == expected_nonce.len()
        && token_nonce
            .as_bytes()
            .ct_eq(expected_nonce.as_bytes())
            .unwrap_u8()
            == 1;
    if !nonce_ok {
        return Err(AppError::Auth("id_token nonce mismatch".to_string()));
    }

    let email = claims
        .email
        .as_deref()
        .map(str::trim)
        .filter(|e| !e.is_empty())
        .ok_or_else(|| AppError::Auth("id_token missing email".to_string()))?
        .to_ascii_lowercase();

    Ok(VerifiedIdentity {
        provider_sub: claims.sub.clone(),
        email,
        email_verified: claims.email_verified.unwrap_or(false),
        display_name: claims
            .name
            .as_deref()
            .map(str::trim)
            .filter(|n| !n.is_empty())
            .map(str::to_string),
        nonce: expected_nonce.to_string(),
    })
}

pub fn select_key<'a>(jwks: &'a Jwks, kid: &str) -> Option<&'a Jwk> {
    jwks.keys.iter().find(|k| k.kid == kid)
}

async fn verify_rs256(jwk: &Jwk, signing_input: &str, signature: &[u8]) -> Result<bool> {
    use js_sys::{Object, Reflect, Uint8Array};
    use wasm_bindgen::{JsCast, JsValue};
    use wasm_bindgen_futures::JsFuture;

    let global = js_sys::global()
        .dyn_into::<web_sys::WorkerGlobalScope>()
        .map_err(|_| AppError::Internal("no worker global scope".to_string()))?;
    let crypto = global
        .crypto()
        .map_err(|_| AppError::Internal("crypto unavailable".to_string()))?;
    let subtle = crypto.subtle();

    let jwk_obj = Object::new();
    let set = |key: &str, value: &JsValue| -> Result<()> {
        Reflect::set(&jwk_obj, &JsValue::from_str(key), value)
            .map_err(|_| AppError::Internal("jwk build failed".to_string()))?;
        Ok(())
    };
    set("kty", &JsValue::from_str("RSA"))?;
    set("alg", &JsValue::from_str("RS256"))?;
    set("n", &JsValue::from_str(&jwk.n))?;
    set("e", &JsValue::from_str(&jwk.e))?;
    set("ext", &JsValue::TRUE)?;

    let algorithm = Object::new();
    Reflect::set(
        &algorithm,
        &JsValue::from_str("name"),
        &JsValue::from_str("RSASSA-PKCS1-v1_5"),
    )
    .map_err(|_| AppError::Internal("algorithm build failed".to_string()))?;
    let hash = Object::new();
    Reflect::set(
        &hash,
        &JsValue::from_str("name"),
        &JsValue::from_str("SHA-256"),
    )
    .map_err(|_| AppError::Internal("algorithm build failed".to_string()))?;
    Reflect::set(&algorithm, &JsValue::from_str("hash"), &hash)
        .map_err(|_| AppError::Internal("algorithm build failed".to_string()))?;

    let usages = js_sys::Array::of1(&JsValue::from_str("verify"));
    let import = subtle
        .import_key_with_object("jwk", &jwk_obj, &algorithm, false, &usages)
        .map_err(|_| AppError::Internal("importKey rejected jwk".to_string()))?;
    let key = JsFuture::from(import)
        .await
        .map_err(|_| AppError::Internal("importKey failed".to_string()))?;
    let key: web_sys::CryptoKey = key
        .dyn_into()
        .map_err(|_| AppError::Internal("importKey returned non-key".to_string()))?;

    let sig = Uint8Array::from(signature);
    let data = Uint8Array::from(signing_input.as_bytes());
    let verify = subtle
        .verify_with_object_and_buffer_source_and_buffer_source(
            &algorithm,
            &key,
            &sig.into(),
            &data.into(),
        )
        .map_err(|_| AppError::Internal("subtle.verify rejected input".to_string()))?;
    let outcome = JsFuture::from(verify)
        .await
        .map_err(|_| AppError::Internal("subtle.verify failed".to_string()))?;

    Ok(outcome.as_bool().unwrap_or(false))
}

pub async fn fetch_jwks() -> Result<Jwks> {
    let mut res = worker::Fetch::Url(
        GOOGLE_JWKS_URL
            .parse()
            .map_err(|_| AppError::Internal("bad jwks url".to_string()))?,
    )
    .send()
    .await
    .map_err(|e| AppError::Internal(format!("jwks fetch failed: {e}")))?;

    if res.status_code() != 200 {
        return Err(AppError::Internal(format!(
            "jwks fetch status {}",
            res.status_code()
        )));
    }

    res.json::<Jwks>()
        .await
        .map_err(|e| AppError::Internal(format!("jwks parse failed: {e}")))
}

pub async fn verify_id_token(
    id_token: &str,
    allowed_audiences: &[String],
    expected_nonce: &str,
    now: i64,
) -> Result<VerifiedIdentity> {
    let split = split_token(id_token)?;
    let claims = decode_claims(split.claims_b64)?;
    let identity = validate_claims(&claims, allowed_audiences, expected_nonce, now)?;

    let jwks = fetch_jwks().await?;
    let jwk = select_key(&jwks, &split.header.kid)
        .ok_or_else(|| AppError::Auth("unknown id_token signing key".to_string()))?;

    if !verify_rs256(jwk, split.signing_input, &split.signature).await? {
        return Err(AppError::Auth("id_token signature invalid".to_string()));
    }

    Ok(identity)
}

#[cfg(test)]
#[path = "google_tests.rs"]
mod google_tests;
