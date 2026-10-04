use std::io::{self, Read, Write};
use std::net::TcpStream;
use std::sync::RwLock;
use std::time::Duration;

use imap::extensions::idle::SetReadTimeout;
use imap::Session;
use imap_proto::types::{BodyStructure, ContentDisposition, ContentEncoding, SectionPath};
use rustls_connector::RustlsConnector;

use crate::error::AppError;
use crate::folder_normalize::{normalize_folder, FolderMetadata};
use crate::models::account::Encryption;

use log::{info, warn};

pub struct ImapTlsStream(rustls_connector::TlsStream<TcpStream>);

impl Read for ImapTlsStream {
    fn read(&mut self, buf: &mut [u8]) -> io::Result<usize> {
        self.0.read(buf)
    }
}

impl Write for ImapTlsStream {
    fn write(&mut self, buf: &[u8]) -> io::Result<usize> {
        self.0.write(buf)
    }

    fn flush(&mut self) -> io::Result<()> {
        self.0.flush()
    }
}

impl SetReadTimeout for ImapTlsStream {
    fn set_read_timeout(&mut self, timeout: Option<Duration>) -> imap::Result<()> {
        self.0
            .sock
            .set_read_timeout(timeout)
            .map_err(imap::Error::Io)
    }
}

fn decode_rfc2047_header(bytes: &[u8]) -> String {
    if bytes.is_empty() {
        return String::new();
    }

    let unescaped = unescape_imap_quoted_bytes(bytes);
    let bytes: &[u8] = &unescaped;

    let data_with_newline = if bytes.last() == Some(&b'\n') {
        bytes.to_vec()
    } else {
        let mut extended = bytes.to_vec();
        extended.push(b'\n');
        extended
    };

    let mut stream = mail_parser::parsers::MessageStream::new(&data_with_newline);
    let header_value = stream.parse_unstructured();

    header_value
        .into_text()
        .map(|cow| cow.to_string())
        .filter(|s| !s.is_empty())
        .unwrap_or_else(|| String::from_utf8_lossy(bytes).to_string())
}

/// Unescape IMAP RFC 3501 §4.3.2 quoted-string escapes.
///
/// imap-proto's `quoted` parser uses nom's `escaped` combinator which
/// recognizes `\"` and `\\` but does NOT decode them — the literal backslash
/// remains in the returned byte slice. Without this step, an IMAP envelope
/// subject like `"Ordered: \"Ikari\""` reaches the UI as
/// `Ordered: \"Ikari\"` (visible backslashes).
fn unescape_imap_quoted_bytes(bytes: &[u8]) -> Vec<u8> {
    let mut out = Vec::with_capacity(bytes.len());
    let mut i = 0;
    while i < bytes.len() {
        if bytes[i] == b'\\' && i + 1 < bytes.len() {
            let next = bytes[i + 1];
            if next == b'"' || next == b'\\' {
                out.push(next);
                i += 2;
                continue;
            }
        }
        out.push(bytes[i]);
        i += 1;
    }
    out
}

#[derive(Debug, Clone)]
pub struct ImapAttachmentMeta {
    pub part_id: String,
    pub filename: Option<String>,
    pub mime_type: String,
    pub size: i64,
    pub content_id: Option<String>,
}

pub struct ImapClient {
    session: Option<Session<ImapTlsStream>>,
    /// Track the currently selected folder to avoid redundant SELECT calls
    selected_folder: Option<String>,
    /// When true, Drop will NOT auto-logout (session was moved to pool).
    pooled: bool,
}

type BodyFetchResult = Result<Option<(Option<String>, Option<String>)>, AppError>;

pub struct BodyFetchWithMdn {
    pub body_text: Option<String>,
    pub body_html: Option<String>,
    pub mdn_requested: Option<String>,
}

#[derive(Clone)]
pub struct ImapEnvelope {
    pub uid: u32,
    pub message_id: String,
    pub in_reply_to: Option<String>,
    pub subject: String,
    pub from_address: String,
    pub from_name: Option<String>,
    pub to_addresses: Vec<String>,
    pub cc_addresses: Vec<String>,
    pub date: String,
    pub flags: Vec<String>,
    pub size: u32,
    pub has_attachments: bool,
    pub attachments: Vec<ImapAttachmentMeta>,
}

struct XOAuth2Authenticator {
    user: String,
    access_token: String,
}

impl imap::Authenticator for XOAuth2Authenticator {
    type Response = String;
    fn process(&self, _data: &[u8]) -> Self::Response {
        format!(
            "user={}\x01auth=Bearer {}\x01\x01",
            self.user, self.access_token
        )
    }
}

fn connect_with_timeout(host: &str, port: u16) -> Result<std::net::TcpStream, AppError> {
    use std::net::ToSocketAddrs;
    let addrs = format!("{}:{}", host, port)
        .to_socket_addrs()
        .map_err(|e| {
            AppError::Network(format!(
                "DNS resolution failed for {}:{}: {}",
                host, port, e
            ))
        })?;

    let timeout = std::time::Duration::from_secs(15);
    let mut last_err = None;
    for addr in addrs {
        match std::net::TcpStream::connect_timeout(&addr, timeout) {
            Ok(stream) => {
                let _ = stream.set_read_timeout(Some(timeout));
                let _ = stream.set_write_timeout(Some(timeout));
                return Ok(stream);
            }
            Err(e) => {
                last_err = Some(e);
            }
        }
    }

    Err(AppError::Network(format!(
        "Failed to connect to {}:{}: {}",
        host,
        port,
        last_err
            .map(|e| e.to_string())
            .unwrap_or_else(|| "no addresses found".to_string())
    )))
}

