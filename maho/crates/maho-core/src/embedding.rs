use crate::error::CoreError;

pub const EMBED_DIM: usize = 384;

pub fn embed(texts: &[String]) -> Result<Vec<Vec<f32>>, CoreError> {
    Ok(texts.iter().map(|_| vec![0.0f32; EMBED_DIM]).collect())
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
