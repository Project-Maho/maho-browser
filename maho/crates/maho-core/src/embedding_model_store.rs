//! Local embedding model fetch, sha256 verification and profile cache.

use std::path::{Path, PathBuf};

use futures_util::StreamExt;
use sha2::{Digest, Sha256};
use std::io::Write;

use crate::error::CoreError;

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ModelFile {
    pub name: &'static str,
    pub size: u64,
    pub sha256: &'static str,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ModelManifest {
    pub dir_name: &'static str,
    pub base_url: String,
    pub files: Vec<ModelFile>,
}

const E5_SMALL_BASE_URL: &str = "https://huggingface.co/intfloat/multilingual-e5-small/resolve/614241f622f53c4eeff9890bdc4f31cfecc418b3";

pub fn multilingual_e5_small() -> ModelManifest {
    ModelManifest {
        dir_name: "multilingual-e5-small-614241f6",
        base_url: E5_SMALL_BASE_URL.to_string(),
        files: vec![
            ModelFile {
                name: "config.json",
                size: 655,
                sha256: "69137736cab8b8903a07fe8afaafdda25aac55415a12a55d1bffa9f581abf959",
            },
            ModelFile {
                name: "tokenizer.json",
                size: 17_082_730,
                sha256: "0b44a9d7b51c3c62626640cda0e2c2f70fdacdc25bbbd68038369d14ebdf4c39",
            },
            ModelFile {
                name: "model.safetensors",
                size: 470_641_600,
                sha256: "1a55775f53449dac10a2bcbc312469fac40b96d53198c407081a831f81c98477",
            },
        ],
    }
}

pub fn model_dir(profile_dir: &Path, manifest: &ModelManifest) -> PathBuf {
    profile_dir.join("models").join(manifest.dir_name)
}

fn file_matches(path: &Path, file: &ModelFile) -> bool {
    let Ok(meta) = std::fs::metadata(path) else {
        return false;
    };
    if meta.len() != file.size {
        return false;
    }
    let Ok(mut reader) = std::fs::File::open(path) else {
        return false;
    };
    let mut hasher = Sha256::new();
    if std::io::copy(&mut reader, &mut hasher).is_err() {
        return false;
    }
    hex(&hasher.finalize()) == file.sha256
}

fn hex(bytes: &[u8]) -> String {
    bytes.iter().map(|b| format!("{b:02x}")).collect()
}

pub fn verified_dir(profile_dir: &Path, manifest: &ModelManifest) -> Option<PathBuf> {
    let dir = model_dir(profile_dir, manifest);
    manifest
        .files
        .iter()
        .all(|file| file_matches(&dir.join(file.name), file))
        .then_some(dir)
}

async fn download_once(
    client: &reqwest::Client,
    url: &str,
    file: &ModelFile,
    target: &Path,
) -> Result<(), CoreError> {
    let partial = target.with_extension("part");
    let result = async {
        let response = client
            .get(url)
            .send()
            .await
            .and_then(|r| r.error_for_status())
            .map_err(|e| CoreError::EmbeddingUnavailable(format!("fetch {}: {e}", file.name)))?;
        let mut out = std::fs::File::create(&partial)
            .map_err(|e| CoreError::Storage(format!("create {}: {e}", partial.display())))?;
        let mut hasher = Sha256::new();
        let mut written: u64 = 0;
        let mut body = response.bytes_stream();
        while let Some(chunk) = body.next().await {
            let chunk = chunk.map_err(|e| {
                CoreError::EmbeddingUnavailable(format!("fetch {}: {e}", file.name))
            })?;
            written += chunk.len() as u64;
            if written > file.size {
                return Err(CoreError::Embedding(format!(
                    "{} is larger than the pinned {} bytes",
                    file.name, file.size
                )));
            }
            hasher.update(&chunk);
            out.write_all(&chunk)
                .map_err(|e| CoreError::Storage(format!("write {}: {e}", file.name)))?;
        }
        out.sync_all()
            .map_err(|e| CoreError::Storage(format!("sync {}: {e}", file.name)))?;
        drop(out);
        let digest = hex(&hasher.finalize());
        if written != file.size || digest != file.sha256 {
            return Err(CoreError::Embedding(format!(
                "{} failed verification (size {written}, sha256 {digest})",
                file.name
            )));
        }
        std::fs::rename(&partial, target)
            .map_err(|e| CoreError::Storage(format!("install {}: {e}", file.name)))
    }
    .await;
    if result.is_err() {
        let _ = std::fs::remove_file(&partial);
    }
    result
}

/// Ensures every manifest file is cached and verified under the profile,
/// downloading what is missing or corrupt. A file that fails verification is
/// deleted and fetched once more; a second failure is returned.
pub async fn ensure_model(
    client: &reqwest::Client,
    profile_dir: &Path,
    manifest: &ModelManifest,
) -> Result<PathBuf, CoreError> {
    let dir = model_dir(profile_dir, manifest);
    std::fs::create_dir_all(&dir)
        .map_err(|e| CoreError::Storage(format!("create {}: {e}", dir.display())))?;
    for file in &manifest.files {
        let target = dir.join(file.name);
        if file_matches(&target, file) {
            continue;
        }
        let _ = std::fs::remove_file(&target);
        let url = format!("{}/{}", manifest.base_url, file.name);
        if let Err(first) = download_once(client, &url, file, &target).await {
            if matches!(first, CoreError::EmbeddingUnavailable(_)) {
                return Err(first);
            }
            download_once(client, &url, file, &target).await?;
        }
    }
    Ok(dir)
}

pub async fn ensure_and_install_default(profile_dir: &Path) -> Result<PathBuf, CoreError> {
    let manifest = multilingual_e5_small();
    let dir = match verified_dir(profile_dir, &manifest) {
        Some(dir) => dir,
        None => ensure_model(&crate::http::shared_http_client(), profile_dir, &manifest).await?,
    };
    let embedder = crate::embedding::LocalEmbedder::load(&dir)?;
    crate::embedding::registry().install(embedder);
    Ok(dir)
}
