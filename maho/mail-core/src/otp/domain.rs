// Copyright 2026 Maho Browser. All rights reserved.

use std::sync::LazyLock;

use regex::Regex;

use super::OtpMessage;

#[derive(Debug, Clone, Copy, PartialEq, Eq, serde::Serialize)]
#[serde(rename_all = "snake_case")]
pub enum DomainBinding {
    NotEvaluated,
    SenderExact,
    SenderSubdomain,
    ForwardedSender,
    LinkExact,
    LinkSubdomain,
    AliasOnly,
    ShortenerOnly,
    Unrelated,
}

impl DomainBinding {
    pub const fn is_trusted(self) -> bool {
        match self {
            Self::SenderExact
            | Self::SenderSubdomain
            | Self::ForwardedSender
            | Self::LinkExact
            | Self::LinkSubdomain => true,
            Self::NotEvaluated | Self::AliasOnly | Self::ShortenerOnly | Self::Unrelated => false,
        }
    }
}

static URL_RE: LazyLock<Option<Regex>> =
    LazyLock::new(|| Regex::new(r#"(?i)\b(?:https?://|www\.)[^\s<>\"']+"#).ok());

static FORWARDED_FROM_RE: LazyLock<Option<Regex>> = LazyLock::new(|| {
    Regex::new(r#"(?im)^(?:from|original-from):\s*(?:[^<\n]*<)?([^>\s]+@[^>\s]+)>?"#).ok()
});

const SHORTENER_DOMAINS: &[&str] = &[
    "bit.ly",
    "t.co",
    "tinyurl.com",
    "goo.gl",
    "ow.ly",
    "lnkd.in",
    "shorturl.at",
];

pub fn domain_binding(input: &OtpMessage<'_>) -> DomainBinding {
    let Some(origin) = input.page_origin.and_then(normalize_domain) else {
        return DomainBinding::NotEvaluated;
    };

    if let Some(binding) = input
        .sender_email
        .and_then(sender_domain)
        .and_then(|sender| sender_binding(&origin, &sender))
    {
        return binding;
    }

    if forwarded_sender_matches(input, &origin) {
        return DomainBinding::ForwardedSender;
    }

    let mut shortener_seen = false;
    for domain in link_domains(input.body_text).chain(link_domains(input.body_html)) {
        if is_shortener(&domain) {
            shortener_seen = true;
            continue;
        }
        match relation(&origin, &domain) {
            DomainRelation::Exact => return DomainBinding::LinkExact,
            DomainRelation::Subdomain => return DomainBinding::LinkSubdomain,
            DomainRelation::Unrelated => {}
        }
    }

    if recipient_alias_mentions_origin(input.recipient_email, &origin) {
        return DomainBinding::AliasOnly;
    }
    if shortener_seen {
        return DomainBinding::ShortenerOnly;
    }
    DomainBinding::Unrelated
}

pub(crate) fn url_ranges(text: &str) -> Vec<std::ops::Range<usize>> {
    URL_RE.as_ref().map_or_else(Vec::new, |re| {
        re.find_iter(text).map(|m| m.start()..m.end()).collect()
    })
}

fn link_domains(text: &str) -> impl Iterator<Item = String> + '_ {
    URL_RE
        .as_ref()
        .into_iter()
        .flat_map(move |re| re.find_iter(text))
        .filter_map(|m| normalize_domain(m.as_str()))
}

fn forwarded_sender_matches(input: &OtpMessage<'_>, origin: &str) -> bool {
    let Some(re) = FORWARDED_FROM_RE.as_ref() else {
        return false;
    };
    re.captures_iter(input.body_text)
        .chain(re.captures_iter(input.body_html))
        .filter_map(|captures| captures.get(1))
        .filter_map(|email| sender_domain(email.as_str()))
        .any(|domain| !matches!(relation(origin, &domain), DomainRelation::Unrelated))
}

fn sender_binding(origin: &str, sender: &str) -> Option<DomainBinding> {
    match relation(origin, sender) {
        DomainRelation::Exact => Some(DomainBinding::SenderExact),
        DomainRelation::Subdomain => Some(DomainBinding::SenderSubdomain),
        DomainRelation::Unrelated => None,
    }
}

fn normalize_domain(raw: &str) -> Option<String> {
    let trimmed = raw
        .trim()
        .trim_matches(|c: char| matches!(c, '<' | '>' | ',' | ';'));
    let without_scheme = trimmed.split_once("://").map_or(trimmed, |(_, rest)| rest);
    let hostish = without_scheme
        .split(['/', '?', '#'])
        .next()
        .unwrap_or(without_scheme);
    let after_at = hostish.rsplit('@').next().unwrap_or(hostish);
    let without_port = after_at.split(':').next().unwrap_or(after_at);
    let domain = without_port
        .trim_matches('.')
        .strip_prefix("www.")
        .unwrap_or(without_port.trim_matches('.'));
    if domain.is_empty() || !domain.is_ascii() || !domain.contains('.') {
        return None;
    }
    Some(domain.to_ascii_lowercase())
}

fn sender_domain(raw: &str) -> Option<String> {
    let inner = raw
        .split_once('<')
        .and_then(|(_, rest)| rest.split_once('>').map(|(email, _)| email))
        .unwrap_or(raw);
    let domain = inner.rsplit_once('@')?.1;
    normalize_domain(domain)
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum DomainRelation {
    Exact,
    Subdomain,
    Unrelated,
}

fn relation(origin: &str, candidate: &str) -> DomainRelation {
    if origin == candidate {
        return DomainRelation::Exact;
    }
    if has_registrable_shape(origin)
        && has_registrable_shape(candidate)
        && (origin.ends_with(&format!(".{candidate}"))
            || candidate.ends_with(&format!(".{origin}")))
    {
        return DomainRelation::Subdomain;
    }
    DomainRelation::Unrelated
}

fn has_registrable_shape(domain: &str) -> bool {
    let labels = domain.split('.').count();
    labels >= 2 && !matches!(domain, "com" | "net" | "org" | "co.uk" | "co.kr")
}

fn is_shortener(domain: &str) -> bool {
    SHORTENER_DOMAINS.contains(&domain)
}

fn recipient_alias_mentions_origin(recipient: Option<&str>, origin: &str) -> bool {
    let Some(local) = recipient.and_then(|email| email.split_once('@').map(|(local, _)| local))
    else {
        return false;
    };
    let Some(alias) = local
        .split_once('+')
        .map(|(_, alias)| alias.to_ascii_lowercase())
    else {
        return false;
    };
    let first_label = origin.split('.').next().unwrap_or(origin);
    alias.contains(first_label)
}
