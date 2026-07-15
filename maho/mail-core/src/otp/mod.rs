// Copyright 2026 Maho Browser. All rights reserved.

mod domain;
mod extract;

pub use domain::{domain_binding, DomainBinding};
pub use extract::extract_otp_candidates;

use chrono::{DateTime, Utc};

#[derive(Debug, Clone, Copy, PartialEq, Eq, serde::Serialize)]
#[serde(rename_all = "snake_case")]
pub enum OtpSource {
    Subject,
    BodyText,
    BodyHtml,
}

#[derive(Debug, Clone, serde::Serialize)]
pub struct OtpCandidate {
    pub code: String,
    pub source: OtpSource,
    pub binding: DomainBinding,
    pub confidence: u16,
    pub expires_at: DateTime<Utc>,
    pub evidence: String,
}

#[derive(Debug, Clone, Copy)]
pub struct OtpMessage<'a> {
    pub sender_email: Option<&'a str>,
    pub recipient_email: Option<&'a str>,
    pub page_origin: Option<&'a str>,
    pub subject: &'a str,
    pub body_text: &'a str,
    pub body_html: &'a str,
    pub received_at: DateTime<Utc>,
    pub now: DateTime<Utc>,
}

pub fn best_otp_candidate(input: &OtpMessage<'_>) -> Option<OtpCandidate> {
    let mut candidates = extract_otp_candidates(input);
    candidates.sort_by(|left, right| {
        right
            .confidence
            .cmp(&left.confidence)
            .then_with(|| source_rank(right.source).cmp(&source_rank(left.source)))
            .then_with(|| left.code.cmp(&right.code))
    });
    candidates.into_iter().next()
}

pub(crate) const fn source_rank(source: OtpSource) -> u16 {
    match source {
        OtpSource::Subject => 3,
        OtpSource::BodyText => 2,
        OtpSource::BodyHtml => 1,
    }
}

#[cfg(test)]
#[allow(clippy::disallowed_methods)]
mod tests;