fn connect_starttls(host: &str, port: u16) -> Result<ImapTlsStream, AppError> {
    let mut tcp = connect_with_timeout(host, port)?;

    let mut buf = [0u8; 4096];
    let _n = tcp
        .read(&mut buf)
        .map_err(|e| AppError::Network(e.to_string()))?;

    tcp.write_all(b"a001 STARTTLS\r\n")
        .map_err(|e| AppError::Network(e.to_string()))?;
    tcp.flush().map_err(|e| AppError::Network(e.to_string()))?;

    let n = tcp
        .read(&mut buf)
        .map_err(|e| AppError::Network(e.to_string()))?;
    let response = String::from_utf8_lossy(&buf[..n]);

    if !response.contains("a001 OK") {
        return Err(AppError::Network(format!(
            "STARTTLS rejected: {}",
            response
        )));
    }

    let connector = RustlsConnector::new_with_native_certs()
        .map_err(|e| AppError::Network(format!("Failed to load system TLS certificates: {}", e)))?;
    let tls = connector
        .connect(host, tcp)
        .map_err(|e| AppError::Network(e.to_string()))?;
    Ok(ImapTlsStream(tls))
}

static TEST_ROOT_CERTS: RwLock<Option<rustls::RootCertStore>> = RwLock::new(None);

/// Inert test fixture injection allowing loopback IMAP test peers with self-signed test root certificates.
/// Default is `None`, which preserves production `RustlsConnector::new_with_native_certs()`.
pub fn set_test_root_certs_for_fixture(roots: Option<rustls::RootCertStore>) {
    if let Ok(mut lock) = TEST_ROOT_CERTS.write() {
        *lock = roots;
    }
}

impl ImapClient {
    /// Select a folder only if it's not already selected.
    /// This avoids redundant IMAP SELECT calls when fetching multiple bodies
    /// from the same folder on the same connection.
    fn select_folder_if_needed(&mut self, folder: &str) -> Result<(), AppError> {
        if self.selected_folder.as_deref() != Some(folder) {
            self.session()
                .select(folder)
                .map_err(|e| AppError::Network(e.to_string()))?;
            self.selected_folder = Some(folder.to_string());
        }
        Ok(())
    }

    fn session(&mut self) -> &mut Session<ImapTlsStream> {
        self.session.as_mut().expect("IMAP session already closed")
    }

    pub fn connect(
        host: &str,
        port: u16,
        encryption: &Encryption,
        username: &str,
        password: &str,
    ) -> Result<Self, AppError> {
        let client = match encryption {
            Encryption::Tls => {
                let tcp = connect_with_timeout(host, port)?;
                if let Ok(guard) = TEST_ROOT_CERTS.read() {
                    if let Some(ref roots) = *guard {
                        let config = rustls::ClientConfig::builder()
                            .with_root_certificates(roots.clone())
                            .with_no_client_auth();
                        return Self::login_tls(tcp, host, username, password, config);
                    }
                }
                let connector = RustlsConnector::new_with_native_certs().map_err(|e| {
                    AppError::Network(format!("Failed to load system TLS certificates: {}", e))
                })?;
                return Self::login_tls(tcp, host, username, password, connector);
            }
            Encryption::StartTls => {
                let tls_stream = connect_starttls(host, port)?;
                imap::Client::new(tls_stream)
            }
            Encryption::None => {
                return Err(AppError::Validation(
                    "Plaintext IMAP connections are not supported. Use TLS or STARTTLS."
                        .to_string(),
                ));
            }
        };

        let session = client
            .login(username, password)
            .map_err(|e| AppError::Auth(e.0.to_string()))?;

        Ok(Self {
            session: Some(session),
            selected_folder: None,
            pooled: false,
        })
    }

    /// Establish a password session using the supplied TLS trust configuration.
    /// Hostname and certificate verification remain the connector's responsibility.
    pub fn login_tls(
        tcp: TcpStream,
        server_name: &str,
        username: &str,
        password: &str,
        connector: impl Into<RustlsConnector>,
    ) -> Result<Self, AppError> {
        let tls = connector.into().connect(server_name, tcp)
            .map_err(|e| AppError::Network(e.to_string()))?;
        let session = imap::Client::new(ImapTlsStream(tls))
            .login(username, password)
            .map_err(|e| AppError::Auth(e.0.to_string()))?;
        Ok(Self { session: Some(session), selected_folder: None, pooled: false })
    }

    pub fn connect_oauth2(
        host: &str,
        port: u16,
        encryption: &Encryption,
        username: &str,
        access_token: &str,
    ) -> Result<Self, AppError> {
        log::debug!(
            "[IMAP] connect_oauth2: host={}, port={}, encryption={:?}, user={}",
            host,
            port,
            encryption,
            username
        );

        let client = match encryption {
            Encryption::Tls => {
                let connector = RustlsConnector::new_with_native_certs().map_err(|e| {
                    AppError::Network(format!("Failed to load system TLS certificates: {}", e))
                })?;
                let tcp = connect_with_timeout(host, port)?;
                let tls_stream = connector
                    .connect(host, tcp)
                    .map_err(|e| AppError::Network(e.to_string()))?;
                imap::Client::new(ImapTlsStream(tls_stream))
            }
            Encryption::StartTls => {
                let tls_stream = connect_starttls(host, port)?;
                imap::Client::new(tls_stream)
            }
            Encryption::None => {
                return Err(AppError::Validation(
                    "Plaintext IMAP connections are not supported. Use TLS or STARTTLS."
                        .to_string(),
                ));
            }
        };

        let auth = XOAuth2Authenticator {
            user: username.to_string(),
            access_token: access_token.to_string(),
        };

        let session = client.authenticate("XOAUTH2", &auth).map_err(|e| {
            warn!("[IMAP] XOAUTH2 auth failed: {}", e.0);
            AppError::Auth(e.0.to_string())
        })?;

        info!("[IMAP] XOAUTH2 auth succeeded for {}", username);
        Ok(Self {
            session: Some(session),
            selected_folder: None,
            pooled: false,
        })
    }

