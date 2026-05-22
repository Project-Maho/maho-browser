use std::pin::Pin;

use tokio::io::{AsyncBufRead, AsyncBufReadExt, AsyncWrite, AsyncWriteExt, BufReader};

use crate::Error;

/// Maximum allowed line length (64 KiB). Lines exceeding this are rejected.
const MAX_LINE_BYTES: usize = 64 * 1024;

/// nd-JSON line transport over async read/write streams.
///
/// Splits on `\n`, validates UTF-8, rejects lines > 64 KiB,
/// and rejects any decoded JSON value that contains embedded newlines
/// (the wire format requires one JSON object per line).
pub struct LineTransport<R, W> {
    reader: BufReader<R>,
    writer: W,
}

impl<R, W> LineTransport<R, W>
where
    R: tokio::io::AsyncRead + Unpin,
    W: AsyncWrite + Unpin,
{
    pub fn new(reader: R, writer: W) -> Self {
        Self {
            reader: BufReader::new(reader),
            writer,
        }
    }

    /// Read the next complete line from the stream.
    ///
    /// Returns `None` on EOF. Returns an error on oversized lines or invalid UTF-8.
    pub async fn read_line(&mut self) -> Result<Option<String>, Error> {
        loop {
            let mut buf = String::new();

            loop {
                let available = self.reader.fill_buf().await.map_err(Error::Io)?;
                if available.is_empty() {
                    if buf.is_empty() {
                        return Ok(None);
                    }
                    break;
                }

                if let Some(pos) = available.iter().position(|&b| b == b'\n') {
                    let to_add = pos;
                    if buf.len() + to_add > MAX_LINE_BYTES {
                        return Err(Error::LineTooLong(buf.len() + to_add));
                    }

                    let chunk = &available[..pos];
                    let text = std::str::from_utf8(chunk).map_err(|_| Error::InvalidUtf8)?;
                    buf.push_str(text);

                    let consume_len = pos + 1;
                    Pin::new(&mut self.reader).consume(consume_len);
                    break;
                } else {
                    let len = available.len();
                    if buf.len() + len > MAX_LINE_BYTES {
                        return Err(Error::LineTooLong(buf.len() + len));
                    }
                    let text = std::str::from_utf8(available).map_err(|_| Error::InvalidUtf8)?;
                    buf.push_str(text);
                    Pin::new(&mut self.reader).consume(len);
                }
            }

            let trimmed = buf.trim();
            if !trimmed.is_empty() {
                return Ok(Some(buf));
            }
        }
    }

    /// Write a JSON value as a single nd-JSON line.
    pub async fn write_message(&mut self, value: &serde_json::Value) -> Result<(), Error> {
        let serialized = serde_json::to_string(value).map_err(Error::Json)?;
        // Reject if the serialized form contains embedded newlines
        if serialized.contains('\n') {
            return Err(Error::EmbeddedNewline);
        }
        self.writer
            .write_all(serialized.as_bytes())
            .await
            .map_err(Error::Io)?;
        self.writer.write_all(b"\n").await.map_err(Error::Io)?;
        self.writer.flush().await.map_err(Error::Io)?;
        Ok(())
    }

    /// Close the write side.
    pub async fn close_write(&mut self) -> Result<(), Error> {
        self.writer.shutdown().await.map_err(Error::Io)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use tokio::io::AsyncWriteExt;

    async fn read_lines(data: &[u8]) -> Vec<Result<Option<String>, Error>> {
        let (client_r, mut server_w) = tokio::io::duplex(64 * 1024 + 256);
        let (_, sink_w) = tokio::io::duplex(64);

        let data = data.to_vec();
        tokio::spawn(async move {
            server_w.write_all(&data).await.unwrap();
            server_w.shutdown().await.unwrap();
        });

        let mut transport = LineTransport::new(client_r, sink_w);
        let mut results = Vec::new();
        loop {
            match transport.read_line().await {
                Ok(None) => {
                    results.push(Ok(None));
                    break;
                }
                Ok(Some(line)) => results.push(Ok(Some(line))),
                Err(e) => {
                    results.push(Err(e));
                    break;
                }
            }
        }
        results
    }

    #[tokio::test]
    async fn single_line() {
        let results = read_lines(b"{\"jsonrpc\":\"2.0\"}\n").await;
        assert_eq!(results.len(), 2);
        assert_eq!(results[0].as_ref().unwrap().as_deref(), Some("{\"jsonrpc\":\"2.0\"}"));
        assert!(results[1].as_ref().unwrap().is_none());
    }

    #[tokio::test]
    async fn multiple_lines_per_chunk() {
        let results = read_lines(b"{\"a\":1}\n{\"b\":2}\n{\"c\":3}\n").await;
        assert_eq!(results.len(), 4); // 3 lines + EOF
        assert_eq!(results[0].as_ref().unwrap().as_deref(), Some("{\"a\":1}"));
        assert_eq!(results[1].as_ref().unwrap().as_deref(), Some("{\"b\":2}"));
        assert_eq!(results[2].as_ref().unwrap().as_deref(), Some("{\"c\":3}"));
        assert!(results[3].as_ref().unwrap().is_none());
    }

    #[tokio::test]
    async fn partial_read_across_newline() {
        // Simulate chunked delivery by writing in two parts
        let (client_r, mut server_w) = tokio::io::duplex(256);
        let (_read_end, sink_w) = tokio::io::duplex(64);

        tokio::spawn(async move {
            // First chunk: partial line
            server_w.write_all(b"{\"par").await.unwrap();
            tokio::time::sleep(std::time::Duration::from_millis(10)).await;
            // Second chunk: rest of line + newline
            server_w.write_all(b"tial\":true}\n").await.unwrap();
            server_w.shutdown().await.unwrap();
        });

        let mut transport = LineTransport::new(client_r, sink_w);
        let line = transport.read_line().await.unwrap().unwrap();
        assert_eq!(line, "{\"partial\":true}");
    }

    #[tokio::test]
    async fn oversized_line_rejected() {
        let big = vec![b'x'; MAX_LINE_BYTES + 1];
        let mut data = big;
        data.push(b'\n');

        let results = read_lines(&data).await;
        assert!(results[0].is_err());
        match results[0].as_ref().unwrap_err() {
            Error::LineTooLong(_) => {}
            other => panic!("expected LineTooLong, got {:?}", other),
        }
    }

    #[tokio::test]
    async fn invalid_utf8_rejected() {
        let data: Vec<u8> = vec![0xff, 0xfe, b'\n'];
        let results = read_lines(&data).await;
        assert!(results[0].is_err());
        match results[0].as_ref().unwrap_err() {
            Error::InvalidUtf8 => {}
            other => panic!("expected InvalidUtf8, got {:?}", other),
        }
    }

    #[tokio::test]
    async fn json_escapes_newlines_in_values() {
        let (read_end, write_end) = tokio::io::duplex(256);
        let (dummy_r, _dummy_w) = tokio::io::duplex(64);
        let mut transport = LineTransport::new(dummy_r, write_end);

        // serde_json escapes \n as \\n, so serialized form has no literal newline
        let value = serde_json::json!({"text": "line1\nline2"});
        let result = transport.write_message(&value).await;
        assert!(result.is_ok());
        drop(read_end);
    }

    #[tokio::test]
    async fn write_and_read_roundtrip() {
        let (client_r, server_w) = tokio::io::duplex(1024);
        let (server_r, client_w) = tokio::io::duplex(1024);

        let mut writer_transport = LineTransport::new(server_r, server_w);
        let mut reader_transport = LineTransport::new(client_r, client_w);

        let msg = serde_json::json!({"jsonrpc": "2.0", "id": 1, "method": "test"});
        writer_transport.write_message(&msg).await.unwrap();
        writer_transport.close_write().await.unwrap();

        let line = reader_transport.read_line().await.unwrap().unwrap();
        let parsed: serde_json::Value = serde_json::from_str(&line).unwrap();
        assert_eq!(parsed["method"], "test");
    }
}
