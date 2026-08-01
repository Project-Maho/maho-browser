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