    pub fn list_folders(&mut self) -> Result<Vec<FolderMetadata>, AppError> {
        let folders = self
            .session()
            .list(Some(""), Some("*"))
            .map_err(|e| AppError::Network(e.to_string()))?;

        let mut result = Vec::new();
        for f in folders.iter() {
            let raw_path = f.name().to_string();
            let display_name = decode_imap_utf7(f.name().as_bytes());
            let delimiter = f.delimiter().map(|s| s.to_string());

            let attr_strings: Vec<String> =
                f.attributes().iter().map(name_attribute_string).collect();
            let attr_refs: Vec<&str> = attr_strings.iter().map(|s| s.as_str()).collect();

            let metadata =
                normalize_folder(&raw_path, display_name, &attr_refs, delimiter.as_deref());
            result.push(metadata);
        }

        Ok(result)
    }

    #[allow(dead_code)]
    pub fn fetch_envelopes(
        &mut self,
        folder: &str,
        offset: u32,
        limit: u32,
    ) -> Result<Vec<ImapEnvelope>, AppError> {
        let mailbox = self
            .session()
            .select(folder)
            .map_err(|e| AppError::Network(e.to_string()))?;
        self.selected_folder = Some(folder.to_string());
        let total = mailbox.exists;

        if total == 0 {
            return Ok(vec![]);
        }

        let end = total.saturating_sub(offset);
        let start = end.saturating_sub(limit).max(1);

        if end == 0 || start > end {
            return Ok(vec![]);
        }

        let range = format!("{}:{}", start, end);
        let messages = self
            .session()
            .fetch(&range, "(UID FLAGS ENVELOPE RFC822.SIZE BODYSTRUCTURE)")
            .map_err(|e| AppError::Network(e.to_string()))?;

        let mut envelopes = Vec::new();
        for msg in messages.iter() {
            if let Some(envelope) = msg.envelope() {
                let attachments = msg
                    .bodystructure()
                    .map(|body| collect_attachments(body, &[]))
                    .unwrap_or_default();

                envelopes.push(ImapEnvelope {
                    uid: msg.uid.unwrap_or(0),
                    message_id: envelope
                        .message_id
                        .as_ref()
                        .map(|m| String::from_utf8_lossy(m).to_string())
                        .unwrap_or_default(),
                    in_reply_to: envelope
                        .in_reply_to
                        .as_ref()
                        .map(|m| String::from_utf8_lossy(m).to_string()),
                    subject: envelope
                        .subject
                        .as_ref()
                        .map(|s| decode_rfc2047_header(s))
                        .unwrap_or_default(),
                    from_address: extract_first_address(&envelope.from),
                    from_name: extract_first_name(&envelope.from),
                    to_addresses: extract_addresses(&envelope.to),
                    cc_addresses: extract_addresses(&envelope.cc),
                    date: envelope
                        .date
                        .as_ref()
                        .map(|d| String::from_utf8_lossy(d).to_string())
                        .unwrap_or_default(),
                    flags: msg.flags().iter().map(|f| format!("{:?}", f)).collect(),
                    size: msg.size.unwrap_or(0),
                    has_attachments: !attachments.is_empty(),
                    attachments,
                });
            }
        }

        envelopes.reverse();
        Ok(envelopes)
    }

    /// Fetch envelopes for messages with UIDs greater than `since_uid`.
    /// If `since_uid` is 0, performs a full sync (fetches last 500 messages).
    /// Returns envelopes and the UIDVALIDITY of the selected mailbox.
    pub fn fetch_envelopes_since_uid(
        &mut self,
        folder: &str,
        since_uid: u32,
    ) -> Result<(Vec<ImapEnvelope>, u32), AppError> {
        let mailbox = self
            .session()
            .select(folder)
            .map_err(|e| AppError::Network(format!("Failed to select folder: {}", e)))?;
        self.selected_folder = Some(folder.to_string());
        let uid_validity = mailbox.uid_validity.unwrap_or(0);

        if mailbox.exists == 0 {
            return Ok((vec![], uid_validity));
        }

        if since_uid == 0 {
            let envelopes = self.fetch_envelopes_from_selected(mailbox.exists, 0, 500)?;
            return Ok((envelopes, uid_validity));
        }

        let uid_range = format!("{}:*", since_uid + 1);
        let fetches = self
            .session()
            .uid_fetch(uid_range, "(UID FLAGS ENVELOPE RFC822.SIZE BODYSTRUCTURE)")
            .map_err(|e| AppError::Network(format!("UID FETCH failed: {}", e)))?;

        let mut envelopes = Vec::new();
        for msg in fetches.iter() {
            let uid = msg.uid.unwrap_or(0);
            // IMAP may include the since_uid itself in the range response
            if uid <= since_uid {
                continue;
            }
            if let Some(envelope) = msg.envelope() {
                let attachments = msg
                    .bodystructure()
                    .map(|body| collect_attachments(body, &[]))
                    .unwrap_or_default();

                envelopes.push(ImapEnvelope {
                    uid,
                    message_id: envelope
                        .message_id
                        .as_ref()
                        .map(|m| String::from_utf8_lossy(m).to_string())
                        .unwrap_or_default(),
                    in_reply_to: envelope
                        .in_reply_to
                        .as_ref()
                        .map(|m| String::from_utf8_lossy(m).to_string()),
                    subject: envelope
                        .subject
                        .as_ref()
                        .map(|s| decode_rfc2047_header(s))
                        .unwrap_or_default(),
                    from_address: extract_first_address(&envelope.from),
                    from_name: extract_first_name(&envelope.from),
                    to_addresses: extract_addresses(&envelope.to),
                    cc_addresses: extract_addresses(&envelope.cc),
                    date: envelope
                        .date
                        .as_ref()
                        .map(|d| String::from_utf8_lossy(d).to_string())
                        .unwrap_or_default(),
                    flags: msg.flags().iter().map(|f| format!("{:?}", f)).collect(),
                    size: msg.size.unwrap_or(0),
                    has_attachments: !attachments.is_empty(),
                    attachments,
                });
            }
        }

        Ok((envelopes, uid_validity))
    }

