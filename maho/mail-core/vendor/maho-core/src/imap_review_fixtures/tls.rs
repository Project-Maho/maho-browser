//! Shared test-only loopback TLS peer. No native trust changes or plaintext path.
use super::ImapClient;
use std::io::{self, BufRead, BufReader, Write};
use std::net::{Shutdown, TcpStream};
use std::sync::{mpsc, Arc, Mutex};
use std::thread::JoinHandle;
use std::time::Duration;

pub const DEADLINE: Duration = Duration::from_secs(5);

pub struct Peer {
    socket: Option<TcpStream>,
    control: TcpStream,
    config: Option<rustls::ClientConfig>,
    server: Option<JoinHandle<io::Result<()>>>,
    done: mpsc::Receiver<()>,
    commands: Arc<Mutex<Vec<String>>>,
}

impl Peer {
    pub fn new(mut reply: impl FnMut(&str, &str) -> String + Send + 'static) -> Self {
        let _ = rustls::crypto::ring::default_provider().install_default();
        let cert = rustls::pki_types::CertificateDer::from(include_bytes!("localhost.der").to_vec());
        let key = rustls::pki_types::PrivatePkcs8KeyDer::from(include_bytes!("localhost-key.der").to_vec());
        let server_config = rustls::ServerConfig::builder().with_no_client_auth()
            .with_single_cert(vec![cert.clone()], key.into()).unwrap();
        let mut roots = rustls::RootCertStore::empty();
        roots.add(cert).unwrap();
        let config = rustls::ClientConfig::builder().with_root_certificates(roots).with_no_client_auth();
        let (socket, server_socket) = std::thread::spawn(|| {
            tokio::runtime::Builder::new_current_thread().enable_all().build().unwrap()
                .block_on(async {
                    tokio::time::timeout(DEADLINE, async {
                        let listener = tokio::net::TcpListener::bind("127.0.0.1:0").await?;
                        let address = listener.local_addr()?;
                        let ((server, _), client) = tokio::try_join!(
                            listener.accept(), tokio::net::TcpStream::connect(address)
                        )?;
                        Ok::<_, io::Error>((client.into_std()?, server.into_std()?))
                    }).await.expect("TCP pair readiness deadline").unwrap()
                })
        }).join().unwrap();
        for stream in [&socket, &server_socket] {
            stream.set_nonblocking(false).unwrap();
            stream.set_read_timeout(Some(DEADLINE)).unwrap();
            stream.set_write_timeout(Some(DEADLINE)).unwrap();
        }
        let control = server_socket.try_clone().unwrap();
        let commands = Arc::new(Mutex::new(Vec::new()));
        let recorded = Arc::clone(&commands);
        let (done_tx, done) = mpsc::channel();
        let server = std::thread::spawn(move || {
            // Sender is dropped on every exit, including an assertion panic.
            let _completion = done_tx;
            let tls = rustls::StreamOwned::new(
                rustls::ServerConnection::new(Arc::new(server_config)).unwrap(), server_socket);
            let mut wire = BufReader::new(tls);
            for _ in 0..256 {
                let mut line = String::new();
                if wire.read_line(&mut line)? == 0 { return Ok(()); }
                let (tag, command) = line.trim_end().split_once(' ').unwrap();
                recorded.lock().unwrap().push(command.to_string());
                let response = reply(tag, command);
                wire.get_mut().write_all(response.as_bytes())?;
                wire.get_mut().flush()?;
                if command.eq_ignore_ascii_case("LOGOUT") { return Ok(()); }
            }
            Err(io::Error::other("fixture command budget exceeded"))
        });
        Self { socket: Some(socket), control, config: Some(config), server: Some(server), done, commands }
    }

    pub fn connect(&mut self) -> ImapClient {
        ImapClient::login_tls(self.socket.take().unwrap(), "localhost", "fixture", "fixture",
            self.config.take().unwrap()).unwrap()
    }

    pub fn commands(&self) -> Vec<String> { self.commands.lock().unwrap().clone() }

    pub fn finish(mut self) -> Vec<String> {
        let done = self.done.recv_timeout(DEADLINE * 2);
        let shutdown = self.control.shutdown(Shutdown::Both);
        let result = self.server.take().unwrap().join();
        assert!(matches!(done, Err(mpsc::RecvTimeoutError::Disconnected)), "peer did not complete: {done:?}");
        if let Err(error) = shutdown {
            assert_eq!(error.kind(), io::ErrorKind::NotConnected);
        }
        result.unwrap().unwrap();
        self.commands()
    }
}

