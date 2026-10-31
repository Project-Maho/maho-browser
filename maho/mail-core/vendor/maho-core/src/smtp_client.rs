use lettre::{
    message::{header::ContentType, Attachment, Mailbox, MultiPart, SinglePart},
    transport::smtp::{
        authentication::{Credentials, Mechanism},
        client::{SmtpConnection, TlsParameters},
        extension::ClientId,
    },
    Message, SmtpTransport,
};
use regex::Regex;
use std::time::Duration;

use crate::error::AppError;
use crate::models::account::Encryption;
use base64::Engine;

#[derive(Debug, Clone, serde::Serialize, serde::Deserialize)]
pub struct ComposeAttachment {
    pub filename: String,
    pub mime_type: String,
    /// Base64-encoded file content
    pub data: String,
}

/// A text/calendar alternative body part for iTIP messages (RFC 6047 §2).
#[derive(Debug, Clone, serde::Serialize, serde::Deserialize)]
pub struct CalendarAlternativePart {
    pub content_type: String,
    pub ics_text: String,
}

/// An inline image extracted from HTML `data:` URLs, to be embedded as a CID-referenced MIME part.
#[derive(Debug, Clone, PartialEq)]
pub struct InlineImage {
    pub content_id: String,
    pub content_type: String,
    pub data: Vec<u8>,
}