    pub fn fetch_envelopes_before_uid(
        &mut self,
        folder: &str,
        before_uid: u32,
    ) -> Result<(Vec<ImapEnvelope>, u32), AppError> {
        let mailbox = self.session().select(folder)
            .map_err(|error| AppError::Network(format!("Failed to select folder: {error}")))?;
        self.selected_folder = Some(folder.to_string());
        let epoch = mailbox.uid_validity.unwrap_or(0);
        if mailbox.exists == 0 || before_uid <= 1 {
            return Ok((Vec::new(), epoch));
        }
        let matches = self.session().search(format!("UID 1:{}", before_uid - 1))
            .map_err(|error| AppError::Network(error.to_string()))?;
        let Some(last_sequence) = matches.into_iter().max() else {
            return Ok((Vec::new(), epoch));
        };
        let mut envelopes = self.fetch_envelopes_from_selected(last_sequence, 0, 500)?;
        envelopes.retain(|envelope| envelope.uid < before_uid);
        Ok((envelopes, epoch))
    }

    pub fn fetch_uid_flags(
        &mut self,
        folder: &str,
        uids: &[u32],
    ) -> Result<(Vec<(u32, bool, bool)>, u32), AppError> {
        let epoch = self.get_uid_validity(folder)?;
        if uids.is_empty() {
            return Ok((Vec::new(), epoch));
        }
        let range = uids.iter().map(u32::to_string).collect::<Vec<_>>().join(",");
        let messages = self.session().uid_fetch(range, "(UID FLAGS)")
            .map_err(|error| AppError::Network(error.to_string()))?;
        let mut flags = Vec::new();
        for message in messages.iter() {
            let uid = message.uid.ok_or_else(|| AppError::Network("Missing UID in flag response".into()))?;
            flags.push((uid, message.flags().iter().any(|flag| matches!(flag, imap::types::Flag::Seen)),
                message.flags().iter().any(|flag| matches!(flag, imap::types::Flag::Flagged))));
        }
        Ok((flags, epoch))
    }

    fn fetch_envelopes_from_selected(
        &mut self,
        total: u32,
        offset: u32,
        limit: u32,
    ) -> Result<Vec<ImapEnvelope>, AppError> {
        if total == 0 || limit == 0 {
            return Ok(vec![]);
        }

        let end = total.saturating_sub(offset);
        let start = end.saturating_sub(limit.saturating_sub(1)).max(1);

        if end == 0 || start > end {
            return Ok(vec![]);
        }

        let range = format!("{}:{}", start, end);
        let messages = self
            .session()
            .fetch(&range, "(UID FLAGS ENVELOPE RFC822.SIZE BODYSTRUCTURE)")
            .map_err(|e| AppError::Network(e.to_string()))?;

        let mut envelopes = Vec::new();
        for msg in messages.iter() {
            if let Some(envelope) = msg.envelope() {
                let attachments = msg
                    .bodystructure()
                    .map(|body| collect_attachments(body, &[]))
                    .unwrap_or_default();

                envelopes.push(ImapEnvelope {
                    uid: msg.uid.unwrap_or(0),
                    message_id: envelope
                        .message_id
                        .as_ref()
                        .map(|m| String::from_utf8_lossy(m).to_string())
                        .unwrap_or_default(),
                    in_reply_to: envelope
                        .in_reply_to
                        .as_ref()
                        .map(|m| String::from_utf8_lossy(m).to_string()),
                    subject: envelope
                        .subject
                        .as_ref()
                        .map(|s| decode_rfc2047_header(s))
                        .unwrap_or_default(),
                    from_address: extract_first_address(&envelope.from),
                    from_name: extract_first_name(&envelope.from),
                    to_addresses: extract_addresses(&envelope.to),
                    cc_addresses: extract_addresses(&envelope.cc),
                    date: envelope
                        .date
                        .as_ref()
                        .map(|d| String::from_utf8_lossy(d).to_string())
                        .unwrap_or_default(),
                    flags: msg.flags().iter().map(|f| format!("{:?}", f)).collect(),
                    size: msg.size.unwrap_or(0),
                    has_attachments: !attachments.is_empty(),
                    attachments,
                });
            }
        }

        if envelopes.len() != (end - start + 1) as usize {
            return Err(AppError::Network("Incomplete mailbox envelope page".into()));
        }
        envelopes.reverse();
        Ok(envelopes)
    }

    #[allow(dead_code)]
    pub fn get_uid_validity(&mut self, folder: &str) -> Result<u32, AppError> {
        let mailbox = self
            .session()
            .select(folder)
            .map_err(|e| AppError::Network(format!("Failed to select folder: {}", e)))?;
        self.selected_folder = Some(folder.to_string());
        Ok(mailbox.uid_validity.unwrap_or(0))
    }

    /// Select and verify the mailbox epoch before any UID mutation.
    pub fn validate_uid_validity(&mut self, folder: &str, expected: u32) -> Result<(), AppError> {
        if expected == 0 || self.get_uid_validity(folder)? != expected {
            return Err(AppError::Validation("Mailbox UIDVALIDITY changed; resync required".into()));
        }
        Ok(())
    }

    #[allow(dead_code)]
    pub fn fetch_email_body(&mut self, folder: &str, uid: u32) -> Result<Vec<u8>, AppError> {
        self.select_folder_if_needed(folder)?;

        let messages = self
            .session()
            .uid_fetch(uid.to_string(), "BODY.PEEK[]")
            .map_err(|e| AppError::Network(e.to_string()))?;

        let message = messages
            .iter()
            .next()
            .ok_or_else(|| AppError::NotFound("Email not found on server".to_string()))?;

        let body = message
            .body()
            .ok_or_else(|| AppError::NotFound("Email body not found".to_string()))?;

        Ok(body.to_vec())
    }

