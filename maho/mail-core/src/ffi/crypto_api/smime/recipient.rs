//! Certificate-bound recipient selection shared by both native backends.
use super::super::validation_err;
use super::MailFfiError;
use base64::Engine as _;

fn invalid() -> MailFfiError {
    validation_err("Invalid or unusable S/MIME recipient certificate")
}

// Read one definite-length DER value without accepting truncated or indefinite data.
fn take<'a>(input: &mut &'a [u8]) -> Result<(u8, &'a [u8]), MailFfiError> {
    let tag = *input.first().ok_or_else(invalid)?;
    let first = *input.get(1).ok_or_else(invalid)?;
    let mut header = 2;
    let length = if first < 128 {
        first as usize
    } else {
        let count = (first & 127) as usize;
        if count == 0 || count > 4 {
            return Err(invalid());
        }
        let mut size = 0usize;
        for byte in input.get(2..2 + count).ok_or_else(invalid)? {
            size = size.checked_mul(256).and_then(|n| n.checked_add(*byte as usize)).ok_or_else(invalid)?;
        }
        header += count;
        size
    };
    let end = header.checked_add(length).ok_or_else(invalid)?;
    let value = input.get(header..end).ok_or_else(invalid)?;
    *input = &input[end..];
    Ok((tag, value))
}

fn value<'a>(input: &mut &'a [u8], expected: u8) -> Result<&'a [u8], MailFfiError> {
    let (tag, bytes) = take(input)?;
    if tag != expected { return Err(invalid()); }
    Ok(bytes)
}

fn time(input: &mut &[u8]) -> Result<i64, MailFfiError> {
    let (tag, bytes) = take(input)?;
    let text = std::str::from_utf8(bytes).map_err(|_| invalid())?;
    let full = match tag {
        0x17 if text.len() == 13 => {
            let year = text.get(..2).ok_or_else(invalid)?.parse::<u32>().map_err(|_| invalid())?;
            format!("{}{text}", if year >= 50 { "19" } else { "20" })
        }
        0x18 if text.len() == 15 => text.to_owned(),
        _ => return Err(invalid()),
    };
    chrono::NaiveDateTime::parse_from_str(&full, "%Y%m%d%H%M%SZ")
        .map(|date| date.and_utc().timestamp()).map_err(|_| invalid())
}

pub(in crate::ffi::crypto_api) fn mailbox(email: &str) -> bool {
    let Some((local, domain)) = email.split_once('@') else { return false; };
    !local.is_empty() && !domain.is_empty() && email.is_ascii()
        && !local.starts_with('.') && !local.ends_with('.') && !local.contains("..")
        && local.bytes().all(|b| b.is_ascii_alphanumeric() || b".!#$%&'+-/=?^_`{|}~".contains(&b))
        && domain.split('.').all(|label| !label.is_empty() && !label.starts_with('-')
            && !label.ends_with('-') && label.bytes().all(|b| b.is_ascii_alphanumeric() || b == b'-'))
}

pub(in crate::ffi::crypto_api) fn emails(pem: &str) -> Result<Vec<String>, MailFfiError> {
    // Platform parser validates the complete certificate before inspecting its DER.
    super::backend().certificate_fingerprint(pem)?;
    let encoded = pem.split_once("-----BEGIN CERTIFICATE-----").and_then(|(_, tail)|
        tail.split_once("-----END CERTIFICATE-----").map(|(data, _)| data)).ok_or_else(invalid)?;
    let der = base64::prelude::BASE64_STANDARD.decode(encoded.chars().filter(|c| !c.is_ascii_whitespace()).collect::<String>()).map_err(|_| invalid())?;
    let mut outer = der.as_slice();
    let mut certificate = value(&mut outer, 0x30)?;
    let mut tbs = value(&mut certificate, 0x30)?;
    if tbs.first() == Some(&0xa0) { take(&mut tbs)?; }
    value(&mut tbs, 0x02)?; // serial
    value(&mut tbs, 0x30)?; // signature algorithm
    value(&mut tbs, 0x30)?; // issuer
    let mut validity = value(&mut tbs, 0x30)?;
    let before = time(&mut validity)?;
    let after = time(&mut validity)?;
    let now = chrono::Utc::now().timestamp();
    if now < before || now > after { return Err(invalid()); }
    let mut subject = value(&mut tbs, 0x30)?;
    let mut subject_emails = Vec::new();
    while !subject.is_empty() {
        let mut set = value(&mut subject, 0x31)?;
        while !set.is_empty() {
            let mut attribute = value(&mut set, 0x30)?;
            let oid = value(&mut attribute, 0x06)?;
            let (tag, bytes) = take(&mut attribute)?;
            if oid == b"\x2a\x86\x48\x86\xf7\x0d\x01\x09\x01" && tag == 0x16 {
                subject_emails.push(std::str::from_utf8(bytes).map_err(|_| invalid())?.to_owned());
            }
        }
    }
    value(&mut tbs, 0x30)?; // public key
    let mut names = Vec::new();
    let mut san_present = false;
    let mut seen = Vec::new();
    while !tbs.is_empty() {
        let (tag, mut bytes) = take(&mut tbs)?;
        if tag != 0xa3 { continue; }
        let mut extensions = value(&mut bytes, 0x30)?;
        while !extensions.is_empty() {
            let mut extension = value(&mut extensions, 0x30)?;
            let oid = value(&mut extension, 0x06)?;
            if seen.contains(&oid) { return Err(invalid()); }
            seen.push(oid);
            if extension.first() == Some(&0x01) { value(&mut extension, 0x01)?; }
            let mut payload = value(&mut extension, 0x04)?;
            match oid {
                b"\x55\x1d\x11" => {
                    san_present = true;
                    let mut sans = value(&mut payload, 0x30)?;
                    while !sans.is_empty() {
                        let (tag, bytes) = take(&mut sans)?;
                        if tag == 0x81 { names.push(std::str::from_utf8(bytes).map_err(|_| invalid())?.to_owned()); }
                    }
                }
                b"\x55\x1d\x0f" => {
                    let bits = value(&mut payload, 0x03)?;
                    if bits.len() < 2 || bits[0] > 7 || bits[1] & 0x20 == 0 { return Err(invalid()); }
                }
                b"\x55\x1d\x25" => {
                    let mut purposes = value(&mut payload, 0x30)?;
                    let mut email = false;
                    while !purposes.is_empty() {
                        email |= value(&mut purposes, 0x06)? == b"\x2b\x06\x01\x05\x05\x07\x03\x04";
                    }
                    if !email { return Err(invalid()); }
                }
                b"\x55\x1d\x13" => {
                    let mut constraints = value(&mut payload, 0x30)?;
                    if constraints.first() == Some(&0x01) && value(&mut constraints, 0x01)? != [0] { return Err(invalid()); }
                }
                _ => {}
            }
        }
    }
    if !san_present { names = subject_emails; }
    names.retain(|name| mailbox(name));
    Ok(names)
}
