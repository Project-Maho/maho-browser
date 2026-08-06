use super::*;

fn claims(nonce: &str, now: i64) -> GoogleClaims {
    GoogleClaims {
        iss: "https://accounts.google.com".to_string(),
        aud: "desktop-client".to_string(),
        sub: "google-sub-123".to_string(),
        exp: now + 3600,
        iat: now,
        nonce: Some(nonce.to_string()),
        email: Some("User@Example.com".to_string()),
        email_verified: Some(true),
        name: Some("Maho User".to_string()),
    }
}

fn auds() -> Vec<String> {
    vec!["desktop-client".to_string(), "ios-client".to_string()]
}

#[test]
fn parse_audiences_splits_and_trims() {
    assert_eq!(
        parse_audiences(" a.apps.googleusercontent.com , b.apps.googleusercontent.com "),
        vec![
            "a.apps.googleusercontent.com".to_string(),
            "b.apps.googleusercontent.com".to_string()
        ]
    );
    assert!(parse_audiences("").is_empty());
    assert!(parse_audiences("  ,  ").is_empty());
}

#[test]
fn valid_claims_yield_normalized_identity() {
    let now = 1_800_000_000;
    let identity = validate_claims(&claims("nonce-abc", now), &auds(), "nonce-abc", now)
        .expect("claims should validate");
    assert_eq!(identity.provider_sub, "google-sub-123");
    assert_eq!(identity.email, "user@example.com");
    assert!(identity.email_verified);
    assert_eq!(identity.display_name.as_deref(), Some("Maho User"));
}

#[test]
fn unexpected_issuer_rejected() {
    let now = 1_800_000_000;
    let mut c = claims("n", now);
    c.iss = "https://evil.example.com".to_string();
    assert!(validate_claims(&c, &auds(), "n", now).is_err());
}

#[test]
fn bare_google_issuer_accepted() {
    let now = 1_800_000_000;
    let mut c = claims("n", now);
    c.iss = "accounts.google.com".to_string();
    assert!(validate_claims(&c, &auds(), "n", now).is_ok());
}

#[test]
fn foreign_audience_rejected() {
    let now = 1_800_000_000;
    let mut c = claims("n", now);
    c.aud = "someone-elses-client".to_string();
    assert!(validate_claims(&c, &auds(), "n", now).is_err());
}

#[test]
fn empty_audience_allowlist_fails_closed() {
    let now = 1_800_000_000;
    assert!(validate_claims(&claims("n", now), &[], "n", now).is_err());
}

#[test]
fn expired_token_rejected() {
    let now = 1_800_000_000;
    let mut c = claims("n", now);
    c.exp = now - 3600;
    assert!(validate_claims(&c, &auds(), "n", now).is_err());
}

#[test]
fn stale_iat_rejected() {
    let now = 1_800_000_000;
    let mut c = claims("n", now - 3600);
    c.exp = now + 3600;
    assert!(validate_claims(&c, &auds(), "n", now).is_err());
}

#[test]
fn future_iat_rejected() {
    let now = 1_800_000_000;
    let mut c = claims("n", now + 3600);
    c.exp = now + 7200;
    assert!(validate_claims(&c, &auds(), "n", now).is_err());
}

#[test]
fn nonce_mismatch_rejected() {
    let now = 1_800_000_000;
    assert!(validate_claims(&claims("issued-nonce", now), &auds(), "other-nonce", now).is_err());
}

#[test]
fn missing_nonce_in_token_rejected() {
    let now = 1_800_000_000;
    let mut c = claims("n", now);
    c.nonce = None;
    assert!(validate_claims(&c, &auds(), "n", now).is_err());
}

#[test]
fn empty_expected_nonce_rejected() {
    let now = 1_800_000_000;
    let mut c = claims("", now);
    c.nonce = Some(String::new());
    assert!(validate_claims(&c, &auds(), "", now).is_err());
}

#[test]
fn missing_email_rejected() {
    let now = 1_800_000_000;
    let mut c = claims("n", now);
    c.email = None;
    assert!(validate_claims(&c, &auds(), "n", now).is_err());
}

#[test]
fn unverified_email_surfaces_as_false() {
    let now = 1_800_000_000;
    let mut c = claims("n", now);
    c.email_verified = Some(false);
    let identity = validate_claims(&c, &auds(), "n", now).expect("should validate");
    assert!(!identity.email_verified);
}