    pub fn fetch_attachment(
        &mut self,
        folder: &str,
        uid: u32,
        part_id: &str,
    ) -> Result<Vec<u8>, AppError> {
        self.select_folder_if_needed(folder)?;

        let fetch_query = format!("(BODYSTRUCTURE BODY.PEEK[{}])", part_id);
        let messages = self
            .session()
            .uid_fetch(uid.to_string(), &fetch_query)
            .map_err(|e| AppError::Network(e.to_string()))?;

        let message = messages
            .iter()
            .next()
            .ok_or_else(|| AppError::NotFound("Message not found".to_string()))?;

        let section_path = part_id_to_section_path(part_id)?;
        let body = message
            .section(&section_path)
            .ok_or_else(|| AppError::NotFound("Attachment data not found".to_string()))?;

        let structure = message
            .bodystructure()
            .ok_or_else(|| AppError::NotFound("Attachment structure not found".to_string()))?;

        let part = find_part_by_path(structure, part_path_from_string(part_id))
            .ok_or_else(|| AppError::NotFound("Attachment structure not found".to_string()))?;

        decode_attachment_bytes(body, part_transfer_encoding_name(part))
    }

    pub fn copy_email(
        &mut self,
        folder: &str,
        uid: u32,
        target_folder: &str,
    ) -> Result<(), AppError> {
        self.select_folder_if_needed(folder)?;
        self.session()
            .uid_copy(uid.to_string(), target_folder)
            .map_err(|e| AppError::Network(e.to_string()))?;
        Ok(())
    }

    pub fn set_flags(&mut self, folder: &str, uid: u32, flags: &str) -> Result<(), AppError> {
        self.select_folder_if_needed(folder)?;
        self.session()
            .uid_store(uid.to_string(), format!("+FLAGS ({})", flags))
            .map_err(|e| AppError::Network(e.to_string()))?;
        Ok(())
    }

    /// Apply a flag change to a whole UID set (RFC 3501 sequence-set, e.g.
    // "1,2,3") with one UID STORE command so batch flag mutations cost one
    // roundtrip per chunk instead of one per message.
    pub fn set_flags_bulk(
        &mut self,
        folder: &str,
        uid_set: &str,
        flags: &str,
    ) -> Result<(), AppError> {
        self.select_folder_if_needed(folder)?;
        self.session()
            .uid_store(uid_set.to_string(), format!("+FLAGS ({})", flags))
            .map_err(|e| AppError::Network(e.to_string()))?;
        Ok(())
    }

    pub fn remove_flags(&mut self, folder: &str, uid: u32, flags: &str) -> Result<(), AppError> {
        self.select_folder_if_needed(folder)?;
        self.session()
            .uid_store(uid.to_string(), format!("-FLAGS ({})", flags))
            .map_err(|e| AppError::Network(e.to_string()))?;
        Ok(())
    }

    pub fn delete_email(&mut self, folder: &str, uid: u32) -> Result<(), AppError> {
        self.select_folder_if_needed(folder)?;
        let capabilities = self.session().capabilities().map_err(|e| AppError::Network(e.to_string()))?;
        if !capabilities.has_str("UIDPLUS") {
            return Err(AppError::Validation("Server does not support UID-scoped expunge".into()));
        }
        self.set_flags(folder, uid, "\\Deleted")?;
        self.session().uid_expunge(uid.to_string()).map_err(|e| AppError::Network(e.to_string()))?;
        Ok(())
    }

    pub fn expunge(&mut self) -> Result<(), AppError> {
        Err(AppError::Validation(
            "Expunge requires an explicit message UID".to_string(),
        ))
    }

    pub fn move_email(
        &mut self,
        folder: &str,
        uid: u32,
        target_folder: &str,
    ) -> Result<(), AppError> {
        self.select_folder_if_needed(folder)?;
        let capabilities = self.session().capabilities()
            .map_err(|e| AppError::Network(e.to_string()))?;
        if !capabilities.has_str("MOVE") {
            return Err(AppError::Validation(
                "Server does not support safe message moves".to_string(),
            ));
        }
        self.session()
            .uid_mv(uid.to_string(), target_folder)
            .map_err(|e| AppError::Network(e.to_string()))?;
        Ok(())
    }

    pub fn fetch_body_by_uid(&mut self, folder: &str, uid: u32) -> BodyFetchResult {
        self.select_folder_if_needed(folder)?;

        let messages = self
            .session()
            .uid_fetch(uid.to_string(), "BODY.PEEK[]")
            .map_err(|e| AppError::Network(e.to_string()))?;

        let message = match messages.iter().next() {
            Some(m) => m,
            None => return Ok(None),
        };

        let body_bytes = match message.body() {
            Some(b) => b,
            None => return Ok(None),
        };

        let parsed = mail_parser::MessageParser::default()
            .parse(body_bytes)
            .ok_or_else(|| AppError::Internal("Failed to parse email body".to_string()))?;

        let body_text = parsed.body_text(0).map(|s| s.to_string());
        let body_html = parsed.body_html(0).map(|s| s.to_string());

        Ok(Some((body_text, body_html)))
    }

    pub fn fetch_body_with_mdn_by_uid(
        &mut self,
        folder: &str,
        uid: u32,
    ) -> Result<Option<BodyFetchWithMdn>, AppError> {
        self.select_folder_if_needed(folder)?;

        let messages = self
            .session()
            .uid_fetch(uid.to_string(), "BODY.PEEK[]")
            .map_err(|e| AppError::Network(e.to_string()))?;

        let message = match messages.iter().next() {
            Some(m) => m,
            None => return Ok(None),
        };

        let body_bytes = match message.body() {
            Some(b) => b,
            None => return Ok(None),
        };

        let parsed = mail_parser::MessageParser::default()
            .parse(body_bytes)
            .ok_or_else(|| AppError::Internal("Failed to parse email body".to_string()))?;

        let body_text = parsed.body_text(0).map(|s| s.to_string());
        let body_html = parsed.body_html(0).map(|s| s.to_string());

        let mdn_requested = parsed
            .header_raw("Disposition-Notification-To")
            .map(|s| s.trim().to_string())
            .filter(|s| !s.is_empty());

        Ok(Some(BodyFetchWithMdn {
            body_text,
            body_html,
            mdn_requested,
        }))
    }