/// Parses HTML for `<img src="data:image/...;base64,...">` tags, extracts each image,
/// replaces the `src` with a `cid:` reference, and returns the modified HTML plus the
/// extracted images. Invalid base64 data is silently skipped (image tag preserved as-is).
pub fn extract_inline_images(html: &str) -> (String, Vec<InlineImage>) {
    let re =
        Regex::new(r#"(?i)<img([^>]*)\ssrc=["']data:(image/[^;]+);base64,([^"']+)["']([^>]*)>"#)
            .expect("valid regex");

    let mut images: Vec<InlineImage> = Vec::new();
    let engine = base64::engine::general_purpose::STANDARD;

    let result = re.replace_all(html, |caps: &regex::Captures| {
        let content_type = caps[2].to_string();
        let b64_data = &caps[3];

        match engine.decode(b64_data) {
            Ok(decoded) => {
                let cid = format!("img{}@maho-mail.local", uuid::Uuid::new_v4().simple());
                images.push(InlineImage {
                    content_id: cid.clone(),
                    content_type,
                    data: decoded,
                });
                format!("<img{} src=\"cid:{}\"{}> ", &caps[1], cid, &caps[4])
            }
            Err(_) => {
                // Invalid base64 — keep original tag unchanged
                caps[0].to_string()
            }
        }
    });

    (result.into_owned(), images)
}

#[derive(Clone)]
struct DispositionNotificationTo(String);

impl lettre::message::header::Header for DispositionNotificationTo {
    fn name() -> lettre::message::header::HeaderName {
        lettre::message::header::HeaderName::new_from_ascii_str("Disposition-Notification-To")
    }

    fn parse(_: &str) -> Result<Self, Box<dyn std::error::Error + Send + Sync>> {
        Err("parse not implemented".into())
    }

    fn display(&self) -> lettre::message::header::HeaderValue {
        lettre::message::header::HeaderValue::new(
            lettre::message::header::HeaderName::new_from_ascii_str("Disposition-Notification-To"),
            self.0.clone(),
        )
    }
}

#[derive(Clone)]
struct EmailReferences(String);

impl lettre::message::header::Header for EmailReferences {
    fn name() -> lettre::message::header::HeaderName {
        lettre::message::header::HeaderName::new_from_ascii_str("References")
    }

    fn parse(_: &str) -> Result<Self, Box<dyn std::error::Error + Send + Sync>> {
        Err("parse not implemented".into())
    }

    fn display(&self) -> lettre::message::header::HeaderValue {
        lettre::message::header::HeaderValue::new(
            lettre::message::header::HeaderName::new_from_ascii_str("References"),
            self.0.clone(),
        )
    }
}

fn build_transport(
    smtp_host: &str,
    smtp_port: u16,
    encryption: &Encryption,
    username: &str,
    secret: &str,
    use_xoauth2: bool,
) -> Result<SmtpTransport, AppError> {
    let creds = Credentials::new(username.to_string(), secret.to_string());

    let transport = match encryption {
        Encryption::Tls => SmtpTransport::relay(smtp_host)
            .map_err(|e| AppError::Network(e.to_string()))?
            .port(smtp_port)
            .credentials(creds)
            .authentication(if use_xoauth2 {
                vec![Mechanism::Xoauth2]
            } else {
                vec![Mechanism::Plain, Mechanism::Login]
            })
            .timeout(Some(Duration::from_secs(10)))
            .build(),
        Encryption::StartTls => SmtpTransport::starttls_relay(smtp_host)
            .map_err(|e| AppError::Network(e.to_string()))?
            .port(smtp_port)
            .credentials(creds)
            .authentication(if use_xoauth2 {
                vec![Mechanism::Xoauth2]
            } else {
                vec![Mechanism::Plain, Mechanism::Login]
            })
            .timeout(Some(Duration::from_secs(10)))
            .build(),
        Encryption::None => {
            return Err(AppError::Validation(
                "Plaintext SMTP (no encryption) is not allowed. Use TLS or STARTTLS.".into(),
            ))
        }
    };

    Ok(transport)
}

pub fn test_connection(
    smtp_host: &str,
    smtp_port: u16,
    encryption: &Encryption,
    username: &str,
    password: &str,
    use_xoauth2: bool,
) -> Result<bool, AppError> {
    let transport = build_transport(
        smtp_host,
        smtp_port,
        encryption,
        username,
        password,
        use_xoauth2,
    )?;

    transport
        .test_connection()
        .map_err(|e| AppError::Network(e.to_string()))
}

#[allow(clippy::too_many_arguments)]
pub fn send_email(
    smtp_host: &str,
    smtp_port: u16,
    encryption: &Encryption,
    username: &str,
    password: &str,
    use_xoauth2: bool,
    from: &str,
    to: &[String],
    cc: &[String],
    bcc: &[String],
    subject: &str,
    body_text: Option<&str>,
    body_html: Option<&str>,
    in_reply_to: Option<&str>,
    references: Option<&str>,
    read_receipt_to: Option<&str>,
    attachments: &[ComposeAttachment],
    calendar_alternative: Option<&CalendarAlternativePart>,
) -> Result<(), AppError> {
    let from_mailbox: Mailbox = from
        .parse()
        .map_err(|e: lettre::address::AddressError| AppError::Validation(e.to_string()))?;

    let mut builder = Message::builder().from(from_mailbox).subject(subject);

    for addr in to {
        let mailbox: Mailbox = addr
            .parse()
            .map_err(|e: lettre::address::AddressError| AppError::Validation(e.to_string()))?;
        builder = builder.to(mailbox);
    }

    for addr in cc {
        let mailbox: Mailbox = addr
            .parse()
            .map_err(|e: lettre::address::AddressError| AppError::Validation(e.to_string()))?;
        builder = builder.cc(mailbox);
    }

    for addr in bcc {
        let mailbox: Mailbox = addr
            .parse()
            .map_err(|e: lettre::address::AddressError| AppError::Validation(e.to_string()))?;
        builder = builder.bcc(mailbox);
    }

    if let Some(reply_id) = in_reply_to {
        builder = builder.in_reply_to(reply_id.to_string());
    }

    if let Some(refs) = references {
        builder = builder.header(EmailReferences(refs.to_string()));
    }

    if let Some(receipt_addr) = read_receipt_to {
        builder = builder.header(DispositionNotificationTo(receipt_addr.to_string()));
    }

    let message = build_mime_message(
        builder,
        body_text,
        body_html,
        &[],
        attachments,
        calendar_alternative,
    )?;

    if matches!(encryption, Encryption::None) {
        return Err(AppError::Validation(
            "Plaintext SMTP (no encryption) is not allowed. Use TLS or STARTTLS.".into(),
        ));
    }
    let tls = TlsParameters::new(smtp_host.to_string())
        .map_err(|error| AppError::Network(error.to_string()))?;
    let hello = ClientId::default();
    let connection = SmtpConnection::connect(
        (smtp_host, smtp_port),
        Some(Duration::from_secs(10)),
        &hello,
        match encryption {
            Encryption::Tls => Some(&tls),
            Encryption::StartTls | Encryption::None => None,
        },
        None,
    ).map_err(smtp_presend_error)?;
    let mut connection = SmtpSession(connection);
    if matches!(encryption, Encryption::StartTls) {
        connection.0.starttls(&tls, &hello)
            .map_err(smtp_presend_error)?;
    }
    let mechanisms = if use_xoauth2 {
        &[Mechanism::Xoauth2][..]
    } else {
        &[Mechanism::Plain, Mechanism::Login][..]
    };
    connection.0.auth(mechanisms, &Credentials::new(username.into(), password.into()))
        .map_err(|error| {
            if error.is_transient() {
                AppError::Network(error.to_string())
            } else {
                AppError::Auth(error.to_string())
            }
        })?;
    connection.0.send(message.envelope(), &message.formatted())
        .map(|_| ())
        .map_err(|error| {
            if error.is_permanent() || error.is_client() {
                AppError::Validation(error.to_string())
            } else if error.is_transient() {
                AppError::Network(error.to_string())
            } else {
                AppError::DeliveryUncertain(error.to_string())
            }
        })
}

struct SmtpSession(SmtpConnection);

fn smtp_presend_error(error: lettre::transport::smtp::Error) -> AppError {
    if error.is_permanent() || error.is_client() {
        AppError::Validation(error.to_string())
    } else {
        AppError::Network(error.to_string())
    }
}

impl Drop for SmtpSession {
    fn drop(&mut self) {
        self.0.abort();
    }
}

enum TextBody {
    Alternative(MultiPart),
    Single(SinglePart),
}

fn build_html_part(html: &str, inline_images: &[InlineImage]) -> MultiPart {
    let html_part = SinglePart::builder()
        .header(ContentType::TEXT_HTML)
        .body(html.to_string());

    let mut related = MultiPart::related().singlepart(html_part);
    for img in inline_images {
        let ct: ContentType = img
            .content_type
            .parse()
            .unwrap_or_else(|_| ContentType::parse("application/octet-stream").unwrap());
        let inline_att = Attachment::new_inline(img.content_id.clone()).body(img.data.clone(), ct);
        related = related.singlepart(inline_att);
    }
    related
}

fn build_text_body(
    body_text: Option<&str>,
    body_html: Option<&str>,
    inline_images: &[InlineImage],
) -> TextBody {
    match (body_text, body_html) {
        (Some(text), Some(html)) => {
            let text_part = SinglePart::builder()
                .header(ContentType::TEXT_PLAIN)
                .body(text.to_string());

            if inline_images.is_empty() {
                TextBody::Alternative(
                    MultiPart::alternative().singlepart(text_part).singlepart(
                        SinglePart::builder()
                            .header(ContentType::TEXT_HTML)
                            .body(html.to_string()),
                    ),
                )
            } else {
                TextBody::Alternative(
                    MultiPart::alternative()
                        .singlepart(text_part)
                        .multipart(build_html_part(html, inline_images)),
                )
            }
        }
        (None, Some(html)) => {
            if inline_images.is_empty() {
                TextBody::Single(
                    SinglePart::builder()
                        .header(ContentType::TEXT_HTML)
                        .body(html.to_string()),
                )
            } else {
                TextBody::Alternative(build_html_part(html, inline_images))
            }
        }
        (Some(text), None) => TextBody::Single(
            SinglePart::builder()
                .header(ContentType::TEXT_PLAIN)
                .body(text.to_string()),
        ),
        (None, None) => TextBody::Single(
            SinglePart::builder()
                .header(ContentType::TEXT_PLAIN)
                .body(String::new()),
        ),
    }
}

#[allow(clippy::too_many_arguments)]
pub fn build_mime_message(
    builder: lettre::message::MessageBuilder,
    body_text: Option<&str>,
    body_html: Option<&str>,
    inline_images: &[InlineImage],
    attachments: &[ComposeAttachment],
    calendar_alternative: Option<&CalendarAlternativePart>,
) -> Result<Message, AppError> {
    match build_mime_body(body_text, body_html, inline_images, attachments, calendar_alternative)? {
        TextBody::Alternative(body) => builder.multipart(body),
        TextBody::Single(body) => builder.singlepart(body),
    }
    .map_err(|e| AppError::Validation(e.to_string()))
}

pub fn build_mime_entity(
    body_text: Option<&str>,
    body_html: Option<&str>,
    attachments: &[ComposeAttachment],
) -> Result<Vec<u8>, AppError> {
    Ok(match build_mime_body(body_text, body_html, &[], attachments, None)? {
        TextBody::Alternative(body) => body.formatted(),
        TextBody::Single(body) => body.formatted(),
    })
}

fn build_mime_body(
    body_text: Option<&str>,
    body_html: Option<&str>,
    inline_images: &[InlineImage],
    attachments: &[ComposeAttachment],
    calendar_alternative: Option<&CalendarAlternativePart>,
) -> Result<TextBody, AppError> {
    let (final_html, final_images) = match body_html {
        Some(html) => {
            let (new_html, images) = extract_inline_images(html);
            (Some(new_html), images)
        }
        None => (None, Vec::new()),
    };
    let inline_images_ref = if inline_images.is_empty() {
        &final_images[..]
    } else {
        inline_images
    };

    let text_body = build_text_body(body_text, final_html.as_deref(), inline_images_ref);

    let message = match (attachments.is_empty(), calendar_alternative) {
        (true, None) => text_body,
        (true, Some(cal)) => {
            let cal_part = build_calendar_singlepart(cal)?;
            let plain_part = SinglePart::builder()
                .header(ContentType::TEXT_PLAIN)
                .body(body_text.unwrap_or("").to_string());
            let mp = MultiPart::alternative()
                .singlepart(plain_part)
                .singlepart(cal_part);
            TextBody::Alternative(mp)
        }
        (false, None) => {
            let mut mixed = match text_body {
                TextBody::Alternative(mp) => MultiPart::mixed().multipart(mp),
                TextBody::Single(sp) => MultiPart::mixed().singlepart(sp),
            };
            let engine = base64::engine::general_purpose::STANDARD;
            for att in attachments {
                let decoded = engine.decode(&att.data).map_err(|e| {
                    AppError::Validation(format!("Invalid base64 attachment data: {}", e))
                })?;
                let content_type: ContentType = att.mime_type.parse().unwrap_or_else(|_| {
                    ContentType::parse("application/octet-stream").expect("static content type")
                });
                let file_attachment =
                    Attachment::new(att.filename.clone()).body(decoded, content_type);
                mixed = mixed.singlepart(file_attachment);
            }
            TextBody::Alternative(mixed)
        }
        (false, Some(cal)) => {
            let cal_part = build_calendar_singlepart(cal)?;
            let plain_part = SinglePart::builder()
                .header(ContentType::TEXT_PLAIN)
                .body(body_text.unwrap_or("").to_string());
            let inner = MultiPart::alternative()
                .singlepart(plain_part)
                .singlepart(cal_part);
            let mut mixed = MultiPart::mixed().multipart(inner);
            let engine = base64::engine::general_purpose::STANDARD;
            for att in attachments {
                let decoded = engine.decode(&att.data).map_err(|e| {
                    AppError::Validation(format!("Invalid base64 attachment data: {}", e))
                })?;
                let content_type: ContentType = att.mime_type.parse().unwrap_or_else(|_| {
                    ContentType::parse("application/octet-stream").expect("static content type")
                });
                let file_attachment =
                    Attachment::new(att.filename.clone()).body(decoded, content_type);
                mixed = mixed.singlepart(file_attachment);
            }
            TextBody::Alternative(mixed)
        }
    };

    Ok(message)
}

fn build_calendar_singlepart(cal: &CalendarAlternativePart) -> Result<SinglePart, AppError> {
    let ct: ContentType = cal.content_type.parse().map_err(|_| {
        AppError::Validation(format!(
            "Invalid calendar content_type: {}",
            cal.content_type
        ))
    })?;
    Ok(SinglePart::builder().header(ct).body(cal.ics_text.clone()))
}

#[cfg(test)]
mod tests {
    use super::*;
    use lettre::message::header::Header;

    #[test]
    fn test_build_text_body_both_text_and_html() {
        let body = build_text_body(Some("plain"), Some("<p>html</p>"), &[]);
        assert!(matches!(body, TextBody::Alternative(_)));
    }

    #[test]
    fn test_build_text_body_html_only() {
        let body = build_text_body(None, Some("<p>html</p>"), &[]);
        assert!(matches!(body, TextBody::Single(_)));
    }

    #[test]
    fn test_build_text_body_text_only() {
        let body = build_text_body(Some("plain"), None, &[]);
        assert!(matches!(body, TextBody::Single(_)));
    }

    #[test]
    fn test_build_text_body_neither() {
        let body = build_text_body(None, None, &[]);
        assert!(matches!(body, TextBody::Single(_)));
    }

    #[test]
    fn test_build_transport_tls() {
        let result = build_transport(
            "smtp.example.com",
            465,
            &Encryption::Tls,
            "user",
            "pass",
            false,
        );
        assert!(result.is_ok());
    }

    #[test]
    fn test_build_transport_starttls() {
        let result = build_transport(
            "smtp.example.com",
            587,
            &Encryption::StartTls,
            "user",
            "pass",
            false,
        );
        assert!(result.is_ok());
    }

    #[test]
    fn test_build_transport_none_encryption() {
        let result = build_transport(
            "smtp.example.com",
            25,
            &Encryption::None,
            "user",
            "pass",
            false,
        );
        assert!(result.is_err());
        assert!(matches!(result.unwrap_err(), AppError::Validation(_)));
    }

    #[test]
    fn test_build_transport_with_xoauth2() {
        let result = build_transport(
            "smtp.example.com",
            465,
            &Encryption::Tls,
            "user",
            "access_token",
            true,
        );
        assert!(result.is_ok());
    }

    #[test]
    fn test_disposition_notification_to_header_name() {
        let name = format!("{}", DispositionNotificationTo::name());
        assert!(name.contains("Disposition-Notification-To"));
    }

    #[test]
    fn test_send_email_rejects_invalid_from_address() {
        let result = send_email(
            "smtp.example.com",
            465,
            &Encryption::Tls,
            "user",
            "pass",
            false,
            "not-an-email",
            &["to@example.com".to_string()],
            &[],
            &[],
            "Subject",
            Some("body"),
            None,
            None,
            None,
            None,
            &[],
            None,
        );
        assert!(result.is_err());
    }

    #[test]
    fn test_extract_inline_images_no_images() {
        let html = "<p>Hello world</p>";
        let (result_html, images) = extract_inline_images(html);
        assert_eq!(result_html, html);
        assert!(images.is_empty());
    }

    #[test]
    fn test_extract_inline_images_one_png() {
        let png_b64 = base64::engine::general_purpose::STANDARD.encode(b"\x89PNG\r\n\x1a\n");
        let html = format!(
            r#"<p>Hi</p><img src="data:image/png;base64,{}" alt="test">"#,
            png_b64
        );
        let (result_html, images) = extract_inline_images(&html);

        assert_eq!(images.len(), 1);
        assert_eq!(images[0].content_type, "image/png");
        assert_eq!(images[0].data, b"\x89PNG\r\n\x1a\n");
        assert!(result_html.contains("cid:"));
        assert!(!result_html.contains("data:image"));
        assert!(result_html.contains(&format!("cid:{}", images[0].content_id)));
    }

    #[test]
    fn test_extract_inline_images_two_different_types() {
        let png_b64 = base64::engine::general_purpose::STANDARD.encode(b"fakepng");
        let jpeg_b64 = base64::engine::general_purpose::STANDARD.encode(b"fakejpeg");
        let html = format!(
            r#"<img src="data:image/png;base64,{}"><img src="data:image/jpeg;base64,{}">"#,
            png_b64, jpeg_b64
        );
        let (result_html, images) = extract_inline_images(&html);

        assert_eq!(images.len(), 2);
        assert_eq!(images[0].content_type, "image/png");
        assert_eq!(images[0].data, b"fakepng");
        assert_eq!(images[1].content_type, "image/jpeg");
        assert_eq!(images[1].data, b"fakejpeg");
        assert!(result_html.contains(&format!("cid:{}", images[0].content_id)));
        assert!(result_html.contains(&format!("cid:{}", images[1].content_id)));
        assert_ne!(images[0].content_id, images[1].content_id);
    }

    #[test]
    fn test_extract_inline_images_invalid_base64_skipped() {
        let html = r#"<img src="data:image/png;base64,!!!not-valid-base64!!!">"#;
        let (result_html, images) = extract_inline_images(html);

        assert!(images.is_empty());
        assert_eq!(result_html, html);
    }

    #[test]
    fn test_extract_inline_images_preserves_http_urls() {
        let html = r#"<img src="https://example.com/photo.png">"#;
        let (result_html, images) = extract_inline_images(html);

        assert!(images.is_empty());
        assert_eq!(result_html, html);
    }

    #[test]
    fn test_build_text_body_with_inline_images_wraps_related() {
        let images = vec![InlineImage {
            content_id: "img1@maho-mail.local".to_string(),
            content_type: "image/png".to_string(),
            data: vec![0x89, 0x50, 0x4e, 0x47],
        }];
        let body = build_text_body(
            Some("plain text"),
            Some(r#"<p><img src="cid:img1@maho-mail.local"></p>"#),
            &images,
        );
        assert!(matches!(body, TextBody::Alternative(_)));
    }

    #[test]
    fn test_full_mime_output_contains_cid_and_content_id() {
        let png_b64 = base64::engine::general_purpose::STANDARD.encode(b"PNGDATA");
        let html = format!(
            r#"<p>Hello</p><img src="data:image/png;base64,{}">"#,
            png_b64
        );

        let message = Message::builder()
            .from("sender@example.com".parse::<Mailbox>().unwrap())
            .to("recipient@example.com".parse::<Mailbox>().unwrap())
            .subject("Test inline image");

        let (processed_html, inline_images) = extract_inline_images(&html);
        let text_body = build_text_body(Some("Hello"), Some(&processed_html), &inline_images);

        let msg = match text_body {
            TextBody::Alternative(mp) => message.multipart(mp).unwrap(),
            TextBody::Single(sp) => message.singlepart(sp).unwrap(),
        };

        let raw = msg.formatted();
        let formatted = String::from_utf8_lossy(&raw);
        assert!(formatted.contains("multipart/related"));
        assert!(formatted.contains("Content-ID:"));
        assert!(formatted.contains("cid:"));
        assert!(formatted.contains("image/png"));
        assert!(!formatted.contains("data:image"));
    }
}
