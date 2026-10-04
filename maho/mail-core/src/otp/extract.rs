// Copyright 2026 Maho Browser. All rights reserved.

use std::collections::BTreeMap;
use std::sync::LazyLock;

use chrono::Duration;
use regex::Regex;

use super::domain::{domain_binding, url_ranges, DomainBinding};
use super::{source_rank, OtpCandidate, OtpMessage, OtpSource};

const OTP_TTL: Duration = Duration::minutes(10);

static CANDIDATE_RE: LazyLock<Option<Regex>> =
    LazyLock::new(|| Regex::new(r"[0-9][0-9\-\s]{2,30}[0-9]").ok());

static PHONE_RE: LazyLock<Option<Regex>> = LazyLock::new(|| {
    Regex::new(r"(?x)(?:\+?1[\s.-]?)?(?:\([2-9][0-9]{2}\)|[2-9][0-9]{2})[\s.-]?[2-9][0-9]{2}[\s.-]?[0-9]{4}")
        .ok()
});

static PRICE_RE: LazyLock<Option<Regex>> = LazyLock::new(|| {
    Regex::new(r"(?i)(?:\$\s*|usd\s+)[0-9]{1,6}(?:,[0-9]{3})*(?:\.[0-9]{2})?").ok()
});

static ZIP_RE: LazyLock<Option<Regex>> = LazyLock::new(|| {
    Regex::new(r"(?i)\b(?:zip|postal|postcode|shipping|billing|address)\D{0,16}[0-9]{5}(?:-[0-9]{4})?\b|\b[A-Z]{2}\s+[0-9]{5}(?:-[0-9]{4})?\b")
        .ok()
});

pub fn extract_otp_candidates(input: &OtpMessage<'_>) -> Vec<OtpCandidate> {
    let expires_at = input.received_at + OTP_TTL;
    if input.now > expires_at {
        return Vec::new();
    }

    let binding = domain_binding(input);
    if input.page_origin.is_some() && !binding.is_trusted() {
        return Vec::new();
    }

    let mut deduped = BTreeMap::<String, OtpCandidate>::new();
    for candidate in collect_from_text(input.subject, OtpSource::Subject, binding, expires_at) {
        keep_best(&mut deduped, candidate);
    }
    for candidate in collect_from_text(input.body_text, OtpSource::BodyText, binding, expires_at) {
        keep_best(&mut deduped, candidate);
    }
    for candidate in collect_from_text(
        &html_to_text(input.body_html),
        OtpSource::BodyHtml,
        binding,
        expires_at,
    ) {
        keep_best(&mut deduped, candidate);
    }
    deduped.into_values().collect()
}

fn collect_from_text(
    text: &str,
    source: OtpSource,
    binding: DomainBinding,
    expires_at: chrono::DateTime<chrono::Utc>,
) -> Vec<OtpCandidate> {
    let normalized = strip_zero_width(text);
    let exclusions = exclusion_ranges(&normalized);
    let Some(candidate_re) = CANDIDATE_RE.as_ref() else {
        return Vec::new();
    };
    candidate_re
        .find_iter(&normalized)
        .filter_map(|m| {
            candidate_from_match(
                &normalized,
                m.start(),
                m.end(),
                &exclusions,
                source,
                binding,
                expires_at,
            )
        })
        .collect()
}

fn keep_best(candidates: &mut BTreeMap<String, OtpCandidate>, candidate: OtpCandidate) {
    match candidates.get(&candidate.code) {
        Some(existing)
            if existing.confidence > candidate.confidence
                || (existing.confidence == candidate.confidence
                    && source_rank(existing.source) >= source_rank(candidate.source)) => {}
        Some(_) | None => {
            candidates.insert(candidate.code.clone(), candidate);
        }
    }
}

