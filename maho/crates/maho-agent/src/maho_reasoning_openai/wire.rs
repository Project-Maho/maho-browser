use serde::{Deserialize, Serialize};
use swiftide_core::chat_completion::{ChatMessage, ToolOutput, ToolSpec};

#[derive(Serialize)]
pub(super) struct OpenAiChatRequest {
    pub(super) model: String,
    pub(super) messages: Vec<OpenAiMessage>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub(super) tools: Option<Vec<OpenAiTool>>,
    pub(super) stream: bool,
}

#[derive(Serialize)]
#[serde(tag = "role")]
pub(super) enum OpenAiMessage {
    #[serde(rename = "system")]
    System { content: String },
    #[serde(rename = "user")]
    User { content: String },
    #[serde(rename = "assistant")]
    Assistant {
        #[serde(skip_serializing_if = "Option::is_none")]
        content: Option<String>,
        #[serde(skip_serializing_if = "Option::is_none")]
        tool_calls: Option<Vec<OpenAiToolCall>>,
    },
    #[serde(rename = "tool")]
    Tool {
        tool_call_id: String,
        content: String,
    },
}

#[derive(Serialize)]
pub(super) struct OpenAiTool {
    #[serde(rename = "type")]
    ty: String,
    function: OpenAiFunctionSpec,
}

#[derive(Serialize)]
struct OpenAiFunctionSpec {
    name: String,
    description: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    parameters: Option<serde_json::Value>,
}

#[derive(Serialize, Deserialize, Clone, Debug)]
pub(super) struct OpenAiToolCall {
    pub(super) id: String,
    #[serde(rename = "type")]
    ty: String,
    pub(super) function: OpenAiFunctionCall,
}

#[derive(Serialize, Deserialize, Clone, Debug)]
pub(super) struct OpenAiFunctionCall {
    pub(super) name: String,
    pub(super) arguments: String,
}

#[derive(Deserialize)]
pub(super) struct ChatCompletionResponseJson {
    pub(super) choices: Vec<ChoiceJson>,
}

#[derive(Deserialize)]
pub(super) struct ChoiceJson {
    pub(super) message: MessageJson,
}

#[derive(Deserialize)]
pub(super) struct MessageJson {
    pub(super) content: Option<String>,
    pub(super) reasoning_content: Option<String>,
    pub(super) reasoning: Option<String>,
    pub(super) tool_calls: Option<Vec<OpenAiToolCall>>,
}

#[derive(Deserialize, Debug)]
pub(super) struct ChatCompletionChunk {
    pub(super) choices: Vec<ChoiceChunk>,
}

#[derive(Deserialize, Debug)]
pub(super) struct ChoiceChunk {
    pub(super) delta: DeltaChunk,
}

#[derive(Deserialize, Debug)]
pub(super) struct DeltaChunk {
    pub(super) content: Option<String>,
    pub(super) reasoning_content: Option<String>,
    pub(super) reasoning: Option<String>,
    pub(super) tool_calls: Option<Vec<ToolCallChunk>>,
}

#[derive(Deserialize, Debug)]
pub(super) struct ToolCallChunk {
    pub(super) index: usize,
    pub(super) id: Option<String>,
    pub(super) function: Option<FunctionCallChunk>,
}

#[derive(Deserialize, Debug)]
pub(super) struct FunctionCallChunk {
    pub(super) name: Option<String>,
    pub(super) arguments: Option<String>,
}

pub(super) fn map_message(message: &ChatMessage) -> OpenAiMessage {
    match message {
        ChatMessage::System(content) => OpenAiMessage::System {
            content: content.clone(),
        },
        ChatMessage::User(content) => OpenAiMessage::User {
            content: content.clone(),
        },
        ChatMessage::Summary(content) => OpenAiMessage::System {
            content: format!("Summary of conversation: {content}"),
        },
        ChatMessage::Assistant(content, tool_calls) => OpenAiMessage::Assistant {
            content: content.clone(),
            tool_calls: tool_calls.as_ref().map(|calls| {
                calls
                    .iter()
                    .map(|call| OpenAiToolCall {
                        id: call.id().to_string(),
                        ty: "function".to_string(),
                        function: OpenAiFunctionCall {
                            name: call.name().to_string(),
                            arguments: call.args().unwrap_or("{}").to_string(),
                        },
                    })
                    .collect()
            }),
        },
        ChatMessage::ToolOutput(call, output) => {
            let content = match output {
                ToolOutput::Text(text) => text.clone(),
                ToolOutput::Fail(text) => format!("Error: {text}"),
                ToolOutput::FeedbackRequired(value)
                | ToolOutput::Stop(value)
                | ToolOutput::AgentFailed(value) => {
                    value.as_ref().map(ToString::to_string).unwrap_or_default()
                }
                _ => String::new(),
            };
            OpenAiMessage::Tool {
                tool_call_id: call.id().to_string(),
                content,
            }
        }
    }
}

pub(super) fn map_tool(spec: &ToolSpec) -> OpenAiTool {
    OpenAiTool {
        ty: "function".to_string(),
        function: OpenAiFunctionSpec {
            name: spec.name.clone(),
            description: spec.description.clone(),
            parameters: spec
                .parameters_schema
                .as_ref()
                .and_then(|schema| serde_json::to_value(schema).ok()),
        },
    }
}
