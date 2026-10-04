//! Contacts capability (`maho contacts search/resolve`).
//!
//! SECURITY CONTRACT: the macOS backend is read-only and authorizationStatus-only —
//! access request is never made, so these commands can never trigger
//! a TCC prompt. Unauthorized processes get the typed `PermissionRequired` error with
//! the System Settings deep link instead.
//!
//! Non-macOS targets return the honest `Unsupported` error (no fake data).

use crate::error::PlatformError;
#[cfg(target_os = "macos")]
use crate::error::DEEP_LINK_CONTACTS;
#[cfg(not(target_os = "macos"))]
use crate::error::NON_MACOS_REMEDIATION;
use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub struct Contact {
    pub identifier: String,
    pub name: String,
    pub phones: Vec<String>,
    pub emails: Vec<String>,
}

#[cfg(any(target_os = "macos", test))]
pub(crate) fn filter_contacts(contacts: &[Contact], query: &str) -> Vec<Contact> {
    let q = query.to_lowercase();
    contacts
        .iter()
        .filter(|c| contact_matches_query(c, &q))
        .cloned()
        .collect()
}

#[cfg(any(target_os = "macos", test))]
fn contact_matches_query(contact: &Contact, lowered_query: &str) -> bool {
    let mut hay = String::with_capacity(64);
    hay.push_str(&contact.name);
    hay.push(' ');
    for p in &contact.phones {
        hay.push_str(p);
        hay.push(' ');
    }
    for e in &contact.emails {
        hay.push_str(e);
        hay.push(' ');
    }
    hay.to_lowercase().contains(lowered_query)
}

#[cfg(any(target_os = "macos", test))]
fn matches_handle(contact: &Contact, handle: &str) -> bool {
    let h = handle.trim();
    if h.contains('@') {
        // Email handles: case-insensitive exact match.
        return contact.emails.iter().any(|e| e.eq_ignore_ascii_case(h));
    }
    // Phone handles: carriers format numbers with spaces/dashes/parens and
    // may add a country code, so compare digit-normalized strings. Suffix
    // matching on both sides covers the country-code case while the minimum
    // digit-length guard (7) keeps short handles from over-matching.
    let h_digits: String = h.chars().filter(|c| c.is_ascii_digit()).collect();
    if h_digits.len() < 7 {
        return contact.phones.iter().any(|p| p == h);
    }
    contact.phones.iter().any(|p| {
        if p == h {
            return true;
        }
        let p_digits: String = p.chars().filter(|c| c.is_ascii_digit()).collect();
        !p_digits.is_empty()
            && (p_digits == h_digits
                || p_digits.ends_with(&h_digits)
                || h_digits.ends_with(&p_digits))
    })
}

#[cfg(any(target_os = "macos", test))]
fn resolve_from(contacts: &[Contact], handles: &[String]) -> Vec<Contact> {
    let mut out: Vec<Contact> = Vec::new();
    for c in contacts {
        if handles.iter().any(|h| matches_handle(c, h))
            && !out.iter().any(|o| o.identifier == c.identifier)
        {
            out.push(c.clone());
        }
    }
    out
}

pub fn search(query: &str) -> Result<Vec<Contact>, PlatformError> {
    #[cfg(target_os = "macos")]
    {
        search_with(query, |name| {
            let predicate = name.map(|name| mac::predicate_matching_name(&mac::ns_string(name)));
            mac_fetch(predicate.as_deref())
        })
    }
    #[cfg(not(target_os = "macos"))]
    {
        let _ = query;
        Err(PlatformError::Unsupported {
            platform: std::env::consts::OS.to_string(),
            capability: "contacts".to_string(),
            remediation: NON_MACOS_REMEDIATION.to_string(),
        })
    }
}

#[cfg(any(target_os = "macos", test))]
fn search_with(
    query: &str,
    fetch: impl FnOnce(Option<&str>) -> Result<Vec<Contact>, PlatformError>,
) -> Result<Vec<Contact>, PlatformError> {
    if query.trim().is_empty() {
        return Ok(Vec::new());
    }
    // Name-only predicates discard supported phone/email substring matches.
    let contacts = fetch(None)?;
    Ok(filter_contacts(&contacts, query))
}

pub fn resolve(handles: &[String]) -> Result<Vec<Contact>, PlatformError> {
    #[cfg(target_os = "macos")]
    {
        let mut found: Vec<Contact> = Vec::new();
        for handle in handles {
            let handle = handle.trim();
            if handle.is_empty() {
                continue;
            }
            // Handle-type heuristic: '@' means email, anything else a phone number.
            let contacts = if handle.contains('@') {
                let ns = mac::ns_string(handle);
                mac::predicate_matching_email(&ns)
            } else {
                // SAFETY: factory creates a new value-object; no shared state.
                let number = unsafe {
                    objc2_contacts::CNPhoneNumber::phoneNumberWithStringValue(&mac::ns_string(
                        handle,
                    ))
                }
                .expect("CNPhoneNumber factory must return a value");
                mac::predicate_matching_phone(&number)
            };
            let contacts = mac_fetch(Some(&contacts))?;
            for c in resolve_from(&contacts, std::slice::from_ref(&handle.to_string())) {
                if !found.iter().any(|f| f.identifier == c.identifier) {
                    found.push(c);
                }
            }
        }
        Ok(found)
    }
    #[cfg(not(target_os = "macos"))]
    {
        let _ = handles;
        Err(PlatformError::Unsupported {
            platform: std::env::consts::OS.to_string(),
            capability: "contacts".to_string(),
            remediation: NON_MACOS_REMEDIATION.to_string(),
        })
    }
}