    pub fn create_folder(&mut self, folder_name: &str) -> Result<(), AppError> {
        self.session()
            .create(folder_name)
            .map_err(|e| AppError::Network(format!("Failed to create folder: {}", e)))?;
        Ok(())
    }

    pub fn rename_folder(&mut self, from: &str, to: &str) -> Result<(), AppError> {
        self.session()
            .rename(from, to)
            .map_err(|e| AppError::Network(format!("Failed to rename folder: {}", e)))?;
        Ok(())
    }

    pub fn delete_folder(&mut self, folder_name: &str) -> Result<(), AppError> {
        self.session()
            .delete(folder_name)
            .map_err(|e| AppError::Network(format!("Failed to delete folder: {}", e)))?;
        Ok(())
    }

    pub fn logout(&mut self) -> Result<(), AppError> {
        if let Some(mut session) = self.session.take() {
            session
                .logout()
                .map_err(|e| AppError::Network(e.to_string()))?;
        }
        Ok(())
    }

    pub fn noop(&mut self) -> Result<(), AppError> {
        self.session()
            .noop()
            .map_err(|e| AppError::Network(e.to_string()))?;
        Ok(())
    }

    pub fn mark_pooled(&mut self) {
        self.pooled = true;
    }

    pub fn clear_selected_folder(&mut self) {
        self.selected_folder = None;
    }

    /// Take ownership of the underlying IMAP session.
    /// Used by the IDLE worker which needs direct session access for `wait_while`.
    pub fn take_session(mut self) -> Option<Session<ImapTlsStream>> {
        self.pooled = true; // prevent Drop from logging out
        self.session.take()
    }

    pub fn idle_wait(
        &mut self,
        folder: &str,
        timeout: std::time::Duration,
    ) -> Result<bool, AppError> {
        self.select_folder_if_needed(folder)?;

        let idle_handle = self
            .session()
            .idle()
            .map_err(|e| AppError::Network(e.to_string()))?;

        let result = idle_handle
            .wait_with_timeout(timeout)
            .map_err(|e| AppError::Network(e.to_string()))?;

        Ok(matches!(
            result,
            imap::extensions::idle::WaitOutcome::MailboxChanged
        ))
    }
}

impl Drop for ImapClient {
    fn drop(&mut self) {
        if self.pooled {
            return;
        }
        if let Some(mut session) = self.session.take() {
            let _ = session.logout();
        }
    }
}

/// RFC 3501 §5.1.3 — IMAP Modified UTF-7 decoder.
/// `&` starts a modified-base64 section (using `,` instead of `/`), terminated by `-`.
/// The base64 payload encodes big-endian UTF-16. `&-` is a literal `&`.
fn decode_imap_utf7(bytes: &[u8]) -> String {
    let input = String::from_utf8_lossy(bytes);
    let mut result = String::new();
    let mut chars = input.chars().peekable();

    while let Some(ch) = chars.next() {
        if ch != '&' {
            result.push(ch);
            continue;
        }
        match chars.peek() {
            Some(&'-') => {
                chars.next();
                result.push('&');
            }
            _ => {
                let mut payload = String::new();
                let mut terminated = false;
                for c in chars.by_ref() {
                    if c == '-' {
                        terminated = true;
                        break;
                    }
                    payload.push(c);
                }
                if !terminated {
                    // Malformed: unterminated &, emit raw chars
                    result.push('&');
                    result.push_str(&payload);
                    continue;
                }
                let standard_b64: String = payload
                    .chars()
                    .map(|c| if c == ',' { '/' } else { c })
                    .collect();
                if let Some(utf16_bytes) = decode_base64_bytes(&standard_b64) {
                    let mut utf16 = Vec::new();
                    let mut i = 0;
                    while i + 1 < utf16_bytes.len() {
                        utf16.push(u16::from_be_bytes([utf16_bytes[i], utf16_bytes[i + 1]]));
                        i += 2;
                    }
                    if let Ok(decoded) = String::from_utf16(&utf16) {
                        result.push_str(&decoded);
                    } else {
                        // Invalid UTF-16, emit raw
                        result.push('&');
                        result.push_str(&payload);
                        result.push('-');
                    }
                }
            }
        }
    }
    result
}

fn decode_base64_bytes(input: &str) -> Option<Vec<u8>> {
    fn b64_val(c: u8) -> Option<u8> {
        match c {
            b'A'..=b'Z' => Some(c - b'A'),
            b'a'..=b'z' => Some(c - b'a' + 26),
            b'0'..=b'9' => Some(c - b'0' + 52),
            b'+' => Some(62),
            b'/' => Some(63),
            _ => None,
        }
    }

    let mut buf: u32 = 0;
    let mut bits: u32 = 0;
    let mut out = Vec::new();

    for &byte in input.as_bytes() {
        if let Some(val) = b64_val(byte) {
            buf = (buf << 6) | val as u32;
            bits += 6;
            while bits >= 8 {
                bits -= 8;
                out.push((buf >> bits) as u8);
                buf &= (1 << bits) - 1;
            }
        }
    }
    Some(out)
}

