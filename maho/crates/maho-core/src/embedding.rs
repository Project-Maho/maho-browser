use crate::error::CoreError;

pub const EMBED_DIM: usize = 384;

pub fn embed(texts: &[String]) -> Result<Vec<Vec<f32>>, CoreError> {
    Ok(texts.iter().map(|_| vec![0.0f32; EMBED_DIM]).collect())
}

pub fn bytes_to_vec(bytes: &[u8]) -> Result<Vec<f32>, CoreError> {
    if !bytes.len().is_multiple_of(4) {
        return Err(CoreError::Embedding("invalid byte length".into()));
    }
    let (chunks, _) = bytes.as_chunks::<4>();
    Ok(chunks.iter().map(|c| f32::from_le_bytes(*c)).collect())
}

pub fn vec_to_bytes(v: &[f32]) -> Vec<u8> {
    v.iter().flat_map(|f| f.to_le_bytes()).collect()
}
