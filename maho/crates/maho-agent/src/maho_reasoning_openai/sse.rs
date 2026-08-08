use super::wire::ChatCompletionChunk;
use futures::{Stream, StreamExt};
use std::pin::Pin;
use swiftide_core::chat_completion::{
    errors::LanguageModelError, ChatCompletionResponse, ChatCompletionResponseDelta,
    ChatCompletionStream, ToolCallAccum,
};

type SseByteStream =
    Pin<Box<dyn Stream<Item = std::result::Result<Vec<u8>, LanguageModelError>> + Send>>;

enum SseLineOutcome {
    Continue,
    Done,
    Yield(ChatCompletionResponse),
}

fn accumulate_chunk(
    chunk: ChatCompletionChunk,
    accumulated: &mut ChatCompletionResponse,
) -> Option<ChatCompletionResponse> {
    let choice = chunk.choices.first()?;
    let reasoning = choice
        .delta
        .reasoning_content
        .as_deref()
        .or(choice.delta.reasoning.as_deref());

    accumulated.append_message_delta(choice.delta.content.as_deref());
    accumulated.append_reasoning_delta(reasoning);
    if let Some(tool_calls) = &choice.delta.tool_calls {
        for tool_call in tool_calls {
            accumulated.append_tool_call_delta(
                tool_call.index,
                tool_call.id.as_deref(),
                tool_call
                    .function
                    .as_ref()
                    .and_then(|function| function.name.as_deref()),
                tool_call
                    .function
                    .as_ref()
                    .and_then(|function| function.arguments.as_deref()),
            );
        }
    }

    let tool_calls_chunk = choice.delta.tool_calls.as_ref().map(|tool_calls| {
        tool_calls
            .iter()
            .map(|tool_call| {
                (
                    tool_call.index,
                    ToolCallAccum {
                        id: tool_call.id.clone(),
                        name: tool_call
                            .function
                            .as_ref()
                            .and_then(|function| function.name.clone()),
                        arguments: tool_call
                            .function
                            .as_ref()
                            .and_then(|function| function.arguments.clone()),
                    },
                )
            })
            .collect()
    });

    Some(ChatCompletionResponse {
        message: accumulated.message.clone(),
        tool_calls: accumulated.tool_calls.clone(),
        delta: Some(ChatCompletionResponseDelta {
            message_chunk: choice.delta.content.clone(),
            reasoning_chunk: reasoning.map(str::to_string),
            tool_calls_chunk,
        }),
        ..ChatCompletionResponse::default()
    })
}

fn process_sse_line(line: &str, accumulated: &mut ChatCompletionResponse) -> SseLineOutcome {
    let line = line.trim();
    if line.is_empty() {
        return SseLineOutcome::Continue;
    }
    if line == "data: [DONE]" {
        return SseLineOutcome::Done;
    }
    let Some(json) = line.strip_prefix("data: ") else {
        return SseLineOutcome::Continue;
    };
    let Ok(chunk) = serde_json::from_str::<ChatCompletionChunk>(json) else {
        return SseLineOutcome::Continue;
    };

    accumulate_chunk(chunk, accumulated).map_or(SseLineOutcome::Continue, SseLineOutcome::Yield)
}

pub(super) fn stream_sse_bytes(byte_stream: SseByteStream) -> ChatCompletionStream {
    let stream = futures::stream::unfold(
        (byte_stream, Vec::new(), ChatCompletionResponse::default()),
        move |(mut byte_stream, mut buffer, mut accumulated)| async move {
            loop {
                if let Some(position) = buffer.iter().position(|&byte| byte == b'\n') {
                    let line_bytes = buffer.drain(..position + 1).collect::<Vec<u8>>();
                    let line = String::from_utf8_lossy(&line_bytes);
                    match process_sse_line(&line, &mut accumulated) {
                        SseLineOutcome::Continue => continue,
                        SseLineOutcome::Done => return None,
                        SseLineOutcome::Yield(response) => {
                            return Some((Ok(response), (byte_stream, buffer, accumulated)));
                        }
                    }
                }

                match byte_stream.next().await {
                    Some(Ok(bytes)) => buffer.extend_from_slice(&bytes),
                    Some(Err(error)) => {
                        return Some((Err(error), (byte_stream, buffer, accumulated)));
                    }
                    None => {
                        if buffer.is_empty() {
                            return None;
                        }
                        let line = String::from_utf8_lossy(&buffer);
                        return match process_sse_line(&line, &mut accumulated) {
                            SseLineOutcome::Yield(response) => {
                                Some((Ok(response), (byte_stream, Vec::new(), accumulated)))
                            }
                            SseLineOutcome::Continue | SseLineOutcome::Done => None,
                        };
                    }
                }
            }
        },
    );

    Box::pin(stream)
}