fn extract_first_address(addrs: &Option<Vec<imap_proto::Address<'_>>>) -> String {
    addrs
        .as_ref()
        .and_then(|list: &Vec<imap_proto::Address<'_>>| list.first())
        .map(|a| {
            let mailbox = a
                .mailbox
                .map(|m| String::from_utf8_lossy(m).to_string())
                .unwrap_or_default();
            let host = a
                .host
                .map(|h| String::from_utf8_lossy(h).to_string())
                .unwrap_or_default();
            if host.is_empty() {
                mailbox
            } else {
                format!("{}@{}", mailbox, host)
            }
        })
        .unwrap_or_default()
}

fn extract_first_name(addrs: &Option<Vec<imap_proto::Address<'_>>>) -> Option<String> {
    addrs
        .as_ref()
        .and_then(|list: &Vec<imap_proto::Address<'_>>| list.first())
        .and_then(|a| {
            a.name
                .map(decode_rfc2047_header)
                .filter(|n: &String| !n.is_empty())
        })
}

fn extract_addresses(addrs: &Option<Vec<imap_proto::Address<'_>>>) -> Vec<String> {
    addrs
        .as_ref()
        .map(|list: &Vec<imap_proto::Address<'_>>| {
            list.iter()
                .map(|a| {
                    let mailbox = a
                        .mailbox
                        .map(|m| String::from_utf8_lossy(m).to_string())
                        .unwrap_or_default();
                    let host = a
                        .host
                        .map(|h| String::from_utf8_lossy(h).to_string())
                        .unwrap_or_default();
                    if host.is_empty() {
                        mailbox
                    } else {
                        format!("{}@{}", mailbox, host)
                    }
                })
                .collect()
        })
        .unwrap_or_default()
}

fn collect_attachments(body: &BodyStructure<'_>, prefix: &[u32]) -> Vec<ImapAttachmentMeta> {
    match body {
        BodyStructure::Basic { common, other, .. } | BodyStructure::Text { common, other, .. } => {
            if is_attachment(common.disposition.as_ref()) {
                vec![ImapAttachmentMeta {
                    part_id: part_path_string(prefix),
                    filename: attachment_filename(
                        common.disposition.as_ref(),
                        common.ty.params.as_ref(),
                    ),
                    mime_type: format!("{}/{}", common.ty.ty, common.ty.subtype),
                    size: other.octets as i64,
                    content_id: other.id.map(|s| s.to_string()),
                }]
            } else {
                vec![]
            }
        }
        BodyStructure::Message {
            common,
            other,
            body,
            ..
        } => {
            let mut attachments = Vec::new();
            if is_attachment(common.disposition.as_ref()) {
                attachments.push(ImapAttachmentMeta {
                    part_id: part_path_string(prefix),
                    filename: attachment_filename(
                        common.disposition.as_ref(),
                        common.ty.params.as_ref(),
                    ),
                    mime_type: format!("{}/{}", common.ty.ty, common.ty.subtype),
                    size: other.octets as i64,
                    content_id: other.id.map(|s| s.to_string()),
                });
            }
            let mut nested_prefix = prefix.to_vec();
            nested_prefix.push(1);
            attachments.extend(collect_attachments(body, &nested_prefix));
            attachments
        }
        BodyStructure::Multipart { common, bodies, .. } => {
            let mut attachments = Vec::new();
            if is_attachment(common.disposition.as_ref()) {
                attachments.push(ImapAttachmentMeta {
                    part_id: part_path_string(prefix),
                    filename: attachment_filename(
                        common.disposition.as_ref(),
                        common.ty.params.as_ref(),
                    ),
                    mime_type: format!("{}/{}", common.ty.ty, common.ty.subtype),
                    size: 0,
                    content_id: None,
                });
            }
            for (idx, part) in bodies.iter().enumerate() {
                let mut next_prefix = prefix.to_vec();
                next_prefix.push((idx + 1) as u32);
                attachments.extend(collect_attachments(part, &next_prefix));
            }
            attachments
        }
    }
}

fn is_attachment(disposition: Option<&ContentDisposition<'_>>) -> bool {
    disposition
        .map(|disp| disp.ty.eq_ignore_ascii_case("attachment"))
        .unwrap_or(false)
}

fn attachment_filename(
    disposition: Option<&ContentDisposition<'_>>,
    params: Option<&Vec<(&str, &str)>>,
) -> Option<String> {
    disposition
        .and_then(|d| d.params.as_ref())
        .and_then(|pairs| {
            pairs
                .iter()
                .find(|(k, _)| k.eq_ignore_ascii_case("filename"))
                .map(|(_, v)| (*v).to_string())
        })
        .or_else(|| {
            params.and_then(|pairs| {
                pairs
                    .iter()
                    .find(|(k, _)| k.eq_ignore_ascii_case("name"))
                    .map(|(_, v)| (*v).to_string())
            })
        })
}

fn part_path_string(prefix: &[u32]) -> String {
    prefix
        .iter()
        .map(u32::to_string)
        .collect::<Vec<_>>()
        .join(".")
}

fn part_path_from_string(part_id: &str) -> Vec<u32> {
    part_id
        .split('.')
        .filter_map(|part| part.parse::<u32>().ok())
        .collect()
}

fn part_id_to_section_path(part_id: &str) -> Result<SectionPath, AppError> {
    let parts = part_path_from_string(part_id);
    if parts.is_empty() {
        return Err(AppError::Validation(
            "Invalid attachment part id".to_string(),
        ));
    }
    Ok(SectionPath::Part(parts, None))
}

fn find_part_by_path<'a>(
    body: &'a BodyStructure<'a>,
    path: Vec<u32>,
) -> Option<&'a BodyStructure<'a>> {
    if path.is_empty() {
        return Some(body);
    }

    match body {
        BodyStructure::Basic { .. } | BodyStructure::Text { .. } => None,
        BodyStructure::Message { body: nested, .. } => {
            if path == vec![1] {
                Some(nested)
            } else {
                None
            }
        }
        BodyStructure::Multipart { bodies, .. } => {
            let idx = path[0].checked_sub(1)? as usize;
            let next = bodies.get(idx)?;
            if path.len() == 1 {
                Some(next)
            } else {
                find_part_by_path(next, path[1..].to_vec())
            }
        }
    }
}