#[test]
fn absent_email_verified_defaults_to_false() {
    let now = 1_800_000_000;
    let mut c = claims("n", now);
    c.email_verified = None;
    let identity = validate_claims(&c, &auds(), "n", now).expect("should validate");
    assert!(!identity.email_verified);
}

#[test]
fn blank_sub_rejected() {
    let now = 1_800_000_000;
    let mut c = claims("n", now);
    c.sub = "   ".to_string();
    assert!(validate_claims(&c, &auds(), "n", now).is_err());
}

#[test]
fn select_key_matches_kid() {
    let jwks = Jwks {
        keys: vec![
            Jwk {
                kid: "k1".to_string(),
                n: "n1".to_string(),
                e: "AQAB".to_string(),
            },
            Jwk {
                kid: "k2".to_string(),
                n: "n2".to_string(),
                e: "AQAB".to_string(),
            },
        ],
    };
    assert_eq!(select_key(&jwks, "k2").map(|k| k.n.as_str()), Some("n2"));
    assert!(select_key(&jwks, "missing").is_none());
}

#[test]
fn hash_nonce_is_stable_and_distinct() {
    assert_eq!(hash_nonce("abc"), hash_nonce("abc"));
    assert_ne!(hash_nonce("abc"), hash_nonce("abd"));
}

#[test]
fn token_exchange_body_includes_secret_and_form_encodes() {
    let body = build_token_exchange_body(
        "client-123.apps.googleusercontent.com",
        "GOCSPX-secret/value",
        "4/0Aauth-code+value",
        "verifier-abc.def~ghi",
        "http://127.0.0.1:56142",
    );
    // Fixed field order lets us assert the whole encoded body deterministically.
    assert_eq!(
        body,
        "client_id=client-123.apps.googleusercontent.com\
         &client_secret=GOCSPX-secret%2Fvalue\
         &code=4%2F0Aauth-code%2Bvalue\
         &code_verifier=verifier-abc.def~ghi\
         &grant_type=authorization_code\
         &redirect_uri=http%3A%2F%2F127.0.0.1%3A56142"
    );
}

#[test]
fn token_exchange_body_carries_client_secret_field() {
    // The whole point of the server-side exchange: the secret is present so
    // Google's desktop-client token endpoint does not reject with
    // "client_secret is missing".
    let body = build_token_exchange_body("cid", "the-secret", "code", "verifier", "http://x");
    assert!(body.contains("client_secret=the-secret"));
    assert!(body.contains("grant_type=authorization_code"));
}

#[test]
fn verify_rs256_accepts_valid_signature_and_rejects_tampering() {
    use base64::engine::general_purpose::URL_SAFE_NO_PAD;
    use base64::Engine as _;
    use rsa::traits::PublicKeyParts;
    use rsa::{Pkcs1v15Sign, RsaPrivateKey, RsaPublicKey};
    use sha2::{Digest, Sha256};

    let mut rng = rand::thread_rng();
    let private_key = RsaPrivateKey::new(&mut rng, 2048).expect("generate rsa key");
    let public_key = RsaPublicKey::from(&private_key);
    let jwk = Jwk {
        kid: "test-kid".to_string(),
        n: URL_SAFE_NO_PAD.encode(public_key.n().to_bytes_be()),
        e: URL_SAFE_NO_PAD.encode(public_key.e().to_bytes_be()),
    };

    // A JWT signing input is `base64url(header).base64url(claims)`.
    let signing_input = "eyJhbGciOiJSUzI1NiJ9.eyJzdWIiOiJnb29nbGUtc3ViIn0";
    let digest = Sha256::digest(signing_input.as_bytes());
    let signature = private_key
        .sign(Pkcs1v15Sign::new::<Sha256>(), &digest)
        .expect("sign");

    assert!(
        verify_rs256(&jwk, signing_input, &signature).unwrap(),
        "a genuine RS256 signature must verify"
    );

    let mut tampered_sig = signature.clone();
    tampered_sig[0] ^= 0xFF;
    assert!(
        !verify_rs256(&jwk, signing_input, &tampered_sig).unwrap(),
        "a tampered signature must not verify"
    );

    assert!(
        !verify_rs256(&jwk, "different.signing.input", &signature).unwrap(),
        "a signature over different input must not verify"
    );
}
