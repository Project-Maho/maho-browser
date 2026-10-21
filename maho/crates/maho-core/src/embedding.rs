use std::path::Path;
use std::sync::{Arc, RwLock};

use candle_core::{DType, Device, Tensor};
use candle_nn::VarBuilder;
use candle_transformers::models::bert::{BertModel, Config};
use tokenizers::{PaddingParams, PaddingStrategy, Tokenizer, TruncationParams};

use crate::error::CoreError;

pub const EMBED_DIM: usize = 384;
pub const EMBEDDING_MODEL_ID: &str = "intfloat/multilingual-e5-small";
pub const EMBEDDING_MODEL_REVISION: &str = "614241f622f53c4eeff9890bdc4f31cfecc418b3";
const MAX_TOKENS: usize = 512;
// e5 models are trained with a role prefix; "query: " is the documented
// choice when both sides of the comparison are the same kind of text.
const E5_PREFIX: &str = "query: ";

pub struct LocalEmbedder {
    model: BertModel,
    tokenizer: Tokenizer,
}

impl LocalEmbedder {
    pub fn load(dir: &Path) -> Result<Self, CoreError> {
        let unavailable = |what: &str, e: &dyn std::fmt::Display| {
            CoreError::EmbeddingUnavailable(format!("{what}: {e}"))
        };
        let config_text = std::fs::read_to_string(dir.join("config.json"))
            .map_err(|e| unavailable("read config.json", &e))?;
        let config: Config =
            serde_json::from_str(&config_text).map_err(|e| unavailable("parse config.json", &e))?;
        if config.hidden_size != EMBED_DIM {
            return Err(CoreError::EmbeddingUnavailable(format!(
                "model hidden size {} does not match {EMBED_DIM}",
                config.hidden_size
            )));
        }
        let mut tokenizer = Tokenizer::from_file(dir.join("tokenizer.json"))
            .map_err(|e| unavailable("load tokenizer.json", &e))?;
        tokenizer
            .with_truncation(Some(TruncationParams {
                max_length: MAX_TOKENS,
                ..Default::default()
            }))
            .map_err(|e| unavailable("configure truncation", &e))?;
        tokenizer.with_padding(Some(PaddingParams {
            strategy: PaddingStrategy::BatchLongest,
            ..Default::default()
        }));
        let weights = std::fs::read(dir.join("model.safetensors"))
            .map_err(|e| unavailable("read model.safetensors", &e))?;
        let vb = VarBuilder::from_buffered_safetensors(weights, DType::F32, &Device::Cpu)
            .map_err(|e| unavailable("map weights", &e))?;
        let model = BertModel::load(vb, &config).map_err(|e| unavailable("load model", &e))?;
        Ok(Self { model, tokenizer })
    }

    pub fn embed(&self, texts: &[String]) -> Result<Vec<Vec<f32>>, CoreError> {
        if texts.is_empty() {
            return Ok(Vec::new());
        }
        let failed = |e: &dyn std::fmt::Display| CoreError::Embedding(e.to_string());
        let inputs: Vec<String> = texts.iter().map(|t| format!("{E5_PREFIX}{t}")).collect();
        let encodings = self
            .tokenizer
            .encode_batch(inputs, true)
            .map_err(|e| failed(&e))?;
        let rows = encodings.len();
        let width = encodings[0].get_ids().len();
        let mut ids = Vec::with_capacity(rows * width);
        let mut mask = Vec::with_capacity(rows * width);
        for encoding in &encodings {
            ids.extend(encoding.get_ids().iter().map(|&v| v as i64));
            mask.extend(encoding.get_attention_mask().iter().map(|&v| v as i64));
        }
        let device = Device::Cpu;
        let ids = Tensor::from_vec(ids, (rows, width), &device).map_err(|e| failed(&e))?;
        let mask = Tensor::from_vec(mask, (rows, width), &device).map_err(|e| failed(&e))?;
        let token_types = ids.zeros_like().map_err(|e| failed(&e))?;
        let hidden = self
            .model
            .forward(&ids, &token_types, Some(&mask))
            .map_err(|e| failed(&e))?;
        let pooled = (|| -> candle_core::Result<Tensor> {
            let mask = mask.to_dtype(DType::F32)?.unsqueeze(2)?;
            let summed = hidden.broadcast_mul(&mask)?.sum(1)?;
            let counts = mask.sum(1)?;
            let mean = summed.broadcast_div(&counts)?;
            let norm = mean.sqr()?.sum_keepdim(1)?.sqrt()?;
            mean.broadcast_div(&norm)
        })()
        .map_err(|e| failed(&e))?;
        pooled.to_vec2::<f32>().map_err(|e| failed(&e))
    }
}

#[derive(Default)]
pub struct EmbeddingRegistry {
    model: RwLock<Option<Arc<LocalEmbedder>>>,
}

impl EmbeddingRegistry {
    pub fn install(&self, embedder: LocalEmbedder) {
        *self.model.write().unwrap_or_else(|e| e.into_inner()) = Some(Arc::new(embedder));
    }

    pub fn is_available(&self) -> bool {
        self.model
            .read()
            .unwrap_or_else(|e| e.into_inner())
            .is_some()
    }

    pub fn embed(&self, texts: &[String]) -> Result<Vec<Vec<f32>>, CoreError> {
        let model = self
            .model
            .read()
            .unwrap_or_else(|e| e.into_inner())
            .clone()
            .ok_or_else(|| {
                CoreError::EmbeddingUnavailable("no local embedding model is installed".into())
            })?;
        model.embed(texts)
    }
}

pub fn registry() -> &'static EmbeddingRegistry {
    static REGISTRY: std::sync::OnceLock<EmbeddingRegistry> = std::sync::OnceLock::new();
    REGISTRY.get_or_init(EmbeddingRegistry::default)
}

pub fn embed(texts: &[String]) -> Result<Vec<Vec<f32>>, CoreError> {
    registry().embed(texts)
}

pub fn bytes_to_vec(bytes: &[u8]) -> Result<Vec<f32>, CoreError> {
    if bytes.len() % 4 != 0 {
        return Err(CoreError::Embedding("invalid byte length".into()));
    }
    Ok(bytes
        .chunks_exact(4)
        .map(|c| f32::from_le_bytes([c[0], c[1], c[2], c[3]]))
        .collect())
}

pub fn vec_to_bytes(v: &[f32]) -> Vec<u8> {
    v.iter().flat_map(|f| f.to_le_bytes()).collect()
}