#[cfg(test)]
mod tests {
    use super::stream_sse_bytes;
    use anyhow::{anyhow, Result};
    use futures::{stream, StreamExt, TryStreamExt};
    use swiftide_core::chat_completion::ChatCompletionResponse;

    async fn collect_responses(chunks: &[&str]) -> Result<Vec<ChatCompletionResponse>> {
        let bytes = chunks
            .iter()
            .map(|chunk| Ok(chunk.as_bytes().to_vec()))
            .collect::<Vec<_>>();
        stream_sse_bytes(Box::pin(stream::iter(bytes)))
            .map(|result| result.map_err(anyhow::Error::from))
            .try_collect()
            .await
    }

    #[tokio::test]
    async fn accumulates_message_and_tool_calls_across_sse_chunks() -> Result<()> {
        // Given
        let chunks = [
            "data: {\"choices\":[{\"delta\":{\"content\":\"Hel\"}}]}\n",
            "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,\"id\":\"call_weather\",\"function\":{\"name\":\"weather\",\"arguments\":\"{\\\"city\\\":\\\"\"}}]}}]}\n",
            "data: {\"choices\":[{\"delta\":{\"content\":\"lo\",\"tool_calls\":[{\"index\":0,\"function\":{\"arguments\":\"Paris\\\"}\"}}]}}]}\n",
            "data: [DONE]\n",
        ];

        // When
        let responses = collect_responses(&chunks).await?;

        // Then
        let final_response = responses
            .last()
            .ok_or_else(|| anyhow!("stream should yield an accumulated response"))?;
        assert_eq!(final_response.message(), Some("Hello"));
        let tool_call = final_response
            .tool_calls()
            .and_then(|calls| calls.first())
            .ok_or_else(|| anyhow!("final response should contain the assembled tool call"))?;
        assert_eq!(tool_call.id(), "call_weather");
        assert_eq!(tool_call.name(), "weather");
        assert_eq!(tool_call.args(), Some("{\"city\":\"Paris\"}"));
        Ok(())
    }

    #[tokio::test]
    async fn parses_reasoning_content_and_reasoning_aliases() -> Result<()> {
        // Given
        let chunks = [
            "data: {\"choices\":[{\"delta\":{\"reasoning_content\":\"first thought\"}}]}\n",
            "data: {\"choices\":[{\"delta\":{\"reasoning\":\"second thought\"}}]}\n",
            "data: [DONE]\n",
        ];

        // When
        let responses = collect_responses(&chunks).await?;

        // Then
        let reasoning = responses
            .iter()
            .filter_map(|response| response.delta.as_ref())
            .filter_map(|delta| delta.reasoning_chunk.as_deref())
            .collect::<Vec<_>>();
        assert_eq!(reasoning, vec!["first thought", "second thought"]);
        Ok(())
    }

    #[tokio::test]
    async fn accumulates_final_tool_call_chunk_without_newline_or_done() -> Result<()> {
        // Given
        let chunks = [
            "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,\"id\":\"call_tail\",\"function\":{\"name\":\"lookup\",\"arguments\":\"{\\\"query\\\":\\\"\"}}]}}]}\n",
            "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,\"function\":{\"arguments\":\"maho\\\"}\"}}]}}]}",
        ];

        // When
        let responses = collect_responses(&chunks).await?;

        // Then
        let final_response = responses
            .last()
            .ok_or_else(|| anyhow!("EOF tail should yield an accumulated response"))?;
        let tool_call = final_response
            .tool_calls()
            .and_then(|calls| calls.first())
            .ok_or_else(|| anyhow!("EOF tail should finalize the tool call"))?;
        assert_eq!(tool_call.id(), "call_tail");
        assert_eq!(tool_call.name(), "lookup");
        assert_eq!(tool_call.args(), Some("{\"query\":\"maho\"}"));
        Ok(())
    }
}
