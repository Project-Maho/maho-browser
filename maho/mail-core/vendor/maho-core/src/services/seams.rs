use crate::error::AppError;

#[derive(Debug, Clone, serde::Serialize, serde::Deserialize, PartialEq)]
pub enum MailEvent {
    NewMail {
        account_id: String,
        email_id: String,
    },
    SyncProgress {
        account_id: String,
        folder_id: String,
        progress: f64,
    },
    BackfillDone {
        account_id: String,
    },
    SendStatus {
        account_id: String,
        email_id: String,
        status: String,
    },
}

pub trait EventSink: Send + Sync {
    async fn emit(&self, event: MailEvent) -> Result<(), AppError>;
}

pub trait SecretStore: Send + Sync {
    fn get_password(&self, account_id: &str) -> Result<Option<String>, AppError>;
    fn set_password(&self, account_id: &str, password: &str) -> Result<(), AppError>;
    fn delete_password(&self, account_id: &str) -> Result<(), AppError>;
}

#[derive(Debug, Clone, serde::Serialize, serde::Deserialize)]
pub struct TranslationCapabilities {
    pub supported_source_languages: Vec<String>,
    pub supported_target_languages: Vec<String>,
    pub is_local: bool,
}

pub trait Translator: Send + Sync {
    fn get_capabilities(&self) -> TranslationCapabilities;
    async fn translate(
        &self,
        text: &str,
        source_lang: &str,
        target_lang: &str,
    ) -> Result<String, AppError>;
}

pub trait Lifecycle: Send + Sync {
    async fn start(&self) -> Result<(), AppError>;
    async fn graceful_flush(&self, timeout_secs: u64) -> Result<(), AppError>;
    async fn cancel(&self) -> Result<(), AppError>;
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::{Arc, Mutex};

    struct MockEventSink {
        events: Arc<Mutex<Vec<MailEvent>>>,
    }

    impl EventSink for MockEventSink {
        async fn emit(&self, event: MailEvent) -> Result<(), AppError> {
            self.events.lock().unwrap().push(event);
            Ok(())
        }
    }

    struct MockSecretStore {
        secrets: Arc<Mutex<std::collections::HashMap<String, String>>>,
    }

    impl SecretStore for MockSecretStore {
        fn get_password(&self, account_id: &str) -> Result<Option<String>, AppError> {
            Ok(self.secrets.lock().unwrap().get(account_id).cloned())
        }
        fn set_password(&self, account_id: &str, password: &str) -> Result<(), AppError> {
            self.secrets
                .lock()
                .unwrap()
                .insert(account_id.to_string(), password.to_string());
            Ok(())
        }
        fn delete_password(&self, account_id: &str) -> Result<(), AppError> {
            self.secrets.lock().unwrap().remove(account_id);
            Ok(())
        }
    }

    struct MockTranslator;

    impl Translator for MockTranslator {
        fn get_capabilities(&self) -> TranslationCapabilities {
            TranslationCapabilities {
                supported_source_languages: vec!["en".to_string()],
                supported_target_languages: vec!["ko".to_string()],
                is_local: true,
            }
        }
        async fn translate(
            &self,
            text: &str,
            _source_lang: &str,
            _target_lang: &str,
        ) -> Result<String, AppError> {
            Ok(format!("{}_translated", text))
        }
    }

    #[tokio::test]
    async fn test_mock_event_sink() {
        let events = Arc::new(Mutex::new(Vec::new()));
        let sink = MockEventSink {
            events: events.clone(),
        };
        let event = MailEvent::NewMail {
            account_id: "acc1".to_string(),
            email_id: "em1".to_string(),
        };
        sink.emit(event.clone()).await.unwrap();
        assert_eq!(events.lock().unwrap()[0], event);
    }

    #[test]
    fn test_mock_secret_store() {
        let secrets = Arc::new(Mutex::new(std::collections::HashMap::new()));
        let store = MockSecretStore { secrets };
        store.set_password("acc1", "secret123").unwrap();
        assert_eq!(
            store.get_password("acc1").unwrap(),
            Some("secret123".to_string())
        );
        store.delete_password("acc1").unwrap();
        assert_eq!(store.get_password("acc1").unwrap(), None);
    }

    #[tokio::test]
    async fn test_mock_translator() {
        let translator = MockTranslator;
        let caps = translator.get_capabilities();
        assert_eq!(caps.supported_source_languages[0], "en");
        assert_eq!(caps.supported_target_languages[0], "ko");
        assert!(caps.is_local);

        let res = translator.translate("hello", "en", "ko").await.unwrap();
        assert_eq!(res, "hello_translated");
    }
}
