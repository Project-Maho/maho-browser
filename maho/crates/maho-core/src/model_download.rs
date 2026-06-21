use std::path::PathBuf;

#[derive(Debug, Clone, serde::Serialize, serde::Deserialize)]
pub struct ModelInfo {
    pub id: String,
    pub display_name: String,
    pub url: String,
    pub size_bytes: u64,
    pub sha256: String,
}

#[derive(Debug, Clone, Copy, serde::Serialize, serde::Deserialize, PartialEq)]
#[serde(rename_all = "snake_case")]
pub enum ModelDownloadState {
    NotStarted,
    Downloading,
    Verifying,
    Ready,
    Failed,
}

pub fn default_models_dir() -> PathBuf {
    std::env::var("MAHO_MODELS_DIR")
        .map(PathBuf::from)
        .unwrap_or_else(|_| PathBuf::from(".maho/models"))
}

pub fn gemma_3_1b_q4() -> ModelInfo {
    ModelInfo {
        id: "gemma-3-1b-q4_k_m".to_string(),
        display_name: "Gemma 3 1B (Offline, Q4)".to_string(),
        url: "https://huggingface.co/bartowski/google-gemma-3-1b-it-qat-GGUF/resolve/main/google-gemma-3-1b-it-qat-Q4_K_M.gguf".to_string(),
        size_bytes: 612_184_000,
        sha256: "".to_string(),
    }
}

pub fn model_path(model: &ModelInfo) -> PathBuf {
    default_models_dir().join(format!("{}.gguf", model.id))
}

pub fn is_downloaded(model: &ModelInfo) -> bool {
    let path = model_path(model);
    path.exists() && std::fs::metadata(&path).map(|metadata| metadata.len() > 0).unwrap_or(false)
}

pub fn delete_model(model: &ModelInfo) -> Result<(), std::io::Error> {
    let path = model_path(model);
    if path.exists() {
        std::fs::remove_file(path)?;
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_gemma_3_1b_info() {
        let model = gemma_3_1b_q4();
        assert!(model.display_name.contains("Gemma"));
        assert!(model.url.contains("gguf"));
    }

    #[test]
    fn test_default_models_dir_returns_path() {
        let dir = default_models_dir();
        assert!(dir.to_string_lossy().contains("models"));
    }
}
