// Copyright 2026 Maho Browser. All rights reserved.

use chrono::{TimeZone, Utc};

use super::{
    best_otp_candidate, domain_binding, extract_otp_candidates, DomainBinding, OtpMessage,
    OtpSource,
};

fn received_at() -> chrono::DateTime<Utc> {
    Utc.with_ymd_and_hms(2026, 7, 15, 12, 0, 0).unwrap()
}

fn message_with_body(body_text: &str) -> OtpMessage<'_> {
    OtpMessage {
        sender_email: Some("security@example.com"),
        recipient_email: Some("user@example.com"),
        page_origin: Some("https://example.com/login"),
        subject: "",
        body_text,
        body_html: "",
        received_at: received_at(),
        now: received_at() + chrono::Duration::minutes(2),
    }
}

#[test]
fn best_candidate_excludes_false_positive_matrix() {
    // Each case pairs an OTP-context keyword (so `has_otp_context` passes and the
    // exclusion logic — not mere keyword-absence — is what suppresses the match)
    // with a number drawn from a distinct false-positive class. Every case MUST
    // yield no candidate.
    let cases = [
        ("year_low", "Your verification code expired back in 2000."),
        ("year_mid", "Your security notice references 2024 filings."),
        ("year_high", "Login policy for 2099 renewal was updated."),
        (
            "zip_keyword",
            "Your security alert: shipping zip 94105 changed.",
        ),
        (
            "zip_plus_four",
            "Verification receipt for billing postal 94105-1234.",
        ),
        (
            "zip_state_form",
            "Security notice: delivered to CA 94105 today.",
        ),
        (
            "phone_paren",
            "For your security, call +1 (415) 555-2671 now.",
        ),
        (
            "phone_dashes",
            "Verification hotline is 415-555-2671 today.",
        ),
        (
            "phone_dotted",
            "Security desk verification line: 415.555.2671.",
        ),
        (
            "price_cents",
            "Your security deposit of $4521 was received.",
        ),
        (
            "price_big",
            "Verification code total charged: $123456.00 today.",
        ),
        (
            "price_thousands",
            "Security invoice for $1,234.56 is verified.",
        ),
        (
            "url_path",
            "Verify at https://example.com/confirm/887766 to proceed.",
        ),
        (
            "url_query",
            "Login via https://example.com/x?token=445566&n=1 please.",
        ),
        (
            "url_www",
            "Your security link is www.example.com/verify/998877 today.",
        ),
        (
            "credit_card_spaced",
            "Your card 4111 1111 1111 1111 security code is stored.",
        ),
        (
            "credit_card_dashed",
            "Verification: card 4111-1111-1111-1111 on file.",
        ),
    ];

    for (label, text) in cases {
        let input = message_with_body(text);
        assert!(
            best_otp_candidate(&input).is_none(),
            "{label} should not produce an OTP candidate (text: {text:?})"
        );
        assert!(
            extract_otp_candidates(&input).is_empty(),
            "{label} should not leak any candidate through extraction"
        );
    }
}

#[test]
fn best_candidate_survives_adjacent_false_positives() {
    // The real OTP must win even when the message also contains a year, a phone
    // number, a price, a ZIP, or URL-embedded digits.
    let cases = [
        (
            "year_noise",
            "Your verification code is 553311. Copyright 2024 Acme Inc.",
            "553311",
        ),
        (
            "phone_noise",
            "Enter code 662244 to sign in. Support: +1 (415) 555-2671.",
            "662244",
        ),
        (
            "price_noise",
            "Your login code: 771155. Order total was $19.99 shipped.",
            "771155",
        ),
        (
            "zip_noise",
            "Security code 443322. Shipping to zip 94105-1234 confirmed.",
            "443322",
        ),
        (
            "url_noise",
            "Verification code 889900. Track at https://example.com/p/123123.",
            "889900",
        ),
    ];

    for (label, text, expected) in cases {
        let input = message_with_body(text);
        let candidate =
            best_otp_candidate(&input).unwrap_or_else(|| panic!("{label} should extract a code"));
        assert_eq!(candidate.code, expected, "{label} picked the wrong number");
        assert!(
            extract_otp_candidates(&input)
                .iter()
                .all(|c| c.code == expected),
            "{label} leaked a false-positive candidate alongside the real code"
        );
    }
}

#[test]
fn best_candidate_reassembles_zero_width_digits() {
    let input = message_with_body(
        "Your verification code is 1\u{200B}2\u{200C}3\u{200D}4\u{FEFF}5\u{2060}6.",
    );

    let candidate = best_otp_candidate(&input).expect("zero-width code");

    assert_eq!(candidate.code, "123456");
}

#[test]
fn best_candidate_dedups_by_code_and_prefers_subject() {
    let input = OtpMessage {
        sender_email: Some("security@example.com"),
        recipient_email: Some("user@example.com"),
        page_origin: Some("example.com"),
        subject: "Verification code 123456",
        body_text: "Use code 123456 to sign in.",
        body_html: "",
        received_at: received_at(),
        now: received_at() + chrono::Duration::minutes(1),
    };

    let candidate = best_otp_candidate(&input).expect("subject code");

    assert_eq!(candidate.code, "123456");
    assert_eq!(candidate.source, OtpSource::Subject);
}