fn candidate_from_match(
    text: &str,
    start: usize,
    end: usize,
    exclusions: &[std::ops::Range<usize>],
    source: OtpSource,
    binding: DomainBinding,
    expires_at: chrono::DateTime<chrono::Utc>,
) -> Option<OtpCandidate> {
    if overlaps_any(start, end, exclusions) || embedded_in_word(text, start, end) {
        return None;
    }
    let code: String = text[start..end]
        .chars()
        .filter(char::is_ascii_digit)
        .collect();
    if !(4..=8).contains(&code.len()) || is_year(&code) || !has_otp_context(text, start, end) {
        return None;
    }
    let confidence = confidence_for(source, binding, code.len());
    Some(OtpCandidate {
        code,
        source,
        binding,
        confidence,
        expires_at,
        evidence: evidence(text, start, end),
    })
}

fn strip_zero_width(text: &str) -> String {
    text.chars()
        .filter(|c| {
            !matches!(
                c,
                '\u{200B}' | '\u{200C}' | '\u{200D}' | '\u{FEFF}' | '\u{2060}'
            )
        })
        .collect()
}

fn html_to_text(html: &str) -> String {
    let mut out = String::with_capacity(html.len());
    let mut in_tag = false;
    for c in html.chars() {
        match c {
            '<' => in_tag = true,
            '>' => {
                in_tag = false;
                out.push(' ');
            }
            _ if !in_tag => out.push(c),
            _ => {}
        }
    }
    out
}

fn exclusion_ranges(text: &str) -> Vec<std::ops::Range<usize>> {
    let mut ranges = url_ranges(text);
    if let Some(re) = PHONE_RE.as_ref() {
        ranges.extend(re.find_iter(text).map(|m| m.start()..m.end()));
    }
    if let Some(re) = PRICE_RE.as_ref() {
        ranges.extend(re.find_iter(text).map(|m| m.start()..m.end()));
    }
    if let Some(re) = ZIP_RE.as_ref() {
        ranges.extend(re.find_iter(text).map(|m| m.start()..m.end()));
    }
    ranges
}

fn overlaps_any(start: usize, end: usize, ranges: &[std::ops::Range<usize>]) -> bool {
    ranges
        .iter()
        .any(|range| start < range.end && end > range.start)
}

fn embedded_in_word(text: &str, start: usize, end: usize) -> bool {
    let previous = start
        .checked_sub(1)
        .and_then(|idx| text.as_bytes().get(idx))
        .copied();
    let next = text.as_bytes().get(end).copied();
    previous.is_some_and(|b| b.is_ascii_alphanumeric())
        || next.is_some_and(|b| b.is_ascii_alphanumeric())
}

fn is_year(code: &str) -> bool {
    code.len() == 4
        && code
            .parse::<u16>()
            .is_ok_and(|year| (2000..=2099).contains(&year))
}

fn has_otp_context(text: &str, start: usize, end: usize) -> bool {
    let context = evidence(text, start, end).to_ascii_lowercase();
    [
        "code",
        "otp",
        "verification",
        "verify",
        "security",
        "login",
        "sign in",
        "authentication",
        "one-time",
        "2fa",
        "passcode",
    ]
    .iter()
    .any(|token| context.contains(token))
}

fn confidence_for(source: OtpSource, binding: DomainBinding, code_len: usize) -> u16 {
    let source_score = match source {
        OtpSource::Subject => 60,
        OtpSource::BodyText => 50,
        OtpSource::BodyHtml => 45,
    };
    let binding_score = match binding {
        DomainBinding::SenderExact | DomainBinding::LinkExact => 30,
        DomainBinding::SenderSubdomain
        | DomainBinding::LinkSubdomain
        | DomainBinding::ForwardedSender => 25,
        DomainBinding::NotEvaluated
        | DomainBinding::AliasOnly
        | DomainBinding::ShortenerOnly
        | DomainBinding::Unrelated => 0,
    };
    source_score + binding_score + if code_len == 6 { 5 } else { 0 }
}

fn evidence(text: &str, start: usize, end: usize) -> String {
    let left = text[..start]
        .char_indices()
        .rev()
        .nth(48)
        .map_or(0, |(idx, _)| idx);
    let right = text[end..]
        .char_indices()
        .nth(48)
        .map_or(text.len(), |(idx, _)| end + idx);
    text[left..right].trim().to_string()
}
