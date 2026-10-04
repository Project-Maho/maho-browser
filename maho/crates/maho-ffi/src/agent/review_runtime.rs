// Deterministic runtime fixture for C ABI callback regressions.
// NOTE: this file is include!()d into the events.rs test module, which already
// imports Arc/Mutex; adding them here is a duplicate-import error.
use maho_agent::{AgentError, AgentRuntime, AgentStreamEvent, PermissionCallback, SecureKeyBundle};
use maho_types::chat::{ChatContent, ChatMessage};

#[derive(Default)]
struct ReviewRuntime {
    prompt: Mutex<String>,
    messages: Mutex<Vec<String>>,
    fail: bool,
}

#[async_trait::async_trait]
impl AgentRuntime for ReviewRuntime {
    async fn run_turn(
        &self,
        _: &str,
        message: ChatMessage,
        on_token: Option<Box<dyn for<'a> Fn(&'a str) + Send + Sync + 'static>>,
        on_event: Option<Arc<dyn Fn(AgentStreamEvent) + Send + Sync + 'static>>,
    ) -> Result<ChatMessage, AgentError> {
        if let ChatContent::Text(text) = message.content {
            self.messages.lock().unwrap().push(text);
        }
        if let Some(cb) = on_token {
            cb("fixture-token");
        }
        if let Some(cb) = on_event {
            cb(AgentStreamEvent::Thinking("fixture-thinking".into()));
        }
        if self.fail {
            return Err(AgentError::ExecutionError("fixture-error".into()));
        }
        Ok(ChatMessage::assistant(ChatContent::text(
            "fixture-response",
        )))
    }

    async fn cancel(&self, _: &str) -> Result<(), AgentError> {
        Ok(())
    }
    async fn list_tools(&self) -> Result<Vec<maho_agent::ToolDefinition>, AgentError> {
        Ok(vec![])
    }
    fn set_permission_callback(&self, _: PermissionCallback) -> Result<(), AgentError> {
        Ok(())
    }
    fn set_secure_storage_callback(
        &self,
        _: Box<dyn Fn(&str) -> Result<SecureKeyBundle, AgentError> + Send + Sync>,
    ) -> Result<(), AgentError> {
        Ok(())
    }
    fn run_journal(&self) -> Arc<Mutex<maho_agent::run_journal::RunJournal>> {
        Arc::new(Mutex::new(maho_agent::run_journal::RunJournal::default()))
    }
    fn set_system_prompt(&self, prompt: &str) {
        *self.prompt.lock().unwrap() = prompt.into();
    }
}