#[test]
fn best_candidate_expires_after_ten_minutes() {
    let input = OtpMessage {
        sender_email: Some("security@example.com"),
        recipient_email: Some("user@example.com"),
        page_origin: Some("example.com"),
        subject: "Verification code 123456",
        body_text: "",
        body_html: "",
        received_at: received_at(),
        now: received_at() + chrono::Duration::minutes(11),
    };

    assert!(best_otp_candidate(&input).is_none());
}

#[test]
fn best_candidate_requires_domain_binding_when_origin_is_present() {
    let input = OtpMessage {
        sender_email: Some("security@paypa1.com"),
        recipient_email: Some("user@example.com"),
        page_origin: Some("https://paypal.com/signin"),
        subject: "Verification code 123456",
        body_text: "",
        body_html: "",
        received_at: received_at(),
        now: received_at() + chrono::Duration::minutes(1),
    };

    assert!(best_otp_candidate(&input).is_none());
}

#[test]
fn domain_binding_documents_forwarded_alias_shortener_subdomain_and_lookalike_cases() {
    let forwarded = OtpMessage {
        sender_email: Some("forwarder@gmail.com"),
        recipient_email: Some("user@example.com"),
        page_origin: Some("github.com"),
        subject: "Fwd: code",
        body_text: "Forwarded message\nFrom: GitHub <noreply@github.com>\nCode 123456",
        body_html: "",
        received_at: received_at(),
        now: received_at(),
    };
    assert_eq!(domain_binding(&forwarded), DomainBinding::ForwardedSender);

    let alias_only = OtpMessage {
        sender_email: Some("forwarder@gmail.com"),
        recipient_email: Some("user+github@gmail.com"),
        page_origin: Some("github.com"),
        subject: "Code 123456",
        body_text: "",
        body_html: "",
        received_at: received_at(),
        now: received_at(),
    };
    assert_eq!(domain_binding(&alias_only), DomainBinding::AliasOnly);

    let shortener_only = OtpMessage {
        sender_email: Some("mailer@notifications.example"),
        recipient_email: Some("user@example.com"),
        page_origin: Some("github.com"),
        subject: "Code 123456",
        body_text: "Use https://bit.ly/123456 to continue.",
        body_html: "",
        received_at: received_at(),
        now: received_at(),
    };
    assert_eq!(
        domain_binding(&shortener_only),
        DomainBinding::ShortenerOnly
    );

    let subdomain = OtpMessage {
        sender_email: Some("security@login.github.com"),
        recipient_email: Some("user@example.com"),
        page_origin: Some("github.com"),
        subject: "Code 123456",
        body_text: "",
        body_html: "",
        received_at: received_at(),
        now: received_at(),
    };
    assert_eq!(domain_binding(&subdomain), DomainBinding::SenderSubdomain);

    let lookalike = OtpMessage {
        sender_email: Some("security@paypa1.com"),
        recipient_email: Some("user@example.com"),
        page_origin: Some("paypal.com"),
        subject: "Code 123456",
        body_text: "",
        body_html: "",
        received_at: received_at(),
        now: received_at(),
    };
    assert_eq!(domain_binding(&lookalike), DomainBinding::Unrelated);
}

#[test]
fn domain_binding_covers_not_evaluated_and_trusted_sender_and_link_cases() {
    let not_evaluated = OtpMessage {
        sender_email: Some("security@github.com"),
        recipient_email: Some("user@example.com"),
        page_origin: None,
        subject: "Code 123456",
        body_text: "",
        body_html: "",
        received_at: received_at(),
        now: received_at(),
    };
    assert_eq!(domain_binding(&not_evaluated), DomainBinding::NotEvaluated);

    let sender_exact = OtpMessage {
        sender_email: Some("noreply@github.com"),
        recipient_email: Some("user@example.com"),
        page_origin: Some("https://github.com/login"),
        subject: "Code 123456",
        body_text: "",
        body_html: "",
        received_at: received_at(),
        now: received_at(),
    };
    assert_eq!(domain_binding(&sender_exact), DomainBinding::SenderExact);

    let link_exact = OtpMessage {
        sender_email: Some("relay@sendgrid.net"),
        recipient_email: Some("user@example.com"),
        page_origin: Some("github.com"),
        subject: "Code 123456",
        body_text: "Continue at https://github.com/sessions/verify to sign in.",
        body_html: "",
        received_at: received_at(),
        now: received_at(),
    };
    assert_eq!(domain_binding(&link_exact), DomainBinding::LinkExact);

    let link_subdomain = OtpMessage {
        sender_email: Some("relay@sendgrid.net"),
        recipient_email: Some("user@example.com"),
        page_origin: Some("github.com"),
        subject: "Code 123456",
        body_text: "Continue at https://auth.github.com/verify to sign in.",
        body_html: "",
        received_at: received_at(),
        now: received_at(),
    };
    assert_eq!(
        domain_binding(&link_subdomain),
        DomainBinding::LinkSubdomain
    );
}