// ---------------------------------------------------------------------------
// macOS backend (Contacts framework, read-only)
// ---------------------------------------------------------------------------

#[cfg(target_os = "macos")]
mod mac {
    use super::{Contact, PlatformError, DEEP_LINK_CONTACTS};
    use objc2::rc::Retained;
    use objc2::runtime::ProtocolObject;
    use objc2_contacts::{
        CNAuthorizationStatus, CNContact, CNContactStore, CNEntityType, CNKeyDescriptor,
    };
    use objc2_foundation::{NSArray, NSPredicate, NSString};

    fn key(k: &'static NSString) -> Retained<NSString> {
        // SAFETY: framework key constants are process-wide live NSString objects.
        unsafe { Retained::retain(std::ptr::from_ref(k).cast_mut()) }
            .expect("framework key constant must be a live object")
    }

    fn keys_to_fetch() -> Retained<NSArray<ProtocolObject<dyn CNKeyDescriptor>>> {
        let k_identifier = key(unsafe { objc2_contacts::CNContactIdentifierKey });
        let k_given = key(unsafe { objc2_contacts::CNContactGivenNameKey });
        let k_family = key(unsafe { objc2_contacts::CNContactFamilyNameKey });
        let k_org = key(unsafe { objc2_contacts::CNContactOrganizationNameKey });
        let k_phone = key(unsafe { objc2_contacts::CNContactPhoneNumbersKey });
        let k_email = key(unsafe { objc2_contacts::CNContactEmailAddressesKey });
        let objs: Vec<Retained<ProtocolObject<dyn CNKeyDescriptor>>> = vec![
            ProtocolObject::from_retained(k_identifier),
            ProtocolObject::from_retained(k_given),
            ProtocolObject::from_retained(k_family),
            ProtocolObject::from_retained(k_org),
            ProtocolObject::from_retained(k_phone),
            ProtocolObject::from_retained(k_email),
        ];
        NSArray::from_retained_slice(&objs)
    }

    pub(super) fn ns_string(s: &str) -> Retained<NSString> {
        NSString::from_str(s)
    }

    pub(super) fn predicate_matching_name(name: &NSString) -> Retained<NSPredicate> {
        // SAFETY: class-method call returning a new predicate object.
        unsafe { CNContact::predicateForContactsMatchingName(name) }
    }

    pub(super) fn predicate_matching_email(email: &NSString) -> Retained<NSPredicate> {
        // SAFETY: class-method call returning a new predicate object.
        unsafe { CNContact::predicateForContactsMatchingEmailAddress(email) }
    }

    pub(super) fn predicate_matching_phone(
        number: &objc2_contacts::CNPhoneNumber,
    ) -> Retained<NSPredicate> {
        // SAFETY: class-method call returning a new predicate object.
        unsafe { CNContact::predicateForContactsMatchingPhoneNumber(number) }
    }

    pub(super) fn permission_error() -> PlatformError {
        PlatformError::PermissionRequired {
            scope: "contacts".to_string(),
            deep_link: DEEP_LINK_CONTACTS.to_string(),
        }
    }

    pub(super) fn authorized() -> bool {
        // SAFETY: plain class-method call with no side effects.
        let status =
            unsafe { CNContactStore::authorizationStatusForEntityType(CNEntityType::Contacts) };
        status == CNAuthorizationStatus::Authorized
    }

    fn ns_err_text(err: &objc2_foundation::NSError) -> String {
        err.localizedDescription().to_string()
    }

    pub(super) fn fetch(predicate: Option<&NSPredicate>) -> Result<Vec<Contact>, PlatformError> {
        // SAFETY: read-only store usage; no access request is ever made.
        let store = unsafe { CNContactStore::new() };
        let keys = keys_to_fetch();
        let array = if let Some(predicate) = predicate {
            // SAFETY: read-only fetch; predicate and keys are live objects.
            unsafe { store.unifiedContactsMatchingPredicate_keysToFetch_error(predicate, &keys) }
                .map_err(|e| PlatformError::Io(ns_err_text(&e)))?
        } else {
            use objc2::AnyThread;
            use objc2_contacts::CNContactFetchRequest;
            // SAFETY: a nil predicate enumerates authorized contacts read-only.
            let request = unsafe {
                CNContactFetchRequest::initWithKeysToFetch(CNContactFetchRequest::alloc(), &keys)
            };
            let result = unsafe { store.enumeratorForContactFetchRequest_error(&request) }
                .map_err(|e| PlatformError::Io(ns_err_text(&e)))?;
            unsafe { result.value() }.allObjects()
        };

        let mut out = Vec::with_capacity(array.len());
        for c in array.iter() {
            // SAFETY: plain property reads.
            let given = unsafe { c.givenName() }.to_string();
            let family = unsafe { c.familyName() }.to_string();
            let org = unsafe { c.organizationName() }.to_string();
            let mut name = format!("{given} {family}");
            if name.trim().is_empty() {
                name = org;
            }
            let mut phones = Vec::new();
            let labels = unsafe { c.phoneNumbers() };
            for lv in labels.iter() {
                // SAFETY: plain property reads.
                let number = unsafe { lv.value().stringValue() };
                phones.push(number.to_string());
            }
            let mut emails = Vec::new();
            let labels = unsafe { c.emailAddresses() };
            for lv in labels.iter() {
                // SAFETY: plain property read.
                emails.push(unsafe { lv.value() }.to_string());
            }
            out.push(Contact {
                identifier: unsafe { c.identifier() }.to_string(),
                name,
                phones,
                emails,
            });
        }
        Ok(out)
    }
}

