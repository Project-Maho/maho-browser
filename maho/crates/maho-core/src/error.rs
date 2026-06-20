use std::fmt;

#[derive(Debug)]
pub enum CoreError {
    Embedding(String),
    Network(String),
    Parse(String),
    Storage(String),
}

impl fmt::Display for CoreError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Embedding(s) => write!(f, "Embedding error: {}", s),
            Self::Network(s) => write!(f, "Network error: {}", s),
            Self::Parse(s) => write!(f, "Parse error: {}", s),
            Self::Storage(s) => write!(f, "Storage error: {}", s),
        }
    }
}

impl std::error::Error for CoreError {}