fn part_transfer_encoding_name(body: &BodyStructure<'_>) -> &'static str {
    match body {
        BodyStructure::Basic { other, .. }
        | BodyStructure::Text { other, .. }
        | BodyStructure::Message { other, .. } => match other.transfer_encoding {
            ContentEncoding::Base64 => "base64",
            ContentEncoding::QuotedPrintable => "quoted-printable",
            _ => "other",
        },
        BodyStructure::Multipart { .. } => "other",
    }
}

fn decode_attachment_bytes(bytes: &[u8], encoding: &'static str) -> Result<Vec<u8>, AppError> {
    match encoding {
        "base64" => mail_parser::decoders::base64::base64_decode(bytes)
            .ok_or_else(|| AppError::Internal("Failed to decode base64 attachment".to_string())),
        "quoted-printable" => {
            mail_parser::decoders::quoted_printable::quoted_printable_decode(bytes).ok_or_else(
                || AppError::Internal("Failed to decode quoted-printable attachment".to_string()),
            )
        }
        _ => Ok(bytes.to_vec()),
    }
}

/// IMAP wire form of a LIST attribute (e.g. `\Drafts`). Special-use attributes
/// arrive as `Custom`; `Debug` would render them as `Custom("\\Drafts")`, which
/// `classify_folder` cannot match.
fn name_attribute_string(attr: &imap::types::NameAttribute<'_>) -> String {
    use imap::types::NameAttribute;
    match attr {
        NameAttribute::NoInferiors => "\\Noinferiors".to_string(),
        NameAttribute::NoSelect => "\\Noselect".to_string(),
        NameAttribute::Marked => "\\Marked".to_string(),
        NameAttribute::Unmarked => "\\Unmarked".to_string(),
        NameAttribute::Custom(s) => s.to_string(),
    }
}

#[cfg(test)]
#[path = "imap_review_tests.rs"]
mod review_tests;

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn name_attribute_string_keeps_special_use_classifiable() {
        use crate::folder_normalize::classify_folder;
        use crate::models::folder::FolderType;
        use imap::types::NameAttribute;

        let drafts = NameAttribute::from("\\Drafts");
        assert_eq!(name_attribute_string(&drafts), "\\Drafts");
        assert_eq!(name_attribute_string(&NameAttribute::NoSelect), "\\Noselect");

        let attrs = [name_attribute_string(&drafts)];
        let refs: Vec<&str> = attrs.iter().map(String::as_str).collect();
        let metadata = normalize_folder("[Gmail]/Drafts", "[Gmail]/Drafts".to_string(), &refs, Some("/"));
        assert!(matches!(classify_folder(&metadata), FolderType::Drafts));

        let noselect = [name_attribute_string(&NameAttribute::NoSelect)];
        let refs: Vec<&str> = noselect.iter().map(String::as_str).collect();
        assert!(!normalize_folder("[Gmail]", "[Gmail]".to_string(), &refs, Some("/")).is_selectable);
    }

    #[test]
    fn test_decode_rfc2047_ascii_passthrough() {
        let input = b"Hello World";
        assert_eq!(decode_rfc2047_header(input), "Hello World");
    }

    #[test]
    fn test_decode_rfc2047_utf8_base64() {
        let input = b"=?UTF-8?B?SGVsbG8gV29ybGQ=?=";
        assert_eq!(decode_rfc2047_header(input), "Hello World");
    }

    #[test]
    fn test_decode_rfc2047_utf8_base64_japanese() {
        let input = b"=?UTF-8?B?5pys6Kqe44Gu5ZCN?=";
        assert_eq!(decode_rfc2047_header(input), "本語の名");
    }

    #[test]
    fn test_decode_rfc2047_iso8859_1_quoted_printable() {
        let input = b"=?ISO-8859-1?Q?Hello_World?=";
        assert_eq!(decode_rfc2047_header(input), "Hello World");
    }

    #[test]
    fn test_decode_rfc2047_iso8859_1_quoted_printable_special() {
        let input = b"=?ISO-8859-1?Q?Caf=e9?=";
        assert_eq!(decode_rfc2047_header(input), "Café");
    }

    #[test]
    fn test_decode_rfc2047_mixed_encoded_plain() {
        let input = b"Hello =?UTF-8?B?V29ybGQ=?= Test";
        assert_eq!(decode_rfc2047_header(input), "Hello World Test");
    }

    #[test]
    fn test_decode_rfc2047_empty_input() {
        let input = b"";
        assert_eq!(decode_rfc2047_header(input), "");
    }

    #[test]
    fn test_decode_rfc2047_invalid_fallback() {
        let input = b"\xff\xfe invalid utf8";
        let result = decode_rfc2047_header(input);
        assert!(!result.is_empty());
    }

    #[test]
    fn test_decode_rfc2047_unescapes_imap_quoted_backslash_quote() {
        let input = br#"Ordered: \"Ikari Disinfection Drain Cleaner\""#;
        assert_eq!(
            decode_rfc2047_header(input),
            r#"Ordered: "Ikari Disinfection Drain Cleaner""#,
        );
    }

    #[test]
    fn test_decode_rfc2047_unescapes_imap_quoted_double_backslash() {
        let input = br"Path: C:\\Users\\foo";
        assert_eq!(decode_rfc2047_header(input), r"Path: C:\Users\foo");
    }

    #[test]
    fn test_decode_rfc2047_preserves_lone_backslash_before_non_special() {
        let input = br"Latex: \alpha + \beta";
        assert_eq!(decode_rfc2047_header(input), r"Latex: \alpha + \beta");
    }

    #[test]
    fn test_decode_rfc2047_unescape_then_rfc2047_decode() {
        let input = br#"=?UTF-8?Q?Re:?= \"Quoted\""#;
        assert_eq!(decode_rfc2047_header(input), r#"Re: "Quoted""#);
    }
}