#[cfg(target_os = "macos")]
fn mac_fetch(
    predicate: Option<&objc2_foundation::NSPredicate>,
) -> Result<Vec<Contact>, PlatformError> {
    if !mac::authorized() {
        return Err(mac::permission_error());
    }
    mac::fetch(predicate)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn sample() -> Vec<Contact> {
        vec![
            Contact {
                identifier: "id-1".to_string(),
                name: "Indo Yoon".to_string(),
                phones: vec!["+82 10 1234 5678".to_string()],
                emails: vec!["admin@mahobrowser.com".to_string()],
            },
            Contact {
                identifier: "id-2".to_string(),
                name: "Jane Doe".to_string(),
                phones: vec!["+1 555 0100".to_string()],
                emails: vec!["jane@example.org".to_string()],
            },
        ]
    }

    #[test]
    fn search_fetches_phone_and_email_candidates() {
        for query in ["555", "example.org"] {
            let found = search_with(query, |name| {
                Ok(sample()
                    .into_iter()
                    .filter(|contact| {
                        name.is_none_or(|name| {
                            contact.name.to_lowercase().contains(&name.to_lowercase())
                        })
                    })
                    .collect())
            })
            .unwrap();
            assert_eq!(found.len(), 1, "missing contact for {query}");
            assert_eq!(found[0].identifier, "id-2");
        }
    }

    #[test]
    fn filter_matches_name_case_insensitive() {
        let got = filter_contacts(&sample(), "indo");
        assert_eq!(got.len(), 1);
        assert_eq!(got[0].identifier, "id-1");
    }

    #[test]
    fn filter_matches_phone_substring() {
        let got = filter_contacts(&sample(), "555");
        assert_eq!(got.len(), 1);
        assert_eq!(got[0].identifier, "id-2");
    }

    #[test]
    fn filter_matches_email() {
        let got = filter_contacts(&sample(), "mahobrowser.com");
        assert_eq!(got.len(), 1);
        assert_eq!(got[0].identifier, "id-1");
    }

    #[test]
    fn filter_empty_query_returns_all() {
        assert_eq!(filter_contacts(&sample(), "").len(), 2);
    }

    #[test]
    fn filter_no_match_returns_empty() {
        assert!(filter_contacts(&sample(), "zzz-not-there").is_empty());
    }

    #[test]
    #[cfg(any(target_os = "macos", test))]
    fn resolve_from_exact_handle_match() {
        let got = resolve_from(&sample(), &["+1 555 0100".to_string()]);
        assert_eq!(got.len(), 1);
        assert_eq!(got[0].identifier, "id-2");
    }

    #[test]
    fn resolve_from_formatted_phone_matches_digits_only() {
        // Framework returns "(555) 010-0100" but the caller passes E.164 digits.
        let mut c = sample()[1].clone();
        c.phones = vec!["(555) 010-0100".to_string()];
        let got = resolve_from(std::slice::from_ref(&c), &["+15550100100".to_string()]);
        assert_eq!(got.len(), 1);
        assert_eq!(got[0].identifier, "id-2");
    }

    #[test]
    fn resolve_from_email_case_insensitive() {
        let got = resolve_from(&sample(), &["Admin@MahoBrowser.com".to_string()]);
        assert_eq!(got.len(), 1);
        assert_eq!(got[0].identifier, "id-1");
    }

    #[test]
    fn resolve_from_short_numeric_handle_no_overmatch() {
        // 3-digit handles must not suffix-match long phone numbers.
        assert!(resolve_from(&sample(), &["100".to_string()]).is_empty());
    }

    #[test]
    #[cfg(any(target_os = "macos", test))]
    fn resolve_from_no_match_returns_empty() {
        assert!(resolve_from(&sample(), &["+1 000 0000".to_string()]).is_empty());
    }

    #[test]
    fn serialization_round_trip() {
        let c = &sample()[0];
        let json = serde_json::to_string(c).expect("serialize");
        let back: Contact = serde_json::from_str(&json).expect("deserialize");
        assert_eq!(*c, back);
    }
}
