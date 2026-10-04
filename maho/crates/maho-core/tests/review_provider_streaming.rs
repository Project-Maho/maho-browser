use futures_util::StreamExt;
use maho_core::{
    llm_provider::{LLMProvider, OpenAICompatibleProvider},
    providers::anthropic::AnthropicProvider,
};
use maho_types::llm::LLMStreamChunk;
use std::{
    io::{Read, Write},
    net::TcpListener,
    time::Duration,
};

fn serve(
    chunks: Vec<Vec<u8>>,
) -> (
    String,
    tokio::sync::oneshot::Receiver<()>,
    std::thread::JoinHandle<()>,
) {
    let listener = TcpListener::bind("127.0.0.1:0").unwrap();
    let endpoint = format!("http://{}", listener.local_addr().unwrap());
    let (tx, rx) = tokio::sync::oneshot::channel();
    let thread = std::thread::spawn(move || {
        let (mut socket, _) = listener.accept().unwrap();
        socket
            .set_read_timeout(Some(Duration::from_secs(10)))
            .unwrap();
        let mut request = Vec::new();
        let mut byte = [0];
        while !request.ends_with(b"\r\n\r\n") {
            socket.read_exact(&mut byte).unwrap();
            request.push(byte[0]);
        }
        let headers = String::from_utf8(request).unwrap();
        let len = headers
            .lines()
            .find_map(|l| {
                l.to_lowercase()
                    .strip_prefix("content-length:")
                    .map(|n| n.trim().parse::<usize>().unwrap())
            })
            .unwrap();
        socket.read_exact(&mut vec![0; len]).unwrap();
        socket.write_all(b"HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n").unwrap();
        for chunk in chunks {
            write!(socket, "{:x}\r\n", chunk.len()).unwrap();
            socket.write_all(&chunk).unwrap();
            socket.write_all(b"\r\n").unwrap();
        }
        socket.write_all(b"0\r\n\r\n").unwrap();
        tx.send(()).unwrap();
    });
    (endpoint, rx, thread)
}

#[tokio::test(flavor = "current_thread")]
async fn utf8_codepoints_may_span_http_chunks() {
    for anthropic in [false, true] {
        for text in ["\u{1f642}", "\u{6f22}"] {
            for split in 1..text.len() {
                let body = if anthropic {
                    format!(
                        "event: content_block_delta\ndata: {}\n\n",
                        serde_json::json!({"delta":{"type":"text_delta","text":text}})
                    )
                } else {
                    format!(
                        "data: {}\n\n",
                        serde_json::json!({"choices":[{"delta":{"content":text}}]})
                    )
                };
                let boundary = body.find(text).unwrap() + split;
                let (endpoint, sent, thread) = serve(vec![
                    body.as_bytes()[..boundary].to_vec(),
                    body.as_bytes()[boundary..].to_vec(),
                ]);
                let provider: Box<dyn LLMProvider> = if anthropic {
                    Box::new(AnthropicProvider::new(endpoint, String::new()))
                } else {
                    Box::new(OpenAICompatibleProvider::new(endpoint, String::new()))
                };
                let mut stream = provider.complete("test", vec![], vec![]);
                tokio::time::timeout(Duration::from_secs(10), sent)
                    .await
                    .unwrap()
                    .unwrap();
                let output = tokio::time::timeout(Duration::from_secs(10), async {
                    let mut output = String::new();
                    while let Some(chunk) = stream.next().await {
                        if let LLMStreamChunk::Token(token) = chunk.unwrap() {
                            output.push_str(&token);
                        }
                    }
                    output
                })
                .await
                .unwrap();
                thread.join().unwrap();
                assert_eq!(output, text);
            }
        }
    }
}

#[tokio::test(flavor = "current_thread")]
async fn burst_larger_than_channel_preserves_tokens_and_tools() {
    for anthropic in [false, true] {
        // One SSE block makes the pre-fix synchronous try_send loop overflow
        // deterministically, even if the consumer is ready on every poll.
        let mut body = String::new();
        if anthropic {
            for i in 0..600 {
                body.push_str(&format!("event: content_block_delta\ndata: {{\"delta\":{{\"type\":\"text_delta\",\"text\":\"{i},\"}}}}\n\n"));
            }
        } else {
            let calls: Vec<_> = (0..600)
                .map(|i| serde_json::json!({"index":0,"function":{"arguments":format!("{i},")}}))
                .collect();
            body = format!(
                "data: {}\n\n",
                serde_json::json!({"choices":[{"delta":{"content":"token", "tool_calls":calls}}]})
            );
        }
        let (endpoint, sent, thread) = serve(vec![body.into_bytes()]);
        let provider: Box<dyn LLMProvider> = if anthropic {
            Box::new(AnthropicProvider::new(endpoint, String::new()))
        } else {
            Box::new(OpenAICompatibleProvider::new(endpoint, String::new()))
        };
        let mut stream = provider.complete("test", vec![], vec![]);
        tokio::time::timeout(Duration::from_secs(10), sent)
            .await
            .unwrap()
            .unwrap();
        let output = tokio::time::timeout(Duration::from_secs(10), async {
            let mut output = String::new();
            while let Some(chunk) = stream.next().await {
                match chunk.unwrap() {
                    LLMStreamChunk::Token(t) if anthropic => output.push_str(&t),
                    LLMStreamChunk::ToolCallDelta {
                        arguments_delta, ..
                    } => output.push_str(&arguments_delta),
                    _ => {}
                }
            }
            output
        })
        .await
        .unwrap();
        thread.join().unwrap();
        assert_eq!(
            output,
            (0..600).map(|i| format!("{i},")).collect::<String>()
        );
    }
}