impl Drop for Peer {
    fn drop(&mut self) {
        if let Some(server) = self.server.take() {
            // Every supplied handler is finite, or uses a DEADLINE-bounded
            // channel barrier. Shutdown releases TLS reads/writes on unwind.
            if let Err(error) = self.control.shutdown(Shutdown::Both) {
                if error.kind() != io::ErrorKind::NotConnected { eprintln!("peer shutdown: {error}"); }
            }
            let completion = self.done.recv_timeout(DEADLINE * 2);
            if !matches!(completion, Err(mpsc::RecvTimeoutError::Disconnected)) {
                eprintln!("peer cleanup completion: {completion:?}");
            }
            if let Err(error) = server.join() { eprintln!("peer panic during cleanup: {error:?}"); }
        }
    }
}

#[derive(Default)]
pub struct Mailbox {
    pub messages: std::collections::BTreeMap<u32, Vec<&'static str>>,
    pub epoch: u32,
    pub fetch_sizes: Vec<usize>,
}

impl Mailbox {
    pub fn seeded(count: u32) -> Self {
        Self { messages: (1..=count).map(|uid| (uid, vec![])).collect(), epoch: 1234, fetch_sizes: vec![] }
    }

    pub fn reply(&mut self, tag: &str, command: &str) -> String {
        let upper = command.to_uppercase();
        let mut response = String::new();
        if upper.starts_with("SELECT ") {
            response.push_str(&format!("* {} EXISTS\r\n* OK [UIDVALIDITY {}] epoch\r\n* OK [UIDNEXT {}] next\r\n",
                self.messages.len(), self.epoch, self.messages.keys().next_back().copied().unwrap_or(0) + 1));
        } else if upper == "CAPABILITY" {
            response.push_str("* CAPABILITY IMAP4rev1 MOVE UIDPLUS\r\n");
        } else if let Some(set) = upper.strip_prefix("SEARCH UID ") {
            let maximum = self.messages.keys().next_back().copied().unwrap_or(0);
            let sequences = self.messages.keys().enumerate()
                .filter(|(_, uid)| in_set(set, **uid, maximum))
                .map(|(index, _)| (index + 1).to_string()).collect::<Vec<_>>().join(" ");
            response.push_str("* SEARCH");
            if !sequences.is_empty() {
                response.push(' ');
                response.push_str(&sequences);
            }
            response.push_str("\r\n");
        } else if upper.starts_with("UID FETCH ") || upper.starts_with("FETCH ") {
            let uid_fetch = upper.starts_with("UID ");
            let range = upper.split_whitespace().nth(if uid_fetch { 2 } else { 1 }).unwrap();
            let max = if uid_fetch { self.messages.keys().next_back().copied().unwrap_or(0) } else { self.messages.len() as u32 };
            let mut fetched = 0;
            for (index, (&uid, flags)) in self.messages.iter().enumerate() {
                let sequence = index as u32 + 1;
                if !in_set(range, if uid_fetch { uid } else { sequence }, max) { continue; }
                fetched += 1;
                if upper.contains("BODY[") || upper.contains("BODY.PEEK[") || upper.ends_with("RFC822") {
                    let body = format!("From: sender@example.invalid\r\nContent-Type: text/plain\r\n\r\nbody for UID {uid}\r\n");
                    response.push_str(&format!("* {sequence} FETCH (UID {uid} BODY[] {{{}}}\r\n{body})\r\n", body.len()));
                } else {
                    response.push_str(&format!("* {sequence} FETCH (UID {uid} FLAGS ({})", flags.join(" ")));
                    if upper.contains("ENVELOPE") {
                        response.push_str(&format!(" ENVELOPE (\"Fri, 04 Sep 2026 23:30:00 -0700\" \"Message {uid}\" ((NIL NIL \"sender\" \"example.invalid\")) NIL NIL NIL NIL NIL NIL \"<{uid}@example.invalid>\") RFC822.SIZE 100"));
                    }
                    response.push_str(")\r\n");
                }
            }
            self.fetch_sizes.push(fetched);
        } else if upper == "LOGOUT" {
            response.push_str("* BYE done\r\n");
        }
        response.push_str(&format!("{tag} OK complete\r\n"));
        response
    }
}

fn in_set(set: &str, value: u32, max: u32) -> bool {
    let number = |text: &str| if text == "*" { max } else { text.parse::<u32>().unwrap() };
    set.split(',').any(|part| {
        let (a, b) = part.split_once(':').unwrap_or((part, part));
        let (a, b) = (number(a), number(b));
        (a.min(b)..=a.max(b)).contains(&value)
    })
}
