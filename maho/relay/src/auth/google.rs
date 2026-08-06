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

fn verify_rs256(jwk: &Jwk, signing_input: &str, signature: &[u8]) -> Result<bool> {
    use rsa::{BigUint, Pkcs1v15Sign, RsaPublicKey};
    use sha2::{Digest, Sha256};

    // Google JWKs carry the modulus/exponent as base64url (no padding). Verify
    // RS256 in pure Rust so the check runs identically on native tests and the
    // wasm Workers runtime. The previous WebCrypto path rejected valid Google
    // keys at importKey on workerd, 500ing every Google login.
    let n = BigUint::from_bytes_be(
        &URL_SAFE_NO_PAD
            .decode(jwk.n.as_bytes())
            .map_err(|_| AppError::Auth("malformed jwk modulus".to_string()))?,
    );
    let e = BigUint::from_bytes_be(
        &URL_SAFE_NO_PAD
            .decode(jwk.e.as_bytes())
            .map_err(|_| AppError::Auth("malformed jwk exponent".to_string()))?,
    );
    let public_key = RsaPublicKey::new(n, e)
        .map_err(|_| AppError::Internal("invalid jwk rsa key".to_string()))?;

    let digest = Sha256::digest(signing_input.as_bytes());
    Ok(public_key
        .verify(Pkcs1v15Sign::new::<Sha256>(), &digest, signature)
        .is_ok())
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

    if !verify_rs256(jwk, split.signing_input, &split.signature)? {
        return Err(AppError::Auth("id_token signature invalid".to_string()));
    }

    Ok(identity)
}

pub const GOOGLE_TOKEN_URL: &str = "https://oauth2.googleapis.com/token";

#[derive(Debug, Deserialize)]
struct TokenExchangeResponse {
    #[serde(default)]
    id_token: Option<String>,
}

/// Percent-encodes a value for an `application/x-www-form-urlencoded` body.
fn form_encode_value(value: &str) -> String {
    value
        .bytes()
        .map(|b| match b {
            b'A'..=b'Z' | b'a'..=b'z' | b'0'..=b'9' | b'-' | b'_' | b'.' | b'~' => {
                (b as char).to_string()
            }
            _ => format!("%{b:02X}"),
        })
        .collect()
}

/// Builds the Google token-endpoint request body for the desktop
/// authorization-code exchange. Pure so the encoding is unit-testable off the
/// Workers runtime.
pub fn build_token_exchange_body(
    client_id: &str,
    client_secret: &str,
    code: &str,
    code_verifier: &str,
    redirect_uri: &str,
) -> String {
    [
        ("client_id", client_id),
        ("client_secret", client_secret),
        ("code", code),
        ("code_verifier", code_verifier),
        ("grant_type", "authorization_code"),
        ("redirect_uri", redirect_uri),
    ]
    .iter()
    .map(|(k, v)| format!("{}={}", form_encode_value(k), form_encode_value(v)))
    .collect::<Vec<_>>()
    .join("&")
}

/// Exchanges a PKCE authorization code for a Google `id_token` server-side.
///
/// The desktop OAuth client type requires the client secret at the token
/// endpoint even with PKCE, so the exchange runs here where the secret lives
/// as a Worker secret and never ships in any client binary. Only the resulting
/// `id_token` is returned to the caller; Google access/refresh tokens are
/// discarded (identity-only login uses no Gmail scope).
pub async fn exchange_code(
    client_id: &str,
    client_secret: &str,
    code: &str,
    code_verifier: &str,
    redirect_uri: &str,
) -> Result<String> {
    use worker::{Fetch, Headers, Method, Request, RequestInit};

    let body =
        build_token_exchange_body(client_id, client_secret, code, code_verifier, redirect_uri);

    let headers = Headers::new();
    headers
        .set("Content-Type", "application/x-www-form-urlencoded")
        .map_err(|_| AppError::Internal("token exchange header build failed".to_string()))?;

    let mut init = RequestInit::new();
    init.with_method(Method::Post)
        .with_headers(headers)
        .with_body(Some(body.into()));

    let request = Request::new_with_init(GOOGLE_TOKEN_URL, &init)
        .map_err(|_| AppError::Internal("token exchange request build failed".to_string()))?;

    let mut res = Fetch::Request(request)
        .send()
        .await
        .map_err(|e| AppError::Internal(format!("token exchange fetch failed: {e}")))?;

    if res.status_code() != 200 {
        return Err(AppError::Auth("google token exchange rejected".to_string()));
    }

    let parsed: TokenExchangeResponse = res
        .json()
        .await
        .map_err(|e| AppError::Internal(format!("token response parse failed: {e}")))?;

    parsed
        .id_token
        .filter(|t| !t.is_empty())
        .ok_or_else(|| AppError::Auth("google token response missing id_token".to_string()))
}

#[cfg(test)]
#[path = "google_tests.rs"]
mod google_tests;
